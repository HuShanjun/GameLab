@echo off
setlocal enabledelayedexpansion

rem ==============================================================================
rem  ubt_gen_compile_commands.bat - Generate compile_commands.json via UBT
rem  Usage:
rem    ubt_gen_compile_commands.bat [Target] [Platform] [Configuration] [ExtraArgs...]
rem  Examples:
rem    ubt_gen_compile_commands.bat
rem    ubt_gen_compile_commands.bat Client Win64 Development
rem    ubt_gen_compile_commands.bat ClientEditor Win64 Development
rem ==============================================================================

set "PROJECT_DIR=%~dp0"
set "PROJECT_NAME=Client"
set "PROJECT_FILE=%PROJECT_DIR%%PROJECT_NAME%.uproject"
set "DEFAULT_TARGET=ClientEditor"
set "DEFAULT_PLATFORM=Win64"
set "DEFAULT_CONFIG=Development"
set "OUTPUT_DIR=%PROJECT_DIR%CompileCommands"
set "OUTPUT_FILE=%OUTPUT_DIR%\compile_commands.json"

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
echo [UBT Gen ClangDB] Project:       %PROJECT_NAME%
echo [UBT Gen ClangDB] Target:        %TARGET%
echo [UBT Gen ClangDB] Platform:      %PLATFORM%
echo [UBT Gen ClangDB] Configuration: %CONFIG%
echo [UBT Gen ClangDB] Output Dir:    %OUTPUT_DIR%
echo [UBT Gen ClangDB] Engine:        %ENGINE_ROOT%
if defined EXTRA_ARGS echo [UBT Gen ClangDB] Extra Flags:   %EXTRA_ARGS%
echo ==============================================================================
if not exist "%OUTPUT_DIR%" mkdir "%OUTPUT_DIR%"
call "%BUILD_BAT%" -Mode=GenerateClangDatabase -Project="%PROJECT_FILE%" -Target="%TARGET% %PLATFORM% %CONFIG%" -OutputDir="%OUTPUT_DIR%" %EXTRA_ARGS%
set "GEN_EXIT_CODE=%ERRORLEVEL%"

rem Fallback: If UBT wrote compile_commands.json to Engine root or project root, move to CompileCommands
if not exist "%OUTPUT_FILE%" (
    if exist "%ENGINE_ROOT%\compile_commands.json" (
        echo [UBT Gen ClangDB] Moving compile_commands.json from Engine root to output dir...
        move /y "%ENGINE_ROOT%\compile_commands.json" "%OUTPUT_FILE%" >nul
    ) else if exist "%PROJECT_DIR%compile_commands.json" (
        echo [UBT Gen ClangDB] Moving compile_commands.json from project root to output dir...
        move /y "%PROJECT_DIR%compile_commands.json" "%OUTPUT_FILE%" >nul
    )
)

if exist "%OUTPUT_FILE%" (
    echo [UBT Gen ClangDB] Successfully generated compile_commands.json at:
    echo   "%OUTPUT_FILE%"
) else (
    echo [UBT Gen ClangDB] Failed to locate generated compile_commands.json.
    if %GEN_EXIT_CODE% equ 0 set "GEN_EXIT_CODE=1"
)

exit /b %GEN_EXIT_CODE%
