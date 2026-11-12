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
 * A2T 与 HISM 军团资产一键导出与自动化处理管线
 */
class VGCROWDLEGIONEDITOR_API FCrowdLegionExporter
{
public:
	/** Tools 菜单工具 1：Skeletal Mesh 转为 Static Mesh（保留多材质插槽与原始 UV） */
	static void ExecuteConvertSkeletalMeshToStaticMesh();

	/** Tools 菜单工具 2：生成 Static A2T 材质并赋值给 Static Mesh 对应插槽（去重+保留原节点逻辑+MakeMaterialAttributes->BoneAnim） */
	static void ExecuteGenerateStaticA2TMaterials();

	/** Tools 菜单工具 3：从当前 Content Browser 选中的 DataAsset 生成/更新动画元数据 DataTable (FUnitAnimationTableRow) */
	static void ExecuteGenerateAnimMetaDataTableFromSelectedAsset();

	/** Tools 菜单工具 4：选中 DataAsset 烘焙（只烘焙 VAT 与同步已有 MI 参数，不生成 StaticMesh/材质/AnimMetaDT） */
	static void ExecuteBakeFromSelectedDataAsset();

	/** Tools 菜单工具 5：从当前 Content Browser 选中的 DataAsset 执行全流程一键导出与烘焙 */
	static void ExecuteExportFromSelectedAsset();

	/** 获取选中的 SkeletalMesh (若直接选中 SkeletalMesh 则返回，若选中 DataAsset 则返回其绑定的 SkeletalMesh) */
	static USkeletalMesh* GetSelectedSkeletalMesh(UAnimToTextureDataAsset** OutDataAsset = nullptr);
	/**
	 * 将指定的源材质（UMaterial）拷贝为新的 A2T 材质，并在原有节点逻辑后无损拼接：
	 * MakeMaterialAttributes -> MF_CrowdLegionBoneAnimation (Frame 连 GetCrowdLegionFrame) -> UseMaterialAttributes
	 */
	static UMaterial* DuplicateAndConvertMaterialToA2T(
		UMaterial* SourceMat,
		const FString& TargetPackagePath,
		const FName TargetAssetName,
		UMaterialFunction* BoneAnimMF,
		UMaterialFunction* GetCrowdLegionFrameMF);

	/**
	 * 为给定的材质接口（UMaterial 或 UMaterialInstance）生成对应的 A2T 材质接口（带去重缓存）：
	 * - 若为 UMaterial：调用 DuplicateAndConvertMaterialToA2T 生成 A2T 材质
	 * - 若为 UMaterialInstance：先转换底层 UMaterial 为 A2T 母材质，再创建 A2T 材质实例并全量拷贝参数
	 */
	static UMaterialInterface* ConvertMaterialInterfaceToA2T(
		UMaterialInterface* SourceMatInterface,
		const FString& TargetDir,
		UMaterialFunction* BoneAnimMF,
		UMaterialFunction* GetCrowdLegionFrameMF,
		TMap<UMaterialInterface*, UMaterialInterface*>& InOutCache);

	/** 根据 SkeletalMesh 插槽列表，为 StaticMesh 生成并赋予对应的 A2T 材质（一一对应） */
	static bool GenerateAndAssignA2TMaterialsForStaticMesh(
		USkeletalMesh* SkelMesh,
		UStaticMesh* StaticMesh,
		const FString& TargetDir,
		TArray<UMaterialInterface*>& OutAssignedMaterials);

	/** 获取 Content Browser 中当前选中的 AnimToTextureDataAsset (若未选中返回 nullptr) */
	static UAnimToTextureDataAsset* GetSelectedAnimToTextureDataAsset();

	/** 确保 /VgCrowdLegion/Materials/GetCrowdLegionFrame 存在（纯表达式算帧，无 Custom HLSL） */
	static bool EnsureGetCrowdLegionFrame(UMaterialFunction*& OutFunc);

	/** 检查或创建插件 Content 目录下的 MF_CrowdLegionBoneAnimation */
	static bool EnsurePluginBoneAnimationMF(UMaterialFunction*& OutBoneAnimMF);

	/** 导出 StaticMesh，保留全部材质插槽与原始 UV（保存到目标包路径，自动避开 Lightmap UV1 冲突） */
	static UStaticMesh* ExportStaticMeshFromSkeletalMesh(USkeletalMesh* SkelMesh, const FString& TargetStaticMeshPath);

	/** 烘焙指定 DataAsset（按 BP_AnimToTexture 流程：SetLightMapIndex -> AnimationToTexture -> UpdateMaterialInstanceFromDataAsset） */
	static bool BakeA2TDataAsset(UAnimToTextureDataAsset* DataAsset, UStaticMesh* BakedMesh);

	/** 根据 DataAsset 动画序列生成或更新动画元数据 DataTable (FUnitAnimationTableRow) */
	static UDataTable* GenerateAnimMetaDataTable(UAnimToTextureDataAsset* DataAsset, const FString& TargetPackagePath);

private:
	static UTexture2D* FindOrCreateVATTexture(UAnimToTextureDataAsset* DataAsset, const FString& Suffix);
	/** 安全解析 SoftPtr（避免官方 Get*Texture ensure 崩溃）；不在 Textures/ 下或缺失则创建并写回 SoftPtr */
	static UTexture2D* EnsureVATTexture(UAnimToTextureDataAsset* DataAsset, TSoftObjectPtr<UTexture2D>& SoftPtr, const FString& Suffix);
};
