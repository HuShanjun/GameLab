// Copyright Epic Games, Inc. All Rights Reserved.

#include "VgCrowdLegionEditor.h"
#include "CrowdLegionExporter.h"
#include "ToolMenus.h"

#define LOCTEXT_NAMESPACE "FVgCrowdLegionEditorModule"

void FVgCrowdLegionEditorModule::StartupModule()
{
	UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FVgCrowdLegionEditorModule::RegisterMenus));
}

void FVgCrowdLegionEditorModule::ShutdownModule()
{
	UToolMenus::UnRegisterStartupCallback(this);
	UToolMenus::UnregisterOwner(this);
}

void FVgCrowdLegionEditorModule::RegisterMenus()
{
	FToolMenuOwnerScoped OwnerScoped(this);

	UToolMenu* ToolsMenu = UToolMenus::Get()->ExtendMenu("LevelEditor.MainMenu.Tools");
	if (!ToolsMenu)
	{
		return;
	}

	FToolMenuSection& Section = ToolsMenu->FindOrAddSection("VgCrowdLegionTools", LOCTEXT("VgCrowdLegionTools_Section", "海量军团 (VgCrowdLegion)"));
	Section.AddMenuEntry(
		"ConvertSkelMeshToStaticMesh",
		LOCTEXT("ConvertSkelMeshToStaticMesh_Label", "1. 骨骼网格体转静态网格体 (SkelMesh -> StaticMesh)"),
		LOCTEXT("ConvertSkelMeshToStaticMesh_Tooltip", "从选中的 SkeletalMesh (或 DataAsset) 导出 StaticMesh，完整保留多材质插槽与原始 UV"),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateStatic(&FCrowdLegionExporter::ExecuteConvertSkeletalMeshToStaticMesh))
	);

	Section.AddMenuEntry(
		"GenerateStaticA2TMaterials",
		LOCTEXT("GenerateStaticA2TMaterials_Label", "2. 生成并绑定 Static A2T 材质 (SkelMesh -> StaticMesh)"),
		LOCTEXT("GenerateStaticA2TMaterials_Tooltip", "复制原材质蓝图，原节点逻辑接入 MakeMaterialAttributes -> MF_CrowdLegionBoneAnimation (Frame 连 GetCrowdLegionFrame)，并与 StaticMesh 材质槽一一对应"),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateStatic(&FCrowdLegionExporter::ExecuteGenerateStaticA2TMaterials))
	);

	Section.AddMenuEntry(
		"GenerateAnimMetaDataTable",
		LOCTEXT("GenerateAnimMetaDataTable_Label", "3. 根据 A2T DataAsset 生成动画元数据表 (AnimMeta)"),
		LOCTEXT("GenerateAnimMetaDataTable_Tooltip", "从当前在 Content Browser 中选中的 AnimToTextureDataAsset 一键生成或更新 FUnitAnimationTableRow 动画元数据 DataTable"),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateStatic(&FCrowdLegionExporter::ExecuteGenerateAnimMetaDataTableFromSelectedAsset))
	);

	Section.AddMenuEntry(
		"BakeSelectedDataAsset",
		LOCTEXT("BakeSelectedDataAsset_Label", "4. 选中 DataAsset 烘焙 (只烘焙)"),
		LOCTEXT("BakeSelectedDataAsset_Tooltip", "仅对选中的 AnimToTextureDataAsset 执行烘焙（SetLightMapIndex -> AnimationToTexture -> UpdateMaterialInstanceFromDataAsset），不生成 StaticMesh、材质或 AnimMeta DT"),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateStatic(&FCrowdLegionExporter::ExecuteBakeFromSelectedDataAsset))
	);

	Section.AddMenuEntry(
		"ExportA2TAssets",
		LOCTEXT("ExportA2TAssets_Label", "5. A2T 军团资产一键导出与烘焙 (全流程)"),
		LOCTEXT("ExportA2TAssets_Tooltip", "串联执行全流程：1. 导出 StaticMesh (Meshes/) -> 2. 生成并绑定 Static A2T 材质 (Materials/) -> 3. A2T 烘焙 (Textures/ + MI) -> 4. 生成 AnimMeta DataTable"),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateStatic(&FCrowdLegionExporter::ExecuteExportFromSelectedAsset))
	);
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FVgCrowdLegionEditorModule, VgCrowdLegionEditor)
