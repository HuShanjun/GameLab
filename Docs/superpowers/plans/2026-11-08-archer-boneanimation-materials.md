# Archer BoneAnimation Materials Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add an Editor menu action that creates `/Game/CrowdLegion/Characters/Archer/Materials/BoneAnimation/` parent material `M_Archer_BoneAnimation` (TextureSample → `MF_BoneAnimation` → output) plus three MIs with distinct BaseColor textures.

**Architecture:** Extend `FCrowdLegionExporter` with a dedicated generator that reuses `EnsurePluginMasterMaterials` only for ensuring `MF_BoneAnimation` exists, then builds a **simple** parent material (scalar `Frame`, no PerInstanceCustomData). Register a Tools → 海量军团 menu entry. Idempotent: load-or-create packages under fixed paths.

**Tech Stack:** UE 5.8, `VgCrowdLegionEditor`, MaterialEditingLibrary / MaterialExpression APIs, UMaterialInstanceConstant.

## Global Constraints

- Frame driving = **simple scalar `Frame`** only (no PackedRate / CustomData network)
- Output under `/Game/CrowdLegion/Characters/Archer/Materials/BoneAnimation/`
- MI textures: `T_Soldier_C_Color`, `T_Soldier_C_Color_2`, `T_Soldier_C_Color_4`
- Do not modify original `M_Soldier_C_Color*` materials
- Spec: `Docs/superpowers/specs/2026-11-08-archer-boneanimation-materials-design.md`

## File Map

| Path | Responsibility |
|------|----------------|
| `.../CrowdLegionExporter.h` | Declare `ExecuteGenerateArcherBoneAnimationMaterials` + helpers |
| `.../CrowdLegionExporter.cpp` | Implement parent + MI generation |
| `.../VgCrowdLegionEditor.cpp` | Menu entry wiring |
| Content (runtime-generated) | `M_Archer_BoneAnimation` + 3 MIs |

---

### Task 1: Declare exporter API

**Files:**
- Modify: `Client/Plugins/Development/VgCrowdLegion/Source/VgCrowdLegionEditor/Public/CrowdLegionExporter.h`

**Interfaces:**
- Consumes: existing `EnsurePluginMasterMaterials`
- Produces: `ExecuteGenerateArcherBoneAnimationMaterials()` public static entry

- [ ] **Step 1: Add declarations to `FCrowdLegionExporter`**

In the `public:` section after `ExecuteExportFromSelectedAsset()`, add:

```cpp
/** Tools 菜单：生成 Archer 简单版 BoneAnimation 父材质 + 三个 BaseColor MI */
static void ExecuteGenerateArcherBoneAnimationMaterials();

/** 确保 / 创建 M_Archer_BoneAnimation（TextureSample + MF_BoneAnimation + Frame 标量） */
static bool EnsureArcherBoneAnimationParentMaterial(UMaterial*& OutParentMat, UMaterialFunction* BoneAnimMF);

/** 创建或更新三个 Archer BoneAnimation 材质实例并绑定贴图 */
static bool EnsureArcherBoneAnimationMaterialInstances(UMaterial* ParentMat);
```

- [ ] **Step 2: Commit**

```powershell
git add Client/Plugins/Development/VgCrowdLegion/Source/VgCrowdLegionEditor/Public/CrowdLegionExporter.h
git commit -m "Declare Archer BoneAnimation material generator API."
```

---

### Task 2: Implement parent material + MI generation

**Files:**
- Modify: `Client/Plugins/Development/VgCrowdLegion/Source/VgCrowdLegionEditor/Private/CrowdLegionExporter.cpp`

**Interfaces:**
- Consumes: `EnsurePluginMasterMaterials` (for `OutBoneAnimMF` only; ignore Crowd Legion master if already present)
- Produces: content assets under fixed package paths

**Constants to add near existing path constants:**

```cpp
static const TCHAR* GArcherBoneAnimParentPkg = TEXT("/Game/CrowdLegion/Characters/Archer/Materials/BoneAnimation/M_Archer_BoneAnimation");
static const TCHAR* GArcherBoneAnimDir = TEXT("/Game/CrowdLegion/Characters/Archer/Materials/BoneAnimation");
static const TCHAR* GTexColor = TEXT("/Game/CrowdLegion/Characters/Archer/Textures/T_Soldier_C_Color.T_Soldier_C_Color");
static const TCHAR* GTexColor2 = TEXT("/Game/CrowdLegion/Characters/Archer/Textures/T_Soldier_C_Color_2.T_Soldier_C_Color_2");
static const TCHAR* GTexColor4 = TEXT("/Game/CrowdLegion/Characters/Archer/Textures/T_Soldier_C_Color_4.T_Soldier_C_Color_4");
```

- [ ] **Step 1: Implement `EnsureArcherBoneAnimationParentMaterial`**

Behavior:

1. `StaticLoadObject` parent at `GArcherBoneAnimParentPkg`. If exists, set `OutParentMat` and return true (idempotent; optional rebuild only if you detect missing expressions — prefer recreate-by-delete only when broken).
2. Else `CreatePackage` for that path, `NewObject<UMaterial>(Package, TEXT("M_Archer_BoneAnimation"), RF_Public|RF_Standalone)`.
3. Set `bUsedWithInstancedStaticMeshes = true`.
4. Create expressions:
   - `UMaterialExpressionTextureSampleParameter2D` named `BaseColorTexture`
   - `UMaterialExpressionScalarParameter` named `Frame` (default `0`)
   - `UMaterialExpressionMaterialFunctionCall` with `SetMaterialFunction(BoneAnimMF)` then `UpdateFromFunctionResource()`
5. Connect `Frame` scalar into MF call input named `Frame`.
6. Connect TextureSample RGB → BaseColor (`UMaterialEditingLibrary::ConnectMaterialProperty` or EditorOnly property connects). If Substrate project: follow the same Substrate branch pattern already in `EnsurePluginMasterMaterials` for BaseColor only (no Metallic/Roughness required unless needed for compile).
7. Connect MF outputs:
   - Prefer pins commonly named / used for WPO and Normal from official MF (inspect `FunctionOutputs`; connect WorldPositionOffset / Normal if present). If only one float3 displacement-style output exists, connect to World Position Offset.
8. `UMaterialEditingLibrary::RecompileMaterial`, `PostEditChange`, `MarkPackageDirty`, `FAssetRegistryModule::AssetCreated`.

Minimal connection sketch (non-Substrate):

```cpp
UMaterialEditingLibrary::ConnectMaterialProperty(BaseColorTex, TEXT("RGB"), MP_BaseColor);
// After locating MF WorldPositionOffset-compatible output expression/output index:
UMaterialEditingLibrary::ConnectMaterialProperty(BoneAnimCall, TEXT("..."), MP_WorldPositionOffset);
```

If output pin names differ, iterate `BoneAnimCall->FunctionOutputs` and match names containing `Position` / `Offset` / `Normal`.

- [ ] **Step 2: Implement `EnsureArcherBoneAnimationMaterialInstances`**

For each of three entries `{ "MI_Archer_BoneAnimation_Color", GTexColor }`, `{ "..._Color_2", GTexColor2 }`, `{ "..._Color_4", GTexColor4 }`:

1. Package path = `GArcherBoneAnimDir / AssetName`
2. Load or `NewObject<UMaterialInstanceConstant>`
3. `SetParentEditorOnly(ParentMat)`
4. Load `UTexture2D*` from path; if null, log error and return false
5. `SetTextureParameterValueEditorOnly(FMaterialParameterInfo(TEXT("BaseColorTexture")), Tex)`
6. `UMaterialEditingLibrary::UpdateMaterialInstance(MIC)`, dirty, `AssetCreated` if new

- [ ] **Step 3: Implement `ExecuteGenerateArcherBoneAnimationMaterials`**

```cpp
void FCrowdLegionExporter::ExecuteGenerateArcherBoneAnimationMaterials()
{
	UMaterial* UnusedCrowdMaster = nullptr;
	UMaterialFunction* BoneMF = nullptr;
	if (!EnsurePluginMasterMaterials(UnusedCrowdMaster, BoneMF) || !BoneMF)
	{
		FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(TEXT("无法准备 MF_BoneAnimation。")));
		return;
	}

	UMaterial* Parent = nullptr;
	if (!EnsureArcherBoneAnimationParentMaterial(Parent, BoneMF) || !Parent)
	{
		FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(TEXT("创建 M_Archer_BoneAnimation 失败。")));
		return;
	}

	if (!EnsureArcherBoneAnimationMaterialInstances(Parent))
	{
		FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(TEXT("创建 Archer BoneAnimation MI 失败（检查贴图是否存在）。")));
		return;
	}

	FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(
		TEXT("已生成/更新:\n/Game/CrowdLegion/Characters/Archer/Materials/BoneAnimation/\n- M_Archer_BoneAnimation\n- MI_Archer_BoneAnimation_Color(_2/_4)")));
}
```

Note: `EnsurePluginMasterMaterials` currently creates `M_Crowd_Legion` if missing — acceptable side effect; do not delete that behavior.

- [ ] **Step 4: Add required includes if missing**

Ensure cpp has: `Materials/MaterialInstanceConstant.h`, `MaterialEditingLibrary.h`, `Factories/MaterialFactoryNew.h` only if used, `AssetRegistry/AssetRegistryModule.h`, `Misc/MessageDialog.h`, texture headers.

- [ ] **Step 5: Commit**

```powershell
git add Client/Plugins/Development/VgCrowdLegion/Source/VgCrowdLegionEditor/Private/CrowdLegionExporter.cpp
git commit -m "Implement Archer BoneAnimation parent material and MI generator."
```

---

### Task 3: Register Tools menu entry

**Files:**
- Modify: `Client/Plugins/Development/VgCrowdLegion/Source/VgCrowdLegionEditor/Private/VgCrowdLegionEditor.cpp`

**Interfaces:**
- Consumes: `FCrowdLegionExporter::ExecuteGenerateArcherBoneAnimationMaterials`
- Produces: menu item under 海量军团 section

- [ ] **Step 1: Add menu entry after existing Export entry**

```cpp
Section.AddMenuEntry(
	"GenerateArcherBoneAnimationMaterials",
	LOCTEXT("GenerateArcherBoneAnim_Label", "生成 Archer BoneAnimation 材质"),
	LOCTEXT("GenerateArcherBoneAnim_Tooltip", "创建 M_Archer_BoneAnimation 父材质（TextureSample+MF_BoneAnimation）及三个 BaseColor 示例材质实例"),
	FSlateIcon(),
	FUIAction(FExecuteAction::CreateStatic(&FCrowdLegionExporter::ExecuteGenerateArcherBoneAnimationMaterials))
);
```

- [ ] **Step 2: Commit**

```powershell
git add Client/Plugins/Development/VgCrowdLegion/Source/VgCrowdLegionEditor/Private/VgCrowdLegionEditor.cpp
git commit -m "Add Tools menu entry for Archer BoneAnimation materials."
```

---

### Task 4: Build Editor target and verify in Editor

**Files:**
- Verify generated Content assets (created at runtime in Editor)

- [ ] **Step 1: Build**

```powershell
cmd /c "E:\UnrealRepo\GameLab\Client\ubt_build.bat ClientEditor Win64 Development"
```

Expected: exit code `0`

- [ ] **Step 2: Run generator in Editor**

1. Open `Client.uproject`
2. `Tools → 海量军团 (VgCrowdLegion) → 生成 Archer BoneAnimation 材质`
3. Confirm success dialog

- [ ] **Step 3: Acceptance checks**

```powershell
$root = "E:\UnrealRepo\GameLab\Client\Content\CrowdLegion\Characters\Archer\Materials\BoneAnimation"
@(
  (Test-Path "$root\M_Archer_BoneAnimation.uasset"),
  (Test-Path "$root\MI_Archer_BoneAnimation_Color.uasset"),
  (Test-Path "$root\MI_Archer_BoneAnimation_Color_2.uasset"),
  (Test-Path "$root\MI_Archer_BoneAnimation_Color_4.uasset")
) | ForEach-Object { $_ }
```

Expected: four `True`

In Editor, open parent material: confirm `BaseColorTexture`, `Frame`, `MF_BoneAnimation` call; open each MI: BaseColor texture matches Color / Color_2 / Color_4.

- [ ] **Step 4: Commit generated assets (project Content)**

```powershell
git add Client/Content/CrowdLegion/Characters/Archer/Materials/BoneAnimation
git status --short Client/Content/CrowdLegion/Characters/Archer/Materials/BoneAnimation
git commit -m "Add generated Archer BoneAnimation materials."
```

If LFS required for `.uasset`, ensure they are tracked via existing `.gitattributes`.

---

## Spec Coverage Self-Review

| Spec requirement | Task |
|------------------|------|
| Parent: TextureSample + MF_BoneAnimation + Frame scalar | Task 2 |
| Three MIs with distinct textures | Task 2 |
| Paths under Archer/.../BoneAnimation | Task 2 |
| Menu entry | Task 3 |
| Idempotent generate | Task 2 |
| No original Soldier mat edits | All tasks |
| Build + verify | Task 4 |
