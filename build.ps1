[CmdletBinding(PositionalBinding = $false)]
param(
    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]]$RemainingArguments
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Invoke-Native {
    param(
        [Parameter(Mandatory = $true)]
        [string]$FilePath,

        [string[]]$ArgumentList = @()
    )

    & $FilePath @ArgumentList
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }
}

$RepoRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$BuildDirectory = Join-Path $RepoRoot "build"
$BootstrapPath = Join-Path $BuildDirectory "build.exe"

Push-Location $RepoRoot
try {
    New-Item -ItemType Directory -Path $BuildDirectory -Force | Out-Null

    Invoke-Native "tcc" @(
        "-Isrc"
        "-Wall"
        "-Werror"
        "-g"
        "-lws2_32",
        "build.c"
        "-o"
        $BootstrapPath
    )

    Invoke-Native $BootstrapPath $RemainingArguments
}
finally {
    Pop-Location
}
