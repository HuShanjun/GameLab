# Unit HISM 基类抽取 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 将 `UCrowdUnitComponent` / `UCavalryUnitComponent` 抽到抽象基类 `UUnitHISMComponent`，用多行层 DataTable 驱动 N 个 HISM，Cell CRUD 与统一相位推送集中在基类。

**Architecture:** 新建 `UnitHISMComponent.h/.cpp` 承载 `FUnitHISMLayerTableRow`、`FUnitAnimationTableRow`、`FCrowdCellInstanceData`、层运行时数组与全部 Cell/Anim API。Crowd/Cavalry 改为薄 `UCLASS` 派生。`CrowdLegionActor` 在 Spawn/导出时组装临时或资产层表后调用新 `SetupHISM`。

**Tech Stack:** UE 5.8、`VgCrowdLegion` Runtime + Editor、`UHierarchicalInstancedStaticMeshComponent`、`UDataTable`

**Spec:** `Docs/superpowers/specs/2026-11-13-unit-hism-base-design.md`

## Global Constraints

- 引擎 5.8；模块路径 `Client/Plugins/Development/VgCrowdLegion/`
- CustomData 仍为 4 槽：`[TimeOffset, SignedPlayRate, StartFrame, EndFrame]`
- 动画跨层对齐：**仅 `AnimIndex` 行号**，不做按名跨层映射
- 材质：仅从每层 `StaticMesh` 槽拷贝
- **删除**旧 `SetupHISM(Mesh, Materials, …)` 与 `SetupCavalryHISM(...)`
- 保留 `UCrowdUnitComponent` / `UCavalryUnitComponent` 类型名
- 无自动化单测框架：以 **UBT 编译通过** + **编辑器 PIE 清单** 作为验收
- 提交信息用中文或约定英文均可；每 Task 结束 commit（用户已允许自动 commit）

## File map

| 文件 | 职责 |
|------|------|
| Create `.../Public/UnitHISMComponent.h` | 共享结构体 + 抽象基类 API |
| Create `.../Private/UnitHISMComponent.cpp` | Setup / CRUD / Push 实现 |
| Modify `.../Public/CrowdUnitComponent.h` + `.cpp` | 薄派生；类型可从基类头 include |
| Modify `.../Public/CavalryUnitComponent.h` + `.cpp` | 薄派生；删双 HISM 专用实现 |
| Modify `CrowdStatsComponent.*` | include 基类类型；HISM 用 `GetHISM(0)` 如需要 |
| Modify `CrowdLegionActor.*` | 组装层表 + 新 Setup；`GetHISM(0)` |
| Modify Docs（按需） | Setup 契约说明 |

---

### Task 1: 基类头文件与层表行 + 空壳 Setup

**Files:**
- Create: `Client/Plugins/Development/VgCrowdLegion/Source/VgCrowdLegion/Public/UnitHISMComponent.h`
- Create: `Client/Plugins/Development/VgCrowdLegion/Source/VgCrowdLegion/Private/UnitHISMComponent.cpp`
- Modify: `Client/Plugins/Development/VgCrowdLegion/Source/VgCrowdLegion/Public/CrowdUnitComponent.h`（临时仍保留旧结构，本 Task 末仅保证新文件进模块编译；完整迁移动 Task 2–3）

**Interfaces:**
- Produces:
  - `FUnitHISMLayerTableRow`（`StaticMesh`, `AnimMetaTable`）
  - `FUnitAnimationTableRow` / `FCrowdCellInstanceData`（从现 `CrowdUnitComponent.h` **原样搬迁**到本头，含 CustomData 常量）
  - `USTRUCT FUnitHISMLayerRuntime`：`HISMCmp`, `Animations`, `AnimNameToIndex`
  - `UUnitHISMComponent`：`SetupHISM(UDataTable*, float, int32)`、`GetLayerCount()`、`GetHISM(int32)`、`GetCellCount()` 等声明
  - `static UDataTable* CreateTransientLayerTable(UObject* Outer, const TArray<FUnitHISMLayerTableRow>& Rows)`（供 Actor 组装）

- [ ] **Step 1: 写入 `UnitHISMComponent.h`**

将现有 `FUnitAnimationTableRow`、`FCrowdCellInstanceData`（及别名）从 `CrowdUnitComponent.h` 剪切到本文件。新增：

```cpp
USTRUCT(BlueprintType)
struct VGCROWDLEGION_API FUnitHISMLayerTableRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layer")
	TObjectPtr<UStaticMesh> StaticMesh;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layer")
	TObjectPtr<UDataTable> AnimMetaTable;
};

USTRUCT()
struct FUnitHISMLayerRuntime
{
	GENERATED_BODY()

	UPROPERTY()
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> HISMCmp;

	UPROPERTY()
	TArray<FUnitAnimationTableRow> Animations;

	UPROPERTY(Transient)
	TMap<FName, int32> AnimNameToIndex;
};

UCLASS(Abstract, ClassGroup = (Crowd))
class VGCROWDLEGION_API UUnitHISMComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	UUnitHISMComponent();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CrowdLegion | Setup")
	float CullDistance = 20000.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "CrowdLegion | Setup")
	TObjectPtr<UDataTable> LayerSetupTable;

	UFUNCTION(BlueprintCallable, Category = "CrowdLegion | Setup")
	bool SetupHISM(UDataTable* LayerTable, float InCullDistance, int32 PreallocateCount = 0);

	UFUNCTION(BlueprintPure, Category = "CrowdLegion | HISM")
	int32 GetLayerCount() const { return Layers.Num(); }

	UFUNCTION(BlueprintPure, Category = "CrowdLegion | HISM")
	UHierarchicalInstancedStaticMeshComponent* GetHISM(int32 LayerIndex) const;

	// Cell / Anim API 声明与现 Crowd 一致（实现放 Task 2）
	// ...

	static UDataTable* CreateTransientLayerTable(UObject* Outer, const TArray<FUnitHISMLayerTableRow>& Rows);

protected:
	void DestroyLayers();
	void ParseAnimTable(UDataTable* Table, FUnitHISMLayerRuntime& OutLayer);
	void PushCellCustomData(int32 Index, bool bMarkRenderStateDirty = true);
	static float GetAnimClock(const UWorld* World);

	UPROPERTY()
	TArray<FUnitHISMLayerRuntime> Layers;

	UPROPERTY()
	TArray<FCrowdCellInstanceData> CellsData;
};
```

- [ ] **Step 2: 实现 `SetupHISM` / `DestroyLayers` / `CreateTransientLayerTable` / `ParseAnimTable` / `GetHISM`**

要点（对齐 spec）：

1. `DestroyLayers`：ClearInstances、DestroyComponent 各 HISM，`Layers.Empty()`，`CellsData.Empty()`。
2. `SetupHISM`：校验 `LayerTable`；`GetRowMap()` 或 `GetAllRows<FUnitHISMLayerTableRow>`；无有效 Mesh 行则 `return false`。
3. 每有效行：`NewObject<UHISM>(Owner, *FString::Printf(TEXT("UnitHISM_%d"), Idx))`，Register、Attach、SetStaticMesh、从 Mesh 拷材质、`SetNumCustomDataFloats(4)`、`SetCullDistances(0, InCullDistance)`、PreAllocate。
4. `ParseAnimTable` 逻辑复制现 `LoadAnimationsFromDataTable` / Cavalry `ParseDataTable`。
5. `CreateTransientLayerTable`：`NewObject<UDataTable>(Outer)`，`RowStruct = FUnitHISMLayerTableRow::StaticStruct()`，逐行 `AddRow(FName(*FString::Printf(TEXT("Layer_%d"), i)), Row)`。

- [ ] **Step 3: 编译 Runtime 模块**

Run（在工程习惯路径下）:

```bat
ubt_build.bat
```

或等价 UBT 编 `ClientEditor` Development。  
Expected: 新文件链入后可通过；若 `CrowdUnitComponent.h` 仍完整，允许暂时双份结构体 **仅本 Task 中间态**——更干净做法是 Step 1 后立刻让 `CrowdUnitComponent.h` `#include "UnitHISMComponent.h"` 并删除重复结构体定义，旧类暂时仍 `UActorComponent` 派生（Task 3 再改继承）。

**推荐本 Task 内完成结构体去重：** `CrowdUnitComponent.h` include 基类头并删除重复 USTRUCT，避免 UHT 重复注册。

- [ ] **Step 4: Commit**

```bash
git add Client/Plugins/Development/VgCrowdLegion/Source/VgCrowdLegion/Public/UnitHISMComponent.h
git add Client/Plugins/Development/VgCrowdLegion/Source/VgCrowdLegion/Private/UnitHISMComponent.cpp
git add Client/Plugins/Development/VgCrowdLegion/Source/VgCrowdLegion/Public/CrowdUnitComponent.h
git commit -m "feat(CrowdLegion): 新增 UUnitHISMComponent 与层表 SetupHISM"
```

---

### Task 2: 基类实现 Cell CRUD + 统一相位 Push

**Files:**
- Modify: `UnitHISMComponent.h` / `UnitHISMComponent.cpp`
- 参考实现来源: 现 `CrowdUnitComponent.cpp`、`CavalryUnitComponent.cpp`（多 HISM 循环）

**Interfaces:**
- Consumes: Task 1 的 `Layers` / `SetupHISM`
- Produces: 完整 Cell/Anim API（签名与现 Crowd 公开接口一致）

- [ ] **Step 1: 声明并实现下列 API（全部对所有层同步）**

```cpp
int32 AddCell(const FTransform& Transform, int32 AnimIndex = 0, float PlayRate = 1.f, bool bLoop = true, float StartTimeOffset = 0.f);
void BatchAddCells(const TArray<FTransform>& Transforms, int32 AnimIndex = 0, float PlayRate = 1.f, bool bLoop = true);
bool RemoveCell(int32 CellIndex); // 各层 RemoveInstance + CellsData.RemoveAtSwap
void ClearCells();
bool GetCellData(int32 CellIndex, FCrowdCellInstanceData& OutData) const;
bool UpdateCellTransform(int32 CellIndex, const FTransform& NewTransform, bool bWorldSpace = true);
void BatchMoveCells(const FVector& MovementDelta, const TArray<int32>& IgnoredIndices, bool bFaceMovementDirection = true, bool bTeleport = false);
void SetCellAnim(int32 CellIndex, int32 NewAnimIndex, float InPlayRate = 1.f, bool bInLoop = true);
void SetCellAnimByName(int32 CellIndex, FName AnimName, float InPlayRate = 1.f, bool bInLoop = true);
void SetAllCellsAnim(int32 NewAnimIndex, float InPlayRate = 1.f, bool bInLoop = true);
void SetAllCellsAnimByName(FName AnimName, float InPlayRate = 1.f, bool bInLoop = true);
int32 FindAnimationIndex(FName AnimName) const; // 仅 Layers[0]
bool IsLoopingAnimation(int32 AnimIndex) const; // 仅 Layers[0]
const TArray<FCrowdCellInstanceData>& GetAllInstanceData() const;
```

`AddCell`：**禁止**再懒调用旧 Mesh Setup；若 `Layers.Num()==0` 则 Warning 并 `return INDEX_NONE`。

对每一层 `AddInstance` / `AddInstances`；失败则回滚已加层实例（或 ClearCells）并返回失败——优先实现：先确认所有 `HISMCmp` 非空，再统一添加。

- [ ] **Step 2: 实现 `PushCellCustomData`**

```cpp
void UUnitHISMComponent::PushCellCustomData(int32 Index, bool bMarkRenderStateDirty)
{
	if (!CellsData.IsValidIndex(Index)) return;
	const FCrowdCellInstanceData& D = CellsData[Index];
	const float SafeRate = FMath::Max(0.0001f, FMath::Abs(D.PlayRate));
	const float SignedRate = D.bLoop ? SafeRate : -SafeRate;

	for (int32 L = 0; L < Layers.Num(); ++L)
	{
		FUnitHISMLayerRuntime& Layer = Layers[L];
		if (!Layer.HISMCmp) continue;
		if (!Layer.Animations.IsValidIndex(D.AnimIndex))
		{
			UE_LOG(LogTemp, Warning, TEXT("[UnitHISM] AnimIndex %d OOB on layer %d"), D.AnimIndex, L);
			continue;
		}
		float StartFrame = float(Layer.Animations[D.AnimIndex].StartFrame);
		float EndFrame = float(Layer.Animations[D.AnimIndex].EndFrame);
		if (EndFrame < StartFrame) EndFrame = StartFrame;

		float CustomData[4];
		CustomData[0] = D.Phase;
		CustomData[1] = SignedRate;
		CustomData[2] = StartFrame;
		CustomData[3] = EndFrame;
		Layer.HISMCmp->SetCustomData(Index, MakeArrayView(CustomData), bMarkRenderStateDirty && L == Layers.Num() - 1);
	}
}
```

相位规则与现逻辑一致：loop → `FRand()*10`；one-shot → `-GetAnimClock(World)`。

- [ ] **Step 3: 编译**

Expected: `UnitHISMComponent.cpp` 无链接错误。

- [ ] **Step 4: Commit**

```bash
git commit -m "feat(CrowdLegion): 基类实现多 HISM Cell CRUD 与统一相位推送"
```

---

### Task 3: Crowd / Cavalry 改为薄派生并删除旧实现

**Files:**
- Modify: `CrowdUnitComponent.h` / `CrowdUnitComponent.cpp`
- Modify: `CavalryUnitComponent.h` / `CavalryUnitComponent.cpp`

**Interfaces:**
- Consumes: `UUnitHISMComponent` 全部 API
- Produces: 可 `CreateDefaultSubobject<UCrowdUnitComponent>` / `UCavalryUnitComponent` 的空壳类

- [ ] **Step 1: 重写 `CrowdUnitComponent.h`**

```cpp
#pragma once
#include "UnitHISMComponent.h"
#include "CrowdUnitComponent.generated.h"

UCLASS(ClassGroup = (Crowd), meta = (BlueprintSpawnableComponent))
class VGCROWDLEGION_API UCrowdUnitComponent : public UUnitHISMComponent
{
	GENERATED_BODY()
public:
	UCrowdUnitComponent();

	/** 兼容旧调用：等同 GetHISM(0) */
	UFUNCTION(BlueprintPure, Category = "CrowdLegion | HISM")
	UHierarchicalInstancedStaticMeshComponent* GetHISMCmp() const { return GetHISM(0); }
};

using UCrowdUnitManager = UCrowdUnitComponent;
```

- [ ] **Step 2: `CrowdUnitComponent.cpp` 仅构造函数**

```cpp
#include "CrowdUnitComponent.h"
UCrowdUnitComponent::UCrowdUnitComponent() {}
```

删除全部旧 Setup/CRUD/Push 实现。

- [ ] **Step 3: 同样精简 `CavalryUnitComponent`**

```cpp
UCLASS(ClassGroup = (Crowd), meta = (BlueprintSpawnableComponent))
class VGCROWDLEGION_API UCavalryUnitComponent : public UUnitHISMComponent
{
	GENERATED_BODY()
public:
	UCavalryUnitComponent();
};
```

删除 `SetupCavalryHISM`、`HorseHISMCmp`、`RiderHISMCmp`、双表字段与全部重复 CRUD。

- [ ] **Step 4: 编译 Editor 目标**

Expected: 此时 `CrowdLegionActor.cpp` 会因旧 API **编译失败**——若希望本 Task 绿，可与 Task 4 合并；否则本 Task 只保证 Runtime 头自洽，Actor 修改必须紧随。

**执行约定：** Task 3 + Task 4 在同一编译窗口完成（先改派生类，立刻改 Actor），再共同 commit，或分两个 commit 但中间不推送损坏状态。

- [ ] **Step 5: Commit（若 Actor 已同步）或暂存到 Task 4 一并提交**

---

### Task 4: 迁移 `CrowdLegionActor` 与 Stats

**Files:**
- Modify: `.../VgCrowdLegionEditor/Public/CrowdLegionActor.h`
- Modify: `.../VgCrowdLegionEditor/Private/CrowdLegionActor.cpp`
- Modify: `.../VgCrowdLegion/Private/CrowdStatsComponent.cpp`（若引用 `GetHISMCmp` 以外的已删字段）

**Interfaces:**
- Consumes: `SetupHISM`、`CreateTransientLayerTable`、`FUnitHISMLayerTableRow`

- [ ] **Step 1: Actor 增加辅助组装层表**

在 `CrowdLegionActor.cpp` 匿名命名空间或 private 方法：

```cpp
static UDataTable* MakeLayers(UObject* Outer, UStaticMesh* Mesh0, UDataTable* Anim0, UStaticMesh* Mesh1 = nullptr, UDataTable* Anim1 = nullptr)
{
	TArray<FUnitHISMLayerTableRow> Rows;
	if (Mesh0)
	{
		FUnitHISMLayerTableRow R; R.StaticMesh = Mesh0; R.AnimMetaTable = Anim0; Rows.Add(R);
	}
	if (Mesh1)
	{
		FUnitHISMLayerTableRow R; R.StaticMesh = Mesh1; R.AnimMetaTable = Anim1; Rows.Add(R);
	}
	return UUnitHISMComponent::CreateTransientLayerTable(Outer, Rows);
}
```

- [ ] **Step 2: 改 `SpawnLegion`**

解析 Mesh（现有 DataAsset 逻辑可保留在 Actor 局部变量，**不再写** `CrowdComponent->Mesh`）：

```cpp
UStaticMesh* Mesh = /* 从 Crowd 旧字段迁移：改为 Actor 自有 Mesh/DataAsset */;
UDataTable* AnimMeta = /* 从 DataAsset 路径加载 DT_*_AnimMeta 或 Actor 上配置 */;
UDataTable* LayerDT = MakeLayers(this, Mesh, AnimMeta);
CrowdComponent->SetupHISM(LayerDT, CrowdComponent->CullDistance, NumUnits);
CrowdComponent->ClearCells();
// BatchAddCells 不变
```

**细节面板：** 若组件上已删除 `Mesh`/`AnimMetaDataTable`，在 **Actor** 上保留（或新增）`InfantryMesh` / `InfantryAnimMetaTable` 供配置；导出流程同样写 Actor 字段或直接 `MakeLayers`。

最小迁移：Actor 继续用现有 `DataAsset` 推 Mesh+AnimMeta，组件不再存 Mesh。

- [ ] **Step 3: 改一键导出里绑定组件的片段**（约 `CrowdLegionActor.cpp` 117–136 行）

删除对 `CrowdComponent->Mesh/Materials/AnimMetaDataTable/LoadAnimationsFromDataTable/SetupHISM(Mesh,SlotMats)` 的写入；改为 `MakeLayers` + `SetupHISM(LayerDT, CullDistance, 0)`。

- [ ] **Step 4: 改 `SpawnCavalryLegionCustom`**

用已解析的 `FinalHorseMesh/Anim`、`FinalRiderMesh/Anim`：

```cpp
UDataTable* LayerDT = MakeLayers(this, FinalHorseMesh, FinalHorseAnimTable, FinalRiderMesh, FinalRiderAnimTable);
CavalryComponent->SetupHISM(LayerDT, CavalryComponent->CullDistance, InNumUnits);
```

删除 `SetupCavalryHISM(...)` 与材质数组拼装（材质由 Setup 从 Mesh 拷）。

- [ ] **Step 5: `GetHISMCmp()` 调用点**

`TargetHISM = CrowdComponent->GetHISMCmp()` 可保留（Task 3 兼容包装）或改为 `GetHISM(0)`。

- [ ] **Step 6: 全量编译 ClientEditor**

Expected: **PASS**，无旧 Setup 符号。

- [ ] **Step 7: Commit**

```bash
git commit -m "refactor(CrowdLegion): Crowd/Cavalry 薄派生并迁移 Actor 到层表 Setup"
```

---

### Task 5: 文档与验收

**Files:**
- Modify: `Docs/AnimToTexture使用文档.md`（若仍写旧 Setup）
- Modify: `Docs/Cursor-UE5-Blueprint开发方法论.md` 无需改
- Optional: 在 spec 下追加「实现完成」备注

- [ ] **Step 1: 全文搜索旧 API**

```bat
rg "SetupCavalryHISM|SetupHISM\(.*Mesh|AnimMetaDataTable|HorseHISMCmp" Client/Plugins/Development/VgCrowdLegion Docs
```

Expected: 无 C++ 旧签名；Docs 已更新或仅历史叙述。

- [ ] **Step 2: 编辑器手工验收清单**

1. 打开含 `ACrowdLegionActor` 的关卡。  
2. `SpawnLegion`：单层 HISM，单元可见，Idle 动画 CustomData 正常。  
3. `SpawnCavalryLegion`：两层实例数相等，位移一致，切换动画后两层相位/速率一致、帧范围可不同。  
4. 杀伤/移除 Cell 后两层实例数仍对齐。

- [ ] **Step 3: Commit 文档**

```bash
git commit -m "docs: 更新 CrowdLegion SetupHISM 层表契约说明"
```

---

## Spec coverage self-check

| Spec 项 | Task |
|---------|------|
| `FUnitHISMLayerTableRow` + Setup 三参数 | Task 1 |
| 材质从 Mesh 拷 | Task 1 |
| Cell CRUD 在基类 | Task 2 |
| 统一相位 Push，AnimIndex 对齐 | Task 2 |
| Crowd/Cavalry 薄派生 | Task 3 |
| 删除旧 Setup，迁移 Actor | Task 4 |
| 验收 1 行 / N 行 | Task 5 |
| 不改 CustomData 语义 | 全程 |

## Placeholder scan

无 TBD；编译命令以仓库 `ubt_build.bat` 为准。
