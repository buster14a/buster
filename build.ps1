#!/usr/bin/env pwsh
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'


$clangArgs = @(
)

& clang preamble.c, -o build/preamble.exe -Isrc -std=gnu2x -Werror -Wall -Wno-unused-function -lws2_32
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
