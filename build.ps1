#!/usr/bin/env pwsh
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# Don't put left braces into a new line, otherwise Powershell will do weird stuff
if (!(Test-Path variable:BUSTER_LLVM_VERSION_MAJOR)) {
    Get-Content -LiteralPath "build.env" | ForEach-Object {
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
    $rhs = $line.Substring($eq + 1).Trim()

    function Parse-BashArrayRhs {
        param([Parameter(Mandatory=$true)][string]$Text)

        # Expect something like: ( ... )
        $t = $Text.Trim()
        if ($t.Length -lt 2 -or $t[0] -ne '(') { return $null }

        # Find matching closing ')' (allowing quotes/escapes)
        $i = 1
        $inSingle = $false
        $inDouble = $false
        $escaped  = $false

        while ($i -lt $t.Length) {
            $ch = $t[$i]

            if ($escaped) { $escaped = $false; $i++; continue }

            if (-not $inSingle -and $ch -eq '\') { $escaped = $true; $i++; continue }

            if (-not $inDouble -and $ch -eq "'" ) { $inSingle = -not $inSingle; $i++; continue }
            if (-not $inSingle -and $ch -eq '"' ) { $inDouble = -not $inDouble; $i++; continue }

            if (-not $inSingle -and -not $inDouble -and $ch -eq ')') { break }

            $i++
        }

        if ($i -ge $t.Length -or $t[$i] -ne ')') {
            throw "Unterminated bash array RHS: $Text"
        }

        $inner = $t.Substring(1, $i - 1) # between '(' and ')'

        # Tokenize inner elements (bash-like, practical subset)
        $tokens = New-Object System.Collections.Generic.List[string]
        $sb = New-Object System.Text.StringBuilder
        $inSingle = $false
        $inDouble = $false
        $escaped  = $false

        for ($j = 0; $j -lt $inner.Length; $j++) {
            $ch = $inner[$j]

            if ($escaped) {
                # In bash, backslash escapes next char (outside single quotes; in double quotes it's partial).
                [void]$sb.Append($ch)
                $escaped = $false
                continue
            }

            if ($inSingle) {
                if ($ch -eq "'") {
                    $inSingle = $false
                } else {
                    [void]$sb.Append($ch)
                }
                continue
            }

            if ($inDouble) {
                if ($ch -eq '"') {
                    $inDouble = $false
                    continue
                }

                if ($ch -eq '\') {
                    # In bash double-quotes, backslash can escape: \, ", $, `, newline
                    if ($j + 1 -lt $inner.Length) {
                        $next = $inner[$j + 1]
                        if ($next -in @('\','"','$','`',"`n")) {
                            $j++
                            [void]$sb.Append($next)
                            continue
                        }
                    }
                    # Otherwise treat '\' literally
                    [void]$sb.Append('\')
                    continue
                }

                [void]$sb.Append($ch)
                continue
            }

            # Not in quotes
            if ($ch -match '\s') {
                if ($sb.Length -gt 0) {
                    $tokens.Add($sb.ToString())
                    $null = $sb.Clear()
                }
                continue
            }

            if ($ch -eq "'") { $inSingle = $true; continue }
            if ($ch -eq '"') { $inDouble = $true; continue }

            if ($ch -eq '\') { $escaped = $true; continue }

            [void]$sb.Append($ch)
        }

        if ($escaped) {
            # trailing backslash: treat it literally
            [void]$sb.Append('\')
        }

        if ($inSingle -or $inDouble) {
            throw "Unterminated quote in bash array RHS: $Text"
        }

        if ($sb.Length -gt 0) {
            $tokens.Add($sb.ToString())
        }

        return ,$tokens.ToArray()
    }

    # Try parse as array first: KEY=(...)
    $arr = $null
    if ($rhs.StartsWith('(')) {
        $arr = Parse-BashArrayRhs -Text $rhs
    }

    if ($null -ne $arr) {
        # Set as a PowerShell array
        Set-Variable -Name $key -Value $arr -Scope Script
        return
    }

    # Scalar: strip surrounding quotes (your existing behavior)
    $val = $rhs
    if (($val.Length -ge 2) -and (($val.StartsWith('"') -and $val.EndsWith('"')) -or ($val.StartsWith("'") -and $val.EndsWith("'")))) {
        $val = $val.Substring(1, $val.Length - 2)
    }

    Set-Variable -Name $key -Value $val -Scope Script
}
}

$BUSTER_CI                = 0
$BUSTER_DOWNLOAD_TOOLCHAIN= 0
$BUSTER_SELF_HOSTED       = 0
$BUSTER_FUZZ_DURATION_SECONDS = 0

if ($args.Count -ge 1) {
    Set-PSDebug -Trace 1

    if ($args.Count -ge 2) {
        foreach ($arg in $args[1..($args.Count-1)]) {
            if ($args.Count -lt 2) {
                break
            }

            switch -Regex ($arg) {
                '^--ci=(.+)$'                 { $BUSTER_CI                 = [int]$Matches[1]; break }
                '^--download_toolchain=(.+)$' { $BUSTER_DOWNLOAD_TOOLCHAIN = [int]$Matches[1]; break }
                '^--self-hosted=(.+)$'        { $BUSTER_SELF_HOSTED        = [int]$Matches[1]; break }
                '^--fuzz-duration=(.+)$'        { $BUSTER_FUZZ_DURATION_SECONDS        = [int]$Matches[1]; break }
                default { break }
            }
        }
    }
}

if ($BUSTER_FUZZ_DURATION_SECONDS -eq '0') {
    if ($BUSTER_CI -eq '1') {
        if ($BUSTER_SELF_HOSTED -eq '1') {
            $BUSTER_FUZZ_DURATION_SECONDS = $BUSTER_FAST_FUZZ_DURATION_SECONDS
        } else {
            $branch = git branch --show-current

            if ($branch -eq 'main') {
                $BUSTER_FUZZ_DURATION_SECONDS = $BUSTER_THOROUGH_FUZZ_DURATION_SECONDS
            } else {
                $BUSTER_FUZZ_DURATION_SECONDS = $BUSTER_FAST_FUZZ_DURATION_SECONDS
            }
        }
    } else {
        $BUSTER_FUZZ_DURATION_SECONDS = $BUSTER_FAST_FUZZ_DURATION_SECONDS
    }
}


$BUSTER_OS = 'windows'

$nativeArch = $env:PROCESSOR_ARCHITEW6432
if (-not $nativeArch) {
    $nativeArch = $env:PROCESSOR_ARCHITECTURE
}

switch ($nativeArch.ToUpperInvariant()) {
    'AMD64' { $BUSTER_ARCH = 'x86_64' }
    'ARM64' { $BUSTER_ARCH = 'aarch64' }
    default { throw "error: unknown CPU architecture: $nativeArch" }
}

if ($BUSTER_CI -eq 1) {
  try {
    $cpuName = (Get-CimInstance Win32_Processor | Select-Object -First 1 -ExpandProperty Name)
    Write-Host "CPU: $cpuName"
  } catch {}
}

$BUSTER_LLVM_VERSION_STRING = "$BUSTER_LLVM_VERSION_MAJOR.$BUSTER_LLVM_VERSION_MINOR.$BUSTER_LLVM_VERSION_REVISION"
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

$buildDir = 'build'
New-Item -ItemType Directory -Force -Path $buildDir | Out-Null

$builderExe = Join-Path $buildDir 'builder.exe'
$builderExe = $builderExe -replace '\\', '/'

$clangArgs = @(
  'build.c',
  '-o', $builderExe,
  '-fuse-ld=lld',
  ('-DBUSTER_TOOLCHAIN_ABSOLUTE_PATH=\"' + $BUSTER_TOOLCHAIN_ABSOLUTE_PATH + '\"'),
  ('-DBUSTER_CLANG_ABSOLUTE_PATH=\"' + $BUSTER_CLANG_ABSOLUTE_PATH + '\"'),
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

& $BUSTER_CLANG_ABSOLUTE_PATH @clangArgs
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

& $builderExe @args "--fuzz-duration=$BUSTER_FUZZ_DURATION_SECONDS"
exit $LASTEXITCODE
