#!/usr/bin/env python3
import argparse
import platform
import shutil
import shlex
import subprocess
import sys
from pathlib import Path


CONFIGS = ("Debug", "Release", "RelWithDebInfo", "MinSizeRel")
OPTIMIZED_CONFIGS = ("Release", "RelWithDebInfo", "MinSizeRel")


def cmake_bool(value):
    normalized = value.upper()
    if normalized in ("ON", "TRUE", "YES", "1"):
        return "ON"
    if normalized in ("OFF", "FALSE", "NO", "0"):
        return "OFF"
    raise argparse.ArgumentTypeError(f"expected ON or OFF, got {value!r}")


def add_cmake_bool_argument(parser, name, default):
    option = name.replace("_", "-")
    parser.add_argument(
        f"--{option}",
        dest=name,
        nargs="?",
        const="ON",
        default=default,
        type=cmake_bool,
        metavar="ON|OFF",
    )
    parser.add_argument(
        f"--no-{option}",
        dest=name,
        action="store_const",
        const="OFF",
    )


def parse_arguments(argv):
    parser = argparse.ArgumentParser(
        allow_abbrev=False,
        description="Configure the buster CMake build.",
    )
    parser.add_argument(
        "--build-directory",
        "--build-dir",
        default="build",
        help="CMake build directory.",
    )
    parser.add_argument(
        "--config",
        "--configuration",
        choices=CONFIGS,
        default=None,
        help="Default CMake build configuration.",
    )
    parser.add_argument(
        "--cc",
        default="clang",
        help="C compiler command.",
    )
    parser.add_argument(
        "--linker",
        default=None,
        help="CMake linker type. Defaults to MOLD on Linux with clang/gcc, otherwise DEFAULT.",
    )
    add_cmake_bool_argument(parser, "ci", "OFF")
    add_cmake_bool_argument(parser, "fuzz", "OFF")
    add_cmake_bool_argument(parser, "optimize", None)
    add_cmake_bool_argument(parser, "sanitize", "OFF")
    add_cmake_bool_argument(parser, "lto", "OFF")
    add_cmake_bool_argument(parser, "time_trace", "OFF")
    add_cmake_bool_argument(parser, "include_tests", "ON")
    add_cmake_bool_argument(parser, "link_libc", "ON")
    return parser.parse_known_args(argv)


def remove_path(path):
    if path.is_dir() and not path.is_symlink():
        shutil.rmtree(path)
    elif path.exists() or path.is_symlink():
        path.unlink()


def command_string(command):
    return " ".join(shlex.quote(str(argument)) for argument in command)


def config_is_optimized(config):
    return config in OPTIMIZED_CONFIGS


def main(argv):
    arguments, cmake_arguments = parse_arguments(argv)

    build_directory = arguments.build_directory
    print(f"BUSTER_BUILD_DIRECTORY: {build_directory}", flush=True)

    build_config = arguments.config
    if build_config is None:
        build_config = "Release" if arguments.optimize == "ON" else "Debug"

    config_optimize = "ON" if config_is_optimized(build_config) else "OFF"
    optimize_was_explicit = arguments.optimize is not None
    if arguments.optimize is None:
        optimize = config_optimize
    elif arguments.optimize == config_optimize:
        optimize = arguments.optimize
    else:
        print(
            f"error: --optimize {arguments.optimize} conflicts with --config {build_config}",
            file=sys.stderr,
        )
        return 2

    print(f"BUSTER_BUILD_CONFIG: {build_config}", flush=True)

    buster_option_args = []
    if optimize_was_explicit:
        buster_option_args.append(f"-DBUSTER_OPTIMIZE={optimize}")

    build_path = Path(build_directory)
    remove_path(build_path)
    build_path.mkdir(parents=True, exist_ok=True)

    cc = arguments.cc
    if cc in ("zig cc", "zig;cc"):
        cc = "zig"

    print(f"BUSTER_CC: {cc}", flush=True)

    cmake_compiler_args = []
    cmake_extra_args = []

    if "zig" in cc:
        cmake_extra_args = [
            "-DCMAKE_C_LINKER_DEPFILE_SUPPORTED=FALSE",
            "-DCMAKE_C_LINK_DEPENDS_USE_LINKER=FALSE",
        ]
        c_compiler = cc
        cxx_compiler = cc
        asm_compiler = cc
        cmake_compiler_args = [
            f"-DCMAKE_C_COMPILER={c_compiler};cc",
            f"-DCMAKE_CXX_COMPILER={cxx_compiler};c++",
            f"-DCMAKE_ASM_COMPILER={asm_compiler};cc",
        ]
    elif "clang" in cc:
        c_compiler = cc
        cxx_compiler = cc.replace("clang", "clang++", 1)
        asm_compiler = cc
    elif "gcc" in cc:
        c_compiler = cc
        cxx_compiler = cc.replace("gcc", "g++", 1)
        asm_compiler = cc
    elif "cl" in cc:
        c_compiler = cc
        cxx_compiler = cc
        asm_compiler = cc
    else:
        c_compiler = cc
        cxx_compiler = ""
        asm_compiler = ""

    if not cmake_compiler_args:
        if cxx_compiler == "" or asm_compiler == "":
            cmake_compiler_args = [
                f"-DCMAKE_C_COMPILER={c_compiler}",
            ]
        else:
            cmake_compiler_args = [
                f"-DCMAKE_C_COMPILER={c_compiler}",
                f"-DCMAKE_CXX_COMPILER={cxx_compiler}",
                f"-DCMAKE_ASM_COMPILER={asm_compiler}",
            ]

    linker = arguments.linker
    if not linker:
        if platform.system() == "Linux":
            if "zig" not in cc and "tcc" not in cc:
                linker = "MOLD"

    if not linker:
        linker = "DEFAULT"

    print(f"BUSTER_LINKER: {linker}", flush=True)

    command = [
        "cmake",
        "--warn-uninitialized",
        "-Werror=dev",
        "-B",
        build_directory,
        "-G",
        "Ninja Multi-Config",
        f"-DCMAKE_DEFAULT_BUILD_TYPE={build_config}",
        f"-DCMAKE_CONFIGURATION_TYPES={';'.join(CONFIGS)}",
        *cmake_compiler_args,
        f"-DCMAKE_LINKER_TYPE={linker}",
        f"-DBUSTER_FUZZ={arguments.fuzz}",
        *buster_option_args,
        f"-DBUSTER_SANITIZE={arguments.sanitize}",
        f"-DBUSTER_LTO={arguments.lto}",
        f"-DBUSTER_TIME_TRACE={arguments.time_trace}",
        f"-DBUSTER_INCLUDE_TESTS={arguments.include_tests}",
        f"-DBUSTER_CI={arguments.ci}",
        f"-DBUSTER_LINK_LIBC={arguments.link_libc}",
        *cmake_extra_args,
        *cmake_arguments,
    ]

    print(f"+ {command_string(command)}", flush=True)
    return subprocess.call(command)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
