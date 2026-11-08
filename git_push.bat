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
