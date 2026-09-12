[CmdletBinding(PositionalBinding = $false)]
param(
    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]]$RemainingArguments
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$RepoRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
. (Join-Path $RepoRoot "tools/bootstrap_driver.ps1")
Push-Location $RepoRoot
try {
    Invoke-BusterBootstrapDriver -RepoRoot $RepoRoot -RemainingArguments $RemainingArguments
}
finally {
    Pop-Location
}
