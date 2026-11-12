# Unit HISM 基类抽取设计

**日期：** 2026-11-13  
**状态：** 已批准  
**范围：** `UCrowdUnitComponent` / `UCavalryUnitComponent` 提取公共基类；统一 `SetupHISM`；Cell CRUD 上移；多 HISM 数据驱动与统一相位推送

## 背景

当前 `UCrowdUnitComponent`（单 HISM）与 `UCavalryUnitComponent`（马+骑手双 HISM）并行实现 Cell 增删改查、Transform 同步与 PerInstanceCustomData 推送，逻辑大量重复且 Setup API 不一致。

## 目标

1. 抽取抽象基类，按 **DataTable 多行** 管理 **N 个 HISM 层**。
2. 统一 `SetupHISM(UDataTable* LayerTable, float CullDistance, int32 PreallocateCount)`。
3. Cell 增删改查与动画切换集中在基类；每层用同一逻辑状态推 GPU。
4. 多 HISM 下同一 Cell：**变换一致**；**Phase / PlayRate / bLoop / AnimIndex 一致**；仅各层 `StartFrame`/`EndFrame` 来自各自动画表的同一 `AnimIndex` 行。
5. 保留 `UCrowdUnitComponent` / `UCavalryUnitComponent` 类型名（薄派生）。

## 非目标

- 不修改 CustomData 4 槽语义及 `GetCrowdLegionFrame` 材质约定。
- 不做跨层「按动作名」映射（动画对齐为 **行号 `AnimIndex`**）。
- 不删除 `UCavalryUnitComponent` 类。
- 不引入玩法状态机 / AI。
- 不改 Bake/Exporter，除非其直接调用已删除的旧 Setup API（若有则改为新契约）。

## 已确认决策

| 项 | 选择 |
|----|------|
| 层配置 | 一张表多行；每行 = 一层 HISM（`StaticMesh` + `AnimMetaTable`） |
| 材质 | Setup 时从该行 `StaticMesh` 槽拷贝 |
| 层次 | 基类完整承载 N HISM + CRUD + 推送；Crowd/Cavalry 薄派生 |
| 动画对齐 | 硬按 `AnimIndex` 下标取每层动画表第 N 行 |
| 旧 API | 破坏性删除旧 `SetupHISM` / `SetupCavalryHISM`，调用方一并改 |

## 架构（方案 1）

```text
UActorComponent
  └── UUnitHISMComponent (Abstract)
        ├── CellsData[]                    // 逻辑唯一真相
        ├── Layers[]                       // 每层: HISM + Animations(+可选 Name→Index)
        ├── SetupHISM(LayerTable, Cull, Prealloc)
        ├── Cell CRUD / Anim API / PushCellCustomData
        ├── UCrowdUnitComponent            // 薄派生（蓝图/Actor 类型名）
        └── UCavalryUnitComponent          // 薄派生
```

### 层配置表行

```cpp
// FUnitHISMLayerTableRow : FTableRowBase
TObjectPtr<UStaticMesh> StaticMesh;
TObjectPtr<UDataTable>  AnimMetaTable; // 行类型: FUnitAnimationTableRow
```

组件级参数：`CullDistance`、`PreallocateCount`（经 `SetupHISM` 传入并保存需要的字段）、`LayerSetupTable` 引用（便于回看）。

### SetupHISM 契约

```cpp
void SetupHISM(UDataTable* LayerTable, float InCullDistance, int32 PreallocateCount = 0);
```

1. `LayerTable` 行类型必须为 `FUnitHISMLayerTableRow`。
2. 无任何有效行（空表或全部缺 Mesh）→ Setup 失败并 Warning。
3. 单行缺 `StaticMesh` 或无法建 HISM → 跳过该行并 Warning；若最终 0 层则失败。
4. 行有 Mesh 但 `AnimMetaTable` 为空 → 仍创建该层 HISM，`Animations` 为空并 Warning（随后按索引切动画易越界，由 Push 路径 Warning）。
5. 按行序重建层：创建/配置 `UnitHISM_N`，Mesh、从 Mesh 拷材质、`NumCustomDataFloats = 4`、`SetCullDistances(0, InCullDistance)`、可选预分配。
6. 每层从 `AnimMetaTable` 填充 `Animations`（及 Layer0 的 `AnimNameToIndex`，供 `SetCellAnimByName`）。
7. Setup **重置**：清空 `CellsData` 与旧实例；旧公开 Setup 签名删除。

### Cell 与 GPU

- `CellsData[i]` 与每一层 `HISM` 实例下标 **1:1**。
- CRUD / `UpdateCellTransform` / `BatchMoveCells`：先更新逻辑数据，再对**所有层**做相同实例操作；`RemoveCell` 各层统一 **swap-with-last**。
- 任一 HISM 操作失败 → 整次 Cell 操作失败并 Log，避免层间实例数漂移。

**`PushCellCustomData(i)`（每层 L）：**

| 槽 | 值 |
|----|----|
| `[0]` | `CellsData[i].Phase`（全层相同） |
| `[1]` | Signed PlayRate（loop≥0 / one-shot&lt;0，与现约定一致；全层相同） |
| `[2]` | `Layers[L].Animations[AnimIndex].StartFrame` |
| `[3]` | `Layers[L].Animations[AnimIndex].EndFrame` |

`AnimIndex` 越界：该层跳过写入并 Warning。

**按名切动画：** 仅在 **Layer0** 查名→`AnimIndex`，再走索引路径；其它层只按下标取帧。

**`IsLoopingAnimation`：** 以 Layer0 对应行的 `bDefaultLoop` 为准（调用方传入的 `bLoop` 仍可覆盖写入 Cell）。

### 派生类与访问器

- Crowd / Cavalry：无额外业务逻辑；可 BlueprintSpawnable。
- 对外访问：`GetLayerCount()`、`GetHISM(int32 LayerIndex)`；不再依赖 Cavalry 上 `HorseHISMCmp`/`RiderHISMCmp` 独立字段（本次不提供废弃别名，除非后续明确要求）。

## 调用方迁移

| 位置 | 变更 |
|------|------|
| `CrowdLegionActor` | 步兵/骑兵改为提供层 DataTable 后调用 `SetupHISM` |
| `UCrowdStatsComponent` | 继续引用 `UCrowdUnitComponent`；逻辑 Cell API 走基类；若用单 HISM 则改为 `GetHISM(0)` |
| 相关 Docs | 更新 Setup 签名与层表说明 |

## 验收

1. 1 行层表：行为与现单 HISM 步兵等价。
2. N 行层表：同 Cell 变换与相位/速率一致；仅起止帧按层表 `AnimIndex` 行不同。
3. 工程内无旧 `SetupHISM(Mesh, Materials, …)` / `SetupCavalryHISM` 残留。
4. CustomData 仍为 4 浮点，与现材质约定兼容。

## 实现时注意

- 新文件建议：`UnitHISMComponent.h/.cpp`（或等价命名）；`FUnitHISMLayerTableRow` 可与现动画行同放或紧邻基类头文件。
- `UCrowdUnitComponent.h` 中已有 `FUnitAnimationTableRow` / `FCrowdCellInstanceData` 可上移到基类头或共享头，避免 Cavalry 再依赖「步兵组件头」造成循环。
- 模块 `VgCrowdLegion.Build.cs` 无需新依赖（仍用 Engine HISM + DataTable）。
