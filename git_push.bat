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
