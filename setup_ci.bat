@echo off
setlocal EnableExtensions EnableDelayedExpansion

REM ---- Preconditions (GitHub Actions provides these) ----
if not defined GITHUB_WORKSPACE (
  echo ERROR: GITHUB_WORKSPACE is not set.
  exit /b 1
)
if not defined GITHUB_ENV (
  echo ERROR: GITHUB_ENV is not set.
  exit /b 1
)

REM ---- Build setup_ci.exe ----
REM Equivalent to: clang -std=gnu2x -g -o $GITHUB_WORKSPACE/setup_ci $GITHUB_WORKSPACE/setup_ci.c
clang -std=gnu2x -g -o "%GITHUB_WORKSPACE%\setup_ci.exe" "%GITHUB_WORKSPACE%\setup_ci.c" || exit /b 1

echo %GITHUB_WORKSPACE%

REM ---- Run setup_ci to generate helper file ----
REM Batch cannot "source helper.sh", so generate helper.bat instead.
"%GITHUB_WORKSPACE%\setup_ci.exe" "%GITHUB_WORKSPACE%\helper.bat" || exit /b 1

dir /b
type "%GITHUB_WORKSPACE%\helper.bat"

REM ---- Load helper (sets env vars in current process) ----
call "%GITHUB_WORKSPACE%\helper.bat" || exit /b 1

REM ---- Export to GitHub Actions environment ----
>> "%GITHUB_ENV%" echo BUSTER_OS=%BUSTER_OS%
>> "%GITHUB_ENV%" echo BUSTER_ARCH=%BUSTER_ARCH%

REM ---- CPU model printing (roughly matching your per-OS branches) ----
REM On Windows runners, prefer CIM via PowerShell (wmic may be missing/deprecated).
powershell -NoProfile -Command ^
  "(Get-CimInstance Win32_Processor | Select-Object -First 1 -ExpandProperty Name)" || (
    wmic cpu get name
  )

REM ---- Toolchain paths (Windows-safe) ----
REM Your bash used: $HOME/dev/toolchain/install
if not defined USERPROFILE (
  echo ERROR: USERPROFILE is not set.
  exit /b 1
)
set "BUSTER_TOOLCHAIN_INSTALL_PATH=%USERPROFILE%\dev\toolchain\install"
set "BUSTER_TOOLCHAIN_BASENAME=llvm_%BUSTER_LLVM_VERSION%_%BUSTER_ARCH%-%BUSTER_OS%-Release"
set "BUSTER_TOOLCHAIN_7Z=%BUSTER_TOOLCHAIN_BASENAME%.7z"
set "BUSTER_TOOLCHAIN_ABSOLUTE_PATH=%BUSTER_TOOLCHAIN_INSTALL_PATH%\%BUSTER_TOOLCHAIN_BASENAME%"

echo Buster arch %BUSTER_ARCH%
echo Buster os %BUSTER_OS%
echo BUSTER_TOOLCHAIN_INSTALL_PATH %BUSTER_TOOLCHAIN_INSTALL_PATH%
echo BUSTER_TOOLCHAIN_BASENAME %BUSTER_TOOLCHAIN_BASENAME%
echo BUSTER_TOOLCHAIN_7Z %BUSTER_TOOLCHAIN_7Z%
echo BUSTER_TOOLCHAIN_ABSOLUTE_PATH %BUSTER_TOOLCHAIN_ABSOLUTE_PATH%

REM ---- Download & extract if missing ----
if not exist "%BUSTER_TOOLCHAIN_ABSOLUTE_PATH%" (
  set "BUSTER_TOOLCHAIN_URL=https://github.com/buster14a/toolchain/releases/download/v%BUSTER_LLVM_VERSION%/%BUSTER_TOOLCHAIN_7Z%"

  echo Downloading !BUSTER_TOOLCHAIN_URL!
  curl -L -o "!BUSTER_TOOLCHAIN_7Z!" "!BUSTER_TOOLCHAIN_URL!" || exit /b 1

  if not exist "!BUSTER_TOOLCHAIN_INSTALL_PATH!" mkdir "!BUSTER_TOOLCHAIN_INSTALL_PATH!" || exit /b 1

  echo Extracting !BUSTER_TOOLCHAIN_7Z! to !BUSTER_TOOLCHAIN_INSTALL_PATH!
  7z x "!BUSTER_TOOLCHAIN_7Z!" -o"!BUSTER_TOOLCHAIN_INSTALL_PATH!" -y || exit /b 1
)

REM ---- Export CLANG to point into toolchain ----
set "CLANG=%BUSTER_TOOLCHAIN_ABSOLUTE_PATH%\bin\clang.exe"
>> "%GITHUB_ENV%" echo CLANG=%CLANG%

echo CLANG=%CLANG%

exit /b 0

