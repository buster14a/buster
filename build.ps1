[CmdletBinding(PositionalBinding = $false)]
param(
    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]]$RemainingArguments
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
# Load the two system modules this wrapper uses by name. Windows PowerShell 5.1
# otherwise resolves the first unqualified cmdlet by analyzing every module on
# PSModulePath, ~23 s per process with a cold analysis cache (#3979).
Import-Module Microsoft.PowerShell.Management, Microsoft.PowerShell.Utility

$RepoRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
. (Join-Path $RepoRoot "tools/bootstrap_driver.ps1")
Push-Location $RepoRoot
try {
    Invoke-BusterBootstrapDriver -RepoRoot $RepoRoot -RemainingArguments $RemainingArguments
}
finally {
    Pop-Location
}
