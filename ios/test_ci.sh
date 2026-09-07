#!/usr/bin/env bash
set -euo pipefail

repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
cd "$repo_root"

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

cmake --version
xcrun --sdk iphonesimulator --show-sdk-path

# Xcode 26 has a documented first-run simulator failure mode after the host
# macOS image changes. Apple recommends rebuilding the simulator dyld shared
# caches before booting. GitHub's Intel macOS image is ephemeral and exercised
# exactly this path: boot eventually completed, but freshly launched x86-64
# simulator apps immediately disappeared before they could emit the CI result
# marker. Keep the workaround narrowly scoped to GitHub's Intel simulator leg;
# Apple Silicon is unaffected and local/Forgejo runs retain their normal state.
if [[ $arch == x86_64 && ${GITHUB_ACTIONS:-false} == true ]]; then
    echo "Refreshing Intel iOS simulator dyld shared caches"
    if ! xcrun simctl runtime dyld_shared_cache update --all; then
        echo "error: failed to refresh iOS simulator dyld shared caches" >&2
        exit 1
    fi
fi

configure_started=$SECONDS
cmake --warn-uninitialized -Werror=dev \
    -B "$build_directory" \
    -G "Ninja Multi-Config" \
    -DCMAKE_SYSTEM_NAME=iOS \
    -DCMAKE_OSX_SYSROOT=iphonesimulator \
    -DCMAKE_OSX_ARCHITECTURES="$arch" \
    -DCMAKE_OSX_DEPLOYMENT_TARGET="$deployment_target" \
    -DCMAKE_DEFAULT_BUILD_TYPE="${build_configs[0]}" \
    -DCMAKE_CONFIGURATION_TYPES="Debug;Release" \
    -DCMAKE_LINKER_TYPE=DEFAULT \
    -DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY \
    -DBUSTER_CI=ON \
    -DBUSTER_OPTIMIZE=OFF \
    -DBUSTER_SANITIZE=OFF \
    -DBUSTER_FUZZ_AVAILABLE=OFF \
    -DBUSTER_INCLUDE_TESTS=ON \
    -DBUSTER_CHECK_OPTIONAL_WARNINGS=OFF \
    -DBUSTER_DEVELOPER_TARGETS=OFF
echo "TIMING_IOS configure_seconds=$((SECONDS - configure_started))"

app_paths=()
for build_config in "${build_configs[@]}"; do
    echo "Building iOS ${build_config} app bundle"
    build_started=$SECONDS
    # Build only the app bundle. Its POST_BUILD step copies tests into ide.app;
    # simulator execution is deliberately kept outside Ninja so
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

# Xcode 26 deliberately stopped shipping Intel support in simulator runtimes by
# default. GitHub's macos-26-intel image currently exposes iOS runtimes that
# boot, install an x86-64 bundle, report a launch PID, and then terminate that
# process before user code runs; rebuilding dyld caches does not change the
# result. Keep the hosted Intel leg useful and deterministic by making it an
# x86-64 iOS compile/link/bundle gate. The Apple-Silicon macOS leg still boots
# the simulator and executes both configurations end to end, while local Intel
# runners with a universal simulator runtime continue through the normal launch
# path because this exception is GitHub-only.
if [[ $arch == x86_64 && ${GITHUB_ACTIONS:-false} == true ]]; then
    for index in "${!build_configs[@]}"; do
        build_config=${build_configs[$index]}
        app_bundle=${app_paths[$index]}
        executable="$app_bundle/ide"
        if [[ ! -f $executable ]]; then
            echo "error: expected iOS ${build_config} executable not found at '$executable'" >&2
            exit 1
        fi
        if ! lipo -verify_arch x86_64 "$executable"; then
            echo "error: iOS ${build_config} bundle does not contain an x86-64 simulator executable" >&2
            exit 1
        fi
        echo "iOS ${build_config} x86-64 simulator bundle built and linked successfully."
    done
    echo "iOS x86-64 execution skipped on GitHub macos-26-intel: Xcode 26 simulator runtimes do not provide reliable Intel execution coverage."
    exit 0
fi

launch_args=(--batch)
for index in "${!build_configs[@]}"; do
    launch_args+=("${build_configs[$index]}" "${app_paths[$index]}")
done

# The launcher boots once, installs/launches each labeled bundle sequentially,
# validates each result marker independently, and shuts the selected simulator
# down once through its EXIT trap.
bash ios/launch_simulator.sh "${launch_args[@]}"
