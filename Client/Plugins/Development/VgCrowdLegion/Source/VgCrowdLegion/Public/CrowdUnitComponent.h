// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/DataTable.h"
#include "CrowdUnitComponent.generated.h"

class UHierarchicalInstancedStaticMeshComponent;
class UStaticMesh;
class UMaterialInterface;
class UAnimToTextureDataAsset;

/**
 * 动画元数据 DataTable 数据表行结构体
 * 记录动画片段名称、起止帧与循环属性，完全由数据资产驱动
 */
USTRUCT(BlueprintType)
struct VGCROWDLEGION_API FUnitAnimationTableRow : public FTableRowBase
{
	GENERATED_BODY()

	/** 动画片段名称 (如 Idle, Run, Attack, Death) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation")
	FName AnimName = NAME_None;

	/** 在全局 VAT 贴图上的起始帧偏移 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation")
	int32 StartFrame = 0;

	/** 动画总帧长 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation")
	int32 NumFrames = 1;

	/** 是否循环播放 (true 为循环, false 为单次定格) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation")
	bool bDefaultLoop = true;
};

/** 保持向下兼容的类型别名 */
using FUnitAnimationMeta = FUnitAnimationTableRow;

/**
 * 每 Cell 实例动画状态数据 (与 GPU PerInstanceCustomData 严格对齐)
 * CustomData[0]:TimeOffset
 * CustomData[1]:PlayRate 
 * CustomData[2]:StartFrame                                                                                                                    
 * CustomData[3]:EndFrame
 */
USTRUCT(BlueprintType)
struct VGCROWDLEGION_API FCrowdCellInstanceData
{
	GENERATED_BODY()

	// CustomData 布局约定 (与 GetCrowdLegionFrame / M_Crowd_Legion PICD 下标对齐):
	// [0]=TimeOffset [1]=PlayRate(>=0 loop, <0 one-shot) [2]=StartFrame [3]=EndFrame
	static constexpr int32 CD_TimeOffset = 0;
	static constexpr int32 CD_PlayRate   = 1;
	static constexpr int32 CD_StartFrame = 2;
	static constexpr int32 CD_EndFrame   = 3;
	static constexpr int32 CustomDataCount = 4;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CrowdCell")
	int32 AnimIndex = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CrowdCell")
	float PlayRate = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CrowdCell")
	bool bLoop = true;

	/** CPU 侧存储的 TimeOffset：循环 = 0~10s 随机；单次 = -Now 使 Time+TimeOffset 从 0 起播 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CrowdCell")
	float Phase = 0.0f;

	/** 兼容别名：与 Phase 同值 (stored TimeOffset) */
	float StartTime = 0.0f;
};

/** 兼容旧代码引用的结构体别名 */
using FSoldierInstanceData = FCrowdCellInstanceData;

/**
 * 通用海量单元组件 (UCrowdUnitComponent)
 * 职责：专注底层 HISM 实例管理、4 槽位 CustomData 动画驱动、Cell 单个与批量增删改移。
 * 不包含任何具体游戏业务逻辑或状态机，纯数据驱动。
 */
UCLASS(ClassGroup = (Crowd), meta = (BlueprintSpawnableComponent))
class VGCROWDLEGION_API UCrowdUnitComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UCrowdUnitComponent();

public:
	// --- 核心配置 ---

	/** 渲染网格体 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CrowdLegion | Setup")
	TObjectPtr<UStaticMesh> Mesh;

	/** 配套顶点动画材质 (需包含 4 个 PerInstanceCustomData 算帧网络) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CrowdLegion | Setup")
	TObjectPtr<UMaterialInterface> Material;

	/** 动画元数据数据表 (描述各动画起止帧与循环属性) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CrowdLegion | Animation")
	TObjectPtr<UDataTable> AnimMetaDataTable;

	/** 最大视距剔除距离 (厘米) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CrowdLegion | Setup")
	float CullDistance = 20000.0f;

public:
	// --- HISM 初始化与配置 ---

	/** 初始化或更新 HISM 组件渲染网格体与材质 */
	UFUNCTION(BlueprintCallable, Category = "CrowdLegion | Setup")
	void SetupHISM(UStaticMesh* InMesh = nullptr, UMaterialInterface* InMaterial = nullptr, int32 PreallocateCount = 0);

	/** 从指定的 DataTable 加载动画元数据表 */
	UFUNCTION(BlueprintCallable, Category = "CrowdLegion | Animation")
	void LoadAnimationsFromDataTable(UDataTable* InTable = nullptr);

	// --- Cell 实例生命周期操作 (增删改) ---

	/** 单个添加 Cell 实例 */
	UFUNCTION(BlueprintCallable, Category = "CrowdLegion | Cell")
	int32 AddCell(const FTransform& Transform, int32 AnimIndex = 0, float PlayRate = 1.0f, bool bLoop = true, float StartTimeOffset = 0.0f);

	/** 批量添加 Cell 实例 */
	UFUNCTION(BlueprintCallable, Category = "CrowdLegion | Cell")
	void BatchAddCells(const TArray<FTransform>& Transforms, int32 AnimIndex = 0, float PlayRate = 1.0f, bool bLoop = true);

	/** 移除指定索引的 Cell */
	UFUNCTION(BlueprintCallable, Category = "CrowdLegion | Cell")
	bool RemoveCell(int32 CellIndex);

	/** 清空所有 Cell 实例 */
	UFUNCTION(BlueprintCallable, Category = "CrowdLegion | Cell")
	void ClearCells();

	/** 获取当前有效 Cell 实例总数 */
	UFUNCTION(BlueprintPure, Category = "CrowdLegion | Cell")
	int32 GetCellCount() const { return CellsData.Num(); }

	/** 获取指定 Cell 的动画状态数据副本 */
	UFUNCTION(BlueprintPure, Category = "CrowdLegion | Cell")
	bool GetCellData(int32 CellIndex, FCrowdCellInstanceData& OutData) const;

	/** 更新单个 Cell 的空间变换 */
	UFUNCTION(BlueprintCallable, Category = "CrowdLegion | Cell")
	bool UpdateCellTransform(int32 CellIndex, const FTransform& NewTransform, bool bWorldSpace = true);

	/** 批量单帧位移：沿 MovementDelta 平移所有单元 (支持排除指定 Cell，如已阵亡单位) */
	UFUNCTION(BlueprintCallable, Category = "CrowdLegion | Cell")
	void BatchMoveCells(const FVector& MovementDelta, const TArray<int32>& IgnoredIndices, bool bFaceMovementDirection = true, bool bTeleport = false);

	// --- Cell 动画状态修改 ---

	/** 修改指定 Cell 的动画状态 (按动作索引) */
	UFUNCTION(BlueprintCallable, Category = "CrowdLegion | Animation")
	void SetCellAnim(int32 CellIndex, int32 NewAnimIndex, float InPlayRate = 1.0f, bool bInLoop = true);

	/** 修改指定 Cell 的动画状态 (按动画片段名称) */
	UFUNCTION(BlueprintCallable, Category = "CrowdLegion | Animation")
	void SetCellAnimByName(int32 CellIndex, FName AnimName, float InPlayRate = 1.0f, bool bInLoop = true);

	/** 批量修改所有 Cell 的动画状态 (按动作索引) */
	UFUNCTION(BlueprintCallable, Category = "CrowdLegion | Animation")
	void SetAllCellsAnim(int32 NewAnimIndex, float InPlayRate = 1.0f, bool bInLoop = true);

	/** 批量修改所有 Cell 的动画状态 (按动画片段名称) */
	UFUNCTION(BlueprintCallable, Category = "CrowdLegion | Animation")
	void SetAllCellsAnimByName(FName AnimName, float InPlayRate = 1.0f, bool bInLoop = true);

	// --- 只读数据查询接口 (供独立统计分析组件等使用) ---

	/** 获取当前全部已注册动画数量 */
	UFUNCTION(BlueprintPure, Category = "CrowdLegion | Animation")
	int32 GetAnimationCount() const { return Animations.Num(); }

	/** 根据动作名称查找对应动画索引 (未找到返回 INDEX_NONE) */
	UFUNCTION(BlueprintPure, Category = "CrowdLegion | Animation")
	int32 FindAnimationIndex(FName AnimName) const;

	/** 只读获取全部 Cell 实例数据数组 */
	const TArray<FCrowdCellInstanceData>& GetAllInstanceData() const { return CellsData; }

	/** 只读获取已注册动画元数据数组 */
	const TArray<FUnitAnimationTableRow>& GetRegisteredAnimations() const { return Animations; }

	/** 获取底层的 HISM 组件 */
	UFUNCTION(BlueprintPure, Category = "CrowdLegion | HISM")
	UHierarchicalInstancedStaticMeshComponent* GetHISMCmp() const { return HISMCmp; }

private:
	/** 将单个 Cell 的动画数据同步至 GPU CustomData */
	void PushCellCustomData(int32 Index, bool bMarkRenderStateDirty = true);

private:
	/** HISM 组件：承载海量网格实例，合并为一个 Draw Call */
	UPROPERTY(VisibleAnywhere, Category = "CrowdLegion | HISM")
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> HISMCmp;

	/** 本地维护的 Cell 动画状态数组，与 HISM 实例索引 1:1 对齐 */
	UPROPERTY()
	TArray<FCrowdCellInstanceData> CellsData;

	/** 从 DataTable 加载的动作元数据列表 */
	UPROPERTY(VisibleAnywhere, Category = "CrowdLegion | Animation")
	TArray<FUnitAnimationTableRow> Animations;

	/** 动作名称到索引的快速查找表 */
	UPROPERTY(Transient)
	TMap<FName, int32> AnimNameToIndex;
};

/** 保持向前兼容的类名别名 */
using UCrowdUnitManager = UCrowdUnitComponent;
