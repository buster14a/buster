#!/usr/bin/env bash
set -euo pipefail

release_base='https://github.com/buster14a/vulkan-sdk/releases/download/ci-latest'
runner_os=${RUNNER_OS:-}
runner_arch=${RUNNER_ARCH:-}

case "$runner_os/$runner_arch" in
  Linux/X64)
    asset='vulkan-sdk-linux-x86_64.tar.gz'
    install_base='/opt/vulkan-sdk'
    platform='linux'
    arch='x86_64'
    ;;
  Linux/ARM64)
    asset='vulkan-sdk-linux-aarch64.tar.gz'
    install_base='/opt/vulkan-sdk'
    platform='linux'
    arch='aarch64'
    ;;
  macOS/X64)
    echo 'No Vulkan SDK package is published for macOS x86-64; using the default non-Vulkan configuration.'
    exit 0
    ;;
  macOS/ARM64)
    asset='vulkan-sdk-macos-aarch64.tar.gz'
    install_base="$RUNNER_TEMP/vulkan-sdk"
    platform='macos'
    arch='aarch64'
    ;;
  *)
    echo "Unsupported GitHub runner: $runner_os/$runner_arch" >&2
    exit 2
    ;;
esac

archive="$RUNNER_TEMP/$asset"
listing="$RUNNER_TEMP/vulkan-sdk-archive.list"
curl --fail --silent --show-error --location --retry 5 --retry-delay 2 --retry-all-errors \
  --output "$archive" "$release_base/$asset"

tar -tzf "$archive" > "$listing"
version_name=$(awk -F/ '$1 ~ /^vulkan-sdk-/ { print $1; exit }' "$listing")
if [[ -z "$version_name" ]]; then
  echo "Could not determine the SDK version directory in $archive" >&2
  exit 1
fi

if [[ "$install_base" == /opt/* ]]; then
  sudo mkdir -p "$install_base"
  sudo tar -xzf "$archive" -C "$install_base"
  version_dir="$install_base/$version_name"
  sudo ln -sfn "$version_dir" "$install_base/current"
else
  mkdir -p "$install_base"
  tar -xzf "$archive" -C "$install_base"
  version_dir="$install_base/$version_name"
  ln -sfn "$version_dir" "$install_base/current"
fi

sdk_root="$install_base/current"
sdk_arch_dir="$sdk_root/$platform-$arch"
if [[ ! -f "$sdk_root/setup-env.sh" || ! -f "$sdk_arch_dir/include/vulkan/vulkan.h" ]]; then
  echo "SDK installation is incomplete under $sdk_root" >&2
  exit 1
fi

# Keep the package's setup contract, but retain the stable `current` path in
# all exported variables so Buster's CMake probes resolve the same location.
source "$sdk_root/setup-env.sh"
export VULKAN_SDK="$sdk_arch_dir"
export PATH="$VULKAN_SDK/bin:${PATH:-}"
export CPATH="$VULKAN_SDK/include:${CPATH:-}"
export CMAKE_PREFIX_PATH="$VULKAN_SDK:${CMAKE_PREFIX_PATH:-}"
export PKG_CONFIG_PATH="$VULKAN_SDK/lib/pkgconfig:${PKG_CONFIG_PATH:-}"

case "$runner_os" in
  Linux)
    export LD_LIBRARY_PATH="$VULKAN_SDK/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
    ;;
  macOS)
    export DYLD_LIBRARY_PATH="$VULKAN_SDK/lib${DYLD_LIBRARY_PATH:+:$DYLD_LIBRARY_PATH}"
    ;;
esac

if [[ "$runner_os" == Linux ]]; then
  resolved_current=$(readlink -f "$install_base/current")
  [[ "$resolved_current" == "$version_dir" ]]
fi

{
  echo "VULKAN_SDK=$VULKAN_SDK"
  echo "CPATH=$CPATH"
  echo "CMAKE_PREFIX_PATH=$CMAKE_PREFIX_PATH"
  echo "PKG_CONFIG_PATH=$PKG_CONFIG_PATH"
  case "$runner_os" in
    Linux) echo "LD_LIBRARY_PATH=$LD_LIBRARY_PATH" ;;
    macOS) echo "DYLD_LIBRARY_PATH=$DYLD_LIBRARY_PATH" ;;
  esac
} >> "$GITHUB_ENV"
printf '%s\n' "$VULKAN_SDK/bin" >> "$GITHUB_PATH"

echo "Installed Vulkan SDK: $VULKAN_SDK"
echo "SDK current link: $install_base/current -> $version_dir"
