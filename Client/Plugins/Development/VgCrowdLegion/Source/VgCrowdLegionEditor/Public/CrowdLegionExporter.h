// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class UAnimToTextureDataAsset;
class UMaterial;
class UMaterialFunction;
class UTexture2D;
class UStaticMesh;
class UMaterialInstanceConstant;
class UDataTable;
/**
 * 通用网格布局信息 (根据材质插槽数量自动计算行列分配与象限坐标)
 */
struct FAtlasGridLayout
{
	int32 NumSlots = 1;
	int32 GridCols = 1;
	int32 GridRows = 1;
	int32 CellWidth = 2048;
	int32 CellHeight = 2048;
	int32 AtlasWidth = 4096;
	int32 AtlasHeight = 4096;

	static FAtlasGridLayout CalculateLayout(int32 InNumSlots, int32 MaxAtlasSize = 4096)
	{
		FAtlasGridLayout Layout;
		Layout.NumSlots = FMath::Max(1, InNumSlots);
		Layout.GridCols = FMath::CeilToInt(FMath::Sqrt(static_cast<float>(Layout.NumSlots)));
		Layout.GridRows = FMath::CeilToInt(static_cast<float>(Layout.NumSlots) / static_cast<float>(Layout.GridCols));

		Layout.AtlasWidth = MaxAtlasSize;
		Layout.AtlasHeight = MaxAtlasSize;
		Layout.CellWidth = FMath::Max(1, Layout.AtlasWidth / Layout.GridCols);
		Layout.CellHeight = FMath::Max(1, Layout.AtlasHeight / Layout.GridRows);
		return Layout;
	}
};

/**
 * A2T 与 HISM 军团资产一键导出与自动化处理管线
 */
class VGCROWDLEGIONEDITOR_API FCrowdLegionExporter
{
public:
	/** Tools 菜单命令入口：从当前 Content Browser 选中的 DataAsset 执行全流程导出与烘焙 */
	static void ExecuteExportFromSelectedAsset();

	/** Tools 菜单：生成 Archer 简单版 BoneAnimation 父材质 + 三个 BaseColor MI */
	static void ExecuteGenerateArcherBoneAnimationMaterials();

	/** 获取 Content Browser 中当前选中的 AnimToTextureDataAsset (若未选中返回 nullptr) */
	static UAnimToTextureDataAsset* GetSelectedAnimToTextureDataAsset();

	/** 确保 /VgCrowdLegion/Materials/GetCrowdLegionFrame 存在（纯表达式算帧，无 Custom HLSL） */
	static bool EnsureGetCrowdLegionFrame(UMaterialFunction*& OutFunc);

	/** 步骤 1: 检查或创建插件 Content 目录下的 MF_CrowdLegionBoneAnimation 与 M_Crowd_Legion */
	static bool EnsurePluginMasterMaterials(UMaterial*& OutMasterMat, UMaterialFunction*& OutBoneAnimMF);

	/** 确保 / 创建 M_Archer_BoneAnimation（TextureSample + GetCrowdLegionFrame + MF_CrowdLegionBoneAnimation） */
	static bool EnsureArcherBoneAnimationParentMaterial(UMaterial*& OutParentMat, UMaterialFunction* BoneAnimMF);

	/** 创建或更新三个 Archer BoneAnimation 材质实例并绑定贴图 */
	static bool EnsureArcherBoneAnimationMaterialInstances(UMaterial* ParentMat);

	/** 步骤 2: 根据 SkeletalMesh 材质合并生成 2x2 Texture Atlas 大图 */
	static UTexture2D* GenerateAtlasTextureFromSkelMesh(USkeletalMesh* SkelMesh, const FString& TargetPackagePath);

	/** 步骤 3: 导出 StaticMesh 并将各 Section 的 UV0 重映射至 Atlas 对应象限，最后合并为 1 个插槽 */
	static UStaticMesh* ExportStaticMeshWithAtlasUVRemap(USkeletalMesh* SkelMesh, const FString& TargetStaticMeshPath);

	/** 步骤 4: 执行骨骼动画纹理分配与 A2T 核心烘焙 */
	static bool BakeA2TDataAsset(UAnimToTextureDataAsset* DataAsset, UMaterial* MasterMaterial, UStaticMesh* BakedMesh, UTexture2D* AtlasTexture);

	/** 步骤 5: 根据 DataAsset 动画序列生成或更新动画元数据 DataTable (FUnitAnimationTableRow) */
	static UDataTable* GenerateAnimMetaDataTable(UAnimToTextureDataAsset* DataAsset, const FString& TargetPackagePath);

private:
	static UTexture2D* FindOrCreateVATTexture(UAnimToTextureDataAsset* DataAsset, const FString& Suffix);
	static void SyncMaterialInstanceRanges(UAnimToTextureDataAsset* DataAsset, UMaterialInstanceConstant* MatInst);
};
