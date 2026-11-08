# git_push Python Refactor Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Move git_push core logic into `tools/git_push.py`; shrink `git_push.bat` to a `python` launcher.

**Architecture:** Single stdlib Python script owns git subprocess calls, interactive prompts, argparse CLI, squash message assembly, and push. Bat only checks for `python` and forwards `%*`.

**Tech Stack:** Python 3, git CLI, Windows `.bat`

## Global Constraints

- Spec: `Docs/superpowers/specs/2026-11-08-git-push-python-design.md`
- Behavior from: `Docs/superpowers/specs/2026-11-08-git-push-bat-design.md`
- No third-party packages; no force push; no `GIT_PUSH_*` env vars
- Bat invokes: `python "%~dp0tools\git_push.py" %*`

## File Map

| Path | Change |
|------|--------|
| `tools/git_push.py` | Create |
| `git_push.bat` | Replace with thin wrapper |

---

### Task 1: Implement `tools/git_push.py`

**Files:**
- Create: `tools/git_push.py`

- [ ] **Step 1: Write the full script**

Implement `main()` with:

- `run_git(*args, check=False) -> CompletedProcess` using `subprocess.run(..., text=True, encoding="utf-8", errors="replace")`
- Resolve toplevel; `os.chdir`; reject detached HEAD (`rev-parse --abbrev-ref HEAD` == `HEAD`)
- `fetch origin` warn-on-fail
- Detect `origin/{branch}` via `rev-parse --verify --quiet`
- Ahead count / log for `origin/{branch}..HEAD` or first-push path
- `argparse`: `--squash`, `--title`, `--append-subjects`, `--yes`
- Prompts via `input()` when flags absent
- Squash: dirty check (`status --porcelain -uno`), message file with title + optional `  N. subject` from `log --reverse --format=%s`, `reset --soft`, `commit --allow-empty -F`, `push -u origin HEAD`
- Exit codes: 0 success / nothing to push; 1 errors
- Never pass `--force` / `--force-with-lease`

- [ ] **Step 2: Smoke test**

```powershell
python tools/git_push.py
```

Expected when synced: print no commits / exit 0.  
With test branch + env-free flags:

```powershell
python tools/git_push.py --squash --title "test squash" --append-subjects --yes
```

- [ ] **Step 3: Commit**

```powershell
git add tools/git_push.py
git commit -m "Add tools/git_push.py with push and squash logic."
```

---

### Task 2: Replace `git_push.bat` wrapper

**Files:**
- Modify: `git_push.bat`

- [ ] **Step 1: Replace contents**

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

- [ ] **Step 2: Verify bat forwards**

```powershell
cmd /c git_push.bat
```

Same behavior as `python tools/git_push.py`.

- [ ] **Step 3: Grep — no force; no GIT_PUSH_**

```powershell
Select-String -Path git_push.bat,tools/git_push.py -Pattern "force|GIT_PUSH_"
```

Expected: no matches (or only comments if any — prefer zero).

- [ ] **Step 4: Commit**

```powershell
git add git_push.bat
git commit -m "Thin git_push.bat wrapper around tools/git_push.py."
```

---

## Spec Coverage

| Requirement | Task |
|-------------|------|
| Python core logic | Task 1 |
| Bat launcher + `python` only | Task 2 |
| CLI instead of GIT_PUSH_* | Task 1 |
| Same squash/push rules | Task 1 |
| No force | Task 1–2 |
