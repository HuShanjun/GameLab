# git_push Python 重构设计

**日期：** 2026-11-08  
**状态：** 已批准  
**范围：** 将 `git_push.bat` 核心逻辑迁到 `tools/git_push.py`；bat 仅作 Windows 入口  
**前序：** `Docs/superpowers/specs/2026-11-08-git-push-bat-design.md`（推送/squash 行为不变）

## 目标

1. 用 Python 实现推送与可选 squash 的全部业务逻辑
2. 根目录 `git_push.bat` 只负责调用 `python tools\git_push.py`
3. 非交互自动化改为 CLI 参数（不再使用 `GIT_PUSH_*` 环境变量）

## 非目标

- 不 force push
- 不引入第三方 Python 包（仅标准库）
- 不改为 PowerShell 入口
- 不改变规格中的 squash 文案格式与远程分支策略

## 文件

| 路径 | 职责 |
|------|------|
| `tools/git_push.py` | 核心：repo 检测、fetch、列表、交互/CLI、squash、push |
| `git_push.bat` | `cd` 仓库根（或依赖 py 内 cd）；`python "%~dp0tools\git_push.py" %*` |

## 行为（继承前序规格）

1. `git rev-parse --show-toplevel` 定位仓库并切换工作目录
2. 拒绝 detached HEAD
3. `git fetch origin`（失败警告，继续）
4. 未推送：`origin/<branch>..HEAD`；无远程同名分支则首次推送且 **禁止 squash**
5. 无未推送 → 退出 0
6. 打印 `git log --oneline`
7. 是否 squash：交互默认否；或 `--squash`
8. 直接推送：`git push -u origin HEAD`
9. Squash：
   - 脏跟踪文件警告；交互确认或 `--yes`
   - 标题：交互或 `--title`（必填）
   - 是否拼接 subject：交互或 `--append-subjects`
   - 正文格式：标题 + 空行 + `  N. <subject>`（`git log --reverse --format=%s`，旧→新）
   - `git reset --soft origin/<branch>` → `git commit --allow-empty -F <msgfile>` → `git push -u origin HEAD`

## CLI

```
python tools/git_push.py
python tools/git_push.py --squash --title "..." [--append-subjects] [--yes]
```

| 参数 | 含义 |
|------|------|
| `--squash` | 合并未推送为一条再推送 |
| `--title TEXT` | squash 提交标题（与 `--squash` 同用；缺省则交互询问） |
| `--append-subjects` | 拼接编号 subject 列表 |
| `--yes` | 脏树时跳过确认 |

无相关参数时对应步骤走 `input()`。

## bat 入口

```bat
@echo off
setlocal
cd /d "%~dp0"
where python >nul 2>&1
if errorlevel 1 (
  echo [ERROR] python not found in PATH.
  exit /b 1
)
python "%~dp0tools\git_push.py" %*
exit /b %ERRORLEVEL%
```

（Python 内仍以 `git rev-parse --show-toplevel` 为准，支持从子目录被调用时若将来扩展；当前 bat 先 `cd` 到仓库根。）

## 验收

- `git_push.bat` / `python tools/git_push.py` 在无未推送时退出 0
- 不 squash：历史条数不变，仅 push
- `--squash --title T --append-subjects`：一条 commit，正文含 `  1. ` / `  2. `
- 首次无远程分支时 `--squash` 报错
- 源码无 `--force` / `--force-with-lease`
- 无 `GIT_PUSH_*` 依赖
