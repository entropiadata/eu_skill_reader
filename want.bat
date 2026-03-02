@echo off
setlocal enabledelayedexpansion
cd /d "%~dp0"

REM ============================================================================
REM EU Skill Reader - Task Runner
REM
REM Usage: want <command> [args...]
REM Run without arguments to see available commands.
REM ============================================================================

REM ========================== Command Definitions =============================
REM
REM Add commands here. Each command needs two things:
REM   1. A function :cmd_<name> with the implementation (use exit /b for return)
REM   2. An entry in :show_help for the usage listing
REM
REM Commands can call other commands with: call :cmd_<name> [args...]
REM The caller can check !ERRORLEVEL! after the call to detect failure.
REM
REM Available commands:
REM   build [Release|Publish]   - Configure and build via CMake
REM   clean                     - Remove build artifacts
REM   test                      - Run all test suites
REM   test:synth                - Run synthetic digit tests only
REM   test:png                  - Run PNG regression tests only
REM   format                    - Format source files with clang-format
REM   pub                       - Build Publish, fail on warnings, copy to dist/
REM   hash:release              - Print SHA-512 of Release executable
REM   hash:publish              - Print SHA-512 of Publish executable
REM   hash:debug                - Print SHA-512 of Debug executable
REM
REM ==========================================================================

if "%~1"=="" call :show_help & exit /b 0

REM Route to command function
if /i "%~1"=="build"        call :cmd_build %2 & exit /b !ERRORLEVEL!
if /i "%~1"=="clean"        call :cmd_clean & exit /b !ERRORLEVEL!
if /i "%~1"=="test"         call :cmd_test & exit /b !ERRORLEVEL!
if /i "%~1"=="test:synth"   call :cmd_test_synth & exit /b !ERRORLEVEL!
if /i "%~1"=="test:png"     call :cmd_test_png & exit /b !ERRORLEVEL!
if /i "%~1"=="format"       call :cmd_format & exit /b !ERRORLEVEL!
if /i "%~1"=="pub"          call :cmd_pub & exit /b !ERRORLEVEL!
if /i "%~1"=="hash:release" call :cmd_hash_release & exit /b !ERRORLEVEL!
if /i "%~1"=="hash:publish" call :cmd_hash_publish & exit /b !ERRORLEVEL!
if /i "%~1"=="hash:debug"   call :cmd_hash_debug & exit /b !ERRORLEVEL!

echo Unknown command: %~1
echo.
call :show_help
exit /b 1

REM ========================== Command Functions ================================

:cmd_build
    set CONFIG=%~1
    if "%CONFIG%"=="" set CONFIG=Release
    echo [build] Configuring with CMake...
    cmake -B build -G "Visual Studio 17 2022" -A x64
    if !ERRORLEVEL! NEQ 0 (
        echo [build] Trying VS 2019...
        cmake -B build -G "Visual Studio 16 2019" -A x64
    )
    if !ERRORLEVEL! NEQ 0 (
        echo [build] CMake configuration failed.
        exit /b 1
    )
    echo [build] Building %CONFIG%...
    cmake --build build --config %CONFIG%
    if !ERRORLEVEL! NEQ 0 (
        echo [build] Build failed.
        exit /b 1
    )
    echo [build] Success: build\%CONFIG%\eu_skill_reader.exe
    exit /b 0

:cmd_clean
    echo [clean] Removing build artifacts...
    if exist "build\Release" rmdir /s /q "build\Release"
    if exist "build\Debug" rmdir /s /q "build\Debug"
    del *.obj 2>nul
    echo [clean] Done.
    exit /b 0

:cmd_test
    call :cmd_test_synth
    if !ERRORLEVEL! NEQ 0 exit /b 1
    echo.
    call :cmd_test_png
    if !ERRORLEVEL! NEQ 0 exit /b 1
    exit /b 0

:cmd_test_synth
    echo [test:synth] Running synthetic digit tests...
    if not exist "build\Release\test_numbers.exe" (
        echo [test:synth] Not found. Building first...
        call :cmd_build Release
        if !ERRORLEVEL! NEQ 0 exit /b 1
    )
    build\Release\test_numbers.exe
    if !ERRORLEVEL! NEQ 0 (
        echo [test:synth] FAILED
        exit /b 1
    )
    exit /b 0

:cmd_test_png
    echo [test:png] Running PNG regression tests...
    if not exist "build\Release\test_png_numbers.exe" (
        echo [test:png] Not found. Building first...
        call :cmd_build Release
        if !ERRORLEVEL! NEQ 0 exit /b 1
    )
    build\Release\test_png_numbers.exe "%~dp0."
    if !ERRORLEVEL! NEQ 0 (
        echo [test:png] FAILED
        exit /b 1
    )
    exit /b 0

:cmd_format
    echo [format] Formatting source files...
    set CLANG_FMT=
    where clang-format >nul 2>&1
    if !ERRORLEVEL! EQU 0 (
        set "CLANG_FMT=clang-format"
    ) else if exist "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\Llvm\bin\clang-format.exe" (
        set "CLANG_FMT=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\Llvm\bin\clang-format.exe"
    ) else if exist "C:\Program Files\LLVM\bin\clang-format.exe" (
        set "CLANG_FMT=C:\Program Files\LLVM\bin\clang-format.exe"
    )
    if "!CLANG_FMT!"=="" (
        echo [format] clang-format.exe not found. Install LLVM or Visual Studio 2022.
        exit /b 1
    )
    "!CLANG_FMT!" -i src\*.cpp src\*.h
    echo [format] Done.
    exit /b 0

:cmd_pub
    echo [pub] Cleaning dist\...
    if exist "dist" rmdir /s /q "dist"
    mkdir dist
    echo [pub] Building Publish (warnings as errors)...
    call :cmd_build Publish
    if !ERRORLEVEL! NEQ 0 (
        echo [pub] Build failed. Fix errors/warnings before publishing.
        exit /b 1
    )
    echo [pub] Copying to dist\...
    copy /y "build\Publish\eu_skill_reader.exe" "dist\eu_skill_reader.exe" >nul
    if !ERRORLEVEL! NEQ 0 (
        echo [pub] Failed to copy executable.
        exit /b 1
    )
    echo [pub] Generating SHA-512 hash...
    certutil -hashfile "dist\eu_skill_reader.exe" SHA512 | findstr /v "hash" > "dist\eu_skill_reader.sha512"
    echo [pub] Done: dist\eu_skill_reader.exe
    echo [pub] Hash: dist\eu_skill_reader.sha512
    type "dist\eu_skill_reader.sha512"
    exit /b 0

:cmd_hash_release
    if not exist "build\Release\eu_skill_reader.exe" (
        echo [hash:release] No build found. Run: want build
        exit /b 1
    )
    echo [hash:release] SHA-512 of build\Release\eu_skill_reader.exe:
    certutil -hashfile build\Release\eu_skill_reader.exe SHA512 | findstr /v "hash"
    exit /b 0

:cmd_hash_publish
    if not exist "build\Publish\eu_skill_reader.exe" (
        echo [hash:publish] No build found. Run: want build Publish
        exit /b 1
    )
    echo [hash:publish] SHA-512 of build\Publish\eu_skill_reader.exe:
    certutil -hashfile build\Publish\eu_skill_reader.exe SHA512 | findstr /v "hash"
    exit /b 0

:cmd_hash_debug
    if not exist "build\Debug\eu_skill_reader.exe" (
        echo [hash:debug] No build found. Run: want build Debug
        exit /b 1
    )
    echo [hash:debug] SHA-512 of build\Debug\eu_skill_reader.exe:
    certutil -hashfile build\Debug\eu_skill_reader.exe SHA512 | findstr /v "hash"
    exit /b 0

REM ========================== Help ============================================

:show_help
    echo EU Skill Reader - Task Runner
    echo.
    echo Usage: want ^<command^> [args...]
    echo.
    echo Commands:
    echo   build [Release^|Publish]  Build the project via CMake (default: Release)
    echo   clean                     Remove build artifacts
    echo   test                      Run all test suites
    echo   test:synth                Run synthetic digit tests (149 tests)
    echo   test:png                  Run PNG regression tests (339 rows)
    echo   format                    Format source files with clang-format
    echo   pub                       Build Publish, fail on warnings, copy to dist/
    echo   hash:release              Print SHA-512 of Release executable
    echo   hash:publish              Print SHA-512 of Publish executable
    echo   hash:debug                Print SHA-512 of Debug executable
    exit /b 0
