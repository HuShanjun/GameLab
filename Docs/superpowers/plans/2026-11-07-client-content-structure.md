# Client Content Game/Editor/Developer Skeleton Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Under `Client/Content/`, create `Game/`, `Editor/`, and `Developer/` skeletons; move personal files into `Developer/`; exclude `Editor` and `Developer` from packaging — without moving existing template game assets.

**Architecture:** Disk-level folder convention plus `ProjectPackagingSettings.DirectoriesToNeverCook`. Existing template paths (`ThirdPerson`, `Characters`, `Input`, etc.) stay at `Content/` root. Empty folders are tracked via `.gitkeep`.

**Tech Stack:** Unreal Engine 5.8 project layout, `DefaultGame.ini`, Git, PowerShell on Windows.

## Global Constraints

- Scope is skeleton only — do **not** migrate `ThirdPerson` / `Characters` / `Input` / `LevelPrototyping` into `Game/`
- Do **not** change `DefaultEngine.ini` map or GameMode paths
- NeverCook paths must be exactly `/Game/Editor` and `/Game/Developer`
- Spec: `Docs/superpowers/specs/2026-11-07-client-content-structure-design.md`

## File Map

| Path | Responsibility |
|------|----------------|
| `Client/Content/Game/.gitkeep` | Track empty Game root |
| `Client/Content/Editor/.gitkeep` | Track empty Editor root |
| `Client/Content/Developer/.gitkeep` | Track Developer root (optional if children exist) |
| `Client/Content/Developer/hushanjun/` | Personal sandbox (moved from `Developers/hushanjun`) |
| `Client/Config/DefaultGame.ini` | Add NeverCook for Editor + Developer |
| `Client/Content/Developers/` | Remove after move if empty |

---

### Task 1: Create Game and Editor skeletons

**Files:**
- Create: `Client/Content/Game/.gitkeep`
- Create: `Client/Content/Editor/.gitkeep`

**Interfaces:**
- Consumes: none
- Produces: tracked empty dirs `Content/Game`, `Content/Editor`

- [ ] **Step 1: Create directories and placeholder files**

```powershell
New-Item -ItemType Directory -Force -Path "E:\UnrealRepo\GameLab\Client\Content\Game" | Out-Null
New-Item -ItemType Directory -Force -Path "E:\UnrealRepo\GameLab\Client\Content\Editor" | Out-Null
Set-Content -Path "E:\UnrealRepo\GameLab\Client\Content\Game\.gitkeep" -Value "" -NoNewline
Set-Content -Path "E:\UnrealRepo\GameLab\Client\Content\Editor\.gitkeep" -Value "" -NoNewline
```

- [ ] **Step 2: Verify directories exist**

```powershell
Test-Path "E:\UnrealRepo\GameLab\Client\Content\Game\.gitkeep"
Test-Path "E:\UnrealRepo\GameLab\Client\Content\Editor\.gitkeep"
```

Expected: both `True`

- [ ] **Step 3: Commit**

```powershell
git add Client/Content/Game/.gitkeep Client/Content/Editor/.gitkeep
git commit -m "Add Content/Game and Content/Editor directory skeletons."
```

---

### Task 2: Create Developer and migrate personal folder

**Files:**
- Create: `Client/Content/Developer/` (via move)
- Move: `Client/Content/Developers/hushanjun/` → `Client/Content/Developer/hushanjun/`
- Delete: `Client/Content/Developers/` if empty after move
- Create: `Client/Content/Developer/.gitkeep` only if needed for an otherwise-empty Developer root (skip if `hushanjun` is present)

**Interfaces:**
- Consumes: existing `Client/Content/Developers/hushanjun/`
- Produces: `Client/Content/Developer/hushanjun/`; no `Content/Developers/` leftover

- [ ] **Step 1: Create Developer root and move hushanjun**

```powershell
New-Item -ItemType Directory -Force -Path "E:\UnrealRepo\GameLab\Client\Content\Developer" | Out-Null
Move-Item -Path "E:\UnrealRepo\GameLab\Client\Content\Developers\hushanjun" -Destination "E:\UnrealRepo\GameLab\Client\Content\Developer\hushanjun"
```

- [ ] **Step 2: Remove empty Developers tree**

```powershell
$devLegacy = "E:\UnrealRepo\GameLab\Client\Content\Developers"
if (Test-Path $devLegacy) {
  $left = Get-ChildItem $devLegacy -Force -Recurse -ErrorAction SilentlyContinue
  if (-not $left) { Remove-Item $devLegacy -Recurse -Force }
  else {
    # If only empty nested dirs remain, remove the whole tree
    Remove-Item $devLegacy -Recurse -Force -ErrorAction SilentlyContinue
  }
}
```

- [ ] **Step 3: Verify layout**

```powershell
Test-Path "E:\UnrealRepo\GameLab\Client\Content\Developer\hushanjun"
Test-Path "E:\UnrealRepo\GameLab\Client\Content\Developers"
```

Expected: first `True`, second `False`

- [ ] **Step 4: Commit**

```powershell
git add -A Client/Content/Developer Client/Content/Developers
git status
git commit -m "Move personal Content/Developers into Content/Developer."
```

If `git status` shows no changes (empty folders never tracked), create `Client/Content/Developer/.gitkeep` and commit that plus any tracked path updates:

```powershell
Set-Content -Path "E:\UnrealRepo\GameLab\Client\Content\Developer\.gitkeep" -Value "" -NoNewline
git add Client/Content/Developer/.gitkeep
git commit -m "Track Content/Developer root for personal sandboxes."
```

---

### Task 3: Exclude Editor and Developer from cooking

**Files:**
- Modify: `Client/Config/DefaultGame.ini`

**Interfaces:**
- Consumes: Task 1–2 directories at `/Game/Editor` and `/Game/Developer`
- Produces: packaging settings that never cook those paths

- [ ] **Step 1: Read current file**

```powershell
Get-Content "E:\UnrealRepo\GameLab\Client\Config\DefaultGame.ini"
```

Expected current content ends after `[ConsoleVariables]` block (no `ProjectPackagingSettings` yet).

- [ ] **Step 2: Append packaging NeverCook settings**

Write `Client/Config/DefaultGame.ini` to this exact full content:

```ini
[/Script/EngineSettings.GeneralProjectSettings]
ProjectID=6A1FFB4C4C14F0A18368AE88DA7878FB
ProjectName=Third Person BP Game Template

[ConsoleVariables]
CommonUI.CheckKeyboardFocusAndParentage=1
CommonUI.DisallowUserFocusedWidgetForPendingFocusRecipient=1
CommonUI.FallbackToDesiredOnAutoRestoreFailure=1

[/Script/UnrealEd.ProjectPackagingSettings]
+DirectoriesToNeverCook=(Path="/Game/Editor")
+DirectoriesToNeverCook=(Path="/Game/Developer")
```

Do **not** add `/Game/Game` to NeverCook.

- [ ] **Step 3: Verify ini entries**

```powershell
Select-String -Path "E:\UnrealRepo\GameLab\Client\Config\DefaultGame.ini" -Pattern "DirectoriesToNeverCook"
```

Expected: two lines — `/Game/Editor` and `/Game/Developer`

- [ ] **Step 4: Commit**

```powershell
git add Client/Config/DefaultGame.ini
git commit -m "Exclude Content/Editor and Content/Developer from cooking."
```

---

### Task 4: Acceptance verification

**Files:**
- Verify only (no edits unless a prior task left a gap)

**Interfaces:**
- Consumes: outputs of Tasks 1–3
- Produces: confirmation checklist against the spec

- [ ] **Step 1: Run acceptance checks**

```powershell
$root = "E:\UnrealRepo\GameLab\Client"
@(
  (Test-Path "$root\Content\Game\.gitkeep"),
  (Test-Path "$root\Content\Editor\.gitkeep"),
  (Test-Path "$root\Content\Developer\hushanjun"),
  -not (Test-Path "$root\Content\Developers"),
  (Test-Path "$root\Content\ThirdPerson"),
  (Select-String -Path "$root\Config\DefaultGame.ini" -Pattern 'Path="/Game/Editor"' -Quiet),
  (Select-String -Path "$root\Config\DefaultGame.ini" -Pattern 'Path="/Game/Developer"' -Quiet),
  -not (Select-String -Path "$root\Config\DefaultEngine.ini" -Pattern 'Game/Game/' -Quiet)
) | ForEach-Object { $_ }
```

Expected: eight `True` values  
(Last check: Engine map paths were not rewritten under `/Game/Game/...`)

- [ ] **Step 2: Confirm git clean for intended files**

```powershell
git status
```

Expected: working tree clean after Tasks 1–3 commits (or only unrelated local UE `Saved/`/`Intermediate/` noise — do not commit those).

- [ ] **Step 3: No further commit unless Step 1 failed and required a fix**

If Step 1 fails, fix the failing task’s files, re-run Step 1, then commit the fix with a message naming the gap (e.g. `Fix missing Developer NeverCook entry.`).

---

## Spec Coverage Self-Review

| Spec requirement | Task |
|------------------|------|
| Create `Content/Game/` | Task 1 |
| Create `Content/Editor/` | Task 1 |
| Create `Content/Developer/` + migrate `hushanjun` | Task 2 |
| Remove empty `Developers/` | Task 2 |
| NeverCook `/Game/Editor` and `/Game/Developer` | Task 3 |
| Do not migrate template assets / do not change map paths | Tasks 1–4 (explicit non-touch) + Task 4 check |
| `.gitkeep` for trackable empty dirs | Task 1 (and Task 2 fallback) |
| Acceptance criteria | Task 4 |
