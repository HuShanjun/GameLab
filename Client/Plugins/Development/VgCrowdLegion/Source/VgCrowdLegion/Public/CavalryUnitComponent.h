// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UnitHISMComponent.h"
#include "CavalryUnitComponent.generated.h"

/**
 * 骑兵多 HISM 单元组件（薄派生；层表通常 ≥2 行，相位在基类统一）
 */
UCLASS(ClassGroup = (Crowd), meta = (BlueprintSpawnableComponent))
class VGCROWDLEGION_API UCavalryUnitComponent : public UUnitHISMComponent
{
	GENERATED_BODY()

public:
	UCavalryUnitComponent();

	/** 按动作名添加（内部走 Layer0 名→索引） */
	UFUNCTION(BlueprintCallable, Category = "Cavalry|Instance")
	int32 AddCellByName(const FTransform& Transform, FName AnimName = NAME_None, float PlayRate = 1.0f, bool bLoop = true, float StartTimeOffset = 0.0f);
};
