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
