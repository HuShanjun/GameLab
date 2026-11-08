# GetCrowdLegionFrame + MF Rename Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add `GetCrowdLegionFrame` (no Custom HLSL), rename plugin MF to `MF_CrowdLegionBoneAnimation` with Frame from that function, align `UCrowdUnitComponent` CustomData to AutoPlay-style floats, and rebuild `M_Crowd_Legion` / `M_Archer_BoneAnimation` generators accordingly.

**Architecture:** Exporter creates/rebuilds material functions with pure expression graphs. Component writes `[TimeOffset, SignedPlayRate, StartFrame, EndFrame]`. Parent materials wire PICD → `GetCrowdLegionFrame` → `MF_CrowdLegionBoneAnimation.Frame`.

**Tech Stack:** UE 5.8, VgCrowdLegion / VgCrowdLegionEditor, MaterialExpression APIs.

## Global Constraints

- No `UMaterialExpressionCustom` for frame math
- SampleRate is a function input (default 30)
- PlayRate sign: `>=0` loop, `<0` one-shot hold last frame
- Spec: `Docs/superpowers/specs/2026-11-08-getcrowdlegionframe-design.md`

## File Map

| Path | Change |
|------|--------|
| `CrowdLegionExporter.h/.cpp` | Ensure GetCrowdLegionFrame; rename MF; rebuild parents |
| `CrowdUnitComponent.h/.cpp` | CustomData constants + PushCellCustomData |
| `CrowdStatsComponent.cpp` | Match new TimeOffset / EndFrame semantics |
| Plugin Content | `GetCrowdLegionFrame`, `MF_CrowdLegionBoneAnimation`; deprecate old MF name |

---

### Task 1: Align `UCrowdUnitComponent` CustomData

**Files:**
- Modify: `Client/Plugins/Development/VgCrowdLegion/Source/VgCrowdLegion/Public/CrowdUnitComponent.h`
- Modify: `Client/Plugins/Development/VgCrowdLegion/Source/VgCrowdLegion/Private/CrowdUnitComponent.cpp`
- Modify: `Client/Plugins/Development/VgCrowdLegion/Source/VgCrowdLegion/Private/CrowdStatsComponent.cpp`

- [ ] **Step 1: Update constants in `FCrowdCellInstanceData`**

Replace CD indices with:

```cpp
static constexpr int32 CD_TimeOffset = 0;
static constexpr int32 CD_PlayRate   = 1;
static constexpr int32 CD_StartFrame = 2;
static constexpr int32 CD_EndFrame   = 3;
static constexpr int32 CustomDataCount = 4;
```

Update struct comment to match. Keep `Phase`/`StartTime` fields as CPU-side storage for the TimeOffset value (or rename Phase comment to “stored TimeOffset”).

- [ ] **Step 2: Rewrite `PushCellCustomData`**

```cpp
float StartFrame = 0.f;
float EndFrame = 0.f;
if (Animations.IsValidIndex(D.AnimIndex))
{
	StartFrame = float(Animations[D.AnimIndex].StartFrame);
	const int32 NumFrames = FMath::Max(1, Animations[D.AnimIndex].NumFrames);
	EndFrame = StartFrame + float(NumFrames - 1);
}
const float SafeRate = FMath::Max(0.0001f, FMath::Abs(D.PlayRate));
const float SignedRate = D.bLoop ? SafeRate : -SafeRate;
// D.Phase already holds random phase (loop) or -Now / Now per SetCellAnim — see Step 3
float CustomData[4];
CustomData[CD_TimeOffset] = D.Phase; // TimeOffset
CustomData[CD_PlayRate] = SignedRate;
CustomData[CD_StartFrame] = StartFrame;
CustomData[CD_EndFrame] = EndFrame;
```

- [ ] **Step 3: Fix AddCell / SetCellAnim TimeOffset for one-shot**

Loop: `Phase = FRand() * 10.f` (TimeOffset)  
One-shot: `Phase = -GetCurrentAnimationTime(World)` so `Time + TimeOffset` starts at 0  

Remove use of PackedRate / NumFrames in CustomData array.

- [ ] **Step 4: Update `CrowdStatsComponent` frame estimate**

Use EndFrame semantics and `Time + Phase` for both (one-shot Phase is negative start):

```cpp
const float Elapsed = FMath::Max(0.f, (CurrentTime + D.Phase) * FMath::Abs(D.PlayRate));
const float Length = FMath::Max(1.f, EndFrame - StartFrame + 1.f);
FrameOffset = D.bLoop ? Floor(Fmod(Elapsed*30, Length)) : Min(Length-1, Floor(Elapsed*30));
```

(Or Abs(PlayRate) and ignore bLoop if deriving from signed rate stored only in GPU — prefer keep `D.bLoop`.)

- [ ] **Step 5: Commit**

```powershell
git add Client/Plugins/Development/VgCrowdLegion/Source/VgCrowdLegion
git commit -m "Align CrowdUnit CustomData with AutoPlay TimeOffset/PlayRate/Start/End."
```

---

### Task 2: Implement `EnsureGetCrowdLegionFrame` material function

**Files:**
- Modify: `CrowdLegionExporter.h` — declare `static bool EnsureGetCrowdLegionFrame(UMaterialFunction*& OutFunc);`
- Modify: `CrowdLegionExporter.cpp`

**Path:** `/VgCrowdLegion/Materials/GetCrowdLegionFrame`

- [ ] **Step 1: Build expression graph (no Custom)**

Create/load `UMaterialFunction` named `GetCrowdLegionFrame`.

FunctionInputs (Scalar): `TimeOffset`, `Playrate`, `StartFrame`, `EndFrame`, `SampleRate` (preview default 30), optional `Time` OR use internal `UMaterialExpressionTime` (bIgnorePause=true) if Time not exposed — **expose Time as input** with optional default from Time node via preview, matching GetAutoPlayFrame style (inputs for TimeOffset/Playrate/StartFrame/EndFrame/SampleRate + Time expression inside).

Minimal internal graph:

1. Inputs → Abs(Playrate) → RateAbs  
2. `Playrate >= 0` via `UMaterialExpressionIf` (A=Playrate, B=0, A>=B → 1 else 0) or compare nodes available in engine  
3. `Elapsed = (Time + TimeOffset) * RateAbs * SampleRate`  
4. `Length = Max(1, EndFrame - StartFrame + 1)`  
5. Loop path: `Floor(Fmod(Elapsed, Length))`  
6. One-shot: `Min(Length - 1, Floor(Elapsed))`  
7. `If(bLoop, LoopOffset, OneShotOffset)`  
8. `Frame = StartFrame + Offset` → FunctionOutput `Frame`

Use: `MaterialExpressionAbs`, `Add`, `Multiply`, `Max`, `Min`, `Fmod`, `Floor`, `If`, `FunctionInput`, `FunctionOutput`, `Time`.

**Do not** add `UMaterialExpressionCustom`.

- [ ] **Step 2: Wire helpers in Exporter to call Ensure before MF ensure**

- [ ] **Step 3: Commit**

```powershell
git commit -m "Add EnsureGetCrowdLegionFrame material function generator."
```

(Content asset may appear after editor run in Task 5.)

---

### Task 3: Rename MF → `MF_CrowdLegionBoneAnimation` and attach GetCrowdLegionFrame

**Files:**
- Modify: `CrowdLegionExporter.cpp` constants and `EnsurePluginMasterMaterials`

- [ ] **Step 1: Change package constants**

```cpp
static const TCHAR* GPluginMFPackage = TEXT("/VgCrowdLegion/Materials/MF_CrowdLegionBoneAnimation");
static const TCHAR* GGetCrowdLegionFramePkg = TEXT("/VgCrowdLegion/Materials/GetCrowdLegionFrame");
// Official source for duplicate still:
static const TCHAR* GOfficialMFPath = TEXT("/AnimToTexture/Materials/MaterialFunctions/MF_BoneAnimation.MF_BoneAnimation");
```

- [ ] **Step 2: Ensure MF under new name**

- Load `MF_CrowdLegionBoneAnimation`; if missing, duplicate official MF into new package/name  
- If old `/VgCrowdLegion/Materials/MF_BoneAnimation` exists, prefer migrate (duplicate to new name) then stop referencing old path  
- Ensure Frame FunctionInput exists (existing logic)  
- **Do not** bake GetCrowdLegionFrame inside MF if parent supplies Frame — keep Frame as FunctionInput; parents connect GetCrowdLegionFrame → Frame  

- [ ] **Step 3: Commit**

```powershell
git commit -m "Rename plugin bone MF to MF_CrowdLegionBoneAnimation."
```

---

### Task 4: Rebuild `M_Crowd_Legion` and `M_Archer_BoneAnimation` generators

**Files:**
- Modify: `CrowdLegionExporter.cpp` (`EnsurePluginMasterMaterials` crowd master branch; `EnsureArcherBoneAnimationParentMaterial`)
- Modify: tooltips in `VgCrowdLegionEditor.cpp` / header comments

- [ ] **Step 1: M_Crowd_Legion creation path**

When creating/rebuilding master:

1. Remove CustomCalc + old CD_NumFrames/StartTime/PackedRate network  
2. Add PICD 0..3: TimeOffset, Playrate, StartFrame, EndFrame  
3. Add Time (IgnorePause), SampleRate scalar param default 30  
4. Call `GetCrowdLegionFrame` MF; connect PICD+Time+SampleRate to its inputs  
5. Call `MF_CrowdLegionBoneAnimation`; connect GetCrowdLegionFrame output → Frame; keep Material Attributes chain as designed  
6. Force rebuild if asset already exists (clear expressions), same as Archer parent  

- [ ] **Step 2: Archer parent**

Same Frame path: PICD → GetCrowdLegionFrame → MF_CrowdLegionBoneAnimation.Frame  
Keep TextureSample → MakeMA → MF → Material Attributes / Front Material  
Remove lone Frame scalar as sole driver (optional keep unused — prefer remove to avoid confusion)

- [ ] **Step 3: Update strings/dialogs** mentioning `MF_BoneAnimation`

- [ ] **Step 4: Commit**

```powershell
git commit -m "Wire Crowd/Archer masters to GetCrowdLegionFrame and renamed MF."
```

---

### Task 5: Build, regenerate assets, verify

- [ ] **Step 1: Build**

```powershell
cmd /c "E:\UnrealRepo\GameLab\Client\ubt_build.bat ClientEditor Win64 Development"
```

Expected: exit 0

- [ ] **Step 2: Unattended generate**

```powershell
& "D:\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "E:\UnrealRepo\GameLab\Client\Client.uproject" -unattended -NullRHI -nosplash -nosound -log "-ExecCmds=CrowdLegion.GenerateArcherBoneAnimationMaterials" -Quit
```

Also trigger Crowd master ensure via existing export menu or add console cmd `CrowdLegion.EnsurePluginMasterMaterials` if needed — if only Archer cmd exists, call `EnsurePluginMasterMaterials` from Archer execute path (already does) so both GetCrowdLegionFrame + MF + Archer parent rebuild; for `M_Crowd_Legion` add force-rebuild in Ensure when MF name changed.

- [ ] **Step 3: Acceptance**

```powershell
@(
  (Test-Path "E:\UnrealRepo\GameLab\Client\Plugins\Development\VgCrowdLegion\Content\Materials\GetCrowdLegionFrame.uasset"),
  (Test-Path "E:\UnrealRepo\GameLab\Client\Plugins\Development\VgCrowdLegion\Content\Materials\MF_CrowdLegionBoneAnimation.uasset"),
  (Test-Path "E:\UnrealRepo\GameLab\Client\Content\CrowdLegion\Characters\Archer\Materials\BoneAnimation\M_Archer_BoneAnimation.uasset")
)
```

String-scan Archer parent / M_Crowd_Legion for `GetCrowdLegionFrame` and absence of large Custom frame code blobs.

- [ ] **Step 4: Commit generated Content**

```powershell
git add Client/Plugins/Development/VgCrowdLegion/Content/Materials Client/Content/CrowdLegion/Characters/Archer/Materials/BoneAnimation
git commit -m "Add generated GetCrowdLegionFrame and rebuilt CrowdLegion materials."
```

---

## Spec Coverage

| Requirement | Task |
|-------------|------|
| GetCrowdLegionFrame, no Custom | Task 2 |
| SampleRate input default 30 | Task 2 |
| MF rename + Frame from GetCrowdLegionFrame | Task 3–4 |
| Component CustomData layout | Task 1 |
| M_Crowd_Legion / M_Archer generators | Task 4 |
| Build + regenerate | Task 5 |
