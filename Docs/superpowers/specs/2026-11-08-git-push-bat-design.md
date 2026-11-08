# git_push.bat 设计

**日期：** 2026-11-08  
**状态：** 已批准  
**范围：** 仓库根目录纯 `.bat` 脚本：推送当前分支本地未推送 commit 到远程同名分支；可选 squash 为一条记录

## 目标

1. 一键推送当前分支到 `origin/<同名分支>`（不存在则创建并 `-u` 设 upstream）
2. 可选将未推送 commit 合并（squash）为一条后再推送
3. squash 时交互输入标题；可选拼接未推送 subject 列表（两空格缩进 + `1. / 2.` 编号）

## 非目标

- 不 force push
- 不自动提交工作区未提交改动
- 不切换默认分支、不删除远程分支
- 不引入 PowerShell 脚本依赖（逻辑全部在 `.bat` 内；可用临时文件拼 commit message）

## 交互流程

1. 定位仓库根（`git rev-parse --show-toplevel`），`cd` 过去
2. 取当前分支名；拒绝 detached HEAD
3. `git fetch origin`（失败则警告，继续使用本地已有 `origin/<branch>` 信息）
4. 计算未推送集合：
   - 若存在 `origin/<branch>`：`origin/<branch>..HEAD`
   - 若不存在：提示将首次推送；**不允许 squash**（无 soft-reset 基线），仅可直接 push
5. 无未推送 commit → 提示并退出 0
6. 打印未推送 commit 列表（`git log --oneline`）
7. 询问「是否合并成一条记录？[y/N]」
8. **否：** `git push -u origin HEAD` → 结束
9. **是（squash）：**
   - 若工作区/暂存区有已跟踪文件改动：警告并二次确认是否继续
   - 询问标题（一行，非空）
   - 询问「是否拼接未推送记录到正文？[y/N]」
   - 组装 commit message 写入临时文件：
     - 始终：第一行 = 标题
     - 若拼接：空行后每行 `  N. <subject>`（两个空格缩进；N 从 1 起；subject 来自 `git log --format=%s`）
   - `git reset --soft origin/<branch>`
   - `git commit -F <临时文件>`
   - 删除临时文件
   - `git push -u origin HEAD`
10. 打印成功或失败；非 0 退出码表示失败

## 提交说明格式（拼接开启时）

```
用户输入的标题

  1. 最早一条未推送 subject
  2. 下一条 subject
```

**列表顺序：** `git log --reverse --format=%s origin/<branch>..HEAD`（从旧到新）。

## 错误与边界

| 情况 | 行为 |
|------|------|
| 非 git 仓库 | 报错退出 |
| detached HEAD | 报错退出 |
| 无未推送 | 提示退出 0 |
| 首次推送且选 squash | 报错，建议先普通推送或建远程分支 |
| push / commit 失败 | 报错退出；squash 已 reset 时提示可用 `git reflog` 恢复 |
| 未跟踪文件 | 不纳入 squash；不阻塞 |

## 验收

- 根目录存在 `git_push.bat`
- 无未推送时退出且不 push
- 选 N：直接 push，历史条数不变
- 选 Y + 不拼接：远程仅多 1 条，message 为标题
- 选 Y + 拼接：message 含缩进编号列表
- 从不执行 `--force` / `--force-with-lease`（脚本源码可 grep 确认）
