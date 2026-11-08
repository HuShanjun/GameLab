# git_push.bat Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add repo-root `git_push.bat` that pushes the current branch to `origin/<same-name>` and optionally squashes unpushed commits into one interactive message.

**Architecture:** Single pure Windows `.bat` script. Uses `git` CLI, temp message file for multi-line commits, `set /p` prompts. No PowerShell dependency. Soft-reset squash against `origin/<branch>` then normal (non-force) push.

**Tech Stack:** Windows cmd `.bat`, Git

## Global Constraints

- Spec: `Docs/superpowers/specs/2026-11-08-git-push-bat-design.md`
- Pure `.bat` only (no companion `.ps1`)
- Never `--force` / `--force-with-lease`
- Push target: `origin` + current branch name (`git push -u origin HEAD`)
- Squash list order: `git log --reverse --format=%s origin/<branch>..HEAD`
- Squash list lines: two spaces + `N. ` + subject
- First push (no `origin/<branch>`): allow direct push only; reject squash

## File Map

| Path | Change |
|------|--------|
| `git_push.bat` | Create — full interactive push/squash script |

---

### Task 1: Create `git_push.bat` (bootstrap + list + direct push)

**Files:**
- Create: `git_push.bat`

**Interfaces:**
- Produces: runnable script with repo-root cd, branch detection, fetch, unpushed listing, Y/N squash prompt, non-squash `git push -u origin HEAD`

- [ ] **Step 1: Write `git_push.bat` with header, helpers, and push-without-squash path**

Create `git_push.bat` at repo root with this content (Task 2 will extend the squash branch; include a stub label `:do_squash` that echoes "not implemented" and exits 1 for now — replaced in Task 2):

```bat
@echo off
setlocal EnableExtensions EnableDelayedExpansion
chcp 65001 >nul

rem Resolve repo root
for /f "delims=" %%i in ('git rev-parse --show-toplevel 2^>nul') do set "REPO_ROOT=%%i"
if not defined REPO_ROOT (
  echo [ERROR] Not inside a git repository.
  exit /b 1
)
cd /d "%REPO_ROOT%"

rem Reject detached HEAD
for /f "delims=" %%i in ('git rev-parse --abbrev-ref HEAD 2^>nul') do set "BRANCH=%%i"
if /i "%BRANCH%"=="HEAD" (
  echo [ERROR] Detached HEAD. Checkout a branch first.
  exit /b 1
)
if not defined BRANCH (
  echo [ERROR] Cannot resolve current branch.
  exit /b 1
)

echo [INFO] Branch: %BRANCH%
echo [INFO] Repo:   %REPO_ROOT%

rem Fetch (non-fatal)
git fetch origin
if errorlevel 1 (
  echo [WARN] git fetch origin failed; using local remote-tracking info.
)

rem Does origin/BRANCH exist?
set "HAS_REMOTE=0"
git rev-parse --verify --quiet "origin/%BRANCH%" >nul 2>&1
if not errorlevel 1 set "HAS_REMOTE=1"

if "%HAS_REMOTE%"=="1" (
  set "RANGE=origin/%BRANCH%..HEAD"
) else (
  echo [INFO] No origin/%BRANCH% yet — this will be a first push.
  set "RANGE="
)

rem Count unpushed commits
set "AHEAD=0"
if "%HAS_REMOTE%"=="1" (
  for /f %%c in ('git rev-list --count %RANGE% 2^>nul') do set "AHEAD=%%c"
) else (
  rem First push: treat all commits reachable as needing push only if remote empty of this branch.
  rem Spec: if no remote branch, still allow push; "no unpushed" means local equals nothing to send —
  rem use: if branch has at least 1 commit, ahead is "all local history for first push" display via log limit.
  for /f %%c in ('git rev-list --count HEAD 2^>nul') do set "AHEAD=%%c"
)

if "%AHEAD%"=="0" (
  echo [INFO] No commits to push.
  exit /b 0
)

echo.
echo [INFO] Commits to push ^(%AHEAD%^):
if "%HAS_REMOTE%"=="1" (
  git log --oneline %RANGE%
) else (
  git log --oneline -n %AHEAD%
)
echo.

set "SQUASH=N"
set /p "SQUASH=Merge into one commit before push? [y/N]: "
if /i "%SQUASH%"=="Y" goto do_squash
if /i "%SQUASH%"=="YES" goto do_squash

echo [INFO] Pushing without squash...
git push -u origin HEAD
if errorlevel 1 (
  echo [ERROR] git push failed.
  exit /b 1
)
echo [OK] Pushed %BRANCH% to origin/%BRANCH%.
exit /b 0

:do_squash
echo [ERROR] Squash path not implemented yet.
exit /b 1
```

- [ ] **Step 2: Smoke-test non-squash path (dry logic)**

From repo root with a clean tree that is already synced to origin (or temporarily on a throwaway branch with no ahead commits):

```bat
git_push.bat
```

Expected when ahead=0: `[INFO] No commits to push.` exit 0.

When ahead≥1 and answer `N`: performs `git push -u origin HEAD` (only run if user intends a real push; otherwise verify script reaches the prompt).

- [ ] **Step 3: Commit scaffold**

```powershell
git add git_push.bat
git commit -m "Add git_push.bat scaffold with direct push path."
```

---

### Task 2: Implement squash path (message file + soft reset + push)

**Files:**
- Modify: `git_push.bat` — replace `:do_squash` stub with full implementation

**Interfaces:**
- Consumes: `%BRANCH%`, `%HAS_REMOTE%`, `%RANGE%`, `%REPO_ROOT%` from Task 1
- Produces: squash flow per spec

- [ ] **Step 1: Replace `:do_squash` section**

Replace from `:do_squash` to end of file with:

```bat
:do_squash
if not "%HAS_REMOTE%"=="1" (
  echo [ERROR] Cannot squash on first push — origin/%BRANCH% does not exist.
  echo         Push once without squash, or create the remote branch first.
  exit /b 1
)

rem Dirty tracked files warning
set "DIRTY=0"
for /f %%i in ('git status --porcelain -uno 2^>nul') do set "DIRTY=1"
if "%DIRTY%"=="1" (
  echo [WARN] Working tree has tracked modifications.
  set "CONT=N"
  set /p "CONT=Continue squash anyway? [y/N]: "
  if /i not "!CONT!"=="Y" if /i not "!CONT!"=="YES" (
    echo [INFO] Aborted.
    exit /b 1
  )
)

set "TITLE="
set /p "TITLE=Commit title (required): "
if not defined TITLE (
  echo [ERROR] Title is required.
  exit /b 1
)
rem Trim check: empty after spaces only — basic reject if blank
if "%TITLE%"=="" (
  echo [ERROR] Title is required.
  exit /b 1
)

set "APPEND=N"
set /p "APPEND=Append numbered unpushed subjects to body? [y/N]: "

set "MSGFILE=%TEMP%\git_push_msg_%RANDOM%.txt"
(
  echo %TITLE%
) > "%MSGFILE%"

if /i "%APPEND%"=="Y" goto append_list
if /i "%APPEND%"=="YES" goto append_list
goto after_msg

:append_list
rem Blank line then "  N. subject" oldest-first
>> "%MSGFILE%" echo.
set "N=0"
for /f "delims=" %%s in ('git log --reverse --format^=%%s %RANGE%') do (
  set /a N+=1
  >> "%MSGFILE%" echo   !N!. %%s
)

:after_msg
echo.
echo [INFO] Commit message preview:
type "%MSGFILE%"
echo.

echo [INFO] Soft-reset to origin/%BRANCH% and recreate one commit...
git reset --soft "origin/%BRANCH%"
if errorlevel 1 (
  echo [ERROR] git reset --soft failed.
  del "%MSGFILE%" >nul 2>&1
  exit /b 1
)

git commit -F "%MSGFILE%"
if errorlevel 1 (
  echo [ERROR] git commit failed. Tip: git reflog to recover pre-reset HEAD.
  del "%MSGFILE%" >nul 2>&1
  exit /b 1
)
del "%MSGFILE%" >nul 2>&1

echo [INFO] Pushing squashed commit...
git push -u origin HEAD
if errorlevel 1 (
  echo [ERROR] git push failed. Tip: git reflog to recover if needed.
  exit /b 1
)
echo [OK] Squashed and pushed %BRANCH% to origin/%BRANCH%.
exit /b 0
```

**Note on `echo %TITLE%`:** If title contains `&` `|` `<` `>` it can break cmd. Document in script comment: avoid those characters in title, or implement delayed-expansion write. Prefer writing title via:

```bat
> "%MSGFILE%" echo(!TITLE!
```

Use delayed expansion for TITLE as well: after `set /p "TITLE=..."`, write with `echo(!TITLE!`.

Correct write block:

```bat
> "%MSGFILE%" echo(!TITLE!
```

And for append lines use `echo(  !N!. %%s` carefully — prefer:

```bat
>> "%MSGFILE%" echo(  !N!. %%s
```

- [ ] **Step 2: Grep safety — no force push**

```powershell
Select-String -Path git_push.bat -Pattern "force" -CaseSensitive:$false
```

Expected: no `--force` / `--force-with-lease` (comments mentioning "non-force" are ok if present; prefer zero matches on `force`).

- [ ] **Step 3: Manual acceptance (local)**

1. Create a throwaway branch from `main`, make 2 empty commits, push branch once, make 1 more commit — run script, choose N → remote has 1 new commit.
2. Make 2 more commits, run script, choose Y, title `test squash`, append Y → remote history shows 1 commit; `git log -1 --format=%B` shows:

```
test squash

  1. ...
  2. ...
```

3. Delete throwaway branch locally/remotely when done.

Alternatively, if network/policy blocks throwaway push: walk through prompts offline up to push and verify message file content by temporarily inserting `type "%MSGFILE%" & pause` (remove before commit).

- [ ] **Step 4: Commit**

```powershell
git add git_push.bat
git commit -m "Implement squash path in git_push.bat."
```

---

### Task 3: Final acceptance against spec checklist

**Files:**
- Verify only: `git_push.bat`
- Spec: `Docs/superpowers/specs/2026-11-08-git-push-bat-design.md`

- [ ] **Step 1: Checklist**

| Check | How |
|-------|-----|
| Root `git_push.bat` exists | `Test-Path git_push.bat` |
| No force flags | `Select-String force git_push.bat` empty for push flags |
| First-push squash rejected | Logic present: `HAS_REMOTE` check in `:do_squash` |
| List format `  N. ` | append loop uses two spaces + number |
| Order oldest-first | `--reverse` in log |
| Direct push uses `-u origin HEAD` | present in both paths |

- [ ] **Step 2: Commit design+script together if any leftover edits**

If only verification with no code changes, skip commit.

```powershell
git status -sb
```

---

## Spec Coverage

| Requirement | Task |
|-------------|------|
| Repo root bat, cd to toplevel | Task 1 |
| Reject detached HEAD | Task 1 |
| fetch + warn | Task 1 |
| List unpushed; exit 0 if none | Task 1 |
| Push to origin same-name `-u` | Task 1–2 |
| Optional squash Y/N | Task 1–2 |
| First push: no squash | Task 2 |
| Dirty tree warn + confirm | Task 2 |
| Interactive title | Task 2 |
| Optional numbered body `  N. subject` oldest-first | Task 2 |
| soft reset + commit -F + push | Task 2 |
| No force push | Task 2–3 |
| reflog tip on failure | Task 2 |
