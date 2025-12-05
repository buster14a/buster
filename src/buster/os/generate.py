#!/usr/bin/env python3
import os
import platform
import shutil
import subprocess
import sys
import re
import urllib.request

from pathlib import Path

def get_version(cmd):
    """Return (major, minor, patch) version tuple for clang, or None if unavailable."""
    try:
        out = subprocess.check_output([cmd, "--version"], text=True, stderr=subprocess.STDOUT)
    except (subprocess.CalledProcessError, FileNotFoundError, PermissionError):
        return None

    # Example: "clang version 19.1.0 ..."
    m = re.search(r"clang version (\d+)\.(\d+)\.(\d+)", out)
    if not m:
        m = re.search(r"clang version (\d+)\.(\d+)", out)
    if not m:
        return None

    nums = [int(x) for x in m.groups()]
    while len(nums) < 3:
        nums.append(0)
    return tuple(nums)

CANDIDATE_NAMES = ["clang", "clang-21", "clang-20", "clang-19"]

def find_candidates():
    """Return candidate clang executables (only specific versions)."""
    candidates = []

    # 1. Look in PATH
    for name in CANDIDATE_NAMES:
        exe = shutil.which(name)
        if exe:
            candidates.append(exe)

    # 2. Common install dirs
    search_dirs = [
        "/usr/bin",
        "/usr/local/bin",
        "/opt/homebrew/opt/llvm/bin",  # macOS Apple Silicon Homebrew
        "/usr/local/opt/llvm/bin",     # macOS Intel Homebrew
        "C:\\Program Files\\LLVM\\bin" # Windows LLVM
    ]
    for d in search_dirs:
        if os.path.isdir(d):
            for name in CANDIDATE_NAMES:
                exe = Path(d) / name
                if exe.exists():
                    candidates.append(str(exe))

    return sorted(set(candidates))

def get_compiler_paths():
    candidates = find_candidates()
    versions = []
    for exe in candidates:
        v = get_version(exe)
        if v:
            versions.append((v, exe))

    if not versions:
        print("No supported clang installation found.", file=sys.stderr)
        sys.exit(1)

    # Pick the newest version
    version, clang_path = max(versions, key=lambda x: x[0])

    # Derive clang++: same dir, replace name
    base = Path(clang_path).parent
    name = Path(clang_path).name
    if "-" in name:  # e.g., clang-20
        clangxx_name = name.replace("clang", "clang++", 1)
    else:
        clangxx_name = "clang++"
    clangxx_path = base / clangxx_name

    if not clangxx_path.exists():
        clangxx_path = shutil.which("clang++") or ""

    return [ clang_path, clangxx_path ]

def main():
    # --- defaults ---
    LLVM_VERSION = os.environ.get("LLVM_VERSION", "21.1.3")
    BUSTER_CI = os.environ.get("BUSTER_CI", "0")

    CMAKE_BUILD_TYPE = os.environ.get("CMAKE_BUILD_TYPE")
    LLVM_CMAKE_BUILD_TYPE = os.environ.get("LLVM_CMAKE_BUILD_TYPE")

    if not CMAKE_BUILD_TYPE:
        CMAKE_BUILD_TYPE = "Debug"
        LLVM_CMAKE_BUILD_TYPE = "Release"
    elif not LLVM_CMAKE_BUILD_TYPE:
        LLVM_CMAKE_BUILD_TYPE = CMAKE_BUILD_TYPE

    # --- OS detection ---
    OSTYPE = sys.platform
    if OSTYPE.startswith("darwin"):
        BUSTER_OS = "macos"
    elif OSTYPE.startswith("linux"):
        BUSTER_OS = "linux"
    elif OSTYPE.startswith("msys") or OSTYPE.startswith("win"):
        BUSTER_OS = "windows"
    else:
        print(f"Unidentified OS tag: {OSTYPE}")
        return 1

    # --- Arch detection ---
    BUSTER_NATIVE_ARCH_STRING = platform.machine()
    if BUSTER_NATIVE_ARCH_STRING in ("x86_64", "AMD64"):
        BUSTER_ARCH = "x86_64"
    elif BUSTER_NATIVE_ARCH_STRING in ("arm64", "aarch64", "ARM64"):
        BUSTER_ARCH = "aarch64"
    else:
        print(f"Unidentified arch tag: {BUSTER_NATIVE_ARCH_STRING}")
        return 1

    LLVM_BASENAME = f"llvm_{LLVM_VERSION}_{BUSTER_ARCH}-{BUSTER_OS}-{LLVM_CMAKE_BUILD_TYPE}"
    INSTALL_PATH = Path.home() / "dev/toolchain/install"

    # --- CMAKE_PREFIX_PATH ---
    CMAKE_PREFIX_PATH = os.environ.get("CMAKE_PREFIX_PATH")
    if not CMAKE_PREFIX_PATH:
        CMAKE_PREFIX_PATH = str(
            INSTALL_PATH / LLVM_BASENAME
        )
    print(CMAKE_PREFIX_PATH)

    if Path(CMAKE_PREFIX_PATH).is_dir() == False:
        LLVM_7Z = LLVM_BASENAME + ".7z"
        LLVM_URL = f"https://github.com/buster14a/toolchain/releases/download/v{LLVM_VERSION}/" + LLVM_7Z
        LLVM_7Z_PATH = Path(LLVM_7Z)
        print(f"Toolchain not found. Downloading {LLVM_URL} -> {LLVM_7Z_PATH}...")
        urllib.request.urlretrieve(LLVM_URL, LLVM_7Z_PATH)

        print(f"Extracting {LLVM_7Z_PATH} -> {INSTALL_PATH}")
        INSTALL_PATH.mkdir(parents=True, exist_ok=True)
        extract_result = subprocess.run(["7z", "x", str(LLVM_7Z_PATH), f"-o{INSTALL_PATH}", "-y"], text=True)

        if extract_result.returncode != 0:
            print("Extraction failed")
            return 1

    limine_path_str = "limine"

    if not os.path.isdir(limine_path_str):
        limine_clone = subprocess.run([
            "git",
            "clone",
            "https://codeberg.org/Limine/Limine.git",
            limine_path_str,
            "--branch=v10.x-binary",
            "--depth=1",
        ], text=True)
        if limine_clone.returncode != 0:
            print("Limine clone failed")
            return 1

    # --- OPT_ARGS ---
    build_type = CMAKE_BUILD_TYPE.split("-", 1)[0]
    if build_type == "Debug":
        OPT_ARGS = "-O0 -g"
    elif build_type.startswith("Release"):
        OPT_ARGS = "-O3"
    elif build_type.startswith("RelWithDebInfo"):
        OPT_ARGS = "-O2 -g"
    else:
        print(f"Unidentified build_type tag: {build_type}")
        return 1

    # --- CMAKE_LINKER_TYPE ---
    if BUSTER_OS == "linux" and BUSTER_CI == "0":
        CMAKE_LINKER_TYPE = "MOLD"
    else:
        CMAKE_LINKER_TYPE = "LLD"

    if BUSTER_OS == "macos":
        xc_sdk_path_result = subprocess.run([
            "xcrun",
            "--show-sdk-path",
        ], capture_output=True, text=True)
        XC_SDK_PATH= xc_sdk_path_result.stdout.strip()
    else:
        XC_SDK_PATH=""
    
    if BUSTER_OS == "windows":
        EXE_EXTENSION=".exe"
    else:
        EXE_EXTENSION=""

    # --- compilers ---
    CLANG_PATH = f"{CMAKE_PREFIX_PATH}/bin/clang{EXE_EXTENSION}"
    CLANGXX_PATH = f"{CMAKE_PREFIX_PATH}/bin/clang++{EXE_EXTENSION}"

    # --- cache dir ---
    BUSTER_CACHE_DIR = os.environ.get("BUSTER_CACHE_DIR", "buster-cache")
    Path(BUSTER_CACHE_DIR).mkdir(parents=True, exist_ok=True)

    # --- build dir ---
    build_dir = Path("build")
    if build_dir.exists():
        shutil.rmtree(build_dir)
    build_dir.mkdir()

    # --- run cmake ---
    env = os.environ.copy()
    env["PATH"] = f"{CMAKE_PREFIX_PATH}/bin:" + env["PATH"]
    result = subprocess.run([
        "cmake",
        "--log-level=VERBOSE",
        "..",
        "-G", "Ninja",
        f"-DCMAKE_PREFIX_PATH={CMAKE_PREFIX_PATH}",
        f"-DCMAKE_BUILD_TYPE={CMAKE_BUILD_TYPE}",
        f"-DCMAKE_CXX_COMPILER={CLANGXX_PATH}",
        f"-DCMAKE_C_COMPILER={CLANG_PATH}",
        f"-DCMAKE_LINKER_TYPE={CMAKE_LINKER_TYPE}",
        "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON",
        "-DCMAKE_COLOR_DIAGNOSTICS=ON",
        f"-DXC_SDK_PATH={XC_SDK_PATH}",
    ], cwd="build", env=env)

    return_code = result.returncode

    if return_code == 0:
        print("CMake process launch succeeded!")
    else:
        print("CMake process launch failed!")

    return result.returncode

if __name__ == "__main__":
    sys.exit(main())
