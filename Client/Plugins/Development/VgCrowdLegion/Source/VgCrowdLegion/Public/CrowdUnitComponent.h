// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UnitHISMComponent.h"
#include "CrowdUnitComponent.generated.h"

/**
 * 步兵/通用海量单元组件（薄派生，逻辑在 UUnitHISMComponent）
 */
UCLASS(ClassGroup = (Crowd), meta = (BlueprintSpawnableComponent))
class VGCROWDLEGION_API UCrowdUnitComponent : public UUnitHISMComponent
{
	GENERATED_BODY()

public:
	UCrowdUnitComponent();

	/** 兼容旧调用：等同 GetHISM(0) */
	UFUNCTION(BlueprintPure, Category = "CrowdLegion | HISM")
	UHierarchicalInstancedStaticMeshComponent* GetHISMCmp() const { return GetHISM(0); }
};

using UCrowdUnitManager = UCrowdUnitComponent;
