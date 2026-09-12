#!/usr/bin/env python3
"""Pack retained native CI evidence into one verified tar.gz, losing no file.

The differential runner writes one small file per child phase; uploading each
as its own artifact entry was a measured tail (#409). This packs the evidence
tree once, re-reads the archive against every file's SHA-256 before publishing
it, and keeps result.json/summary.md readable beside it. Links and special
files are refused rather than followed. Build/test policy remains in build.c.

Entry points: pack() for callers and tests, main() for the workflow step.
plan() owns the exclusion policy; verify() owns the round-trip check.
"""
import argparse
import hashlib
import io
import os
from pathlib import Path
import shutil
import stat
import sys
import tarfile
import tempfile

ARCHIVE = "native-ci-logs.tar.gz"
SUMMARIES = ("result.json", "summary.md")
# Generated executables and objects below differential/ are reproducible from
# the retained sources and argv; the per-file upload excluded them the same way.
GENERATED = frozenset(("program", "subject.o"))
# A file replaced by a link after planning is refused, not followed.
READ_FLAGS = os.O_RDONLY | getattr(os, "O_NOFOLLOW", 0) | getattr(os, "O_BINARY", 0)


def generated(relative):
    parts = relative.split("/")
    return len(parts) > 1 and parts[0] == "differential" and parts[-1] in GENERATED


def plan(source):
    """Return sorted (relative path, is directory) entries and the excluded count.

    Paths stay plain strings: this walks tens of thousands of entries. Only
    directories retain their modes and mtimes, including generated-only ones.
    """
    entries = []
    excluded = 0
    pending = [(os.fspath(source), "")]
    while pending:
        directory, prefix = pending.pop()
        entries.append((prefix[:-1], True))
        with os.scandir(directory) as scan:
            for entry in scan:
                relative = prefix + entry.name
                if entry.is_symlink():
                    raise ValueError(f"refusing symbolic link in evidence: {relative}")
                if entry.is_dir(follow_symlinks=False):
                    pending.append((entry.path, relative + "/"))
                elif not entry.is_file(follow_symlinks=False):
                    raise ValueError(f"refusing special file in evidence: {relative}")
                elif generated(relative):
                    excluded += 1
                else:
                    entries.append((relative, False))
    entries.sort(key=lambda item: item[0].split("/"))
    return entries, excluded


def verify(archive, digests, directories):
    """Re-read the archive; require exactly the planned members and file bytes."""
    files = {}
    found = set()
    with tarfile.open(archive, "r:gz") as bundle:
        for member in bundle:
            if member.name in files or member.name in found:
                raise ValueError(f"duplicate archive member: {member.name}")
            if member.isdir():
                found.add(member.name)
            elif member.isfile():
                files[member.name] = hashlib.sha256(bundle.extractfile(member).read()).digest()
            else:
                raise ValueError(f"unexpected archive member type: {member.name}")
    if files != digests or found != directories:
        raise ValueError("archive contents differ from the evidence tree")


def pack(source, output):
    """Publish output/ARCHIVE plus summary copies; return (files packed, files excluded)."""
    source = Path(source).absolute()
    output = Path(output).absolute()
    for path in (source, output):
        if any(part.is_symlink() for part in (path, *path.parents)):
            raise ValueError(f"refusing symbolic link in evidence path: {path}")
    source = source.resolve()
    output = output.resolve()
    if output == source or source in output.parents or output in source.parents:
        raise ValueError("the upload directory must be separate from the evidence tree")
    output.mkdir(parents=True, exist_ok=True)
    archive = output / ARCHIVE
    # Nothing from an earlier attempt may be uploaded beside a failed pack.
    for name in (archive.name, ARCHIVE + ".partial") + SUMMARIES:
        (output / name).unlink(missing_ok=True)
    if any(output.iterdir()):
        raise ValueError("the upload directory contains unexpected entries")
    if not source.is_dir():
        raise ValueError(f"missing evidence directory: {source}")
    entries, excluded = plan(source)
    retained = {relative for relative, directory in entries if not directory}
    if not set(SUMMARIES) <= retained:
        raise ValueError("missing evidence summaries: result.json and summary.md are required")
    root = os.fspath(source)
    digests = {}
    directories = set()
    staging = Path(tempfile.mkdtemp(prefix=output.name + ".partial-", dir=output.parent))
    partial = staging / ARCHIVE
    try:
        with tarfile.open(partial, "w:gz", compresslevel=6, format=tarfile.PAX_FORMAT) as bundle:
            for relative, directory in entries:
                path = os.path.join(root, relative)
                info = tarfile.TarInfo(source.name + "/" + relative if relative else source.name)
                data = None
                if directory:
                    status = os.lstat(path)
                    if not stat.S_ISDIR(status.st_mode):
                        raise ValueError(f"evidence changed while packing: {relative}")
                    info.type = tarfile.DIRTYPE
                    directories.add(info.name)
                else:
                    with open(os.open(path, READ_FLAGS), "rb") as stream:
                        status = os.fstat(stream.fileno())
                        if not stat.S_ISREG(status.st_mode):
                            raise ValueError(f"evidence changed while packing: {relative}")
                        data = stream.read()
                    info.size = len(data)
                    digests[info.name] = hashlib.sha256(data).digest()
                info.mode = stat.S_IMODE(status.st_mode)
                info.mtime = status.st_mtime
                bundle.addfile(info, None if data is None else io.BytesIO(data))
                if relative in SUMMARIES and data is not None:
                    # Publish the exact bytes included in the verified archive,
                    # never a second read of a potentially changed summary.
                    (staging / relative).write_bytes(data)
        verify(partial, digests, directories)
        # Publish all three files together only after every write and close
        # succeeds. Removing the empty destination also works on Windows.
        output.rmdir()
        os.replace(staging, output)
    finally:
        if staging.exists():
            shutil.rmtree(staging)
    return len(digests), excluded


def main(argv=None):
    parser = argparse.ArgumentParser(description="Pack native CI evidence into one verified archive.")
    parser.add_argument("--source", required=True, type=Path, help="evidence tree, normally RUNNER_TEMP/buster-ci")
    parser.add_argument("--output", required=True, type=Path, help="new upload directory outside the evidence tree")
    options = parser.parse_args(argv)
    status = 1
    try:
        files, excluded = pack(options.source, options.output)
        print(f"CI_EVIDENCE packed {files} files into {options.output / ARCHIVE}; "
              f"excluded {excluded} generated program/subject.o outputs")
        status = 0
    except (OSError, ValueError, tarfile.TarError) as error:
        print(f"CI evidence packing failed: {error}", file=sys.stderr)
    return status


if __name__ == "__main__":
    sys.exit(main())
