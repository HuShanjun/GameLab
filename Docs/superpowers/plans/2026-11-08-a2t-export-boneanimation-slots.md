# A2T Multi-Slot BoneAnimation Export Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Refactor CrowdLegion one-click A2T export to keep multi-material slots, assign per-slot BoneAnimation MIs (Archer-style), and store absolute `EndFrame` in anim meta while preserving existing `bDefaultLoop`.

**Architecture:** Change the export orchestrator and SM conversion to skip Atlas/UV merge; add per-slot MI generation reusing Archer parent graph helpers; rename `FUnitAnimationTableRow::NumFrames` → `EndFrame` and update runtime consumers; preserve loop flags when regenerating tables by AnimName.

**Tech Stack:** UE 5.8, VgCrowdLegion / VgCrowdLegionEditor, AnimToTexture BPLibrary

## Global Constraints

- Spec: `Docs/superpowers/specs/2026-11-08-a2t-export-boneanimation-slots-design.md`
- No Atlas generation or UV quadrant remap in the one-click pipeline
- Keep all SM material slots; each slot gets BoneAnimation MI; `SetMaterial(i, MI_i)`
- `NumFrames` → `EndFrame` (absolute inclusive end); length = `EndFrame - StartFrame + 1`
- Existing DataTable rows matched by `AnimName`: do not overwrite `bDefaultLoop`
- No force-push / unrelated refactors

## File Map

| Path | Change |
|------|--------|
| `CrowdUnitComponent.h/.cpp` | Row field rename; PushCell uses `EndFrame` |
| `CrowdStatsComponent.cpp` | Use `EndFrame` for length / estimates |
| `CrowdLegionExporter.h/.cpp` | Export SM without merge; bake without Atlas; per-slot MIs; anim table |
| `CrowdLegionActor.cpp` | Call new pipeline (no Atlas) |
| `VgCrowdLegionEditor.cpp` | Success dialog copy if it mentions Atlas / 1 Draw Call |

---

### Task 1: Rename `NumFrames` → `EndFrame` + runtime

**Files:**
- Modify: `Client/Plugins/Development/VgCrowdLegion/Source/VgCrowdLegion/Public/CrowdUnitComponent.h`
- Modify: `Client/Plugins/Development/VgCrowdLegion/Source/VgCrowdLegion/Private/CrowdUnitComponent.cpp`
- Modify: `Client/Plugins/Development/VgCrowdLegion/Source/VgCrowdLegion/Private/CrowdStatsComponent.cpp`

- [ ] **Step 1: Update row struct**

In `FUnitAnimationTableRow`, replace:

```cpp
/** 动画总帧长 */
UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation")
int32 NumFrames = 1;
```

with:

```cpp
/** 在全局 VAT 贴图上的结束帧（含） */
UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation")
int32 EndFrame = 0;
```

- [ ] **Step 2: `PushCellCustomData`**

Use row end directly:

```cpp
StartFrame = float(Animations[D.AnimIndex].StartFrame);
EndFrame = float(Animations[D.AnimIndex].EndFrame);
if (EndFrame < StartFrame)
{
	EndFrame = StartFrame;
}
```

- [ ] **Step 3: Stats**

Replace `NumFrames` usages with `EndFrame - StartFrame + 1` (or store/display end as appropriate). Frame estimate already has EndFrame local — feed from row `.EndFrame`.

- [ ] **Step 4: Build runtime module**

```powershell
cmd /c "E:\UnrealRepo\GameLab\Client\ubt_build.bat ClientEditor Win64 Development"
```

Expect compile errors in Exporter until Task 3 — either build only after Task 3, or temporarily fix Exporter `Row.NumFrames` in same commit if building now.

**Prefer:** commit runtime rename together with Exporter table writer in Task 3 if single compile gate is easier; otherwise stub Exporter in this commit.

- [ ] **Step 5: Commit** (if compiling clean)

```powershell
git add Client/Plugins/Development/VgCrowdLegion/Source/VgCrowdLegion
git commit -m "Rename anim meta NumFrames to absolute EndFrame in runtime."
```

---

### Task 2: Export StaticMesh without Atlas / slot merge

**Files:**
- Modify: `CrowdLegionExporter.h`
- Modify: `CrowdLegionExporter.cpp`
- Modify: `CrowdLegionActor.cpp`

- [ ] **Step 1: Add `ExportStaticMeshFromSkeletalMesh`**

```cpp
static UStaticMesh* ExportStaticMeshFromSkeletalMesh(USkeletalMesh* SkelMesh, const FString& TargetStaticMeshPath);
```

Implementation: call `UAnimToTextureBPLibrary::ConvertSkeletalMeshToStaticMesh` (same as today), save package, **do not** remap UVs, **do not** force `MaterialIndex = 0` / `StaticMaterials.SetNum(1)`.

Keep `ExportStaticMeshWithAtlasUVRemap` body unused by pipeline or make it call the new function + log deprecation.

- [ ] **Step 2: Wire orchestrators**

In `ExecuteExportFromSelectedAsset` and `ACrowdLegionActor::ExportAndBakeA2TLegion`:

- Remove `GenerateAtlasTextureFromSkelMesh` call
- Use `ExportStaticMeshFromSkeletalMesh`
- Update success dialog strings (no Atlas / 1 Draw Call)

- [ ] **Step 3: Commit**

```powershell
git commit -m "Export A2T static mesh without atlas UV merge or slot collapse."
```

---

### Task 3: Per-slot BoneAnimation MIs + bake without Atlas

**Files:**
- Modify: `CrowdLegionExporter.h/.cpp`

- [ ] **Step 1: Helper to extract BaseColor texture from `UMaterialInterface*`**

Walk material instance/parent texture parameters named `BaseColor`, `BaseColorTexture`, or first used diffuse — match patterns already used in Atlas generator / Archer MI setup.

- [ ] **Step 2: `EnsureCharacterBoneAnimationParentMaterial`**

Reuse Archer parent builder logic (or call `EnsureArcherBoneAnimationParentMaterial` when path is Archer; otherwise create `M_<Mesh>_BoneAnimation` under the DataAsset’s Materials/BoneAnimation folder with same graph as Archer).

- [ ] **Step 3: `ApplyBoneAnimationMaterialsToStaticMesh`**

For each slot `i` on `BakedMesh`:

1. Source mat from mesh slot (or original Skel slot `i`)
2. Create/update MI under `{BaseDir}/Materials/BoneAnimation/MI_{Mesh}_BoneAnimation_{i}`
3. Parent = BoneAnimation parent; set `BaseColorTexture`
4. `UAnimToTextureBPLibrary::UpdateMaterialInstanceFromDataAsset(DataAsset, MI, ...)` after bake (or once parent baked)
5. `BakedMesh->SetMaterial(i, MI)`

- [ ] **Step 4: Change `BakeA2TDataAsset` signature**

```cpp
static bool BakeA2TDataAsset(UAnimToTextureDataAsset* DataAsset, UStaticMesh* BakedMesh);
```

- Run `AnimationToTexture`
- Ensure parent + per-slot MIs
- Update each MI from DataAsset
- Assign all slots
- Drop `AtlasTexture` / single `M_Crowd_Legion` slot-0-only bind from this path (plugin master ensure may remain for other tools)

- [ ] **Step 5: Commit**

```powershell
git commit -m "Assign per-slot BoneAnimation materials during A2T bake."
```

---

### Task 4: Anim meta table EndFrame + preserve Default Loop

**Files:**
- Modify: `CrowdLegionExporter.cpp` `GenerateAnimMetaDataTable`

- [ ] **Step 1: Preserve loops**

Before `EmptyTable()`, if table already had rows, snapshot `TMap<FName, bool>` AnimName → `bDefaultLoop`.

- [ ] **Step 2: Write EndFrame**

```cpp
Row.StartFrame = AccumulatedFrames;
Row.EndFrame = AccumulatedFrames + FMath::Max(1, FrameCount) - 1;
if (const bool* ExistingLoop = OldLoops.Find(SeqName))
{
	Row.bDefaultLoop = *ExistingLoop;
}
else
{
	Row.bDefaultLoop = bLoopHeuristic;
}
```

- [ ] **Step 3: Build + manual check**

```powershell
cmd /c "E:\UnrealRepo\GameLab\Client\ubt_build.bat ClientEditor Win64 Development"
```

In Editor: run Tools → A2T 一键导出；confirm SM slot count > 1 when source has multiple materials; each slot BoneAnimation MI; re-run export and confirm edited `bDefaultLoop` sticks; `EndFrame` column present.

- [ ] **Step 4: Commit**

```powershell
git commit -m "Write anim meta EndFrame and preserve existing Default Loop."
```

---

## Spec Coverage

| Requirement | Task |
|-------------|------|
| No Atlas / UV merge / slot collapse | Task 2 |
| Per-slot Archer-style BoneAnimation + SetMaterial | Task 3 |
| NumFrames → EndFrame runtime | Task 1 |
| Table EndFrame + preserve bDefaultLoop by AnimName | Task 4 |
| Orchestrator / Actor / dialogs | Task 2–3 |
