#!/usr/bin/env python3
import argparse
import shlex
import subprocess
import sys
import time


CONFIGS = ("Debug", "Release", "RelWithDebInfo", "MinSizeRel")


def parse_arguments(argv):
    if "--" in argv:
        separator = argv.index("--")
        argv, native_arguments = argv[:separator], argv[separator + 1:]
    else:
        native_arguments = []

    parser = argparse.ArgumentParser(
        allow_abbrev=False,
        description="Build buster with CMake.",
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
        help="Build configuration.",
    )
    parser.add_argument(
        "-v",
        "--verbose",
        action="store_true",
        help="Show verbose build output.",
    )
    parser.add_argument(
        "--quiet",
        action="store_true",
        help="Pass --quiet to the native build tool and suppress wrapper output.",
    )
    parser.add_argument(
        "--clean-first",
        action="store_true",
        help="Build target clean first, then build.",
    )
    parser.add_argument(
        "-j",
        "--parallel",
        nargs="?",
        const="",
        default=None,
        metavar="JOBS",
        help="Build in parallel, optionally with a job count.",
    )
    parser.add_argument(
        "-t",
        "--target",
        dest="option_targets",
        action="append",
        default=[],
        help="Build target.",
    )
    parser.add_argument("targets", nargs="*", help="Build targets.")
    arguments = parser.parse_args(argv)
    return arguments, native_arguments


def command_string(command):
    return " ".join(shlex.quote(str(argument)) for argument in command)


def timed_subprocess_call(command, description, quiet=False):
    start_ns = time.perf_counter_ns()
    try:
        return subprocess.call(command)
    finally:
        if not quiet:
            elapsed_ns = time.perf_counter_ns() - start_ns
            elapsed_seconds = elapsed_ns / 1_000_000_000
            print(
                f"{description} took {elapsed_seconds:.3f} seconds ({elapsed_ns} nanoseconds)",
                flush=True,
            )


def main(argv):
    arguments, native_arguments = parse_arguments(argv)

    command = ["cmake", "--build", arguments.build_directory]

    if arguments.config:
        command.extend(["--config", arguments.config])

    if arguments.parallel is not None:
        command.append("--parallel")
        if arguments.parallel:
            command.append(arguments.parallel)

    targets = [*arguments.option_targets, *arguments.targets]
    if targets:
        command.extend(["--target", *targets])

    if arguments.clean_first:
        command.append("--clean-first")

    if arguments.verbose:
        command.append("--verbose")

    quiet = arguments.quiet or "--quiet" in native_arguments
    if arguments.quiet and "--quiet" not in native_arguments:
        native_arguments.append("--quiet")

    if native_arguments:
        command.extend(["--", *native_arguments])

    if not quiet:
        print(f"+ {command_string(command)}", flush=True)
    return timed_subprocess_call(command, "CMake build", quiet=quiet)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
