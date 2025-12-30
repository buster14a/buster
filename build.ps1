Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$BUSTER_CI = 0

foreach ($arg in $args) {
    switch ($arg) {
        '--ci=0' { $BUSTER_CI = 0 }
        '--ci=1' { $BUSTER_CI = 1 }
    }
}

$BUSTER_BUILD_DIRECTORY = "build"
New-Item -ItemType Directory -Force -Path $BUSTER_BUILD_DIRECTORY | Out-Null
& clang preamble.c -o "$BUSTER_BUILD_DIRECTORY/preamble.exe" -Isrc -Wall -Werror -std=gnu2x -Wno-unused-function -funsigned-char -lws2_32 "-DBUSTER_CI=$BUSTER_CI" -g
& "$BUSTER_BUILD_DIRECTORY/preamble.exe" @args
exit $LASTEXITCODE
