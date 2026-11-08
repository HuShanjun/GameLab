# VgCrowdLegion + Client C++ Module Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Enable `VgCrowdLegion` and `AnimToTexture` in the Client project, add a minimal compilable `Client` C++ game module, and version-control plugin source (not plugin Content).

**Architecture:** Convert the Blueprint-only uproject into a thin C++ game host (`Source/Client` + Targets) so project plugins with C++ can build. Keep `VgCrowdLegion` under `Plugins/Development/`; track only `.uplugin` + `Source/`; ignore `Content/` locally.

**Tech Stack:** Unreal Engine 5.8, UnrealBuildTool, Git, PowerShell / batch on Windows.

## Global Constraints

- EngineAssociation remains `5.8`
- No GameMode / GameInstance / Character C++ classes
- Do not change `DefaultEngine.ini` map or BP GameMode paths
- Do not move plugin path `Client/Plugins/Development/VgCrowdLegion/`
- Do not commit `Client/Plugins/Development/VgCrowdLegion/Content/`
- Spec: `Docs/superpowers/specs/2026-11-07-vgcrowdlegion-and-client-cpp-design.md`

## File Map

| Path | Responsibility |
|------|----------------|
| `Client/Source/Client.Target.cs` | Game target |
| `Client/Source/ClientEditor.Target.cs` | Editor target |
| `Client/Source/Client/Client.Build.cs` | Game module rules |
| `Client/Source/Client/Public/Client.h` | Module header |
| `Client/Source/Client/Private/Client.cpp` | `IMPLEMENT_PRIMARY_GAME_MODULE` |
| `Client/Client.uproject` | Modules + plugin enablement |
| `.gitignore` | Ignore plugin Content |
| Plugin `Source/**` + `.uplugin` | Already on disk; add to Git |
| `Client/ubt_build.bat` | Optional: fix project name for local builds (currently `RagdollSample`) |

---

### Task 1: Create minimal Client C++ module and Targets

**Files:**
- Create: `Client/Source/Client.Target.cs`
- Create: `Client/Source/ClientEditor.Target.cs`
- Create: `Client/Source/Client/Client.Build.cs`
- Create: `Client/Source/Client/Public/Client.h`
- Create: `Client/Source/Client/Private/Client.cpp`

**Interfaces:**
- Consumes: none
- Produces: module name `Client`; targets `Client` (Game) and `ClientEditor` (Editor)

- [ ] **Step 1: Write `Client.Target.cs`**

```csharp
// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.Collections.Generic;

public class ClientTarget : TargetRules
{
	public ClientTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.V5;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_6;
		ExtraModuleNames.Add("Client");
	}
}
```

- [ ] **Step 2: Write `ClientEditor.Target.cs`**

```csharp
// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.Collections.Generic;

public class ClientEditorTarget : TargetRules
{
	public ClientEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.V5;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_6;
		ExtraModuleNames.Add("Client");
	}
}
```

If UBT errors that `Unreal5_6` is undefined on this engine install, change **both** Target files to `EngineIncludeOrderVersion.Latest` and continue.

- [ ] **Step 3: Write `Client.Build.cs`**

```csharp
// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class Client : ModuleRules
{
	public Client(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore"
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
		});
	}
}
```

- [ ] **Step 4: Write module header and cpp**

`Client/Source/Client/Public/Client.h`:

```cpp
// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
```

`Client/Source/Client/Private/Client.cpp`:

```cpp
// Copyright Epic Games, Inc. All Rights Reserved.

#include "Client.h"
#include "Modules/ModuleManager.h"

IMPLEMENT_PRIMARY_GAME_MODULE(FDefaultGameModuleImpl, Client, "Client");
```

- [ ] **Step 5: Verify files exist**

```powershell
@(
  "E:\UnrealRepo\GameLab\Client\Source\Client.Target.cs",
  "E:\UnrealRepo\GameLab\Client\Source\ClientEditor.Target.cs",
  "E:\UnrealRepo\GameLab\Client\Source\Client\Client.Build.cs",
  "E:\UnrealRepo\GameLab\Client\Source\Client\Public\Client.h",
  "E:\UnrealRepo\GameLab\Client\Source\Client\Private\Client.cpp"
) | ForEach-Object { Test-Path $_ }
```

Expected: five `True`

- [ ] **Step 6: Commit**

```powershell
git add Client/Source
git commit -m "Add minimal Client C++ game module and Targets."
```

---

### Task 2: Enable Client module and plugins in uproject

**Files:**
- Modify: `Client/Client.uproject`

**Interfaces:**
- Consumes: Task 1 module name `Client`
- Produces: uproject that loads `Client`, `VgCrowdLegion`, `AnimToTexture`

- [ ] **Step 1: Write full `Client.uproject`**

Replace contents with exactly:

```json
{
	"FileVersion": 3,
	"EngineAssociation": "5.8",
	"Category": "",
	"Description": "",
	"Modules": [
		{
			"Name": "Client",
			"Type": "Runtime",
			"LoadingPhase": "Default"
		}
	],
	"Plugins": [
		{
			"Name": "ModelingToolsEditorMode",
			"Enabled": true,
			"TargetAllowList": [
				"Editor"
			]
		},
		{
			"Name": "GameplayStateTree",
			"Enabled": true
		},
		{
			"Name": "VgCrowdLegion",
			"Enabled": true
		},
		{
			"Name": "AnimToTexture",
			"Enabled": true
		}
	]
}
```

- [ ] **Step 2: Verify JSON keys**

```powershell
Select-String -Path "E:\UnrealRepo\GameLab\Client\Client.uproject" -Pattern '"Name": "(Client|VgCrowdLegion|AnimToTexture)"'
```

Expected: matches for `Client`, `VgCrowdLegion`, `AnimToTexture`

- [ ] **Step 3: Confirm Engine map paths untouched**

```powershell
Select-String -Path "E:\UnrealRepo\GameLab\Client\Config\DefaultEngine.ini" -Pattern "GlobalDefaultGameMode|GameDefaultMap"
```

Expected: still `/Game/ThirdPerson/...` paths (not rewritten by this task)

- [ ] **Step 4: Commit**

```powershell
git add Client/Client.uproject
git commit -m "Enable Client module, VgCrowdLegion, and AnimToTexture."
```

---

### Task 3: Gitignore plugin Content; track plugin source

**Files:**
- Modify: `.gitignore`
- Add (git): `Client/Plugins/Development/VgCrowdLegion/VgCrowdLegion.uplugin`
- Add (git): `Client/Plugins/Development/VgCrowdLegion/Source/**`
- Do not add: `Client/Plugins/Development/VgCrowdLegion/Content/**`

**Interfaces:**
- Consumes: existing on-disk plugin tree
- Produces: tracked plugin code; ignored Content

- [ ] **Step 1: Append ignore rule to `.gitignore`**

Append these lines to `E:\UnrealRepo\GameLab\.gitignore` (keep existing rules intact):

```
# VgCrowdLegion local content (test maps / materials) — not versioned
Client/Plugins/Development/VgCrowdLegion/Content/
```

- [ ] **Step 2: Stage plugin source + uplugin only**

```powershell
git add .gitignore
git add Client/Plugins/Development/VgCrowdLegion/VgCrowdLegion.uplugin
git add Client/Plugins/Development/VgCrowdLegion/Source
git status --short Client/Plugins/Development/VgCrowdLegion
```

Expected: staged `.uplugin` and `Source/` files; **no** `Content/` staged. If any `Content/` appears staged, unstage it:

```powershell
git reset HEAD -- Client/Plugins/Development/VgCrowdLegion/Content
```

- [ ] **Step 3: Commit**

```powershell
git commit -m "Track VgCrowdLegion source; ignore plugin Content."
```

---

### Task 4: Acceptance checks (+ optional UBT)

**Files:**
- Optionally fix: `Client/ubt_build.bat` (still hardcodes `RagdollSample`) — only if running a local build

**Interfaces:**
- Consumes: Tasks 1–3 outputs
- Produces: verification checklist pass/fail

- [ ] **Step 1: Run acceptance script**

```powershell
$root = "E:\UnrealRepo\GameLab"
@(
  (Test-Path "$root\Client\Source\Client\Private\Client.cpp"),
  (Test-Path "$root\Client\Source\Client.Target.cs"),
  (Test-Path "$root\Client\Source\ClientEditor.Target.cs"),
  (Select-String -Path "$root\Client\Client.uproject" -Pattern '"Name": "Client"' -Quiet),
  (Select-String -Path "$root\Client\Client.uproject" -Pattern '"Name": "VgCrowdLegion"' -Quiet),
  (Select-String -Path "$root\Client\Client.uproject" -Pattern '"Name": "AnimToTexture"' -Quiet),
  (Select-String -Path "$root\.gitignore" -Pattern 'VgCrowdLegion/Content/' -Quiet),
  -not (git -C $root ls-files "Client/Plugins/Development/VgCrowdLegion/Content" | Select-Object -First 1),
  [bool](git -C $root ls-files "Client/Plugins/Development/VgCrowdLegion/VgCrowdLegion.uplugin"),
  [bool](git -C $root ls-files "Client/Plugins/Development/VgCrowdLegion/Source/VgCrowdLegion/VgCrowdLegion.Build.cs")
) | ForEach-Object { $_ }
```

Expected: ten `True` values

- [ ] **Step 2 (optional): Fix `ubt_build.bat` project name and try Editor build**

If verifying compile locally, set in `Client/ubt_build.bat`:

```bat
set "PROJECT_NAME=Client"
set "DEFAULT_TARGET=ClientEditor"
```

Then run:

```powershell
cmd /c "E:\UnrealRepo\GameLab\Client\ubt_build.bat ClientEditor Win64 Development"
```

Expected: exit code `0`. If the machine lacks UE 5.8 / VS toolchain, record the failure and **do not** block Tasks 1–3 as failed — note it in the completion summary.

If `ubt_build.bat` was fixed and should be tracked:

```powershell
git add Client/ubt_build.bat
git commit -m "Point ubt_build.bat at Client project targets."
```

- [ ] **Step 3: Final git status**

```powershell
git status
```

Expected: clean for planned files; plugin `Content/` either ignored or untracked (not staged). Unrelated untracked files (e.g. old plans, other bats) may remain — do not add unless needed for Step 2.

---

## Spec Coverage Self-Review

| Spec requirement | Task |
|------------------|------|
| Minimal Client module + Targets | Task 1 |
| uproject Modules + VgCrowdLegion + AnimToTexture | Task 2 |
| Track plugin Source + uplugin | Task 3 |
| Ignore plugin Content | Task 3 |
| No business C++ / no Engine map changes | Tasks 1–2 (explicit) + Task 4 check |
| Acceptance | Task 4 |
