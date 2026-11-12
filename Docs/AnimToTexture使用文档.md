# AnimToTexture（A2T）使用文档

**引擎：** Unreal Engine 5.8  
**插件路径：** `Engine/Plugins/Experimental/AnimToTexture`  
**内容挂载：** `/AnimToTexture/...`  
**相关文档：** [A2T材质蓝图基础逻辑分析.md](./A2T材质蓝图基础逻辑分析.md)

本文面向本仓库 Client 工程，说明如何用官方 AnimToTexture 把骨骼动画烘焙成 VAT（Vertex / Bone Animation Texture），并挂到静态网格 + 材质上，用 ISM/HISM 大规模播放。

---

## 1. 它是什么

AnimToTexture 把一帧帧骨骼（或顶点）动画写入贴图，运行时在 **GPU 材质**里采样贴图做变形，不再走 Skeletal Mesh 动画系统。

典型用途：人群、背景角色、大量重复单位。

| 模式 | 贴图内容 | 特点 |
|------|----------|------|
| **Bone（推荐）** | 骨骼位置 / 旋转 + 顶点蒙皮权重 | 同骨架可复用，精度与开销更均衡 |
| **Vertex** | 每顶点位置 / 法线 | 更直接，贴图更大 |

官方示例（Content Browser 打开 **Show Plugin Content**）：

| 资产 | 路径 |
|------|------|
| 烘焙工具蓝图 | `/AnimToTexture/Characters/Mannequin/BP_AnimToTexture` |
| Bone DataAsset | `/AnimToTexture/Characters/Mannequin/Data/DA_BoneAnimation` |
| Bone 示例网格 | `/AnimToTexture/Characters/Mannequin/SM_Mannequin_BoneAnimation` |
| Bone 示例材质 | `/AnimToTexture/Characters/Mannequin/Materials/BoneAnimation/...` |
| AutoPlay 实例 Actor | `/AnimToTexture/Characters/Mannequin/BP_InstancerAutoPlayData` |
| Frame 实例 Actor | `/AnimToTexture/Characters/Mannequin/BP_InstancerFrameData` |
| Bone Material Layer | `/AnimToTexture/Materials/ML_BoneAnimation` |

本项目已在 `Client.uproject` 中启用 `AnimToTexture`；军团扩展见 `Client/Plugins/Development/VgCrowdLegion`。

---

## 2. 整体流程（一览）

```
Skeletal Mesh + AnimSequences
        │
        ├─① 转 Static Mesh（保留 UV 槽写顶点索引）
        ├─② 创建空 VAT 贴图（BonePos / BoneRot / BoneWeight）
        ├─③ 配置 AnimToTextureDataAsset
        ├─④ Bake → 写入贴图 + 回填 DataAsset Info + 更新材质实例
        ├─⑤ 制作 / 绑定 A2T 材质（母材质 + MI + ML_BoneAnimation）
        └─⑥ ISM/HISM + CustomData 播放 / 切动画
```

---

## 3. 启用与准备

### 3.1 启用插件

1. `Edit → Plugins`，搜索 **AnimToTexture**，启用后重启编辑器。  
2. 或确认 `Client.uproject` 中有：

```json
{ "Name": "AnimToTexture", "Enabled": true }
```

### 3.2 Content Browser 设置

- 勾选 **Show Plugin Content**
- （可选）Show Engine Content

### 3.3 准备源资产

- 一份 **Skeletal Mesh**（如 Mannequin / 自有角色）
- 若干 **Anim Sequence**（Idle / Walk / Attack…），将烘焙进同一张 VAT（帧区间首尾相接）

建议在项目自己的目录操作（例如 `Content/Game/A2T/...`），不要直接改插件示例资产。

---

## 4. 具体操作步骤

下列步骤可用官方 `BP_AnimToTexture`（Editor Utility）辅助，也可手动配置 DataAsset 后调用 `UAnimToTextureBPLibrary::AnimationToTexture`。

### 步骤 1：Skeletal Mesh → Static Mesh

1. 打开 Skeletal Mesh。
2. 工具栏 **Make Static Mesh**，保存到项目目录（如 `SM_MyChar_A2T`）。
3. 检查 Static Mesh：
   - 预留 **非 Lightmap** 的 UV Channel（默认常用 **UV1**）给 A2T 写顶点索引。
   - 若报 `UV Channel already used by LightMap`：调整 Lightmap UV Index，或关闭冲突的 Lightmap UV 生成后再烘焙。
4. 材质槽：为静态网格准备 **独立的 Material Instance 副本**（不要与骨骼网格共用同一 MI，避免互相覆盖烘焙参数）。

也可用蓝图/C++：`UAnimToTextureBPLibrary::ConvertSkeletalMeshToStaticMesh`。

### 步骤 2：创建 VAT 贴图占位

Bone 模式需要三张 `Texture2D`（可先空占位，烘焙时写入）：

| 贴图 | 建议命名 | 用途 |
|------|----------|------|
| Bone Position | `TX_BonePosition` | 骨骼位置 |
| Bone Rotation | `TX_BoneRotation` | 骨骼旋转（轴 + 角） |
| Bone Weight | `TX_BoneWeight` | 顶点骨骼索引 / 权重 |

精度可选 8bit / 16bit（DataAsset 的 `Precision`）。分辨率由烘焙根据骨骼数 × 总帧数自动选择，受 `MaxWidth` / `MaxHeight` 限制。

### 步骤 3：创建并配置 `AnimToTextureDataAsset`

1. 内容浏览器：`Miscellaneous → Data Asset`，类选 **AnimToTextureDataAsset**。  
2. 或复制示例 `DA_BoneAnimation` 再改。

#### 必填 / 常用字段

| 分类 | 字段 | 说明 |
|------|------|------|
| SkeletalMesh | `SkeletalMesh` / `SkeletalLODIndex` | 源骨骼网格与 LOD |
| StaticMesh | `StaticMesh` / `StaticLODIndex` | 目标静态网格与 LOD |
| StaticMesh | `UVChannel` | 存顶点索引的 UV（默认 1；勿与 Lightmap UV 冲突） |
| Mapping | `NumDriverTriangles` / `Sigma` | 骨骼→静态顶点映射；越大越柔、越慢 |
| Texture | `Mode` | 选 **Bone** |
| Texture | `MaxWidth` / `MaxHeight` | 贴图上限（默认 4096） |
| Texture | `Precision` | EightBits / SixteenBits |
| Texture | `BonePosition/Rotation/WeightTexture` | 步骤 2 的三张贴图 |
| Animation | `SampleRate` | 采样率，默认 **30** |
| Animation | `AnimSequences[]` | 要烘焙的动画列表 |
| Animation | `bEnabled` / `bUseCustomRange` / `StartFrame`/`EndFrame` | 是否启用、是否截取子区间 |
| Material | `bAutoPlay` | 烘焙后写入 MI 的默认播放方式 |
| Material | `AnimationIndex` | AutoPlay 时默认播第几段 |
| Material | `NumBoneInfluences` | One / Two / Four |

`AnimSequences` 中每段启用的动画会按顺序拼进 VAT；烘焙后 `Info → Animations[]` 会生成每段的全局 `StartFrame` / `EndFrame`。

### 步骤 4：执行烘焙

任选其一：

- 打开 `BP_AnimToTexture`，按节点流程指向你的 DataAsset / MI，**Run Editor Utility Blueprint**  
- 或在 DataAsset 资产操作 / 蓝图中调用 **`Animation To Texture`**（`UAnimToTextureBPLibrary::AnimationToTexture`）

成功后应看到：

- 三张骨骼贴图有数据  
- DataAsset `Info`：`NumFrames`、`NumBones`、`BoneMinBBox` / `BoneSizeBBox`、`Animations[]` 已填充  
- 若调用了 `UpdateMaterialInstanceFromDataAsset`，对应 MI 参数已同步  

### 步骤 5：把材质绑到 Static Mesh

将更新后的 `MI_*_BoneAnimation` 赋给静态网格对应 Material Slot，保存。可用示例网格 `SM_Mannequin_BoneAnimation` 对照槽位。

### 步骤 6：场景中播放

见第 6 节。可先拖入示例 `BP_InstancerAutoPlayData` / `BP_InstancerFrameData` 验证。

---

## 5. 材质如何制作

A2T 材质分三层：**表面着色母材质**、**Material Layer（动画变形）**、**Material Instance（烘焙参数）**。

### 5.1 资产关系

```
Skeletal/Static Mesh 材质槽
        │
        ▼
MI_Body_BoneAnimation  ──parent──►  M_Body_BoneAnimation（母材质）
        │                              │
        │ Layer Parameters             │ Material Attribute Layers
        ▼                              ▼
   贴图 / 帧参数 / 开关           ML_BoneAnimation（官方层）
                                       │
                                       ▼
                              MF_BoneAnimation / BlendBonePositionAndNormal
```

官方参考：

- 母材质：`/AnimToTexture/Characters/Mannequin/Materials/BoneAnimation/M_Body_BoneAnimation`
- 身体 MI：`.../MI_Body_BoneAnimation`
- Logo MI：`.../MI_ChestLog_BoneAnimation`
- Layer：`/AnimToTexture/Materials/ML_BoneAnimation`

### 5.2 从零改一份已有角色材质（Bone 模式）

#### A. 复制资产

1. 复制角色母材质 → `M_MyChar_BoneAnimation`  
2. 复制各 Slot 的 MI → `MI_MyChar_Body_A2T` 等，Parent 指到新母材质  
3. Static Mesh 槽位改为这些新 MI  

#### B. 母材质打开「材质属性」工作流

1. 勾选 **Use Material Attributes**  
2. 用 `MakeMaterialAttributes` 接好原有 BaseColor / Metallic / Roughness / Normal 等  
3. 勾选 **Used with Instanced Static Meshes**（要上 ISM/HISM 必须开）  

#### C. 接入动画层

在最终输出前加入：

1. **Material Attribute Layers**  
   - Background / Layer 使用 **`ML_BoneAnimation`**（或复制后的项目内 Layer）  
2. 用 `GetMaterialAttributes` / `SetMaterialAttributes`（及必要时 `BlendAngleCorrectedNormals`）把层输出的 **World Position Offset**、**Normal** 合回表面属性  
3. 接到最终 Material Attributes 输出  

对照官方 `M_Body_BoneAnimation` 节点布局最稳妥。

也可把动画逻辑做成 **Material Function** 调用（项目 `VgCrowdLegion` 的 `MF_BoneAnimation` + `M_Crowd_Legion` 即此思路，并扩展了自定义算帧）。

#### D. 材质实例 Layer Parameters

打开 MI → **Layer Parameters**（菜单 Window → Layer Parameters）：

烘焙前后关注：

| 参数 | 说明 |
|------|------|
| `BonePositionTexture` / `BoneRotationTexture` / `BoneWeightsTexture` | 三张 VAT |
| `MinBBox` / `SizeBBox` | 位置解包 |
| `NumBones` / `RowsPerFrame` / `BoneWeightsRowsPerFrame` | 布局 |
| `NumFrames` / `SampleRate` | 总帧与采样率 |
| `AutoPlay` | 自动播 / 手动帧 |
| `UseDynamicParameters` | **实例化播放时设为 false**，改走 PerInstanceCustomData |
| `StartFrame` / `EndFrame` | 默认片段（AutoPlay） |
| `UseTwoInfluences` / `UseFourInfluences` | 影响骨数量 |
| `UseUV0`~`UseUV3` | 顶点索引 UV，通常 UV1 |

推荐用 API 同步，避免手填出错：

```text
UpdateMaterialInstanceFromDataAsset(DataAsset, MaterialInstance, GlobalParameter 或 LayerParameter)
```

Association 需与母材质里参数实际挂在 **Global** 还是 **Layer** 一致；官方示例多为 Layer 参数，按你母材质结构选择。

### 5.3 母材质必须满足的开关

| 设置 | 要求 |
|------|------|
| Use Material Attributes | 建议开启（官方示例路径） |
| Used with Instanced Static Meshes | ISM/HISM 必开 |
| UseDynamicParameters | 单网格可用 true；大规模实例用 **false** |
| AutoPlay | 预览默认动画；实例化时仍由 CustomData 驱动片段 |

### 5.4 多材质槽

同一角色多个 Slot（身体、盔甲、Logo）应：

- **各自一份 MI**  
- **共用同一套 VAT 与播放 CustomData**  
- 仅表面贴图不同  

官方 `MI_Body_BoneAnimation` + `MI_ChestLog_BoneAnimation` 即此模式。

---

## 6. 运行时播放（ISM / HISM）

### 6.1 初始化

```cpp
// bAutoPlay=true：每实例 4 个 float（AutoPlayData）
// bAutoPlay=false：每实例 2 个 float（FrameData）
UAnimToTextureInstancePlaybackLibrary::SetupInstancedMeshComponent(
    MeshComponent, NumInstances, /*bAutoPlay*/ true);
```

HISM 继承 ISM，同样适用。材质侧 **`UseDynamicParameters = false`**，且 `NumCustomDataFloats` 与结构体一致。

### 6.2 AutoPlayData（GPU 推帧，适合循环）

结构体字段 → CustomData：

| Index | 字段 | 含义 |
|-------|------|------|
| 0 | `TimeOffset` | 时间相位 |
| 1 | `PlayRate` | 播放速率 |
| 2 | `StartFrame` | 片段起始（全局帧） |
| 3 | `EndFrame` | 片段结束（含） |

```cpp
FAnimToTextureAutoPlayData Data;
UAnimToTextureInstancePlaybackLibrary::GetAutoPlayDataFromDataAsset(
    DataAsset, AnimIndex, Data, TimeOffset, PlayRate);
UAnimToTextureInstancePlaybackLibrary::UpdateInstanceAutoPlayData(
    Comp, InstanceIndex, Data, true);
// 或 BatchUpdateInstancesAutoPlayData(...)
```

**切换动画：** 只改该实例的 `StartFrame`/`EndFrame`（来自 `DataAsset->Animations[i]`），无需换 Mesh/材质。

材质内算帧（与 C++ `GetFrame` 一致）：

```text
Frame = StartFrame + Fmod((Time + TimeOffset) * PlayRate * SampleRate,
                          EndFrame - StartFrame + 1)
```

注意：官方 AutoPlay **始终循环**（Fmod），没有「播完停住」。

### 6.3 FrameData（CPU 指定帧，可做单次）

| Index | 字段 |
|-------|------|
| 0 | `Frame` |
| 1 | `PrevFrame`（运动模糊） |

```cpp
FAnimToTextureFrameData FrameData;
UAnimToTextureInstancePlaybackLibrary::GetFrameDataFromDataAsset(
    DataAsset, AnimIndex, Time, FrameData, TimeOffset, PlayRate);
// 单次：自行 clamp 到 [StartFrame, EndFrame] 后
UAnimToTextureInstancePlaybackLibrary::UpdateInstanceFrameData(
    Comp, InstanceIndex, FrameData, true);
```

### 6.4 循环 vs 单次（怎么选）

| 需求 | 做法 |
|------|------|
| 循环 Idle/Walk | AutoPlayData + 官方材质 |
| 单次 Attack/Death 定格 | FrameData + Tick 里 `clamp` 到 EndFrame |
| 海量实例、少 Tick | 自定义材质算帧（如本仓库 `M_Crowd_Legion`：`PackedRate` 正负表示循环/单次） |

详细参数与算帧公式见：[A2T材质蓝图基础逻辑分析.md](./A2T材质蓝图基础逻辑分析.md)。

---

## 7. 推荐工作流清单（Checklist）

- [ ] 启用 AnimToTexture，能看到 `/AnimToTexture` 插件内容  
- [ ] 从 Skeletal Mesh 生成专用 Static Mesh，UV Channel 与 Lightmap 无冲突  
- [ ] 创建 BonePos / BoneRot / BoneWeight 三张贴图  
- [ ] 配置 DataAsset：Mesh、贴图、`Mode=Bone`、AnimSequences、SampleRate  
- [ ] 复制并改造母材质 + MI，接入 `ML_BoneAnimation`，开启 ISM  
- [ ] 执行 `AnimationToTexture`，确认 Info 与 MI 参数已更新  
- [ ] Static Mesh 槽位绑定新 MI  
- [ ] `SetupInstancedMeshComponent` + AutoPlay/FrameData 验证多实例与切动画  

---

## 8. 常见问题

| 问题 | 处理 |
|------|------|
| 烘焙报 UV / Lightmap 冲突 | 改 `UVChannel` 或 Static Mesh Lightmap 设置 |
| 有贴图但不动 | 检查 MI 是否绑定；`AutoPlay`/`UseDynamicParameters`；ISM CustomData 是否写入 |
| 实例动作全同步 | 给 AutoPlay 不同 `TimeOffset` |
| 切动画无效 | 确认改的是实例 CustomData 的 Start/End，且 DataAsset.Animations 区间正确 |
| 单次动画会循环 | 官方 AutoPlay 必循环；改用 FrameData clamp 或自定义材质 |
| 编辑器预览与 PIE 不一致 | 注意 Time 节点是否 Ignore Pause；CustomData 是否 MarkRenderStateDirty |
| 多 Slot 只有一部分动 | 每个 Slot 的 MI 都要同步 VAT 参数（同一 DataAsset 更新多次） |

---

## 9. 与本仓库 VgCrowdLegion 的关系

| 项目 | 说明 |
|------|------|
| 依赖 | `VgCrowdLegion.uplugin` 依赖 `AnimToTexture` |
| 导出 | `CrowdLegionExporter` 基于官方 `M_Body_BoneAnimation` / `MF_BoneAnimation` 生成军团材质 |
| 播放 | `UUnitHISMComponent`（`UCrowdUnitComponent` / `UCavalryUnitComponent`）+ 层表驱动 N 个 HISM，4 槽 CustomData，统一相位，支持循环 / 单次定格 |
| 插件 Content | 本地测试图可不入库（见 `.gitignore`）；源码在 `Client/Plugins/Development/VgCrowdLegion/Source` |

人群 / 军团场景优先走 VgCrowdLegion 管线；学习官方逻辑、做单网格预览时用本文 + 官方 Mannequin 示例。

---

## 10. API / 源码速查

| 符号 | 作用 |
|------|------|
| `UAnimToTextureDataAsset` | 烘焙配置与动画帧区间 Info |
| `UAnimToTextureBPLibrary::AnimationToTexture` | 执行烘焙 |
| `UAnimToTextureBPLibrary::ConvertSkeletalMeshToStaticMesh` | 骨骼转静态 |
| `UAnimToTextureBPLibrary::UpdateMaterialInstanceFromDataAsset` | MI 参数同步 |
| `UAnimToTextureInstancePlaybackLibrary` | ISM/HISM 播放与切动画 |
| `AnimToTextureParamNames` | 材质参数名常量（`AnimToTextureDataAsset.h`） |
