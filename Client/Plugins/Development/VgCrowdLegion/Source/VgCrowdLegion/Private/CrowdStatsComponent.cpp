// Copyright Epic Games, Inc. All Rights Reserved.

#include "CrowdStatsComponent.h"
#include "UnitHISMComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Misc/App.h"
#include "GameTime.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Framework/Application/SlateApplication.h"
#include "Engine/GameViewportClient.h"
#include "Engine/Engine.h"
#include "Styling/CoreStyle.h"
#include "UnrealClient.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Misc/DateTime.h"
#include "UObject/UnrealType.h"
#include "GameFramework/PlayerController.h"

/** 获取与材质渲染器 View.RealTime 严格一致的高精度当前时间戳 */
static float GetStatsCurrentAnimationTime(const UWorld* World)
{
	if (World)
	{
		return static_cast<float>(World->GetRealTimeSeconds());
	}
	return static_cast<float>(FGameTime::GetTimeSinceAppStart().GetRealTimeSeconds());
}

UCrowdStatsComponent::UCrowdStatsComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
}

void UCrowdStatsComponent::BeginPlay()
{
	Super::BeginPlay();

	if (GetWorld() && GetWorld()->IsGameWorld())
	{
		if (bShowDebugStatsHUD)
		{
			CreateDebugStatsWidget();
		}
		OpenPerfCsvIfNeeded();
	}
}

void UCrowdStatsComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ClosePerfCsv();
	RemoveDebugStatsWidget();
	Super::EndPlay(EndPlayReason);
}

void UCrowdStatsComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	UWorld* World = GetWorld();
	if (World && World->IsGameWorld())
	{
		if (bShowDebugStatsHUD && !DebugStatsWidget.IsValid())
		{
			CreateDebugStatsWidget();
		}
		else if (!bShowDebugStatsHUD && DebugStatsWidget.IsValid())
		{
			RemoveDebugStatsWidget();
		}

		if (bEnablePerfCsv && bSegmentActive && !PerfCsvAbsolutePath.IsEmpty())
		{
			UGameViewportClient* Viewport = World->GetGameViewport();
			FStatUnitData* StatUnit = Viewport ? Viewport->GetStatUnitData() : nullptr;
			if (StatUnit)
			{
				SumFrameMs += StatUnit->FrameTime;
				SumGameMs += StatUnit->GameThreadTime;
				SumDrawMs += StatUnit->RenderThreadTime;
				SumGpuMs += StatUnit->GPUFrameTime[0];
				SumRhitMs += StatUnit->RHITTime;
				++SegmentSampleCount;
			}
		}
	}
}

UUnitHISMComponent* UCrowdStatsComponent::GetValidUnitComponent() const
{
	if (UnitComponent.IsValid())
	{
		return UnitComponent.Get();
	}

	AActor* Owner = GetOwner();
	if (Owner)
	{
		return Owner->FindComponentByClass<UUnitHISMComponent>();
	}
	return nullptr;
}

int32 UCrowdStatsComponent::GetBonesPerUnit() const
{
	// 军团士兵平均骨骼基准 (标准人形骨骼为 68 根)
	return 68;
}

int32 UCrowdStatsComponent::GetTotalEquivalentBones() const
{
	const UUnitHISMComponent* Comp = GetValidUnitComponent();
	const int32 TotalUnits = Comp ? Comp->GetCellCount() : 0;
	return TotalUnits * GetBonesPerUnit();
}

int32 UCrowdStatsComponent::GetCurrentClipFrameCount() const
{
	const UUnitHISMComponent* Comp = GetValidUnitComponent();
	if (!Comp)
	{
		return 60;
	}

	const TArray<FUnitAnimationTableRow>& Anims = Comp->GetRegisteredAnimations();
	if (Anims.Num() == 0)
	{
		return 60;
	}

	// 统计数量最多的动作对应的总帧数
	const TArray<FCrowdCellInstanceData>& Instances = Comp->GetAllInstanceData();
	if (Instances.Num() == 0)
	{
		return FMath::Max(1, Anims[0].EndFrame - Anims[0].StartFrame + 1);
	}

	TMap<int32, int32> AnimUsage;
	for (const FCrowdCellInstanceData& D : Instances)
	{
		AnimUsage.FindOrAdd(D.AnimIndex)++;
	}

	int32 MostPopularAnim = 0;
	int32 MaxCount = -1;
	for (const auto& Pair : AnimUsage)
	{
		if (Pair.Value > MaxCount)
		{
			MaxCount = Pair.Value;
			MostPopularAnim = Pair.Key;
		}
	}

	if (!Anims.IsValidIndex(MostPopularAnim))
	{
		return 60;
	}
	const FUnitAnimationTableRow& Row = Anims[MostPopularAnim];
	return FMath::Max(1, Row.EndFrame - Row.StartFrame + 1);
}

int32 UCrowdStatsComponent::GetAliveUnitCount() const
{
	const UUnitHISMComponent* Comp = GetValidUnitComponent();
	if (!Comp)
	{
		return 0;
	}

	const TArray<FUnitAnimationTableRow>& Anims = Comp->GetRegisteredAnimations();
	const TArray<FCrowdCellInstanceData>& Instances = Comp->GetAllInstanceData();

	// 找出所有死亡动作索引
	TSet<int32> DeathIndices;
	for (int32 i = 0; i < Anims.Num(); ++i)
	{
		const FString Name = Anims[i].AnimName.ToString().ToLower();
		if (Name.Contains(TEXT("death")) || Name.Contains(TEXT("die")))
		{
			DeathIndices.Add(i);
		}
	}

	int32 Alive = 0;
	for (const FCrowdCellInstanceData& D : Instances)
	{
		if (!DeathIndices.Contains(D.AnimIndex))
		{
			Alive++;
		}
	}
	return Alive;
}

int32 UCrowdStatsComponent::GetDeadUnitCount() const
{
	const UUnitHISMComponent* Comp = GetValidUnitComponent();
	const int32 Total = Comp ? Comp->GetCellCount() : 0;
	return Total - GetAliveUnitCount();
}

int32 UCrowdStatsComponent::CalculateInstanceCurrentFrame(const FCrowdCellInstanceData& D, float CurrentTime) const
{
	const UUnitHISMComponent* Comp = GetValidUnitComponent();
	float StartFrame = 0.f;
	float EndFrame = 0.f;

	if (Comp)
	{
		const TArray<FUnitAnimationTableRow>& Anims = Comp->GetRegisteredAnimations();
		if (Anims.IsValidIndex(D.AnimIndex))
		{
			StartFrame = static_cast<float>(Anims[D.AnimIndex].StartFrame);
			EndFrame = static_cast<float>(Anims[D.AnimIndex].EndFrame);
			if (EndFrame < StartFrame)
			{
				EndFrame = StartFrame;
			}
		}
	}

	const float Elapsed = FMath::Max(0.f, (CurrentTime + D.Phase) * FMath::Abs(D.PlayRate));
	const float Length = FMath::Max(1.f, EndFrame - StartFrame + 1.f);
	const float FrameOffset = D.bLoop
		? FMath::FloorToFloat(FMath::Fmod(Elapsed * 30.f, Length))
		: FMath::Min(Length - 1.f, FMath::FloorToFloat(Elapsed * 30.f));

	return FMath::RoundToInt(StartFrame + FrameOffset);
}

int32 UCrowdStatsComponent::GetActivePoseCount() const
{
	const UUnitHISMComponent* Comp = GetValidUnitComponent();
	if (!Comp)
	{
		return 0;
	}

	const TArray<FCrowdCellInstanceData>& Instances = Comp->GetAllInstanceData();
	if (Instances.Num() == 0)
	{
		return 0;
	}

	TSet<int32> UniqueSampledFrames;
	const float CurrentTime = GetStatsCurrentAnimationTime(GetWorld());

	for (const FCrowdCellInstanceData& D : Instances)
	{
		const int32 SampledFrame = CalculateInstanceCurrentFrame(D, CurrentTime);
		UniqueSampledFrames.Add(SampledFrame);
	}

	return UniqueSampledFrames.Num();
}

FText UCrowdStatsComponent::GetFormattedStatsText() const
{
	const UUnitHISMComponent* Comp = GetValidUnitComponent();
	const int32 TotalUnits = Comp ? Comp->GetCellCount() : 0;
	const int32 AliveUnits = GetAliveUnitCount();
	const int32 DeadUnits  = TotalUnits - AliveUnits;
	const int32 BonesPerUnit = GetBonesPerUnit();
	const int32 TotalEquivalentBones = GetTotalEquivalentBones();
	const int32 ActivePoseCount = GetActivePoseCount();
	const int32 ClipFrameCount = GetCurrentClipFrameCount();
	const float DeltaSeconds = FApp::GetDeltaTime();
	const float FPS = DeltaSeconds > 0.0001f ? (1.0f / DeltaSeconds) : 0.0f;

	FString StateDetails;
	if (Comp)
	{
		const TArray<FUnitAnimationTableRow>& Anims = Comp->GetRegisteredAnimations();
		const TArray<FCrowdCellInstanceData>& Instances = Comp->GetAllInstanceData();

		TMap<int32, int32> StateCounts;
		for (const FCrowdCellInstanceData& D : Instances)
		{
			StateCounts.FindOrAdd(D.AnimIndex)++;
		}

		for (const auto& Pair : StateCounts)
		{
			const FString AnimName = Anims.IsValidIndex(Pair.Key) ? Anims[Pair.Key].AnimName.ToString() : FString::Printf(TEXT("Anim_%d"), Pair.Key);
			StateDetails += FString::Printf(TEXT("  - %s: %d\n"), *AnimName, Pair.Value);
		}
	}

	const FString StatsString = FString::Printf(
		TEXT("═════════════════════════════════════════════════\n")
		TEXT("   【海量军团 A2T 顶点动画实时性能统计】\n")
		TEXT("═════════════════════════════════════════════════\n")
		TEXT("• 当前帧率 (FPS)         : %.1f\n")
		TEXT("• 同屏单位总数 (Units)   : %d\n")
		TEXT("  - 存活士兵 (Alive)     : %d\n")
		TEXT("  - 阵亡士兵 (Dead)      : %d\n")
		TEXT("• 单人等效骨骼数         : %d 根\n")
		TEXT("• 同屏等效骨骼总数       : %d 根 (零 CPU 蒙皮开销)\n")
		TEXT("• 同屏独立姿态去重数     : %d 个不同 Pose (姿态库上限 %d 帧)\n")
		TEXT("• 姿态采样覆盖率         : %.1f%%\n")
		TEXT("• 动作状态分布:\n%s")
		TEXT("═════════════════════════════════════════════════"),
		FPS,
		TotalUnits,
		AliveUnits,
		DeadUnits,
		BonesPerUnit,
		TotalEquivalentBones,
		ActivePoseCount,
		ClipFrameCount,
		ClipFrameCount > 0 ? (static_cast<float>(ActivePoseCount) / static_cast<float>(ClipFrameCount) * 100.0f) : 0.0f,
		*StateDetails
	);

	return FText::FromString(StatsString);
}

void UCrowdStatsComponent::CreateDebugStatsWidget()
{
	if (DebugStatsWidget.IsValid() || !GEngine || !GEngine->GameViewport)
	{
		return;
	}

	TWeakObjectPtr<UCrowdStatsComponent> WeakThis(this);

	SAssignNew(DebugStatsWidget, SBorder)
		.Padding(FMargin(12.0f, 10.0f))
		.BorderBackgroundColor(FLinearColor(0.02f, 0.02f, 0.04f, 0.85f))
		.HAlign(HAlign_Right)
		.VAlign(VAlign_Top)
		[
			SNew(SBox)
			[
				SNew(STextBlock)
				.Font_Lambda([WeakThis]() -> FSlateFontInfo
				{
					const int32 FontSize = WeakThis.IsValid() ? WeakThis->DebugStatsFontSize : 16;
					return FCoreStyle::GetDefaultFontStyle("Bold", FontSize);
				})
				.ColorAndOpacity(FSlateColor(FLinearColor(0.2f, 1.0f, 0.4f, 1.0f)))
				.Text_Lambda([WeakThis]() -> FText
				{
					if (WeakThis.IsValid())
					{
						return WeakThis->GetFormattedStatsText();
					}
					return FText::GetEmpty();
				})
			]
		];

	GEngine->GameViewport->AddViewportWidgetContent(DebugStatsWidget.ToSharedRef(), 100);
}

void UCrowdStatsComponent::RemoveDebugStatsWidget()
{
	if (DebugStatsWidget.IsValid())
	{
		if (GEngine && GEngine->GameViewport)
		{
			GEngine->GameViewport->RemoveViewportWidgetContent(DebugStatsWidget.ToSharedRef());
		}
		DebugStatsWidget.Reset();
	}
}

FString UCrowdStatsComponent::BuildPerfCsvPath() const
{
	const FString Dir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("CrowdLegion"));
	IFileManager::Get().MakeDirectory(*Dir, true);
	const FString Stamp = FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S"));
	return FPaths::Combine(Dir, FString::Printf(TEXT("PerfStats_%s.csv"), *Stamp));
}

float UCrowdStatsComponent::ResolveSpreadRadius() const
{
	const AActor* Owner = GetOwner();
	if (!Owner)
	{
		return -1.0f;
	}
	const FFloatProperty* FloatProp = FindFProperty<FFloatProperty>(Owner->GetClass(), TEXT("SpreadRadius"));
	if (!FloatProp)
	{
		return -1.0f;
	}
	return FloatProp->GetPropertyValue_InContainer(Owner);
}

void UCrowdStatsComponent::OpenPerfCsvIfNeeded()
{
	if (!bEnablePerfCsv || !PerfCsvAbsolutePath.IsEmpty())
	{
		return;
	}

	PerfCsvAbsolutePath = BuildPerfCsvPath();
	const FString Header = TEXT("Anim,Radius,Units,Poses,Bones,Frame_ms,Game_ms,Draw_ms,GPU_ms,RHIT_ms\n");
	if (!FFileHelper::SaveStringToFile(Header, *PerfCsvAbsolutePath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		UE_LOG(LogTemp, Warning, TEXT("[CrowdStats] Failed to create CSV: %s"), *PerfCsvAbsolutePath);
		PerfCsvAbsolutePath.Reset();
		return;
	}
	UE_LOG(LogTemp, Log, TEXT("[CrowdStats] Perf CSV: %s"), *PerfCsvAbsolutePath);

#if !UE_BUILD_SHIPPING
	if (UWorld* World = GetWorld())
	{
		if (World->IsGameWorld())
		{
			if (APlayerController* PC = World->GetFirstPlayerController())
			{
				PC->ConsoleCommand(TEXT("stat unit"));
			}
		}
	}
#endif
}

void UCrowdStatsComponent::ClosePerfCsv()
{
	FlushCurrentSegment();
	bSegmentActive = false;
	PerfCsvAbsolutePath.Reset();
}

void UCrowdStatsComponent::StartSegment(FName AnimName)
{
	SegmentAnimName = AnimName;
	SegmentSampleCount = 0;
	SumFrameMs = SumGameMs = SumDrawMs = SumGpuMs = SumRhitMs = 0.0;
	bSegmentActive = true;
}

void UCrowdStatsComponent::FlushCurrentSegment()
{
	if (!bEnablePerfCsv || !bSegmentActive || PerfCsvAbsolutePath.IsEmpty())
	{
		return;
	}
	if (SegmentSampleCount <= 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("[CrowdStats] Skip CSV row for '%s': no Stat Unit samples."),
			*SegmentAnimName.ToString());
		return;
	}

	const double Inv = 1.0 / static_cast<double>(SegmentSampleCount);
	const UUnitHISMComponent* Comp = GetValidUnitComponent();
	const int32 Units = Comp ? Comp->GetCellCount() : 0;
	const int32 Poses = GetActivePoseCount();
	const int32 Bones = GetTotalEquivalentBones();
	const float Radius = ResolveSpreadRadius();

	const FString Line = FString::Printf(
		TEXT("%s,%.0f,%d,%d,%d,%.3f,%.3f,%.3f,%.3f,%.3f\n"),
		*SegmentAnimName.ToString(),
		Radius,
		Units,
		Poses,
		Bones,
		SumFrameMs * Inv,
		SumGameMs * Inv,
		SumDrawMs * Inv,
		SumGpuMs * Inv,
		SumRhitMs * Inv);

	if (!FFileHelper::SaveStringToFile(
		Line,
		*PerfCsvAbsolutePath,
		FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM,
		&IFileManager::Get(),
		EFileWrite::FILEWRITE_Append))
	{
		UE_LOG(LogTemp, Warning, TEXT("[CrowdStats] Failed to append CSV row."));
	}
}

void UCrowdStatsComponent::NotifyAnimSegment(FName AnimName)
{
	if (!bEnablePerfCsv)
	{
		return;
	}

	OpenPerfCsvIfNeeded();
	if (PerfCsvAbsolutePath.IsEmpty())
	{
		return;
	}

	if (bSegmentActive)
	{
		FlushCurrentSegment();
	}
	StartSegment(AnimName);
}
