#!/usr/bin/env bash
set -euo pipefail

repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
cd "$repo_root"

if [[ -f /opt/vulkan-sdk/current/setup-env.sh ]]; then
    # Metal shaders are compiled from Slang, so slangc from the Vulkan SDK is
    # still required even though iOS does not use the Vulkan renderer.
    # shellcheck disable=SC1091
    source /opt/vulkan-sdk/current/setup-env.sh
fi

if ! command -v xcrun >/dev/null 2>&1; then
    echo "error: xcrun was not found; Xcode command line tools are required" >&2
    exit 1
fi

build_config=${BUSTER_IOS_BUILD_CONFIG:-Debug}
deployment_target=${BUSTER_IOS_DEPLOYMENT_TARGET:-13.0}
# Apple Silicon runners run the simulator natively as arm64.
arch=${BUSTER_IOS_ARCH:-arm64}
build_directory=${BUSTER_IOS_BUILD_DIRECTORY:-build/ios-simulator-${arch}-${build_config}}

# A monospace font to bundle into the .app. The in-app test always exercises the
# GUI/rendering path (run_app), which builds a font atlas and hard-fails if no
# font loads, so a font must be present. Prefer FiraCode (matches the desktop),
# then fall back to a macOS system monospace font that is always installed. The
# bundled file is renamed to FiraCode-Regular.ttf inside the .app regardless, and
# the truetype parser handles .ttc collections, so any of these work.
ios_font=${BUSTER_IOS_FONT:-}
if [[ -z $ios_font ]]; then
    for candidate in \
        /Library/Fonts/FiraCode-Regular.ttf \
        "$HOME/Library/Fonts/FiraCode-Regular.ttf" \
        /System/Library/Fonts/SFNSMono.ttf \
        /System/Library/Fonts/Menlo.ttc \
        /System/Library/Fonts/Monaco.ttf; do
        if [[ -f $candidate ]]; then
            ios_font=$candidate
            break
        fi
    done
fi
if [[ -z $ios_font || ! -f $ios_font ]]; then
    echo "error: no font found to bundle into ide.app; set BUSTER_IOS_FONT to a .ttf/.ttc path" >&2
    exit 1
fi
echo "Bundling font: $ios_font"

cmake --version
xcrun --sdk iphonesimulator --show-sdk-path

cmake --warn-uninitialized -Werror=dev \
    -B "$build_directory" \
    -G "Ninja Multi-Config" \
    -DCMAKE_SYSTEM_NAME=iOS \
    -DCMAKE_OSX_SYSROOT=iphonesimulator \
    -DCMAKE_OSX_ARCHITECTURES="$arch" \
    -DCMAKE_OSX_DEPLOYMENT_TARGET="$deployment_target" \
    -DCMAKE_DEFAULT_BUILD_TYPE="$build_config" \
    -DCMAKE_CONFIGURATION_TYPES="Debug;Release;RelWithDebInfo;MinSizeRel" \
    -DCMAKE_LINKER_TYPE=DEFAULT \
    -DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY \
    -DBUSTER_CI=ON \
    -DBUSTER_OPTIMIZE=OFF \
    -DBUSTER_SANITIZE=OFF \
    -DBUSTER_FUZZ=OFF \
    -DBUSTER_INCLUDE_TESTS=ON \
    -DBUSTER_CHECK_OPTIONAL_WARNINGS=OFF \
    -DBUSTER_DEVELOPER_TARGETS=OFF \
    -DBUSTER_IOS_FONT="$ios_font"

# Build only the app bundle (its POST_BUILD steps copy the tests dir and font
# into ide.app). The on-simulator run is deliberately NOT the `test_all` Ninja
# target: Ninja buffers a custom command's output until it exits, which makes the
# boot/install/launch look hung. Invoking launch_simulator.sh directly streams
# its progress live so a stall is visible (and bounded by the script's timeouts).
cmake --build "$build_directory" --config "$build_config" --target ide --verbose

app_bundle="$build_directory/$build_config/ide.app"
if [[ ! -d $app_bundle ]]; then
    echo "error: expected app bundle not found at '$app_bundle'" >&2
    exit 1
fi

BUSTER_IOS_APP_BUNDLE="$app_bundle" bash ios/launch_simulator.sh
