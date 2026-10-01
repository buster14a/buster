#!/usr/bin/env python3
"""Rebuild a CI log directory from readable regular files only.

Desktop logs may contain symlinks into build/.buster-cache, and
actions/upload-artifact follows links, failing on intentionally unreadable
cache entries. sanitize() moves the tree aside, copies back only regular files
and real directories without preserving links or permissions, and records
every skipped entry in artifact-sanitization.txt. It never fails on an
unreadable entry; the upload step owns the artifact verdict.

Entry points: sanitize() for callers and tests, main() for the workflow step.
"""
import argparse
import os
from pathlib import Path
import shutil
import stat
import sys

REPORT = "artifact-sanitization.txt"
UNSANITIZED_SUFFIX = "-unsanitized"
COPY_CHUNK_BYTES = 1024 * 1024


def remove(path):
    if path.is_symlink() or path.is_file():
        path.unlink()
    elif path.is_dir():
        shutil.rmtree(path, ignore_errors=True)


def copy_tree(source, root):
    skipped = []
    for directory, dirnames, filenames in os.walk(source, topdown=True, followlinks=False):
        current = Path(directory)
        relative = current.relative_to(source)
        retained_directories = []
        for name in dirnames:
            entry = current / name
            try:
                mode = entry.lstat().st_mode
            except OSError as error:
                skipped.append(f"{relative / name}: {error}")
                continue
            if stat.S_ISDIR(mode) and not stat.S_ISLNK(mode):
                retained_directories.append(name)
                (root / relative / name).mkdir(parents=True, exist_ok=True)
            else:
                skipped.append(f"{relative / name}: non-directory or symbolic link")
        dirnames[:] = retained_directories
        for name in filenames:
            entry = current / name
            destination = root / relative / name
            try:
                mode = entry.lstat().st_mode
                if not stat.S_ISREG(mode):
                    skipped.append(f"{relative / name}: non-regular file")
                    continue
                destination.parent.mkdir(parents=True, exist_ok=True)
                with entry.open("rb") as input_file, destination.open("wb") as output_file:
                    shutil.copyfileobj(input_file, output_file, length=COPY_CHUNK_BYTES)
            except OSError as error:
                destination.unlink(missing_ok=True)
                skipped.append(f"{relative / name}: {error}")
    return skipped


def sanitize(root):
    """Sanitize root in place and return the skipped entry descriptions."""
    root = Path(root)
    source = root.with_name(root.name + UNSANITIZED_SUFFIX)
    remove(source)
    if root.is_symlink() or root.is_file():
        root.unlink()
    elif root.is_dir():
        root.rename(source)
    root.mkdir(parents=True, exist_ok=True)
    skipped = copy_tree(source, root) if source.is_dir() else []
    (root / REPORT).write_text("\n".join(skipped) + ("\n" if skipped else ""), encoding="utf-8")
    shutil.rmtree(source, ignore_errors=True)
    return skipped


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--root", required=True, type=Path, help="log directory, normally RUNNER_TEMP/buster-ci")
    arguments = parser.parse_args(argv)
    skipped = sanitize(arguments.root)
    print(f"CI_LOG_SANITIZATION root={arguments.root} skipped={len(skipped)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
