@echo off
setlocal enabledelayedexpansion

:: GT4Hooks - one build entry point for every game, region and platform.
::
::   build                          list the games, their regions and the platforms
::   build gt4o US for:pcsx2        build Gran Turismo 4 Online, US, for PCSX2
::   build gt4o US for:ps2          the same, for a real console
::   build tt EU for:ps2            build Tourist Trophy, Europe, for a real console
::   build clean tt US for:ps2      remove that build's output
::   build clean tt US              remove it for both platforms
::
::   build gt4o US for:pcsx2 out:C:\gt4\SCUS_974.36.elf
::                                  ... and copy the finished executable there
::
:: A build needs its platform. pcsx2 builds carry what only works in the
:: emulator - HostFS, and in Gran Turismo 4 Online the 128 MB mode - and ps2
:: builds leave it out. for: can go anywhere after the game.
::
:: out: is additive: the build output stays where it always goes and the
:: finished executable is copied to that path as well, so the copy the game
:: boots from is made by the build rather than by hand. A path naming a folder,
:: or ending with a backslash, takes the executable's own name. Quote the whole
:: argument if the path has spaces: "out:C:\my games\gt4\SCUS_974.36.elf".
:: The copy is size-checked afterwards.
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

:: ---- arguments -------------------------------------------------------------
:: `clean` leads, so it reads as a verb: build clean tt US. The region follows
:: the game; for:<platform> can come anywhere after them.
set "ACTION="
set "GAME="
set "REGION="
set "PLATFORM="
set "OUTCOPY="

:parse
if "%~1"=="" goto :parsed
set "ARG=%~1"
shift
if /I "%ARG:~0,4%"=="for:" (
    set "PLATFORM=%ARG:~4%"
    goto :parse
)
if /I "%ARG:~0,4%"=="out:" (
    if defined OUTCOPY (
        echo Only one out: can be given.
        goto :usage_fail
    )
    set "OUTCOPY=%ARG:~4%"
    if not defined OUTCOPY (
        echo out: needs a path - e.g. out:C:\gt4\SCUS_974.36.elf
        goto :usage_fail
    )
    goto :parse
)
if /I "%ARG%"=="clean" if not defined GAME if not defined ACTION (
    set "ACTION=clean"
    goto :parse
)
if not defined GAME (
    set "GAME=%ARG%"
    goto :parse
)
if not defined REGION (
    set "REGION=%ARG%"
    goto :parse
)
echo Unexpected argument "%ARG%".
goto :usage_fail
:parsed

if "%GAME%"=="" goto :list
if /I "%GAME%"=="--list" goto :list
if /I "%GAME%"=="-h" goto :usage
if /I "%GAME%"=="--help" goto :usage

if not exist "%ROOT%source\games\%GAME%\game.mk" (
    echo Unknown game "%GAME%".
    goto :list
)

:: The output folder is named after the platform, so spell it one way.
if /I "%PLATFORM%"=="ps2" set "PLATFORM=ps2"
if /I "%PLATFORM%"=="pcsx2" set "PLATFORM=pcsx2"
if defined PLATFORM if not "%PLATFORM%"=="ps2" if not "%PLATFORM%"=="pcsx2" (
    echo Unknown platform "for:%PLATFORM%" - use for:ps2 or for:pcsx2.
    exit /B 1
)

if not defined PLATFORM if /I not "%ACTION%"=="clean" (
    echo Which platform is this build for? Add one of:
    echo     for:pcsx2   PCSX2 - with HostFS, and the 128 MB mode where the game has it
    echo     for:ps2     a real console
    echo e.g.  build %GAME% %REGION% for:pcsx2
    exit /B 1
)

:: print-config needs a platform even when cleaning both, but nothing read
:: back here before the build depends on which.
set "CFGPLATFORM=%PLATFORM%"
if not defined CFGPLATFORM set "CFGPLATFORM=pcsx2"
set "MAKEARGS=GAME=%GAME% PLATFORM=%CFGPLATFORM%"
if not "%REGION%"=="" set "MAKEARGS=%MAKEARGS% REGION=%REGION%"

:: ---- learn the configuration from make -------------------------------------
set "BUILD="
set "GAME_ELF="
set "CLAMP_BSS="
set "BASE_ADDRESS="
set "PLUGIN_RESERVE="
set "CAVE2_ADDRESS="
set "CAVE2_RESERVE="
set "CAVE2B_ADDRESS="
set "CAVE2B_RESERVE="
set "CAVE2C_ADDRESS="
set "CAVE2C_RESERVE="
set "DEVRAM_ADDRESS="
set "DEVRAM_RESERVE="
set "STARTUP_SLOT="
set "STARTUP_CALL="

:: Written to a file rather than read through for /f backticks: the command has
:: to carry a quoted -File path, and cmd mangles that inside a backtick block.
:: Every variable make prints is set, PLATFORM included - which for a clean of
:: both platforms is only the stand-in above - so the choice is put back after.
set "WANTPLATFORM=%PLATFORM%"
set "CFGTMP=%TEMP%\gt4hooks_cfg_%RANDOM%.txt"
%PSRUN% "%VSMAKE%" %MAKEARGS% --no-print-directory -s print-config > "%CFGTMP%" 2>nul
for /f "usebackq tokens=1,* delims==" %%A in ("%CFGTMP%") do (
    set "%%A=%%B"
)
del /q "%CFGTMP%" >nul 2>&1
set "PLATFORM=%WANTPLATFORM%"

:: A blank BUILD would give the injector an empty -o, which is silently
:: destructive. Refuse rather than guess.
if "%BUILD%"=="" (
    echo ERROR: could not read the build configuration from make.
    echo        Region "%REGION%" may not be valid for game "%GAME%".
    exit /B 1
)

set "BUILDDIR=%WORKDIR%\out\%GAME%\%BUILD%"

:: Cleaning happens here rather than through make: make's `rm -rf` depends on
:: which shell it picks up, and a bare Windows box has no rm. Only the platform
:: folders go; anything else in the build's folder, such as output from before
:: builds were split by platform, is left alone.
if /I "%ACTION%"=="clean" (
    if defined OUTCOPY echo Note: out: is ignored for clean - nothing is built.
    if defined PLATFORM (
        call :clean_one "%BUILDDIR%\%PLATFORM%"
    ) else (
        call :clean_one "%BUILDDIR%\ps2"
        call :clean_one "%BUILDDIR%\pcsx2"
    )
    exit /B 0
)

set "OUTDIR=%BUILDDIR%\%PLATFORM%"
set "PLUGIN=%OUTDIR%\plugin.elf"

echo Building %GAME% / %BUILD% for %PLATFORM%  ^(%GAME_ELF%^)  base=%BASE_ADDRESS%

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
    if defined OUTCOPY echo out: had nothing to copy - no executable was produced.
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

:: ---- crt0's startup call ----------------------------------------------------
:: The injector hooks startup by turning crt0's `ei` into a jal to the plugin's
:: INVOKER, which makes the next word that jal's delay slot. Where that word is
:: crt0's own call into the game - Gran Turismo 4 Online, Tourist Trophy - the
:: slot holds a jump, and a PS2 runs that differently from PCSX2: PCSX2 enters
:: INVOKER, a PS2 takes crt0's call and never runs init(), so no hook goes in.
:: A game that names the call (STARTUP_SLOT/STARTUP_CALL_<region> in its
:: game.mk) has the slot made a nop here and makes the call from INVOKER; for
:: any other, a jump there is reported as a warning.
python "%ROOT%tools\startup_slot.py" "%OUTELF%" "%PLUGIN%" --base "%BASEIMG%" --slot "%STARTUP_SLOT%" --call "%STARTUP_CALL%"
if errorlevel 1 (
    del /q "%OUTELF%" >nul 2>&1
    echo Moving crt0's startup call failed - %GAME_ELF%.elf removed.
    exit /B 1
)

:: ---- more caves ----------------------------------------------------------------
:: The injector only knows about one address. A game that links parts of its
:: plugin elsewhere (CAVE2/CAVE2B/CAVE2C_<region> in its game.mk) gets them
:: written in here. Without them the game would jump into code nothing put
:: there, so a failure removes the output rather than leave a half-built ELF.
if not "%CAVE2_ADDRESS%"=="" (
    python "%ROOT%tools\inject_cave2.py" "%OUTELF%" "%PLUGIN%" --section .cave2 --addr %CAVE2_ADDRESS% --reserve %CAVE2_RESERVE% --base "%BASEIMG%"
    if errorlevel 1 (
        del /q "%OUTELF%" >nul 2>&1
        echo Adding .cave2 failed - %GAME_ELF%.elf removed.
        exit /B 1
    )
)
if not "%CAVE2B_ADDRESS%"=="" (
    python "%ROOT%tools\inject_cave2.py" "%OUTELF%" "%PLUGIN%" --section .cave2b --addr %CAVE2B_ADDRESS% --reserve %CAVE2B_RESERVE% --base "%BASEIMG%"
    if errorlevel 1 (
        del /q "%OUTELF%" >nul 2>&1
        echo Adding .cave2b failed - %GAME_ELF%.elf removed.
        exit /B 1
    )
)
if not "%CAVE2C_ADDRESS%"=="" (
    python "%ROOT%tools\inject_cave2.py" "%OUTELF%" "%PLUGIN%" --section .cave2c --addr %CAVE2C_ADDRESS% --reserve %CAVE2C_RESERVE% --base "%BASEIMG%"
    if errorlevel 1 (
        del /q "%OUTELF%" >nul 2>&1
        echo Adding .cave2c failed - %GAME_ELF%.elf removed.
        exit /B 1
    )
)

:: The dev RAM reserve, in pcsx2 builds of a game that has one. It lies past
:: the end of the game's file image, so it goes in as a segment of its own.
if /I "%PLATFORM%"=="pcsx2" if not "%DEVRAM_ADDRESS%"=="" (
    python "%ROOT%tools\inject_cave2.py" "%OUTELF%" "%PLUGIN%" --section .devram --addr %DEVRAM_ADDRESS% --reserve %DEVRAM_RESERVE% --base "%BASEIMG%"
    if errorlevel 1 (
        del /q "%OUTELF%" >nul 2>&1
        echo Adding .devram failed - %GAME_ELF%.elf removed.
        exit /B 1
    )
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

:: ---- the extra copy --------------------------------------------------------
:: out: is additive: the build output above stays where it is and the finished
:: executable goes to the given path as well.
if defined OUTCOPY (
    set "DEST=%OUTCOPY%"
    if "%OUTCOPY:~-1%"=="\" set "DEST=%OUTCOPY%%GAME_ELF%.elf"
    if exist "%OUTCOPY%\" set "DEST=%OUTCOPY%\%GAME_ELF%.elf"
    call :copy_out
    if errorlevel 1 exit /B 1
)
exit /B 0

:: ---------------------------------------------------------------------------
:: Copies %OUTELF% to %DEST% and checks the result. A copy that keeps a
:: previous build's length - a real failure seen before - reads as a bad build
:: later, so the sizes are compared here instead.
:copy_out
for %%D in ("%DEST%") do set "DESTDIR=%%~dpD"
if not exist "%DESTDIR%" (
    echo ERROR: no folder "%DESTDIR%" to copy into - nothing was copied.
    exit /B 1
)
copy /Y "%OUTELF%" "%DEST%" >nul
if errorlevel 1 (
    echo ERROR: could not copy to "%DEST%".
    exit /B 1
)
set "SRCSIZE="
set "DSTSIZE="
for %%I in ("%OUTELF%") do set "SRCSIZE=%%~zI"
for %%I in ("%DEST%") do set "DSTSIZE=%%~zI"
if not "%SRCSIZE%"=="%DSTSIZE%" (
    echo ERROR: the copy at "%DEST%" is %DSTSIZE% bytes, not %SRCSIZE%.
    exit /B 1
)
echo Also wrote %DEST%  %DSTSIZE% bytes
goto :eof

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
echo Platforms: ps2 ^(a real console^), pcsx2
echo.
echo Usage: build ^<game^> [region] for:^<platform^> [out:^<path^>]
echo        e.g. build gt4o US for:pcsx2
echo        build clean ^<game^> [region] [for:^<platform^>]
exit /B 0

:usage
echo Usage: build ^<game^> [region] for:^<platform^> [out:^<path^>]
echo        e.g. build gt4o US for:pcsx2
echo        build clean ^<game^> [region] [for:^<platform^>]
echo        build --list
echo Platforms: ps2 ^(a real console^), pcsx2
echo.
echo out: also copies the finished executable to ^<path^>, as well as writing it
echo to the usual place. A path naming a folder takes the executable's own name.
echo Quote it if it has spaces: "out:C:\my games\gt4\SCUS_974.36.elf"
exit /B 0

:usage_fail
call :usage
exit /B 1

:clean_one
if exist "%~1" (
    rmdir /s /q "%~1"
    echo Removed %~1
) else (
    echo Nothing to clean at %~1
)
goto :eof
