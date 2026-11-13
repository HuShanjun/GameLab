# BatchMoveCells CPU Transform 缓存 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 让 `UUnitHISMComponent` 以 `CellTransforms` 为世界空间 Transform 的唯一 CPU 真相源，`BatchMoveCells` 原地运算后批量同步各层 HISM，并去掉 `IgnoredIndices`。

**Architecture:** 并行 `TArray<FTransform> CellTransforms` 与 `CellsData` 下标 1:1。所有 Add/Remove/Clear/Update/Move 先改 CPU 数组，再写 GPU。死亡单位由 `CrowdLegionActor` 先 `RemoveCell` 再整队移动。

**Tech Stack:** Unreal Engine 5.8、`UHierarchicalInstancedStaticMeshComponent`、VgCrowdLegion Runtime/Editor 模块

## Global Constraints

- Transform 存世界空间；不变式 `CellTransforms.Num() == CellsData.Num() == 每层 InstanceCount`
- 不改 CustomData 4 槽 / 动画 API / Bake
- Move 整表 `BatchUpdateInstancesTransforms`，不做脏区
- 破坏性去掉 `BatchMoveCells` 的 `IgnoredIndices` 参数
- 禁止把 `.uasset` / `.umap` 当文本改

---

## File Structure

| 文件 | 职责 |
|------|------|
| `Client/Plugins/Development/VgCrowdLegion/Source/VgCrowdLegion/Public/UnitHISMComponent.h` | `CellTransforms`、新签名、只读访问器 |
| `Client/Plugins/Development/VgCrowdLegion/Source/VgCrowdLegion/Private/UnitHISMComponent.cpp` | CRUD / Move 维护缓存并同步 GPU |
| `Client/Plugins/Development/VgCrowdLegion/Source/VgCrowdLegionEditor/Private/CrowdLegionActor.cpp` | 先 Remove 死亡再 Move；中心点读 CPU 缓存 |
| `Docs/武器和骑兵方案.md` | 去掉旧 `IgnoredIndices` 签名描述 |
| `Docs/superpowers/specs/2026-11-13-unit-hism-base-design.md` | 补一句「先 Remove 再 Move」指向本功能 |

本仓库无 AutomationTest；每任务用编译 / grep / 不变式断言替代单元测试，最后 PIE 手验。

---

### Task 1: 声明 `CellTransforms` 与新 API

**Files:**
- Modify: `Client/Plugins/Development/VgCrowdLegion/Source/VgCrowdLegion/Public/UnitHISMComponent.h`

**Interfaces:**
- Consumes: 现有 `CellsData` / `BatchMoveCells` 声明
- Produces:
  - `TArray<FTransform> CellTransforms`（protected）
  - `bool GetCellTransform(int32 CellIndex, FTransform& OutTransform) const`
  - `const TArray<FTransform>& GetAllCellTransforms() const`
  - `void BatchMoveCells(const FVector& MovementDelta, bool bFaceMovementDirection = true, bool bTeleport = false)`

- [ ] **Step 1: 改头文件声明**

在 `GetCellData` 附近增加只读 Transform API；改 `BatchMoveCells` 签名；在 `CellsData` 旁增加成员：

```cpp
UFUNCTION(BlueprintPure, Category = "CrowdLegion | Cell")
bool GetCellTransform(int32 CellIndex, FTransform& OutTransform) const;

UFUNCTION(BlueprintPure, Category = "CrowdLegion | Cell")
const TArray<FTransform>& GetAllCellTransforms() const { return CellTransforms; }

UFUNCTION(BlueprintCallable, Category = "CrowdLegion | Cell")
void BatchMoveCells(const FVector& MovementDelta, bool bFaceMovementDirection = true, bool bTeleport = false);

// protected:
UPROPERTY()
TArray<FCrowdCellInstanceData> CellsData;

UPROPERTY()
TArray<FTransform> CellTransforms;
```

删除旧签名中的 `const TArray<int32>& IgnoredIndices`。

- [ ] **Step 2: 确认头文件无旧参数残留**

Run (PowerShell):

```powershell
Select-String -Path "Client\Plugins\Development\VgCrowdLegion\Source\VgCrowdLegion\Public\UnitHISMComponent.h" -Pattern "IgnoredIndices"
```

Expected: 无匹配。

- [ ] **Step 3: Commit**

```powershell
git add Client/Plugins/Development/VgCrowdLegion/Source/VgCrowdLegion/Public/UnitHISMComponent.h
git commit -m "refactor(CrowdLegion): BatchMoveCells 去掉 IgnoredIndices，声明 CellTransforms"
```

---

### Task 2: CRUD 路径维护 `CellTransforms`

**Files:**
- Modify: `Client/Plugins/Development/VgCrowdLegion/Source/VgCrowdLegion/Private/UnitHISMComponent.cpp`

**Interfaces:**
- Consumes: Task 1 的 `CellTransforms` / `GetCellTransform`
- Produces: Add / BatchAdd / Remove / Clear / Update / GetCellTransform 保持不变式

- [ ] **Step 1: 实现 `GetCellTransform`**

在 `GetCellData` 后加入：

```cpp
bool UUnitHISMComponent::GetCellTransform(int32 CellIndex, FTransform& OutTransform) const
{
	if (!CellTransforms.IsValidIndex(CellIndex))
	{
		return false;
	}
	OutTransform = CellTransforms[CellIndex];
	return true;
}
```

- [ ] **Step 2: `AddCell` 追加缓存**

在 `CellsData.Add(Data);` 之前或之后（须与实例下标一致）加入：

```cpp
CellTransforms.Add(Transform);
```

（`AddInstance(Transform, true)` 已按世界空间；`Transform` 即世界空间。）

- [ ] **Step 3: `BatchAddCells` 追加缓存**

在向各层 `AddInstances` 之后、写 `CellsData` 循环中，同步追加：

```cpp
CellTransforms.Reserve(StartIdx + AddCount);
// 在循环内或循环前：
for (const FTransform& T : Transforms)
{
	CellTransforms.Add(T);
}
```

保证最终 `CellTransforms.Num() == CellsData.Num()`。推荐在现有 `for (i)` 写 `CellsData` 的循环里同时 `CellTransforms.Add(Transforms[i])`，或循环前一次 `Append`：

```cpp
CellTransforms.Append(Transforms);
```

放在 `CellsData` 填充之前或之后均可，但不得只填一边。

- [ ] **Step 4: `RemoveCell` / `ClearCells` 同步**

```cpp
// RemoveCell — CellsData.RemoveAtSwap 旁：
CellTransforms.RemoveAtSwap(CellIndex);

// ClearCells — CellsData.Empty() 旁：
CellTransforms.Empty();
```

- [ ] **Step 5: `UpdateCellTransform` 写 CPU 再推 GPU**

```cpp
bool UUnitHISMComponent::UpdateCellTransform(int32 CellIndex, const FTransform& NewTransform, bool bWorldSpace)
{
	if (!AreLayersReady() || !CellsData.IsValidIndex(CellIndex) || !CellTransforms.IsValidIndex(CellIndex))
	{
		return false;
	}

	FTransform WorldT = NewTransform;
	if (!bWorldSpace)
	{
		UHierarchicalInstancedStaticMeshComponent* Primary = Layers[0].HISMCmp;
		WorldT = NewTransform * Primary->GetComponentTransform();
	}
	CellTransforms[CellIndex] = WorldT;

	bool bAllOk = true;
	for (FUnitHISMLayerRuntime& Layer : Layers)
	{
		if (!Layer.HISMCmp->UpdateInstanceTransform(CellIndex, WorldT, true, true, true))
		{
			bAllOk = false;
		}
	}
	return bAllOk;
}
```

说明：CPU 始终存世界空间；推 GPU 统一 `bWorldSpace=true` + `WorldT`，避免本地/世界混用。

- [ ] **Step 6: 临时让 cpp 能编过（若 Task 3 未做）**

若本任务单独提交：把 `BatchMoveCells` 定义改成新签名，函数体可先 `unimplemented` 式早退或留空实现，但推荐与 Task 3 同提交。若分提交，先改签名并 `return;` 占位，下一步立刻实现。

- [ ] **Step 7: Commit**

```powershell
git add Client/Plugins/Development/VgCrowdLegion/Source/VgCrowdLegion/Private/UnitHISMComponent.cpp
git commit -m "feat(CrowdLegion): CRUD 维护 CellTransforms 作为 CPU 真相源"
```

---

### Task 3: 重写 `BatchMoveCells`

**Files:**
- Modify: `Client/Plugins/Development/VgCrowdLegion/Source/VgCrowdLegion/Private/UnitHISMComponent.cpp`（`BatchMoveCells`）

**Interfaces:**
- Consumes: `CellTransforms`、新签名
- Produces: 无 `GetInstanceTransform` / 无 `IgnoredIndices` 的批量移动

- [ ] **Step 1: 替换整个 `BatchMoveCells` 实现**

```cpp
void UUnitHISMComponent::BatchMoveCells(const FVector& MovementDelta, bool bFaceMovementDirection, bool bTeleport)
{
	if (!AreLayersReady() || CellTransforms.Num() == 0)
	{
		return;
	}

	const bool bHasFacing = bFaceMovementDirection && !MovementDelta.GetSafeNormal2D().IsNearlyZero();
	const FQuat TargetQuat = bHasFacing
		? MovementDelta.GetSafeNormal2D().Rotation().Quaternion()
		: FQuat::Identity;

	for (FTransform& T : CellTransforms)
	{
		T.AddToTranslation(MovementDelta);
		if (bHasFacing)
		{
			T.SetRotation(TargetQuat);
		}
	}

	for (FUnitHISMLayerRuntime& Layer : Layers)
	{
		Layer.HISMCmp->BatchUpdateInstancesTransforms(0, CellTransforms, true, true, bTeleport);
	}
}
```

删除：`IgnoredSet`、`GetInstanceTransform`、临时 `UpdatedTransforms` 拷贝（直接传 `CellTransforms`）。

- [ ] **Step 2: 确认 cpp 无旧路径**

```powershell
Select-String -Path "Client\Plugins\Development\VgCrowdLegion\Source\VgCrowdLegion\Private\UnitHISMComponent.cpp" -Pattern "IgnoredIndices|GetInstanceTransform"
```

Expected: 无匹配。

- [ ] **Step 3: Commit**

```powershell
git add Client/Plugins/Development/VgCrowdLegion/Source/VgCrowdLegion/Private/UnitHISMComponent.cpp
git commit -m "perf(CrowdLegion): BatchMoveCells 原地改 CellTransforms 并批量同步 GPU"
```

---

### Task 4: 更新 `CrowdLegionActor` 调用方

**Files:**
- Modify: `Client/Plugins/Development/VgCrowdLegion/Source/VgCrowdLegionEditor/Private/CrowdLegionActor.cpp`

**Interfaces:**
- Consumes: `RemoveCell`、`BatchMoveCells(Delta, Face, Teleport)`、`GetAllCellTransforms()`
- Produces: Tick 先删死亡再移动；中心点读 CPU 缓存

- [ ] **Step 1: 改 `GetLegionCenterLocation` 读 CPU 缓存**

```cpp
FVector ACrowdLegionActor::GetLegionCenterLocation() const
{
	if (!UnitComponent || UnitComponent->GetCellCount() == 0)
	{
		return GetActorLocation();
	}

	const TArray<FTransform>& Transforms = UnitComponent->GetAllCellTransforms();
	FVector SumLoc = FVector::ZeroVector;
	for (const FTransform& T : Transforms)
	{
		SumLoc += T.GetLocation();
	}
	return SumLoc / static_cast<float>(Transforms.Num());
}
```

- [ ] **Step 2: 改 `Tick`：高→低 Remove 死亡，再 Move**

```cpp
	const FVector Dir = ToTarget.GetSafeNormal2D();
	const FVector MovementDelta = Dir * MarchSpeed * DeltaTime;

	const TArray<FCrowdCellInstanceData>& Cells = UnitComponent->GetAllInstanceData();
	for (int32 i = Cells.Num() - 1; i >= 0; --i)
	{
		if (Cells[i].AnimIndex == DeathAnimIndex)
		{
			UnitComponent->RemoveCell(i);
		}
	}

	if (UnitComponent->GetCellCount() == 0)
	{
		StopLegionMarch();
		return;
	}

	UnitComponent->BatchMoveCells(MovementDelta, true, false);
```

注意：`GetAllInstanceData()` 返回 const 引用；循环中 `RemoveCell` 会改底层数组——必须按索引从高到低删除，且每轮用当前 `Cells` 引用时，因 `RemoveAtSwap` 会打乱，**正确写法是每次重新取或只按索引删并接受 swap**：

推荐稳妥写法（避免悬挂/错删）：

```cpp
	for (int32 i = UnitComponent->GetCellCount() - 1; i >= 0; --i)
	{
		FCrowdCellInstanceData Data;
		if (UnitComponent->GetCellData(i, Data) && Data.AnimIndex == DeathAnimIndex)
		{
			UnitComponent->RemoveCell(i);
		}
	}
```

- [ ] **Step 3: 确认 Actor 无 `IgnoredIndices` / 无对本文件 `GetInstanceTransform`**

```powershell
Select-String -Path "Client\Plugins\Development\VgCrowdLegion\Source\VgCrowdLegionEditor\Private\CrowdLegionActor.cpp" -Pattern "IgnoredIndices|GetInstanceTransform|DeadIndices"
```

Expected: 无匹配。

- [ ] **Step 4: Commit**

```powershell
git add Client/Plugins/Development/VgCrowdLegion/Source/VgCrowdLegionEditor/Private/CrowdLegionActor.cpp
git commit -m "fix(CrowdLegion): 行军先 Remove 死亡单位，中心点读 CellTransforms"
```

---

### Task 5: 文档同步与编译验收

**Files:**
- Modify: `Docs/武器和骑兵方案.md`（约 L209）
- Modify: `Docs/superpowers/specs/2026-11-13-unit-hism-base-design.md`（Cell 与 GPU 段）

**Interfaces:**
- Consumes: 已落地 API
- Produces: Docs 与代码一致；Editor 可编译

- [ ] **Step 1: 更新武器/骑兵方案签名**

将：

```markdown
- `BatchMoveCells(MovementDelta, IgnoredIndices, bFaceMovementDirection, bTeleport)`：马匹与骑手双端以相同位移量更新。
```

改为：

```markdown
- `BatchMoveCells(MovementDelta, bFaceMovementDirection, bTeleport)`：在 CPU `CellTransforms` 上整队平移/朝向后批量同步各 HISM 层；死亡单位由调用方先 `RemoveCell`。
```

- [ ] **Step 2: 更新 unit-hism 基类 spec 一句**

在「Cell 与 GPU」列表中 `BatchMoveCells` 相关句后补：

```markdown
- `BatchMoveCells` 使用组件内 `CellTransforms`（世界空间）原地更新后 `BatchUpdateInstancesTransforms`；不再接受 `IgnoredIndices`（需跳过的 Cell 由调用方先 `RemoveCell`）。详见 `2026-11-13-batch-move-cell-transforms-design.md`。
```

- [ ] **Step 3: 全插件残留扫描**

```powershell
Select-String -Path "Client\Plugins\Development\VgCrowdLegion\**\*.{h,cpp}" -Pattern "IgnoredIndices"
Select-String -Path "Docs\**\*.md" -Pattern "IgnoredIndices"
```

Expected: 设计/计划文档可保留「旧签名」对比；代码与 `武器和骑兵方案.md` 无现行 API 残留。`batch-move-cell-transforms-design.md` 中「旧」签名段落可保留。

- [ ] **Step 4: 编译**

```powershell
.\ubt_build.bat
```

Expected: VgCrowdLegion / VgCrowdLegionEditor 成功，无 `BatchMoveCells` / `CellTransforms` 相关错误。

- [ ] **Step 5: PIE 手验清单**

1. Setup 军团 → 行军：全体平移且朝向移动方向。
2. 将部分单位切死亡动画后再行军：死亡实例被移除，存活者继续移动。
3. 多表层（骑兵）时马/骑手 Transform 一致。

- [ ] **Step 6: Commit**

```powershell
git add Docs/武器和骑兵方案.md Docs/superpowers/specs/2026-11-13-unit-hism-base-design.md
git commit -m "docs: 同步 BatchMoveCells / CellTransforms 说明"
```

---

## Spec Coverage Checklist

| Spec 要求 | Task |
|-----------|------|
| `CellTransforms` 并行数组、世界空间 | 1–2 |
| CRUD 全路径维护缓存 | 2 |
| Move 原地运算 + 批量 GPU | 3 |
| 去掉 `IgnoredIndices` | 1, 3, 4 |
| 调用方先 Remove 再 Move | 4 |
| Docs 同步 | 5 |
| 验收：无 GetInstanceTransform 行军路径、层一致、不变式 | 3–5 |

## Self-Review Notes

- 无 TBD/占位实现步骤。
- `UpdateCellTransform` 统一推世界空间，与缓存约定一致。
- Task 2/3 可合并为一个 commit 若希望避免中间不可链接状态；分提交时 Task 2 Step 6 必须改签名占位。
