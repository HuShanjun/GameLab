// Copyright Epic Games, Inc. All Rights Reserved.

#include "CrowdUnitComponent.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "AnimToTextureDataAsset.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/DataTable.h"
#include "Misc/PackageName.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Misc/App.h"
#include "GameTime.h"

/** 获取与材质渲染器 View.RealTime 严格一致的高精度当前时间戳 */
static float GetCurrentAnimationTime(const UWorld* World)
{
	if (World)
	{
		return static_cast<float>(World->GetRealTimeSeconds());
	}
	return static_cast<float>(FGameTime::GetTimeSinceAppStart().GetRealTimeSeconds());
}

UCrowdUnitComponent::UCrowdUnitComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UCrowdUnitComponent::SetupHISM(UStaticMesh* InMesh, UMaterialInterface* InMaterial, int32 PreallocateCount)
{
	AActor* Owner = GetOwner();
	if (!Owner)
	{
		UE_LOG(LogTemp, Warning, TEXT("[CrowdUnitComponent] SetupHISM failed: Owner is null."));
		return;
	}

	if (InMesh)
	{
		Mesh = InMesh;
	}
	if (InMaterial)
	{
		Material = InMaterial;
	}

	if (!HISMCmp)
	{
		HISMCmp = NewObject<UHierarchicalInstancedStaticMeshComponent>(Owner, TEXT("CrowdLegionHISM"));
		HISMCmp->SetNumCustomDataFloats(FCrowdCellInstanceData::CustomDataCount);
		HISMCmp->RegisterComponent();
		Owner->AddInstanceComponent(HISMCmp);
		HISMCmp->AttachToComponent(Owner->GetRootComponent(), FAttachmentTransformRules::KeepRelativeTransform);
	}
	else
	{
		HISMCmp->SetNumCustomDataFloats(FCrowdCellInstanceData::CustomDataCount);
	}

	if (Mesh && HISMCmp->GetStaticMesh() != Mesh)
	{
		HISMCmp->SetStaticMesh(Mesh);
	}

	if (Material)
	{
		HISMCmp->SetMaterial(0, Material);
	}
	else if (Mesh && Mesh->GetMaterial(0))
	{
		Material = Mesh->GetMaterial(0);
		HISMCmp->SetMaterial(0, Material);
	}

	HISMCmp->SetNumCustomDataFloats(FCrowdCellInstanceData::CustomDataCount);
	HISMCmp->SetCullDistances(0.0f, CullDistance);

	if (PreallocateCount > 0)
	{
		HISMCmp->PreAllocateInstancesMemory(PreallocateCount);
	}

	// 尝试从配置的数据表加载动画元数据
	if (AnimMetaDataTable)
	{
		LoadAnimationsFromDataTable(AnimMetaDataTable);
	}
}

void UCrowdUnitComponent::LoadAnimationsFromDataTable(UDataTable* InTable)
{
	UDataTable* TargetTable = InTable ? InTable : AnimMetaDataTable.Get();
	if (!TargetTable)
	{
		UE_LOG(LogTemp, Warning, TEXT("[CrowdUnitComponent] LoadAnimationsFromDataTable failed: AnimMetaDataTable is null."));
		return;
	}

	AnimMetaDataTable = TargetTable;
	Animations.Reset();
	AnimNameToIndex.Reset();

	static const FString ContextString(TEXT("CrowdUnitComponent::LoadAnimationsFromDataTable"));
	TArray<FUnitAnimationTableRow*> Rows;
	TargetTable->GetAllRows<FUnitAnimationTableRow>(ContextString, Rows);

	for (const FUnitAnimationTableRow* Row : Rows)
	{
		if (!Row) continue;

		const int32 NewIdx = Animations.Add(*Row);
		AnimNameToIndex.Add(Row->AnimName, NewIdx);
	}

	UE_LOG(LogTemp, Log, TEXT("[CrowdUnitComponent] Successfully loaded %d animations from DataTable '%s'."),
		Animations.Num(), *TargetTable->GetName());
}

int32 UCrowdUnitComponent::AddCell(const FTransform& Transform, int32 AnimIndex, float PlayRate, bool bLoop, float StartTimeOffset)
{
	if (!HISMCmp)
	{
		SetupHISM(Mesh, Material);
	}

	if (!HISMCmp)
	{
		return INDEX_NONE;
	}

	FCrowdCellInstanceData Data;
	Data.AnimIndex = AnimIndex;
	Data.PlayRate  = PlayRate;
	Data.bLoop     = bLoop;
	Data.Phase     = bLoop ? (FMath::FRand() * 10.0f) : -GetCurrentAnimationTime(GetWorld());
	Data.StartTime = Data.Phase;
	const int32 NewIdx = HISMCmp->AddInstance(Transform, true);
	CellsData.Add(Data);

	PushCellCustomData(NewIdx, true);
	return NewIdx;
}

void UCrowdUnitComponent::BatchAddCells(const TArray<FTransform>& Transforms, int32 AnimIndex, float PlayRate, bool bLoop)
{
	if (!HISMCmp)
	{
		SetupHISM(Mesh, Material, Transforms.Num());
	}

	if (!HISMCmp || Transforms.Num() == 0)
	{
		return;
	}

	const int32 StartIdx = CellsData.Num();
	const int32 AddCount = Transforms.Num();

	CellsData.Reserve(StartIdx + AddCount);
	const float BaseTime = GetCurrentAnimationTime(GetWorld());

	// 批量添加 HISM 实例变换
	HISMCmp->AddInstances(Transforms, false, true);

	for (int32 i = 0; i < AddCount; ++i)
	{
		FCrowdCellInstanceData Data;
		Data.AnimIndex = AnimIndex;
		Data.PlayRate  = PlayRate;
		Data.bLoop     = bLoop;
		// 循环动作赋予 0~10s 随机 TimeOffset；单次动作为 -Now
		Data.Phase     = bLoop ? (FMath::FRand() * 10.0f) : -BaseTime;
		Data.StartTime = Data.Phase;
		CellsData.Add(Data);
		PushCellCustomData(StartIdx + i, false);
	}

	HISMCmp->MarkRenderStateDirty();
}

bool UCrowdUnitComponent::RemoveCell(int32 CellIndex)
{
	if (!HISMCmp || !CellsData.IsValidIndex(CellIndex))
	{
		return false;
	}

	HISMCmp->RemoveInstance(CellIndex);
	CellsData.RemoveAtSwap(CellIndex);
	return true;
}

void UCrowdUnitComponent::ClearCells()
{
	if (HISMCmp)
	{
		HISMCmp->ClearInstances();
	}
	CellsData.Empty();
}

bool UCrowdUnitComponent::GetCellData(int32 CellIndex, FCrowdCellInstanceData& OutData) const
{
	if (!CellsData.IsValidIndex(CellIndex))
	{
		return false;
	}
	OutData = CellsData[CellIndex];
	return true;
}

bool UCrowdUnitComponent::UpdateCellTransform(int32 CellIndex, const FTransform& NewTransform, bool bWorldSpace)
{
	if (!HISMCmp || !CellsData.IsValidIndex(CellIndex))
	{
		return false;
	}
	return HISMCmp->UpdateInstanceTransform(CellIndex, NewTransform, bWorldSpace, true, true);
}

void UCrowdUnitComponent::BatchMoveCells(const FVector& MovementDelta, const TArray<int32>& IgnoredIndices, bool bFaceMovementDirection, bool bTeleport)
{
	if (!HISMCmp || CellsData.Num() == 0)
	{
		return;
	}

	const int32 Count = CellsData.Num();
	TArray<FTransform> UpdatedTransforms;
	UpdatedTransforms.SetNumUninitialized(Count);

	TSet<int32> IgnoredSet(IgnoredIndices);
	const FRotator TargetRot = bFaceMovementDirection ? MovementDelta.GetSafeNormal2D().Rotation() : FRotator::ZeroRotator;
	bool bAnyMoved = false;

	for (int32 i = 0; i < Count; ++i)
	{
		FTransform T;
		HISMCmp->GetInstanceTransform(i, T, true);

		if (!IgnoredSet.Contains(i))
		{
			T.AddToTranslation(MovementDelta);
			if (bFaceMovementDirection)
			{
				T.SetRotation(TargetRot.Quaternion());
			}
			bAnyMoved = true;
		}

		UpdatedTransforms[i] = T;
	}

	if (bAnyMoved)
	{
		HISMCmp->BatchUpdateInstancesTransforms(0, UpdatedTransforms, true, true, bTeleport);
	}
}

void UCrowdUnitComponent::PushCellCustomData(int32 Index, bool bMarkRenderStateDirty)
{
	if (!HISMCmp || !CellsData.IsValidIndex(Index))
	{
		return;
	}

	const FCrowdCellInstanceData& D = CellsData[Index];

	float StartFrame = 0.f;
	float EndFrame = 0.f;
	if (Animations.IsValidIndex(D.AnimIndex))
	{
		StartFrame = float(Animations[D.AnimIndex].StartFrame);
		const int32 NumFrames = FMath::Max(1, Animations[D.AnimIndex].NumFrames);
		EndFrame = StartFrame + float(NumFrames - 1);
	}
	const float SafeRate = FMath::Max(0.0001f, FMath::Abs(D.PlayRate));
	const float SignedRate = D.bLoop ? SafeRate : -SafeRate;
	// D.Phase already holds random phase (loop) or -Now per SetCellAnim / AddCell
	float CustomData[4];
	CustomData[FCrowdCellInstanceData::CD_TimeOffset] = D.Phase; // TimeOffset
	CustomData[FCrowdCellInstanceData::CD_PlayRate] = SignedRate;
	CustomData[FCrowdCellInstanceData::CD_StartFrame] = StartFrame;
	CustomData[FCrowdCellInstanceData::CD_EndFrame] = EndFrame;
	HISMCmp->SetCustomData(Index, MakeArrayView(CustomData), bMarkRenderStateDirty);
}

int32 UCrowdUnitComponent::FindAnimationIndex(FName AnimName) const
{
	if (const int32* Found = AnimNameToIndex.Find(AnimName))
	{
		return *Found;
	}

	const FString TargetStr = AnimName.ToString().ToLower();
	for (int32 i = 0; i < Animations.Num(); ++i)
	{
		if (Animations[i].AnimName.ToString().ToLower().Contains(TargetStr))
		{
			return i;
		}
	}
	return INDEX_NONE;
}

void UCrowdUnitComponent::SetCellAnim(int32 CellIndex, int32 NewAnimIndex, float InPlayRate, bool bInLoop)
{
	if (!CellsData.IsValidIndex(CellIndex))
	{
		return;
	}

	CellsData[CellIndex].AnimIndex = NewAnimIndex;
	CellsData[CellIndex].PlayRate = InPlayRate;
	CellsData[CellIndex].bLoop = bInLoop;
	CellsData[CellIndex].Phase = bInLoop ? (FMath::FRand() * 10.0f) : -GetCurrentAnimationTime(GetWorld());
	CellsData[CellIndex].StartTime = CellsData[CellIndex].Phase;
	PushCellCustomData(CellIndex, true);
}

void UCrowdUnitComponent::SetCellAnimByName(int32 CellIndex, FName AnimName, float InPlayRate, bool bInLoop)
{
	const int32 Idx = FindAnimationIndex(AnimName);
	if (Idx != INDEX_NONE)
	{
		SetCellAnim(CellIndex, Idx, InPlayRate, bInLoop);
	}
}

void UCrowdUnitComponent::SetAllCellsAnim(int32 NewAnimIndex, float InPlayRate, bool bInLoop)
{
	if (!HISMCmp)
	{
		return;
	}

	const float CurrentTime = GetCurrentAnimationTime(GetWorld());
	const int32 Count = CellsData.Num();
	for (int32 i = 0; i < Count; ++i)
	{
		CellsData[i].AnimIndex = NewAnimIndex;
		CellsData[i].PlayRate = InPlayRate;
		CellsData[i].bLoop = bInLoop;
		CellsData[i].Phase = bInLoop ? (FMath::FRand() * 10.0f) : -CurrentTime;
		CellsData[i].StartTime = CellsData[i].Phase;
		PushCellCustomData(i, i == Count - 1);
	}

	if (HISMCmp)
	{
		HISMCmp->MarkRenderStateDirty();
	}
}

void UCrowdUnitComponent::SetAllCellsAnimByName(FName AnimName, float InPlayRate, bool bInLoop)
{
	const int32 Idx = FindAnimationIndex(AnimName);
	if (Idx != INDEX_NONE)
	{
		SetAllCellsAnim(Idx, InPlayRate, bInLoop);
	}
}
