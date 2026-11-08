@echo off
setlocal EnableExtensions EnableDelayedExpansion
chcp 65001 >nul

rem Prompts read from CON (or GIT_PUSH_* env overrides for automation).
rem Git runs in cmd /c so they do not consume this script's stdin.

for /f "delims=" %%i in ('git rev-parse --show-toplevel 2^>nul') do set "REPO_ROOT=%%i"
if not defined REPO_ROOT (
  echo [ERROR] Not inside a git repository.
  exit /b 1
)
cd /d "%REPO_ROOT%"

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

cmd /c "git fetch origin <nul"
if errorlevel 1 (
  echo [WARN] git fetch origin failed; using local remote-tracking info.
)

set "HAS_REMOTE=0"
cmd /c "git rev-parse --verify --quiet origin/%BRANCH% <nul >nul 2>&1"
if not errorlevel 1 set "HAS_REMOTE=1"

if "%HAS_REMOTE%"=="1" (
  set "RANGE=origin/%BRANCH%..HEAD"
) else (
  echo [INFO] No origin/%BRANCH% yet - this will be a first push.
  set "RANGE="
)

set "AHEAD=0"
if "%HAS_REMOTE%"=="1" (
  for /f %%c in ('cmd /c "git rev-list --count %RANGE% <nul 2>nul"') do set "AHEAD=%%c"
) else (
  for /f %%c in ('cmd /c "git rev-list --count HEAD <nul 2>nul"') do set "AHEAD=%%c"
)

if "%AHEAD%"=="0" (
  echo [INFO] No commits to push.
  exit /b 0
)

echo.
echo [INFO] Commits to push ^(%AHEAD%^):
if "%HAS_REMOTE%"=="1" (
  cmd /c "git --no-pager log --oneline %RANGE% <nul"
) else (
  cmd /c "git --no-pager log --oneline -n %AHEAD% <nul"
)
echo.

set "SQUASH=N"
if defined GIT_PUSH_SQUASH (
  set "SQUASH=%GIT_PUSH_SQUASH%"
) else (
  set /p "SQUASH=Merge into one commit before push? [y/N]: " < con
)
if /i "%SQUASH%"=="Y" goto do_squash
if /i "%SQUASH%"=="YES" goto do_squash

echo [INFO] Pushing without squash...
cmd /c "git push -u origin HEAD <nul"
if errorlevel 1 (
  echo [ERROR] git push failed.
  exit /b 1
)
echo [OK] Pushed %BRANCH% to origin/%BRANCH%.
exit /b 0

:do_squash
if not "%HAS_REMOTE%"=="1" (
  echo [ERROR] Cannot squash on first push - origin/%BRANCH% does not exist.
  echo         Push once without squash, or create the remote branch first.
  exit /b 1
)

set "DIRTY=0"
for /f %%i in ('cmd /c "git status --porcelain -uno <nul 2>nul"') do set "DIRTY=1"
if "%DIRTY%"=="1" (
  echo [WARN] Working tree has tracked modifications.
  set "CONT=N"
  if defined GIT_PUSH_CONT (
    set "CONT=%GIT_PUSH_CONT%"
  ) else (
    set /p "CONT=Continue squash anyway? [y/N]: " < con
  )
  if /i "!CONT!"=="Y" goto dirty_ok
  if /i "!CONT!"=="YES" goto dirty_ok
  echo [INFO] Aborted.
  exit /b 1
)
:dirty_ok

set "TITLE="
if defined GIT_PUSH_TITLE (
  set "TITLE=%GIT_PUSH_TITLE%"
) else (
  set /p "TITLE=Commit title (required): " < con
)
if not defined TITLE (
  echo [ERROR] Title is required.
  exit /b 1
)
if "!TITLE!"=="" (
  echo [ERROR] Title is required.
  exit /b 1
)

set "APPEND=N"
if defined GIT_PUSH_APPEND (
  set "APPEND=%GIT_PUSH_APPEND%"
) else (
  set /p "APPEND=Append numbered unpushed subjects to body? [y/N]: " < con
)

set "MSGFILE=%TEMP%\git_push_msg_%RANDOM%.txt"
> "%MSGFILE%" echo(!TITLE!

if /i "!APPEND!"=="Y" goto append_list
if /i "!APPEND!"=="YES" goto append_list
goto after_msg

:append_list
>> "%MSGFILE%" echo.
set "N=0"
for /f "delims=" %%s in ('cmd /c "git --no-pager log --reverse --format=%%s %RANGE% <nul"') do (
  set /a N+=1
  >> "%MSGFILE%" echo(  !N!. %%s
)

:after_msg
echo.
echo [INFO] Commit message preview:
type "%MSGFILE%"
echo.

echo [INFO] Soft-reset to origin/%BRANCH% and recreate one commit...
cmd /c "git reset --soft origin/%BRANCH% <nul"
if errorlevel 1 (
  echo [ERROR] git reset --soft failed.
  del "%MSGFILE%" >nul 2>&1
  exit /b 1
)

cmd /c git commit --allow-empty -F "%MSGFILE%" ^<nul
if errorlevel 1 (
  echo [ERROR] git commit failed. Tip: git reflog to recover pre-reset HEAD.
  del "%MSGFILE%" >nul 2>&1
  exit /b 1
)
del "%MSGFILE%" >nul 2>&1

echo [INFO] Pushing squashed commit...
cmd /c "git push -u origin HEAD <nul"
if errorlevel 1 (
  echo [ERROR] git push failed. Tip: git reflog to recover if needed.
  exit /b 1
)
echo [OK] Squashed and pushed %BRANCH% to origin/%BRANCH%.
exit /b 0
