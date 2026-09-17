#!/usr/bin/env python3
"""Fetch pinned SDK archives and publish only byte-bound census headers.

Network setup is separate from the offline materializer. Archives are verified
before reading members; each selected regular file is also checked against the
reviewed dependency descriptor. No upstream file is edited or vendored.
"""
import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import tarfile
import tempfile
import zipfile

from ci_zig import download_archive, verify_archive


def relative(value):
    path = PurePosixPath(value)
    if not value or path.is_absolute() or '..' in path.parts or path.as_posix() != value or '\\' in value:
        raise ValueError(f'noncanonical SDK path: {value}')
    return path


def verify(data, record):
    if len(data) != record['bytes'] or hashlib.sha256(data).hexdigest() != record['sha256']:
        raise ValueError(f'SDK member identity mismatch: {record["source"]}')


def publish(root, record, data):
    verify(data, record)
    path = root / relative(record['source'])
    for parent in [path, *path.parents]:
        if parent == root.parent:
            break
        if parent.is_symlink():
            raise ValueError(f'SDK destination is a symlink: {parent}')
    path.parent.mkdir(parents=True, exist_ok=True)
    if path.exists():
        verify(path.read_bytes(), record)
    else:
        with path.open('xb') as stream:
            stream.write(data)


def prepare(manifest_path, dependency_path, root, cache):
    manifest = json.loads(manifest_path.read_text())
    if manifest['version'] != 1:
        raise ValueError('unsupported SDK manifest')
    dependency = json.loads(dependency_path.read_text())
    records = {record['source']: record for record in dependency['projects']}
    files = manifest['files']
    if len({record['source'] for record in files}) != len(files):
        raise ValueError('duplicate SDK destination')
    archives = {archive['name']: archive for archive in manifest['archives']}
    for record in files:
        relative(record['source'])
        relative(record['member'])
        archive = archives[record['archive']]
        expected = records[record['source']]
        provenance = f'sdk/{archive["sha256"]}/{record["member"]}'
        if (expected['provenance'] != provenance or expected['bytes'] != record['bytes'] or
                expected['sha256'] != record['sha256']):
            raise ValueError(f'SDK dependency binding mismatch: {record["source"]}')
    expected_sources = {source for source in records if source.startswith('sdk-headers/')}
    if expected_sources != {record['source'] for record in files}:
        raise ValueError('SDK inventory does not match dependency descriptor')
    cache.mkdir(parents=True, exist_ok=True)
    for name, archive in archives.items():
        selected = {record['member']: record for record in files if record['archive'] == name}
        path = cache / archive['sha256']
        if not path.exists():
            download_archive(archive['url'], path, archive['bytes'])
        if path.stat().st_size != archive['bytes']:
            raise ValueError('SDK archive size mismatch')
        verify_archive(path, archive['sha256'])
        if zipfile.is_zipfile(path):
            with zipfile.ZipFile(path) as package:
                for member, record in selected.items():
                    info = package.getinfo(member)
                    if info.is_dir() or (info.external_attr >> 16) & 0o170000 == 0o120000:
                        raise ValueError('SDK member is not a regular file')
                    publish(root, record, package.read(info))
        else:
            with tarfile.open(path, 'r:xz') as package:
                found = set()
                for info in package:
                    if info.name in selected:
                        if not info.isfile() or info.name in found:
                            raise ValueError('SDK member is not a unique regular file')
                        found.add(info.name)
                        publish(root, selected[info.name], package.extractfile(info).read())
                if found != set(selected):
                    raise ValueError('SDK archive is missing declared headers')
    return len(files)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source-root', type=Path, default=Path('.'))
    parser.add_argument('--cache-directory', type=Path)
    args = parser.parse_args()
    root = args.source_root.resolve()
    cache = args.cache_directory or Path(tempfile.gettempdir()) / 'buster-retirement-sdk-archives'
    count = prepare(root / 'docs/native-retirement-sdks-v1.json',
                    root / 'docs/native-retirement-dependencies-v1.json', root, cache)
    print(f'NATIVE_RETIREMENT_SDKS verified_headers={count}')


if __name__ == '__main__':
    main()
