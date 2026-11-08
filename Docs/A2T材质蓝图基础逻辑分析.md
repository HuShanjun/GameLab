# A2T 材质蓝图基础逻辑分析

**范围：** `/AnimToTexture/Characters/Mannequin/Materials/BoneAnimation/`  
**引擎路径（UE 5.8）：** `Engine/Plugins/Experimental/AnimToTexture`  
**依据：** 材质资产参数、`AnimToTextureInstancePlaybackHelpers`、`ML_BoneAnimation` / `GetAutoPlayFrame`、以及项目内 `VgCrowdLegion` 扩展实现

---

## 1. 三份材质各自做什么、参数含义

该目录下是 **3 个材质资产**（材质图，不是蓝图 Actor）：

| 资产 | 类型 | 作用 |
|------|------|------|
| `M_Body_BoneAnimation` | **母材质** | 表面着色 + 挂上骨骼 VAT 层；`Use Material Attributes`、`Used with Instanced Static Meshes` |
| `MI_Body_BoneAnimation` | **材质实例** | 身体槽：烘焙后的骨骼贴图 / 区间参数写在这里 |
| `MI_ChestLog_BoneAnimation` | **材质实例** | Logo 槽：同一套骨骼动画层，表面贴图不同 |

真正算帧 / 蒙皮在 Material Layer：`ML_BoneAnimation`（经 `Material Attribute Layers` 叠进母材质），核心采样在 `MF_BoneAnimation` → `BlendBonePositionAndNormal`。

### 1.1 母材质图基础链路

```
表面属性 (BaseColor / Normal / …)
        ↓
Material Attribute Layers  ← Layer = ML_BoneAnimation
        ↓
把层输出的 World Position Offset / Normal 合回最终 Material Attributes
```

`ML_BoneAnimation` 内部大致是：

1. `GetFrameSwitch`：按开关选择「自动播」或「指定帧」
2. 用当前 `Frame` 去采样 `BonePosition` / `BoneRotation` / `BoneWeights` 贴图
3. GPU 蒙皮，输出位移与法线

### 1.2 配置参数（烘焙写入 MI / Layer Parameters）

#### 贴图与布局

| 参数 | 含义 |
|------|------|
| `BonePositionTexture` | 每帧骨骼位置（相对 RefPose，已归一化） |
| `BoneRotationTexture` | 每帧骨骼旋转（轴 + 角，已归一化） |
| `BoneWeightsTexture` | 顶点骨骼索引与权重 |
| `RowsPerFrame` | 骨骼位姿贴图每一帧占几行 |
| `BoneWeightsRowsPerFrame` | 权重贴图布局行数 |
| `NumBones` | 骨骼数 |
| `MinBBox` / `SizeBBox` | 位置解包用包围盒 |
| `NumFrames` | VAT 总帧数（整条贴图高度方向） |

#### 播放相关（材质侧）

| 参数 | 含义 |
|------|------|
| `AutoPlay` | 静态开关：自动播 vs 外部指定帧 |
| `UseDynamicParameters` | true：用材质标量；false：用 **PerInstanceCustomData**（实例化必走这条路径） |
| `SampleRate` | 采样率，默认 **30** |
| `StartFrame` / `EndFrame` | 当前片段在全局 VAT 上的闭区间 |
| `TimeOffset` / `Playrate` | 时间相位与播放倍率（AutoPlay） |
| `Frame` / `PrevFrame` | 手动指定当前 / 上一帧（运动模糊） |

#### 质量开关

| 参数 | 含义 |
|------|------|
| `UseTwoInfluences` / `UseFourInfluences` | 2 / 4 骨影响混合 |
| `UseUV0` ~ `UseUV3` | 顶点索引存在哪条 UV（常见 UV1） |

烘焙时由 `UpdateMaterialInstanceFromDataAsset` 把 DataAsset 的贴图、BBox、`NumFrames`、`SampleRate`、默认 `Start/EndFrame`、`AutoPlay` 等写进材质实例。

---

## 2. HISM 下多个实例如何切换动画

官方播放库面向 **ISM**；**HISM 继承 ISM**，同一套 `NumCustomDataFloats` + `SetCustomData` 可用。

先分配 CustomData 槽位（与材质 `UseDynamicParameters = false` 对齐）：

```cpp
// bAutoPlay=true → 4 float；false → 2 float
SetupInstancedMeshComponent(HISM, NumInstances, /*bAutoPlay*/ true);
```

### 2.1 模式 A：`FAnimToTextureAutoPlayData`（GPU 自己推帧，适合循环）

CustomData 布局（与结构体内存一致）：

| Index | 字段 | 作用 |
|-------|------|------|
| 0 | `TimeOffset` | 相位，错开播放 |
| 1 | `PlayRate` | 速度 |
| 2 | `StartFrame` | 片段起始全局帧 |
| 3 | `EndFrame` | 片段结束全局帧（含） |

**切换动画** = 只改该实例的 `StartFrame` / `EndFrame`（可顺带改 `TimeOffset` / `PlayRate`），**不必换材质**：

```cpp
FAnimToTextureAutoPlayData Data;
UAnimToTextureInstancePlaybackLibrary::GetAutoPlayDataFromDataAsset(
    DataAsset, AnimationIndex, Data, /*TimeOffset*/ 0.f, /*PlayRate*/ 1.f);
UAnimToTextureInstancePlaybackLibrary::UpdateInstanceAutoPlayData(
    HISM, InstanceIndex, Data, true);
```

`GetAutoPlayDataFromDataAsset` 从 `DataAsset->Animations[i]` 取该片段的帧区间。

### 2.2 模式 B：`FAnimToTextureFrameData`（CPU 指定帧）

| Index | 字段 |
|-------|------|
| 0 | `Frame` |
| 1 | `PrevFrame` |

每帧（或按需）调用 `UpdateInstanceFrameData`；切动画时把 `Frame` 钳在目标片段的 `[Start, End]`。

### 2.3 要点

- 多动画都在 **同一张 VAT** 上首尾相接；切动作只是换 **帧窗口**，不是换 Mesh / 材质。
- 身体 / Logo 两个 MI 共用同一套 CustomData，实例一层驱动即可。

---

## 3. 如何推帧；循环 vs 单次

### 3.1 官方 AutoPlay（材质 `GetAutoPlayFrame`）

与 C++ `UAnimToTextureInstancePlaybackLibrary::GetFrame` 一致，本质是 **永远取模循环**：

```text
Frame = StartFrame
      + Fmod( (Time + TimeOffset) * PlayRate * SampleRate ,
              EndFrame - StartFrame + 1 )
```

- **循环动画**：用 AutoPlay；改 `Start/End` 即换片段；`TimeOffset` 做随机相位。
- **单次播放**：官方这套 **没有**「播完停在最后一帧」的开关，`Fmod` 会一直绕回开头。

单次可选做法如下。

### 3.2 方案①：FrameData（官方推荐思路）

```text
每 Tick:
  Frame = Start + Elapsed * SampleRate * PlayRate
  Frame = clamp(Frame, Start, End)   // 到 End 后定格
  PrevFrame = max(Start, Frame - 1)
  UpdateInstanceFrameData(...)
```

### 3.3 方案②：自定义材质（项目 VgCrowdLegion）

`M_Crowd_Legion` 使用 4 槽：`StartFrame`、`NumFrames`、`StartTime`、`PackedRate`：

```text
bLoop = (PackedRate >= 0)
PlayRate = abs(PackedRate)
ElapsedFrames = (Time - StartTime) * PlayRate * SampleRate
FrameOffset = bLoop
    ? floor(fmod(ElapsedFrames, NumFrames))
    : min(NumFrames - 1, floor(ElapsedFrames))   // 单次定格
Frame = StartFrame + FrameOffset
```

切换时：`SetCellAnim` 写入新 `StartFrame` / `NumFrames`；循环给随机 `Phase`；单次把 `StartTime` 设为当前实时时间；`PackedRate` 正数 = 循环，负数 = 单次。

---

## 4. 对照小结

| 需求 | 官方 BoneAnimation 材质路径 | VgCrowdLegion 扩展 |
|------|---------------------------|-------------------|
| 基础功能 | 母材质表面 + `ML_BoneAnimation` GPU 蒙皮 | 同思路，算帧改成自定义 |
| 多实例切动画 | 改 CustomData 的 Start/End（或 Frame） | 改 StartFrame + NumFrames |
| 循环 | AutoPlay + Fmod | PackedRate ≥ 0 |
| 单次定格 | 需 FrameData + CPU clamp | PackedRate &lt; 0，GPU clamp |

**实践建议：**

- 在 **纯官方 MI** 上做 HISM 单次攻击 / 死亡：优先 **FrameData + Tick 钳制**。
- 需要海量实例、尽量零 Tick：使用 `PackedRate` 符号位编码的自定义算帧方案更合适。

---

## 5. 相关源码与资产索引

| 路径 / 符号 | 说明 |
|-------------|------|
| `Content/.../BoneAnimation/M_Body_BoneAnimation` | 母材质 |
| `Content/.../BoneAnimation/MI_Body_BoneAnimation` | 身体材质实例 |
| `Content/.../BoneAnimation/MI_ChestLog_BoneAnimation` | Logo 材质实例 |
| `/AnimToTexture/Materials/ML_BoneAnimation` | 骨骼动画 Material Layer |
| `/AnimToTexture/Materials/MaterialFunctions/GetAutoPlayFrame` | AutoPlay 算帧 |
| `/AnimToTexture/Materials/MaterialFunctions/GetFrameSwitch` | AutoPlay / 指定帧切换 |
| `UAnimToTextureInstancePlaybackLibrary` | ISM/HISM CustomData 播放 API |
| `AnimToTextureParamNames`（`AnimToTextureDataAsset.h`） | 材质参数名常量 |
| `UCrowdUnitComponent` / `M_Crowd_Legion` | 项目内循环 / 单次扩展实现 |
