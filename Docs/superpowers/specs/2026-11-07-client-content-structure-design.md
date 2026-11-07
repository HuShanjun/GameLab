# Client Content 三分目录骨架设计

**日期：** 2026-11-07  
**状态：** 已批准（做法 3：仅建骨架）  
**范围：** `Client/Content/` 目录约定 + 烹饪排除配置

## 背景

`Client` 为 UE 5.8 蓝图版第三人称模板。`Content/` 顶层仍是模板目录（`ThirdPerson`、`Characters`、`Input` 等），缺少明确的 Game / Editor / Developer 分层。

## 目标

在 `Content/` 下建立三分约定：

| 目录 | 用途 | 是否进包 |
|------|------|----------|
| `Content/Game/` | 今后游戏相关逻辑与资产 | 是 |
| `Content/Editor/` | 编辑器扩展资产 | 否 |
| `Content/Developer/` | 开发者个人文件 | 否 |

## 非目标（本次不做）

- 不将现有 `ThirdPerson` / `Characters` / `Input` / `LevelPrototyping` 等迁入 `Game/`
- 不修改默认地图、GameMode 等引擎路径配置
- 不新增 C++ 模块或项目插件
- 不重命名或收拢模板资产命名

## 目标结构（落地后）

```
Content/
├── Game/                 # 新建空目录
├── Editor/               # 新建空目录
├── Developer/            # 新建；承接个人沙箱
│   └── hushanjun/        # 自 Content/Developers/hushanjun 迁入
├── ThirdPerson/          # 保持现状
├── Characters/
├── Input/
├── LevelPrototyping/
├── Collections/
├── __ExternalActors__/
└── __ExternalObjects__/
```

`Content/Developers/`：将 `hushanjun` 迁到 `Developer/hushanjun` 后，若目录已空则删除。

## 配置变更

在 `Client/Config/DefaultGame.ini` 的 `ProjectPackagingSettings` 中增加：

```ini
[/Script/UnrealEd.ProjectPackagingSettings]
+DirectoriesToNeverCook=(Path="/Game/Editor")
+DirectoriesToNeverCook=(Path="/Game/Developer")
```

说明：UE 内容根挂载为 `/Game`，故磁盘路径 `Content/Editor` 对应 `/Game/Editor`。

`Content/Game/` 不加入 NeverCook。

## 空目录落地方式

UE 不跟踪完全空的文件夹时，在 `Game/`、`Editor/`、`Developer/` 下各放置一个可入库的占位文件（例如 `.gitkeep`），确保 Git 保留目录。

## 验收标准

1. `Content/Game`、`Content/Editor`、`Content/Developer` 存在且可被 Git 跟踪
2. `Content/Developer/hushanjun` 存在（自原 `Developers/hushanjun` 迁入）
3. `DefaultGame.ini` 已排除 `/Game/Editor` 与 `/Game/Developer` 的烹饪
4. 现有模板玩法路径未改，工程仍可按原地图/GameMode 打开

## 后续（不在本规格内）

将模板内容迁入 `Content/Game/`、更新地图与 GameMode 路径、清理 redirectors——单独开规格与计划。
