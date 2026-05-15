#!/usr/bin/env python3
import os
import platform
import shutil
import shlex
import subprocess
import sys
from pathlib import Path


def env_default(name, default):
    value = os.environ.get(name)
    if value == "" or value is None:
        return default
    return value


def remove_path(path):
    if path.is_dir() and not path.is_symlink():
        shutil.rmtree(path)
    elif path.exists() or path.is_symlink():
        path.unlink()


def command_string(command):
    return " ".join(shlex.quote(str(argument)) for argument in command)


def main(argv):
    build_directory = env_default("BUSTER_BUILD_DIRECTORY", "build")
    print(f"BUSTER_BUILD_DIRECTORY: {build_directory}", flush=True)

    build_path = Path(build_directory)
    remove_path(build_path)
    build_path.mkdir(parents=True, exist_ok=True)

    buster_ci = env_default("BUSTER_CI", "OFF")
    buster_fuzz = env_default("BUSTER_FUZZ", "OFF")
    buster_optimize = env_default("BUSTER_OPTIMIZE", "OFF")
    buster_sanitize = env_default("BUSTER_SANITIZE", "OFF")
    buster_lto = env_default("BUSTER_LTO", "OFF")
    buster_time_trace = env_default("BUSTER_TIME_TRACE", "OFF")
    buster_include_tests = env_default("BUSTER_INCLUDE_TESTS", "ON")
    buster_link_libc = env_default("BUSTER_LINK_LIBC", "ON")
    buster_cc = env_default("BUSTER_CC", "clang")

    if buster_cc in ("zig cc", "zig;cc"):
        buster_cc = "zig"

    print(f"BUSTER_CC: {buster_cc}", flush=True)

    cmake_compiler_args = []
    cmake_extra_args = []

    if "zig" in buster_cc:
        cmake_extra_args = [
            "-DCMAKE_C_LINKER_DEPFILE_SUPPORTED=FALSE",
            "-DCMAKE_C_LINK_DEPENDS_USE_LINKER=FALSE",
        ]
        buster_asm = buster_cc
        buster_cxx = buster_cc
        cmake_compiler_args = [
            f"-DCMAKE_C_COMPILER={buster_cc};cc",
            f"-DCMAKE_CXX_COMPILER={buster_cxx};c++",
            f"-DCMAKE_ASM_COMPILER={buster_asm};cc",
        ]
    elif "clang" in buster_cc:
        buster_asm = buster_cc
        buster_cxx = buster_cc.replace("clang", "clang++", 1)
    elif "gcc" in buster_cc:
        buster_asm = buster_cc
        buster_cxx = buster_cc.replace("gcc", "g++", 1)
    else:
        buster_asm = buster_cc
        buster_cxx = "clang++"

    if not cmake_compiler_args:
        cmake_compiler_args = [
            f"-DCMAKE_C_COMPILER={buster_cc}",
            f"-DCMAKE_CXX_COMPILER={buster_cxx}",
            f"-DCMAKE_ASM_COMPILER={buster_asm}",
        ]

    buster_linker = os.environ.get("BUSTER_LINKER")
    if not buster_linker:
        if platform.system() == "Linux":
            if "zig" not in buster_cc and "tcc" not in buster_cc:
                buster_linker = "MOLD"

    if not buster_linker:
        buster_linker = "DEFAULT"

    print(f"BUSTER_LINKER: {buster_linker}", flush=True)

    command = [
        "cmake",
        "--warn-uninitialized",
        "-Werror=dev",
        "-B",
        build_directory,
        "-G",
        "Ninja",
        *cmake_compiler_args,
        f"-DCMAKE_LINKER_TYPE={buster_linker}",
        f"-DBUSTER_FUZZ={buster_fuzz}",
        f"-DBUSTER_OPTIMIZE={buster_optimize}",
        f"-DBUSTER_SANITIZE={buster_sanitize}",
        f"-DBUSTER_LTO={buster_lto}",
        f"-DBUSTER_TIME_TRACE={buster_time_trace}",
        f"-DBUSTER_INCLUDE_TESTS={buster_include_tests}",
        f"-DBUSTER_CI={buster_ci}",
        f"-DBUSTER_LINK_LIBC={buster_link_libc}",
        *cmake_extra_args,
        *argv,
    ]

    print(f"+ {command_string(command)}", flush=True)
    return subprocess.call(command)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
