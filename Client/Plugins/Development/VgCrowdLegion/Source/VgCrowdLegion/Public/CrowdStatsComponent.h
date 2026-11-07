// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "CrowdUnitComponent.h"
#include "CrowdStatsComponent.generated.h"

class UCrowdUnitComponent;
class SWidget;

/**
 * 军团实时性能与姿态统计组件 (独立负责骨骼统计、Pose去重分析与 Slate HUD 渲染展示)
 * 遵循单一职责设计，与 UCrowdUnitComponent 解耦
 */
UCLASS(ClassGroup = (Crowd), meta = (BlueprintSpawnableComponent))
class VGCROWDLEGION_API UCrowdStatsComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UCrowdStatsComponent();

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

public:
	/** 目标绑定的单元组件 (若为空则自动在当前 Owner Actor 上查找) */
	UPROPERTY(BlueprintReadWrite, Category = "CrowdLegion | Stats")
	TWeakObjectPtr<UCrowdUnitComponent> CrowdUnitComponent;

	/** 是否在屏幕右上角显示军团实时骨骼与 Pose 统计 HUD */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CrowdLegion | Stats")
	bool bShowDebugStatsHUD = true;

	/** 调试 HUD 字体大小 (运行时支持在 Details 面板随时放大/缩小，范围 8~72，默认 16) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CrowdLegion | Stats", meta = (ClampMin = "8", ClampMax = "72"))
	int32 DebugStatsFontSize = 16;

	/** 运行时动态设置调试 HUD 字体大小 */
	UFUNCTION(BlueprintCallable, Category = "CrowdLegion | Stats")
	void SetDebugStatsFontSize(int32 NewFontSize)
	{
		DebugStatsFontSize = FMath::Clamp(NewFontSize, 8, 72);
	}

	/** 获取单人虚拟骨骼数量 (从绑定的 DataTable 或组件提取) */
	UFUNCTION(BlueprintPure, Category = "CrowdLegion | Stats")
	int32 GetBonesPerUnit() const;

	/** 获取同屏等效骨骼总数 (实例数 * 单人骨骼数，体现 A2T 零 CPU 蒙皮开销驱动数万骨骼的能力) */
	UFUNCTION(BlueprintPure, Category = "CrowdLegion | Stats")
	int32 GetTotalEquivalentBones() const;

	/** 获取当前同屏去重采样独立 Pose 数 (实时根据全军每实例当前瞬间在 GPU 顶点着色器采样的非重复姿态帧统计) */
	UFUNCTION(BlueprintPure, Category = "CrowdLegion | Stats")
	int32 GetActivePoseCount() const;

	/** 获取当前主要播放动作的姿态库帧数 */
	UFUNCTION(BlueprintPure, Category = "CrowdLegion | Stats")
	int32 GetCurrentClipFrameCount() const;

	/** 获取当前存活士兵数量 (非死亡动作的实例) */
	UFUNCTION(BlueprintPure, Category = "CrowdLegion | Stats")
	int32 GetAliveUnitCount() const;

	/** 获取当前阵亡士兵数量 */
	UFUNCTION(BlueprintPure, Category = "CrowdLegion | Stats")
	int32 GetDeadUnitCount() const;

	/** 计算指定实例在当前时刻对应的绝对动画采样帧号 */
	int32 CalculateInstanceCurrentFrame(const FCrowdCellInstanceData& D, float CurrentTime) const;

	/** 生成格式化的实时统计文本 */
	FText GetFormattedStatsText() const;

	/** 创建并挂载右上角 Slate 统计面板 */
	void CreateDebugStatsWidget();

	/** 移除右上角 Slate 统计面板 */
	void RemoveDebugStatsWidget();

private:
	/** 获取有效的 UCrowdUnitComponent 引用 */
	UCrowdUnitComponent* GetValidCrowdComponent() const;

private:
	/** 右上角 Slate 统计面板句柄 */
	TSharedPtr<SWidget> DebugStatsWidget;
};
