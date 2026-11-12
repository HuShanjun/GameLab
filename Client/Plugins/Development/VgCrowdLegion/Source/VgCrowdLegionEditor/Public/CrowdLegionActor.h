// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CrowdUnitComponent.h"
#include "CrowdLegionActor.generated.h"

class USkeletalMesh;
class UAnimToTextureDataAsset;
class UCrowdStatsComponent;
class UCrowdUnitComponent;
class UCavalryUnitComponent;
/**
 * 军团编辑器视口 Actor (纯 Editor 逻辑)
 * 组合 UCrowdUnitComponent 与 UCrowdStatsComponent 提供场景视口交互与可视化调试测试
 */
UCLASS(Blueprintable, BlueprintType)
class VGCROWDLEGIONEDITOR_API ACrowdLegionActor : public AActor
{
	GENERATED_BODY()

public:
	ACrowdLegionActor();

	virtual void Tick(float DeltaTime) override;

protected:
	virtual void BeginPlay() override;
public:
	// --- 配置属性 ---
	/** 是否在 BeginPlay 时自动初始化生成军团 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CrowdLegion | Config")
	bool bAutoInitOnBeginPlay = true;

	/** 军团核心渲染与实例驱动组件 (通用无业务逻辑) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "CrowdLegion", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UCrowdUnitComponent> CrowdComponent;

	/** 军团实时性能与姿态统计组件 (独立负责骨骼统计、Pose去重及 HUD 展示) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "CrowdLegion", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UCrowdStatsComponent> StatsComponent;

	/** 骑兵核心驱动组件 (管理马匹与骑手双 HISM) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "CrowdLegion | Cavalry", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UCavalryUnitComponent> CavalryComponent;
	// --- 军团生成配置 (从组件移至 Actor) ---
	/** 同屏单位生成数量 (默认 1000 实例) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CrowdLegion | CrowdSetup", meta = (ClampMin = "1", ClampMax = "100000"))
	int32 NumUnits = 1000;

	/** 单位随机散布半径 (厘米) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CrowdLegion | CrowdSetup", meta = (ClampMin = "100.0"))
	float SpreadRadius = 3000.0f;

	/** 是否给生成的单位赋予随机偏航角 (Yaw) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CrowdLegion | CrowdSetup")
	bool bRandomYaw = true;

	/** A2T 核心数据资产 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CrowdLegion | CrowdSetup")
	TObjectPtr<UAnimToTextureDataAsset> DataAsset;

	// --- 动作状态配置 (在 Actor 中配置映射，不硬编码在通用组件中) ---
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CrowdLegion | AnimConfig")
	int32 IdleAnimIndex = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CrowdLegion | AnimConfig")
	int32 RunAnimIndex = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CrowdLegion | AnimConfig")
	int32 AttackAnimIndex = 2;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CrowdLegion | AnimConfig")
	int32 DeathAnimIndex = 3;
	// --- 骑兵生成配置 ---
	/** 马匹静态网格体 (可选；若未指定则自动从 HorseDataAsset 读取) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CrowdLegion | CavalrySetup")
	TObjectPtr<UStaticMesh> HorseMesh;

	/** 马匹材质列表 (留空则从网格插槽自动填充) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CrowdLegion | CavalrySetup")
	TArray<TObjectPtr<UMaterialInterface>> HorseMaterials;

	/** 马匹 A2T 数据资产 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CrowdLegion | CavalrySetup")
	TObjectPtr<UAnimToTextureDataAsset> HorseDataAsset;

	/** 马匹动画元数据数据表 (FUnitAnimationTableRow) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CrowdLegion | CavalrySetup")
	TObjectPtr<UDataTable> HorseAnimMetaDataTable;

	/** 骑手静态网格体 (可选；若未指定则自动从 RiderDataAsset 读取) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CrowdLegion | CavalrySetup")
	TObjectPtr<UStaticMesh> RiderMesh;

	/** 骑手材质列表 (留空则从网格插槽自动填充) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CrowdLegion | CavalrySetup")
	TArray<TObjectPtr<UMaterialInterface>> RiderMaterials;

	/** 骑手 A2T 数据资产 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CrowdLegion | CavalrySetup")
	TObjectPtr<UAnimToTextureDataAsset> RiderDataAsset;

	/** 骑手动画元数据数据表 (FUnitAnimationTableRow) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CrowdLegion | CavalrySetup")
	TObjectPtr<UDataTable> RiderAnimMetaDataTable;

	/** 骑兵生成数量 (默认 200 骑) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CrowdLegion | CavalrySetup", meta = (ClampMin = "1", ClampMax = "50000"))
	int32 NumCavalryUnits = 200;

	/** 骑兵随机散布半径 (厘米) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CrowdLegion | CavalrySetup", meta = (ClampMin = "100.0"))
	float CavalrySpreadRadius = 2500.0f;

	/** 骑兵待机动作名称 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CrowdLegion | CavalryAnimConfig")
	FName CavalryIdleAnimName = TEXT("Idle");

	/** 骑兵奔跑动作名称 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CrowdLegion | CavalryAnimConfig")
	FName CavalryRunAnimName = TEXT("Run");

	/** 骑兵攻击动作名称 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CrowdLegion | CavalryAnimConfig")
	FName CavalryAttackAnimName = TEXT("Attack");

	/** 骑兵阵亡动作名称 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CrowdLegion | CavalryAnimConfig")
	FName CavalryDeathAnimName = TEXT("Death");
	/** 用于导出 StaticMesh 的源骨骼网格体 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CrowdLegion | MeshExport")
	TObjectPtr<USkeletalMesh> SourceSkeletalMesh;

	/** 右上角调试 HUD 字体大小快捷调节 (范围 8~72，默认 16) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CrowdLegion | Stats", meta = (ClampMin = "8", ClampMax = "72"))
	int32 DebugStatsFontSize = 16;

	/** 快捷调整右上角 HUD 字体大小 */
	UFUNCTION(CallInEditor, BlueprintCallable, Category = "CrowdLegion | Stats")
	void SetStatsFontSize(int32 NewSize);

	// --- 细节面板快速操作 (CallInEditor) ---

	/** 一键导出 StaticMesh、合并多贴图至 Atlas、创建配套材质并烘焙 A2T */
	UFUNCTION(CallInEditor, BlueprintCallable, Category = "CrowdLegion | Actions")
	void ExportAndBakeA2TLegion();

	/** 立即生成/重建海量军团 (1000 实例，1 个 Draw Call) */
	UFUNCTION(CallInEditor, BlueprintCallable, Category = "CrowdLegion | Actions")
	void SpawnLegion();

	/** 将全体军团士兵切换为待机动作 (Idle) */
	UFUNCTION(CallInEditor, BlueprintCallable, Category = "CrowdLegion | Actions")
	void SetLegionIdle();

	/** 将全体军团士兵切换为奔跑动作 (Run) */
	UFUNCTION(CallInEditor, BlueprintCallable, Category = "CrowdLegion | Actions")
	void SetLegionRun();

	/** 将全体军团士兵切换为攻击动作 (Attack) */
	UFUNCTION(CallInEditor, BlueprintCallable, Category = "CrowdLegion | Actions")
	void SetLegionAttack();

	/** 将全体军团士兵切换为阵亡动作 (Death) */
	UFUNCTION(CallInEditor, BlueprintCallable, Category = "CrowdLegion | Actions")
	void KillAllLegion();

	/** 清空当前场景中生成的军团实例 */
	UFUNCTION(CallInEditor, BlueprintCallable, Category = "CrowdLegion | Actions")
	void ClearLegion();

	/** 让军团全军向前持续行军推进 2000 厘米 (自动切 Run 奔跑，到达自动切 Idle 待机) */
	UFUNCTION(CallInEditor, BlueprintCallable, Category = "CrowdLegion | Actions")
	void MarchLegionForward();

	/** 停止军团当前的行军移动 */
	UFUNCTION(CallInEditor, BlueprintCallable, Category = "CrowdLegion | Actions")
	void StopLegionMarch();

	// --- 骑兵生成与操作接口 ---

	/** 立即生成/重建海量骑兵军团 (详细面板 CallInEditor 快捷按钮 / 蓝图调用) */
	UFUNCTION(CallInEditor, BlueprintCallable, Category = "CrowdLegion | CavalryActions")
	void SpawnCavalryLegion();

	/** 自定义参数批量生成骑兵阵列 */
	UFUNCTION(BlueprintCallable, Category = "CrowdLegion | CavalryActions")
	void SpawnCavalryLegionCustom(int32 InNumUnits, float InSpreadRadius, FName InInitialAnim = TEXT("Idle"));

	/** 单独添加一个骑兵单位 (在指定 Transform 处生成马匹与骑手，返回实例索引) */
	UFUNCTION(BlueprintCallable, Category = "CrowdLegion | CavalryActions")
	int32 AddCavalryUnit(const FTransform& Transform, FName AnimName = TEXT("Idle"), float PlayRate = 1.0f, bool bLoop = true);

	/** 将全体骑兵切换为待机动作 */
	UFUNCTION(CallInEditor, BlueprintCallable, Category = "CrowdLegion | CavalryActions")
	void SetCavalryIdle();

	/** 将全体骑兵切换为奔跑动作 */
	UFUNCTION(CallInEditor, BlueprintCallable, Category = "CrowdLegion | CavalryActions")
	void SetCavalryRun();

	/** 将全体骑兵切换为攻击动作 */
	UFUNCTION(CallInEditor, BlueprintCallable, Category = "CrowdLegion | CavalryActions")
	void SetCavalryAttack();

	/** 将全体骑兵切换为阵亡动作 */
	UFUNCTION(CallInEditor, BlueprintCallable, Category = "CrowdLegion | CavalryActions")
	void KillAllCavalry();

	/** 清空场景中的骑兵实例 */
	UFUNCTION(CallInEditor, BlueprintCallable, Category = "CrowdLegion | CavalryActions")
	void ClearCavalry();

	/** 获取骑兵组件引用 */
	UFUNCTION(BlueprintPure, Category = "CrowdLegion | Cavalry")
	UCavalryUnitComponent* GetCavalryComponent() const { return CavalryComponent.Get(); }
	bool IsLoopingAnimation(int32 AnimIndex) const;

private:
	UPROPERTY(Transient)
	bool bIsMarching = false;

	UPROPERTY(Transient)
	FVector MarchTargetLocation = FVector::ZeroVector;

	UPROPERTY(Transient)
	float MarchSpeed = 400.0f;

	UPROPERTY(Transient)
	float MarchAcceptanceRadius = 50.0f;

	FVector GetLegionCenterLocation() const;

private:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "CrowdLegion", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USceneComponent> SceneRoot;
};
