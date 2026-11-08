# GetCrowdLegionFrame + MF 重命名与 CustomData 对齐设计

**日期：** 2026-11-08  
**状态：** 已批准  
**范围：** 新建 `GetCrowdLegionFrame` 材质函数；`MF_BoneAnimation` 重命名为 `MF_CrowdLegionBoneAnimation` 并接入该算帧；更新 `UCrowdUnitComponent` HISM CustomData；更新 `M_Crowd_Legion` / `M_Archer_BoneAnimation` 生成逻辑

## 目标

1. 提供与官方 `GetAutoPlayFrame` 参数对齐、支持循环/单次的材质函数 `GetCrowdLegionFrame`（**禁止 Custom HLSL 节点**）
2. 插件 `MF_BoneAnimation` → `MF_CrowdLegionBoneAnimation`，Frame 由 `GetCrowdLegionFrame` 驱动
3. `UCrowdUnitComponent` 写入的 PerInstanceCustomData 与上述 4 参数对齐
4. 父材质生成统一使用新 MF，去掉旧 Custom 算帧网络

## 非目标

- 不改官方 AnimToTexture 插件资产
- 不改 DataTable 行结构（仍用 StartFrame + NumFrames；组件内换算 EndFrame）
- 不在本规格内做完整 PIE 人群关卡验收（实现计划中做编译 + 资产生成验证）

## GetCrowdLegionFrame

**路径：** `/VgCrowdLegion/Materials/GetCrowdLegionFrame`

**函数输入：**

| 输入 | 默认 | 说明 |
|------|------|------|
| TimeOffset | 0 | 相位 / 单次起始补偿 |
| Playrate | 1 | 速率；**≥0 循环，&lt;0 单次定格** |
| StartFrame | 0 | 全局起始帧 |
| EndFrame | 1 | 全局结束帧（含） |
| SampleRate | 30 | 与官方一致 |
| Time | （可接内部 Time） | 全局时间 |

**算帧（材质表达式：Abs / Multiply / Add / Max / Floor / Fmod / Min / If）：**

```
RateAbs = Abs(Playrate)
bLoop   = Playrate >= 0
Elapsed = (Time + TimeOffset) * RateAbs * SampleRate
Length  = max(1, EndFrame - StartFrame + 1)
Offset  = bLoop ? Floor(Fmod(Elapsed, Length))
                : Min(Length - 1, Floor(Elapsed))
Frame   = StartFrame + Offset
```

**输出：** `Frame`（Scalar）

创建方式：Exporter 幂等生成（复制 `GetAutoPlayFrame` 再改图，或纯表达式搭建），不得使用 `UMaterialExpressionCustom`。

## MF_CrowdLegionBoneAnimation

- 由现有 `/VgCrowdLegion/Materials/MF_BoneAnimation` **重命名/迁移** 为 `/VgCrowdLegion/Materials/MF_CrowdLegionBoneAnimation`
- 保留骨骼 VAT 采样逻辑；**Frame** 输入改为内部调用 `GetCrowdLegionFrame`，或其外部由父材质传入已由 `GetCrowdLegionFrame` 算出的 Frame
- 推荐：父材质内 `GetCrowdLegionFrame` → `MF_CrowdLegionBoneAnimation.Frame`（MF 仍暴露 Frame 输入，便于预览）

若磁盘仍存在旧 `MF_BoneAnimation`，生成逻辑不再引用；可删除或保留为废弃副本（实现时优先 Rename/Duplicate 到新名并更新引用）。

## CustomData 布局（UCrowdUnitComponent）

| Index | 含义 |
|-------|------|
| 0 | TimeOffset：循环 = `[0,10)` 随机相位；单次 = `-Now`（使 `Time+TimeOffset` 从 0 起算） |
| 1 | PlayRate：`bLoop ? +Abs(Rate) : -Abs(Rate)` |
| 2 | StartFrame |
| 3 | EndFrame = StartFrame + NumFrames - 1 |

更新 `FCrowdCellInstanceData` 常量注释与 `PushCellCustomData`；`NumCustomDataFloats` 仍为 4。

同步检查 `CrowdStatsComponent` 等若硬编码旧布局则一并修改。

## 父材质生成

### M_Crowd_Legion

- 使用 `MF_CrowdLegionBoneAnimation` 替代 `MF_BoneAnimation`
- **删除** 4× PerInstanceCustomData + Time + SampleRate + Custom HLSL 算帧网络
- 改为：4× `PerInstanceCustomData` → `GetCrowdLegionFrame`（+ Time / SampleRate 输入）→ MF.Frame
- 表面着色逻辑可保留

### M_Archer_BoneAnimation

- 同样改用 `MF_CrowdLegionBoneAnimation`
- Frame 来自 `GetCrowdLegionFrame` + PICD（不再仅用标量 `Frame` 参数；可保留 Frame 标量仅作预览覆盖则需 Switch——本规格默认 **只用 PICD 路径**，与军团一致）
- 链路保持：`TextureSample → MakeMaterialAttributes → MF_CrowdLegionBoneAnimation → Material Attributes / Front Material`

## 验收

1. Content 存在 `GetCrowdLegionFrame`、`MF_CrowdLegionBoneAnimation`
2. 两函数与父材质图中无 `Custom` 算帧节点
3. `PushCellCustomData` 写入 TimeOffset/PlayRate/StartFrame/EndFrame
4. 重新生成 `M_Crowd_Legion` / `M_Archer_BoneAnimation` 后引用新 MF + GetCrowdLegionFrame
5. `ClientEditor` 编译通过

## 风险

- 重命名 MF 后旧引用断裂：生成逻辑须强制重建父材质表达式
- 单次播放依赖 `TimeOffset = -Now` 与材质 `Time` 同源（RealTime / IgnorePause 与组件一致）
