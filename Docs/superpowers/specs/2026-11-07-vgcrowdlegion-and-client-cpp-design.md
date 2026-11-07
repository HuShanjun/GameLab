# VgCrowdLegion 启用 + Client C++ 最小模块设计

**日期：** 2026-11-07  
**状态：** 已批准  
**范围：** 启用项目插件、显式依赖 AnimToTexture、添加最小可编译 Client 游戏模块、插件 Content 不入库

## 背景

`Client` 原为蓝图工程。磁盘上已有 `Client/Plugins/Development/VgCrowdLegion/`（含 Runtime + Editor C++ 与本地 Content），但未在 `Client.uproject` 中启用，也未纳入版本库。工程尚无 `Source/` 游戏模块，不利于编译带 C++ 的项目插件。

## 目标

1. 在 `Client.uproject` 启用 `VgCrowdLegion` 与 `AnimToTexture`
2. 添加最小可编译的 `Client` Runtime 模块与 Target 文件
3. 将插件源码与 `.uplugin` 入库；插件 `Content/` 本地保留、不进 Git

## 非目标

- 不添加 GameMode / GameInstance / Character 等业务 C++ 类
- 不修改 `DefaultEngine.ini` 默认地图或 BP GameMode
- 不将插件 Content（测图、材质、BP）入库
- 不移动插件目录（保持 `Plugins/Development/VgCrowdLegion`）
- 不强制在本规格内完成一次完整 Editor/Game 编译（可作为实现计划中的可选验证）

## Client C++ 模块布局

```
Client/Source/
├── Client.Target.cs
├── ClientEditor.Target.cs
└── Client/
    ├── Client.Build.cs
    ├── Public/Client.h
    └── Private/Client.cpp
```

约定：

- 模块名与工程名一致：`Client`
- `Client.Build.cs` 依赖常见运行时模块（至少 `Core`、`CoreUObject`、`Engine`）
- 模块入口实现 `IMPLEMENT_PRIMARY_GAME_MODULE`（或等价的游戏模块宏），无业务类型

## uproject 变更

在现有 Plugins 列表基础上增加：

```json
{ "Name": "VgCrowdLegion", "Enabled": true },
{ "Name": "AnimToTexture", "Enabled": true }
```

并增加 Modules：

```json
"Modules": [
  {
    "Name": "Client",
    "Type": "Runtime",
    "LoadingPhase": "Default"
  }
]
```

保留已有：`ModelingToolsEditorMode`、`GameplayStateTree`。

## 插件版本库策略

| 路径 | 处理 |
|------|------|
| `Client/Plugins/Development/VgCrowdLegion/VgCrowdLegion.uplugin` | 入库 |
| `Client/Plugins/Development/VgCrowdLegion/Source/**` | 入库 |
| `Client/Plugins/Development/VgCrowdLegion/Content/**` | 不入库；`.gitignore` 忽略 |

`AnimToTexture` 为引擎/已声明依赖插件，不拷贝进本仓库；仅在 `uproject` 显式启用。

## 验收标准

1. `Client.uproject` 含 `Client` 模块，且 `VgCrowdLegion`、`AnimToTexture` 为 Enabled
2. `Client/Source/` 下存在 Target 与 `Client` 模块最小源文件
3. Git 跟踪插件 `Source` + `.uplugin`，不跟踪插件 `Content/`
4. 现有 BP 默认地图 / GameMode 配置未被本变更改写

## 风险与说明

- 首次打开工程时编辑器可能提示编译 C++ 模块；需本机安装对应 UE 5.8 工具链
- 忽略插件 Content 后，他人克隆仓库后缺少插件测图与材质资产，需自行补齐或后续另开规格入库
