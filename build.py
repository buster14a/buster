#!/usr/bin/env python3
import argparse
import subprocess
import sys


def parse_arguments(argv):
    parser = argparse.ArgumentParser(
        allow_abbrev=False,
        description="Build buster with Ninja.",
    )
    parser.add_argument(
        "--build-directory",
        "--build-dir",
        default="build",
        help="Ninja build directory.",
    )
    return parser.parse_known_args(argv)


def main(argv):
    arguments, ninja_arguments = parse_arguments(argv)

    return subprocess.call(["ninja", "-C", arguments.build_directory, *ninja_arguments])


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
