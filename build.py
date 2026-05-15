#!/usr/bin/env python3
import os
import subprocess
import sys


def main(argv):
    build_directory = os.environ.get("BUSTER_BUILD_DIRECTORY")
    if not build_directory:
        build_directory = "build"

    return subprocess.call(["ninja", "-C", build_directory, *argv])


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
