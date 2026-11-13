# BatchMoveCells：CPU Transform 缓存优化

**日期：** 2026-11-13  
**状态：** 已批准  
**范围：** `UUnitHISMComponent` 维护 `CellTransforms`；`BatchMoveCells` 原地运算后批量同步 GPU；去掉 `IgnoredIndices`；更新 `CrowdLegionActor` 调用方

## 背景

当前 `BatchMoveCells` 每帧对 Layer0 做 `GetInstanceTransform`，用 `IgnoredIndices` 跳过死亡单位，再 `BatchUpdateInstancesTransforms` 写回各层。反复从 HISM 读变换成本高，且忽略列表与「移动」职责耦合。

## 目标

1. 组件内保存与 Cell 1:1 的世界空间 `TArray<FTransform> CellTransforms`。
2. 每次移动直接在 CPU 数组上运算，不再 `GetInstanceTransform`。
3. 运算结果经 `BatchUpdateInstancesTransforms` 批量同步到所有 HISM 层。
4. 去掉 `IgnoredIndices`；死亡单位由调用方先 `RemoveCell`，再整队移动。
5. `CellTransforms` 作为 Transform 的唯一 CPU 真相源：所有 CRUD / Update / Move 路径读写该数组后再同步 GPU。

## 非目标

- 不改 CustomData 4 槽语义或动画 API。
- 不引入按 AnimIndex / 掩码过滤移动。
- 不做脏区或部分实例上传；Move 始终整表批量同步。
- 不改 Bake / Exporter / 材质。

## 已确认决策

| 项 | 选择 |
|----|------|
| Transform 存储 | 并行 `TArray<FTransform> CellTransforms`（不嵌入 `FCrowdCellInstanceData`） |
| 空间 | 世界空间 |
| 真相源 | CPU `CellTransforms`；各层 HISM 为 GPU 镜像 |
| 忽略列表 | 删除；调用方先 `RemoveCell` 再 Move |
| 批量策略 | 全表 `BatchUpdateInstancesTransforms(0, CellTransforms, …)` |

## 架构

```text
UUnitHISMComponent
  ├── CellsData[]       // 动画逻辑状态
  ├── CellTransforms[]  // 世界空间 Transform（与 CellsData 下标 1:1）
  └── Layers[].HISMCmp  // GPU 实例镜像
```

**不变式：** `CellTransforms.Num() == CellsData.Num() == 每层 InstanceCount`。

### API 变更

```cpp
// 旧
void BatchMoveCells(const FVector& MovementDelta, const TArray<int32>& IgnoredIndices,
                    bool bFaceMovementDirection = true, bool bTeleport = false);

// 新
void BatchMoveCells(const FVector& MovementDelta,
                    bool bFaceMovementDirection = true, bool bTeleport = false);
```

可选只读访问（实现时按需）：`GetCellTransform(int32)` / `GetAllCellTransforms()`，便于 Stats / Actor 读中心点而不经 HISM。

### CRUD / Move 数据流

| API | CPU | GPU |
|-----|-----|-----|
| `AddCell` / `BatchAddCells` | 追加 `CellTransforms` | 各层 `AddInstance(s)` |
| `RemoveCell` | `CellsData` + `CellTransforms` 同下标 `RemoveAtSwap` | 各层 `RemoveInstance` |
| `ClearCells` / `SetupHISM` 重置 | 清空 `CellTransforms` | 清空实例 |
| `UpdateCellTransform` | 写 `CellTransforms[i]`（`bWorldSpace==false` 时先转到世界空间） | 各层 `UpdateInstanceTransform` |
| `BatchMoveCells` | 全员：`AddToTranslation(Delta)`；可选统一朝向 | 各层 `BatchUpdateInstancesTransforms(0, CellTransforms, true, true, bTeleport)` |

`BatchMoveCells` 伪代码：

```text
if !AreLayersReady || CellTransforms.Num()==0 → return
if bFaceMovementDirection:
  TargetRot = MovementDelta.GetSafeNormal2D().Rotation()  // 2D 近零则跳过朝向
for each T in CellTransforms:
  T.AddToTranslation(MovementDelta)
  if facing applicable: T.SetRotation(TargetRot)
for each Layer: BatchUpdateInstancesTransforms(0, CellTransforms, true, true, bTeleport)
```

### 调用方：`CrowdLegionActor::Tick`

1. 收集 `AnimIndex == DeathAnimIndex` 的下标。
2. **从高到低** `RemoveCell`（避免 swap 打乱未处理下标）。
3. 调用无 `IgnoredIndices` 的 `BatchMoveCells(MovementDelta, true, false)`。

### 边界

- `MovementDelta` 近零且不改朝向：可早退（可选）。
- `bFaceMovementDirection` 且 Delta 2D 近零：只平移，不改旋转。

## 文档同步

- 更新 `Docs/武器和骑兵方案.md` 中仍写 `IgnoredIndices` 的 `BatchMoveCells` 签名说明。
- 基类设计文档若提及忽略列表，改为指向本 spec 的「先 Remove 再 Move」。

## 验收

1. 行军路径无 `GetInstanceTransform`；死亡单位先移除再整队移动。
2. 多 HISM 层同 Cell Transform 一致。
3. Add / RemoveSwap / Clear / Update / Move 后 `CellTransforms.Num()` 与 `CellsData`、各层实例数一致。
4. 工程内无 `BatchMoveCells(..., IgnoredIndices, ...)` 残留调用。

## 实现注意

- 改动文件：`UnitHISMComponent.h/.cpp`、`CrowdLegionActor.cpp`；相关 Docs。
- 破坏性蓝图签名变更：去掉 `IgnoredIndices` 参数；编辑器内旧节点需重链。
- `RemoveCell` 必须两边同时 `RemoveAtSwap`，保持下标对齐。
