# Cursor + UE5 + 蓝图开发方法论

> 适用工程：GameLab（Unreal Engine **5.8**，`Client/Client.uproject`）  
> 相关插件：`VgCrowdLegion`、`AnimToTexture`  
> 目标：用 Cursor 高效**阅读**与**开发** C++ / 蓝图 / 材质，并正确使用 Rules、插件与 MCP。

---

## 1. 核心心智模型

Cursor 并不能「打开并看懂」二进制 `.uasset`。正确姿势是三层闭环：

| 层 | 角色 | 典型载体 |
|----|------|----------|
| **Cursor** | 文本世界的 IDE | C++、Build.cs、Rules、`Docs/` |
| **MCP** | 编辑器世界的机械手 | 蓝图节点、材质参数、关卡、PIE、截图 |
| **Docs** | 蓝图 / 材质的可读源码 | 参数表、节点链路、设计规格 |

一句话：

> **Cursor = C++ + Docs + Rules；MCP = 进引擎改资产；Docs = 蓝图可读源。三者闭环。**

原则：

- 能进 C++ 的逻辑进 C++；蓝图只做装配与调参。
- 复杂蓝图 / 材质逻辑先写成 `Docs/*.md`，再改资产。
- **不要**让 Agent 直接编辑 `.uasset` 二进制。

本仓库已有范例：`Docs/A2T材质蓝图基础逻辑分析.md` —— Agent 读文档 ≈ 读蓝图。

---

## 2. 三类工作面

| 工作面 | Cursor 能直接做 | 需要引擎 / MCP |
|--------|-----------------|----------------|
| C++ / Build.cs / 插件源码 | 读、改、跳转、改编译脚本 | Live Coding / UBT |
| 蓝图图、材质图、关卡、UMG | 几乎不能读二进制 `.uasset` | MCP 进编辑器，或人工导出文本进 Docs |
| 设计意图 / 参数约定 / 资产关系 | Markdown 文档 | 文档当「源码」维护 |

### 职责分工（建议写进 Rules）

| 放 C++ | 放蓝图 | 放 DataAsset / 材质参数 |
|--------|--------|-------------------------|
| 性能路径、算法、VAT bake、批量工具 | 关卡流程、简单状态、调参 UI | 帧区间、贴图槽、角色变体 |

---

## 3. 工程准备（一次配好）

### 3.1 工作区

- 用 Cursor 打开仓库根：`E:\UnrealRepo\GameLab`（至少包含 `Client/` + `Docs/`）。
- 不要只打开 `Content/`，也不要把引擎安装目录放进工作区。

### 3.2 索引与忽略

建议忽略 / 排除索引：

- `Binaries/`、`Intermediate/`、`Saved/`、`DerivedDataCache/`
- 大体量二进制资产可存在于仓库，但**不要指望 Agent 理解 `.uasset` 内容**

C++ 智能：

1. 先编一次 Editor。
2. 用仓库内 `ubt_gen_compile_commands.bat`（或等价 UBT `GenerateClangDatabase`）生成 `compile_commands.json`。
3. 配置 `.clangd`，用 **clangd** 做跳转与补全。
- Agent 侧用 Grep / `@文件` / Codebase 做语义搜索。
- 避免同时启用多个互相冲突的 C++ 语言服务。

可选 Cursor / VS Code 侧扩展：

- **clangd**
- **EngineLink / unreal-intellisense** 一类（编译数据库 + 生成 UE Rules）

### 3.3 仓库知识层（建议落地）

```text
AGENTS.md                          # 项目总约定（Agent 自动读取）
.cursor/rules/ue-cpp.mdc           # C++ / 反射 / 模块约定
.cursor/rules/blueprint-policy.mdc # 蓝图边界与暴露策略
.cursor/rules/crowdlegion.mdc      # VgCrowdLegion / A2T 专有约定
.cursor/mcp.json                   # MCP（含 token 时勿提交）
Docs/                              # 设计、分析、蓝图可读源
```

`AGENTS.md` 建议写死：

- 引擎版本 **5.8**，工程路径 `Client/Client.uproject`
- 启用插件：`VgCrowdLegion`、`AnimToTexture`
- C++ 改完要 UBT / Live Coding；蓝图改完要在编辑器 Compile
- 二进制资产靠 MCP 或人工；分析文档落在 `Docs/`
- 复杂材质 / 蓝图改动必须同步更新对应 Docs

Rules 实践：

- 单文件尽量 &lt; 500 行；按主题拆分。
- 发现 Agent 重复犯错时再补规则，而不是一次写百科。
- Rules 入库，团队共享。

---

## 4. 「阅读」方法论

### 4.1 读 C++（Cursor 主场）

推荐步骤：

1. **定位**：在目标模块 / 插件目录里找责任类（例如 `@Client/Plugins/Development/VgCrowdLegion`）。
2. **链路**：从 `UFUNCTION(BlueprintCallable)` / 子系统追到参数写入与运行时采样。
3. **对照文档**：`@Docs/...` 与代码是否一致。
4. **改前约束**：只动目标模块，不碰无关 Content。

Ask / 只读模式适合摸调用链；Agent 模式适合多文件修改。

### 4.2 读蓝图 / 材质（Docs + MCP）

**无 MCP：**

1. 编辑器打开图 → 把节点链路、参数表、关键节点名记进 `Docs/`。
2. Cursor 读 / 改文档，并对照 C++。
3. 需要改图时，按文档人工改，或再接入 MCP。

**有 MCP（编辑器必须开着）：**

- 列出某路径下不编译的蓝图及原因。
- 导出 Blueprint EventGraph / 材质参数列表。
- PIE 截图做视觉验收。

### 4.3 读资产关系

优先问清：

- 引用谁 / 被谁引用（MCP asset referencers）。
- DataAsset / MI 参数由谁写入（C++ bake 还是编辑器）。
- Soft Path 与 Content 目录约定（对照 Content 结构文档）。

---

## 5. 「开发」方法论

### 5.1 推荐闭环

```text
需求
  → Docs 设计短文（或更新分析页）
  → C++ API（暴露给蓝图）
  → MCP / 人工改蓝图与材质装配
  → PIE / 截图验证
  → 回写 Docs
  → （可选）提交
```

大型改动可先走 Plan / Superpowers 规格文档（本仓库 `Docs/superpowers/specs/`）。

### 5.2 提示词习惯

差：

- 「帮我改士兵蓝图」
- 「优化 Crowd」

好：

- 「编辑器已打开 Client。用 MCP：在 `BP_Soldier_xxx` 的 BeginPlay 调用 C++ `UVgXxx::Y`；不要新建无关资产；改完 Compile，并说明改了哪些节点。」
- 「只分析 `VgCrowdLegion` 运行时帧采样；先给调用链与瓶颈假设，未确认前不要改代码。」

提示词应包含：**范围、约束、验收方式、是否允许新建资产**。

### 5.3 Cursor 模式选择

| 模式 | 用途 |
|------|------|
| Ask | 摸系统、审设计、只读分析 |
| Agent | 改 C++、写 Docs、调 MCP |
| Plan | 跨 C++ / 蓝图 / 资产的大改，先计划再动手 |

---

## 6. MCP：让 Cursor 进入 Unreal Editor

### 6.1 方案对比（UE 5.8，选一个主用）

| 方案 | 特点 | 适合 |
|------|------|------|
| [EngineWire](https://enginewire.dev/cursor-unreal-engine) | 编辑器内 HTTP MCP，Cursor 直连，单工具检索能力 | 想少折腾、蓝图 / 关卡操作 |
| [UE-MCP](https://ue-mcp.com/docs/getting-started) | `npx ue-mcp init`，动作面很全 | 重度自动化、可扩展 |
| [UEAgent](https://carlxsmg.github.io/UEAgent/) | 插件即服务器，工具多、有校验回读 | 重度蓝图 / 材质 Agent |

### 6.2 EngineWire 配置示例

项目级 `.cursor/mcp.json` 或用户级 `~/.cursor/mcp.json`：

```json
{
  "mcpServers": {
    "unreal-engine": {
      "url": "http://127.0.0.1:3000/mcp",
      "headers": {
        "X-MCP-Capability-Token": "<token>"
      }
    }
  }
}
```

stdio 备选（不手动管 token，需 Node.js）：

```json
{
  "mcpServers": {
    "unreal-engine": {
      "command": "npx",
      "args": ["-y", "unreal-engine-mcp-server"],
      "env": {
        "UE_PROJECT_PATH": "E:/UnrealRepo/GameLab/Client"
      }
    }
  }
}
```

### 6.3 使用要点

1. **先打开 UE 编辑器**，确认状态栏 MCP 端口已就绪。
2. Token 通常在 `Saved/MCP/...` —— **勿提交 git**；含 token 的 `mcp.json` 应 gitignore，或改用用户级配置。
3. Cursor Settings → MCP 显示已连接后，再让 Agent 调用。
4. 危险操作（删资产、大规模改图）必须人工确认。

### 6.4 MCP 该用 / 不该用

**该用：**

- 建 / 改蓝图节点与变量
- 改 MI / 材质参数
- 摆 Actor、查引用
- Compile、PIE、截图回传

**不该用：**

- 替代 C++ 性能关键逻辑
- 编辑器未打开时硬调 MCP
- 批量提交未审查的 Content 二进制变更

---

## 7. 能力分层

```text
L0  Cursor Agent + Grep / @Codebase + Rules
L1  clangd + compile_commands（C++ 智能）
L2  UE MCP（EngineWire / UE-MCP / UEAgent）
L3  流程技能（如 Superpowers：brainstorm → plan → verify）
L4  领域文档（Docs/ 下 A2T、CrowdLegion 设计与分析）
```

**最小可用集：**

1. clangd + Rules / `AGENTS.md`
2. 一个 UE MCP
3. Docs 作为蓝图 / 材质可读源

不必一次全上。

---

## 8. 日常工作流（按任务类型）

### A. 只改 C++ 插件

1. `@` 相关 `.h/.cpp` 与 `*.Build.cs`
2. Agent 改代码
3. Live Coding 或 `ubt_build.bat`
4. 编辑器验证 Blueprint 调用签名是否仍匹配

### B. 改材质 / 蓝图逻辑（如 BoneAnimation）

1. 先更新或对照 `Docs/` 分析页
2. C++ 侧确认 bake / 参数写入点
3. MCP：改 MI / 材质层参数 / Compile
4. PIE 截图回传 Cursor 做视觉验收
5. 文档与实现双向校准

### C. 新功能（系统级）

1. 短设计进 `Docs/` 或 `Docs/superpowers/specs/`
2. C++ 骨架 + `BlueprintCallable` 接口
3. MCP 做蓝图装配
4. 验证通过后再提交

### D. Review Content 变更

- 二进制 diff 几乎不可读。
- 提交说明写清：改了哪些资产、为什么。
- 同步 Docs；能导出的图结构变更可贴文本摘要。

---

## 9. GameLab 落地清单

| 阶段 | 动作 | 状态 |
|------|------|------|
| 文档 | 本方法论：`Docs/Cursor-UE5-Blueprint开发方法论.md` | 已完成 |
| Rules | `AGENTS.md` + `.cursor/rules/`（`ue-cpp` / `blueprint-policy` / `crowdlegion`） | 已完成 |
| MCP 模板 | `.cursor/mcp.json.example`；真实 `.cursor/mcp.json` 已加入 `.gitignore` | 已完成 |
| 本周 | 安装一个 MCP（EngineWire / UE-MCP / UEAgent），复制 example → `mcp.json` 填 token，验证「列蓝图错误 / 改 MI 参数 / PIE 截图」 | 待做 |
| 持续 | 每动一块复杂蓝图 / 材质，强制有对应 Docs；Agent 默认先 `@Docs` | 进行中 |

---

## 10. 相关文档与路径

| 路径 | 说明 |
|------|------|
| `Client/Client.uproject` | 工程入口（UE 5.8） |
| `Client/Plugins/Development/VgCrowdLegion/` | CrowdLegion 插件 |
| `Docs/A2T材质蓝图基础逻辑分析.md` | 材质 / BoneAnimation 可读分析范例 |
| `Docs/AnimToTexture使用文档.md` | A2T 使用说明 |
| `Docs/superpowers/specs/` | 功能规格与设计草稿 |
| `ubt_build.bat` / `ubt_gen_compile_commands.bat` | 构建与 clang 数据库 |

---

## 11. 修订记录

| 日期 | 说明 |
|------|------|
| 2026-11-08 | 初版：Cursor + UE5 + 蓝图阅读 / 开发方法论（含插件与 MCP） |
| 2026-11-08 | 落地 `AGENTS.md`、`.cursor/rules/*`、`mcp.json.example` |
