// Copyright Epic Games, Inc. All Rights Reserved.

#include "CrowdLegionActor.h"
#include "CrowdUnitComponent.h"
#include "CavalryUnitComponent.h"
#include "UnitHISMComponent.h"
#include "CrowdStatsComponent.h"
#include "CrowdLegionExporter.h"
#include "Components/SceneComponent.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "AnimToTextureDataAsset.h"
#include "Engine/DataTable.h"
#include "Misc/MessageDialog.h"
#include "Misc/PackageName.h"

namespace CrowdLegionActor_Private
{
	static UDataTable* MakeLayerTable(
		UObject* Outer,
		UStaticMesh* Mesh0,
		UDataTable* Anim0,
		UStaticMesh* Mesh1 = nullptr,
		UDataTable* Anim1 = nullptr)
	{
		TArray<FUnitHISMLayerTableRow> Rows;
		if (Mesh0)
		{
			FUnitHISMLayerTableRow Row;
			Row.StaticMesh = Mesh0;
			Row.AnimMetaTable = Anim0;
			Rows.Add(Row);
		}
		if (Mesh1)
		{
			FUnitHISMLayerTableRow Row;
			Row.StaticMesh = Mesh1;
			Row.AnimMetaTable = Anim1;
			Rows.Add(Row);
		}
		return UUnitHISMComponent::CreateTransientLayerTable(Outer, Rows);
	}

	static UDataTable* TryLoadAnimMetaBesideDataAsset(UAnimToTextureDataAsset* DataAsset)
	{
		if (!DataAsset)
		{
			return nullptr;
		}
		const FString BaseDir = FPackageName::GetLongPackagePath(DataAsset->GetOutermost()->GetName());
		const FString SkelName = DataAsset->GetSkeletalMesh()
			? DataAsset->GetSkeletalMesh()->GetName()
			: DataAsset->GetName();
		const FString TablePkgPath = BaseDir / (TEXT("DT_") + SkelName + TEXT("_AnimMeta"));
		const FString ObjectPath = TablePkgPath + TEXT(".") + FPackageName::GetShortName(TablePkgPath);
		return Cast<UDataTable>(StaticLoadObject(UDataTable::StaticClass(), nullptr, *ObjectPath));
	}

	static UStaticMesh* ResolveMeshFromDataAsset(UAnimToTextureDataAsset* DataAsset)
	{
		if (!DataAsset)
		{
			return nullptr;
		}
		UStaticMesh* Mesh = DataAsset->GetStaticMesh();
		if (!Mesh && !DataAsset->StaticMesh.IsNull())
		{
			Mesh = DataAsset->StaticMesh.LoadSynchronous();
		}
		return Mesh;
	}
}

ACrowdLegionActor::ACrowdLegionActor()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	CrowdComponent = CreateDefaultSubobject<UCrowdUnitComponent>(TEXT("CrowdComponent"));

	StatsComponent = CreateDefaultSubobject<UCrowdStatsComponent>(TEXT("StatsComponent"));
	StatsComponent->CrowdUnitComponent = CrowdComponent;

	CavalryComponent = CreateDefaultSubobject<UCavalryUnitComponent>(TEXT("CavalryComponent"));
}

void ACrowdLegionActor::BeginPlay()
{
	Super::BeginPlay();

	if (StatsComponent)
	{
		StatsComponent->DebugStatsFontSize = DebugStatsFontSize;
		StatsComponent->CrowdUnitComponent = CrowdComponent;
	}

	if (bAutoInitOnBeginPlay)
	{
		SpawnLegion();
		if (HorseMesh || HorseDataAsset || RiderMesh || RiderDataAsset)
		{
			SpawnCavalryLegion();
		}
	}
}

void ACrowdLegionActor::SetStatsFontSize(int32 NewSize)
{
	DebugStatsFontSize = FMath::Clamp(NewSize, 8, 72);
	if (StatsComponent)
	{
		StatsComponent->SetDebugStatsFontSize(DebugStatsFontSize);
	}
}

void ACrowdLegionActor::ExportAndBakeA2TLegion()
{
	UAnimToTextureDataAsset* TargetDataAsset = DataAsset;
	if (!TargetDataAsset)
	{
		// 若 Actor 上未配置，尝试直接从当前选中的资产导出
		FCrowdLegionExporter::ExecuteExportFromSelectedAsset();
		return;
	}

	USkeletalMesh* SkelMesh = SourceSkeletalMesh.Get();
	if (!SkelMesh)
	{
		SkelMesh = TargetDataAsset->GetSkeletalMesh();
	}

	if (!SkelMesh)
	{
		FMessageDialog::Open(
			EAppMsgType::Ok,
			FText::FromString(TEXT("请先在 CrowdLegionActor 或 DataAsset 中指定 SourceSkeletalMesh！")));
		return;
	}

	UMaterialFunction* BoneAnimMF = nullptr;
	if (!FCrowdLegionExporter::EnsurePluginBoneAnimationMF(BoneAnimMF) || !BoneAnimMF)
	{
		return;
	}

	const FString DataAssetPackage = TargetDataAsset->GetOutermost()->GetName();
	const FString BaseDir = FPackageName::GetLongPackagePath(DataAssetPackage);
	const FString SkelMeshName = SkelMesh->GetName();

	const FString StaticMeshPkgPath = BaseDir / TEXT("Meshes") / (TEXT("SM_") + SkelMeshName + TEXT("_A2T"));
	UStaticMesh* BakedStaticMesh = FCrowdLegionExporter::ExportStaticMeshFromSkeletalMesh(SkelMesh, StaticMeshPkgPath);
	if (!BakedStaticMesh)
	{
		return;
	}

	TargetDataAsset->StaticMesh = BakedStaticMesh;
	TargetDataAsset->MarkPackageDirty();

	const FString MatTargetDir = BaseDir / TEXT("Materials");
	TArray<UMaterialInterface*> AssignedMaterials;
	if (!FCrowdLegionExporter::GenerateAndAssignA2TMaterialsForStaticMesh(SkelMesh, BakedStaticMesh, MatTargetDir, AssignedMaterials))
	{
		return;
	}

	const bool bBakeSuccess = FCrowdLegionExporter::BakeA2TDataAsset(TargetDataAsset, BakedStaticMesh);
	const FString TablePkgPath = BaseDir / (TEXT("DT_") + SkelMeshName + TEXT("_AnimMeta"));
	UDataTable* AnimMetaTable = FCrowdLegionExporter::GenerateAnimMetaDataTable(TargetDataAsset, TablePkgPath);

	CrowdMesh = BakedStaticMesh;
	if (AnimMetaTable)
	{
		CrowdAnimMetaDataTable = AnimMetaTable;
	}
	if (CrowdComponent)
	{
		UDataTable* LayerDT = CrowdLegionActor_Private::MakeLayerTable(this, CrowdMesh, CrowdAnimMetaDataTable);
		CrowdComponent->SetupHISM(LayerDT, CrowdComponent->CullDistance, 0);
	}

	FMessageDialog::Open(
		EAppMsgType::Ok,
		FText::FromString(FString::Printf(TEXT("【A2T 军团资产一键导出%s！】\n\n- DataAsset: %s\n- StaticMesh: %s（%d 材质插槽）\n- 每槽 BoneAnimation 材质已绑定\n- 动画数据表: %s\n- 军团组件已自动同步绑定！"),
			bBakeSuccess ? TEXT("成功") : TEXT("部分失败"),
			*TargetDataAsset->GetName(),
			*BakedStaticMesh->GetName(),
			BakedStaticMesh->GetStaticMaterials().Num(),
			AnimMetaTable ? *AnimMetaTable->GetName() : TEXT("None"))));
}

void ACrowdLegionActor::SpawnLegion()
{
	if (!CrowdComponent)
	{
		return;
	}

	UStaticMesh* FinalMesh = CrowdMesh.Get();
	if (!FinalMesh)
	{
		FinalMesh = CrowdLegionActor_Private::ResolveMeshFromDataAsset(DataAsset);
		CrowdMesh = FinalMesh;
	}

	UDataTable* FinalAnimMeta = CrowdAnimMetaDataTable.Get();
	if (!FinalAnimMeta)
	{
		FinalAnimMeta = CrowdLegionActor_Private::TryLoadAnimMetaBesideDataAsset(DataAsset);
		CrowdAnimMetaDataTable = FinalAnimMeta;
	}

	UDataTable* LayerDT = CrowdLegionActor_Private::MakeLayerTable(this, FinalMesh, FinalAnimMeta);
	if (!CrowdComponent->SetupHISM(LayerDT, CrowdComponent->CullDistance, NumUnits))
	{
		UE_LOG(LogTemp, Warning, TEXT("[CrowdLegionActor] SpawnLegion: SetupHISM failed."));
		return;
	}
	CrowdComponent->ClearCells();

	TArray<FTransform> Transforms;
	Transforms.Reserve(NumUnits);

	const FVector Center = GetActorLocation();
	for (int32 i = 0; i < NumUnits; ++i)
	{
		const float Angle = FMath::FRand() * 2.0f * PI;
		const float Dist = FMath::Sqrt(FMath::FRand()) * SpreadRadius;
		const FVector Offset(FMath::Cos(Angle) * Dist, FMath::Sin(Angle) * Dist, 0.0f);
		const FRotator Rot(0.0f, bRandomYaw ? (FMath::FRand() * 360.0f) : 0.0f, 0.0f);

		Transforms.Add(FTransform(Rot, Center + Offset));
	}

	CrowdComponent->BatchAddCells(Transforms, IdleAnimIndex, 1.0f, true);
}

void ACrowdLegionActor::SetLegionIdle()
{
	if (CrowdComponent)
	{
		CrowdComponent->SetAllCellsAnim(IdleAnimIndex, 1.0f, IsLoopingAnimation(IdleAnimIndex));
	}
}

void ACrowdLegionActor::SetLegionRun()
{
	if (CrowdComponent)
	{
		CrowdComponent->SetAllCellsAnim(RunAnimIndex, 1.0f, IsLoopingAnimation(RunAnimIndex));
	}
}

void ACrowdLegionActor::SetLegionAttack()
{
	if (CrowdComponent)
	{
		CrowdComponent->SetAllCellsAnim(AttackAnimIndex, 1.0f, IsLoopingAnimation(AttackAnimIndex));
	}
}

void ACrowdLegionActor::KillAllLegion()
{
	if (CrowdComponent)
	{
		CrowdComponent->SetAllCellsAnim(DeathAnimIndex, 1.0f, IsLoopingAnimation(DeathAnimIndex));
	}
}

void ACrowdLegionActor::ClearLegion()
{
	StopLegionMarch();
	if (CrowdComponent)
	{
		CrowdComponent->ClearCells();
	}
	ClearCavalry();
}

FVector ACrowdLegionActor::GetLegionCenterLocation() const
{
	UHierarchicalInstancedStaticMeshComponent* TargetHISM = CrowdComponent ? CrowdComponent->GetHISM(0) : nullptr;
	if (!TargetHISM || TargetHISM->GetInstanceCount() == 0)
	{
		UHierarchicalInstancedStaticMeshComponent* CavalryHISM =
			CavalryComponent ? CavalryComponent->GetHISM(0) : nullptr;
		if (CavalryHISM && CavalryHISM->GetInstanceCount() > 0)
		{
			TargetHISM = CavalryHISM;
		}
		else
		{
			return GetActorLocation();
		}
	}

	const int32 Count = TargetHISM->GetInstanceCount();
	if (Count == 0)
	{
		return GetActorLocation();
	}

	FVector SumLoc = FVector::ZeroVector;
	for (int32 i = 0; i < Count; ++i)
	{
		FTransform T;
		TargetHISM->GetInstanceTransform(i, T, true);
		SumLoc += T.GetLocation();
	}
	return SumLoc / static_cast<float>(Count);
}

void ACrowdLegionActor::MarchLegionForward()
{
	const bool bHasInfantry = (CrowdComponent && CrowdComponent->GetCellCount() > 0);
	const bool bHasCavalry = (CavalryComponent && CavalryComponent->GetCellCount() > 0);
	if (!bHasInfantry && !bHasCavalry)
	{
		return;
	}

	const FVector Center = GetLegionCenterLocation();
	const FVector Forward = GetActorForwardVector();
	MarchTargetLocation = Center + Forward * 2000.0f;
	bIsMarching = true;
	SetActorTickEnabled(true);

	// 全军切换为 Run 动作
	if (bHasInfantry)
	{
		CrowdComponent->SetAllCellsAnim(RunAnimIndex, 1.0f, IsLoopingAnimation(RunAnimIndex));
	}
	if (bHasCavalry)
	{
		SetCavalryRun();
	}
}

void ACrowdLegionActor::StopLegionMarch()
{
	if (!bIsMarching)
	{
		return;
	}

	bIsMarching = false;
	SetActorTickEnabled(false);

	if (CrowdComponent)
	{
		CrowdComponent->SetAllCellsAnim(IdleAnimIndex, 1.0f, IsLoopingAnimation(IdleAnimIndex));
	}
	if (CavalryComponent && CavalryComponent->GetCellCount() > 0)
	{
		SetCavalryIdle();
	}
}

void ACrowdLegionActor::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	const bool bHasInfantry = (CrowdComponent && CrowdComponent->GetCellCount() > 0);
	const bool bHasCavalry = (CavalryComponent && CavalryComponent->GetCellCount() > 0);
	if (!bIsMarching || (!bHasInfantry && !bHasCavalry))
	{
		SetActorTickEnabled(false);
		return;
	}
	const FVector CurrentCenter = GetLegionCenterLocation();
	const FVector ToTarget = MarchTargetLocation - CurrentCenter;
	const float Dist2D = ToTarget.Size2D();

	if (Dist2D <= MarchAcceptanceRadius)
	{
		StopLegionMarch();
		return;
	}

	const FVector MoveDir = ToTarget.GetSafeNormal2D();
	const float StepDist = FMath::Min(Dist2D, MarchSpeed * DeltaTime);
	const FVector MovementDelta = MoveDir * StepDist;

	// 找出所有已阵亡单位 (其 AnimIndex 为 DeathAnimIndex)，忽略其移动
	if (bHasInfantry)
	{
		TArray<int32> DeadIndices;
		const TArray<FCrowdCellInstanceData>& Cells = CrowdComponent->GetAllInstanceData();
		for (int32 i = 0; i < Cells.Num(); ++i)
		{
			if (Cells[i].AnimIndex == DeathAnimIndex)
			{
				DeadIndices.Add(i);
			}
		}
		CrowdComponent->BatchMoveCells(MovementDelta, DeadIndices, true, false);
	}
	if (bHasCavalry)
	{
		TArray<int32> DeadCavalryIndices;
		const int32 DeathCavalryAnimIdx = CavalryComponent->FindAnimationIndex(CavalryDeathAnimName);
		for (int32 i = 0; i < CavalryComponent->GetCellCount(); ++i)
		{
			FCrowdCellInstanceData CellData;
			if (CavalryComponent->GetCellData(i, CellData))
			{
				const bool bIsDead = (DeathCavalryAnimIdx != INDEX_NONE)
					? (CellData.AnimIndex == DeathCavalryAnimIdx)
					: (!CellData.bLoop && !CavalryComponent->IsLoopingAnimation(CellData.AnimIndex));
				if (bIsDead)
				{
					DeadCavalryIndices.Add(i);
				}
			}
		}
		CavalryComponent->BatchMoveCells(MovementDelta, DeadCavalryIndices, true, false);
	}
}

void ACrowdLegionActor::SpawnCavalryLegion()
{
	SpawnCavalryLegionCustom(NumCavalryUnits, CavalrySpreadRadius, CavalryIdleAnimName);
}

void ACrowdLegionActor::SpawnCavalryLegionCustom(int32 InNumUnits, float InSpreadRadius, FName InInitialAnim)
{
	if (!CavalryComponent)
	{
		return;
	}

	// 1. 解析马匹静态网格体
	UStaticMesh* FinalHorseMesh = HorseMesh.Get();
	if (!FinalHorseMesh && HorseDataAsset)
	{
		FinalHorseMesh = HorseDataAsset->GetStaticMesh();
		if (!FinalHorseMesh && !HorseDataAsset->StaticMesh.IsNull())
		{
			FinalHorseMesh = HorseDataAsset->StaticMesh.LoadSynchronous();
		}
	}

	// 2. 解析骑手静态网格体
	UStaticMesh* FinalRiderMesh = RiderMesh.Get();
	if (!FinalRiderMesh && RiderDataAsset)
	{
		FinalRiderMesh = RiderDataAsset->GetStaticMesh();
		if (!FinalRiderMesh && !RiderDataAsset->StaticMesh.IsNull())
		{
			FinalRiderMesh = RiderDataAsset->StaticMesh.LoadSynchronous();
		}
	}

	// 3. 解析马匹动画元数据数据表
	UDataTable* FinalHorseAnimTable = HorseAnimMetaDataTable.Get();
	if (!FinalHorseAnimTable && HorseDataAsset)
	{
		const FString BaseDir = FPackageName::GetLongPackagePath(HorseDataAsset->GetOutermost()->GetName());
		const FString SkelName = HorseDataAsset->GetSkeletalMesh() ? HorseDataAsset->GetSkeletalMesh()->GetName() : HorseDataAsset->GetName();
		const FString TablePkgPath = BaseDir / (TEXT("DT_") + SkelName + TEXT("_AnimMeta"));
		FinalHorseAnimTable = Cast<UDataTable>(StaticLoadObject(UDataTable::StaticClass(), nullptr, *(TablePkgPath + TEXT(".") + FPackageName::GetShortName(TablePkgPath))));
	}

	// 4. 解析骑手动画元数据数据表
	UDataTable* FinalRiderAnimTable = RiderAnimMetaDataTable.Get();
	if (!FinalRiderAnimTable && RiderDataAsset)
	{
		const FString BaseDir = FPackageName::GetLongPackagePath(RiderDataAsset->GetOutermost()->GetName());
		const FString SkelName = RiderDataAsset->GetSkeletalMesh() ? RiderDataAsset->GetSkeletalMesh()->GetName() : RiderDataAsset->GetName();
		const FString TablePkgPath = BaseDir / (TEXT("DT_") + SkelName + TEXT("_AnimMeta"));
		FinalRiderAnimTable = Cast<UDataTable>(StaticLoadObject(UDataTable::StaticClass(), nullptr, *(TablePkgPath + TEXT(".") + FPackageName::GetShortName(TablePkgPath))));
	}

	UDataTable* LayerDT = CrowdLegionActor_Private::MakeLayerTable(
		this, FinalHorseMesh, FinalHorseAnimTable, FinalRiderMesh, FinalRiderAnimTable);
	if (!CavalryComponent->SetupHISM(LayerDT, CavalryComponent->CullDistance, InNumUnits))
	{
		UE_LOG(LogTemp, Warning, TEXT("[CrowdLegionActor] SpawnCavalryLegion: SetupHISM failed."));
		return;
	}

	CavalryComponent->ClearCells();

	if (InNumUnits <= 0)
	{
		return;
	}

	TArray<FTransform> Transforms;
	Transforms.Reserve(InNumUnits);
	const FVector Center = GetActorLocation();

	for (int32 i = 0; i < InNumUnits; ++i)
	{
		const float Angle = FMath::FRand() * 2.0f * PI;
		const float Dist = FMath::Sqrt(FMath::FRand()) * InSpreadRadius;
		const FVector Offset(FMath::Cos(Angle) * Dist, FMath::Sin(Angle) * Dist, 0.0f);
		const FRotator Rot(0.0f, bRandomYaw ? (FMath::FRand() * 360.0f) : 0.0f, 0.0f);
		Transforms.Add(FTransform(Rot, Center + Offset));
	}

	const FName InitAnim = InInitialAnim.IsNone() ? CavalryIdleAnimName : InInitialAnim;
	CavalryComponent->BatchAddCellsByName(Transforms, InitAnim, 1.0f, true);
	UE_LOG(LogTemp, Log, TEXT("[CrowdLegionActor] Spawned %d cavalry units (SpreadRadius=%.1f, InitialAnim='%s')."),
		InNumUnits, InSpreadRadius, *InitAnim.ToString());
}

int32 ACrowdLegionActor::AddCavalryUnit(const FTransform& Transform, FName AnimName, float PlayRate, bool bLoop)
{
	if (!CavalryComponent)
	{
		return INDEX_NONE;
	}

	if (CavalryComponent->GetLayerCount() < 2)
	{
		SpawnCavalryLegionCustom(0, 0.0f, AnimName);
	}

	const FName TargetAnim = AnimName.IsNone() ? CavalryIdleAnimName : AnimName;
	return CavalryComponent->AddCellByName(Transform, TargetAnim, PlayRate, bLoop);
}

void ACrowdLegionActor::SetCavalryIdle()
{
	if (CavalryComponent)
	{
		CavalryComponent->SetAllCellsAnimByName(CavalryIdleAnimName, 1.0f, true);
	}
}

void ACrowdLegionActor::SetCavalryRun()
{
	if (CavalryComponent)
	{
		CavalryComponent->SetAllCellsAnimByName(CavalryRunAnimName, 1.0f, true);
	}
}

void ACrowdLegionActor::SetCavalryAttack()
{
	if (CavalryComponent)
	{
		CavalryComponent->SetAllCellsAnimByName(CavalryAttackAnimName, 1.0f, false);
	}
}

void ACrowdLegionActor::KillAllCavalry()
{
	if (CavalryComponent)
	{
		CavalryComponent->SetAllCellsAnimByName(CavalryDeathAnimName, 1.0f, false);
	}
}

void ACrowdLegionActor::ClearCavalry()
{
	if (CavalryComponent)
	{
		CavalryComponent->ClearCells();
	}
}


bool ACrowdLegionActor::IsLoopingAnimation(int32 AnimIndex) const
{
	if (CrowdComponent)
	{
		return CrowdComponent->IsLoopingAnimation(AnimIndex);
	}
	return true;
}