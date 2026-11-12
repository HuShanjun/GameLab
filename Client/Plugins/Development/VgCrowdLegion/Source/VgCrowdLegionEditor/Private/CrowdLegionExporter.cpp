// Copyright Epic Games, Inc. All Rights Reserved.

#include "CrowdLegionExporter.h"
#include "AnimToTextureDataAsset.h"
#include "AnimToTextureBPLibrary.h"
#include "ContentBrowserModule.h"
#include "IContentBrowserSingleton.h"
#include "Misc/MessageDialog.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Materials/Material.h"
#include "Materials/MaterialFunction.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/MaterialExpressionPerInstanceCustomData.h"
#include "Materials/MaterialExpressionTime.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionMaterialFunctionCall.h"
#include "Materials/MaterialExpressionFunctionInput.h"
#include "Materials/MaterialExpressionTextureSampleParameter2D.h"
#include "Materials/MaterialExpressionMakeMaterialAttributes.h"
#include "Materials/MaterialExpressionSubstrate.h"
#include "Materials/MaterialExpressionFunctionOutput.h"
#include "Materials/MaterialExpressionAbs.h"
#include "Materials/MaterialExpressionAdd.h"
#include "Materials/MaterialExpressionSubtract.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "Materials/MaterialExpressionMax.h"
#include "Materials/MaterialExpressionMin.h"
#include "Materials/MaterialExpressionFmod.h"
#include "Materials/MaterialExpressionFloor.h"
#include "Materials/MaterialExpressionIf.h"
#include "MaterialEditingLibrary.h"
#include "MaterialGraph/MaterialGraph.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/Package.h"
#include "HAL/IConsoleManager.h"
#include "Misc/PackageName.h"
#include "Engine/DataTable.h"
#include "CrowdUnitComponent.h"

static FAutoConsoleCommand GConvertSkelMeshToStaticMeshCmd(
	TEXT("CrowdLegion.ConvertSkelMeshToStaticMesh"),
	TEXT("1. Skeletal Mesh 转为 Static Mesh（保留多插槽与原始 UV）"),
	FConsoleCommandDelegate::CreateStatic(&FCrowdLegionExporter::ExecuteConvertSkeletalMeshToStaticMesh));

static FAutoConsoleCommand GGenerateStaticA2TMaterialsCmd(
	TEXT("CrowdLegion.GenerateStaticA2TMaterials"),
	TEXT("2. 生成 Static A2T 材质并赋值给 Static Mesh 对应插槽"),
	FConsoleCommandDelegate::CreateStatic(&FCrowdLegionExporter::ExecuteGenerateStaticA2TMaterials));

static FAutoConsoleCommand GGenerateAnimMetaDataTableCmd(
	TEXT("CrowdLegion.GenerateAnimMetaDataTable"),
	TEXT("3. 从 Content Browser 选中的 AnimToTextureDataAsset 生成/更新 FUnitAnimationTableRow DataTable"),
	FConsoleCommandDelegate::CreateStatic(&FCrowdLegionExporter::ExecuteGenerateAnimMetaDataTableFromSelectedAsset));

static FAutoConsoleCommand GBakeDataAssetCmd(
	TEXT("CrowdLegion.BakeDataAsset"),
	TEXT("4. 选中 DataAsset 烘焙（只烘焙 VAT 与更新材质实例，不生成 StaticMesh/材质/AnimMetaDT）"),
	FConsoleCommandDelegate::CreateStatic(&FCrowdLegionExporter::ExecuteBakeFromSelectedDataAsset));

static FAutoConsoleCommand GExportA2TLegionCmd(
	TEXT("CrowdLegion.ExportA2TLegion"),
	TEXT("5. A2T 军团资产一键导出与烘焙（全流程）"),
	FConsoleCommandDelegate::CreateStatic(&FCrowdLegionExporter::ExecuteExportFromSelectedAsset));
static const TCHAR* GPluginMFPackage = TEXT("/VgCrowdLegion/Materials/MF_CrowdLegionBoneAnimation");
static const TCHAR* GGetCrowdLegionFramePkg = TEXT("/VgCrowdLegion/Materials/GetCrowdLegionFrame");
static const TCHAR* GLegacyPluginMFPackage = TEXT("/VgCrowdLegion/Materials/MF_BoneAnimation");
static const TCHAR* GOfficialMFPath = TEXT("/AnimToTexture/Materials/MaterialFunctions/MF_BoneAnimation.MF_BoneAnimation");


UAnimToTextureDataAsset* FCrowdLegionExporter::GetSelectedAnimToTextureDataAsset()
{
	FContentBrowserModule& ContentBrowserModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
	TArray<FAssetData> SelectedAssets;
	ContentBrowserModule.Get().GetSelectedAssets(SelectedAssets);

	for (const FAssetData& AssetData : SelectedAssets)
	{
		UObject* Asset = AssetData.GetAsset();
		if (UAnimToTextureDataAsset* DataAsset = Cast<UAnimToTextureDataAsset>(Asset))
		{
			return DataAsset;
		}
	}
	return nullptr;
}

USkeletalMesh* FCrowdLegionExporter::GetSelectedSkeletalMesh(UAnimToTextureDataAsset** OutDataAsset)
{
	if (OutDataAsset)
	{
		*OutDataAsset = nullptr;
	}

	FContentBrowserModule& ContentBrowserModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
	TArray<FAssetData> SelectedAssets;
	ContentBrowserModule.Get().GetSelectedAssets(SelectedAssets);

	UAnimToTextureDataAsset* SelectedDataAsset = nullptr;
	USkeletalMesh* SelectedSkelMesh = nullptr;
	for (const FAssetData& AssetData : SelectedAssets)
	{
		UObject* Asset = AssetData.GetAsset();
		if (!SelectedDataAsset)
		{
			if (UAnimToTextureDataAsset* DataAsset = Cast<UAnimToTextureDataAsset>(Asset))
			{
				SelectedDataAsset = DataAsset;
			}
		}
		if (!SelectedSkelMesh)
		{
			if (USkeletalMesh* SkelMesh = Cast<USkeletalMesh>(Asset))
			{
				SelectedSkelMesh = SkelMesh;
			}
		}
	}

	if (SelectedDataAsset)
	{
		if (OutDataAsset)
		{
			*OutDataAsset = SelectedDataAsset;
		}
		if (USkeletalMesh* BoundMesh = SelectedDataAsset->GetSkeletalMesh())
		{
			return BoundMesh;
		}
		return SelectedSkelMesh;
	}

	return SelectedSkelMesh;
}

void FCrowdLegionExporter::ExecuteConvertSkeletalMeshToStaticMesh()
{
	UAnimToTextureDataAsset* AssociatedDataAsset = nullptr;
	USkeletalMesh* SkelMesh = GetSelectedSkeletalMesh(&AssociatedDataAsset);
	if (!SkelMesh)
	{
		FMessageDialog::Open(
			EAppMsgType::Ok,
			FText::FromString(TEXT("【SkelMesh 转 StaticMesh】未选中有效骨骼网格体！\n\n请在 Content Browser 中选中一个 SkeletalMesh 资产（或配置了 SkeletalMesh 的 AnimToTextureDataAsset），然后再次点击。")));
		return;
	}

	const FString BaseDir = AssociatedDataAsset
		? FPackageName::GetLongPackagePath(AssociatedDataAsset->GetOutermost()->GetName())
		: FPackageName::GetLongPackagePath(SkelMesh->GetOutermost()->GetName());
	const FString TargetStaticMeshPath = BaseDir / TEXT("Meshes") / (TEXT("SM_") + SkelMesh->GetName() + TEXT("_A2T"));

	UStaticMesh* ExportedMesh = ExportStaticMeshFromSkeletalMesh(SkelMesh, TargetStaticMeshPath);
	if (!ExportedMesh)
	{
		FMessageDialog::Open(
			EAppMsgType::Ok,
			FText::FromString(FString::Printf(TEXT("【SkelMesh 转 StaticMesh】失败！\n\n无法将骨骼网格体 '%s' 导出为 StaticMesh。"), *SkelMesh->GetName())));
		return;
	}

	// 如果关联了 DataAsset，自动绑定导出的 StaticMesh
	if (AssociatedDataAsset)
	{
		AssociatedDataAsset->StaticMesh = ExportedMesh;
		AssociatedDataAsset->MarkPackageDirty();
	}

	// 高亮选中新生成的 StaticMesh
	FContentBrowserModule& ContentBrowserModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
	TArray<UObject*> ObjectsToSync;
	ObjectsToSync.Add(ExportedMesh);
	ContentBrowserModule.Get().SyncBrowserToAssets(ObjectsToSync);

	FMessageDialog::Open(
		EAppMsgType::Ok,
		FText::FromString(FString::Printf(TEXT("【SkelMesh 转 StaticMesh 成功！】\n\n- 源 SkeletalMesh: %s\n- 导出的 StaticMesh: %s\n- 保存路径: %s\n- 包含材质插槽数: %d\n- 原始 UV 与顶点蒙皮权重已完整保留！"),
			*SkelMesh->GetName(),
			*ExportedMesh->GetName(),
			*TargetStaticMeshPath,
			ExportedMesh->GetStaticMaterials().Num())));
}

void FCrowdLegionExporter::ExecuteGenerateStaticA2TMaterials()
{
	UAnimToTextureDataAsset* AssociatedDataAsset = nullptr;
	USkeletalMesh* SkelMesh = GetSelectedSkeletalMesh(&AssociatedDataAsset);
	if (!SkelMesh)
	{
		FMessageDialog::Open(
			EAppMsgType::Ok,
			FText::FromString(TEXT("【生成 Static A2T 材质】未找到有效骨骼网格体！\n\n请在 Content Browser 中选中源 SkeletalMesh（或选中配置了 SkeletalMesh 的 AnimToTextureDataAsset）。")));
		return;
	}

	const FString BaseDir = AssociatedDataAsset
		? FPackageName::GetLongPackagePath(AssociatedDataAsset->GetOutermost()->GetName())
		: FPackageName::GetLongPackagePath(SkelMesh->GetOutermost()->GetName());
	const FString MeshesDir = BaseDir / TEXT("Meshes");
	const FString StaticMeshPkgPath = MeshesDir / (TEXT("SM_") + SkelMesh->GetName() + TEXT("_A2T"));

	// 优先检查 DataAsset 已关联的且位于 Meshes 目录下的 StaticMesh；否则按规范路径查找或导出到 Meshes/
	UStaticMesh* TargetStaticMesh = nullptr;
	if (AssociatedDataAsset)
	{
		if (UStaticMesh* BoundMesh = AssociatedDataAsset->GetStaticMesh())
		{
			const FString BoundDir = FPackageName::GetLongPackagePath(BoundMesh->GetOutermost()->GetName());
			if (BoundDir == MeshesDir)
			{
				TargetStaticMesh = BoundMesh;
			}
		}
	}
	if (!TargetStaticMesh)
	{
		TargetStaticMesh = FindObject<UStaticMesh>(nullptr, *StaticMeshPkgPath);
	}
	if (!TargetStaticMesh && FPackageName::DoesPackageExist(StaticMeshPkgPath))
	{
		TargetStaticMesh = Cast<UStaticMesh>(StaticLoadObject(UStaticMesh::StaticClass(), nullptr, *StaticMeshPkgPath));
	}
	if (!TargetStaticMesh)
	{
		TargetStaticMesh = ExportStaticMeshFromSkeletalMesh(SkelMesh, StaticMeshPkgPath);
	}
	if (!TargetStaticMesh)
	{
		FMessageDialog::Open(
			EAppMsgType::Ok,
			FText::FromString(FString::Printf(TEXT("【生成 Static A2T 材质】找不到对应的 StaticMesh: %s，自动转换也失败。"), *StaticMeshPkgPath)));
		return;
	}

	if (AssociatedDataAsset && AssociatedDataAsset->GetStaticMesh() != TargetStaticMesh)
	{
		AssociatedDataAsset->StaticMesh = TargetStaticMesh;
		AssociatedDataAsset->MarkPackageDirty();
	}

	const FString MatTargetDir = BaseDir / TEXT("Materials");
	TArray<UMaterialInterface*> AssignedMaterials;
	if (!GenerateAndAssignA2TMaterialsForStaticMesh(SkelMesh, TargetStaticMesh, MatTargetDir, AssignedMaterials))
	{
		FMessageDialog::Open(
			EAppMsgType::Ok,
			FText::FromString(TEXT("【生成 Static A2T 材质】生成失败，请检查 Output Log。")));
		return;
	}

	// 高亮选中生成的材质和网格
	FContentBrowserModule& ContentBrowserModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
	TArray<UObject*> ObjectsToSync;
	ObjectsToSync.Add(TargetStaticMesh);
	for (UMaterialInterface* Mat : AssignedMaterials)
	{
		if (Mat)
		{
			ObjectsToSync.Add(Mat);
		}
	}
	ContentBrowserModule.Get().SyncBrowserToAssets(ObjectsToSync);

	FMessageDialog::Open(
		EAppMsgType::Ok,
		FText::FromString(FString::Printf(TEXT("【生成 Static A2T 材质成功！】\n\n- 目标 StaticMesh: %s\n- 已对齐材质插槽数: %d\n- 材质保存目录: %s\n- 材质原有节点逻辑已完整输出到 MakeMaterialAttributes 并连入 MF_CrowdLegionBoneAnimation！"),
			*TargetStaticMesh->GetName(),
			AssignedMaterials.Num(),
			*MatTargetDir)));
}

void FCrowdLegionExporter::ExecuteBakeFromSelectedDataAsset()
{
	UAnimToTextureDataAsset* SelectedDataAsset = GetSelectedAnimToTextureDataAsset();
	if (!SelectedDataAsset)
	{
		FMessageDialog::Open(
			EAppMsgType::Ok,
			FText::FromString(TEXT("【DataAsset 烘焙】未选中有效资产！\n\n请先在 Content Browser (内容浏览器) 中选中一个 AnimToTextureDataAsset 资产，然后再次点击此选项。")));
		return;
	}

	UStaticMesh* TargetStaticMesh = SelectedDataAsset->GetStaticMesh();
	if (!TargetStaticMesh)
	{
		FMessageDialog::Open(
			EAppMsgType::Ok,
			FText::FromString(FString::Printf(TEXT("【DataAsset 烘焙】选中的 DataAsset '%s' 未绑定 StaticMesh！\n\n工具 4 仅执行烘焙（不生成 StaticMesh、材质与 AnimMeta DT）。请先在 DataAsset 中指定 StaticMesh，或先执行工具 1 / 2。"),
				*SelectedDataAsset->GetName())));
		return;
	}

	if (!BakeA2TDataAsset(SelectedDataAsset, TargetStaticMesh))
	{
		FMessageDialog::Open(
			EAppMsgType::Ok,
			FText::FromString(TEXT("【DataAsset 烘焙】烘焙失败，请查看 Output Log 获取详细报错。")));
		return;
	}

	TSet<UMaterialInstanceConstant*> UpdatedMICs;
	const int32 NumSlots = TargetStaticMesh->GetStaticMaterials().Num();
	for (int32 SlotIdx = 0; SlotIdx < NumSlots; ++SlotIdx)
	{
		if (UMaterialInstanceConstant* MIC = Cast<UMaterialInstanceConstant>(TargetStaticMesh->GetMaterial(SlotIdx)))
		{
			UpdatedMICs.Add(MIC);
		}
	}

	FContentBrowserModule& ContentBrowserModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
	TArray<UObject*> ObjectsToSync;
	ObjectsToSync.Add(SelectedDataAsset);
	ContentBrowserModule.Get().SyncBrowserToAssets(ObjectsToSync);

	const FString BaseDir = FPackageName::GetLongPackagePath(SelectedDataAsset->GetOutermost()->GetName());
	FMessageDialog::Open(
		EAppMsgType::Ok,
		FText::FromString(FString::Printf(TEXT("【DataAsset 烘焙成功！】\n\n- 目标 DataAsset: %s\n- 目标 StaticMesh: %s\n- 贴图目录 (Textures/): %s\n- 已同步更新材质实例数: %d\n- 提示：工具 4 仅执行烘焙与 MI 参数同步，未改动 StaticMesh、材质结构或 AnimMeta 数据表。"),
			*SelectedDataAsset->GetName(),
			*TargetStaticMesh->GetName(),
			*(BaseDir / TEXT("Textures")),
			UpdatedMICs.Num())));
}

void FCrowdLegionExporter::ExecuteExportFromSelectedAsset()
{
	// 4.1: 获取当前在资产 Content 中选中的 DataAsset，没有则弹框提醒用户
	UAnimToTextureDataAsset* SelectedDataAsset = GetSelectedAnimToTextureDataAsset();
	if (!SelectedDataAsset)
	{
		FMessageDialog::Open(
			EAppMsgType::Ok,
			FText::FromString(TEXT("【A2T 军团资产一键导出与烘焙】未选中有效资产！\n\n请先在 Content Browser (内容浏览器) 中单选或高亮一个 AnimToTextureDataAsset 资产，然后再次点击此选项。")));
		return;
	}

	USkeletalMesh* SkelMesh = SelectedDataAsset->GetSkeletalMesh();
	if (!SkelMesh)
	{
		FMessageDialog::Open(
			EAppMsgType::Ok,
			FText::FromString(FString::Printf(TEXT("【A2T 军团资产一键导出】选中的 DataAsset '%s' 未指定 SkeletalMesh！\n\n请在 DataAsset 中配置源骨骼网格体后再执行导出。"), *SelectedDataAsset->GetName())));
		return;
	}

	const FString DataAssetPackage = SelectedDataAsset->GetOutermost()->GetName();
	const FString BaseDir = FPackageName::GetLongPackagePath(DataAssetPackage);
	const FString SkelMeshName = SkelMesh->GetName();

	// 步骤 1: SkeletalMesh 转 StaticMesh (保存到 BaseDir/Meshes/)
	const FString StaticMeshPkgPath = BaseDir / TEXT("Meshes") / (TEXT("SM_") + SkelMeshName + TEXT("_A2T"));
	UStaticMesh* BakedStaticMesh = ExportStaticMeshFromSkeletalMesh(SkelMesh, StaticMeshPkgPath);
	if (!BakedStaticMesh)
	{
		FMessageDialog::Open(
			EAppMsgType::Ok,
			FText::FromString(TEXT("【A2T 军团资产一键导出】从 SkeletalMesh 导出 StaticMesh 失败！")));
		return;
	}

	SelectedDataAsset->StaticMesh = BakedStaticMesh;
	SelectedDataAsset->MarkPackageDirty();

	// 步骤 2: 生成并绑定 Static A2T 材质 (去重 + 材质原有逻辑接 MakeMaterialAttributes + MF_CrowdLegionBoneAnimation)
	const FString MatTargetDir = BaseDir / TEXT("Materials");
	TArray<UMaterialInterface*> AssignedMaterials;
	if (!GenerateAndAssignA2TMaterialsForStaticMesh(SkelMesh, BakedStaticMesh, MatTargetDir, AssignedMaterials))
	{
		FMessageDialog::Open(
			EAppMsgType::Ok,
			FText::FromString(TEXT("【A2T 军团资产一键导出】生成 A2T 材质失败！请检查 Output Log。")));
		return;
	}

	// 步骤 3: A2T 烘焙 (贴图生成与材质参数同步)
	const bool bBakeSuccess = BakeA2TDataAsset(SelectedDataAsset, BakedStaticMesh);

	// 步骤 4: 动画元数据 DataTable (AnimName, StartFrame, EndFrame, bDefaultLoop)
	const FString TablePkgPath = BaseDir / (TEXT("DT_") + SkelMeshName + TEXT("_AnimMeta"));
	UDataTable* AnimMetaTable = GenerateAnimMetaDataTable(SelectedDataAsset, TablePkgPath);

	if (bBakeSuccess)
	{
		// 高亮选中生成的 StaticMesh 和 DataTable
		FContentBrowserModule& ContentBrowserModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
		TArray<UObject*> ObjectsToSync;
		ObjectsToSync.Add(BakedStaticMesh);
		if (AnimMetaTable)
		{
			ObjectsToSync.Add(AnimMetaTable);
		}
		ContentBrowserModule.Get().SyncBrowserToAssets(ObjectsToSync);

		FMessageDialog::Open(
			EAppMsgType::Ok,
			FText::FromString(FString::Printf(TEXT("【A2T 军团资产一键导出与烘焙成功！】\n\n- 目标 DataAsset: %s\n- StaticMesh (Meshes/): %s（%d 材质插槽）\n- 材质目录 (Materials/): %s\n- 贴图目录 (Textures/): %s\n- 动画元数据表 (同级目录): %s\n- 骨骼纹理与动画起止帧已全自动烘焙同步完成！"),
				*SelectedDataAsset->GetName(),
				*BakedStaticMesh->GetName(),
				BakedStaticMesh->GetStaticMaterials().Num(),
				*MatTargetDir,
				*(BaseDir / TEXT("Textures")),
				AnimMetaTable ? *AnimMetaTable->GetName() : TEXT("None"))));
	}
	else
	{
		FMessageDialog::Open(
			EAppMsgType::Ok,
			FText::FromString(TEXT("【A2T 军团资产一键导出】A2T 烘焙失败，请查看 Output Log 获取详细报错。")));
	}
}

void FCrowdLegionExporter::ExecuteGenerateAnimMetaDataTableFromSelectedAsset()
{
	UAnimToTextureDataAsset* SelectedDataAsset = GetSelectedAnimToTextureDataAsset();
	if (!SelectedDataAsset)
	{
		FMessageDialog::Open(
			EAppMsgType::Ok,
			FText::FromString(TEXT("【生成 AnimMeta DataTable】未选中有效资产！\n\n请先在 Content Browser (内容浏览器) 中选中一个 AnimToTextureDataAsset 资产，然后再次点击此选项。")));
		return;
	}

	const FString DataAssetPackage = SelectedDataAsset->GetOutermost()->GetName();
	const FString BaseDir = FPackageName::GetLongPackagePath(DataAssetPackage);

	USkeletalMesh* SkelMesh = SelectedDataAsset->GetSkeletalMesh();
	const FString TargetBaseName = SkelMesh ? SkelMesh->GetName() : SelectedDataAsset->GetName();
	const FString TablePkgPath = BaseDir / (TEXT("DT_") + TargetBaseName + TEXT("_AnimMeta"));

	UDataTable* AnimMetaTable = GenerateAnimMetaDataTable(SelectedDataAsset, TablePkgPath);
	if (!AnimMetaTable)
	{
		FMessageDialog::Open(
			EAppMsgType::Ok,
			FText::FromString(FString::Printf(TEXT("【生成 AnimMeta DataTable】失败！\n\n无法从选中的 DataAsset '%s' 生成数据表，请检查 Output Log。"), *SelectedDataAsset->GetName())));
		return;
	}

	// 选中并在内容浏览器中高亮新生成的数据表
	FContentBrowserModule& ContentBrowserModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
	TArray<UObject*> ObjectsToSync;
	ObjectsToSync.Add(AnimMetaTable);
	ContentBrowserModule.Get().SyncBrowserToAssets(ObjectsToSync);

	const int32 RowCount = AnimMetaTable->GetRowNames().Num();
	FMessageDialog::Open(
		EAppMsgType::Ok,
		FText::FromString(FString::Printf(TEXT("【生成 AnimMeta DataTable 成功！】\n\n- 数据表资产: %s\n- 存储路径: %s\n- 包含动作数量: %d\n- 关联骨骼网格体: %s\n\n提示：若数据表原先存在同名动作行，已自动保留您先前手动配置的 bDefaultLoop 循环标记。"),
			*AnimMetaTable->GetName(),
			*TablePkgPath,
			RowCount,
			SkelMesh ? *SkelMesh->GetName() : TEXT("未绑定 (使用 DataAsset 命名)"))));
}

static UMaterialExpressionFunctionInput* AddScalarFunctionInput(
	UMaterialFunction* Func,
	const FName& Name,
	float PreviewScalar,
	int32 PosX,
	int32 PosY,
	bool bUsePreviewAsDefault = true)
{
	UMaterialExpressionFunctionInput* Input = Cast<UMaterialExpressionFunctionInput>(
		UMaterialEditingLibrary::CreateMaterialExpressionInFunction(
			Func, UMaterialExpressionFunctionInput::StaticClass(), PosX, PosY));
	if (!Input)
	{
		return nullptr;
	}
	Input->InputName = Name;
	Input->InputType = FunctionInput_Scalar;
	Input->PreviewValue = FVector4f(PreviewScalar, 0.f, 0.f, 0.f);
	Input->bUsePreviewValueAsDefault = bUsePreviewAsDefault;
	return Input;
}

bool FCrowdLegionExporter::EnsureGetCrowdLegionFrame(UMaterialFunction*& OutFunc)
{
	OutFunc = nullptr;
	const FString ObjectPath = FString(GGetCrowdLegionFramePkg) + TEXT(".GetCrowdLegionFrame");
	OutFunc = Cast<UMaterialFunction>(StaticLoadObject(UMaterialFunction::StaticClass(), nullptr, *ObjectPath));
	if (OutFunc)
	{
		return true;
	}

	UPackage* Package = CreatePackage(GGetCrowdLegionFramePkg);
	if (!Package)
	{
		return false;
	}
	Package->FullyLoad();

	OutFunc = NewObject<UMaterialFunction>(Package, FName(TEXT("GetCrowdLegionFrame")), RF_Public | RF_Standalone);
	if (!OutFunc)
	{
		return false;
	}

	OutFunc->PreEditChange(nullptr);

	// --- Function inputs ---
	UMaterialExpressionFunctionInput* InTimeOffset = AddScalarFunctionInput(OutFunc, TEXT("TimeOffset"), 0.f, -1400, -200);
	UMaterialExpressionFunctionInput* InPlayrate = AddScalarFunctionInput(OutFunc, TEXT("Playrate"), 1.f, -1400, -50);
	UMaterialExpressionFunctionInput* InStartFrame = AddScalarFunctionInput(OutFunc, TEXT("StartFrame"), 0.f, -1400, 100);
	UMaterialExpressionFunctionInput* InEndFrame = AddScalarFunctionInput(OutFunc, TEXT("EndFrame"), 1.f, -1400, 250);
	UMaterialExpressionFunctionInput* InSampleRate = AddScalarFunctionInput(OutFunc, TEXT("SampleRate"), 30.f, -1400, 400);
	UMaterialExpressionFunctionInput* InTime = AddScalarFunctionInput(OutFunc, TEXT("Time"), 0.f, -1400, 550, true);

	UMaterialExpressionTime* InternalTime = Cast<UMaterialExpressionTime>(
		UMaterialEditingLibrary::CreateMaterialExpressionInFunction(
			OutFunc, UMaterialExpressionTime::StaticClass(), -1600, 550));

	if (!InTimeOffset || !InPlayrate || !InStartFrame || !InEndFrame || !InSampleRate || !InTime || !InternalTime)
	{
		UE_LOG(LogTemp, Error, TEXT("[CrowdLegionExporter] Failed to create GetCrowdLegionFrame function inputs."));
		return false;
	}

	InternalTime->bIgnorePause = true;
	InTime->Preview.Connect(0, InternalTime);

	// 1. RateAbs = Abs(Playrate)
	UMaterialExpressionAbs* RateAbs = Cast<UMaterialExpressionAbs>(
		UMaterialEditingLibrary::CreateMaterialExpressionInFunction(
			OutFunc, UMaterialExpressionAbs::StaticClass(), -1100, -50));
	if (!RateAbs)
	{
		UE_LOG(LogTemp, Error, TEXT("[CrowdLegionExporter] Failed to create GetCrowdLegionFrame expression (RateAbs)."));
		return false;
	}
	RateAbs->Input.Connect(0, InPlayrate);

	// 2. Elapsed = (Time + TimeOffset) * RateAbs * SampleRate
	UMaterialExpressionAdd* TimePlusOffset = Cast<UMaterialExpressionAdd>(
		UMaterialEditingLibrary::CreateMaterialExpressionInFunction(
			OutFunc, UMaterialExpressionAdd::StaticClass(), -900, 200));
	if (!TimePlusOffset)
	{
		UE_LOG(LogTemp, Error, TEXT("[CrowdLegionExporter] Failed to create GetCrowdLegionFrame expression (TimePlusOffset)."));
		return false;
	}
	TimePlusOffset->A.Connect(0, InTime);
	TimePlusOffset->B.Connect(0, InTimeOffset);

	UMaterialExpressionMultiply* ElapsedStep1 = Cast<UMaterialExpressionMultiply>(
		UMaterialEditingLibrary::CreateMaterialExpressionInFunction(
			OutFunc, UMaterialExpressionMultiply::StaticClass(), -700, 200));
	if (!ElapsedStep1)
	{
		UE_LOG(LogTemp, Error, TEXT("[CrowdLegionExporter] Failed to create GetCrowdLegionFrame expression (ElapsedStep1)."));
		return false;
	}
	ElapsedStep1->A.Connect(0, TimePlusOffset);
	ElapsedStep1->B.Connect(0, RateAbs);

	UMaterialExpressionMultiply* Elapsed = Cast<UMaterialExpressionMultiply>(
		UMaterialEditingLibrary::CreateMaterialExpressionInFunction(
			OutFunc, UMaterialExpressionMultiply::StaticClass(), -500, 200));
	if (!Elapsed)
	{
		UE_LOG(LogTemp, Error, TEXT("[CrowdLegionExporter] Failed to create GetCrowdLegionFrame expression (Elapsed)."));
		return false;
	}
	Elapsed->A.Connect(0, ElapsedStep1);
	Elapsed->B.Connect(0, InSampleRate);

	// 3. Length = Max(1, EndFrame - StartFrame + 1)
	UMaterialExpressionSubtract* FrameSpan = Cast<UMaterialExpressionSubtract>(
		UMaterialEditingLibrary::CreateMaterialExpressionInFunction(
			OutFunc, UMaterialExpressionSubtract::StaticClass(), -900, 450));
	if (!FrameSpan)
	{
		UE_LOG(LogTemp, Error, TEXT("[CrowdLegionExporter] Failed to create GetCrowdLegionFrame expression (FrameSpan)."));
		return false;
	}
	FrameSpan->A.Connect(0, InEndFrame);
	FrameSpan->B.Connect(0, InStartFrame);

	UMaterialExpressionAdd* SpanPlusOne = Cast<UMaterialExpressionAdd>(
		UMaterialEditingLibrary::CreateMaterialExpressionInFunction(
			OutFunc, UMaterialExpressionAdd::StaticClass(), -700, 450));
	if (!SpanPlusOne)
	{
		UE_LOG(LogTemp, Error, TEXT("[CrowdLegionExporter] Failed to create GetCrowdLegionFrame expression (SpanPlusOne)."));
		return false;
	}
	SpanPlusOne->A.Connect(0, FrameSpan);
	SpanPlusOne->ConstB = 1.f;

	UMaterialExpressionMax* Length = Cast<UMaterialExpressionMax>(
		UMaterialEditingLibrary::CreateMaterialExpressionInFunction(
			OutFunc, UMaterialExpressionMax::StaticClass(), -500, 450));
	if (!Length)
	{
		UE_LOG(LogTemp, Error, TEXT("[CrowdLegionExporter] Failed to create GetCrowdLegionFrame expression (Length)."));
		return false;
	}
	Length->ConstA = 1.f;
	Length->B.Connect(0, SpanPlusOne);

	// 4. Loop path: Floor(Fmod(Elapsed, Length))
	UMaterialExpressionFmod* LoopFmod = Cast<UMaterialExpressionFmod>(
		UMaterialEditingLibrary::CreateMaterialExpressionInFunction(
			OutFunc, UMaterialExpressionFmod::StaticClass(), -300, 50));
	if (!LoopFmod)
	{
		UE_LOG(LogTemp, Error, TEXT("[CrowdLegionExporter] Failed to create GetCrowdLegionFrame expression (LoopFmod)."));
		return false;
	}
	LoopFmod->A.Connect(0, Elapsed);
	LoopFmod->B.Connect(0, Length);

	UMaterialExpressionFloor* LoopOffset = Cast<UMaterialExpressionFloor>(
		UMaterialEditingLibrary::CreateMaterialExpressionInFunction(
			OutFunc, UMaterialExpressionFloor::StaticClass(), -100, 50));
	if (!LoopOffset)
	{
		UE_LOG(LogTemp, Error, TEXT("[CrowdLegionExporter] Failed to create GetCrowdLegionFrame expression (LoopOffset)."));
		return false;
	}
	LoopOffset->Input.Connect(0, LoopFmod);

	// 5. One-shot path: Min(Length - 1, Floor(Elapsed))
	UMaterialExpressionFloor* FloorElapsed = Cast<UMaterialExpressionFloor>(
		UMaterialEditingLibrary::CreateMaterialExpressionInFunction(
			OutFunc, UMaterialExpressionFloor::StaticClass(), -300, 350));
	if (!FloorElapsed)
	{
		UE_LOG(LogTemp, Error, TEXT("[CrowdLegionExporter] Failed to create GetCrowdLegionFrame expression (FloorElapsed)."));
		return false;
	}
	FloorElapsed->Input.Connect(0, Elapsed);

	UMaterialExpressionSubtract* LengthMinusOne = Cast<UMaterialExpressionSubtract>(
		UMaterialEditingLibrary::CreateMaterialExpressionInFunction(
			OutFunc, UMaterialExpressionSubtract::StaticClass(), -300, 500));
	if (!LengthMinusOne)
	{
		UE_LOG(LogTemp, Error, TEXT("[CrowdLegionExporter] Failed to create GetCrowdLegionFrame expression (LengthMinusOne)."));
		return false;
	}
	LengthMinusOne->A.Connect(0, Length);
	LengthMinusOne->ConstB = 1.f;

	UMaterialExpressionMin* OneShotOffset = Cast<UMaterialExpressionMin>(
		UMaterialEditingLibrary::CreateMaterialExpressionInFunction(
			OutFunc, UMaterialExpressionMin::StaticClass(), -100, 400));
	if (!OneShotOffset)
	{
		UE_LOG(LogTemp, Error, TEXT("[CrowdLegionExporter] Failed to create GetCrowdLegionFrame expression (OneShotOffset)."));
		return false;
	}
	OneShotOffset->A.Connect(0, LengthMinusOne);
	OneShotOffset->B.Connect(0, FloorElapsed);

	// 6. bLoop from Playrate >= 0 via If
	UMaterialExpressionIf* LoopSwitch = Cast<UMaterialExpressionIf>(
		UMaterialEditingLibrary::CreateMaterialExpressionInFunction(
			OutFunc, UMaterialExpressionIf::StaticClass(), 100, 200));
	if (!LoopSwitch)
	{
		UE_LOG(LogTemp, Error, TEXT("[CrowdLegionExporter] Failed to create GetCrowdLegionFrame expression (LoopSwitch)."));
		return false;
	}
	LoopSwitch->A.Connect(0, InPlayrate);
	LoopSwitch->ConstB = 0.f;
	LoopSwitch->AGreaterThanB.Connect(0, LoopOffset);
	LoopSwitch->AEqualsB.Connect(0, LoopOffset);
	LoopSwitch->ALessThanB.Connect(0, OneShotOffset);

	// 7. Frame = StartFrame + Offset
	UMaterialExpressionAdd* Frame = Cast<UMaterialExpressionAdd>(
		UMaterialEditingLibrary::CreateMaterialExpressionInFunction(
			OutFunc, UMaterialExpressionAdd::StaticClass(), 300, 200));
	if (!Frame)
	{
		UE_LOG(LogTemp, Error, TEXT("[CrowdLegionExporter] Failed to create GetCrowdLegionFrame expression (Frame)."));
		return false;
	}
	Frame->A.Connect(0, InStartFrame);
	Frame->B.Connect(0, LoopSwitch);

	UMaterialExpressionFunctionOutput* FrameOutput = Cast<UMaterialExpressionFunctionOutput>(
		UMaterialEditingLibrary::CreateMaterialExpressionInFunction(
			OutFunc, UMaterialExpressionFunctionOutput::StaticClass(), 500, 200));
	if (!FrameOutput)
	{
		UE_LOG(LogTemp, Error, TEXT("[CrowdLegionExporter] Failed to create GetCrowdLegionFrame expression (FrameOutput)."));
		return false;
	}
	FrameOutput->OutputName = TEXT("Frame");
	FrameOutput->A.Connect(0, Frame);

	OutFunc->UpdateInputOutputTypes();
	UMaterialEditingLibrary::UpdateMaterialFunction(OutFunc);
	OutFunc->PostEditChange();
	OutFunc->MarkPackageDirty();
	FAssetRegistryModule::AssetCreated(OutFunc);
	UE_LOG(LogTemp, Log, TEXT("[CrowdLegionExporter] Created GetCrowdLegionFrame at '%s'."), GGetCrowdLegionFramePkg);
	return true;
}

static bool ConnectFunctionCallInput(UMaterialExpressionMaterialFunctionCall* Call, const FName& InputName, UMaterialExpression* Source)
{
	if (!Call || !Source)
	{
		return false;
	}

	for (FFunctionExpressionInput& FuncInput : Call->FunctionInputs)
	{
		if (FuncInput.ExpressionInput && FuncInput.ExpressionInput->InputName == InputName)
		{
			FuncInput.Input.Connect(0, Source);
			return true;
		}
	}

	UE_LOG(LogTemp, Error, TEXT("[CrowdLegionExporter] Material function '%s' is missing input '%s'."),
		Call->MaterialFunction ? *Call->MaterialFunction->GetName() : TEXT("None"), *InputName.ToString());
	return false;
}

// PICD[0..3] = TimeOffset, Playrate, StartFrame, EndFrame → GetCrowdLegionFrame (+ Time, SampleRate).
static UMaterialExpressionMaterialFunctionCall* AddCrowdLegionFrameCall(
	UMaterial* Material,
	UMaterialFunction* GetCrowdLegionFrameMF,
	int32 PosX,
	int32 PosY)
{
	if (!Material || !GetCrowdLegionFrameMF)
	{
		return nullptr;
	}

	auto AddPicd = [&](int32 DataIndex, float DefaultValue, int32 Y) -> UMaterialExpressionPerInstanceCustomData*
	{
		UMaterialExpressionPerInstanceCustomData* Node = NewObject<UMaterialExpressionPerInstanceCustomData>(Material);
		Node->DataIndex = DataIndex;
		Node->ConstDefaultValue = DefaultValue;
		Node->MaterialExpressionEditorX = PosX - 500;
		Node->MaterialExpressionEditorY = Y;
		Material->GetExpressionCollection().AddExpression(Node);
		return Node;
	};

	UMaterialExpressionPerInstanceCustomData* CD_TimeOffset = AddPicd(0, 0.0f, PosY - 200);
	UMaterialExpressionPerInstanceCustomData* CD_Playrate = AddPicd(1, 1.0f, PosY - 80);
	UMaterialExpressionPerInstanceCustomData* CD_StartFrame = AddPicd(2, 0.0f, PosY + 40);
	UMaterialExpressionPerInstanceCustomData* CD_EndFrame = AddPicd(3, 1.0f, PosY + 160);

	UMaterialExpressionTime* TimeExpr = NewObject<UMaterialExpressionTime>(Material);
	TimeExpr->bIgnorePause = true;
	TimeExpr->MaterialExpressionEditorX = PosX - 500;
	TimeExpr->MaterialExpressionEditorY = PosY + 300;
	Material->GetExpressionCollection().AddExpression(TimeExpr);

	UMaterialExpressionScalarParameter* SampleRateParam = NewObject<UMaterialExpressionScalarParameter>(Material);
	SampleRateParam->ParameterName = TEXT("SampleRate");
	SampleRateParam->DefaultValue = 30.0f;
	SampleRateParam->MaterialExpressionEditorX = PosX - 500;
	SampleRateParam->MaterialExpressionEditorY = PosY + 420;
	Material->GetExpressionCollection().AddExpression(SampleRateParam);

	UMaterialExpressionMaterialFunctionCall* FrameCall = NewObject<UMaterialExpressionMaterialFunctionCall>(Material);
	FrameCall->MaterialExpressionEditorX = PosX;
	FrameCall->MaterialExpressionEditorY = PosY;
	Material->GetExpressionCollection().AddExpression(FrameCall);
	FrameCall->SetMaterialFunction(GetCrowdLegionFrameMF);
	FrameCall->UpdateFromFunctionResource();

	if (!ConnectFunctionCallInput(FrameCall, TEXT("TimeOffset"), CD_TimeOffset) ||
		!ConnectFunctionCallInput(FrameCall, TEXT("Playrate"), CD_Playrate) ||
		!ConnectFunctionCallInput(FrameCall, TEXT("StartFrame"), CD_StartFrame) ||
		!ConnectFunctionCallInput(FrameCall, TEXT("EndFrame"), CD_EndFrame) ||
		!ConnectFunctionCallInput(FrameCall, TEXT("SampleRate"), SampleRateParam) ||
		!ConnectFunctionCallInput(FrameCall, TEXT("Time"), TimeExpr))
	{
		return nullptr;
	}

	return FrameCall;
}


bool FCrowdLegionExporter::EnsurePluginBoneAnimationMF(UMaterialFunction*& OutBoneAnimMF)
{
	UMaterialFunction* GetCrowdLegionFrameMF = nullptr;
	if (!EnsureGetCrowdLegionFrame(GetCrowdLegionFrameMF))
	{
		UE_LOG(LogTemp, Error, TEXT("[CrowdLegionExporter] Failed to ensure GetCrowdLegionFrame before MF_CrowdLegionBoneAnimation."));
		return false;
	}

	// 检查或创建 /VgCrowdLegion/Materials/MF_CrowdLegionBoneAnimation
	OutBoneAnimMF = Cast<UMaterialFunction>(StaticLoadObject(UMaterialFunction::StaticClass(), nullptr, *(FString(GPluginMFPackage) + TEXT(".MF_CrowdLegionBoneAnimation"))));
	if (!OutBoneAnimMF)
	{
		UMaterialFunction* SourceMF = Cast<UMaterialFunction>(StaticLoadObject(
			UMaterialFunction::StaticClass(), nullptr, *(FString(GLegacyPluginMFPackage) + TEXT(".MF_BoneAnimation"))));
		const bool bMigratedFromLegacy = (SourceMF != nullptr);
		if (!SourceMF)
		{
			SourceMF = Cast<UMaterialFunction>(StaticLoadObject(UMaterialFunction::StaticClass(), nullptr, GOfficialMFPath));
			if (!SourceMF)
			{
				UE_LOG(LogTemp, Error, TEXT("[CrowdLegionExporter] Official MF_BoneAnimation template not found: %s"), GOfficialMFPath);
				return false;
			}
		}

		UPackage* MFPackage = CreatePackage(GPluginMFPackage);
		if (!MFPackage) return false;
		MFPackage->FullyLoad();

		OutBoneAnimMF = Cast<UMaterialFunction>(StaticDuplicateObject(SourceMF, MFPackage, FName(TEXT("MF_CrowdLegionBoneAnimation"))));
		if (!OutBoneAnimMF) return false;

		OutBoneAnimMF->PreEditChange(nullptr);

		// 定位内部的 BlendBonePositionAndNormal 节点
		UMaterialExpressionMaterialFunctionCall* BlendCall = nullptr;
		for (const TObjectPtr<UMaterialExpression>& Expr : OutBoneAnimMF->GetExpressions())
		{
			if (UMaterialExpressionMaterialFunctionCall* FC = Cast<UMaterialExpressionMaterialFunctionCall>(Expr))
			{
				if (FC->MaterialFunction && FC->MaterialFunction->GetName().Contains(TEXT("BlendBonePositionAndNormal")))
				{
					BlendCall = FC;
					break;
				}
			}
		}

		if (BlendCall)
		{
			BlendCall->UpdateFromFunctionResource();

			// 查找或创建 FunctionInput: Frame 节点
			UMaterialExpressionFunctionInput* FrameInputExpr = nullptr;
			for (const TObjectPtr<UMaterialExpression>& Expr : OutBoneAnimMF->GetExpressions())
			{
				if (UMaterialExpressionFunctionInput* FI = Cast<UMaterialExpressionFunctionInput>(Expr))
				{
					if (FI->InputName == TEXT("Frame"))
					{
						FrameInputExpr = FI;
						break;
					}
				}
			}

			if (!FrameInputExpr)
			{
				FrameInputExpr = NewObject<UMaterialExpressionFunctionInput>(OutBoneAnimMF);
				FrameInputExpr->InputName = TEXT("Frame");
				FrameInputExpr->Description = TEXT("Custom Per-Instance Animation Frame");
				FrameInputExpr->InputType = FunctionInput_Scalar;
				FrameInputExpr->bUsePreviewValueAsDefault = false;
				FrameInputExpr->MaterialExpressionEditorX = -1200;
				FrameInputExpr->MaterialExpressionEditorY = 200;
				OutBoneAnimMF->GetExpressionCollection().AddExpression(FrameInputExpr);
			}

			// 连接至 BlendBonePositionAndNormal 的 Frame 引脚
			for (FFunctionExpressionInput& In : BlendCall->FunctionInputs)
			{
				if (In.ExpressionInput && In.ExpressionInput->InputName == TEXT("Frame"))
				{
					In.Input.Connect(0, FrameInputExpr);
					break;
				}
			}
		}

		OutBoneAnimMF->UpdateInputOutputTypes();
		OutBoneAnimMF->PostEditChange();
		OutBoneAnimMF->MarkPackageDirty();
		FAssetRegistryModule::AssetCreated(OutBoneAnimMF);
		if (bMigratedFromLegacy)
		{
			UE_LOG(LogTemp, Log, TEXT("[CrowdLegionExporter] Migrated legacy MF_BoneAnimation to MF_CrowdLegionBoneAnimation at '%s'."), GPluginMFPackage);
		}
		else
		{
			UE_LOG(LogTemp, Log, TEXT("[CrowdLegionExporter] Successfully created plugin MF_CrowdLegionBoneAnimation at '%s'!"), GPluginMFPackage);
		}
	}

	return (OutBoneAnimMF != nullptr);
}

static void EnsureFreeA2TUVChannel(UStaticMesh* StaticMesh, int32 LODIndex, int32 A2TUVChannel, bool bAlwaysApplySetLightMapIndex)
{
	if (!StaticMesh || !StaticMesh->IsSourceModelValid(LODIndex) || A2TUVChannel < 0)
	{
		return;
	}

	while (StaticMesh->GetNumUVChannels(LODIndex) < A2TUVChannel)
	{
		if (!StaticMesh->AddUVChannel(LODIndex))
		{
			break;
		}
	}

	const FStaticMeshSourceModel& SourceModel = StaticMesh->GetSourceModel(LODIndex);
	const bool bConflict = SourceModel.BuildSettings.bGenerateLightmapUVs &&
		SourceModel.BuildSettings.DstLightmapIndex == A2TUVChannel;

	if (bAlwaysApplySetLightMapIndex || bConflict)
	{
		const int32 TargetLightmapIndex = FMath::Max(2, A2TUVChannel + 1);
		bool bAddedAll = true;
		while (StaticMesh->GetNumUVChannels(LODIndex) <= TargetLightmapIndex)
		{
			if (!StaticMesh->AddUVChannel(LODIndex))
			{
				bAddedAll = false;
				break;
			}
		}

		if (bAddedAll)
		{
			const bool bOk = UAnimToTextureBPLibrary::SetLightMapIndex(StaticMesh, LODIndex, TargetLightmapIndex, true);
			UE_LOG(LogTemp, Log, TEXT("[CrowdLegionExporter] SetLightMapIndex on '%s' (LOD=%d, LightmapIndex=%d, GenerateLightmapUVs=true) -> %s."),
				*StaticMesh->GetName(), LODIndex, TargetLightmapIndex, bOk ? TEXT("OK") : TEXT("Failed"));
		}
		else
		{
			UAnimToTextureBPLibrary::SetLightMapIndex(StaticMesh, LODIndex, 0, false);
			UE_LOG(LogTemp, Warning, TEXT("[CrowdLegionExporter] Max UV channels reached on '%s'; disabled Lightmap UV generation to free UV%d for A2T."),
				*StaticMesh->GetName(), A2TUVChannel);
		}
	}
}

UStaticMesh* FCrowdLegionExporter::ExportStaticMeshFromSkeletalMesh(USkeletalMesh* SkelMesh, const FString& TargetStaticMeshPath)
{
	if (!SkelMesh)
	{
		return nullptr;
	}

	UStaticMesh* OutStaticMesh = UAnimToTextureBPLibrary::ConvertSkeletalMeshToStaticMesh(SkelMesh, TargetStaticMeshPath, 0);
	if (!OutStaticMesh)
	{
		return nullptr;
	}

	// Keep all material slots and original UVs — no atlas quadrant remap / slot collapse.
	// 预留默认 UV1 给 A2T（避免 ConvertSkeletalMeshToStaticMesh 默认将 Lightmap UV 写在 UV1 引起烘焙冲突）
	EnsureFreeA2TUVChannel(OutStaticMesh, 0, 1, false);
	OutStaticMesh->PostEditChange();
	OutStaticMesh->MarkPackageDirty();
	FAssetRegistryModule::AssetCreated(OutStaticMesh);
	UE_LOG(LogTemp, Log, TEXT("[CrowdLegionExporter] Exported StaticMesh '%s' with %d material slot(s)."),
		*TargetStaticMeshPath, OutStaticMesh->GetStaticMaterials().Num());
	return OutStaticMesh;
}


UTexture2D* FCrowdLegionExporter::FindOrCreateVATTexture(UAnimToTextureDataAsset* DataAsset, const FString& Suffix)
{
	if (!DataAsset) return nullptr;

	const FString BaseDir = FPackageName::GetLongPackagePath(DataAsset->GetOutermost()->GetName());
	const FString TextureAssetName = DataAsset->GetName() + Suffix;
	const FString TargetPackageName = BaseDir / TEXT("Textures") / TextureAssetName;

	UPackage* Package = CreatePackage(*TargetPackageName);
	if (!Package) return nullptr;
	Package->FullyLoad();

	UTexture2D* Texture = FindObject<UTexture2D>(Package, *TextureAssetName);
	if (!Texture && FPackageName::DoesPackageExist(TargetPackageName))
	{
		Texture = Cast<UTexture2D>(StaticLoadObject(UTexture2D::StaticClass(), nullptr, *(TargetPackageName + TEXT(".") + TextureAssetName)));
	}
	if (!Texture)
	{
		Texture = NewObject<UTexture2D>(Package, FName(*TextureAssetName), RF_Public | RF_Standalone);
		if (Texture)
		{
			FAssetRegistryModule::AssetCreated(Texture);
			Texture->MarkPackageDirty();
		}
	}
	return Texture;
}

UTexture2D* FCrowdLegionExporter::EnsureVATTexture(UAnimToTextureDataAsset* DataAsset, TSoftObjectPtr<UTexture2D>& SoftPtr, const FString& Suffix)
{
	if (!DataAsset)
	{
		return nullptr;
	}

	const FString BaseDir = FPackageName::GetLongPackagePath(DataAsset->GetOutermost()->GetName());
	const FString TargetTexturesDir = BaseDir / TEXT("Textures");
	const FSoftObjectPath SoftPath = SoftPtr.ToSoftObjectPath();
	const bool bInTargetTexturesDir = SoftPath.IsValid() &&
		(FPackageName::GetLongPackagePath(SoftPath.GetLongPackageName()) == TargetTexturesDir);

	if (bInTargetTexturesDir)
	{
		// 已在内存中：直接用，绕过官方 GetAsset 的 ensureMsgf
		if (UTexture2D* Existing = SoftPtr.Get())
		{
			return Existing;
		}

		// Soft 路径有效但磁盘资产缺失时，官方 Get*Texture() 会 ensure 崩溃；这里用 TryLoad 安全探测
		if (UTexture2D* Loaded = Cast<UTexture2D>(SoftPath.TryLoad()))
		{
			return Loaded;
		}

		UE_LOG(LogTemp, Warning, TEXT("[CrowdLegionExporter] VAT texture soft ref missing '%s'; creating under '%s'."),
			*SoftPath.ToString(), *TargetTexturesDir);
		SoftPtr.Reset();
	}
	else if (SoftPath.IsValid())
	{
		UE_LOG(LogTemp, Log, TEXT("[CrowdLegionExporter] Updating VAT texture '%s' into '%s'."),
			*SoftPath.ToString(), *TargetTexturesDir);
		SoftPtr.Reset();
	}

	UTexture2D* Created = FindOrCreateVATTexture(DataAsset, Suffix);
	if (Created)
	{
		SoftPtr = Created;
		DataAsset->MarkPackageDirty();
	}
	return Created;
}


static FExpressionInput* GetMakeMAInput(UMaterialExpressionMakeMaterialAttributes* MakeMA, EMaterialProperty Prop)
{
	if (!MakeMA)
	{
		return nullptr;
	}
	switch (Prop)
	{
	case MP_BaseColor: return &MakeMA->BaseColor;
	case MP_Specular: return &MakeMA->Specular;
	case MP_Normal: return &MakeMA->Normal;
	case MP_Tangent: return &MakeMA->Tangent;
	case MP_Metallic: return &MakeMA->Metallic;
	case MP_Roughness: return &MakeMA->Roughness;
	case MP_Anisotropy: return &MakeMA->Anisotropy;
	case MP_AmbientOcclusion: return &MakeMA->AmbientOcclusion;
	case MP_EmissiveColor: return &MakeMA->EmissiveColor;
	case MP_Opacity: return &MakeMA->Opacity;
	case MP_OpacityMask: return &MakeMA->OpacityMask;
	case MP_SubsurfaceColor: return &MakeMA->SubsurfaceColor;
	case MP_WorldPositionOffset: return &MakeMA->WorldPositionOffset;
	case MP_Displacement: return &MakeMA->Displacement;
	case MP_ShadingModel: return &MakeMA->ShadingModel;
	case MP_Refraction: return &MakeMA->Refraction;
	case MP_PixelDepthOffset: return &MakeMA->PixelDepthOffset;
	case MP_CustomData0: return &MakeMA->ClearCoat;
	case MP_CustomData1: return &MakeMA->ClearCoatRoughness;
	default: return nullptr;
	}
}

UMaterial* FCrowdLegionExporter::DuplicateAndConvertMaterialToA2T(
	UMaterial* SourceMat,
	const FString& TargetPackagePath,
	const FName TargetAssetName,
	UMaterialFunction* BoneAnimMF,
	UMaterialFunction* GetCrowdLegionFrameMF)
{
	if (!SourceMat || TargetPackagePath.IsEmpty() || TargetAssetName.IsNone() || !BoneAnimMF || !GetCrowdLegionFrameMF)
	{
		return nullptr;
	}

	UPackage* MatPackage = CreatePackage(*TargetPackagePath);
	if (!MatPackage)
	{
		return nullptr;
	}
	MatPackage->FullyLoad();

	// 若已存在同名对象，先重命名到 Transient 避免 Duplicate 冲突
	if (UObject* ExistingObj = FindObject<UObject>(MatPackage, *TargetAssetName.ToString()))
	{
		ExistingObj->Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors | REN_NonTransactional);
	}

	UMaterial* NewMat = Cast<UMaterial>(StaticDuplicateObject(SourceMat, MatPackage, TargetAssetName));
	if (!NewMat)
	{
		UE_LOG(LogTemp, Error, TEXT("[CrowdLegionExporter] Failed to duplicate material '%s' to '%s'."),
			*SourceMat->GetName(), *TargetPackagePath);
		return nullptr;
	}

	NewMat->PreEditChange(nullptr);
	NewMat->SetMaterialUsage(MATUSAGE_InstancedStaticMeshes);

	UMaterialEditorOnlyData* EditorOnly = NewMat->GetEditorOnlyData();

	// 计算现有节点布局范围
	int32 MaxX = 0;
	int32 CenterY = 0;
	const TArray<TObjectPtr<UMaterialExpression>>& Expressions = NewMat->GetExpressionCollection().Expressions;
	if (Expressions.Num() > 0)
	{
		int32 MinY = MAX_int32;
		int32 MaxY = MIN_int32;
		for (const auto& Expr : Expressions)
		{
			if (Expr)
			{
				MaxX = FMath::Max(MaxX, Expr->MaterialExpressionEditorX);
				MinY = FMath::Min(MinY, Expr->MaterialExpressionEditorY);
				MaxY = FMath::Max(MaxY, Expr->MaterialExpressionEditorY);
			}
		}
		CenterY = (MinY == MAX_int32) ? 0 : (MinY + MaxY) / 2;
	}
	const int32 MakeMAX = MaxX + 300;
	const int32 FrameCallX = MakeMAX;
	const int32 BoneAnimX = MakeMAX + 400;

	UMaterialExpression* AttributeSourceExpr = nullptr;
	int32 AttributeSourceOutputIndex = 0;

	if (NewMat->bUseMaterialAttributes && EditorOnly->MaterialAttributes.Expression)
	{
		AttributeSourceExpr = EditorOnly->MaterialAttributes.Expression;
		AttributeSourceOutputIndex = EditorOnly->MaterialAttributes.OutputIndex;
	}
	else
	{
		if (!NewMat->bUseMaterialAttributes && EditorOnly->FrontMaterial.IsConnected())
		{
			UE_LOG(LogTemp, Warning, TEXT("[CrowdLegionExporter] Source material '%s' connects Substrate FrontMaterial directly; MakeMaterialAttributes cannot automatically unpack Substrate FrontMaterial. Please check '%s'."),
				*SourceMat->GetName(), *TargetAssetName.ToString());
		}

		UMaterialExpressionMakeMaterialAttributes* MakeMA = NewObject<UMaterialExpressionMakeMaterialAttributes>(NewMat);
		MakeMA->MaterialExpressionEditorX = MakeMAX;
		MakeMA->MaterialExpressionEditorY = CenterY - 150;
		NewMat->GetExpressionCollection().AddExpression(MakeMA);

		static const EMaterialProperty StandardProperties[] = {
			MP_BaseColor,
			MP_Metallic,
			MP_Specular,
			MP_Roughness,
			MP_Anisotropy,
			MP_Normal,
			MP_Tangent,
			MP_EmissiveColor,
			MP_Opacity,
			MP_OpacityMask,
			MP_WorldPositionOffset,
			MP_SubsurfaceColor,
			MP_CustomData0,
			MP_CustomData1,
			MP_AmbientOcclusion,
			MP_Refraction,
			MP_PixelDepthOffset,
			MP_ShadingModel,
			MP_Displacement
		};

		for (EMaterialProperty Prop : StandardProperties)
		{
			FExpressionInput* OrigInput = NewMat->GetExpressionInputForProperty(Prop);
			FExpressionInput* DestInput = GetMakeMAInput(MakeMA, Prop);
			if (OrigInput && DestInput && OrigInput->Expression)
			{
				DestInput->Expression = OrigInput->Expression;
				DestInput->OutputIndex = OrigInput->OutputIndex;
				DestInput->InputName = OrigInput->InputName;
				DestInput->Mask = OrigInput->Mask;
				DestInput->MaskR = OrigInput->MaskR;
				DestInput->MaskG = OrigInput->MaskG;
				DestInput->MaskB = OrigInput->MaskB;
				DestInput->MaskA = OrigInput->MaskA;

				OrigInput->Expression = nullptr;
				OrigInput->OutputIndex = 0;
			}
		}

		for (int32 UVIndex = 0; UVIndex < 8; ++UVIndex)
		{
			if (EditorOnly->CustomizedUVs[UVIndex].Expression)
			{
				MakeMA->CustomizedUVs[UVIndex].Expression = EditorOnly->CustomizedUVs[UVIndex].Expression;
				MakeMA->CustomizedUVs[UVIndex].OutputIndex = EditorOnly->CustomizedUVs[UVIndex].OutputIndex;
				MakeMA->CustomizedUVs[UVIndex].Mask = EditorOnly->CustomizedUVs[UVIndex].Mask;
				MakeMA->CustomizedUVs[UVIndex].MaskR = EditorOnly->CustomizedUVs[UVIndex].MaskR;
				MakeMA->CustomizedUVs[UVIndex].MaskG = EditorOnly->CustomizedUVs[UVIndex].MaskG;
				MakeMA->CustomizedUVs[UVIndex].MaskB = EditorOnly->CustomizedUVs[UVIndex].MaskB;
				MakeMA->CustomizedUVs[UVIndex].MaskA = EditorOnly->CustomizedUVs[UVIndex].MaskA;

				EditorOnly->CustomizedUVs[UVIndex].Expression = nullptr;
				EditorOnly->CustomizedUVs[UVIndex].OutputIndex = 0;
			}
		}

		AttributeSourceExpr = MakeMA;
		AttributeSourceOutputIndex = 0;
	}

	NewMat->bUseMaterialAttributes = true;

	UMaterialExpressionMaterialFunctionCall* FrameCall = AddCrowdLegionFrameCall(NewMat, GetCrowdLegionFrameMF, FrameCallX, CenterY + 250);
	if (!FrameCall)
	{
		UE_LOG(LogTemp, Error, TEXT("[CrowdLegionExporter] Failed to add GetCrowdLegionFrame call for '%s'."), *TargetAssetName.ToString());
		NewMat->PostEditChange();
		return nullptr;
	}

	UMaterialExpressionMaterialFunctionCall* BoneAnimCall = NewObject<UMaterialExpressionMaterialFunctionCall>(NewMat);
	BoneAnimCall->MaterialExpressionEditorX = BoneAnimX;
	BoneAnimCall->MaterialExpressionEditorY = CenterY;
	NewMat->GetExpressionCollection().AddExpression(BoneAnimCall);
	BoneAnimCall->SetMaterialFunction(BoneAnimMF);
	BoneAnimCall->UpdateFromFunctionResource();

	bool bConnectedAttributes = false;
	bool bConnectedFrame = false;
	for (FFunctionExpressionInput& FuncInput : BoneAnimCall->FunctionInputs)
	{
		if (!FuncInput.ExpressionInput)
		{
			continue;
		}
		const FName InputName = FuncInput.ExpressionInput->InputName;
		if (InputName == TEXT("Material Attributes") || FuncInput.ExpressionInput->InputType == FunctionInput_MaterialAttributes)
		{
			FuncInput.Input.Connect(AttributeSourceOutputIndex, AttributeSourceExpr);
			bConnectedAttributes = true;
		}
		else if (InputName == TEXT("Frame"))
		{
			FuncInput.Input.Connect(0, FrameCall);
			bConnectedFrame = true;
		}
	}

	if (!bConnectedAttributes || !bConnectedFrame)
	{
		UE_LOG(LogTemp, Error, TEXT("[CrowdLegionExporter] BoneAnimation inputs failed on '%s' (Attr=%d Frame=%d)."),
			*TargetAssetName.ToString(), bConnectedAttributes ? 1 : 0, bConnectedFrame ? 1 : 0);
		NewMat->PostEditChange();
		return nullptr;
	}

	// 确保 FrontMaterial 保持为空，仅连接 MaterialAttributes
	UMaterialEditingLibrary::DisconnectMaterialProperty(NewMat, MP_FrontMaterial);
	EditorOnly->FrontMaterial.Expression = nullptr;
	EditorOnly->FrontMaterial.OutputIndex = 0;

	FString MaOutputName = TEXT("Material Attributes");
	int32 MaOutputIndex = INDEX_NONE;
	for (int32 OutIdx = 0; OutIdx < BoneAnimCall->FunctionOutputs.Num(); ++OutIdx)
	{
		if (UMaterialExpressionFunctionOutput* OutputExpr = BoneAnimCall->FunctionOutputs[OutIdx].ExpressionOutput.Get())
		{
			const FString OutputName = OutputExpr->OutputName.ToString();
			if (OutputName.Contains(TEXT("Material Attributes")) || OutputName.Contains(TEXT("MaterialAttributes")))
			{
				MaOutputName = OutputName;
				MaOutputIndex = OutIdx;
				break;
			}
		}
	}
	if (MaOutputIndex == INDEX_NONE && BoneAnimCall->FunctionOutputs.Num() > 0)
	{
		MaOutputIndex = 0;
		if (UMaterialExpressionFunctionOutput* OutputExpr = BoneAnimCall->FunctionOutputs[0].ExpressionOutput.Get())
		{
			MaOutputName = OutputExpr->OutputName.ToString();
		}
	}

	if (MaOutputIndex != INDEX_NONE)
	{
		EditorOnly->MaterialAttributes.Connect(MaOutputIndex, BoneAnimCall);
		UMaterialEditingLibrary::ConnectMaterialProperty(BoneAnimCall, *MaOutputName, MP_MaterialAttributes);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("[CrowdLegionExporter] MF_CrowdLegionBoneAnimation has no output pins on '%s'."),
			*TargetAssetName.ToString());
	}

	if (NewMat->MaterialGraph)
	{
		NewMat->MaterialGraph->RebuildGraph();
	}
	UMaterialEditingLibrary::RecompileMaterial(NewMat);
	NewMat->PostEditChange();
	NewMat->MarkPackageDirty();
	FAssetRegistryModule::AssetCreated(NewMat);

	UE_LOG(LogTemp, Log, TEXT("[CrowdLegionExporter] Successfully generated A2T Material '%s' from source '%s'."),
		*TargetPackagePath, *SourceMat->GetName());
	return NewMat;
}

static UMaterial* GetOrCreateConvertedParentMaterial(
	UMaterial* BaseParentMat,
	const FString& TargetDir,
	UMaterialFunction* BoneAnimMF,
	UMaterialFunction* GetCrowdLegionFrameMF,
	TMap<UMaterial*, UMaterial*>& InOutParentCache)
{
	if (!BaseParentMat)
	{
		return nullptr;
	}
	if (UMaterial** CachedParent = InOutParentCache.Find(BaseParentMat))
	{
		return *CachedParent;
	}

	const FString NewMatAssetName = BaseParentMat->GetName() + TEXT("_A2T");
	const FString NewMatPkgPath = TargetDir / NewMatAssetName;
	UMaterial* NewMat = FCrowdLegionExporter::DuplicateAndConvertMaterialToA2T(
		BaseParentMat, NewMatPkgPath, FName(*NewMatAssetName), BoneAnimMF, GetCrowdLegionFrameMF);
	if (NewMat)
	{
		InOutParentCache.Add(BaseParentMat, NewMat);
	}
	return NewMat;
}

static UMaterialInterface* ConvertMaterialInterfaceToA2T_Internal(
	UMaterialInterface* SourceMatInterface,
	const FString& TargetDir,
	UMaterialFunction* BoneAnimMF,
	UMaterialFunction* GetCrowdLegionFrameMF,
	TMap<UMaterialInterface*, UMaterialInterface*>& InOutCache,
	TMap<UMaterial*, UMaterial*>& InOutParentCache)
{
	if (!SourceMatInterface)
	{
		return nullptr;
	}

	if (UMaterialInterface** Cached = InOutCache.Find(SourceMatInterface))
	{
		return *Cached;
	}

	const FString SourceName = SourceMatInterface->GetName();

	// 情况 1: 直接是 UMaterial (非 Instance)
	if (UMaterial* SourceMat = Cast<UMaterial>(SourceMatInterface))
	{
		UMaterial* NewMat = GetOrCreateConvertedParentMaterial(SourceMat, TargetDir, BoneAnimMF, GetCrowdLegionFrameMF, InOutParentCache);
		if (!NewMat)
		{
			return nullptr;
		}

		// 为直接赋给网格槽的 A2T Material 创建一个材质实例，以便接收 A2T 烘焙写入的 VAT 贴图参数
		const FString NewMICAssetName = TEXT("MI_") + SourceName + TEXT("_A2T");
		const FString NewMICPkgPath = TargetDir / NewMICAssetName;
		UPackage* MICPackage = CreatePackage(*NewMICPkgPath);
		if (!MICPackage)
		{
			InOutCache.Add(SourceMatInterface, NewMat);
			return NewMat;
		}
		MICPackage->FullyLoad();

		UMaterialInstanceConstant* NewMIC = FindObject<UMaterialInstanceConstant>(MICPackage, *NewMICAssetName);
		bool bCreated = false;
		if (!NewMIC)
		{
			NewMIC = NewObject<UMaterialInstanceConstant>(MICPackage, FName(*NewMICAssetName), RF_Public | RF_Standalone);
			bCreated = true;
		}
		if (NewMIC)
		{
			NewMIC->PreEditChange(nullptr);
			NewMIC->SetParentEditorOnly(NewMat);
			UMaterialEditingLibrary::UpdateMaterialInstance(NewMIC);
			NewMIC->PostEditChange();
			NewMIC->MarkPackageDirty();
			if (bCreated)
			{
				FAssetRegistryModule::AssetCreated(NewMIC);
			}
			InOutCache.Add(SourceMatInterface, NewMIC);
			return NewMIC;
		}

		InOutCache.Add(SourceMatInterface, NewMat);
		return NewMat;
	}

	// 情况 2: 是 UMaterialInstanceConstant
	if (UMaterialInstanceConstant* SourceMIC = Cast<UMaterialInstanceConstant>(SourceMatInterface))
	{
		UMaterial* BaseParentMat = SourceMIC->GetMaterial();
		UMaterial* ConvertedParentMat = GetOrCreateConvertedParentMaterial(
			BaseParentMat, TargetDir, BoneAnimMF, GetCrowdLegionFrameMF, InOutParentCache);
		if (!ConvertedParentMat)
		{
			return nullptr;
		}

		const FString NewMICName = SourceName.StartsWith(TEXT("MI_")) ? (SourceName + TEXT("_A2T")) : (TEXT("MI_") + SourceName + TEXT("_A2T"));
		const FString NewMICPkgPath = TargetDir / NewMICName;

		UPackage* MICPackage = CreatePackage(*NewMICPkgPath);
		if (!MICPackage)
		{
			return nullptr;
		}
		MICPackage->FullyLoad();

		UMaterialInstanceConstant* NewMIC = FindObject<UMaterialInstanceConstant>(MICPackage, *NewMICName);
		bool bCreated = false;
		if (!NewMIC)
		{
			NewMIC = NewObject<UMaterialInstanceConstant>(MICPackage, FName(*NewMICName), RF_Public | RF_Standalone);
			bCreated = true;
		}

		if (NewMIC)
		{
			NewMIC->PreEditChange(nullptr);
			NewMIC->SetParentEditorOnly(ConvertedParentMat);
			NewMIC->BasePropertyOverrides = SourceMIC->BasePropertyOverrides;
			NewMIC->CopyMaterialUniformParametersEditorOnly(SourceMIC, true);
			UMaterialEditingLibrary::UpdateMaterialInstance(NewMIC);
			NewMIC->PostEditChange();
			NewMIC->MarkPackageDirty();
			if (bCreated)
			{
				FAssetRegistryModule::AssetCreated(NewMIC);
			}
			InOutCache.Add(SourceMatInterface, NewMIC);
			UE_LOG(LogTemp, Log, TEXT("[CrowdLegionExporter] Successfully generated A2T Material Instance '%s' (Parent='%s')."),
				*NewMICPkgPath, *ConvertedParentMat->GetName());
			return NewMIC;
		}
	}

	return nullptr;
}

UMaterialInterface* FCrowdLegionExporter::ConvertMaterialInterfaceToA2T(
	UMaterialInterface* SourceMatInterface,
	const FString& TargetDir,
	UMaterialFunction* BoneAnimMF,
	UMaterialFunction* GetCrowdLegionFrameMF,
	TMap<UMaterialInterface*, UMaterialInterface*>& InOutCache)
{
	TMap<UMaterial*, UMaterial*> LocalParentCache;
	for (const TPair<UMaterialInterface*, UMaterialInterface*>& Pair : InOutCache)
	{
		if (UMaterial* SrcMat = Cast<UMaterial>(Pair.Key))
		{
			if (UMaterial* DstMat = Cast<UMaterial>(Pair.Value))
			{
				LocalParentCache.Add(SrcMat, DstMat);
			}
			else if (UMaterialInstance* DstMI = Cast<UMaterialInstance>(Pair.Value))
			{
				if (UMaterial* ParentMat = Cast<UMaterial>(DstMI->Parent))
				{
					LocalParentCache.Add(SrcMat, ParentMat);
				}
			}
		}
	}
	return ConvertMaterialInterfaceToA2T_Internal(
		SourceMatInterface, TargetDir, BoneAnimMF, GetCrowdLegionFrameMF, InOutCache, LocalParentCache);
}

bool FCrowdLegionExporter::GenerateAndAssignA2TMaterialsForStaticMesh(
	USkeletalMesh* SkelMesh,
	UStaticMesh* StaticMesh,
	const FString& TargetDir,
	TArray<UMaterialInterface*>& OutAssignedMaterials)
{
	OutAssignedMaterials.Reset();
	if (!SkelMesh || !StaticMesh)
	{
		return false;
	}

	UMaterialFunction* BoneAnimMF = nullptr;
	if (!EnsurePluginBoneAnimationMF(BoneAnimMF) || !BoneAnimMF)
	{
		UE_LOG(LogTemp, Error, TEXT("[CrowdLegionExporter] Failed to ensure MF_CrowdLegionBoneAnimation."));
		return false;
	}

	UMaterialFunction* GetCrowdLegionFrameMF = nullptr;
	if (!EnsureGetCrowdLegionFrame(GetCrowdLegionFrameMF) || !GetCrowdLegionFrameMF)
	{
		UE_LOG(LogTemp, Error, TEXT("[CrowdLegionExporter] Failed to ensure GetCrowdLegionFrame."));
		return false;
	}

	const TArray<FSkeletalMaterial>& SkelMaterials = SkelMesh->GetMaterials();
	const int32 NumSlots = SkelMaterials.Num();

	TMap<UMaterialInterface*, UMaterialInterface*> DeduplicationCache;
	TMap<UMaterial*, UMaterial*> ParentMatCache;
	OutAssignedMaterials.Reserve(NumSlots);

	for (int32 SlotIdx = 0; SlotIdx < NumSlots; ++SlotIdx)
	{
		UMaterialInterface* SourceMat = SkelMaterials[SlotIdx].MaterialInterface;
		if (!SourceMat)
		{
			UE_LOG(LogTemp, Warning, TEXT("[CrowdLegionExporter] SkeletalMesh '%s' Slot %d has null material."),
				*SkelMesh->GetName(), SlotIdx);
			OutAssignedMaterials.Add(nullptr);
			continue;
		}

		UMaterialInterface* A2TMat = ConvertMaterialInterfaceToA2T_Internal(
			SourceMat, TargetDir, BoneAnimMF, GetCrowdLegionFrameMF, DeduplicationCache, ParentMatCache);
		if (A2TMat)
		{
			StaticMesh->SetMaterial(SlotIdx, A2TMat);
			OutAssignedMaterials.Add(A2TMat);
			UE_LOG(LogTemp, Log, TEXT("[CrowdLegionExporter] Assigned A2T Material '%s' to StaticMesh Slot %d."),
				*A2TMat->GetName(), SlotIdx);
		}
		else
		{
			OutAssignedMaterials.Add(SourceMat);
		}
	}

	StaticMesh->PostEditChange();
	StaticMesh->MarkPackageDirty();
	return true;
}

bool FCrowdLegionExporter::BakeA2TDataAsset(UAnimToTextureDataAsset* DataAsset, UStaticMesh* BakedMesh)
{
	if (!DataAsset || !BakedMesh)
	{
		return false;
	}

	if (DataAsset->GetStaticMesh() != BakedMesh)
	{
		DataAsset->StaticMesh = BakedMesh;
	}

	if (DataAsset->Mode == EAnimToTextureMode::Bone)
	{
		// 勿调用 GetBonePositionTexture() 等官方 accessor：Soft 指向丢失资产时 ensure 会崩
		if (!EnsureVATTexture(DataAsset, DataAsset->BonePositionTexture, TEXT("_BonePos")) ||
			!EnsureVATTexture(DataAsset, DataAsset->BoneRotationTexture, TEXT("_BoneRot")) ||
			!EnsureVATTexture(DataAsset, DataAsset->BoneWeightTexture, TEXT("_BoneWeights")))
		{
			UE_LOG(LogTemp, Error, TEXT("[CrowdLegionExporter] Failed to ensure Bone Pos/Rot/Weight VAT textures for '%s'."),
				*DataAsset->GetName());
			return false;
		}
	}
	else if (DataAsset->Mode == EAnimToTextureMode::Vertex)
	{
		if (!EnsureVATTexture(DataAsset, DataAsset->VertexPositionTexture, TEXT("_Pos")) ||
			!EnsureVATTexture(DataAsset, DataAsset->VertexNormalTexture, TEXT("_Norm")))
		{
			UE_LOG(LogTemp, Error, TEXT("[CrowdLegionExporter] Failed to ensure Vertex Pos/Norm VAT textures for '%s'."),
				*DataAsset->GetName());
			return false;
		}
	}
	DataAsset->MarkPackageDirty();

	// 参考 BP_AnimToTexture 流程:
	// 1. SetLightMapIndex (将 Lightmap 移出 UVChannel，默认 LightmapIndex=2, bGenerateLightmapUVs=true)
	EnsureFreeA2TUVChannel(BakedMesh, DataAsset->StaticLODIndex, DataAsset->UVChannel, /*bAlwaysApplySetLightMapIndex=*/true);

	// 2. AnimationToTexture
	const bool bBakeSuccess = UAnimToTextureBPLibrary::AnimationToTexture(DataAsset);
	if (!bBakeSuccess)
	{
		UE_LOG(LogTemp, Error, TEXT("[CrowdLegionExporter] UAnimToTextureBPLibrary::AnimationToTexture failed for '%s'."),
			*DataAsset->GetName());
		return false;
	}

	if (DataAsset->Mode == EAnimToTextureMode::Bone)
	{
		if (UTexture2D* Tex = DataAsset->BonePositionTexture.Get()) Tex->MarkPackageDirty();
		if (UTexture2D* Tex = DataAsset->BoneRotationTexture.Get()) Tex->MarkPackageDirty();
		if (UTexture2D* Tex = DataAsset->BoneWeightTexture.Get()) Tex->MarkPackageDirty();
	}
	else if (DataAsset->Mode == EAnimToTextureMode::Vertex)
	{
		if (UTexture2D* Tex = DataAsset->VertexPositionTexture.Get()) Tex->MarkPackageDirty();
		if (UTexture2D* Tex = DataAsset->VertexNormalTexture.Get()) Tex->MarkPackageDirty();
	}
	BakedMesh->MarkPackageDirty();

	// 3. 对 StaticMesh 槽位上的材质实例执行 UpdateMaterialInstanceFromDataAsset (GlobalParameter)
	bool bHasA2TSlots = false;
	TSet<UMaterialInstanceConstant*> UpdatedMICs;
	const int32 NumSlots = BakedMesh->GetStaticMaterials().Num();
	for (int32 SlotIdx = 0; SlotIdx < NumSlots; ++SlotIdx)
	{
		UMaterialInterface* SlotMat = BakedMesh->GetMaterial(SlotIdx);
		if (UMaterialInstanceConstant* MIC = Cast<UMaterialInstanceConstant>(SlotMat))
		{
			if (!UpdatedMICs.Contains(MIC))
			{
				UAnimToTextureBPLibrary::UpdateMaterialInstanceFromDataAsset(DataAsset, MIC, EMaterialParameterAssociation::GlobalParameter);
				UpdatedMICs.Add(MIC);
			}
			bHasA2TSlots = true;
		}
	}

	if (!bHasA2TSlots)
	{
		UE_LOG(LogTemp, Warning, TEXT("[CrowdLegionExporter] StaticMesh '%s' has no UMaterialInstanceConstant slots; VAT textures baked without MI parameter sync."),
			*BakedMesh->GetName());
	}

	return true;
}
UDataTable* FCrowdLegionExporter::GenerateAnimMetaDataTable(UAnimToTextureDataAsset* DataAsset, const FString& TargetPackagePath)
{
	if (!DataAsset || TargetPackagePath.IsEmpty())
	{
		return nullptr;
	}

	UPackage* TablePackage = CreatePackage(*TargetPackagePath);
	if (!TablePackage)
	{
		return nullptr;
	}
	TablePackage->FullyLoad();

	const FString TableAssetName = FPackageName::GetShortName(TargetPackagePath);
	UDataTable* DataTable = FindObject<UDataTable>(TablePackage, *TableAssetName);
	TMap<FName, bool> ExistingDefaultLoops;
	if (!DataTable)
	{
		DataTable = NewObject<UDataTable>(TablePackage, FName(*TableAssetName), RF_Public | RF_Standalone);
		if (DataTable)
		{
			DataTable->RowStruct = FUnitAnimationTableRow::StaticStruct();
			FAssetRegistryModule::AssetCreated(DataTable);
		}
	}
	else
	{
		DataTable->RowStruct = FUnitAnimationTableRow::StaticStruct();
		static const FString PreserveContext(TEXT("CrowdLegionExporter::GenerateAnimMetaDataTable.PreserveLoop"));
		TArray<FUnitAnimationTableRow*> ExistingRows;
		DataTable->GetAllRows<FUnitAnimationTableRow>(PreserveContext, ExistingRows);
		for (const FUnitAnimationTableRow* Existing : ExistingRows)
		{
			if (Existing && !Existing->AnimName.IsNone())
			{
				ExistingDefaultLoops.Add(Existing->AnimName, Existing->bDefaultLoop);
			}
		}
		DataTable->EmptyTable();
	}

	if (!DataTable)
	{
		return nullptr;
	}

	int32 AccumulatedFrames = 0;
	for (int32 i = 0; i < DataAsset->AnimSequences.Num(); ++i)
	{
		const FAnimToTextureAnimSequenceInfo& SeqInfo = DataAsset->AnimSequences[i];
		if (!SeqInfo.bEnabled || !SeqInfo.AnimSequence)
		{
			continue;
		}

		int32 FrameCount = 0;
		if (SeqInfo.bUseCustomRange)
		{
			FrameCount = SeqInfo.EndFrame - SeqInfo.StartFrame + 1;
		}
		else if (SeqInfo.AnimSequence)
		{
			FrameCount = SeqInfo.AnimSequence->GetNumberOfSampledKeys();
		}
		FrameCount = FMath::Max(1, FrameCount);

		const FName SeqName = SeqInfo.AnimSequence->GetFName();
		const FString NameStr = SeqName.ToString().ToLower();

		bool bLoop = true;
		if (NameStr.Contains(TEXT("death")) || NameStr.Contains(TEXT("die")) ||
			NameStr.Contains(TEXT("attack")) || NameStr.Contains(TEXT("poke")) ||
			NameStr.Contains(TEXT("prick")) || NameStr.Contains(TEXT("cut")) ||
			NameStr.Contains(TEXT("hit")) || NameStr.Contains(TEXT("hurt")))
		{
			bLoop = false;
		}

		FUnitAnimationTableRow Row;
		Row.AnimName = SeqName;
		Row.StartFrame = AccumulatedFrames;
		Row.EndFrame = AccumulatedFrames + FrameCount - 1;
		if (const bool* ExistingLoop = ExistingDefaultLoops.Find(SeqName))
		{
			Row.bDefaultLoop = *ExistingLoop;
		}
		else
		{
			Row.bDefaultLoop = bLoop;
		}

		const FName RowName = *FString::Printf(TEXT("Anim_%d_%s"), i, *SeqName.ToString());
		DataTable->AddRow(RowName, Row);

		AccumulatedFrames += FrameCount;
	}

	DataTable->PostEditChange();
	DataTable->MarkPackageDirty();
	UE_LOG(LogTemp, Log, TEXT("[CrowdLegionExporter] Successfully generated animation metadata DataTable at '%s'."), *TargetPackagePath);
	return DataTable;
}

