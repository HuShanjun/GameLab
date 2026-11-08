# GameLab Agent 指引

## 工程事实

- 引擎：**Unreal Engine 5.8**；工作区打开仓库根目录 `GameLab`，不要打开引擎安装目录。
- 主工程：`Client/Client.uproject`
- 游戏模块：`Client/Source/Client`（最小 Runtime；业务系统优先放插件）
- 相关已启用插件：`VgCrowdLegion`、`AnimToTexture`
- CrowdLegion 路径：`Client/Plugins/Development/VgCrowdLegion/`
- 方法论文档：`Docs/Cursor-UE5-Blueprint开发方法论.md`

## Content 布局

- 新游戏资产优先放在 `Client/Content/Game/`
- `Client/Content/Editor/`、`Client/Content/Developer/` 为 NeverCook，不要放要进包的玩法内容
- 不要把二进制 `.uasset` / `.umap` 当可读源码；复杂蓝图 / 材质逻辑写在 `Docs/`

## 职责划分

| 优先 C++ | 优先蓝图 / 资产 | 优先 Docs |
|----------|-----------------|-----------|
| 性能路径、Bake/导出工具、VAT/Crowd 运行时 | 装配、调参、关卡流程、薄胶水逻辑 | 材质/蓝图分析、规格、Agent 可读图说明 |

## Agent 行为底线

1. **禁止**把 `.uasset` / `.umap` 当文本直接改。编辑器资产由人在 UE 里改，或在编辑器已启动时通过 UE MCP 改。
2. 改 BoneAnimation / A2T / CrowdLegion 材质或蓝图流程前，先读 `Docs/` 对应文档（如 `Docs/A2T材质蓝图基础逻辑分析.md`）；行为变更后同步更新文档。
3. C++ 增删暴露给蓝图的 `UFUNCTION` / `UPROPERTY` 后，须 Live Coding 或 `ubt_build.bat` 编译，再声称蓝图仍可用。
4. 不要自创 Content 目录约定；遵循 `Docs/superpowers/specs/` 已有规格。
5. Diff 保持聚焦。不要大规模重排 Unreal 头文件，不要动无关插件。
6. MCP capability token 与真实 `.cursor/mcp.json` **禁止提交**；模板用 `.cursor/mcp.json.example`。

## 构建与智能提示

- 构建：`ubt_build.bat`（或编辑器 Live Coding）
- Clang 数据库：`ubt_gen_compile_commands.bat` + 工程 `.clangd`
- 分析时可忽略噪声目录：`Binaries/`、`Intermediate/`、`Saved/`、`DerivedDataCache/`
