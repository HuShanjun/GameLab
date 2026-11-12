// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/DataTable.h"
#include "UnitHISMComponent.generated.h"

class UHierarchicalInstancedStaticMeshComponent;
class UStaticMesh;
class UMaterialInterface;

/**
 * 动画元数据 DataTable 行：片段名、起止帧、默认是否循环
 */
USTRUCT(BlueprintType)
struct VGCROWDLEGION_API FUnitAnimationTableRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation")
	FName AnimName = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation")
	int32 StartFrame = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation")
	int32 EndFrame = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation")
	bool bDefaultLoop = true;
};

using FUnitAnimationMeta = FUnitAnimationTableRow;

/**
 * 每 Cell 逻辑状态（与 GPU PerInstanceCustomData 对齐）
 * [0]=TimeOffset [1]=PlayRate(>=0 loop, <0 one-shot) [2]=StartFrame [3]=EndFrame
 */
USTRUCT(BlueprintType)
struct VGCROWDLEGION_API FCrowdCellInstanceData
{
	GENERATED_BODY()

	static constexpr int32 CD_TimeOffset = 0;
	static constexpr int32 CD_PlayRate = 1;
	static constexpr int32 CD_StartFrame = 2;
	static constexpr int32 CD_EndFrame = 3;
	static constexpr int32 CustomDataCount = 4;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CrowdCell")
	int32 AnimIndex = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CrowdCell")
	float PlayRate = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CrowdCell")
	bool bLoop = true;

	/** CPU TimeOffset：循环 = 0~10s 随机；单次 = -Now */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CrowdCell")
	float Phase = 0.0f;

	float StartTime = 0.0f;
};

using FSoldierInstanceData = FCrowdCellInstanceData;

/** SetupHISM 层配置表行：一层 HISM = Mesh + 该层动画元数据表 */
USTRUCT(BlueprintType)
struct VGCROWDLEGION_API FUnitHISMLayerTableRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layer")
	TObjectPtr<UStaticMesh> StaticMesh;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layer")
	TObjectPtr<UDataTable> AnimMetaTable;
};

/** 运行时单层：HISM + 已解析动画 */
USTRUCT()
struct FUnitHISMLayerRuntime
{
	GENERATED_BODY()

	UPROPERTY()
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> HISMCmp;

	UPROPERTY()
	TArray<FUnitAnimationTableRow> Animations;

	UPROPERTY(Transient)
	TMap<FName, int32> AnimNameToIndex;
};

/**
 * 多 HISM 层单元基类：层表驱动 Setup、Cell CRUD、统一相位推送 GPU
 */
UCLASS(Abstract, ClassGroup = (Crowd))
class VGCROWDLEGION_API UUnitHISMComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UUnitHISMComponent();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CrowdLegion | Setup")
	float CullDistance = 20000.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "CrowdLegion | Setup")
	TObjectPtr<UDataTable> LayerSetupTable;

	/** 按层表重建 HISM；成功返回 true */
	UFUNCTION(BlueprintCallable, Category = "CrowdLegion | Setup")
	bool SetupHISM(UDataTable* LayerTable, float InCullDistance, int32 PreallocateCount = 0);

	UFUNCTION(BlueprintPure, Category = "CrowdLegion | HISM")
	int32 GetLayerCount() const { return Layers.Num(); }

	UFUNCTION(BlueprintPure, Category = "CrowdLegion | HISM")
	UHierarchicalInstancedStaticMeshComponent* GetHISM(int32 LayerIndex) const;

	UFUNCTION(BlueprintCallable, Category = "CrowdLegion | Cell")
	int32 AddCell(const FTransform& Transform, int32 AnimIndex = 0, float PlayRate = 1.0f, bool bLoop = true, float StartTimeOffset = 0.0f);

	UFUNCTION(BlueprintCallable, Category = "CrowdLegion | Cell")
	void BatchAddCells(const TArray<FTransform>& Transforms, int32 AnimIndex = 0, float PlayRate = 1.0f, bool bLoop = true);

	UFUNCTION(BlueprintCallable, Category = "CrowdLegion | Cell")
	void BatchAddCellsByName(const TArray<FTransform>& Transforms, FName AnimName = NAME_None, float PlayRate = 1.0f, bool bLoop = true);

	UFUNCTION(BlueprintCallable, Category = "CrowdLegion | Cell")
	bool RemoveCell(int32 CellIndex);

	UFUNCTION(BlueprintCallable, Category = "CrowdLegion | Cell")
	void ClearCells();

	UFUNCTION(BlueprintPure, Category = "CrowdLegion | Cell")
	int32 GetCellCount() const { return CellsData.Num(); }

	UFUNCTION(BlueprintPure, Category = "CrowdLegion | Cell")
	bool GetCellData(int32 CellIndex, FCrowdCellInstanceData& OutData) const;

	UFUNCTION(BlueprintCallable, Category = "CrowdLegion | Cell")
	bool UpdateCellTransform(int32 CellIndex, const FTransform& NewTransform, bool bWorldSpace = true);

	UFUNCTION(BlueprintCallable, Category = "CrowdLegion | Cell")
	void BatchMoveCells(const FVector& MovementDelta, const TArray<int32>& IgnoredIndices, bool bFaceMovementDirection = true, bool bTeleport = false);

	UFUNCTION(BlueprintCallable, Category = "CrowdLegion | Animation")
	void SetCellAnim(int32 CellIndex, int32 NewAnimIndex, float InPlayRate = 1.0f, bool bInLoop = true);

	UFUNCTION(BlueprintCallable, Category = "CrowdLegion | Animation")
	void SetCellAnimByName(int32 CellIndex, FName AnimName, float InPlayRate = 1.0f, bool bInLoop = true);

	UFUNCTION(BlueprintCallable, Category = "CrowdLegion | Animation")
	void SetAllCellsAnim(int32 NewAnimIndex, float InPlayRate = 1.0f, bool bInLoop = true);

	UFUNCTION(BlueprintCallable, Category = "CrowdLegion | Animation")
	void SetAllCellsAnimByName(FName AnimName, float InPlayRate = 1.0f, bool bInLoop = true);

	UFUNCTION(BlueprintPure, Category = "CrowdLegion | Animation")
	int32 GetAnimationCount() const;

	UFUNCTION(BlueprintPure, Category = "CrowdLegion | Animation")
	int32 FindAnimationIndex(FName AnimName) const;

	UFUNCTION(BlueprintPure, Category = "CrowdLegion | Animation")
	bool IsLoopingAnimation(int32 AnimIndex) const;

	const TArray<FCrowdCellInstanceData>& GetAllInstanceData() const { return CellsData; }

	/** Layer0 动画列表（供 Stats 等只读） */
	const TArray<FUnitAnimationTableRow>& GetRegisteredAnimations() const;

	/** 由 Mesh+AnimMeta 行组装临时层表（供 Actor 调用） */
	static UDataTable* CreateTransientLayerTable(UObject* Outer, const TArray<FUnitHISMLayerTableRow>& Rows);

protected:
	void DestroyLayers();
	void ParseAnimTable(UDataTable* Table, FUnitHISMLayerRuntime& OutLayer);
	void PushCellCustomData(int32 Index, bool bMarkRenderStateDirty = true);
	bool AreLayersReady() const;
	static float GetAnimClock(const UWorld* World);

	UPROPERTY()
	TArray<FUnitHISMLayerRuntime> Layers;

	UPROPERTY()
	TArray<FCrowdCellInstanceData> CellsData;
};
