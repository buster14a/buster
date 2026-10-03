# Enter the Visual Studio developer shell for one hosted Windows CI step.
#
# Ownership: the Windows PowerShell steps in .github/workflows/ci.yml
# (Combination matrix (Windows), Execution-mode matrix (Windows) and Native
# MSVC reference differential); tested by tools/ci_vs_dev_shell_test.py.
# Entry point: invoke with `&` from a step. Launch-VsDevShell.ps1 edits the
# process environment, so the caller's later commands see the shell without
# dot-sourcing. Failures throw; this script never calls `exit`, which would end
# only this script and let the calling step continue.
#
# Map:
#   vswhere lookup           -Component selects the newest matching install.
#   Launch-VsDevShell.ps1    always -HostArch amd64; see docs/ci-github-actions.md.
#   -LlvmBin                 prepends the installed LLVM after the shell.
#   -GithubEnv               exports PATH and VSCMD_ARG_TGT_ARCH before probes.
#   -Require / -ClangArch    probe compilers and clang's default target.
param(
    [Parameter(Mandatory = $true)][string]$Arch,
    [Parameter(Mandatory = $true)][string]$Component,
    [string]$LlvmBin = '',
    [string]$GithubEnv = '',
    [string[]]$Require = @(),
    [string]$ClangArch = '',
    [string]$VsWhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
)

$ErrorActionPreference = 'Stop'
if (-not (Test-Path -LiteralPath $VsWhere)) { throw "vswhere was not found at $VsWhere" }
$VsPaths = @(& $VsWhere -latest -products * -requires $Component -property installationPath)
if ($LASTEXITCODE -ne 0 -or $VsPaths.Count -eq 0) { throw "Visual Studio with $Component was not found" }
$VsPath = "$($VsPaths[0])".Trim()
if (-not $VsPath) { throw "Visual Studio with $Component was not found" }
# ARM64 uses an emulated amd64 VS host but the native installed LLVM.
& (Join-Path $VsPath 'Common7\Tools\Launch-VsDevShell.ps1') -Arch $Arch -HostArch amd64 -SkipAutomaticLocation
if ($LlvmBin) {
    $env:PATH = "$LlvmBin;$env:PATH"
}
if ($GithubEnv) {
    # A later result step independently re-probes the exact compiler
    # executables recorded by the driver. Preserve this step's selected
    # Visual Studio/LLVM search path and target architecture for it.
    $PathDelimiter = 'BUSTER_CI_WINDOWS_COMPILER_PATH'
    Add-Content -LiteralPath $GithubEnv -Value "PATH<<$PathDelimiter"
    Add-Content -LiteralPath $GithubEnv -Value $env:PATH
    Add-Content -LiteralPath $GithubEnv -Value $PathDelimiter
    Add-Content -LiteralPath $GithubEnv -Value "VSCMD_ARG_TGT_ARCH=$env:VSCMD_ARG_TGT_ARCH"
}
foreach ($Compiler in $Require) {
    where.exe $Compiler
    if ($LASTEXITCODE -ne 0) { throw "$Compiler was not found on PATH" }
}
if ($ClangArch) {
    $ClangVersion = (& clang --version) -join "`n"
    Write-Output $ClangVersion
    if ($ClangVersion -notmatch "Target:\s*${ClangArch}-") {
        throw "clang does not default to a $ClangArch target"
    }
}
