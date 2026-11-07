// Copyright Epic Games, Inc. All Rights Reserved.

#include "CrowdStatsComponent.h"
#include "CrowdUnitComponent.h"
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

	if (bShowDebugStatsHUD && GetWorld() && GetWorld()->IsGameWorld())
	{
		CreateDebugStatsWidget();
	}
}

void UCrowdStatsComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	RemoveDebugStatsWidget();
	Super::EndPlay(EndPlayReason);
}

void UCrowdStatsComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (GetWorld() && GetWorld()->IsGameWorld())
	{
		if (bShowDebugStatsHUD && !DebugStatsWidget.IsValid())
		{
			CreateDebugStatsWidget();
		}
		else if (!bShowDebugStatsHUD && DebugStatsWidget.IsValid())
		{
			RemoveDebugStatsWidget();
		}
	}
}

UCrowdUnitComponent* UCrowdStatsComponent::GetValidCrowdComponent() const
{
	if (CrowdUnitComponent.IsValid())
	{
		return CrowdUnitComponent.Get();
	}

	AActor* Owner = GetOwner();
	if (Owner)
	{
		return Owner->FindComponentByClass<UCrowdUnitComponent>();
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
	const UCrowdUnitComponent* Comp = GetValidCrowdComponent();
	const int32 TotalUnits = Comp ? Comp->GetCellCount() : 0;
	return TotalUnits * GetBonesPerUnit();
}

int32 UCrowdStatsComponent::GetCurrentClipFrameCount() const
{
	const UCrowdUnitComponent* Comp = GetValidCrowdComponent();
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
		return Anims[0].NumFrames;
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

	return Anims.IsValidIndex(MostPopularAnim) ? Anims[MostPopularAnim].NumFrames : 60;
}

int32 UCrowdStatsComponent::GetAliveUnitCount() const
{
	const UCrowdUnitComponent* Comp = GetValidCrowdComponent();
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
	const UCrowdUnitComponent* Comp = GetValidCrowdComponent();
	const int32 Total = Comp ? Comp->GetCellCount() : 0;
	return Total - GetAliveUnitCount();
}

int32 UCrowdStatsComponent::CalculateInstanceCurrentFrame(const FCrowdCellInstanceData& D, float CurrentTime) const
{
	const UCrowdUnitComponent* Comp = GetValidCrowdComponent();
	float StartFrame = 0.0f;
	float NumFrames = 60.0f;

	if (Comp)
	{
		const TArray<FUnitAnimationTableRow>& Anims = Comp->GetRegisteredAnimations();
		if (Anims.IsValidIndex(D.AnimIndex))
		{
			StartFrame = static_cast<float>(Anims[D.AnimIndex].StartFrame);
			NumFrames  = static_cast<float>(FMath::Max(1, Anims[D.AnimIndex].NumFrames));
		}
	}

	float FrameOffset = 0.0f;
	if (D.bLoop)
	{
		const float LoopSec = FMath::Max(0.0f, (CurrentTime + D.Phase) * FMath::Abs(D.PlayRate));
		FrameOffset = FMath::FloorToFloat(FMath::Fmod(LoopSec * 30.0f, NumFrames));
	}
	else
	{
		const float ElapsedSec = FMath::Max(0.0f, (CurrentTime - D.Phase) * FMath::Abs(D.PlayRate));
		FrameOffset = FMath::Min(NumFrames - 1.0f, FMath::FloorToFloat(ElapsedSec * 30.0f));
	}

	return FMath::RoundToInt(StartFrame + FrameOffset);
}

int32 UCrowdStatsComponent::GetActivePoseCount() const
{
	const UCrowdUnitComponent* Comp = GetValidCrowdComponent();
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
	const UCrowdUnitComponent* Comp = GetValidCrowdComponent();
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
