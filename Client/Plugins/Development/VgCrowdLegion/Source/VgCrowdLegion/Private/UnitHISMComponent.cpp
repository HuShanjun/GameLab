// Copyright Epic Games, Inc. All Rights Reserved.

#include "UnitHISMComponent.h"
#include "CrowdStatsComponent.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameTime.h"

float UUnitHISMComponent::GetAnimClock(const UWorld* World)
{
	if (World)
	{
		return static_cast<float>(World->GetRealTimeSeconds());
	}
	return static_cast<float>(FGameTime::GetTimeSinceAppStart().GetRealTimeSeconds());
}

UUnitHISMComponent::UUnitHISMComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

UHierarchicalInstancedStaticMeshComponent* UUnitHISMComponent::GetHISM(int32 LayerIndex) const
{
	if (!Layers.IsValidIndex(LayerIndex))
	{
		return nullptr;
	}
	return Layers[LayerIndex].HISMCmp;
}

bool UUnitHISMComponent::AreLayersReady() const
{
	if (Layers.Num() == 0)
	{
		return false;
	}
	for (const FUnitHISMLayerRuntime& Layer : Layers)
	{
		if (!Layer.HISMCmp)
		{
			return false;
		}
	}
	return true;
}

void UUnitHISMComponent::DestroyLayers()
{
	ClearCells();
	AActor* Owner = GetOwner();
	for (FUnitHISMLayerRuntime& Layer : Layers)
	{
		if (Layer.HISMCmp)
		{
			Layer.HISMCmp->ClearInstances();
			if (Owner)
			{
				Owner->RemoveInstanceComponent(Layer.HISMCmp);
			}
			Layer.HISMCmp->DestroyComponent();
			Layer.HISMCmp = nullptr;
		}
	}
	Layers.Empty();
}

void UUnitHISMComponent::ParseAnimTable(UDataTable* Table, FUnitHISMLayerRuntime& OutLayer)
{
	OutLayer.Animations.Reset();
	OutLayer.AnimNameToIndex.Reset();
	if (!Table)
	{
		return;
	}

	static const FString ContextString(TEXT("UUnitHISMComponent::ParseAnimTable"));
	TArray<FUnitAnimationTableRow*> Rows;
	Table->GetAllRows<FUnitAnimationTableRow>(ContextString, Rows);
	for (const FUnitAnimationTableRow* Row : Rows)
	{
		if (!Row)
		{
			continue;
		}
		const int32 NewIdx = OutLayer.Animations.Add(*Row);
		OutLayer.AnimNameToIndex.Add(Row->AnimName, NewIdx);
	}
}

UDataTable* UUnitHISMComponent::CreateTransientLayerTable(UObject* Outer, const TArray<FUnitHISMLayerTableRow>& Rows)
{
	if (!Outer || Rows.Num() == 0)
	{
		return nullptr;
	}

	UDataTable* Table = NewObject<UDataTable>(Outer, NAME_None, RF_Transient);
	Table->RowStruct = FUnitHISMLayerTableRow::StaticStruct();
	for (int32 i = 0; i < Rows.Num(); ++i)
	{
		const FName RowName(*FString::Printf(TEXT("Layer_%d"), i));
		Table->AddRow(RowName, Rows[i]);
	}
	return Table;
}

bool UUnitHISMComponent::SetupHISM(UDataTable* LayerTable, float InCullDistance, int32 PreallocateCount)
{
	AActor* Owner = GetOwner();
	if (!Owner)
	{
		UE_LOG(LogTemp, Warning, TEXT("[UnitHISM] SetupHISM failed: Owner is null."));
		return false;
	}
	if (!LayerTable)
	{
		UE_LOG(LogTemp, Warning, TEXT("[UnitHISM] SetupHISM failed: LayerTable is null."));
		return false;
	}

	CullDistance = InCullDistance;
	LayerSetupTable = LayerTable;

	DestroyLayers();

	static const FString ContextString(TEXT("UUnitHISMComponent::SetupHISM"));
	TArray<FUnitHISMLayerTableRow*> RowPtrs;
	LayerTable->GetAllRows<FUnitHISMLayerTableRow>(ContextString, RowPtrs);

	int32 LayerIndex = 0;
	for (const FUnitHISMLayerTableRow* Row : RowPtrs)
	{
		if (!Row || !Row->StaticMesh)
		{
			UE_LOG(LogTemp, Warning, TEXT("[UnitHISM] Skipping layer row without StaticMesh."));
			continue;
		}

		FUnitHISMLayerRuntime Layer;
		const FName CompName(*FString::Printf(TEXT("UnitHISM_%d"), LayerIndex));
		Layer.HISMCmp = NewObject<UHierarchicalInstancedStaticMeshComponent>(Owner, CompName);
		Layer.HISMCmp->SetNumCustomDataFloats(FCrowdCellInstanceData::CustomDataCount);
		Layer.HISMCmp->RegisterComponent();
		Owner->AddInstanceComponent(Layer.HISMCmp);
		Layer.HISMCmp->AttachToComponent(Owner->GetRootComponent(), FAttachmentTransformRules::KeepRelativeTransform);
		Layer.HISMCmp->SetStaticMesh(Row->StaticMesh);

		const TArray<FStaticMaterial>& Slots = Row->StaticMesh->GetStaticMaterials();
		for (int32 SlotIndex = 0; SlotIndex < Slots.Num(); ++SlotIndex)
		{
			if (Slots[SlotIndex].MaterialInterface)
			{
				Layer.HISMCmp->SetMaterial(SlotIndex, Slots[SlotIndex].MaterialInterface);
			}
		}

		Layer.HISMCmp->SetNumCustomDataFloats(FCrowdCellInstanceData::CustomDataCount);
		Layer.HISMCmp->SetCullDistances(0.0f, CullDistance);
		if (PreallocateCount > 0)
		{
			Layer.HISMCmp->PreAllocateInstancesMemory(PreallocateCount);
		}

		if (!Row->AnimMetaTable)
		{
			UE_LOG(LogTemp, Warning, TEXT("[UnitHISM] Layer %d has null AnimMetaTable."), LayerIndex);
		}
		ParseAnimTable(Row->AnimMetaTable, Layer);

		Layers.Add(MoveTemp(Layer));
		++LayerIndex;
	}

	if (Layers.Num() == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("[UnitHISM] SetupHISM failed: no valid layers in '%s'."), *LayerTable->GetName());
		return false;
	}

	UE_LOG(LogTemp, Log, TEXT("[UnitHISM] SetupHISM ok: %d layers from '%s' (Cull=%.0f, Prealloc=%d)."),
		Layers.Num(), *LayerTable->GetName(), CullDistance, PreallocateCount);
	return true;
}

int32 UUnitHISMComponent::AddCell(const FTransform& Transform, int32 AnimIndex, float PlayRate, bool bLoop, float StartTimeOffset)
{
	if (!AreLayersReady())
	{
		UE_LOG(LogTemp, Warning, TEXT("[UnitHISM] AddCell failed: layers not ready. Call SetupHISM first."));
		return INDEX_NONE;
	}

	FCrowdCellInstanceData Data;
	Data.AnimIndex = AnimIndex;
	Data.PlayRate = PlayRate;
	Data.bLoop = bLoop;
	const float BasePhase = bLoop ? (FMath::FRand() * 10.0f) : -GetAnimClock(GetWorld());
	Data.Phase = BasePhase + StartTimeOffset;
	Data.StartTime = Data.Phase;

	int32 NewIdx = INDEX_NONE;
	for (int32 L = 0; L < Layers.Num(); ++L)
	{
		const int32 Idx = Layers[L].HISMCmp->AddInstance(Transform, true);
		if (L == 0)
		{
			NewIdx = Idx;
		}
		else if (Idx != NewIdx)
		{
			UE_LOG(LogTemp, Error, TEXT("[UnitHISM] AddCell index mismatch layer0=%d layer%d=%d"), NewIdx, L, Idx);
			ClearCells();
			return INDEX_NONE;
		}
	}

	CellsData.Add(Data);
	CellTransforms.Add(Transform);
	PushCellCustomData(NewIdx, true);
	return NewIdx;
}

void UUnitHISMComponent::BatchAddCells(const TArray<FTransform>& Transforms, int32 AnimIndex, float PlayRate, bool bLoop)
{
	if (!AreLayersReady() || Transforms.Num() == 0)
	{
		return;
	}

	const int32 StartIdx = CellsData.Num();
	const int32 AddCount = Transforms.Num();
	CellsData.Reserve(StartIdx + AddCount);
	CellTransforms.Reserve(StartIdx + AddCount);
	const float BaseTime = GetAnimClock(GetWorld());

	for (FUnitHISMLayerRuntime& Layer : Layers)
	{
		Layer.HISMCmp->AddInstances(Transforms, false, true);
	}

	CellTransforms.Append(Transforms);
	for (int32 i = 0; i < AddCount; ++i)
	{
		FCrowdCellInstanceData Data;
		Data.AnimIndex = AnimIndex;
		Data.PlayRate = PlayRate;
		Data.bLoop = bLoop;
		Data.Phase = bLoop ? (FMath::FRand() * 10.0f) : -BaseTime;
		Data.StartTime = Data.Phase;
		CellsData.Add(Data);
		PushCellCustomData(StartIdx + i, false);
	}

	for (FUnitHISMLayerRuntime& Layer : Layers)
	{
		Layer.HISMCmp->MarkRenderStateDirty();
	}
}

void UUnitHISMComponent::BatchAddCellsByName(const TArray<FTransform>& Transforms, FName AnimName, float PlayRate, bool bLoop)
{
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

bool UUnitHISMComponent::RemoveCell(int32 CellIndex)
{
	if (!AreLayersReady() || !CellsData.IsValidIndex(CellIndex))
	{
		return false;
	}

	for (FUnitHISMLayerRuntime& Layer : Layers)
	{
		Layer.HISMCmp->RemoveInstance(CellIndex);
	}
	CellsData.RemoveAtSwap(CellIndex);
	CellTransforms.RemoveAtSwap(CellIndex);
	return true;
}

void UUnitHISMComponent::ClearCells()
{
	for (FUnitHISMLayerRuntime& Layer : Layers)
	{
		if (Layer.HISMCmp)
		{
			Layer.HISMCmp->ClearInstances();
		}
	}
	CellsData.Empty();
	CellTransforms.Empty();
}

bool UUnitHISMComponent::GetCellData(int32 CellIndex, FCrowdCellInstanceData& OutData) const
{
	if (!CellsData.IsValidIndex(CellIndex))
	{
		return false;
	}
	OutData = CellsData[CellIndex];
	return true;
}

bool UUnitHISMComponent::GetCellTransform(int32 CellIndex, FTransform& OutTransform) const
{
	if (!CellTransforms.IsValidIndex(CellIndex))
	{
		return false;
	}
	OutTransform = CellTransforms[CellIndex];
	return true;
}

bool UUnitHISMComponent::UpdateCellTransform(int32 CellIndex, const FTransform& NewTransform, bool bWorldSpace)
{
	if (!AreLayersReady() || !CellsData.IsValidIndex(CellIndex) || !CellTransforms.IsValidIndex(CellIndex))
	{
		return false;
	}

	FTransform WorldT = NewTransform;
	if (!bWorldSpace)
	{
		UHierarchicalInstancedStaticMeshComponent* Primary = Layers[0].HISMCmp;
		WorldT = NewTransform * Primary->GetComponentTransform();
	}
	CellTransforms[CellIndex] = WorldT;

	bool bAllOk = true;
	for (FUnitHISMLayerRuntime& Layer : Layers)
	{
		if (!Layer.HISMCmp->UpdateInstanceTransform(CellIndex, WorldT, true, true, true))
		{
			bAllOk = false;
		}
	}
	return bAllOk;
}

void UUnitHISMComponent::BatchMoveCells(const FVector& MovementDelta, bool bFaceMovementDirection, bool bTeleport)
{
	if (!AreLayersReady() || CellTransforms.Num() == 0)
	{
		return;
	}

	const bool bHasFacing = bFaceMovementDirection && !MovementDelta.GetSafeNormal2D().IsNearlyZero();
	const FQuat TargetQuat = bHasFacing
		? MovementDelta.GetSafeNormal2D().Rotation().Quaternion()
		: FQuat::Identity;

	for (FTransform& T : CellTransforms)
	{
		T.AddToTranslation(MovementDelta);
		if (bHasFacing)
		{
			T.SetRotation(TargetQuat);
		}
	}

	for (FUnitHISMLayerRuntime& Layer : Layers)
	{
		Layer.HISMCmp->BatchUpdateInstancesTransforms(0, CellTransforms, true, true, bTeleport);
	}
}

void UUnitHISMComponent::PushCellCustomData(int32 Index, bool bMarkRenderStateDirty)
{
	if (!CellsData.IsValidIndex(Index))
	{
		return;
	}

	const FCrowdCellInstanceData& D = CellsData[Index];
	const float SafeRate = FMath::Max(0.0001f, FMath::Abs(D.PlayRate));
	const float SignedRate = D.bLoop ? SafeRate : -SafeRate;

	for (int32 L = 0; L < Layers.Num(); ++L)
	{
		FUnitHISMLayerRuntime& Layer = Layers[L];
		if (!Layer.HISMCmp)
		{
			continue;
		}
		if (!Layer.Animations.IsValidIndex(D.AnimIndex))
		{
			UE_LOG(LogTemp, Warning, TEXT("[UnitHISM] AnimIndex %d OOB on layer %d (anims=%d)"),
				D.AnimIndex, L, Layer.Animations.Num());
			continue;
		}

		float StartFrame = float(Layer.Animations[D.AnimIndex].StartFrame);
		float EndFrame = float(Layer.Animations[D.AnimIndex].EndFrame);
		if (EndFrame < StartFrame)
		{
			EndFrame = StartFrame;
		}

		float CustomData[FCrowdCellInstanceData::CustomDataCount];
		CustomData[FCrowdCellInstanceData::CD_TimeOffset] = D.Phase;
		CustomData[FCrowdCellInstanceData::CD_PlayRate] = SignedRate;
		CustomData[FCrowdCellInstanceData::CD_StartFrame] = StartFrame;
		CustomData[FCrowdCellInstanceData::CD_EndFrame] = EndFrame;
		const bool bDirty = bMarkRenderStateDirty && (L == Layers.Num() - 1);
		Layer.HISMCmp->SetCustomData(Index, MakeArrayView(CustomData), bDirty);
	}
}

int32 UUnitHISMComponent::GetAnimationCount() const
{
	return Layers.Num() > 0 ? Layers[0].Animations.Num() : 0;
}

const TArray<FUnitAnimationTableRow>& UUnitHISMComponent::GetRegisteredAnimations() const
{
	static const TArray<FUnitAnimationTableRow> Empty;
	return Layers.Num() > 0 ? Layers[0].Animations : Empty;
}

int32 UUnitHISMComponent::FindAnimationIndex(FName AnimName) const
{
	if (Layers.Num() == 0)
	{
		return INDEX_NONE;
	}
	const FUnitHISMLayerRuntime& Layer0 = Layers[0];
	if (const int32* Found = Layer0.AnimNameToIndex.Find(AnimName))
	{
		return *Found;
	}

	const FString TargetStr = AnimName.ToString().ToLower();
	for (int32 i = 0; i < Layer0.Animations.Num(); ++i)
	{
		if (Layer0.Animations[i].AnimName.ToString().ToLower().Contains(TargetStr))
		{
			return i;
		}
	}
	return INDEX_NONE;
}

bool UUnitHISMComponent::IsLoopingAnimation(int32 AnimIndex) const
{
	if (Layers.Num() > 0 && Layers[0].Animations.IsValidIndex(AnimIndex))
	{
		return Layers[0].Animations[AnimIndex].bDefaultLoop;
	}
	return true;
}

void UUnitHISMComponent::SetCellAnim(int32 CellIndex, int32 NewAnimIndex, float InPlayRate, bool bInLoop)
{
	if (!CellsData.IsValidIndex(CellIndex))
	{
		return;
	}

	CellsData[CellIndex].AnimIndex = NewAnimIndex;
	CellsData[CellIndex].PlayRate = InPlayRate;
	CellsData[CellIndex].bLoop = bInLoop;
	CellsData[CellIndex].Phase = bInLoop ? (FMath::FRand() * 10.0f) : -GetAnimClock(GetWorld());
	CellsData[CellIndex].StartTime = CellsData[CellIndex].Phase;
	PushCellCustomData(CellIndex, true);
}

void UUnitHISMComponent::SetCellAnimByName(int32 CellIndex, FName AnimName, float InPlayRate, bool bInLoop)
{
	const int32 Idx = FindAnimationIndex(AnimName);
	if (Idx != INDEX_NONE)
	{
		SetCellAnim(CellIndex, Idx, InPlayRate, bInLoop);
	}
}

void UUnitHISMComponent::SetAllCellsAnim(int32 NewAnimIndex, float InPlayRate, bool bInLoop)
{
	const int32 Count = CellsData.Num();
	if (Count == 0)
	{
		return;
	}

	const float CurrentTime = GetAnimClock(GetWorld());
	for (int32 i = 0; i < Count; ++i)
	{
		CellsData[i].AnimIndex = NewAnimIndex;
		CellsData[i].PlayRate = InPlayRate;
		CellsData[i].bLoop = bInLoop;
		CellsData[i].Phase = bInLoop ? (FMath::FRand() * 10.0f) : -CurrentTime;
		CellsData[i].StartTime = CellsData[i].Phase;
		PushCellCustomData(i, false);
	}

	for (FUnitHISMLayerRuntime& Layer : Layers)
	{
		if (Layer.HISMCmp)
		{
			Layer.HISMCmp->MarkRenderStateDirty();
		}
	}

	FName AnimName = NAME_None;
	if (Layers.Num() > 0 && Layers[0].Animations.IsValidIndex(NewAnimIndex))
	{
		AnimName = Layers[0].Animations[NewAnimIndex].AnimName;
	}
	if (AnimName.IsNone())
	{
		AnimName = *FString::Printf(TEXT("Anim_%d"), NewAnimIndex);
	}

	if (AActor* Owner = GetOwner())
	{
		if (UCrowdStatsComponent* Stats = Owner->FindComponentByClass<UCrowdStatsComponent>())
		{
			Stats->NotifyAnimSegment(AnimName);
		}
	}
}

void UUnitHISMComponent::SetAllCellsAnimByName(FName AnimName, float InPlayRate, bool bInLoop)
{
	const int32 Idx = FindAnimationIndex(AnimName);
	if (Idx != INDEX_NONE)
	{
		SetAllCellsAnim(Idx, InPlayRate, bInLoop);
	}
}
