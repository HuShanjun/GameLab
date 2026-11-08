# Archer BoneAnimation 材质生成设计

**日期：** 2026-11-08  
**状态：** 已批准  
**范围：** 基于 Archer 三份颜色材质，生成简单版 BoneAnimation 父材质 + 三个示例材质实例

## 背景

`/Game/CrowdLegion/Characters/Archer/Materials/` 下现有：

- `M_Soldier_C_Color_Mat`
- `M_Soldier_C_Color_2_Mat`
- `M_Soldier_C_Color_4_Mat`

附近贴图：`T_Soldier_C_Color`、`T_Soldier_C_Color_2`、`T_Soldier_C_Color_4`。  
需要一套可用于 A2T 预览的 BoneAnimation 材质：父材质 = Texture Sample + `MF_BoneAnimation`；三个 MI 分别绑不同 BaseColor 贴图。Frame 驱动采用**简单版（方案 A）**，不接 PerInstanceCustomData 算帧。

## 目标

1. 生成父材质 `M_Archer_BoneAnimation`
2. 生成三个 Material Instance，分别使用三张 Soldier Color 贴图
3. 通过 Editor 菜单一键生成/更新，逻辑落在 `VgCrowdLegionEditor`（扩展 `CrowdLegionExporter` 或等价工具函数）

## 非目标

- 不接 HISM / `PackedRate` 自定义算帧（那是 `M_Crowd_Legion` 路径）
- 不自动烘焙 AnimToTextureDataAsset
- 不修改三份原始 `M_Soldier_C_Color*` 材质
- 不强制把插件 `Content/` 测图入库（本产出在 `/Game/CrowdLegion/...`，属项目 Content）

## 产出路径与命名

```
/Game/CrowdLegion/Characters/Archer/Materials/BoneAnimation/
├── M_Archer_BoneAnimation
├── MI_Archer_BoneAnimation_Color      → T_Soldier_C_Color
├── MI_Archer_BoneAnimation_Color_2    → T_Soldier_C_Color_2
└── MI_Archer_BoneAnimation_Color_4    → T_Soldier_C_Color_4
```

## 父材质规格

- **BaseColor：** `TextureSampleParameter2D`，参数名 `BaseColorTexture`（可被 MI 覆盖）
- **动画：** 调用 `/VgCrowdLegion/Materials/MF_BoneAnimation`（若不存在则按现有 Exporter 逻辑从官方模板复制）
- **Frame：** 标量参数 `Frame`（默认 0）接入 MF 的 Frame 输入；不建 PerInstanceCustomData 网络
- **输出：** BaseColor → 材质 BaseColor；MF 的位移/法线输出接到 WPO / Normal（按 MF 实际引脚连接）
- **标志：** `bUsedWithInstancedStaticMeshes = true`
- 不要求 Substrate 专用路径；与当前工程渲染设置兼容即可（优先非 Substrate 标准管线连接）

## 材质实例规格

| 资产名 | Parent | BaseColorTexture |
|--------|--------|------------------|
| `MI_Archer_BoneAnimation_Color` | `M_Archer_BoneAnimation` | `/Game/CrowdLegion/Characters/Archer/Textures/T_Soldier_C_Color` |
| `MI_Archer_BoneAnimation_Color_2` | 同上 | `.../T_Soldier_C_Color_2` |
| `MI_Archer_BoneAnimation_Color_4` | 同上 | `.../T_Soldier_C_Color_4` |

VAT 相关参数（BonePositionTexture 等）本次可不填；后续由 DataAsset `UpdateMaterialInstanceFromDataAsset` 更新。

## 编辑器入口

在现有 Tools →「海量军团 (VgCrowdLegion)」菜单增加一项，例如：

**生成 Archer BoneAnimation 材质**

行为：

1. 确保 `MF_BoneAnimation` 可用  
2. 创建或覆盖更新 `M_Archer_BoneAnimation`  
3. 创建或更新三个 MI 并设置贴图  
4. Save / 标记 dirty；弹窗或 Log 提示结果路径  

若目标目录或贴图缺失，失败并给出明确错误信息。

## 验收标准

1. 上述 4 个资产存在于指定 Content 路径  
2. 父材质含 `BaseColorTexture` 与 `MF_BoneAnimation` 调用，且 `Frame` 为标量参数  
3. 三个 MI 的 BaseColor 分别指向三张不同 Soldier Color 贴图  
4. 父材质启用 Instanced Static Meshes  
5. 菜单可重复执行（幂等更新，不另造重复资产）

## 风险

- 若引擎为 Substrate 默认，标准 BaseColor 连接可能需按 `M_Crowd_Legion` 现有分支做兼容；实现时复用 Exporter 中已有 Substrate 检测逻辑。
- `MF_BoneAnimation` 若缺少 Frame 输入，需先走现有 Exporter 的 Frame FunctionInput 补丁逻辑。
