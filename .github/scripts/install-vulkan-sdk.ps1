$ErrorActionPreference = 'Stop'

$releaseBase = 'https://github.com/buster14a/vulkan-sdk/releases/download/ci-latest'
$runnerArch = $env:RUNNER_ARCH
switch ($runnerArch) {
    'X64' {
        $asset = 'vulkan-sdk-windows-x86_64.zip'
        $sdkArch = 'x86_64'
    }
    'ARM64' {
        $asset = 'vulkan-sdk-windows-aarch64.zip'
        $sdkArch = 'aarch64'
    }
    default { throw "Unsupported GitHub runner architecture: $runnerArch" }
}

$installBase = Join-Path $env:RUNNER_TEMP 'vulkan-sdk'
$archive = Join-Path $env:RUNNER_TEMP $asset
$versionRoot = Join-Path $installBase 'version'

New-Item -ItemType Directory -Force -Path $installBase | Out-Null
Invoke-WebRequest -Uri "$releaseBase/$asset" -OutFile $archive
Expand-Archive -Path $archive -DestinationPath $versionRoot -Force

$versionDir = Get-ChildItem -LiteralPath $versionRoot -Directory -Filter 'vulkan-sdk-*' | Select-Object -First 1
if ($null -eq $versionDir) { throw "Could not find the SDK version directory under $versionRoot" }

$sdkRoot = $versionDir.FullName
$sdkDir = Join-Path $sdkRoot "windows-$sdkArch"
$setup = Join-Path $sdkRoot 'setup-env.ps1'
$header = Join-Path (Join-Path (Join-Path $sdkDir 'include') 'vulkan') 'vulkan.h'
if (-not (Test-Path -LiteralPath $setup) -or -not (Test-Path -LiteralPath $header)) {
    throw "SDK installation is incomplete under $sdkRoot"
}

. $setup
$env:VULKAN_SDK = $sdkDir
$env:Path = (Join-Path $env:VULKAN_SDK 'bin') + [IO.Path]::PathSeparator + $env:Path
$existingCpath = if ($env:CPATH) { $env:CPATH } else { '' }
$existingCmakePrefixPath = if ($env:CMAKE_PREFIX_PATH) { $env:CMAKE_PREFIX_PATH } else { '' }
$env:CPATH = (Join-Path $env:VULKAN_SDK 'include') + [IO.Path]::PathSeparator + $existingCpath
$env:CMAKE_PREFIX_PATH = $env:VULKAN_SDK + [IO.Path]::PathSeparator + $existingCmakePrefixPath

@(
    "VULKAN_SDK=$env:VULKAN_SDK"
    "CPATH=$env:CPATH"
    "CMAKE_PREFIX_PATH=$env:CMAKE_PREFIX_PATH"
) | Out-File -FilePath $env:GITHUB_ENV -Append -Encoding utf8
(Join-Path $env:VULKAN_SDK 'bin') | Out-File -FilePath $env:GITHUB_PATH -Append -Encoding utf8

Write-Host "Installed Vulkan SDK: $env:VULKAN_SDK"
