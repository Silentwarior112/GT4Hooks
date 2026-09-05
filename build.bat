@echo off
setlocal enabledelayedexpansion

:: GT4Hooks - one build entry point for every game and region.
::
::   build                 list the games and their regions
::   build gt4o US         build Gran Turismo 4 Online, US
::   build tt EU           build Tourist Trophy, Europe
::   build clean tt US     remove that build's output
::
:: Everything game-specific is read back from `make print-config`, so this file
:: contains no address, ELF name, region list or game name.
::
:: Nothing binary lives in this repository. Two directories outside it are used,
:: both overridable from the environment:
::
::   PS2SDK_HOME   the ps2sdk checkout, with its bundled EE toolchain
::                 default: ..\ps2sdk-main
::   WORK          base images, build output and the plugin injector
::                 default: ..\GT4Hooks-work
::
:: See the README for what belongs in each.

set "ROOT=%~dp0"

:: Read the overrides if set, else fall back to siblings of the repo. These are
:: copied into differently-named locals on purpose: exporting these back into
:: make would hand it Windows backslash separators.
set "SDKDIR=%PS2SDK_HOME%"
if not defined SDKDIR set "SDKDIR=%ROOT%..\ps2sdk-main"
set "WORKDIR=%WORK%"
if not defined WORKDIR set "WORKDIR=%ROOT%..\GT4Hooks-work"

:: Kept as two variables on purpose: folding the -File path into one quoted
:: value nests quotes, which cmd mangles inside for /f.
set "PSRUN=powershell -NoProfile -ExecutionPolicy Bypass -File"
set "VSMAKE=%SDKDIR%\ee\bin\vsmake.ps1"

if not exist "%VSMAKE%" (
    echo ERROR: no ps2sdk at "%SDKDIR%".
    echo        GT4Hooks does not vendor the SDK - see the README.
    echo        Set PS2SDK_HOME to your checkout, or put it at ..\ps2sdk-main
    exit /B 1
)

:: `clean` leads, so it reads as a verb: build clean tt US
set "ACTION="
if /I "%~1"=="clean" (
    set "ACTION=clean"
    set "GAME=%~2"
    set "REGION=%~3"
) else (
    set "GAME=%~1"
    set "REGION=%~2"
)

if "%GAME%"=="" goto :list
if /I "%GAME%"=="--list" goto :list
if /I "%GAME%"=="-h" goto :usage
if /I "%GAME%"=="--help" goto :usage

if not exist "%ROOT%source\games\%GAME%\game.mk" (
    echo Unknown game "%GAME%".
    goto :list
)

set "MAKEARGS=GAME=%GAME%"
if not "%REGION%"=="" set "MAKEARGS=%MAKEARGS% REGION=%REGION%"

:: ---- learn the configuration from make -------------------------------------
set "BUILD="
set "GAME_ELF="
set "CLAMP_BSS="
set "BASE_ADDRESS="
set "PLUGIN_RESERVE="

:: Written to a file rather than read through for /f backticks: the command has
:: to carry a quoted -File path, and cmd mangles that inside a backtick block.
set "CFGTMP=%TEMP%\gt4hooks_cfg_%RANDOM%.txt"
%PSRUN% "%VSMAKE%" %MAKEARGS% --no-print-directory -s print-config > "%CFGTMP%" 2>nul
for /f "usebackq tokens=1,* delims==" %%A in ("%CFGTMP%") do (
    set "%%A=%%B"
)
del /q "%CFGTMP%" >nul 2>&1

:: A blank BUILD would give the injector an empty -o, which is silently
:: destructive. Refuse rather than guess.
if "%BUILD%"=="" (
    echo ERROR: could not read the build configuration from make.
    echo        Region "%REGION%" may not be valid for game "%GAME%".
    exit /B 1
)

set "OUTDIR=%WORKDIR%\out\%GAME%\%BUILD%"
set "PLUGIN=%OUTDIR%\plugin.elf"

:: Cleaning happens here rather than through make: make's `rm -rf` depends on
:: which shell it picks up, and a bare Windows box has no rm.
if /I "%ACTION%"=="clean" (
    if exist "%OUTDIR%" (
        rmdir /s /q "%OUTDIR%"
        echo Removed %OUTDIR%
    ) else (
        echo Nothing to clean at %OUTDIR%
    )
    exit /B 0
)

echo Building %GAME% / %BUILD%  ^(%GAME_ELF%^)  base=%BASE_ADDRESS%

if not exist "%OUTDIR%\obj" mkdir "%OUTDIR%\obj" 2>nul

:: ---- compile + link --------------------------------------------------------
%PSRUN% "%VSMAKE%" %MAKEARGS%
if not exist "%PLUGIN%" (
    echo Build failed - plugin.elf was not produced.
    exit /B 1
)

for %%I in ("%PLUGIN%") do set "PLUGIN_SIZE=%%~zI"
echo Plugin: %PLUGIN%  %PLUGIN_SIZE% bytes  ^(reserve %PLUGIN_RESERVE%^)

:: ---- inject ----------------------------------------------------------------
set "BASEIMG=%WORKDIR%\bases\BASE_%BUILD%"
set "INJECTOR=%WORKDIR%\ps2plugininjector.exe"

if not exist "%BASEIMG%" (
    echo.
    echo No base image at "%BASEIMG%" - stopping after the plugin build.
    echo Create it with PDTools.GT4ElfBuilderTool:
    echo     GT4ElfBuilderTool.exe ^<CORE file^> "%BASEIMG%"
    exit /B 0
)
if not exist "%INJECTOR%" (
    echo ps2plugininjector.exe not found at "%INJECTOR%"
    exit /B 1
)

set "OUTELF=%OUTDIR%\%GAME_ELF%.elf"
del /q "%OUTELF%" >nul 2>&1

:: Note: ps2plugininjector returns a non-zero exit code even on success, so the
:: output file is what we check.
"%INJECTOR%" -i "%PLUGIN%" -o "%OUTELF%" "%BASEIMG%"
if not exist "%OUTELF%" (
    echo Injection failed - %GAME_ELF%.elf was not produced.
    exit /B 1
)

:: ---- optional .bss memsz clamp --------------------------------------------
if "%CLAMP_BSS%"=="1" (
    if exist "%ROOT%tools\clamp_bss.py" (
        python "%ROOT%tools\clamp_bss.py" "%OUTELF%" --base %BASE_ADDRESS%
    ) else (
        echo NOTE: tools\clamp_bss.py not found, skipping .bss memsz clamp.
    )
)

echo.
echo Wrote %OUTELF%
exit /B 0

:: ---------------------------------------------------------------------------
:list
echo GT4Hooks - games found in source\games\:
echo.
for /d %%S in ("%ROOT%source\games\*") do (
    if exist "%%S\game.mk" (
        set "NAME=%%~nxS"
        set "LONG="
        set "REGS="
        for /f "usebackq tokens=1,* delims=:=" %%A in (`findstr /b /c:"GAME_LONGNAME" /c:"REGIONS " "%%S\game.mk"`) do (
            set "K=%%A"
            set "V=%%B"
            if "!K:~0,13!"=="GAME_LONGNAME" set "LONG=!V!"
            if "!K:~0,7!"=="REGIONS" set "REGS=!V!"
        )
        echo   !NAME!  -  !LONG!
        echo        regions:!REGS!
    )
)
echo.
echo Usage: build ^<game^> [region]      e.g. build gt4o US
echo        build clean ^<game^> [region]
exit /B 0

:usage
echo Usage: build ^<game^> [region]      e.g. build gt4o US
echo        build clean ^<game^> [region]
echo        build --list
exit /B 0
