# A2T 军团一键导出：多插槽 BoneAnimation 与 EndFrame 设计

**日期：** 2026-11-08  
**状态：** 已批准  
**范围：** VgCrowdLegion 一键导出与烘焙：去掉 Atlas/插槽合并；每槽生成并套用 BoneAnimation 材质；动画表 `NumFrames`→`EndFrame` 且保留已有行的 `bDefaultLoop`

## 目标

1. 导出 Static Mesh **不再合并材质插槽**，取消 Atlas 贴图与 UV 象限重映射  
2. 各插槽材质按 **Archer BoneAnimation** 方式生成 MI，并用生成结果 **替换对应插槽**  
3. 动画元数据表：字段 `NumFrames` 改为 **`EndFrame`（绝对结束帧，含）**；表已存在且按 AnimName 命中的行 **不修改 `bDefaultLoop`**

## 非目标

- 不改官方 AnimToTexture 插件源码  
- 不强制迁移磁盘上旧 Atlas 资产（可遗留；新导出不再生成）  
- 不在本规格内重做 GetCrowdLegionFrame / MF 重命名（沿用已有）

## Static Mesh 导出

**现状：** `ExportStaticMeshWithAtlasUVRemap` 将 UV 打进 2×2 象限，并把所有 Section 材质索引压成 0、`StaticMaterials` 只留 1 个。

**目标行为：**

1. `UAnimToTextureBPLibrary::ConvertSkeletalMeshToStaticMesh`（或等价）得到 SM  
2. **保留** 原插槽数量与 Section→Material 映射  
3. **不**调用 Atlas 生成，**不**做 UV 象限缩放/偏移  

API：新增或改写为 `ExportStaticMeshFromSkeletalMesh`；旧 Atlas 路径从一键管线移除（函数可留作未使用/废弃）。

## 每槽 BoneAnimation 材质

**父材质：** 与 Archer 一致的图——`BaseColorTexture` → `MakeMaterialAttributes` → `MF_CrowdLegionBoneAnimation`；Frame = `GetCrowdLegionFrame`（PICD + SampleRate）。可按角色包路径生成 `M_<Character>_BoneAnimation`，或复用已有 `M_Archer_BoneAnimation` 当角色为 Archer。

**MakeMaterialAttributes 默认表面输入（`EnsureBoneAnimationParentMaterial` 显式 Constant 接入）：** Metallic `0.0`、Specular `0.5`、Roughness `0.5`、Normal `(0,0,1)`、Tangent `(1,0,0)`、Ambient Occlusion `1.0`。

**每槽 MI：**

1. 从该槽原始材质（Skel 导出前或 SM 转换后槽上材质）解析 BaseColor / 主贴图  
2. 创建或更新 `MI_<Name>_BoneAnimation_<SlotIndex>`（或稳定后缀）  
3. Parent = 上述父材质；`SetTextureParameterValueEditorOnly(BaseColorTexture, ...)`  
4. `BakedStaticMesh->SetMaterial(SlotIndex, MaterialInstance)`  

**烘焙：** `BakeA2TDataAsset` 去掉对 Atlas 的依赖；VAT 相关参数仍通过现有 `UpdateMaterialInstanceFromDataAsset`（或等价）写到父材质/各 MI——以实现时官方 API 能更新的对象为准，保证运行时 BoneAnimation 能读到动画贴图。

## 动画 DataTable

**行结构 `FUnitAnimationTableRow`：**

| 字段 | 变更 |
|------|------|
| `AnimName` | 不变 |
| `StartFrame` | 不变 |
| `NumFrames` | **删除**，改为 `EndFrame`（绝对结束帧，含） |
| `bDefaultLoop` | 语义不变 |

**生成 `GenerateAnimMetaDataTable`：**

- `FrameCount` 算法不变（自定义范围或 `GetNumberOfSampledKeys`）  
- `StartFrame` = 累计起始；`EndFrame = StartFrame + FrameCount - 1`  
- 若目标 DataTable **已存在**：按 `AnimName` 查找旧行；命中则 **拷贝旧 `bDefaultLoop`**，只更新帧字段；未命中则名字启发式设 Loop  
- 若表不存在：全部行用启发式  

**运行时：**

- `PushCellCustomData`：`CD_EndFrame` = 行的 `EndFrame`（不再 `Start + NumFrames - 1`）  
- Stats 帧长：`max(1, EndFrame - StartFrame + 1)`  

## 一键管线顺序

`ExecuteExportFromSelectedAsset` / `ExportAndBakeA2TLegion`：

1. `EnsurePluginMasterMaterials`（及 GetCrowdLegionFrame / MF）  
2. （删除）Atlas 生成  
3. 导出多插槽 SM  
4. 确保父材质 + 每槽 MI，并 `SetMaterial`  
5. `BakeA2TDataAsset`（无 Atlas 参数）  
6. `GenerateAnimMetaDataTable`（EndFrame + 保留 Loop）  

## 验收

- 新导出 SM 插槽数与源 Skel 一致（或与转换结果一致），不为 1  
- 各槽材质为 BoneAnimation MI，BaseColor 来自原槽  
- 无新 Atlas 资产写出；UV 非象限缩放布局  
- DataTable 行为字段为 `EndFrame`；重导时已有 AnimName 的 `bDefaultLoop` 不变  
- HISM CustomData 仍为 `[TimeOffset, PlayRate, StartFrame, EndFrame]`，播放正确  
