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
		"ExportA2TAssets",
		LOCTEXT("ExportA2TAssets_Label", "A2T 军团资产一键导出与烘焙"),
		LOCTEXT("ExportA2TAssets_Tooltip", "从当前在 Content Browser 中选中的 AnimToTextureDataAsset 导出 StaticMesh、合并多贴图至 Atlas、创建配套材质并烘焙 A2T"),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateStatic(&FCrowdLegionExporter::ExecuteExportFromSelectedAsset))
	);
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FVgCrowdLegionEditorModule, VgCrowdLegionEditor)
