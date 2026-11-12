// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "CrowdUnitComponent.h"
#include "CavalryUnitComponent.generated.h"

class UHierarchicalInstancedStaticMeshComponent;
class UStaticMesh;
class UMaterialInterface;
class UDataTable;

/**
 * 骑兵双 HISM 联动驱动组件 (Cavalry Unit Component)
 *
 * 架构特点：
 * - 内部同时管理两个独立的 HISM 组件：马匹 (HorseHISMCmp) 与骑手 (RiderHISMCmp)
 * - 双方实例下标严格 1:1 对齐，共用相同的空间变换 (Transform)
 * - 动画驱动接口与 UCrowdUnitComponent 保持一致，通过同名动作名（如 "Idle", "Run"）分别在两张数据表中检索起止帧
 * - 马匹与骑手强制共享相同的动画相位 (TimeOffset / Phase) 与播放速率 (PlayRate / SignedRate)，实现毫秒级绝对同步
 */
UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class VGCROWDLEGION_API UCavalryUnitComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UCavalryUnitComponent();

	/** 马匹分层实例化网格组件 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cavalry|Components")
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> HorseHISMCmp;

	/** 骑手分层实例化网格组件 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cavalry|Components")
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> RiderHISMCmp;

	/** 马匹静态网格体 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cavalry|Mesh")
	TObjectPtr<UStaticMesh> HorseMesh;

	/** 马匹材质列表 (留空则从 HorseMesh 槽位自动读取) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cavalry|Mesh")
	TArray<TObjectPtr<UMaterialInterface>> HorseMaterials;

	/** 骑手静态网格体 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cavalry|Mesh")
	TObjectPtr<UStaticMesh> RiderMesh;

	/** 骑手材质列表 (留空则从 RiderMesh 槽位自动读取) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cavalry|Mesh")
	TArray<TObjectPtr<UMaterialInterface>> RiderMaterials;

	/** 马匹动画元数据表 (FUnitAnimationTableRow) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cavalry|Animation")
	TObjectPtr<UDataTable> HorseAnimMetaDataTable;

	/** 骑手动画元数据表 (FUnitAnimationTableRow) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cavalry|Animation")
	TObjectPtr<UDataTable> RiderAnimMetaDataTable;

	/** HISM 视距剔除距离 (默认 10000 厘米) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cavalry|Culling")
	float CullDistance = 10000.0f;

	/**
	 * 初始化配置马匹与骑手的 HISM 组件
	 * @param InHorseMesh 马匹 StaticMesh
	 * @param InHorseMaterials 马匹材质数组 (可为空)
	 * @param InHorseAnimTable 马匹动画元数据数据表
	 * @param InRiderMesh 骑手 StaticMesh
	 * @param InRiderMaterials 骑手材质数组 (可为空)
	 * @param InRiderAnimTable 骑手动画元数据数据表
	 * @param PreallocateCount 预分配实例数量 (可选)
	 */
	UFUNCTION(BlueprintCallable, Category = "Cavalry|Setup")
	void SetupCavalryHISM(
		UStaticMesh* InHorseMesh,
		const TArray<UMaterialInterface*>& InHorseMaterials,
		UDataTable* InHorseAnimTable,
		UStaticMesh* InRiderMesh,
		const TArray<UMaterialInterface*>& InRiderMaterials,
		UDataTable* InRiderAnimTable,
		int32 PreallocateCount = 0);

	/** 从数据表加载马匹与骑手的动画元数据 */
	UFUNCTION(BlueprintCallable, Category = "Cavalry|Animation")
	void LoadAnimationsFromDataTables(UDataTable* InHorseTable, UDataTable* InRiderTable);

	/**
	 * 添加一个骑兵单位 (同步添加马匹与骑手两个 HISM 实例，共享同一 Transform 与动画相位)
	 * @param Transform 世界空间变换
	 * @param AnimName 初始动作名称 (若两表都有则同名匹配，否则使用首个动作)
	 * @param PlayRate 播放速率
	 * @param bLoop 是否循环
	 * @param StartTimeOffset 额外时间相位偏移 (可选)
	 * @return 新增骑兵的实例索引 (两个 HISM 下标严格一致)
	 */
	UFUNCTION(BlueprintCallable, Category = "Cavalry|Instance")
	int32 AddCell(const FTransform& Transform, FName AnimName = NAME_None, float PlayRate = 1.0f, bool bLoop = true, float StartTimeOffset = 0.0f);

	/** 按动画数组索引添加骑兵单位 */
	UFUNCTION(BlueprintCallable, Category = "Cavalry|Instance")
	int32 AddCellByIndex(const FTransform& Transform, int32 AnimIndex = 0, float PlayRate = 1.0f, bool bLoop = true, float StartTimeOffset = 0.0f);

	/** 批量添加骑兵单位 (按动画索引，马匹与骑手成对快速压入) */
	UFUNCTION(BlueprintCallable, Category = "Cavalry|Instance")
	void BatchAddCells(const TArray<FTransform>& Transforms, int32 AnimIndex = 0, float PlayRate = 1.0f, bool bLoop = true);

	/** 批量添加骑兵单位 (按动作名称) */
	UFUNCTION(BlueprintCallable, Category = "Cavalry|Instance")
	void BatchAddCellsByName(const TArray<FTransform>& Transforms, FName AnimName = NAME_None, float PlayRate = 1.0f, bool bLoop = true);
	/** 移除指定索引的骑兵 (马匹与骑手双端同步移除) */
	UFUNCTION(BlueprintCallable, Category = "Cavalry|Instance")
	bool RemoveCell(int32 CellIndex);

	/** 清空所有骑兵实例 */
	UFUNCTION(BlueprintCallable, Category = "Cavalry|Instance")
	void ClearCells();

	/** 获取指定骑兵的动画状态数据 */
	UFUNCTION(BlueprintCallable, Category = "Cavalry|Instance")
	bool GetCellData(int32 CellIndex, FCrowdCellInstanceData& OutData) const;

	/** 更新单个骑兵的世界变换 (马匹与骑手同步更新) */
	UFUNCTION(BlueprintCallable, Category = "Cavalry|Instance")
	bool UpdateCellTransform(int32 CellIndex, const FTransform& NewTransform, bool bWorldSpace = true);

	/** 批量移动骑兵阵列 (马匹与骑手以相同位移量同步更新) */
	UFUNCTION(BlueprintCallable, Category = "Cavalry|Instance")
	void BatchMoveCells(const FVector& MovementDelta, const TArray<int32>& IgnoredIndices, bool bFaceMovementDirection = false, bool bTeleport = false);

	/**
	 * 按动作名称切换骑兵动画 (马匹与骑手分别在各自数据表中查找同名动作，强锁定相同的相位与播放速率)
	 */
	UFUNCTION(BlueprintCallable, Category = "Cavalry|Animation")
	void SetCellAnimByName(int32 CellIndex, FName AnimName, float InPlayRate = 1.0f, bool bInLoop = true);

	/** 按动画数组索引切换骑兵动画 */
	UFUNCTION(BlueprintCallable, Category = "Cavalry|Animation")
	void SetCellAnim(int32 CellIndex, int32 NewAnimIndex, float InPlayRate = 1.0f, bool bInLoop = true);
	/** 批量修改所有骑兵的动画状态 (按动画索引) */
	UFUNCTION(BlueprintCallable, Category = "Cavalry|Animation")
	void SetAllCellsAnim(int32 NewAnimIndex, float InPlayRate = 1.0f, bool bInLoop = true);

	/** 批量修改所有骑兵的动画状态 (按动画片段名称) */
	UFUNCTION(BlueprintCallable, Category = "Cavalry|Animation")
	void SetAllCellsAnimByName(FName AnimName, float InPlayRate = 1.0f, bool bInLoop = true);

	/** 查找动画名称对应的索引 */
	UFUNCTION(BlueprintPure, Category = "Cavalry|Animation")
	int32 FindAnimationIndex(FName AnimName) const;

	/** 查询当前骑兵总数量 */
	UFUNCTION(BlueprintPure, Category = "Cavalry|Instance")
	int32 GetCellCount() const { return CellsData.Num(); }

	/** 查询指定动画索引是否默认为循环动作 */
	UFUNCTION(BlueprintPure, Category = "Cavalry|Animation")
	bool IsLoopingAnimation(int32 AnimIndex) const;

private:
	/** 向双方 HISM 写入对齐的 PerInstanceCustomData */
	void PushCellCustomData(int32 Index, bool bMarkRenderStateDirty);

	/** 辅助创建/配置 HISM 组件 */
	UHierarchicalInstancedStaticMeshComponent* EnsureHISMComponent(
		TObjectPtr<UHierarchicalInstancedStaticMeshComponent>& InOutComponent,
		const FName& ComponentName,
		UStaticMesh* Mesh,
		const TArray<TObjectPtr<UMaterialInterface>>& Materials);

	/** 解析单个数据表到内存数组与名称映射 */
	static void ParseDataTable(
		UDataTable* Table,
		TArray<FUnitAnimationTableRow>& OutAnimations,
		TMap<FName, int32>& OutNameToIndex);

private:
	/** 骑兵逻辑层状态数据 (马匹与骑手共享同一套逻辑状态) */
	UPROPERTY()
	TArray<FCrowdCellInstanceData> CellsData;

	/** 马匹动画列表 */
	UPROPERTY()
	TArray<FUnitAnimationTableRow> HorseAnimations;

	/** 骑手动画列表 */
	UPROPERTY()
	TArray<FUnitAnimationTableRow> RiderAnimations;

	/** 马匹动作名称到索引的映射 */
	TMap<FName, int32> HorseAnimNameToIndex;

	/** 骑手动作名称到索引的映射 */
	TMap<FName, int32> RiderAnimNameToIndex;
};
