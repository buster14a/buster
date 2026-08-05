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

deployment_target=${BUSTER_IOS_DEPLOYMENT_TARGET:-13.0}
# Apple Silicon runners run the simulator natively as arm64.
arch=${BUSTER_IOS_ARCH:-arm64}
# One directory for all configs: the Ninja Multi-Config generator keeps
# per-config app bundles separate, so configure once and build each requested
# configuration before booting the simulator.
build_directory=${BUSTER_IOS_BUILD_DIRECTORY:-build/ios-simulator-${arch}}

build_configs_string=${BUSTER_IOS_BUILD_CONFIGS:-}
if [[ -z $build_configs_string ]]; then
    if [[ -n ${BUSTER_IOS_BUILD_CONFIG:-} ]]; then
        build_configs_string=$BUSTER_IOS_BUILD_CONFIG
    else
        build_configs_string="Debug Release"
    fi
fi

if [[ $# -gt 0 ]]; then
    case "$1" in
        --all)
            if [[ $# -ne 1 ]]; then
                echo "usage: $0 [--all|Debug|Release ...]" >&2
                exit 2
            fi
            build_configs_string="Debug Release"
            ;;
        --configs)
            shift
            if [[ $# -eq 0 ]]; then
                echo "error: --configs requires at least one configuration" >&2
                exit 2
            fi
            build_configs_string="$*"
            ;;
        *)
            build_configs_string="$*"
            ;;
    esac
fi

build_configs=()
for build_config in $build_configs_string; do
    case "$build_config" in
        Debug|Release) ;;
        *)
            echo "error: iOS CI configurations must be Debug or Release (got '$build_config')" >&2
            exit 1
            ;;
    esac
    build_configs+=("$build_config")
done
if [[ ${#build_configs[@]} -eq 0 ]]; then
    echo "error: no iOS build configurations were selected" >&2
    exit 1
fi

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

configure_started=$SECONDS
cmake --warn-uninitialized -Werror=dev \
    -B "$build_directory" \
    -G "Ninja Multi-Config" \
    -DCMAKE_SYSTEM_NAME=iOS \
    -DCMAKE_OSX_SYSROOT=iphonesimulator \
    -DCMAKE_OSX_ARCHITECTURES="$arch" \
    -DCMAKE_OSX_DEPLOYMENT_TARGET="$deployment_target" \
    -DCMAKE_DEFAULT_BUILD_TYPE="${build_configs[0]}" \
    -DCMAKE_CONFIGURATION_TYPES="Debug;Release;RelWithDebInfo;MinSizeRel" \
    -DCMAKE_LINKER_TYPE=DEFAULT \
    -DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY \
    -DBUSTER_CI=ON \
    -DBUSTER_OPTIMIZE=OFF \
    -DBUSTER_SANITIZE=OFF \
    -DBUSTER_FUZZ_AVAILABLE=OFF \
    -DBUSTER_INCLUDE_TESTS=ON \
    -DBUSTER_CHECK_OPTIONAL_WARNINGS=OFF \
    -DBUSTER_DEVELOPER_TARGETS=OFF \
    -DBUSTER_IOS_FONT="$ios_font"
echo "TIMING_IOS configure_seconds=$((SECONDS - configure_started))"

app_paths=()
for build_config in "${build_configs[@]}"; do
    echo "Building iOS ${build_config} app bundle"
    build_started=$SECONDS
    # Build only the app bundle. Its POST_BUILD steps copy tests and the font
    # into ide.app; simulator execution is deliberately kept outside Ninja so
    # output remains streamed and both configs can share one boot.
    cmake --build "$build_directory" --config "$build_config" --target ide --verbose
    app_bundle="$build_directory/$build_config/ide.app"
    if [[ ! -d $app_bundle ]]; then
        echo "error: expected iOS app bundle not found at '$app_bundle'" >&2
        exit 1
    fi
    app_paths+=("$app_bundle")
    echo "TIMING_IOS build_seconds config=$build_config value=$((SECONDS - build_started))"
done

launch_args=(--batch)
for index in "${!build_configs[@]}"; do
    launch_args+=("${build_configs[$index]}" "${app_paths[$index]}")
done

# The launcher boots once, installs/launches each labeled bundle sequentially,
# validates each result marker independently, and shuts the selected simulator
# down once through its EXIT trap.
bash ios/launch_simulator.sh "${launch_args[@]}"
