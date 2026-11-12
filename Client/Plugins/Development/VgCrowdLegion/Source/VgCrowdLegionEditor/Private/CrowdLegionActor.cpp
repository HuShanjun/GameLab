// Copyright Epic Games, Inc. All Rights Reserved.

#include "CrowdLegionActor.h"
#include "UnitHISMComponent.h"
#include "CrowdStatsComponent.h"
#include "Components/SceneComponent.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"

ACrowdLegionActor::ACrowdLegionActor()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	UnitComponent = CreateDefaultSubobject<UUnitHISMComponent>(TEXT("UnitComponent"));

	StatsComponent = CreateDefaultSubobject<UCrowdStatsComponent>(TEXT("StatsComponent"));
	StatsComponent->UnitComponent = UnitComponent;
}

void ACrowdLegionActor::BeginPlay()
{
	Super::BeginPlay();

	if (StatsComponent)
	{
		StatsComponent->DebugStatsFontSize = DebugStatsFontSize;
		StatsComponent->UnitComponent = UnitComponent;
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

void ACrowdLegionActor::SpawnLegion()
{
	if (!UnitComponent)
	{
		return;
	}

	if (!UnitComponent->SetupHISM(LayerSetupTable, CullDistance, NumUnits))
	{
		UE_LOG(LogTemp, Warning, TEXT("[CrowdLegionActor] SpawnLegion: SetupHISM failed (check LayerSetupTable)."));
		return;
	}

	UnitComponent->ClearCells();

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

	UnitComponent->BatchAddCells(Transforms, IdleAnimIndex, 1.0f, true);
}

void ACrowdLegionActor::SetLegionIdle()
{
	if (UnitComponent)
	{
		UnitComponent->SetAllCellsAnim(IdleAnimIndex, 1.0f, IsLoopingAnimation(IdleAnimIndex));
	}
}

void ACrowdLegionActor::SetLegionRun()
{
	if (UnitComponent)
	{
		UnitComponent->SetAllCellsAnim(RunAnimIndex, 1.0f, IsLoopingAnimation(RunAnimIndex));
	}
}

void ACrowdLegionActor::SetLegionAttack()
{
	if (UnitComponent)
	{
		UnitComponent->SetAllCellsAnim(AttackAnimIndex, 1.0f, IsLoopingAnimation(AttackAnimIndex));
	}
}

void ACrowdLegionActor::KillAllLegion()
{
	if (UnitComponent)
	{
		UnitComponent->SetAllCellsAnim(DeathAnimIndex, 1.0f, IsLoopingAnimation(DeathAnimIndex));
	}
}

void ACrowdLegionActor::ClearLegion()
{
	StopLegionMarch();
	if (UnitComponent)
	{
		UnitComponent->ClearCells();
	}
}

FVector ACrowdLegionActor::GetLegionCenterLocation() const
{
	UHierarchicalInstancedStaticMeshComponent* TargetHISM = UnitComponent ? UnitComponent->GetHISM(0) : nullptr;
	if (!TargetHISM || TargetHISM->GetInstanceCount() == 0)
	{
		return GetActorLocation();
	}

	const int32 Count = TargetHISM->GetInstanceCount();
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
	if (!UnitComponent || UnitComponent->GetCellCount() == 0)
	{
		return;
	}

	const FVector Center = GetLegionCenterLocation();
	const FVector Forward = GetActorForwardVector();
	MarchTargetLocation = Center + Forward * 2000.0f;
	bIsMarching = true;
	SetActorTickEnabled(true);
	UnitComponent->SetAllCellsAnim(RunAnimIndex, 1.0f, IsLoopingAnimation(RunAnimIndex));
}

void ACrowdLegionActor::StopLegionMarch()
{
	if (!bIsMarching)
	{
		return;
	}

	bIsMarching = false;
	SetActorTickEnabled(false);
	if (UnitComponent)
	{
		UnitComponent->SetAllCellsAnim(IdleAnimIndex, 1.0f, IsLoopingAnimation(IdleAnimIndex));
	}
}

void ACrowdLegionActor::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (!bIsMarching || !UnitComponent || UnitComponent->GetCellCount() == 0)
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

	const FVector Dir = ToTarget.GetSafeNormal2D();
	const FVector MovementDelta = Dir * MarchSpeed * DeltaTime;

	TArray<int32> DeadIndices;
	const TArray<FCrowdCellInstanceData>& Cells = UnitComponent->GetAllInstanceData();
	for (int32 i = 0; i < Cells.Num(); ++i)
	{
		if (Cells[i].AnimIndex == DeathAnimIndex)
		{
			DeadIndices.Add(i);
		}
	}
	UnitComponent->BatchMoveCells(MovementDelta, DeadIndices, true, false);
}

bool ACrowdLegionActor::IsLoopingAnimation(int32 AnimIndex) const
{
	if (UnitComponent)
	{
		return UnitComponent->IsLoopingAnimation(AnimIndex);
	}
	return true;
}
