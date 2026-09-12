Set-StrictMode -Version Latest

function Get-BusterBootstrapFileHash {
    param([Parameter(Mandatory = $true)][string]$Path)
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256 -ErrorAction Stop).Hash.ToLowerInvariant()
}

function Get-BusterBootstrapStringHash {
    param([Parameter(Mandatory = $true)][string]$Text)
    $Algorithm = [System.Security.Cryptography.SHA256]::Create()
    try {
        $Bytes = [System.Text.Encoding]::UTF8.GetBytes($Text)
        return ([BitConverter]::ToString($Algorithm.ComputeHash($Bytes))).Replace("-", "").ToLowerInvariant()
    }
    finally {
        $Algorithm.Dispose()
    }
}

function Get-BusterBootstrapDependencies {
    param(
        [Parameter(Mandatory = $true)][string]$RepoRoot,
        [Parameter(Mandatory = $true)][string]$DependencyFile
    )
    $Raw = (Get-Content -LiteralPath $DependencyFile -Raw -ErrorAction Stop) -replace '\\\r?\n', ' '
    $TargetEnd = [regex]::Match($Raw, ":\s")
    if (-not $TargetEnd.Success) {
        throw "malformed bootstrap dependency file"
    }
    $Body = $Raw.Substring($TargetEnd.Index + 1)
    $Tokens = @()
    $TokenBuilder = New-Object System.Text.StringBuilder
    for ($Index = 0; $Index -lt $Body.Length; $Index += 1) {
        $Character = $Body[$Index]
        if ($Character -eq [char]92 -and $Index + 1 -lt $Body.Length -and [char]::IsWhiteSpace($Body[$Index + 1])) {
            $Index += 1
            [void]$TokenBuilder.Append($Body[$Index])
        }
        elseif ([char]::IsWhiteSpace($Character)) {
            if ($TokenBuilder.Length) {
                $Tokens += $TokenBuilder.ToString()
                [void]$TokenBuilder.Clear()
            }
        }
        else {
            [void]$TokenBuilder.Append($Character)
        }
    }
    if ($TokenBuilder.Length) {
        $Tokens += $TokenBuilder.ToString()
    }
    $Dependencies = @()
    foreach ($Token in $Tokens) {
        $Relative = ($Token -replace "\\", "/") -replace "^\./", ""
        if ($Relative -match "(^|[\\/])\.\.([\\/]|$)") {
            throw "unsafe bootstrap dependency path: $Relative"
        }
        $FullPath = if ([IO.Path]::IsPathRooted($Relative)) { $Relative } else { Join-Path $RepoRoot $Relative }
        if (-not (Test-Path -LiteralPath $FullPath -PathType Leaf)) {
            throw "missing bootstrap dependency: $Relative"
        }
        $Dependencies += [PSCustomObject]@{
            path = $Relative
            sha256 = Get-BusterBootstrapFileHash $FullPath
        }
    }
    return @($Dependencies | Sort-Object path -Unique)
}

function Test-BusterBootstrapManifest {
    param(
        [Parameter(Mandatory = $true)][string]$RepoRoot,
        [Parameter(Mandatory = $true)][string]$EntryDirectory,
        [Parameter(Mandatory = $true)][string]$Marker,
        [Parameter(Mandatory = $true)][string]$ExpectedConfig
    )
    $Valid = $false
    try {
        $Manifest = Get-Content -LiteralPath $Marker -Raw -ErrorAction Stop | ConvertFrom-Json
        $ArtifactName = [string]$Manifest.artifact
        $ArtifactPath = Join-Path $EntryDirectory $ArtifactName
        $Dependencies = @($Manifest.dependencies)
        $Valid = $Manifest.version -eq 1 -and $Manifest.config -eq $ExpectedConfig -and
            $ArtifactName -and [IO.Path]::GetFileName($ArtifactName) -eq $ArtifactName -and
            (Test-Path -LiteralPath $ArtifactPath -PathType Leaf) -and
            (Get-BusterBootstrapFileHash $ArtifactPath) -eq $Manifest.artifact_sha256 -and
            $Dependencies.Count -gt 0
        $SawBuildC = $false
        foreach ($Dependency in $Dependencies) {
            $Relative = [string]$Dependency.path
            $FullPath = if ([IO.Path]::IsPathRooted($Relative)) { $Relative } else { Join-Path $RepoRoot $Relative }
            $Valid = $Valid -and $Relative -notmatch "(^|[\\/])\.\.([\\/]|$)" -and
                (Test-Path -LiteralPath $FullPath -PathType Leaf) -and
                (Get-BusterBootstrapFileHash $FullPath) -eq $Dependency.sha256
            $SawBuildC = $SawBuildC -or [IO.Path]::GetFullPath($FullPath) -eq [IO.Path]::GetFullPath((Join-Path $RepoRoot "build.c"))
        }
        $Valid = $Valid -and $SawBuildC
        if ($Valid) {
            $script:BusterBootstrapArtifact = $ArtifactPath
        }
    }
    catch {
        $Valid = $false
    }
    return $Valid
}

function Invoke-BusterBootstrapDriver {
    param(
        [Parameter(Mandatory = $true)][string]$RepoRoot,
        [string[]]$RemainingArguments = @()
    )
    $TccCommand = Get-Command "tcc" -CommandType Application -ErrorAction Stop
    $TccPath = $TccCommand.Source
    $BootstrapFlags = @("-Isrc", "-Wall", "-Werror", "-g", "-lws2_32", "-MD")
    $HelperPath = Join-Path $RepoRoot "tools/bootstrap_driver.ps1"
    $ConfigText = @(
        "BUSTER_BOOTSTRAP_CONFIG_V1"
        "compiler`t$TccPath"
        "compiler-sha256`t$(Get-BusterBootstrapFileHash $TccPath)"
        $BootstrapFlags | ForEach-Object { "flag`t$_" }
        "helper-sha256`t$(Get-BusterBootstrapFileHash $HelperPath)"
    ) -join "`n"
    $Config = Get-BusterBootstrapStringHash $ConfigText
    $CacheRoot = Join-Path $RepoRoot ".cache/bootstrap-driver/windows"
    $EntryDirectory = Join-Path $CacheRoot $Config
    if ((Test-Path -LiteralPath $CacheRoot) -and -not (Test-Path -LiteralPath $CacheRoot -PathType Container)) {
        throw "bootstrap cache root is not a directory: $CacheRoot"
    }
    New-Item -ItemType Directory -Path $EntryDirectory -Force -ErrorAction Stop | Out-Null

    foreach ($Marker in @(Get-ChildItem -LiteralPath $EntryDirectory -Filter "*.complete" -File -ErrorAction SilentlyContinue)) {
        if (Test-BusterBootstrapManifest $RepoRoot $EntryDirectory $Marker.FullName $Config) {
            & $script:BusterBootstrapArtifact @RemainingArguments
            exit $LASTEXITCODE
        }
    }

    $Token = "{0}-{1}" -f $PID, [Guid]::NewGuid().ToString("N")
    $DependencyFile = Join-Path $CacheRoot ".bootstrap-$Token.d"
    $TemporaryProbe = Join-Path $CacheRoot ".bootstrap-$Token.probe.exe"
    $TemporaryArtifact = Join-Path $CacheRoot ".bootstrap-$Token.tmp.exe"
    $TemporaryMarker = Join-Path $CacheRoot ".bootstrap-$Token.marker"
    try {
        Push-Location $RepoRoot
        try {
            & $TccPath @BootstrapFlags "-MF" $DependencyFile "build.c" "-o" $TemporaryProbe
            if ($LASTEXITCODE -ne 0) {
                exit $LASTEXITCODE
            }
            $Before = @(Get-BusterBootstrapDependencies $RepoRoot $DependencyFile)
            & $TccPath @BootstrapFlags "-MF" $DependencyFile "build.c" "-o" $TemporaryArtifact
            if ($LASTEXITCODE -ne 0) {
                exit $LASTEXITCODE
            }
            $After = @(Get-BusterBootstrapDependencies $RepoRoot $DependencyFile)
        }
        finally {
            Pop-Location
        }

        if (($Before | ConvertTo-Json -Depth 3 -Compress) -ne ($After | ConvertTo-Json -Depth 3 -Compress)) {
            throw "bootstrap inputs changed while tcc was compiling; retry the command"
        }
        if ((Get-BusterBootstrapFileHash $TccPath) -ne (($ConfigText -split "`n" | Where-Object { $_ -like "compiler-sha256`t*" }) -replace "^compiler-sha256`t", "")) {
            throw "tcc changed while compiling the build driver; retry the command"
        }

        $ArtifactName = "build-$Token.exe"
        $ArtifactPath = Join-Path $EntryDirectory $ArtifactName
        Move-Item -LiteralPath $TemporaryArtifact -Destination $ArtifactPath -ErrorAction Stop
        $Manifest = [ordered]@{
            version = 1
            config = $Config
            artifact = $ArtifactName
            artifact_sha256 = Get-BusterBootstrapFileHash $ArtifactPath
            dependencies = $After
        }
        $Manifest | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $TemporaryMarker -Encoding UTF8 -ErrorAction Stop
        Move-Item -LiteralPath $TemporaryMarker -Destination (Join-Path $EntryDirectory "$ArtifactName.complete") -ErrorAction Stop
        & $ArtifactPath @RemainingArguments
        exit $LASTEXITCODE
    }
    finally {
        Remove-Item -LiteralPath $DependencyFile, $TemporaryProbe, $TemporaryArtifact, $TemporaryMarker -Force -ErrorAction SilentlyContinue
    }
}
