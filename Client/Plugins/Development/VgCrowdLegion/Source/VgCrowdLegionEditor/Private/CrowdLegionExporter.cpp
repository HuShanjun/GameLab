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
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionPerInstanceCustomData.h"
#include "Materials/MaterialExpressionTime.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionMaterialFunctionCall.h"
#include "Materials/MaterialExpressionFunctionInput.h"
#include "Materials/MaterialExpressionTextureSampleParameter2D.h"
#include "Materials/MaterialExpressionSubstrate.h"
#include "MaterialEditingLibrary.h"
#include "MaterialGraph/MaterialGraph.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/Package.h"
#include "ImageUtils.h"
#include "ImageCore.h"
#include "MeshDescription.h"
#include "StaticMeshAttributes.h"
#include "HAL/IConsoleManager.h"
#include "Misc/PackageName.h"
#include "Engine/DataTable.h"
#include "CrowdUnitComponent.h"

static const TCHAR* GPluginMFPackage = TEXT("/VgCrowdLegion/Materials/MF_BoneAnimation");
static const TCHAR* GPluginMasterMatPackage = TEXT("/VgCrowdLegion/Materials/M_Crowd_Legion");
static const TCHAR* GOfficialMFPath = TEXT("/AnimToTexture/Materials/MaterialFunctions/MF_BoneAnimation.MF_BoneAnimation");
static const TCHAR* GOfficialMasterMatPath = TEXT("/AnimToTexture/Characters/Mannequin/Materials/BoneAnimation/M_Body_BoneAnimation.M_Body_BoneAnimation");

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

void FCrowdLegionExporter::ExecuteExportFromSelectedAsset()
{
	// 3.1: 获取当前在资产 Content 中选中的 DataAsset，没有则弹框提醒用户
	UAnimToTextureDataAsset* SelectedDataAsset = GetSelectedAnimToTextureDataAsset();
	if (!SelectedDataAsset)
	{
		FMessageDialog::Open(
			EAppMsgType::Ok,
			FText::FromString(TEXT("【A2T 军团资产导出】未选中有效资产！\n\n请先在 Content Browser (内容浏览器) 中单选或高亮一个 AnimToTextureDataAsset 资产，然后再次点击此选项。")));
		return;
	}

	USkeletalMesh* SkelMesh = SelectedDataAsset->GetSkeletalMesh();
	if (!SkelMesh)
	{
		FMessageDialog::Open(
			EAppMsgType::Ok,
			FText::FromString(FString::Printf(TEXT("【A2T 军团资产导出】选中的 DataAsset '%s' 未指定 SkeletalMesh！\n\n请在 DataAsset 中配置源骨骼网格体后再执行导出。"), *SelectedDataAsset->GetName())));
		return;
	}

	const FString DataAssetPackage = SelectedDataAsset->GetOutermost()->GetName();
	const FString BaseDir = FPackageName::GetLongPackagePath(DataAssetPackage);
	const FString SkelMeshName = SkelMesh->GetName();

	// 3.4: 检查并确保当前插件材质目录下有 M_Crowd_Legion 和 MF_BoneAnimation
	UMaterial* MasterMaterial = nullptr;
	UMaterialFunction* BoneAnimMF = nullptr;
	if (!EnsurePluginMasterMaterials(MasterMaterial, BoneAnimMF))
	{
		FMessageDialog::Open(
			EAppMsgType::Ok,
			FText::FromString(TEXT("【A2T 军团资产导出】创建或检查插件基础材质 M_Crowd_Legion 失败，请检查 Output Log。")));
		return;
	}

	// 3.3: 根据 SkeletalMesh 的材质合并其材质贴图生成 Atlas
	const FString AtlasPkgPath = BaseDir / (TEXT("T_") + SkelMeshName + TEXT("_Atlas"));
	UTexture2D* AtlasTexture = GenerateAtlasTextureFromSkelMesh(SkelMesh, AtlasPkgPath);

	// 3.2: 根据里面的 SkeletalMesh 导出 StaticMesh 并执行 UV 重映射与 1 插槽合并
	const FString StaticMeshPkgPath = BaseDir / (TEXT("SM_") + SkelMeshName + TEXT("_A2T"));
	UStaticMesh* BakedStaticMesh = ExportStaticMeshWithAtlasUVRemap(SkelMesh, StaticMeshPkgPath);
	if (!BakedStaticMesh)
	{
		FMessageDialog::Open(
			EAppMsgType::Ok,
			FText::FromString(TEXT("【A2T 军团资产导出】从 SkeletalMesh 导出 StaticMesh 失败！")));
		return;
	}

	SelectedDataAsset->StaticMesh = BakedStaticMesh;
	SelectedDataAsset->MarkPackageDirty();

	// 3.5 & 3.6: 创建材质实例、设置贴图、创建 Bone 贴图并烘焙 A2T 资产
	const bool bBakeSuccess = BakeA2TDataAsset(SelectedDataAsset, MasterMaterial, BakedStaticMesh, AtlasTexture);

	// 3.7: 自动生成或更新动画元数据 DataTable (描述 AnimName, StartFrame, NumFrames, bDefaultLoop)
	const FString TablePkgPath = BaseDir / (TEXT("DT_") + SkelMeshName + TEXT("_AnimMeta"));
	UDataTable* AnimMetaTable = GenerateAnimMetaDataTable(SelectedDataAsset, TablePkgPath);

	if (bBakeSuccess)
	{
		FMessageDialog::Open(
			EAppMsgType::Ok,
			FText::FromString(FString::Printf(TEXT("【A2T 军团资产导出成功！】\n\n- 目标 DataAsset: %s\n- 导出 StaticMesh: %s (1 Draw Call)\n- 合并 Atlas 贴图: %s\n- 配套材质实例: MI_%s_A2T\n- 动画元数据数据表: %s\n- 骨骼纹理与动画起止帧已全自动烘焙同步完成！"),
				*SelectedDataAsset->GetName(), *BakedStaticMesh->GetName(), AtlasTexture ? *AtlasTexture->GetName() : TEXT("None"),
				*SelectedDataAsset->GetName(), AnimMetaTable ? *AnimMetaTable->GetName() : TEXT("None"))));
	}
	else
	{
		FMessageDialog::Open(
			EAppMsgType::Ok,
			FText::FromString(TEXT("【A2T 军团资产导出】A2T 烘焙失败，请查看 Output Log 获取详细报错。")));
	}
}

bool FCrowdLegionExporter::EnsurePluginMasterMaterials(UMaterial*& OutMasterMat, UMaterialFunction*& OutBoneAnimMF)
{
	// 1. 检查或创建 /CrowdLegion/Materials/MF_BoneAnimation
	OutBoneAnimMF = Cast<UMaterialFunction>(StaticLoadObject(UMaterialFunction::StaticClass(), nullptr, *(FString(GPluginMFPackage) + TEXT(".MF_BoneAnimation"))));
	if (!OutBoneAnimMF)
	{
		UMaterialFunction* OfficialSourceMF = Cast<UMaterialFunction>(StaticLoadObject(UMaterialFunction::StaticClass(), nullptr, GOfficialMFPath));
		if (!OfficialSourceMF)
		{
			UE_LOG(LogTemp, Error, TEXT("[CrowdLegionExporter] Official MF_BoneAnimation template not found: %s"), GOfficialMFPath);
			return false;
		}

		UPackage* MFPackage = CreatePackage(GPluginMFPackage);
		if (!MFPackage) return false;
		MFPackage->FullyLoad();

		OutBoneAnimMF = Cast<UMaterialFunction>(StaticDuplicateObject(OfficialSourceMF, MFPackage, FName(TEXT("MF_BoneAnimation"))));
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
		UE_LOG(LogTemp, Log, TEXT("[CrowdLegionExporter] Successfully created plugin MF_BoneAnimation at '%s'!"), GPluginMFPackage);
	}

	// 2. 检查或创建 /CrowdLegion/Materials/M_Crowd_Legion
	OutMasterMat = Cast<UMaterial>(StaticLoadObject(UMaterial::StaticClass(), nullptr, *(FString(GPluginMasterMatPackage) + TEXT(".M_Crowd_Legion"))));
	if (!OutMasterMat)
	{
		UMaterial* OfficialSourceMat = Cast<UMaterial>(StaticLoadObject(UMaterial::StaticClass(), nullptr, GOfficialMasterMatPath));
		if (!OfficialSourceMat)
		{
			UE_LOG(LogTemp, Error, TEXT("[CrowdLegionExporter] Official M_Body_BoneAnimation template not found: %s"), GOfficialMasterMatPath);
			return false;
		}

		UPackage* MatPackage = CreatePackage(GPluginMasterMatPackage);
		if (!MatPackage) return false;
		MatPackage->FullyLoad();

		OutMasterMat = Cast<UMaterial>(StaticDuplicateObject(OfficialSourceMat, MatPackage, FName(TEXT("M_Crowd_Legion"))));
		if (!OutMasterMat) return false;

		OutMasterMat->PreEditChange(nullptr);

		// 重定向 MF_BoneAnimation 调用节点
		UMaterialExpressionMaterialFunctionCall* BoneAnimFuncCall = nullptr;
		for (const TObjectPtr<UMaterialExpression>& Expr : OutMasterMat->GetExpressions())
		{
			if (UMaterialExpressionMaterialFunctionCall* FC = Cast<UMaterialExpressionMaterialFunctionCall>(Expr))
			{
				if (FC->MaterialFunction && FC->MaterialFunction->GetName().Contains(TEXT("BoneAnimation")))
				{
					BoneAnimFuncCall = FC;
					break;
				}
			}
		}

		if (BoneAnimFuncCall)
		{
			BoneAnimFuncCall->MaterialFunction = OutBoneAnimMF;
			BoneAnimFuncCall->UpdateFromFunctionResource();
		}

		// 构建标准 PBR 与 Substrate 兼容表面着色网络
		UMaterialEditorOnlyData* EditorOnly = OutMasterMat->GetEditorOnlyData();

		UMaterialExpressionTextureSampleParameter2D* BaseColorParam = NewObject<UMaterialExpressionTextureSampleParameter2D>(OutMasterMat);
		BaseColorParam->ParameterName = TEXT("BaseColorTexture");
		BaseColorParam->MaterialExpressionEditorX = -600;
		BaseColorParam->MaterialExpressionEditorY = -400;
		OutMasterMat->GetExpressionCollection().AddExpression(BaseColorParam);

		UMaterialExpressionScalarParameter* MetallicParam = NewObject<UMaterialExpressionScalarParameter>(OutMasterMat);
		MetallicParam->ParameterName = TEXT("Metallic");
		MetallicParam->DefaultValue = 0.0f;
		MetallicParam->MaterialExpressionEditorX = -600;
		MetallicParam->MaterialExpressionEditorY = -150;
		OutMasterMat->GetExpressionCollection().AddExpression(MetallicParam);

		UMaterialExpressionScalarParameter* RoughnessParam = NewObject<UMaterialExpressionScalarParameter>(OutMasterMat);
		RoughnessParam->ParameterName = TEXT("Roughness");
		RoughnessParam->DefaultValue = 0.5f;
		RoughnessParam->MaterialExpressionEditorX = -600;
		RoughnessParam->MaterialExpressionEditorY = 50;
		OutMasterMat->GetExpressionCollection().AddExpression(RoughnessParam);

		// 检查 Substrate 是否开启
		static const auto CVarSubstrate = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.Substrate"));
		const bool bSubstrateEnabled = (CVarSubstrate && CVarSubstrate->GetValueOnAnyThread() != 0);

		if (bSubstrateEnabled)
		{
			UMaterialExpressionSubstrateShadingModels* SubstrateNode = NewObject<UMaterialExpressionSubstrateShadingModels>(OutMasterMat);
			SubstrateNode->MaterialExpressionEditorX = -250;
			SubstrateNode->MaterialExpressionEditorY = -200;
			SubstrateNode->ShadingModelOverride = MSM_DefaultLit;
			OutMasterMat->GetExpressionCollection().AddExpression(SubstrateNode);

			SubstrateNode->BaseColor.Connect(0, BaseColorParam);
			SubstrateNode->Metallic.Connect(0, MetallicParam);
			SubstrateNode->Roughness.Connect(0, RoughnessParam);

			EditorOnly->FrontMaterial.Connect(0, SubstrateNode);
			UMaterialEditingLibrary::ConnectMaterialProperty(SubstrateNode, TEXT(""), MP_FrontMaterial);
		}
		else
		{
			EditorOnly->BaseColor.Connect(0, BaseColorParam);
			EditorOnly->BaseColor.SetMask(1, 1, 1, 1, 0);
			EditorOnly->Metallic.Connect(0, MetallicParam);
			EditorOnly->Roughness.Connect(0, RoughnessParam);

			UMaterialEditingLibrary::ConnectMaterialProperty(BaseColorParam, TEXT("RGB"), MP_BaseColor);
			UMaterialEditingLibrary::ConnectMaterialProperty(MetallicParam, TEXT(""), MP_Metallic);
			UMaterialEditingLibrary::ConnectMaterialProperty(RoughnessParam, TEXT(""), MP_Roughness);
		}

		// 构造由 4 个 PerInstanceCustomData 驱动的高性能零分支算帧网络 (支持无限多种动画)
		// CustomData[0]: StartFrame (当前动画全局起始帧)
		UMaterialExpressionPerInstanceCustomData* CD_StartFrame = NewObject<UMaterialExpressionPerInstanceCustomData>(OutMasterMat);
		CD_StartFrame->DataIndex = 0;
		CD_StartFrame->ConstDefaultValue = 0.0f;
		CD_StartFrame->MaterialExpressionEditorX = -1000;
		CD_StartFrame->MaterialExpressionEditorY = 300;
		OutMasterMat->GetExpressionCollection().AddExpression(CD_StartFrame);

		// CustomData[1]: NumFrames (当前动画总帧长)
		UMaterialExpressionPerInstanceCustomData* CD_NumFrames = NewObject<UMaterialExpressionPerInstanceCustomData>(OutMasterMat);
		CD_NumFrames->DataIndex = 1;
		CD_NumFrames->ConstDefaultValue = 60.0f;
		CD_NumFrames->MaterialExpressionEditorX = -1000;
		CD_NumFrames->MaterialExpressionEditorY = 400;
		OutMasterMat->GetExpressionCollection().AddExpression(CD_NumFrames);

		// CustomData[2]: StartTime (动作开始时间戳，秒)
		UMaterialExpressionPerInstanceCustomData* CD_StartTime = NewObject<UMaterialExpressionPerInstanceCustomData>(OutMasterMat);
		CD_StartTime->DataIndex = 2;
		CD_StartTime->ConstDefaultValue = 0.0f;
		CD_StartTime->MaterialExpressionEditorX = -1000;
		CD_StartTime->MaterialExpressionEditorY = 500;
		OutMasterMat->GetExpressionCollection().AddExpression(CD_StartTime);

		// CustomData[3]: PackedRate (复合播放速率与循环标志: 正数表示循环, 负数表示单次播放并在最后一帧定格)
		UMaterialExpressionPerInstanceCustomData* CD_PackedRate = NewObject<UMaterialExpressionPerInstanceCustomData>(OutMasterMat);
		CD_PackedRate->DataIndex = 3;
		CD_PackedRate->ConstDefaultValue = 1.0f;
		CD_PackedRate->MaterialExpressionEditorX = -1000;
		CD_PackedRate->MaterialExpressionEditorY = 600;
		OutMasterMat->GetExpressionCollection().AddExpression(CD_PackedRate);

		// 全局时间节点 (使用 RealTime 确保在编辑器视口与游戏中均自增)
		UMaterialExpressionTime* TimeExpr = NewObject<UMaterialExpressionTime>(OutMasterMat);
		TimeExpr->bIgnorePause = true;
		TimeExpr->MaterialExpressionEditorX = -1000;
		TimeExpr->MaterialExpressionEditorY = 700;
		OutMasterMat->GetExpressionCollection().AddExpression(TimeExpr);

		// 采样帧率参数 (默认 30.0 FPS)
		UMaterialExpressionScalarParameter* SampleRateParam = NewObject<UMaterialExpressionScalarParameter>(OutMasterMat);
		SampleRateParam->ParameterName = TEXT("SampleRate");
		SampleRateParam->DefaultValue = 30.0f;
		SampleRateParam->MaterialExpressionEditorX = -1000;
		SampleRateParam->MaterialExpressionEditorY = 800;
		OutMasterMat->GetExpressionCollection().AddExpression(SampleRateParam);

		// 纯算术无分支通用 Custom 表达式
		UMaterialExpressionCustom* CustomCalc = NewObject<UMaterialExpressionCustom>(OutMasterMat);
		CustomCalc->OutputType = CMOT_Float1;
		CustomCalc->MaterialExpressionEditorX = -550;
		CustomCalc->MaterialExpressionEditorY = 500;
		CustomCalc->Inputs.Empty();

		auto AddInput = [&](const FName& Name, UMaterialExpression* InputExpr)
		{
			FCustomInput In;
			In.InputName = Name;
			In.Input.Connect(0, InputExpr);
			CustomCalc->Inputs.Add(In);
		};

		AddInput(TEXT("StartFrame"), CD_StartFrame);
		AddInput(TEXT("NumFrames"), CD_NumFrames);
		AddInput(TEXT("StartTime"), CD_StartTime);
		AddInput(TEXT("PackedRate"), CD_PackedRate);
		AddInput(TEXT("Time"), TimeExpr);
		AddInput(TEXT("SampleRate"), SampleRateParam);

		CustomCalc->Code = TEXT(
			"bool bLoop = (PackedRate >= 0.0);\n"
			"float PlayRate = max(0.0001, abs(PackedRate));\n"
			"float ElapsedSec = max(0.0, Time - StartTime);\n"
			"float ElapsedFrames = ElapsedSec * PlayRate * SampleRate;\n"
			"float Length = max(1.0, NumFrames);\n"
			"float FrameOffset = bLoop ? floor(fmod(ElapsedFrames, Length))\n"
			"                          : min(Length - 1.0, floor(ElapsedFrames));\n"
			"return StartFrame + FrameOffset;\n"
		);
		OutMasterMat->GetExpressionCollection().AddExpression(CustomCalc);

		// 连接 CustomCalc 到 MF_BoneAnimation 的 Frame 引脚
		if (BoneAnimFuncCall)
		{
			for (FFunctionExpressionInput& FuncInput : BoneAnimFuncCall->FunctionInputs)
			{
				if (FuncInput.ExpressionInput && FuncInput.ExpressionInput->InputName == TEXT("Frame"))
				{
					FuncInput.Input.Connect(0, CustomCalc);
					break;
				}
			}
		}

		if (OutMasterMat->MaterialGraph)
		{
			OutMasterMat->MaterialGraph->RebuildGraph();
		}
		UMaterialEditingLibrary::RecompileMaterial(OutMasterMat);
		OutMasterMat->PostEditChange();
		OutMasterMat->MarkPackageDirty();
		FAssetRegistryModule::AssetCreated(OutMasterMat);
		UE_LOG(LogTemp, Log, TEXT("[CrowdLegionExporter] Successfully created plugin M_Crowd_Legion at '%s'!"), GPluginMasterMatPackage);
	}

	return (OutMasterMat != nullptr && OutBoneAnimMF != nullptr);
}

/**
 * 语义部件象限分类器 (基于材质名称与插槽名称智能判定所属纹理象限)
 * 象限 0 (左上角 U:0~0.5, V:0~0.5): 身体 / 皮肤 / 头部 / 脸部
 * 象限 1 (右上角 U:0.5~1, V:0~0.5): 衣服 / 铠甲 / 战袍 / 裤子 / 护甲
 * 象限 2 (左下角 U:0~0.5, V:0.5~1): 武器 / 弓箭 / 刀剑 / 道具配件
 */
static int32 ClassifyMaterialQuadrant(const FString& MatName, const FString& SlotName)
{
	const FString Combined = (MatName + TEXT(" ") + SlotName).ToLower();

	// 1. 武器/道具配件判定 (最高优先级，包含 weapon, 4_mat, blade, sword, bow, arrow 等)
	if (Combined.Contains(TEXT("weapon")) || Combined.Contains(TEXT("color_4")) || Combined.Contains(TEXT("4_mat")) ||
		Combined.Contains(TEXT("blade")) || Combined.Contains(TEXT("sword")) || Combined.Contains(TEXT("bow")) || Combined.Contains(TEXT("arrow")))
	{
		return 2;
	}

	// 2. 身体/皮肤/脸部判定 (精确匹配身体与脸部，如 skin, body, face, head, aistandard9, 纯 color_mat)
	if (Combined.Contains(TEXT("aistandard9")) || Combined.Contains(TEXT("skin")) || Combined.Contains(TEXT("body")) ||
		Combined.Contains(TEXT("face")) || Combined.Contains(TEXT("head")) || Combined.Contains(TEXT("color_mat")))
	{
		return 0;
	}

	// 3. 衣服/铠甲/裤子/护甲部件 (所有未被判定为皮肤和武器的其余部件，如 aistandard8, equip, armor, cloth, 2_mat 等)
	return 1;
}

UTexture2D* FCrowdLegionExporter::GenerateAtlasTextureFromSkelMesh(USkeletalMesh* SkelMesh, const FString& TargetPackagePath)
{
	if (!SkelMesh) return nullptr;

	const int32 QuadSize = 2048;
	const int32 AtlasWidth = QuadSize * 2;  // 4096
	const int32 AtlasHeight = QuadSize * 2; // 4096

	TArray<FColor> AtlasPixels;
	AtlasPixels.SetNumUninitialized(AtlasWidth * AtlasHeight);
	for (FColor& Pixel : AtlasPixels)
	{
		Pixel = FColor(128, 128, 128, 255); // 默认填充中性灰
	}

	auto TryLoadTex = [](const FString& Path) -> UTexture2D*
	{
		return Cast<UTexture2D>(StaticLoadObject(UTexture2D::StaticClass(), nullptr, *Path));
	};

	// 针对 3 个核心象限分别分配贴图指针
	UTexture2D* QuadTextures[3] = { nullptr, nullptr, nullptr };

	// 遍历 SkeletalMesh 所有材质，按语义分类匹配各个象限贴图
	for (const FSkeletalMaterial& SkelMat : SkelMesh->GetMaterials())
	{
		const FString SlotName = SkelMat.MaterialSlotName.ToString().ToLower();
		const FString MatName = SkelMat.MaterialInterface ? SkelMat.MaterialInterface->GetName().ToLower() : TEXT("");
		const int32 TargetQuad = ClassifyMaterialQuadrant(MatName, SlotName);

		if (!QuadTextures[TargetQuad] && SkelMat.MaterialInterface)
		{
			TArray<UTexture*> Textures;
			SkelMat.MaterialInterface->GetTexturesInPropertyChain(MP_BaseColor, Textures, nullptr, nullptr);
			for (UTexture* Tex : Textures)
			{
				if (UTexture2D* T2D = Cast<UTexture2D>(Tex))
				{
					if (!T2D->GetPathName().StartsWith(TEXT("/Engine/")))
					{
						QuadTextures[TargetQuad] = T2D;
						break;
					}
				}
			}
		}
	}

	// 备选库自动兜底 (确保 3 个象限纹理完整无缺)
	if (!QuadTextures[0]) QuadTextures[0] = TryLoadTex(TEXT("/Game/Characters/Archer/Textures/T_Soldier_C_Color.T_Soldier_C_Color"));
	if (!QuadTextures[1]) QuadTextures[1] = TryLoadTex(TEXT("/Game/Characters/Archer/Textures/T_Soldier_C_Color_2.T_Soldier_C_Color_2"));
	if (!QuadTextures[2]) QuadTextures[2] = TryLoadTex(TEXT("/Game/Characters/Archer/Textures/T_Soldier_C_Color_4.T_Soldier_C_Color_4"));

	auto PasteTextureToQuadrant = [&](UTexture2D* SrcTex, int32 StartX, int32 StartY)
	{
		if (!SrcTex) return;

		FImage SrcImage;
		if (!FImageUtils::GetTexture2DSourceImage(SrcTex, SrcImage))
		{
			return;
		}

		FImage ResizedImage;
		SrcImage.ResizeTo(ResizedImage, QuadSize, QuadSize, ERawImageFormat::BGRA8, EGammaSpace::sRGB);

		const FColor* SrcColors = reinterpret_cast<const FColor*>(ResizedImage.RawData.GetData());
		for (int32 Row = 0; Row < QuadSize; ++Row)
		{
			FColor* DstRow = AtlasPixels.GetData() + (StartY + Row) * AtlasWidth + StartX;
			FMemory::Memcpy(DstRow, SrcColors + Row * QuadSize, QuadSize * sizeof(FColor));
		}
	};

	// 象限 0 (0, 0): 身体 / 皮肤 / 脸部 (左上角)
	PasteTextureToQuadrant(QuadTextures[0], 0, 0);

	// 象限 1 (2048, 0): 衣服 / 铠甲 / 裤子 (右上角)
	PasteTextureToQuadrant(QuadTextures[1], QuadSize, 0);

	// 象限 2 (0, 2048): 武器 / 装备配件 (左下角)
	PasteTextureToQuadrant(QuadTextures[2], 0, QuadSize);

	UPackage* Package = CreatePackage(*TargetPackagePath);
	if (!Package) return nullptr;
	Package->FullyLoad();

	const FString TextureAssetName = FPaths::GetBaseFilename(TargetPackagePath);
	FCreateTexture2DParameters Params;
	Params.bUseAlpha = false;
	Params.CompressionSettings = TC_Default;

	UTexture2D* AtlasTex = FImageUtils::CreateTexture2D(
		AtlasWidth, AtlasHeight, AtlasPixels,
		Package, TextureAssetName,
		RF_Public | RF_Standalone, Params);

	if (AtlasTex)
	{
		AtlasTex->PostEditChange();
		AtlasTex->MarkPackageDirty();
		FAssetRegistryModule::AssetCreated(AtlasTex);
		UE_LOG(LogTemp, Log, TEXT("[CrowdLegionExporter] Generated High-Res 4096x4096 Atlas: %s"), *TargetPackagePath);
	}
	return AtlasTex;
}

UStaticMesh* FCrowdLegionExporter::ExportStaticMeshWithAtlasUVRemap(USkeletalMesh* SkelMesh, const FString& TargetStaticMeshPath)
{
	if (!SkelMesh) return nullptr;

	UStaticMesh* OutStaticMesh = UAnimToTextureBPLibrary::ConvertSkeletalMeshToStaticMesh(SkelMesh, TargetStaticMeshPath, 0);
	if (!OutStaticMesh)
	{
		return nullptr;
	}

	OutStaticMesh->PreEditChange(nullptr);

	// 获取 MeshDescription 执行精准象限 UV0 重映射
	FMeshDescription* MeshDesc = OutStaticMesh->GetMeshDescription(0);
	if (!MeshDesc)
	{
		MeshDesc = OutStaticMesh->CreateMeshDescription(0);
	}

	if (MeshDesc)
	{
		FStaticMeshAttributes Attributes(*MeshDesc);
		TVertexInstanceAttributesRef<FVector2f> VertexInstanceUVs = Attributes.GetVertexInstanceUVs();

		// 遍历每个 PolygonGroupID，通过绑定的真实材质插槽名称进行精准象限分类
		for (const FPolygonGroupID PolygonGroupID : MeshDesc->PolygonGroups().GetElementIDs())
		{
			const FName SlotFName = Attributes.GetPolygonGroupMaterialSlotNames()[PolygonGroupID];
			const FString SlotName = SlotFName.ToString().ToLower();
			const int32 ActualSlotIndex = OutStaticMesh->GetMaterialIndex(SlotFName);

			FString MatName = TEXT("");
			if (OutStaticMesh->GetStaticMaterials().IsValidIndex(ActualSlotIndex) && OutStaticMesh->GetStaticMaterials()[ActualSlotIndex].MaterialInterface)
			{
				MatName = OutStaticMesh->GetStaticMaterials()[ActualSlotIndex].MaterialInterface->GetName().ToLower();
			}

			// 严密分类：绝对不会将铠甲/裤子错位分配给脸部！
			const int32 TargetQuad = ClassifyMaterialQuadrant(MatName, SlotName);

			FVector2f Scale(0.5f, 0.5f);
			FVector2f Offset(0.0f, 0.0f);

			if (TargetQuad == 1)
			{
				// 象限 1: 衣服 / 铠甲 / 裤子 (右上角)
				Offset = FVector2f(0.5f, 0.0f);
			}
			else if (TargetQuad == 2)
			{
				// 象限 2: 武器 / 配件 (左下角)
				Offset = FVector2f(0.0f, 0.5f);
			}
			else
			{
				// 象限 0: 身体 / 皮肤 / 脸部 (左上角)
				Offset = FVector2f(0.0f, 0.0f);
			}

			for (const FTriangleID TriangleID : MeshDesc->GetPolygonGroupTriangles(PolygonGroupID))
			{
				for (const FVertexInstanceID VertInstID : MeshDesc->GetTriangleVertexInstances(TriangleID))
				{
					FVector2f UV = VertexInstanceUVs.Get(VertInstID, 0);
					UV = UV * Scale + Offset;
					VertexInstanceUVs.Set(VertInstID, 0, UV);
				}
			}
		}

		OutStaticMesh->CommitMeshDescription(0);
	}

	// 压缩所有 Sections 为单一材质插槽 (1 Draw Call)
	const int32 NumLODs = OutStaticMesh->GetNumLODs();
	for (int32 LODIdx = 0; LODIdx < NumLODs; ++LODIdx)
	{
		const int32 NumSections = OutStaticMesh->GetNumSections(LODIdx);
		for (int32 SectionIdx = 0; SectionIdx < NumSections; ++SectionIdx)
		{
			FMeshSectionInfo SectionInfo = OutStaticMesh->GetSectionInfoMap().Get(LODIdx, SectionIdx);
			SectionInfo.MaterialIndex = 0;
			OutStaticMesh->GetSectionInfoMap().Set(LODIdx, SectionIdx, SectionInfo);
		}
	}
	OutStaticMesh->GetOriginalSectionInfoMap().CopyFrom(OutStaticMesh->GetSectionInfoMap());

	TArray<FStaticMaterial>& StaticMaterials = OutStaticMesh->GetStaticMaterials();
	if (StaticMaterials.Num() > 1)
	{
		StaticMaterials.SetNum(1);
	}

	OutStaticMesh->Build();
	OutStaticMesh->PostEditChange();
	OutStaticMesh->MarkPackageDirty();
	FAssetRegistryModule::AssetCreated(OutStaticMesh);

	return OutStaticMesh;
}

UTexture2D* FCrowdLegionExporter::FindOrCreateVATTexture(UAnimToTextureDataAsset* DataAsset, const FString& Suffix)
{
	if (!DataAsset) return nullptr;

	const FString AssetPath = DataAsset->GetOutermost()->GetName();
	const FString TargetPackageName = AssetPath + Suffix;
	const FString TextureAssetName = FPaths::GetBaseFilename(TargetPackageName);

	UPackage* Package = CreatePackage(*TargetPackageName);
	if (!Package) return nullptr;
	Package->FullyLoad();

	UTexture2D* Texture = FindObject<UTexture2D>(Package, *TextureAssetName);
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

void FCrowdLegionExporter::SyncMaterialInstanceRanges(UAnimToTextureDataAsset* DataAsset, UMaterialInstanceConstant* MatInst)
{
	// 纯算术通用管线下，动画起止帧与循环标志已全交由 PerInstanceCustomData 数据驱动，
	// 材质不再依赖任何硬编码的 AnimX_Range 参数。
	if (!DataAsset || !MatInst) return;
}

bool FCrowdLegionExporter::BakeA2TDataAsset(UAnimToTextureDataAsset* DataAsset, UMaterial* MasterMaterial, UStaticMesh* BakedMesh, UTexture2D* AtlasTexture)
{
	if (!DataAsset || !MasterMaterial || !BakedMesh) return false;

	// 1. 自动补全创建 Bone Pos, Rot, Weight 贴图
	if (DataAsset->Mode == EAnimToTextureMode::Bone)
	{
		if (!DataAsset->GetBonePositionTexture())
		{
			DataAsset->BonePositionTexture = FindOrCreateVATTexture(DataAsset, TEXT("_BonePos"));
		}
		if (!DataAsset->GetBoneRotationTexture())
		{
			DataAsset->BoneRotationTexture = FindOrCreateVATTexture(DataAsset, TEXT("_BoneRot"));
		}
		if (!DataAsset->GetBoneWeightTexture())
		{
			DataAsset->BoneWeightTexture = FindOrCreateVATTexture(DataAsset, TEXT("_BoneWeights"));
		}
	}
	DataAsset->MarkPackageDirty();

	// 2. 执行核心 A2T 烘焙
	const bool bBakeSuccess = UAnimToTextureBPLibrary::AnimationToTexture(DataAsset);
	if (!bBakeSuccess)
	{
		return false;
	}

	// 3. 创建材质实例并绑定
	const FString DataAssetPath = DataAsset->GetOutermost()->GetName();
	const FString MatPackageName = DataAssetPath + TEXT("_MatInst");
	const FString MatAssetName = FPaths::GetBaseFilename(MatPackageName);

	UPackage* MatPackage = CreatePackage(*MatPackageName);
	if (!MatPackage) return false;
	MatPackage->FullyLoad();

	UMaterialInstanceConstant* MatInst = FindObject<UMaterialInstanceConstant>(MatPackage, *MatAssetName);
	if (!MatInst)
	{
		MatInst = NewObject<UMaterialInstanceConstant>(MatPackage, FName(*MatAssetName), RF_Public | RF_Standalone);
		if (MatInst)
		{
			MatInst->SetParentEditorOnly(MasterMaterial);
			FAssetRegistryModule::AssetCreated(MatInst);
		}
	}
	else if (MatInst->Parent != MasterMaterial)
	{
		MatInst->SetParentEditorOnly(MasterMaterial);
	}

	if (MatInst)
	{
		// 官方库同步骨骼贴图与尺寸
		UAnimToTextureBPLibrary::UpdateMaterialInstanceFromDataAsset(DataAsset, MatInst, EMaterialParameterAssociation::GlobalParameter);

		// 设置合并后的 Atlas 贴图
		if (AtlasTexture)
		{
			MatInst->SetTextureParameterValueEditorOnly(FMaterialParameterInfo(TEXT("BaseColorTexture")), AtlasTexture);
		}

		// 同步动画区间
		SyncMaterialInstanceRanges(DataAsset, MatInst);

		MatInst->PostEditChange();
		MatInst->MarkPackageDirty();

		// 绑定至网格体
		BakedMesh->SetMaterial(0, MatInst);
		BakedMesh->PostEditChange();
		BakedMesh->MarkPackageDirty();
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

		const FName SeqName = SeqInfo.AnimSequence->GetFName();
		const FString NameStr = SeqName.ToString().ToLower();

		// 默认是否循环：根据名称进行合理初始预设，美术在 DataTable 中可随时自由调整
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
		Row.NumFrames = FMath::Max(1, FrameCount);
		Row.bDefaultLoop = bLoop;

		const FName RowName = *FString::Printf(TEXT("Anim_%d_%s"), i, *SeqName.ToString());
		DataTable->AddRow(RowName, Row);

		AccumulatedFrames += FrameCount;
	}

	DataTable->PostEditChange();
	DataTable->MarkPackageDirty();
	UE_LOG(LogTemp, Log, TEXT("[CrowdLegionExporter] Successfully generated animation metadata DataTable at '%s'."), *TargetPackagePath);
	return DataTable;
}
