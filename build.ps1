#!/usr/bin/env pwsh
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# -----------------------------
# Load .env (classical KEY=VALUE)
# Supports:
#  - blank lines
#  - # comments (whole-line)
#  - optional leading "export "
#  - quoted values "..." or '...'
#  - values may contain '='
# -----------------------------
function Import-DotEnv([string]$Path) {
  if (-not (Test-Path -LiteralPath $Path)) {
    throw "error: $Path not found"
  }

  Get-Content -LiteralPath $Path | ForEach-Object {
    $line = $_.Trim()
    if ($line.Length -eq 0) { return }
    if ($line.StartsWith('#')) { return }

    if ($line.StartsWith('export ')) {
      $line = $line.Substring(7).Trim()
    }

    # split on first '='
    $eq = $line.IndexOf('=')
    if ($eq -lt 1) { return }

    $key = $line.Substring(0, $eq).Trim()
    $val = $line.Substring($eq + 1).Trim()

    # strip surrounding quotes
    if (($val.Length -ge 2) -and (
        ($val.StartsWith('"') -and $val.EndsWith('"')) -or
        ($val.StartsWith("'") -and $val.EndsWith("'"))
      )) {
      $val = $val.Substring(1, $val.Length - 2)
    }

    [System.Environment]::SetEnvironmentVariable($key, $val, 'Process')
  }
}

Import-DotEnv "build.env"

# -----------------------------
# Defaults (match Bash)
# -----------------------------
$BUSTER_OPTIMIZE          = 0
$BUSTER_FUZZ              = 0
$BUSTER_CI                = 0
$BUSTER_DOWNLOAD_TOOLCHAIN= 0
$BUSTER_SELF_HOSTED       = 0
$BUSTER_COMMAND           = 'build'

# -----------------------------
# Args: first arg is command, rest are --key=value
# -----------------------------
if ($args.Count -ge 1) {
    $BUSTER_COMMAND = $args[0]
    Set-PSDebug -Trace 1
}

foreach ($arg in $args[1..($args.Count-1)]) {
  if ($args.Count -lt 2) { break }

  switch -Regex ($arg) {
    '^--optimize=(.+)$'           { $BUSTER_OPTIMIZE           = [int]$Matches[1]; break }
    '^--fuzz=(.+)$'               { $BUSTER_FUZZ               = [int]$Matches[1]; break }
    '^--ci=(.+)$'                 { $BUSTER_CI                 = [int]$Matches[1]; break }
    '^--download_toolchain=(.+)$' { $BUSTER_DOWNLOAD_TOOLCHAIN = [int]$Matches[1]; break }
    '^--self-hosted=(.+)$'        { $BUSTER_SELF_HOSTED        = [int]$Matches[1]; break }
    default { throw "error: unknown argument: $arg" }
  }
}

# -----------------------------
# Detect arch/os (Windows)
# -----------------------------
$BUSTER_OS = 'windows'

# Prefer PROCESSOR_ARCHITEW6432 when present (32-bit pwsh on 64-bit OS)
$nativeArch = $env:PROCESSOR_ARCHITEW6432
if (-not $nativeArch) { $nativeArch = $env:PROCESSOR_ARCHITECTURE }

switch ($nativeArch.ToUpperInvariant()) {
  'AMD64' { $BUSTER_ARCH = 'x86_64' }
  'ARM64' { $BUSTER_ARCH = 'aarch64' }
  default { throw "error: unknown CPU architecture: $nativeArch" }
}

# -----------------------------
# CI logging
# -----------------------------
if ($BUSTER_CI -eq 1) {
  Write-Host "Command: '$BUSTER_COMMAND'"
  try {
    $cpuName = (Get-CimInstance Win32_Processor | Select-Object -First 1 -ExpandProperty Name)
    Write-Host "CPU: $cpuName"
  } catch {}
}

# -----------------------------
# Toolchain naming/paths
# -----------------------------
$maj = $env:BUSTER_LLVM_VERSION_MAJOR
$min = $env:BUSTER_LLVM_VERSION_MINOR
$rev = $env:BUSTER_LLVM_VERSION_REVISION

if (-not $maj -or -not $min -or -not $rev) {
  throw "error: BUSTER_LLVM_VERSION_MAJOR/MINOR/REVISION must be set in build.env"
}

$BUSTER_LLVM_VERSION_STRING = "$maj.$min.$rev"
$BUSTER_TOOLCHAIN_BASENAME  = "llvm_${BUSTER_LLVM_VERSION_STRING}_${BUSTER_ARCH}-${BUSTER_OS}-Release"
$BUSTER_TOOLCHAIN_INSTALL_PATH = Join-Path $env:USERPROFILE 'dev\toolchain\install'
$BUSTER_TOOLCHAIN_ABSOLUTE_PATH = Join-Path $BUSTER_TOOLCHAIN_INSTALL_PATH $BUSTER_TOOLCHAIN_BASENAME
$BUSTER_TOOLCHAIN_ABSOLUTE_PATH = $BUSTER_TOOLCHAIN_ABSOLUTE_PATH -replace '\\', '/'

# Force removing directory if download forced
if ($BUSTER_DOWNLOAD_TOOLCHAIN -eq 1) {
  if (Test-Path -LiteralPath $BUSTER_TOOLCHAIN_ABSOLUTE_PATH) {
    Remove-Item -LiteralPath $BUSTER_TOOLCHAIN_ABSOLUTE_PATH -Recurse -Force -ErrorAction SilentlyContinue
  }
}

# CI implies download if the directory is missing unless self-hosted
if (($BUSTER_CI -eq 1) -and ($BUSTER_SELF_HOSTED -eq 0)) {
    if (-not (Test-Path $BUSTER_TOOLCHAIN_ABSOLUTE_PATH -PathType Container)) {
        $BUSTER_DOWNLOAD_TOOLCHAIN = 1
    }
}

# Download + extract toolchain
if ($BUSTER_DOWNLOAD_TOOLCHAIN -eq 1) {
  $toolchain7z  = "$BUSTER_TOOLCHAIN_BASENAME.7z"
  $toolchainUrl = "https://github.com/buster14a/toolchain/releases/download/v$BUSTER_LLVM_VERSION_STRING/$toolchain7z"

  New-Item -ItemType Directory -Force -Path $BUSTER_TOOLCHAIN_INSTALL_PATH | Out-Null

  Invoke-WebRequest -Uri $toolchainUrl -OutFile $toolchain7z -UseBasicParsing

  & 7z x $toolchain7z "-o$BUSTER_TOOLCHAIN_INSTALL_PATH" -y | Out-Null
  if ($LASTEXITCODE -ne 0) { throw "error: failed to extract $toolchain7z" }

  Remove-Item -LiteralPath $toolchain7z -Force -ErrorAction SilentlyContinue
}

$BUSTER_CLANG_ABSOLUTE_PATH = Join-Path $BUSTER_TOOLCHAIN_ABSOLUTE_PATH 'bin/clang.exe'
$BUSTER_CLANG_ABSOLUTE_PATH = $BUSTER_CLANG_ABSOLUTE_PATH -replace '\\', '/'
if (-not (Test-Path -LiteralPath $BUSTER_CLANG_ABSOLUTE_PATH)) {
  throw "error: clang not found at: $BUSTER_CLANG_ABSOLUTE_PATH"
}

# -----------------------------
# Build builder
# -----------------------------
$buildDir = 'build'
New-Item -ItemType Directory -Force -Path $buildDir | Out-Null

$builderExe = Join-Path $buildDir 'builder.exe'

$clangArgs = @(
  'build.c',
  '-o', $builderExe,
  '-fuse-ld=lld',
  "-DBUSTER_TOOLCHAIN_ABSOLUTE_PATH=`"$BUSTER_TOOLCHAIN_ABSOLUTE_PATH`"",
  "-DBUSTER_CLANG_ABSOLUTE_PATH=`"$BUSTER_CLANG_ABSOLUTE_PATH`"",
  '-O0', '-Isrc', '-std=gnu2x', '-march=native',
  '-DBUSTER_UNITY_BUILD=1',
  '-DBUSTER_USE_IO_RING=0',
  '-DBUSTER_USE_PTHREAD=1',
  '-DBUSTER_INCLUDE_TESTS=1',
  '-Werror', '-Wall', '-Wextra', '-Wpedantic', '-pedantic',
  '-Wno-language-extension-token',
  '-Wno-gnu-auto-type',
  '-Wno-gnu-empty-struct',
  '-Wno-bitwise-instead-of-logical',
  '-Wno-unused-function',
  '-Wno-gnu-flexible-array-initializer',
  '-Wno-missing-field-initializers',
  '-Wno-pragma-once-outside-header',
  '-fwrapv', '-fno-strict-aliasing', '-funsigned-char', '-ferror-limit=1', '-g'
  '-lws2_32'
)

$clangArgs | ForEach-Object {
    Write-Host "ARG: [$($_)]"
}
& $BUSTER_CLANG_ABSOLUTE_PATH @clangArgs
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

# -----------------------------
# Run builder
# -----------------------------
& $builderExe $BUSTER_COMMAND "--optimize=$BUSTER_OPTIMIZE" "--fuzz=$BUSTER_FUZZ" "--ci=$BUSTER_CI"
exit $LASTEXITCODE
