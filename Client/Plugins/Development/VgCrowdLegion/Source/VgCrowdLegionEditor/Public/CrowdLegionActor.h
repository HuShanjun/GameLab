// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "UnitHISMComponent.h"
#include "CrowdLegionActor.generated.h"

class UCrowdStatsComponent;
class UDataTable;

/**
 * 军团视口 Actor：层表 Setup + 生成范围 + 动画索引；负责创建实例与播放/行军
 * VAT 导出请用 Tools / CrowdLegionExporter，不在本 Actor 内处理
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
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CrowdLegion | Config")
	bool bAutoInitOnBeginPlay = true;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "CrowdLegion", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UUnitHISMComponent> UnitComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "CrowdLegion", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UCrowdStatsComponent> StatsComponent;

	/** 层配置表（行类型 FUnitHISMLayerTableRow） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CrowdLegion | Setup")
	TObjectPtr<UDataTable> LayerSetupTable;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CrowdLegion | Setup", meta = (ClampMin = "0.0"))
	float CullDistance = 20000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CrowdLegion | Spawn", meta = (ClampMin = "1", ClampMax = "100000"))
	int32 NumUnits = 1000;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CrowdLegion | Spawn", meta = (ClampMin = "100.0"))
	float SpreadRadius = 3000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CrowdLegion | Spawn")
	bool bRandomYaw = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CrowdLegion | Anim")
	int32 IdleAnimIndex = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CrowdLegion | Anim")
	int32 RunAnimIndex = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CrowdLegion | Anim")
	int32 AttackAnimIndex = 2;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CrowdLegion | Anim")
	int32 DeathAnimIndex = 3;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CrowdLegion | Stats", meta = (ClampMin = "8", ClampMax = "72"))
	int32 DebugStatsFontSize = 16;

	UFUNCTION(CallInEditor, BlueprintCallable, Category = "CrowdLegion | Stats")
	void SetStatsFontSize(int32 NewSize);

	UFUNCTION(CallInEditor, BlueprintCallable, Category = "CrowdLegion | Actions")
	void SpawnLegion();

	UFUNCTION(CallInEditor, BlueprintCallable, Category = "CrowdLegion | Actions")
	void SetLegionIdle();

	UFUNCTION(CallInEditor, BlueprintCallable, Category = "CrowdLegion | Actions")
	void SetLegionRun();

	UFUNCTION(CallInEditor, BlueprintCallable, Category = "CrowdLegion | Actions")
	void SetLegionAttack();

	UFUNCTION(CallInEditor, BlueprintCallable, Category = "CrowdLegion | Actions")
	void KillAllLegion();

	UFUNCTION(CallInEditor, BlueprintCallable, Category = "CrowdLegion | Actions")
	void ClearLegion();

	UFUNCTION(CallInEditor, BlueprintCallable, Category = "CrowdLegion | Actions")
	void MarchLegionForward();

	UFUNCTION(CallInEditor, BlueprintCallable, Category = "CrowdLegion | Actions")
	void StopLegionMarch();

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

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "CrowdLegion", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USceneComponent> SceneRoot;
};
