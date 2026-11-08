// Copyright Epic Games, Inc. All Rights Reserved.

#include "CrowdLegionExporter.h"
#include "AnimToTextureDataAsset.h"
#include "AnimToTextureBPLibrary.h"
#include "ContentBrowserModule.h"
#include "IContentBrowserSingleton.h"
#include "Misc/MessageDialog.h"
#include "Misc/App.h"
#include "FileHelpers.h"
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
#include "ImageUtils.h"
#include "ImageCore.h"
#include "MeshDescription.h"
#include "StaticMeshAttributes.h"
#include "HAL/IConsoleManager.h"
#include "Misc/PackageName.h"
#include "Engine/DataTable.h"
#include "CrowdUnitComponent.h"

static FAutoConsoleCommand GGenerateArcherBoneAnimMatsCmd(
	TEXT("CrowdLegion.GenerateArcherBoneAnimationMaterials"),
	TEXT("Generate Archer BoneAnimation parent material and three BaseColor material instances."),
	FConsoleCommandDelegate::CreateStatic(&FCrowdLegionExporter::ExecuteGenerateArcherBoneAnimationMaterials));

static const TCHAR* GPluginMFPackage = TEXT("/VgCrowdLegion/Materials/MF_CrowdLegionBoneAnimation");
static const TCHAR* GPluginMasterMatPackage = TEXT("/VgCrowdLegion/Materials/M_Crowd_Legion");
static const TCHAR* GGetCrowdLegionFramePkg = TEXT("/VgCrowdLegion/Materials/GetCrowdLegionFrame");
static const TCHAR* GLegacyPluginMFPackage = TEXT("/VgCrowdLegion/Materials/MF_BoneAnimation");
static const TCHAR* GOfficialMFPath = TEXT("/AnimToTexture/Materials/MaterialFunctions/MF_BoneAnimation.MF_BoneAnimation");
static const TCHAR* GOfficialMasterMatPath = TEXT("/AnimToTexture/Characters/Mannequin/Materials/BoneAnimation/M_Body_BoneAnimation.M_Body_BoneAnimation");

static const TCHAR* GArcherBoneAnimParentPkg = TEXT("/Game/CrowdLegion/Characters/Archer/Materials/BoneAnimation/M_Archer_BoneAnimation");
static const TCHAR* GArcherBoneAnimDir = TEXT("/Game/CrowdLegion/Characters/Archer/Materials/BoneAnimation");
static const TCHAR* GTexSoldierColor = TEXT("/Game/CrowdLegion/Characters/Archer/Textures/T_Soldier_C_Color.T_Soldier_C_Color");
static const TCHAR* GTexSoldierColor2 = TEXT("/Game/CrowdLegion/Characters/Archer/Textures/T_Soldier_C_Color_2.T_Soldier_C_Color_2");
static const TCHAR* GTexSoldierColor4 = TEXT("/Game/CrowdLegion/Characters/Archer/Textures/T_Soldier_C_Color_4.T_Soldier_C_Color_4");

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

	// 确保 MF_CrowdLegionBoneAnimation / GetCrowdLegionFrame（BoneAnimation 父材质依赖）
	UMaterial* UnusedCrowdMaster = nullptr;
	UMaterialFunction* BoneAnimMF = nullptr;
	if (!EnsurePluginMasterMaterials(UnusedCrowdMaster, BoneAnimMF) || !BoneAnimMF)
	{
		FMessageDialog::Open(
			EAppMsgType::Ok,
			FText::FromString(TEXT("【A2T 军团资产导出】创建或检查 MF_CrowdLegionBoneAnimation 失败，请检查 Output Log。")));
		return;
	}

	// 导出 StaticMesh：保留多材质插槽与原始 UV
	const FString StaticMeshPkgPath = BaseDir / (TEXT("SM_") + SkelMeshName + TEXT("_A2T"));
	UStaticMesh* BakedStaticMesh = ExportStaticMeshFromSkeletalMesh(SkelMesh, StaticMeshPkgPath);
	if (!BakedStaticMesh)
	{
		FMessageDialog::Open(
			EAppMsgType::Ok,
			FText::FromString(TEXT("【A2T 军团资产导出】从 SkeletalMesh 导出 StaticMesh 失败！")));
		return;
	}

	SelectedDataAsset->StaticMesh = BakedStaticMesh;
	SelectedDataAsset->MarkPackageDirty();

	// A2T 烘焙 + 每槽 BoneAnimation MI
	const bool bBakeSuccess = BakeA2TDataAsset(SelectedDataAsset, BakedStaticMesh);

	// 动画元数据 DataTable (AnimName, StartFrame, EndFrame, bDefaultLoop)
	const FString TablePkgPath = BaseDir / (TEXT("DT_") + SkelMeshName + TEXT("_AnimMeta"));
	UDataTable* AnimMetaTable = GenerateAnimMetaDataTable(SelectedDataAsset, TablePkgPath);

	if (bBakeSuccess)
	{
		FMessageDialog::Open(
			EAppMsgType::Ok,
			FText::FromString(FString::Printf(TEXT("【A2T 军团资产导出成功！】\n\n- 目标 DataAsset: %s\n- 导出 StaticMesh: %s（%d 材质插槽）\n- 每槽 BoneAnimation 材质已生成并绑定\n- 动画元数据数据表: %s\n- 骨骼纹理与动画起止帧已全自动烘焙同步完成！"),
				*SelectedDataAsset->GetName(),
				*BakedStaticMesh->GetName(),
				BakedStaticMesh->GetStaticMaterials().Num(),
				AnimMetaTable ? *AnimMetaTable->GetName() : TEXT("None"))));
	}
	else
	{
		FMessageDialog::Open(
			EAppMsgType::Ok,
			FText::FromString(TEXT("【A2T 军团资产导出】A2T 烘焙失败，请查看 Output Log 获取详细报错。")));
	}
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

static void ClearMaterialExpressions(UMaterial* Material)
{
	if (!Material)
	{
		return;
	}

	TArray<UMaterialExpression*> ExistingExprs;
	for (const TObjectPtr<UMaterialExpression>& Expr : Material->GetExpressions())
	{
		if (Expr)
		{
			ExistingExprs.Add(Expr.Get());
		}
	}
	for (UMaterialExpression* Expr : ExistingExprs)
	{
		UMaterialEditingLibrary::DeleteMaterialExpression(Material, Expr);
	}
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

static int32 FindExpressionOutputIndex(UMaterialExpression* Expression, const FString& OutputName)
{
	if (!Expression || Expression->Outputs.Num() == 0)
	{
		return INDEX_NONE;
	}

	for (int32 OutIdx = 0; OutIdx < Expression->Outputs.Num(); ++OutIdx)
	{
		if (Expression->Outputs[OutIdx].OutputName.ToString() == OutputName)
		{
			return OutIdx;
		}
	}

	return 0;
}

static void ConnectBoneAnimOutputs(UMaterial* Material, UMaterialExpressionMaterialFunctionCall* BoneAnimCall)
{
	FString MaOutputName = TEXT("Material Attributes");
	for (const FFunctionExpressionOutput& FuncOutput : BoneAnimCall->FunctionOutputs)
	{
		if (UMaterialExpressionFunctionOutput* OutputExpr = FuncOutput.ExpressionOutput.Get())
		{
			const FString OutputName = OutputExpr->OutputName.ToString();
			if (OutputName.Contains(TEXT("Material Attributes")) || OutputName.Contains(TEXT("MaterialAttributes")))
			{
				MaOutputName = OutputName;
				break;
			}
		}
	}

	UMaterialEditorOnlyData* EditorOnly = Material->GetEditorOnlyData();
	// Clear Front Material before the MA hookup so a mid-edit compile cannot treat raw Material Attributes as Substrate data.
	UMaterialEditingLibrary::DisconnectMaterialProperty(Material, MP_FrontMaterial);

	const int32 MaOutputIndex = FindExpressionOutputIndex(BoneAnimCall, MaOutputName);
	if (MaOutputIndex != INDEX_NONE)
	{
		EditorOnly->MaterialAttributes.Connect(MaOutputIndex, BoneAnimCall);
	}
	UMaterialEditingLibrary::ConnectMaterialProperty(BoneAnimCall, *MaOutputName, MP_MaterialAttributes);

	// Same CVar the previous crowd graph used. Front Material must be Substrate data when this is on.
	static const auto CVarSubstrate = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.Substrate"));
	const bool bSubstrateEnabled = (CVarSubstrate && CVarSubstrate->GetValueOnAnyThread() != 0);
	if (!bSubstrateEnabled)
	{
		return;
	}

	// GetInputName(0) returns "Attributes"; the UPROPERTY is MaterialAttributes.
	UMaterialExpressionSubstrateConvertMaterialAttributes* ConvertNode = NewObject<UMaterialExpressionSubstrateConvertMaterialAttributes>(Material);
	ConvertNode->Material = Material;
	ConvertNode->ShadingModelOverride = MSM_DefaultLit;
	ConvertNode->MaterialExpressionEditorX = BoneAnimCall->MaterialExpressionEditorX + 300;
	ConvertNode->MaterialExpressionEditorY = BoneAnimCall->MaterialExpressionEditorY;
	Material->GetExpressionCollection().AddExpression(ConvertNode);

	if (EditorOnly->MaterialAttributes.Expression)
	{
		ConvertNode->MaterialAttributes.Connect(EditorOnly->MaterialAttributes.OutputIndex, EditorOnly->MaterialAttributes.Expression);
	}

	const FString ConvertInputName = ConvertNode->GetInputName(0).ToString();
	if (!ConvertNode->MaterialAttributes.GetTracedInput().Expression)
	{
		const bool bNamedConnect = !ConvertInputName.IsEmpty() && UMaterialEditingLibrary::ConnectMaterialExpressions(
			BoneAnimCall, MaOutputName, ConvertNode, ConvertInputName);
		if (!bNamedConnect || !ConvertNode->MaterialAttributes.Expression)
		{
			UE_LOG(LogTemp, Error, TEXT("[CrowdLegionExporter] Failed to connect bone MF '%s' to Substrate Convert input '%s' on %s."),
				*MaOutputName, *ConvertInputName, *Material->GetName());
		}
	}

	EditorOnly->FrontMaterial.Connect(0, ConvertNode);
	UMaterialEditingLibrary::ConnectMaterialProperty(ConvertNode, TEXT(""), MP_FrontMaterial);
}

bool FCrowdLegionExporter::EnsurePluginMasterMaterials(UMaterial*& OutMasterMat, UMaterialFunction*& OutBoneAnimMF)
{
	UMaterialFunction* GetCrowdLegionFrameMF = nullptr;
	if (!EnsureGetCrowdLegionFrame(GetCrowdLegionFrameMF))
	{
		UE_LOG(LogTemp, Error, TEXT("[CrowdLegionExporter] Failed to ensure GetCrowdLegionFrame before MF_CrowdLegionBoneAnimation."));
		return false;
	}

	// 1. 检查或创建 /VgCrowdLegion/Materials/MF_CrowdLegionBoneAnimation
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

	// 2. 检查或强制重建 /VgCrowdLegion/Materials/M_Crowd_Legion
	// 清空表达式后重建：PICD → GetCrowdLegionFrame → MF_CrowdLegionBoneAnimation.Frame
	// 表面属性经 MakeMaterialAttributes 进入 MF，再输出到 Material Attributes。Substrate 开启时经 Convert 接到 Front Material。
	OutMasterMat = Cast<UMaterial>(StaticLoadObject(UMaterial::StaticClass(), nullptr, *(FString(GPluginMasterMatPackage) + TEXT(".M_Crowd_Legion"))));
	bool bCreatedMaster = false;
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
		bCreatedMaster = true;
	}

	OutMasterMat->PreEditChange(nullptr);
	ClearMaterialExpressions(OutMasterMat);

	OutMasterMat->SetMaterialUsage(MATUSAGE_InstancedStaticMeshes);
	OutMasterMat->bUseMaterialAttributes = true;

	UMaterialExpressionTextureSampleParameter2D* BaseColorParam = NewObject<UMaterialExpressionTextureSampleParameter2D>(OutMasterMat);
	BaseColorParam->ParameterName = TEXT("BaseColorTexture");
	BaseColorParam->MaterialExpressionEditorX = -1500;
	BaseColorParam->MaterialExpressionEditorY = -400;
	OutMasterMat->GetExpressionCollection().AddExpression(BaseColorParam);

	UMaterialExpressionScalarParameter* MetallicParam = NewObject<UMaterialExpressionScalarParameter>(OutMasterMat);
	MetallicParam->ParameterName = TEXT("Metallic");
	MetallicParam->DefaultValue = 0.0f;
	MetallicParam->MaterialExpressionEditorX = -1500;
	MetallicParam->MaterialExpressionEditorY = -150;
	OutMasterMat->GetExpressionCollection().AddExpression(MetallicParam);

	UMaterialExpressionScalarParameter* RoughnessParam = NewObject<UMaterialExpressionScalarParameter>(OutMasterMat);
	RoughnessParam->ParameterName = TEXT("Roughness");
	RoughnessParam->DefaultValue = 0.5f;
	RoughnessParam->MaterialExpressionEditorX = -1500;
	RoughnessParam->MaterialExpressionEditorY = 50;
	OutMasterMat->GetExpressionCollection().AddExpression(RoughnessParam);

	UMaterialExpressionMakeMaterialAttributes* MakeMA = NewObject<UMaterialExpressionMakeMaterialAttributes>(OutMasterMat);
	MakeMA->MaterialExpressionEditorX = -1100;
	MakeMA->MaterialExpressionEditorY = -200;
	OutMasterMat->GetExpressionCollection().AddExpression(MakeMA);
	MakeMA->BaseColor.Connect(0, BaseColorParam);
	MakeMA->BaseColor.SetMask(1, 1, 1, 1, 0);
	MakeMA->Metallic.Connect(0, MetallicParam);
	MakeMA->Roughness.Connect(0, RoughnessParam);

	UMaterialExpressionMaterialFunctionCall* FrameCall = AddCrowdLegionFrameCall(OutMasterMat, GetCrowdLegionFrameMF, -1100, 450);
	if (!FrameCall)
	{
		UE_LOG(LogTemp, Error, TEXT("[CrowdLegionExporter] Failed to build GetCrowdLegionFrame network on M_Crowd_Legion."));
		OutMasterMat->PostEditChange();
		return false;
	}

	UMaterialExpressionMaterialFunctionCall* BoneAnimCall = NewObject<UMaterialExpressionMaterialFunctionCall>(OutMasterMat);
	BoneAnimCall->MaterialExpressionEditorX = -500;
	BoneAnimCall->MaterialExpressionEditorY = -100;
	OutMasterMat->GetExpressionCollection().AddExpression(BoneAnimCall);
	BoneAnimCall->SetMaterialFunction(OutBoneAnimMF);
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
			FuncInput.Input.Connect(0, MakeMA);
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
		UE_LOG(LogTemp, Error, TEXT("[CrowdLegionExporter] M_Crowd_Legion failed to connect MF_CrowdLegionBoneAnimation inputs (Attributes=%d Frame=%d)."),
			bConnectedAttributes ? 1 : 0, bConnectedFrame ? 1 : 0);
		OutMasterMat->PostEditChange();
		return false;
	}

	ConnectBoneAnimOutputs(OutMasterMat, BoneAnimCall);

	if (OutMasterMat->MaterialGraph)
	{
		OutMasterMat->MaterialGraph->RebuildGraph();
	}
	UMaterialEditingLibrary::RecompileMaterial(OutMasterMat);
	OutMasterMat->PostEditChange();
	OutMasterMat->MarkPackageDirty();
	if (bCreatedMaster)
	{
		FAssetRegistryModule::AssetCreated(OutMasterMat);
	}
	UE_LOG(LogTemp, Log, TEXT("[CrowdLegionExporter] %s M_Crowd_Legion with GetCrowdLegionFrame at '%s'."),
		bCreatedMaster ? TEXT("Created") : TEXT("Rebuilt"), GPluginMasterMatPackage);

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
	OutStaticMesh->Build();
	OutStaticMesh->PostEditChange();
	OutStaticMesh->MarkPackageDirty();
	FAssetRegistryModule::AssetCreated(OutStaticMesh);
	UE_LOG(LogTemp, Log, TEXT("[CrowdLegionExporter] Exported StaticMesh '%s' with %d material slot(s)."),
		*TargetStaticMeshPath, OutStaticMesh->GetStaticMaterials().Num());
	return OutStaticMesh;
}

UStaticMesh* FCrowdLegionExporter::ExportStaticMeshWithAtlasUVRemap(USkeletalMesh* SkelMesh, const FString& TargetStaticMeshPath)
{
	UE_LOG(LogTemp, Warning, TEXT("[CrowdLegionExporter] ExportStaticMeshWithAtlasUVRemap is deprecated; exporting without atlas UV merge."));
	return ExportStaticMeshFromSkeletalMesh(SkelMesh, TargetStaticMeshPath);
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

UTexture2D* FCrowdLegionExporter::ExtractBaseColorTexture(UMaterialInterface* Material)
{
	if (!Material)
	{
		return nullptr;
	}

	static const FName CandidateNames[] = {
		TEXT("BaseColorTexture"),
		TEXT("BaseColor"),
		TEXT("Diffuse"),
		TEXT("Albedo"),
		TEXT("Color"),
	};

	for (const FName& ParamName : CandidateNames)
	{
		UTexture* Found = nullptr;
		if (Material->GetTextureParameterValue(FMaterialParameterInfo(ParamName), Found) && Found)
		{
			if (UTexture2D* As2D = Cast<UTexture2D>(Found))
			{
				return As2D;
			}
		}
	}
	return nullptr;
}

bool FCrowdLegionExporter::EnsureBoneAnimationParentMaterial(const FString& PackagePath, const FName AssetName, UMaterialFunction* BoneAnimMF, UMaterial*& OutParentMat)
{
	OutParentMat = nullptr;
	if (!BoneAnimMF || PackagePath.IsEmpty() || AssetName.IsNone())
	{
		return false;
	}

	UMaterialFunction* GetCrowdLegionFrameMF = nullptr;
	if (!EnsureGetCrowdLegionFrame(GetCrowdLegionFrameMF) || !GetCrowdLegionFrameMF)
	{
		UE_LOG(LogTemp, Error, TEXT("[CrowdLegionExporter] Failed to ensure GetCrowdLegionFrame for '%s'."), *AssetName.ToString());
		return false;
	}

	const FString ObjectPath = PackagePath + TEXT(".") + AssetName.ToString();
	OutParentMat = Cast<UMaterial>(StaticLoadObject(UMaterial::StaticClass(), nullptr, *ObjectPath));

	bool bCreated = false;
	if (!OutParentMat)
	{
		UPackage* MatPackage = CreatePackage(*PackagePath);
		if (!MatPackage)
		{
			return false;
		}
		MatPackage->FullyLoad();
		OutParentMat = NewObject<UMaterial>(MatPackage, AssetName, RF_Public | RF_Standalone);
		if (!OutParentMat)
		{
			return false;
		}
		bCreated = true;
	}

	OutParentMat->PreEditChange(nullptr);
	ClearMaterialExpressions(OutParentMat);
	OutParentMat->SetMaterialUsage(MATUSAGE_InstancedStaticMeshes);
	OutParentMat->bUseMaterialAttributes = true;

	UMaterialExpressionTextureSampleParameter2D* BaseColorParam = NewObject<UMaterialExpressionTextureSampleParameter2D>(OutParentMat);
	BaseColorParam->ParameterName = TEXT("BaseColorTexture");
	BaseColorParam->MaterialExpressionEditorX = -1200;
	BaseColorParam->MaterialExpressionEditorY = 0;
	OutParentMat->GetExpressionCollection().AddExpression(BaseColorParam);

	UMaterialExpressionMakeMaterialAttributes* MakeMA = NewObject<UMaterialExpressionMakeMaterialAttributes>(OutParentMat);
	MakeMA->MaterialExpressionEditorX = -900;
	MakeMA->MaterialExpressionEditorY = 0;
	OutParentMat->GetExpressionCollection().AddExpression(MakeMA);
	MakeMA->BaseColor.Connect(0, BaseColorParam);
	MakeMA->BaseColor.SetMask(1, 1, 1, 1, 0);

	UMaterialExpressionMaterialFunctionCall* FrameCall = AddCrowdLegionFrameCall(OutParentMat, GetCrowdLegionFrameMF, -900, 400);
	if (!FrameCall)
	{
		OutParentMat->PostEditChange();
		return false;
	}

	UMaterialExpressionMaterialFunctionCall* BoneAnimCall = NewObject<UMaterialExpressionMaterialFunctionCall>(OutParentMat);
	BoneAnimCall->MaterialExpressionEditorX = -500;
	BoneAnimCall->MaterialExpressionEditorY = 0;
	OutParentMat->GetExpressionCollection().AddExpression(BoneAnimCall);
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
			FuncInput.Input.Connect(0, MakeMA);
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
		UE_LOG(LogTemp, Error, TEXT("[CrowdLegionExporter] BoneAnimation parent '%s' failed MF inputs (Attributes=%d Frame=%d)."),
			*AssetName.ToString(), bConnectedAttributes ? 1 : 0, bConnectedFrame ? 1 : 0);
		OutParentMat->PostEditChange();
		return false;
	}

	ConnectBoneAnimOutputs(OutParentMat, BoneAnimCall);
	if (OutParentMat->MaterialGraph)
	{
		OutParentMat->MaterialGraph->RebuildGraph();
	}
	UMaterialEditingLibrary::RecompileMaterial(OutParentMat);
	OutParentMat->PostEditChange();
	OutParentMat->MarkPackageDirty();
	if (bCreated)
	{
		FAssetRegistryModule::AssetCreated(OutParentMat);
	}
	UE_LOG(LogTemp, Log, TEXT("[CrowdLegionExporter] %s BoneAnimation parent '%s'."),
		bCreated ? TEXT("Created") : TEXT("Rebuilt"), *PackagePath);
	return true;
}

bool FCrowdLegionExporter::ApplyBoneAnimationMaterialsToStaticMesh(UAnimToTextureDataAsset* DataAsset, UStaticMesh* BakedMesh, UMaterial* ParentMat)
{
	if (!DataAsset || !BakedMesh || !ParentMat)
	{
		return false;
	}

	const FString BaseDir = FPackageName::GetLongPackagePath(DataAsset->GetOutermost()->GetName());
	const FString BoneAnimDir = BaseDir / TEXT("Materials") / TEXT("BoneAnimation");
	const FString MeshName = BakedMesh->GetName();
	const TArray<FStaticMaterial>& Slots = BakedMesh->GetStaticMaterials();

	// Same source material asset => one shared BoneAnimation MI for all slots that use it.
	TMap<FSoftObjectPath, UMaterialInstanceConstant*> SourceMatToMIC;

	for (int32 SlotIndex = 0; SlotIndex < Slots.Num(); ++SlotIndex)
	{
		UMaterialInterface* SourceMat = Slots[SlotIndex].MaterialInterface;
		const FSoftObjectPath SourceKey = SourceMat ? FSoftObjectPath(SourceMat) : FSoftObjectPath();

		if (UMaterialInstanceConstant** ExistingMIC = SourceMatToMIC.Find(SourceKey))
		{
			BakedMesh->SetMaterial(SlotIndex, *ExistingMIC);
			UE_LOG(LogTemp, Log, TEXT("[CrowdLegionExporter] Slot %d reuses MI '%s' (same source material)."),
				SlotIndex, *(*ExistingMIC)->GetName());
			continue;
		}

		UTexture2D* BaseColorTex = ExtractBaseColorTexture(SourceMat);
		const FString SourceMatName = SourceMat ? SourceMat->GetName() : TEXT("None");
		FString MiName = FString::Printf(TEXT("MI_%s_BoneAnimation_%s"), *MeshName, *SourceMatName);
		FString MiPackagePath = BoneAnimDir / MiName;
		FString MiObjectPath = MiPackagePath + TEXT(".") + MiName;

		for (const TPair<FSoftObjectPath, UMaterialInstanceConstant*>& Pair : SourceMatToMIC)
		{
			if (Pair.Value && Pair.Key != SourceKey && Pair.Value->GetPathName() == MiObjectPath)
			{
				MiName = FString::Printf(TEXT("MI_%s_BoneAnimation_%s_%d"), *MeshName, *SourceMatName, SlotIndex);
				MiPackagePath = BoneAnimDir / MiName;
				MiObjectPath = MiPackagePath + TEXT(".") + MiName;
				break;
			}
		}

		UMaterialInstanceConstant* MIC = Cast<UMaterialInstanceConstant>(StaticLoadObject(UMaterialInstanceConstant::StaticClass(), nullptr, *MiObjectPath));
		const bool bCreated = (MIC == nullptr);
		if (!MIC)
		{
			UPackage* MatPackage = CreatePackage(*MiPackagePath);
			if (!MatPackage)
			{
				return false;
			}
			MatPackage->FullyLoad();
			MIC = NewObject<UMaterialInstanceConstant>(MatPackage, FName(*MiName), RF_Public | RF_Standalone);
			if (!MIC)
			{
				return false;
			}
		}

		MIC->SetParentEditorOnly(ParentMat);
		if (BaseColorTex)
		{
			MIC->SetTextureParameterValueEditorOnly(FMaterialParameterInfo(TEXT("BaseColorTexture")), BaseColorTex);
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("[CrowdLegionExporter] Slot %d ('%s') has no BaseColor texture; MI created without override."),
				SlotIndex, *SourceMatName);
		}

		UAnimToTextureBPLibrary::UpdateMaterialInstanceFromDataAsset(DataAsset, MIC, EMaterialParameterAssociation::GlobalParameter);
		SyncMaterialInstanceRanges(DataAsset, MIC);
		UMaterialEditingLibrary::UpdateMaterialInstance(MIC);
		MIC->PostEditChange();
		MIC->MarkPackageDirty();
		if (bCreated)
		{
			FAssetRegistryModule::AssetCreated(MIC);
		}

		SourceMatToMIC.Add(SourceKey, MIC);
		BakedMesh->SetMaterial(SlotIndex, MIC);
		UE_LOG(LogTemp, Log, TEXT("[CrowdLegionExporter] %s slot %d MI '%s' for source '%s'."),
			bCreated ? TEXT("Created") : TEXT("Updated"), SlotIndex, *MiName, *SourceMatName);
	}

	BakedMesh->PostEditChange();
	BakedMesh->MarkPackageDirty();
	return Slots.Num() > 0;
}

bool FCrowdLegionExporter::BakeA2TDataAsset(UAnimToTextureDataAsset* DataAsset, UStaticMesh* BakedMesh)
{
	if (!DataAsset || !BakedMesh)
	{
		return false;
	}

	UMaterial* UnusedCrowdMaster = nullptr;
	UMaterialFunction* BoneAnimMF = nullptr;
	if (!EnsurePluginMasterMaterials(UnusedCrowdMaster, BoneAnimMF) || !BoneAnimMF)
	{
		return false;
	}

	const FString BaseDir = FPackageName::GetLongPackagePath(DataAsset->GetOutermost()->GetName());
	const FString MeshName = DataAsset->GetSkeletalMesh() ? DataAsset->GetSkeletalMesh()->GetName() : BakedMesh->GetName();
	const FString ParentAssetName = FString::Printf(TEXT("M_%s_BoneAnimation"), *MeshName);
	const FString ParentPackagePath = BaseDir / TEXT("Materials") / TEXT("BoneAnimation") / ParentAssetName;

	UMaterial* ParentMat = nullptr;
	if (!EnsureBoneAnimationParentMaterial(ParentPackagePath, FName(*ParentAssetName), BoneAnimMF, ParentMat) || !ParentMat)
	{
		UE_LOG(LogTemp, Error, TEXT("[CrowdLegionExporter] Failed to ensure BoneAnimation parent for bake."));
		return false;
	}

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

	const bool bBakeSuccess = UAnimToTextureBPLibrary::AnimationToTexture(DataAsset);
	if (!bBakeSuccess)
	{
		return false;
	}

	if (!ApplyBoneAnimationMaterialsToStaticMesh(DataAsset, BakedMesh, ParentMat))
	{
		UE_LOG(LogTemp, Error, TEXT("[CrowdLegionExporter] Failed to apply per-slot BoneAnimation materials."));
		return false;
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

bool FCrowdLegionExporter::EnsureArcherBoneAnimationParentMaterial(UMaterial*& OutParentMat, UMaterialFunction* BoneAnimMF)
{
	return EnsureBoneAnimationParentMaterial(
		FString(GArcherBoneAnimParentPkg),
		FName(TEXT("M_Archer_BoneAnimation")),
		BoneAnimMF,
		OutParentMat);
}

bool FCrowdLegionExporter::EnsureArcherBoneAnimationMaterialInstances(UMaterial* ParentMat)
{
	if (!ParentMat)
	{
		return false;
	}

	struct FArcherMiDesc
	{
		const TCHAR* AssetName;
		const TCHAR* TexturePath;
	};

	const FArcherMiDesc Descs[] = {
		{ TEXT("MI_Archer_BoneAnimation_Color"), GTexSoldierColor },
		{ TEXT("MI_Archer_BoneAnimation_Color_2"), GTexSoldierColor2 },
		{ TEXT("MI_Archer_BoneAnimation_Color_4"), GTexSoldierColor4 },
	};

	for (const FArcherMiDesc& Desc : Descs)
	{
		const FString PackagePath = FString(GArcherBoneAnimDir) / Desc.AssetName;
		const FString ObjectPath = PackagePath + TEXT(".") + Desc.AssetName;

		UTexture2D* BaseColorTex = Cast<UTexture2D>(StaticLoadObject(UTexture2D::StaticClass(), nullptr, Desc.TexturePath));
		if (!BaseColorTex)
		{
			UE_LOG(LogTemp, Error, TEXT("[CrowdLegionExporter] Missing BaseColor texture: %s"), Desc.TexturePath);
			return false;
		}

		UMaterialInstanceConstant* MIC = Cast<UMaterialInstanceConstant>(StaticLoadObject(UMaterialInstanceConstant::StaticClass(), nullptr, *ObjectPath));
		const bool bCreated = (MIC == nullptr);
		if (!MIC)
		{
			UPackage* MatPackage = CreatePackage(*PackagePath);
			if (!MatPackage)
			{
				return false;
			}
			MatPackage->FullyLoad();
			MIC = NewObject<UMaterialInstanceConstant>(MatPackage, FName(Desc.AssetName), RF_Public | RF_Standalone);
			if (!MIC)
			{
				return false;
			}
		}

		MIC->SetParentEditorOnly(ParentMat);
		MIC->SetTextureParameterValueEditorOnly(FMaterialParameterInfo(TEXT("BaseColorTexture")), BaseColorTex);
		UMaterialEditingLibrary::UpdateMaterialInstance(MIC);
		MIC->PostEditChange();
		MIC->MarkPackageDirty();
		if (bCreated)
		{
			FAssetRegistryModule::AssetCreated(MIC);
		}
		UE_LOG(LogTemp, Log, TEXT("[CrowdLegionExporter] %s Archer MI '%s' with texture '%s'."),
			bCreated ? TEXT("Created") : TEXT("Updated"), Desc.AssetName, *BaseColorTex->GetName());
	}

	return true;
}

void FCrowdLegionExporter::ExecuteGenerateArcherBoneAnimationMaterials()
{
	auto Notify = [](const FString& Message, bool bError)
	{
		if (bError)
		{
			UE_LOG(LogTemp, Error, TEXT("[CrowdLegionExporter] %s"), *Message);
		}
		else
		{
			UE_LOG(LogTemp, Log, TEXT("[CrowdLegionExporter] %s"), *Message);
		}
		if (!FApp::IsUnattended())
		{
			FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(Message));
		}
	};

	UMaterial* UnusedCrowdMaster = nullptr;
	UMaterialFunction* BoneMF = nullptr;
	if (!EnsurePluginMasterMaterials(UnusedCrowdMaster, BoneMF) || !BoneMF)
	{
		Notify(TEXT("无法准备 MF_CrowdLegionBoneAnimation。请确认 AnimToTexture 插件已启用。"), true);
		return;
	}

	// Persist plugin masters. Archer-only save below does not write these packages,
	// and -unattended -Quit discards dirty packages that were never saved.
	{
		TArray<UPackage*> PluginPackages;
		auto AddPkg = [&PluginPackages](UObject* Obj)
		{
			if (Obj)
			{
				if (UPackage* Pkg = Obj->GetOutermost())
				{
					PluginPackages.AddUnique(Pkg);
				}
			}
		};
		AddPkg(UnusedCrowdMaster);
		AddPkg(BoneMF);
		if (UMaterialFunction* FrameFn = Cast<UMaterialFunction>(StaticLoadObject(
			UMaterialFunction::StaticClass(), nullptr,
			TEXT("/VgCrowdLegion/Materials/GetCrowdLegionFrame.GetCrowdLegionFrame"))))
		{
			AddPkg(FrameFn);
		}
		if (PluginPackages.Num() > 0)
		{
			UEditorLoadingAndSavingUtils::SavePackages(PluginPackages, true);
		}
	}

	UMaterial* Parent = nullptr;
	if (!EnsureArcherBoneAnimationParentMaterial(Parent, BoneMF) || !Parent)
	{
		Notify(TEXT("创建 M_Archer_BoneAnimation 失败。"), true);
		return;
	}

	if (!EnsureArcherBoneAnimationMaterialInstances(Parent))
	{
		Notify(TEXT("创建 Archer BoneAnimation MI 失败（请检查 T_Soldier_C_Color / _2 / _4 是否存在）。"), true);
		return;
	}

	TArray<UPackage*> PackagesToSave;
	if (UPackage* ParentPkg = Parent->GetOutermost())
	{
		PackagesToSave.AddUnique(ParentPkg);
	}
	const TCHAR* MiNames[] = {
		TEXT("MI_Archer_BoneAnimation_Color"),
		TEXT("MI_Archer_BoneAnimation_Color_2"),
		TEXT("MI_Archer_BoneAnimation_Color_4"),
	};
	for (const TCHAR* MiName : MiNames)
	{
		const FString ObjectPath = FString(GArcherBoneAnimDir) / MiName + TEXT(".") + MiName;
		if (UObject* MiObj = StaticLoadObject(UObject::StaticClass(), nullptr, *ObjectPath))
		{
			if (UPackage* Pkg = MiObj->GetOutermost())
			{
				PackagesToSave.AddUnique(Pkg);
			}
		}
	}
	if (PackagesToSave.Num() > 0)
	{
		UEditorLoadingAndSavingUtils::SavePackages(PackagesToSave, true);
	}

	Notify(TEXT("已生成/更新:\n/Game/CrowdLegion/Characters/Archer/Materials/BoneAnimation/\n- M_Archer_BoneAnimation\n- MI_Archer_BoneAnimation_Color\n- MI_Archer_BoneAnimation_Color_2\n- MI_Archer_BoneAnimation_Color_4"), false);
}
