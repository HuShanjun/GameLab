@echo off
setlocal enabledelayedexpansion

rem ==============================================================================
rem  ubt_build.bat - Unreal Engine 5 UBT Build Script
rem  Usage:
rem    ubt_build.bat [Target] [Platform] [Configuration] [ExtraArgs...]
rem  Examples:
rem    ubt_build.bat
rem    ubt_build.bat RagdollSample Win64 Development
rem    ubt_build.bat RagdollSampleEditor Win64 Development
rem    ubt_build.bat RagdollSample Win64 Shipping
rem ==============================================================================

set "PROJECT_DIR=%~dp0"
set "PROJECT_NAME=Client"
set "PROJECT_FILE=%PROJECT_DIR%%PROJECT_NAME%.uproject"
set "DEFAULT_TARGET=ClientEditor"
set "DEFAULT_PLATFORM=Win64"
set "DEFAULT_CONFIG=Development"

rem Bypass live coding mutex collision when other projects share the same engine
set "UnrealBuildTool_UEBuildConfiguration__bAllowHotReloadFromIDE=false"
if not exist "%PROJECT_FILE%" (
    echo [ERROR] Project file not found: "%PROJECT_FILE%"
    exit /b 1
)

rem --- Locate Unreal Engine 5.8 Installation ---
set "ENGINE_ROOT="

if defined UE_ENGINE_PATH if exist "%UE_ENGINE_PATH%\Engine\Build\BatchFiles\Build.bat" (
    set "ENGINE_ROOT=%UE_ENGINE_PATH%"
)
if not defined ENGINE_ROOT if defined UE_ROOT if exist "%UE_ROOT%\Engine\Build\BatchFiles\Build.bat" (
    set "ENGINE_ROOT=%UE_ROOT%"
)
if not defined ENGINE_ROOT if defined UNREAL_ENGINE_PATH if exist "%UNREAL_ENGINE_PATH%\Engine\Build\BatchFiles\Build.bat" (
    set "ENGINE_ROOT=%UNREAL_ENGINE_PATH%"
)

rem Query Registry (HKLM)
if not defined ENGINE_ROOT (
    for /f "tokens=2*" %%A in ('reg query "HKLM\SOFTWARE\EpicGames\Unreal Engine\5.8" /v InstalledDirectory 2^>nul') do (
        if exist "%%~B\Engine\Build\BatchFiles\Build.bat" set "ENGINE_ROOT=%%~B"
    )
)

rem Query Registry (HKCU builds)
if not defined ENGINE_ROOT (
    for /f "tokens=2*" %%A in ('reg query "HKCU\SOFTWARE\Epic Games\Unreal Engine\Builds" /v "5.8" 2^>nul') do (
        if exist "%%~B\Engine\Build\BatchFiles\Build.bat" set "ENGINE_ROOT=%%~B"
    )
)

rem Check Common Default Paths
if not defined ENGINE_ROOT (
    if exist "D:\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat" (
        set "ENGINE_ROOT=D:\Epic Games\UE_5.8"
    ) else if exist "C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat" (
        set "ENGINE_ROOT=C:\Program Files\Epic Games\UE_5.8"
    ) else if exist "E:\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat" (
        set "ENGINE_ROOT=E:\Epic Games\UE_5.8"
    )
)

if not defined ENGINE_ROOT (
    echo [ERROR] Could not find Unreal Engine 5.8 installation.
    echo Please set UE_ENGINE_PATH environment variable to your UE 5.8 root directory.
    echo Example: set UE_ENGINE_PATH=D:\Epic Games\UE_5.8
    exit /b 1
)

set "BUILD_BAT=%ENGINE_ROOT%\Engine\Build\BatchFiles\Build.bat"
if not exist "%BUILD_BAT%" (
    echo [ERROR] Build.bat not found at: "%BUILD_BAT%"
    exit /b 1
)

rem --- Parse Arguments ---
set "TARGET=%~1"
set "PLATFORM=%~2"
set "CONFIG=%~3"

if "%TARGET%"=="" set "TARGET=%DEFAULT_TARGET%"
if "%PLATFORM%"=="" set "PLATFORM=%DEFAULT_PLATFORM%"
if "%CONFIG%"=="" set "CONFIG=%DEFAULT_CONFIG%"

rem Shift first 3 arguments and collect remaining extra flags
shift
shift
shift
set "EXTRA_ARGS="
:CollectExtra
if not "%~1"=="" (
    set "EXTRA_ARGS=!EXTRA_ARGS! %1"
    shift
    goto :CollectExtra
)

echo ==============================================================================
echo [UBT Build] Project:       %PROJECT_NAME%
echo [UBT Build] Target:        %TARGET%
echo [UBT Build] Platform:      %PLATFORM%
echo [UBT Build] Configuration: %CONFIG%
echo [UBT Build] Engine:        %ENGINE_ROOT%
if defined EXTRA_ARGS echo [UBT Build] Extra Flags:   %EXTRA_ARGS%
echo ==============================================================================

call "%BUILD_BAT%" %TARGET% %PLATFORM% %CONFIG% -Project="%PROJECT_FILE%" -WaitMutex %EXTRA_ARGS%
set "BUILD_EXIT_CODE=%ERRORLEVEL%"

if %BUILD_EXIT_CODE% equ 0 (
    echo [UBT Build] Build SUCCEEDED.
) else (
    echo [UBT Build] Build FAILED with exit code %BUILD_EXIT_CODE%.
)

exit /b %BUILD_EXIT_CODE%
