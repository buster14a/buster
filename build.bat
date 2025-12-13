@echo off
setlocal EnableExtensions EnableDelayedExpansion

REM ------------------------------------------------------------
REM Defaults (equivalent to ${VAR:-})
REM ------------------------------------------------------------

if not defined BUSTER_CI (
    set BUSTER_CI=0
)

if not defined BUSTER_OPTIMIZE (
    set BUSTER_OPTIMIZE=0
)

if not defined BUSTER_LLVM_VERSION (
    set BUSTER_LLVM_VERSION=21.1.7
)

REM ------------------------------------------------------------
REM Toolchain / CI setup
REM ------------------------------------------------------------

if not defined CMAKE_PREFIX_PATH (
    if "%BUSTER_CI%"=="1" (
        call setup_ci.bat || goto :error
    )
)

if not defined BUSTER_ARCH (
    if /I "%PROCESSOR_ARCHITECTURE%"=="ARM64" (
        set BUSTER_ARCH=aarch64
    )

    if /I "%PROCESSOR_ARCHITECTURE%"=="AMD64" (
        set BUSTER_ARCH=x86_64
    )
)

if not defined BUSTER_OS (
    set BUSTER_OS=windows
)

if not defined CMAKE_PREFIX_PATH (
    set CMAKE_PREFIX_PATH=%USERPROFILE%\dev\toolchain\install\llvm_%BUSTER_LLVM_VERSION%_%BUSTER_ARCH%-%BUSTER_OS%-Release
)

if not defined CLANG (
    set CLANG=%CMAKE_PREFIX_PATH%\bin\clang.exe
)

REM ------------------------------------------------------------
REM Argument tracing (set -x equivalent)
REM ------------------------------------------------------------

if not "%~1"=="" (
    echo on
)

REM ------------------------------------------------------------
REM macOS SDK logic (no-op on Windows)
REM ------------------------------------------------------------

set XC_SDK_PATH=
set CLANG_EXTRA_FLAGS=

REM ------------------------------------------------------------
REM Build
REM ------------------------------------------------------------

if not exist build (
    mkdir build || goto :error
)

"%CLANG%" build.c ^
    -o build\builder.exe ^
    -fuse-ld=lld ^
    %CLANG_EXTRA_FLAGS% ^
    -nostdlib ^
    -lkernel32 ^
    -lws2_32 ^
    -Wl,-entry:mainCRTStartup ^
    -Wl,-subsystem:console ^
    -Isrc ^
    -std=gnu2x ^
    -march=native ^
    -DBUSTER_UNITY_BUILD=1 ^
    -DBUSTER_USE_IO_RING=0 ^
    -DBUSTER_USE_PTHREAD=1 ^
    -g ^
    -Werror ^
    -Wall ^
    -Wextra ^
    -Wpedantic ^
    -pedantic ^
    -Wno-language-extension-token ^
    -Wno-gnu-auto-type ^
    -Wno-gnu-empty-struct ^
    -Wno-bitwise-instead-of-logical ^
    -Wno-unused-function ^
    -Wno-gnu-flexible-array-initializer ^
    -Wno-missing-field-initializers ^
    -Wno-pragma-once-outside-header ^
    -fwrapv ^
    -fno-strict-aliasing ^
    -funsigned-char ^
    -ferror-limit=1 || goto :error

REM ------------------------------------------------------------
REM Run builder if build succeeded
REM ------------------------------------------------------------

set BUSTER_REGENERATE=1
build\builder.exe %*
set EXIT_CODE=%ERRORLEVEL%

exit /b %EXIT_CODE%

:error
echo Build failed.
exit /b 1
