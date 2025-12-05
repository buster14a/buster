@echo off
setlocal EnableExtensions EnableDelayedExpansion

rem ----------------------------------------
rem Emulate: set -eu
rem ----------------------------------------

if not defined BUSTER_CI (
    set BUSTER_CI=0
)

rem If args != 0, enable command echo (set -x)
if not "%~1"=="" (
    echo on
)

rem ----------------------------------------
rem Detect clang
rem ----------------------------------------

if not defined CMAKE_PREFIX_PATH (
    for /f "delims=" %%i in ('where clang 2^>nul') do (
        set CLANG=%%i
        goto :clang_found
    )
    echo ERROR: clang not found
    exit /b 1
) else (
    set CLANG=%CMAKE_PREFIX_PATH%\bin\clang.exe
)

:clang_found

rem ----------------------------------------
rem Build step
rem ----------------------------------------

if not exist build (
    mkdir build
)

"%CLANG%" build.c ^
    -o build\builder.exe ^
    -Isrc ^
    -std=gnu2x ^
    -march=native ^
    -DBUSTER_UNITY_BUILD=1 ^
    -DBUSTER_USE_IO_RING=0 ^
    -DBUSTER_USE_PTHREAD=1 ^
    -g ^
    -Werror -Wall -Wextra -Wpedantic -pedantic ^
    -Wno-gnu-auto-type ^
    -Wno-gnu-empty-struct ^
    -Wno-bitwise-instead-of-logical ^
    -Wno-unused-function ^
    -Wno-gnu-flexible-array-initializer ^
    -Wno-missing-field-initializers ^
    -Wno-pragma-once-outside-header ^
    -ferror-limit=1

if errorlevel 1 (
    exit /b %ERRORLEVEL%
)

rem ----------------------------------------
rem Run builder
rem ----------------------------------------

set BUSTER_REGENERATE=1
build\builder.exe %*
exit /b %ERRORLEVEL%
