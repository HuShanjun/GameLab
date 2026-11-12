// Copyright Epic Games, Inc. All Rights Reserved.

#include "CavalryUnitComponent.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameTime.h"

/** 获取与材质渲染器 View.RealTime 严格一致的高精度当前时间戳 */
static float GetCurrentCavalryTime(const UWorld* World)
{
	if (World)
	{
		return static_cast<float>(World->GetRealTimeSeconds());
	}
	return static_cast<float>(FGameTime::GetTimeSinceAppStart().GetRealTimeSeconds());
}

UCavalryUnitComponent::UCavalryUnitComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

UHierarchicalInstancedStaticMeshComponent* UCavalryUnitComponent::EnsureHISMComponent(
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent>& InOutComponent,
	const FName& ComponentName,
	UStaticMesh* Mesh,
	const TArray<TObjectPtr<UMaterialInterface>>& Materials)
{
	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return nullptr;
	}

	if (!InOutComponent)
	{
		InOutComponent = NewObject<UHierarchicalInstancedStaticMeshComponent>(Owner, ComponentName);
		InOutComponent->SetNumCustomDataFloats(FCrowdCellInstanceData::CustomDataCount);
		InOutComponent->RegisterComponent();
		Owner->AddInstanceComponent(InOutComponent);
		InOutComponent->AttachToComponent(Owner->GetRootComponent(), FAttachmentTransformRules::KeepRelativeTransform);
	}
	else
	{
		InOutComponent->SetNumCustomDataFloats(FCrowdCellInstanceData::CustomDataCount);
	}

	if (Mesh && InOutComponent->GetStaticMesh() != Mesh)
	{
		InOutComponent->SetStaticMesh(Mesh);
	}

	if (Materials.Num() > 0)
	{
		for (int32 SlotIndex = 0; SlotIndex < Materials.Num(); ++SlotIndex)
		{
			if (Materials[SlotIndex])
			{
				InOutComponent->SetMaterial(SlotIndex, Materials[SlotIndex]);
			}
		}
	}
	else if (Mesh)
	{
		const TArray<FStaticMaterial>& Slots = Mesh->GetStaticMaterials();
		for (int32 SlotIndex = 0; SlotIndex < Slots.Num(); ++SlotIndex)
		{
			if (Slots[SlotIndex].MaterialInterface)
			{
				InOutComponent->SetMaterial(SlotIndex, Slots[SlotIndex].MaterialInterface);
			}
		}
	}

	InOutComponent->SetNumCustomDataFloats(FCrowdCellInstanceData::CustomDataCount);
	InOutComponent->SetCullDistances(0.0f, CullDistance);
	return InOutComponent;
}

void UCavalryUnitComponent::SetupCavalryHISM(
	UStaticMesh* InHorseMesh,
	const TArray<UMaterialInterface*>& InHorseMaterials,
	UDataTable* InHorseAnimTable,
	UStaticMesh* InRiderMesh,
	const TArray<UMaterialInterface*>& InRiderMaterials,
	UDataTable* InRiderAnimTable,
	int32 PreallocateCount)
{
	AActor* Owner = GetOwner();
	if (!Owner)
	{
		UE_LOG(LogTemp, Warning, TEXT("[CavalryUnitComponent] SetupCavalryHISM failed: Owner is null."));
		return;
	}

	if (InHorseMesh)
	{
		HorseMesh = InHorseMesh;
	}
	if (InHorseMaterials.Num() > 0)
	{
		HorseMaterials.Reset(InHorseMaterials.Num());
		for (UMaterialInterface* Mat : InHorseMaterials)
		{
			HorseMaterials.Add(Mat);
		}
	}

	if (InRiderMesh)
	{
		RiderMesh = InRiderMesh;
	}
	if (InRiderMaterials.Num() > 0)
	{
		RiderMaterials.Reset(InRiderMaterials.Num());
		for (UMaterialInterface* Mat : InRiderMaterials)
		{
			RiderMaterials.Add(Mat);
		}
	}

	EnsureHISMComponent(HorseHISMCmp, TEXT("CavalryHorseHISM"), HorseMesh, HorseMaterials);
	EnsureHISMComponent(RiderHISMCmp, TEXT("CavalryRiderHISM"), RiderMesh, RiderMaterials);

	if (PreallocateCount > 0)
	{
		if (HorseHISMCmp) HorseHISMCmp->PreAllocateInstancesMemory(PreallocateCount);
		if (RiderHISMCmp) RiderHISMCmp->PreAllocateInstancesMemory(PreallocateCount);
	}

	LoadAnimationsFromDataTables(InHorseAnimTable, InRiderAnimTable);
}

void UCavalryUnitComponent::ParseDataTable(
	UDataTable* Table,
	TArray<FUnitAnimationTableRow>& OutAnimations,
	TMap<FName, int32>& OutNameToIndex)
{
	OutAnimations.Reset();
	OutNameToIndex.Reset();
	if (!Table)
	{
		return;
	}

	static const FString ContextString(TEXT("CavalryUnitComponent::ParseDataTable"));
	TArray<FUnitAnimationTableRow*> Rows;
	Table->GetAllRows<FUnitAnimationTableRow>(ContextString, Rows);

	for (const FUnitAnimationTableRow* Row : Rows)
	{
		if (!Row) continue;
		const int32 NewIdx = OutAnimations.Add(*Row);
		OutNameToIndex.Add(Row->AnimName, NewIdx);
	}
}

void UCavalryUnitComponent::LoadAnimationsFromDataTables(UDataTable* InHorseTable, UDataTable* InRiderTable)
{
	if (InHorseTable)
	{
		HorseAnimMetaDataTable = InHorseTable;
	}
	if (InRiderTable)
	{
		RiderAnimMetaDataTable = InRiderTable;
	}

	ParseDataTable(HorseAnimMetaDataTable.Get(), HorseAnimations, HorseAnimNameToIndex);
	ParseDataTable(RiderAnimMetaDataTable.Get(), RiderAnimations, RiderAnimNameToIndex);

	UE_LOG(LogTemp, Log, TEXT("[CavalryUnitComponent] Loaded animations: Horse=%d from '%s', Rider=%d from '%s'."),
		HorseAnimations.Num(), HorseAnimMetaDataTable ? *HorseAnimMetaDataTable->GetName() : TEXT("None"),
		RiderAnimations.Num(), RiderAnimMetaDataTable ? *RiderAnimMetaDataTable->GetName() : TEXT("None"));
}

void UCavalryUnitComponent::PushCellCustomData(int32 Index, bool bMarkRenderStateDirty)
{
	if (!CellsData.IsValidIndex(Index))
	{
		return;
	}

	const FCrowdCellInstanceData& D = CellsData[Index];
	const float SafeRate = FMath::Max(0.0001f, FMath::Abs(D.PlayRate));
	const float SignedRate = D.bLoop ? SafeRate : -SafeRate;
	const float SharedTimeOffset = D.Phase;

	// 1. 马匹 CustomData
	if (HorseHISMCmp)
	{
		float HorseStart = 0.f;
		float HorseEnd = 0.f;
		if (HorseAnimations.IsValidIndex(D.AnimIndex))
		{
			HorseStart = float(HorseAnimations[D.AnimIndex].StartFrame);
			HorseEnd = float(HorseAnimations[D.AnimIndex].EndFrame);
			if (HorseEnd < HorseStart) HorseEnd = HorseStart;
		}

		float HorseCustomData[4];
		HorseCustomData[FCrowdCellInstanceData::CD_TimeOffset] = SharedTimeOffset;
		HorseCustomData[FCrowdCellInstanceData::CD_PlayRate]   = SignedRate;
		HorseCustomData[FCrowdCellInstanceData::CD_StartFrame] = HorseStart;
		HorseCustomData[FCrowdCellInstanceData::CD_EndFrame]   = HorseEnd;
		HorseHISMCmp->SetCustomData(Index, MakeArrayView(HorseCustomData), bMarkRenderStateDirty);
	}

	// 2. 骑手 CustomData (同名动作对应的帧区间，但 TimeOffset 与 SignedRate 绝对锁定一致)
	if (RiderHISMCmp)
	{
		float RiderStart = 0.f;
		float RiderEnd = 0.f;

		// 优先根据马匹动作的 AnimName 在骑手表中查找同名动作
		int32 RiderAnimIdx = INDEX_NONE;
		if (HorseAnimations.IsValidIndex(D.AnimIndex))
		{
			const FName TargetAnimName = HorseAnimations[D.AnimIndex].AnimName;
			if (const int32* Found = RiderAnimNameToIndex.Find(TargetAnimName))
			{
				RiderAnimIdx = *Found;
			}
		}

		// 若未找到同名动作，回退直接使用 D.AnimIndex 或 Index 0
		if (RiderAnimIdx == INDEX_NONE)
		{
			if (RiderAnimations.IsValidIndex(D.AnimIndex))
			{
				RiderAnimIdx = D.AnimIndex;
			}
			else if (RiderAnimations.Num() > 0)
			{
				RiderAnimIdx = 0;
			}
		}

		if (RiderAnimations.IsValidIndex(RiderAnimIdx))
		{
			RiderStart = float(RiderAnimations[RiderAnimIdx].StartFrame);
			RiderEnd = float(RiderAnimations[RiderAnimIdx].EndFrame);
			if (RiderEnd < RiderStart) RiderEnd = RiderStart;
		}

		float RiderCustomData[4];
		RiderCustomData[FCrowdCellInstanceData::CD_TimeOffset] = SharedTimeOffset;
		RiderCustomData[FCrowdCellInstanceData::CD_PlayRate]   = SignedRate;
		RiderCustomData[FCrowdCellInstanceData::CD_StartFrame] = RiderStart;
		RiderCustomData[FCrowdCellInstanceData::CD_EndFrame]   = RiderEnd;
		RiderHISMCmp->SetCustomData(Index, MakeArrayView(RiderCustomData), bMarkRenderStateDirty);
	}
}

int32 UCavalryUnitComponent::AddCell(const FTransform& Transform, FName AnimName, float PlayRate, bool bLoop, float StartTimeOffset)
{
	if (!HorseHISMCmp || !RiderHISMCmp)
	{
		SetupCavalryHISM(HorseMesh, TArray<UMaterialInterface*>(), HorseAnimMetaDataTable, RiderMesh, TArray<UMaterialInterface*>(), RiderAnimMetaDataTable);
	}
	if (!HorseHISMCmp || !RiderHISMCmp)
	{
		UE_LOG(LogTemp, Warning, TEXT("[CavalryUnitComponent] AddCell failed: HISM components not initialized."));
		return INDEX_NONE;
	}

	int32 TargetAnimIndex = 0;
	if (!AnimName.IsNone())
	{
		TargetAnimIndex = FindAnimationIndex(AnimName);
		if (TargetAnimIndex == INDEX_NONE)
		{
			TargetAnimIndex = 0;
		}
	}

	return AddCellByIndex(Transform, TargetAnimIndex, PlayRate, bLoop, StartTimeOffset);
}

int32 UCavalryUnitComponent::AddCellByIndex(const FTransform& Transform, int32 AnimIndex, float PlayRate, bool bLoop, float StartTimeOffset)
{
	if (!HorseHISMCmp || !RiderHISMCmp)
	{
		SetupCavalryHISM(HorseMesh, TArray<UMaterialInterface*>(), HorseAnimMetaDataTable, RiderMesh, TArray<UMaterialInterface*>(), RiderAnimMetaDataTable);
	}
	if (!HorseHISMCmp || !RiderHISMCmp)
	{
		return INDEX_NONE;
	}

	FCrowdCellInstanceData Data;
	Data.AnimIndex = AnimIndex;
	Data.PlayRate  = PlayRate;
	Data.bLoop     = bLoop;
	// 共享相位计算：循环赋予 0~10s 随机相位，单次使用负的当前世界时间
	const float BasePhase = bLoop ? (FMath::FRand() * 10.0f) : -GetCurrentCavalryTime(GetWorld());
	Data.Phase     = BasePhase + StartTimeOffset;
	Data.StartTime = Data.Phase;

	const int32 HorseIdx = HorseHISMCmp->AddInstance(Transform, true);
	const int32 RiderIdx = RiderHISMCmp->AddInstance(Transform, true);

	checkf(HorseIdx == RiderIdx, TEXT("[CavalryUnitComponent] Instance index mismatch: Horse=%d Rider=%d"), HorseIdx, RiderIdx);

	CellsData.Add(Data);
	PushCellCustomData(HorseIdx, true);
	return HorseIdx;
}

void UCavalryUnitComponent::BatchAddCells(const TArray<FTransform>& Transforms, int32 AnimIndex, float PlayRate, bool bLoop)
{
	if (!HorseHISMCmp || !RiderHISMCmp)
	{
		SetupCavalryHISM(HorseMesh, TArray<UMaterialInterface*>(), HorseAnimMetaDataTable, RiderMesh, TArray<UMaterialInterface*>(), RiderAnimMetaDataTable, Transforms.Num());
	}
	if (!HorseHISMCmp || !RiderHISMCmp || Transforms.Num() == 0)
	{
		return;
	}

	const int32 StartIdx = CellsData.Num();
	const int32 AddCount = Transforms.Num();
	CellsData.Reserve(StartIdx + AddCount);
	const float BaseTime = GetCurrentCavalryTime(GetWorld());

	// 双方 HISM 批量添加变换
	HorseHISMCmp->AddInstances(Transforms, false, true);
	RiderHISMCmp->AddInstances(Transforms, false, true);

	for (int32 i = 0; i < AddCount; ++i)
	{
		FCrowdCellInstanceData Data;
		Data.AnimIndex = AnimIndex;
		Data.PlayRate  = PlayRate;
		Data.bLoop     = bLoop;
		Data.Phase     = bLoop ? (FMath::FRand() * 10.0f) : -BaseTime;
		Data.StartTime = Data.Phase;
		CellsData.Add(Data);
		PushCellCustomData(StartIdx + i, false);
	}

	HorseHISMCmp->MarkRenderStateDirty();
	RiderHISMCmp->MarkRenderStateDirty();
}

void UCavalryUnitComponent::BatchAddCellsByName(const TArray<FTransform>& Transforms, FName AnimName, float PlayRate, bool bLoop)
{
	if (!HorseHISMCmp || !RiderHISMCmp)
	{
		SetupCavalryHISM(HorseMesh, TArray<UMaterialInterface*>(), HorseAnimMetaDataTable, RiderMesh, TArray<UMaterialInterface*>(), RiderAnimMetaDataTable, Transforms.Num());
	}

	int32 TargetAnimIndex = 0;
	if (!AnimName.IsNone())
	{
		TargetAnimIndex = FindAnimationIndex(AnimName);
		if (TargetAnimIndex == INDEX_NONE)
		{
			TargetAnimIndex = 0;
		}
	}
	BatchAddCells(Transforms, TargetAnimIndex, PlayRate, bLoop);
}

bool UCavalryUnitComponent::RemoveCell(int32 CellIndex)
{
	if (!CellsData.IsValidIndex(CellIndex))
	{
		return false;
	}

	if (HorseHISMCmp)
	{
		HorseHISMCmp->RemoveInstance(CellIndex);
	}
	if (RiderHISMCmp)
	{
		RiderHISMCmp->RemoveInstance(CellIndex);
	}

	CellsData.RemoveAtSwap(CellIndex);
	return true;
}

void UCavalryUnitComponent::ClearCells()
{
	if (HorseHISMCmp)
	{
		HorseHISMCmp->ClearInstances();
	}
	if (RiderHISMCmp)
	{
		RiderHISMCmp->ClearInstances();
	}
	CellsData.Empty();
}

bool UCavalryUnitComponent::GetCellData(int32 CellIndex, FCrowdCellInstanceData& OutData) const
{
	if (!CellsData.IsValidIndex(CellIndex))
	{
		return false;
	}
	OutData = CellsData[CellIndex];
	return true;
}

bool UCavalryUnitComponent::UpdateCellTransform(int32 CellIndex, const FTransform& NewTransform, bool bWorldSpace)
{
	if (!CellsData.IsValidIndex(CellIndex))
	{
		return false;
	}

	bool bHorseOk = false;
	bool bRiderOk = false;
	if (HorseHISMCmp)
	{
		bHorseOk = HorseHISMCmp->UpdateInstanceTransform(CellIndex, NewTransform, bWorldSpace, true, true);
	}
	if (RiderHISMCmp)
	{
		bRiderOk = RiderHISMCmp->UpdateInstanceTransform(CellIndex, NewTransform, bWorldSpace, true, true);
	}
	return (bHorseOk && bRiderOk);
}

void UCavalryUnitComponent::BatchMoveCells(const FVector& MovementDelta, const TArray<int32>& IgnoredIndices, bool bFaceMovementDirection, bool bTeleport)
{
	if (CellsData.Num() == 0 || !HorseHISMCmp || !RiderHISMCmp)
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
		HorseHISMCmp->GetInstanceTransform(i, T, true);

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
		HorseHISMCmp->BatchUpdateInstancesTransforms(0, UpdatedTransforms, true, true, bTeleport);
		RiderHISMCmp->BatchUpdateInstancesTransforms(0, UpdatedTransforms, true, true, bTeleport);
	}
}

void UCavalryUnitComponent::SetCellAnimByName(int32 CellIndex, FName AnimName, float InPlayRate, bool bInLoop)
{
	const int32 Idx = FindAnimationIndex(AnimName);
	if (Idx != INDEX_NONE)
	{
		SetCellAnim(CellIndex, Idx, InPlayRate, bInLoop);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[CavalryUnitComponent] Animation '%s' not found for cavalry cell %d."),
			*AnimName.ToString(), CellIndex);
	}
}

void UCavalryUnitComponent::SetCellAnim(int32 CellIndex, int32 NewAnimIndex, float InPlayRate, bool bInLoop)
{
	if (!CellsData.IsValidIndex(CellIndex))
	{
		return;
	}

	CellsData[CellIndex].AnimIndex = NewAnimIndex;
	CellsData[CellIndex].PlayRate  = InPlayRate;
	CellsData[CellIndex].bLoop     = bInLoop;
	CellsData[CellIndex].Phase     = bInLoop ? (FMath::FRand() * 10.0f) : -GetCurrentCavalryTime(GetWorld());
	CellsData[CellIndex].StartTime = CellsData[CellIndex].Phase;
	PushCellCustomData(CellIndex, true);
}

void UCavalryUnitComponent::SetAllCellsAnim(int32 NewAnimIndex, float InPlayRate, bool bInLoop)
{
	const int32 Count = CellsData.Num();
	if (Count == 0)
	{
		return;
	}

	const float BaseTime = GetCurrentCavalryTime(GetWorld());
	for (int32 i = 0; i < Count; ++i)
	{
		CellsData[i].AnimIndex = NewAnimIndex;
		CellsData[i].PlayRate  = InPlayRate;
		CellsData[i].bLoop     = bInLoop;
		CellsData[i].Phase     = bInLoop ? (FMath::FRand() * 10.0f) : -BaseTime;
		CellsData[i].StartTime = CellsData[i].Phase;
		PushCellCustomData(i, false);
	}

	if (HorseHISMCmp)
	{
		HorseHISMCmp->MarkRenderStateDirty();
	}
	if (RiderHISMCmp)
	{
		RiderHISMCmp->MarkRenderStateDirty();
	}
}

void UCavalryUnitComponent::SetAllCellsAnimByName(FName AnimName, float InPlayRate, bool bInLoop)
{
	const int32 Idx = FindAnimationIndex(AnimName);
	if (Idx != INDEX_NONE)
	{
		SetAllCellsAnim(Idx, InPlayRate, bInLoop);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[CavalryUnitComponent] SetAllCellsAnimByName failed: Anim '%s' not found."),
			*AnimName.ToString());
	}
}

int32 UCavalryUnitComponent::FindAnimationIndex(FName AnimName) const
{
	// 优先在马匹动画表中检索
	if (const int32* Found = HorseAnimNameToIndex.Find(AnimName))
	{
		return *Found;
	}

	const FString TargetStr = AnimName.ToString().ToLower();
	for (int32 i = 0; i < HorseAnimations.Num(); ++i)
	{
		if (HorseAnimations[i].AnimName.ToString().ToLower().Contains(TargetStr))
		{
			return i;
		}
	}

	// 其次在骑手动画表中检索
	if (const int32* Found = RiderAnimNameToIndex.Find(AnimName))
	{
		return *Found;
	}
	for (int32 i = 0; i < RiderAnimations.Num(); ++i)
	{
		if (RiderAnimations[i].AnimName.ToString().ToLower().Contains(TargetStr))
		{
			return i;
		}
	}

	return INDEX_NONE;
}

bool UCavalryUnitComponent::IsLoopingAnimation(int32 AnimIndex) const
{
	if (HorseAnimations.IsValidIndex(AnimIndex))
	{
		return HorseAnimations[AnimIndex].bDefaultLoop;
	}
	if (RiderAnimations.IsValidIndex(AnimIndex))
	{
		return RiderAnimations[AnimIndex].bDefaultLoop;
	}
	return true;
}
