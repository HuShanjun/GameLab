# CrowdStats 性能 CSV 导出 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 让 `UCrowdStatsComponent` 在每次动画段切换时把场景元数据与 Stat Unit smoothed 五件套的段内均值写入带时间戳的 CSV。

**Architecture:** Stats 组件管理 CSV 文件与累加器；`NotifyAnimSegment` 结算上一段并开新段；`UUnitHISMComponent::SetAllCellsAnim` 成功后 Find Stats 并 Notify。半径通过 Owner 上名为 `SpreadRadius` 的 float 属性反射读取（Runtime 不能依赖 Editor 的 `ACrowdLegionActor`）。

**Tech Stack:** UE 5.8、`FStatUnitData`（`UnrealClient.h` / GameViewport）、`FFileHelper`、`FPaths::ProjectSavedDir()`、VgCrowdLegion Runtime

## Global Constraints

- CSV 列：`Anim,Radius,Units,Poses,Bones,Frame_ms,Game_ms,Draw_ms,GPU_ms,RHIT_ms`
- Stat Unit 用 smoothed：`FrameTime` / `GameThreadTime` / `RenderThreadTime` / `GPUFrameTime` / `RHITTime`
- 每次 BeginPlay 新建 `Saved/CrowdLegion/PerfStats_YYYYMMDD_HHMMSS.csv`
- 人数=`GetCellCount()`；半径=Owner.`SpreadRadius` 或 `-1`；Bones=`GetTotalEquivalentBones()`；Poses=`GetActivePoseCount()`
- 禁止把 `.uasset` / `.umap` 当文本改
- Runtime 模块不得 `#include` Editor 模块的 `CrowdLegionActor.h`

---

## File Structure

| 文件 | 职责 |
|------|------|
| `.../Public/CrowdStatsComponent.h` | `bEnablePerfCsv`、`NotifyAnimSegment`、段累加器与文件路径私有状态 |
| `.../Private/CrowdStatsComponent.cpp` | 开文件 / Tick 采样 / flush 写行 / EndPlay |
| `.../Private/UnitHISMComponent.cpp` | `SetAllCellsAnim` 末尾 Notify（ByName 走此路径即可） |

无 AutomationTest；用编译 + grep + PIE 手验。

---

### Task 1: Stats 头文件 API 与段状态

**Files:**
- Modify: `Client/Plugins/Development/VgCrowdLegion/Source/VgCrowdLegion/Public/CrowdStatsComponent.h`

**Interfaces:**
- Consumes: 现有 Stats HUD API
- Produces:
  - `bool bEnablePerfCsv = true`
  - `void NotifyAnimSegment(FName AnimName)`
  - private：`FlushCurrentSegment()`、`StartSegment(FName)`、`OpenPerfCsv()`、`ClosePerfCsv()`、累加器字段

- [ ] **Step 1: 在 public 区（`SetDebugStatsFontSize` 附近）增加**

```cpp
/** 为 true 时 BeginPlay 创建时间戳 CSV，并在 Notify 边界写出段均值 */
UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CrowdLegion | Stats")
bool bEnablePerfCsv = true;

/** 动画段切换完成时调用：结算上一段均值行并开始新段 */
UFUNCTION(BlueprintCallable, Category = "CrowdLegion | Stats")
void NotifyAnimSegment(FName AnimName);
```

- [ ] **Step 2: 在 private 区增加**

```cpp
void OpenPerfCsvIfNeeded();
void ClosePerfCsv();
void StartSegment(FName AnimName);
void FlushCurrentSegment();
float ResolveSpreadRadius() const;
FString BuildPerfCsvPath() const;

FString PerfCsvAbsolutePath;
bool bSegmentActive = false;
FName SegmentAnimName = NAME_None;
int32 SegmentSampleCount = 0;
double SumFrameMs = 0.0;
double SumGameMs = 0.0;
double SumDrawMs = 0.0;
double SumGpuMs = 0.0;
double SumRhitMs = 0.0;
```

- [ ] **Step 3: Commit**

```powershell
git add Client/Plugins/Development/VgCrowdLegion/Source/VgCrowdLegion/Public/CrowdStatsComponent.h
git commit -m "feat(CrowdLegion): CrowdStats 声明 Perf CSV 与 NotifyAnimSegment API"
```

---

### Task 2: 实现 CSV 开闭、采样、Flush、Notify

**Files:**
- Modify: `Client/Plugins/Development/VgCrowdLegion/Source/VgCrowdLegion/Private/CrowdStatsComponent.cpp`

**Interfaces:**
- Consumes: Task 1 声明；现有 `GetActivePoseCount` / `GetTotalEquivalentBones` / `GetValidUnitComponent`
- Produces: 可运行的段采样与写文件

- [ ] **Step 1: 增加 include**

在现有 include 后加入：

```cpp
#include "Engine/GameViewportClient.h"
#include "UnrealClient.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Misc/DateTime.h"
#include "UObject/UnrealType.h"
```

（若已有 `GameViewportClient` 勿重复。）

- [ ] **Step 2: 实现路径 / 半径 / 开闭文件**

```cpp
FString UCrowdStatsComponent::BuildPerfCsvPath() const
{
	const FString Dir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("CrowdLegion"));
	IFileManager::Get().MakeDirectory(*Dir, true);
	const FString Stamp = FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S"));
	return FPaths::Combine(Dir, FString::Printf(TEXT("PerfStats_%s.csv"), *Stamp));
}

float UCrowdStatsComponent::ResolveSpreadRadius() const
{
	const AActor* Owner = GetOwner();
	if (!Owner)
	{
		return -1.0f;
	}
	const FFloatProperty* FloatProp = FindFProperty<FFloatProperty>(Owner->GetClass(), TEXT("SpreadRadius"));
	if (!FloatProp)
	{
		return -1.0f;
	}
	return FloatProp->GetPropertyValue_InContainer(Owner);
}

void UCrowdStatsComponent::OpenPerfCsvIfNeeded()
{
	if (!bEnablePerfCsv || !PerfCsvAbsolutePath.IsEmpty())
	{
		return;
	}
	PerfCsvAbsolutePath = BuildPerfCsvPath();
	const FString Header = TEXT("Anim,Radius,Units,Poses,Bones,Frame_ms,Game_ms,Draw_ms,GPU_ms,RHIT_ms\n");
	if (!FFileHelper::SaveStringToFile(Header, *PerfCsvAbsolutePath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		UE_LOG(LogTemp, Warning, TEXT("[CrowdStats] Failed to create CSV: %s"), *PerfCsvAbsolutePath);
		PerfCsvAbsolutePath.Reset();
		return;
	}
	UE_LOG(LogTemp, Log, TEXT("[CrowdStats] Perf CSV: %s"), *PerfCsvAbsolutePath);

#if !UE_BUILD_SHIPPING
	if (UWorld* World = GetWorld())
	{
		if (World->IsGameWorld())
		{
			if (APlayerController* PC = World->GetFirstPlayerController())
			{
				PC->ConsoleCommand(TEXT("stat unit"));
			}
		}
	}
#endif
}

void UCrowdStatsComponent::ClosePerfCsv()
{
	FlushCurrentSegment();
	bSegmentActive = false;
	PerfCsvAbsolutePath.Reset();
}
```

- [ ] **Step 3: StartSegment / FlushCurrentSegment / NotifyAnimSegment**

```cpp
void UCrowdStatsComponent::StartSegment(FName AnimName)
{
	SegmentAnimName = AnimName;
	SegmentSampleCount = 0;
	SumFrameMs = SumGameMs = SumDrawMs = SumGpuMs = SumRhitMs = 0.0;
	bSegmentActive = true;
}

void UCrowdStatsComponent::FlushCurrentSegment()
{
	if (!bEnablePerfCsv || !bSegmentActive || PerfCsvAbsolutePath.IsEmpty())
	{
		return;
	}
	if (SegmentSampleCount <= 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("[CrowdStats] Skip CSV row for '%s': no Stat Unit samples."),
			*SegmentAnimName.ToString());
		return;
	}

	const double Inv = 1.0 / static_cast<double>(SegmentSampleCount);
	const UUnitHISMComponent* Comp = GetValidUnitComponent();
	const int32 Units = Comp ? Comp->GetCellCount() : 0;
	const int32 Poses = GetActivePoseCount();
	const int32 Bones = GetTotalEquivalentBones();
	const float Radius = ResolveSpreadRadius();

	const FString Line = FString::Printf(
		TEXT("%s,%.0f,%d,%d,%d,%.3f,%.3f,%.3f,%.3f,%.3f\n"),
		*SegmentAnimName.ToString(),
		Radius,
		Units,
		Poses,
		Bones,
		SumFrameMs * Inv,
		SumGameMs * Inv,
		SumDrawMs * Inv,
		SumGpuMs * Inv,
		SumRhitMs * Inv);

	if (!FFileHelper::SaveStringToFile(Line, *PerfCsvAbsolutePath,
		FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM,
		&IFileManager::Get(),
		EFileWrite::FILEWRITE_Append))
	{
		UE_LOG(LogTemp, Warning, TEXT("[CrowdStats] Failed to append CSV row."));
	}
}

void UCrowdStatsComponent::NotifyAnimSegment(FName AnimName)
{
	if (!bEnablePerfCsv)
	{
		return;
	}
	OpenPerfCsvIfNeeded();
	if (PerfCsvAbsolutePath.IsEmpty())
	{
		return;
	}

	if (bSegmentActive)
	{
		FlushCurrentSegment();
	}
	StartSegment(AnimName);
}
```

- [ ] **Step 4: 改 BeginPlay / EndPlay / TickComponent**

`BeginPlay`：在现有 HUD 逻辑后调用 `OpenPerfCsvIfNeeded()`（仅 `IsGameWorld()`）。

`EndPlay`：在 `RemoveDebugStatsWidget` **之前**调用 `ClosePerfCsv()`。

`TickComponent`：在 HUD 显隐逻辑之后追加：

```cpp
	if (bEnablePerfCsv && bSegmentActive && !PerfCsvAbsolutePath.IsEmpty())
	{
		UWorld* World = GetWorld();
		UGameViewportClient* Viewport = World ? World->GetGameViewport() : nullptr;
		FStatUnitData* StatUnit = Viewport ? Viewport->GetStatUnitData() : nullptr;
		if (StatUnit)
		{
			SumFrameMs += StatUnit->FrameTime;
			SumGameMs += StatUnit->GameThreadTime;
			SumDrawMs += StatUnit->RenderThreadTime;
			SumGpuMs += StatUnit->GPUFrameTime;
			SumRhitMs += StatUnit->RHITTime;
			++SegmentSampleCount;
		}
	}
```

- [ ] **Step 5: 编译 Runtime（或整编辑器）**

```powershell
cd E:\work\GameLab\Client
.\ubt_build.bat
```

Expected: SUCCEEDED（若仅缺 UnitHISM 挂钩仍应能编过 Stats）。

- [ ] **Step 6: Commit**

```powershell
git add Client/Plugins/Development/VgCrowdLegion/Source/VgCrowdLegion/Private/CrowdStatsComponent.cpp Client/Plugins/Development/VgCrowdLegion/Source/VgCrowdLegion/Public/CrowdStatsComponent.h
git commit -m "feat(CrowdLegion): CrowdStats 按动画段采样 Stat Unit 并写 CSV"
```

---

### Task 3: UnitHISM 切动画后 Notify

**Files:**
- Modify: `Client/Plugins/Development/VgCrowdLegion/Source/VgCrowdLegion/Private/UnitHISMComponent.cpp`

**Interfaces:**
- Consumes: `UCrowdStatsComponent::NotifyAnimSegment(FName)`
- Produces: `SetAllCellsAnim` / `ByName`（经前者）自动开段

- [ ] **Step 1: include**

```cpp
#include "CrowdStatsComponent.h"
```

- [ ] **Step 2: 在 `SetAllCellsAnim` 末尾（MarkRenderStateDirty 循环之后）加入**

```cpp
	FName AnimName = NAME_None;
	if (Layers.Num() > 0 && Layers[0].Animations.IsValidIndex(NewAnimIndex))
	{
		AnimName = Layers[0].Animations[NewAnimIndex].AnimName;
	}
	if (AnimName.IsNone())
	{
		AnimName = *FString::Printf(TEXT("Anim_%d"), NewAnimIndex);
	}

	if (AActor* Owner = GetOwner())
	{
		if (UCrowdStatsComponent* Stats = Owner->FindComponentByClass<UCrowdStatsComponent>())
		{
			Stats->NotifyAnimSegment(AnimName);
		}
	}
```

说明：`SetAllCellsAnimByName` 已调用 `SetAllCellsAnim`，无需再 Notify。`SetCellAnim` **不** Notify（规格仅整队切换）。

- [ ] **Step 3: 确认无 Editor 头依赖**

```powershell
Select-String -Path "Client\Plugins\Development\VgCrowdLegion\Source\VgCrowdLegion\**\*.{h,cpp}" -Pattern "CrowdLegionActor.h"
```

Expected: 无匹配。

- [ ] **Step 4: 编译**

```powershell
cd E:\work\GameLab\Client
.\ubt_build.bat
```

Expected: SUCCEEDED。

- [ ] **Step 5: Commit**

```powershell
git add Client/Plugins/Development/VgCrowdLegion/Source/VgCrowdLegion/Private/UnitHISMComponent.cpp
git commit -m "feat(CrowdLegion): SetAllCellsAnim 成功后 Notify CrowdStats 动画段"
```

---

### Task 4: 验收清单（PIE）

**Files:** 无代码（手验）

- [ ] **Step 1: PIE 手验**

1. 放有 `StatsComponent` 的 `ACrowdLegionActor`，`bEnablePerfCsv=true`，Spawn 军团。
2. 依次 Idle → Run → Attack（或等价按钮）。
3. 退出 PIE。
4. 打开最新 `Client/Saved/CrowdLegion/PerfStats_*.csv`（或工程 Saved 对应路径）。

Expected:
- 有表头；至少 2～3 行（首段在第二次切换写出；最后一段 EndPlay flush）。
- `Anim` 与动作名一致；`Radius`≈`SpreadRadius`；`Units`>0；`*_ms` 在 `stat unit` 可用时非全 0。

- [ ] **Step 2: 关闭开关**

`bEnablePerfCsv=false` 再 PIE：不应新建 CSV（或仅旧文件不变）。

- [ ] **Step 3: 若手验通过，在回复中勾选本 Task；无需强制 commit**

---

## Spec Coverage

| Spec 要求 | Task |
|-----------|------|
| 时间戳 CSV + 表头 | 2 |
| 五列元数据 + Stat Unit 五件套均值 | 2 |
| Notify 边界结算；首段不写；EndPlay flush | 2 |
| SetAllCellsAnim 自动 Notify | 3 |
| `bEnablePerfCsv` | 1–2 |
| 半径反射 / 无 Editor 依赖 | 2–3 |
| 可选 `stat unit` | 2 |

## Self-Review Notes

- 半径不用 `Cast<ACrowdLegionActor>`，改用 `FindFProperty<FFloatProperty>(..., "SpreadRadius")`，满足 Runtime 边界且语义等同规格 A。
- `Flush` 后 `Notify` 会 `StartSegment`；`ClosePerfCsv` 先 Flush 再清 `bSegmentActive`，避免双重写。
- `OpenPerfCsvIfNeeded` 在首次 Notify 或 BeginPlay 均可调用；BeginPlay 预创建空表头文件便于确认路径。
