// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "UnitHISMComponent.h"
#include "CrowdStatsComponent.generated.h"

class UUnitHISMComponent;
class SWidget;

/**
 * 军团实时性能与姿态统计组件（与 UUnitHISMComponent 解耦）
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
	/** 目标单元组件（空则在 Owner 上 FindComponentByClass） */
	UPROPERTY(BlueprintReadWrite, Category = "CrowdLegion | Stats")
	TWeakObjectPtr<UUnitHISMComponent> UnitComponent;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CrowdLegion | Stats")
	bool bShowDebugStatsHUD = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CrowdLegion | Stats", meta = (ClampMin = "8", ClampMax = "72"))
	int32 DebugStatsFontSize = 16;

	UFUNCTION(BlueprintCallable, Category = "CrowdLegion | Stats")
	void SetDebugStatsFontSize(int32 NewFontSize)
	{
		DebugStatsFontSize = FMath::Clamp(NewFontSize, 8, 72);
	}

	/** 为 true 时 BeginPlay 创建时间戳 CSV，并在 Notify 边界写出段均值 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CrowdLegion | Stats")
	bool bEnablePerfCsv = true;

	/** 动画段切换完成时调用：结算上一段均值行并开始新段 */
	UFUNCTION(BlueprintCallable, Category = "CrowdLegion | Stats")
	void NotifyAnimSegment(FName AnimName);

	UFUNCTION(BlueprintPure, Category = "CrowdLegion | Stats")
	int32 GetBonesPerUnit() const;

	UFUNCTION(BlueprintPure, Category = "CrowdLegion | Stats")
	int32 GetTotalEquivalentBones() const;

	UFUNCTION(BlueprintPure, Category = "CrowdLegion | Stats")
	int32 GetActivePoseCount() const;

	UFUNCTION(BlueprintPure, Category = "CrowdLegion | Stats")
	int32 GetCurrentClipFrameCount() const;

	UFUNCTION(BlueprintPure, Category = "CrowdLegion | Stats")
	int32 GetAliveUnitCount() const;

	UFUNCTION(BlueprintPure, Category = "CrowdLegion | Stats")
	int32 GetDeadUnitCount() const;

	int32 CalculateInstanceCurrentFrame(const FCrowdCellInstanceData& D, float CurrentTime) const;

	FText GetFormattedStatsText() const;

	void CreateDebugStatsWidget();
	void RemoveDebugStatsWidget();

private:
	UUnitHISMComponent* GetValidUnitComponent() const;

	void OpenPerfCsvIfNeeded();
	void ClosePerfCsv();
	void StartSegment(FName AnimName);
	void FlushCurrentSegment();
	float ResolveSpreadRadius() const;
	FString BuildPerfCsvPath() const;

	TSharedPtr<SWidget> DebugStatsWidget;

	FString PerfCsvAbsolutePath;
	bool bSegmentActive = false;
	FName SegmentAnimName = NAME_None;
	int32 SegmentSampleCount = 0;
	double SumFrameMs = 0.0;
	double SumGameMs = 0.0;
	double SumDrawMs = 0.0;
	double SumGpuMs = 0.0;
	double SumRhitMs = 0.0;
};
