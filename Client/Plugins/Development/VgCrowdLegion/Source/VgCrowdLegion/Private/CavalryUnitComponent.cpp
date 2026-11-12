// Copyright Epic Games, Inc. All Rights Reserved.

#include "CavalryUnitComponent.h"

UCavalryUnitComponent::UCavalryUnitComponent()
{
	CullDistance = 10000.0f;
}

int32 UCavalryUnitComponent::AddCellByName(const FTransform& Transform, FName AnimName, float PlayRate, bool bLoop, float StartTimeOffset)
{
	int32 TargetAnimIndex = 0;
	if (!AnimName.IsNone())
	{
		TargetAnimIndex = FindAnimationIndex(AnimName);
		if (TargetAnimIndex == INDEX_NONE)
		{
			TargetAnimIndex = 0;
		}
	}
	return AddCell(Transform, TargetAnimIndex, PlayRate, bLoop, StartTimeOffset);
}
