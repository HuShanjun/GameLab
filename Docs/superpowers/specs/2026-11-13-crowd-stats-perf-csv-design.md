# CrowdStats 性能 CSV 导出设计

**日期：** 2026-11-13  
**状态：** 已批准  
**范围：** `UCrowdStatsComponent` 按动画段采样 Stat Unit 并写 CSV；`UUnitHISMComponent` 切动画时显式 Notify

## 背景

`UCrowdStatsComponent` 目前仅提供 PIE HUD（人数 / Pose / 骨骼 / FPS）。性能测试需要可对比的表格：每次切换动作后，对该段持续时间内的 Stat Unit 做平均，并附带场景元数据。

## 目标

1. 每次 PIE/BeginPlay 新建带时间戳的 CSV：`Saved/CrowdLegion/PerfStats_YYYYMMDD_HHMMSS.csv`。
2. 列：动画、半径、人数、Pose 数、骨骼数，以及 Stat Unit smoothed 五件套。
3. 显式 `NotifyAnimSegment`：切换完成时开新段；**下一次** Notify（或 EndPlay）时写出上一段各字段的**算术平均**。
4. `SetAllCellsAnim` / `SetAllCellsAnimByName` 成功后自动 Notify，Actor 按钮无需另写。

## 非目标

- 不修改引擎 Stat Unit 实现。
- 不自动轮询 AnimIndex 检测切换。
- 不引入独立 PerfLogger 组件（本需求落在现有 Stats 组件上）。
- 不改 Bake / 材质 / Cell Transform 路径。

## 已确认决策

| 项 | 选择 |
|----|------|
| 切换检测 | 显式 `NotifyAnimSegment`（方案 B） |
| Stat Unit 字段 | smoothed：`Frame` / `Game` / `Draw`(Render) / `GPU` / `RHIT` |
| CSV 文件 | 每次 BeginPlay 新建时间戳文件（方案 B） |
| 半径 / 人数 | 人数=`GetCellCount()`；半径=Owner 若为 `ACrowdLegionActor` 则 `SpreadRadius`，否则 `-1` |
| 架构 | Stats 内采样 + Notify API；UnitHISM 成功切动画后 Find Stats 并 Notify |

## 架构

```text
SetAllCellsAnim / ByName
  └── FindComponentByClass<UCrowdStatsComponent>()->NotifyAnimSegment(AnimName)

UCrowdStatsComponent
  ├── BeginPlay → open CSV + header
  ├── Tick      → accumulate Stat Unit (if segment active)
  ├── Notify    → flush avg row for previous segment, start new
  └── EndPlay   → flush last segment, close file
```

### CSV 列

```text
Anim,Radius,Units,Poses,Bones,Frame_ms,Game_ms,Draw_ms,GPU_ms,RHIT_ms
```

| 列 | 含义 | 取值时机 |
|----|------|----------|
| Anim | 段动画名 | 开段时记录的 `FName` |
| Radius | 生成半径 | **结算瞬间**快照 |
| Units | Cell 数 | 结算瞬间 |
| Poses | `GetActivePoseCount()` | 结算瞬间 |
| Bones | `GetTotalEquivalentBones()` | 结算瞬间 |
| `*_ms` | 段内 Stat Unit smoothed 算术平均 | 段内每帧累加 ÷ 有效帧数 |

数值格式：半径/人数/Pose/骨骼为整数（半径失败为 `-1`）；时间列保留 2～3 位小数。

### Notify / 段生命周期

1. **首个** `NotifyAnimSegment(Name)`：记录 `CurrentAnim`，清零累加器，`bSegmentActive=true`，**不写行**。
2. **后续** Notify：若 `SampleCount>0`，写一行均值；否则 Warning 跳过写行。然后以新 Name 开段。
3. **EndPlay**：同「后续 Notify」的 flush，然后关文件。
4. 同名连续 Notify：仍结算再开新段（允许同动作重复测）。

### Tick 采样

```text
if !bEnablePerfCsv || !bSegmentActive → return
FStatUnitData* D = GameViewport->GetStatUnitData()
if !D → skip frame
SumFrame += D->FrameTime
SumGame  += D->GameThreadTime
SumDraw  += D->RenderThreadTime
SumGPU   += D->GPUFrameTime
SumRHIT  += D->RHITTime
SampleCount++
```

实现时可选：GameWorld BeginPlay 执行 `stat unit`，避免数据全 0（仅开发/非 Shipping）。

### UnitHISM 挂钩

在 `SetAllCellsAnim` / `SetAllCellsAnimByName` 完成 CustomData 推送后：

```cpp
if (AActor* Owner = GetOwner())
{
  if (UCrowdStatsComponent* Stats = Owner->FindComponentByClass<UCrowdStatsComponent>())
  {
    Stats->NotifyAnimSegment(ResolvedAnimName); // ByIndex 时从 Layer0 表取名，越界则 Anim_%d
  }
}
```

Runtime 模块已有 `CrowdStatsComponent`，`UnitHISMComponent` 可直接依赖同模块头文件，无新模块依赖。

### 配置

- `bool bEnablePerfCsv = true`：关闭则不建文件、不采样、Notify 早退。
- 现有 HUD 行为保留，与 CSV 并行。

## 验收

1. PIE 后 `Saved/CrowdLegion/` 下出现新时间戳 CSV，含表头。
2. Idle → Run → Attack：至少两行（首段在第二次切换时写出）；EndPlay 写出最后一段。
3. 行内 `Anim` 与切换目标一致；`Units`/`Poses`/`Bones`/`Radius` 合理；`*_ms` 为段内平均且 >0（在 `stat unit` 可用时）。
4. `bEnablePerfCsv=false` 无新文件。

## 实现注意

- 改动文件：`CrowdStatsComponent.h/.cpp`、`UnitHISMComponent.cpp`（及必要时 `.h`）；Docs 本 spec。
- Editor Actor 无需改按钮逻辑（已走 `SetAllCellsAnim`）。
- CSV 用 `FFileHelper` / `FArchive` 追加写；路径用 `FPaths::ProjectSavedDir()`。
- 避免在 Shipping 强制依赖 Stat Unit；`bEnablePerfCsv` 默认可按 Build 裁剪，本 spec 默认 Editor/Dev 可用即可。
