// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class VgCrowdLegionEditor : ModuleRules
{
	public VgCrowdLegionEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
		
		PublicIncludePaths.AddRange(
			new string[] {
			}
		);
				
		PrivateIncludePaths.AddRange(
			new string[] {
			}
		);
			
		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				"UnrealEd",
				"AssetTools",
				"AssetDefinition",
				"MeshDescription",
				"StaticMeshDescription",
				"AnimToTexture",
				"AnimToTextureEditor",
				"MaterialEditor",
				"VgCrowdLegion",
				"ImageCore"
			}
		);
			
		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"Projects",
				"InputCore",
				"EditorFramework",
				"ToolMenus",
				"Slate",
				"SlateCore"
			}
		);
	}
}
