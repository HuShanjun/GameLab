// Copyright Epic Games, Inc. All Rights Reserved.

#include "CrowdLegionActor.h"
#include "CrowdUnitComponent.h"
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

ACrowdLegionActor::ACrowdLegionActor()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	CrowdComponent = CreateDefaultSubobject<UCrowdUnitComponent>(TEXT("CrowdComponent"));

	StatsComponent = CreateDefaultSubobject<UCrowdStatsComponent>(TEXT("StatsComponent"));
	StatsComponent->CrowdUnitComponent = CrowdComponent;
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

	UMaterial* UnusedCrowdMaster = nullptr;
	UMaterialFunction* BoneAnimMF = nullptr;
	if (!FCrowdLegionExporter::EnsurePluginMasterMaterials(UnusedCrowdMaster, BoneAnimMF) || !BoneAnimMF)
	{
		return;
	}

	const FString DataAssetPackage = TargetDataAsset->GetOutermost()->GetName();
	const FString BaseDir = FPackageName::GetLongPackagePath(DataAssetPackage);
	const FString SkelMeshName = SkelMesh->GetName();

	const FString StaticMeshPkgPath = BaseDir / (TEXT("SM_") + SkelMeshName + TEXT("_A2T"));
	UStaticMesh* BakedStaticMesh = FCrowdLegionExporter::ExportStaticMeshFromSkeletalMesh(SkelMesh, StaticMeshPkgPath);
	if (!BakedStaticMesh)
	{
		return;
	}

	TargetDataAsset->StaticMesh = BakedStaticMesh;
	TargetDataAsset->MarkPackageDirty();

	const bool bBakeSuccess = FCrowdLegionExporter::BakeA2TDataAsset(TargetDataAsset, BakedStaticMesh);

	const FString TablePkgPath = BaseDir / (TEXT("DT_") + SkelMeshName + TEXT("_AnimMeta"));
	UDataTable* AnimMetaTable = FCrowdLegionExporter::GenerateAnimMetaDataTable(TargetDataAsset, TablePkgPath);

	if (CrowdComponent)
	{
		CrowdComponent->Mesh = BakedStaticMesh;
		if (BakedStaticMesh->GetMaterial(0))
		{
			CrowdComponent->Material = BakedStaticMesh->GetMaterial(0);
		}
		if (AnimMetaTable)
		{
			CrowdComponent->AnimMetaDataTable = AnimMetaTable;
			CrowdComponent->LoadAnimationsFromDataTable(AnimMetaTable);
		}
		CrowdComponent->SetupHISM(CrowdComponent->Mesh, CrowdComponent->Material);
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
	// 若未显式指定 Mesh，但配置了 DataAsset，自动同步关联的 StaticMesh、Material 与动画元数据
	if (DataAsset)
	{
		if (!CrowdComponent->Mesh && !DataAsset->StaticMesh.IsNull())
		{
			CrowdComponent->Mesh = DataAsset->StaticMesh.LoadSynchronous();
		}
		if (!CrowdComponent->Material && CrowdComponent->Mesh && CrowdComponent->Mesh->GetMaterial(0))
		{
			CrowdComponent->Material = CrowdComponent->Mesh->GetMaterial(0);
		}
	}

	CrowdComponent->SetupHISM(CrowdComponent->Mesh, CrowdComponent->Material, NumUnits);
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
		CrowdComponent->SetAllCellsAnim(IdleAnimIndex, 1.0f, true);
	}
}

void ACrowdLegionActor::SetLegionRun()
{
	if (CrowdComponent)
	{
		CrowdComponent->SetAllCellsAnim(RunAnimIndex, 1.0f, true);
	}
}

void ACrowdLegionActor::SetLegionAttack()
{
	if (CrowdComponent)
	{
		CrowdComponent->SetAllCellsAnim(AttackAnimIndex, 1.0f, false);
	}
}

void ACrowdLegionActor::KillAllLegion()
{
	if (CrowdComponent)
	{
		CrowdComponent->SetAllCellsAnim(DeathAnimIndex, 1.0f, false);
	}
}

void ACrowdLegionActor::ClearLegion()
{
	StopLegionMarch();
	if (CrowdComponent)
	{
		CrowdComponent->ClearCells();
	}
}

FVector ACrowdLegionActor::GetLegionCenterLocation() const
{
	if (!CrowdComponent)
	{
		return GetActorLocation();
	}

	UHierarchicalInstancedStaticMeshComponent* HISMCmp = CrowdComponent->GetHISMCmp();
	if (!HISMCmp)
	{
		return GetActorLocation();
	}

	const int32 Count = HISMCmp->GetInstanceCount();
	if (Count == 0)
	{
		return GetActorLocation();
	}

	FVector SumLoc = FVector::ZeroVector;
	for (int32 i = 0; i < Count; ++i)
	{
		FTransform T;
		HISMCmp->GetInstanceTransform(i, T, true);
		SumLoc += T.GetLocation();
	}
	return SumLoc / static_cast<float>(Count);
}

void ACrowdLegionActor::MarchLegionForward()
{
	if (!CrowdComponent || CrowdComponent->GetCellCount() == 0)
	{
		return;
	}

	const FVector Center = GetLegionCenterLocation();
	const FVector Forward = GetActorForwardVector();
	MarchTargetLocation = Center + Forward * 2000.0f;
	bIsMarching = true;
	SetActorTickEnabled(true);

	// 全军切换为 Run 动作
	CrowdComponent->SetAllCellsAnim(RunAnimIndex, 1.0f, true);
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
		CrowdComponent->SetAllCellsAnim(IdleAnimIndex, 1.0f, true);
	}
}

void ACrowdLegionActor::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (!bIsMarching || !CrowdComponent)
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
