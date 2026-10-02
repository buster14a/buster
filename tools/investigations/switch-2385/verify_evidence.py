"""Offline integrity/census of retained hosted artifacts; executes no artifact."""
import argparse
import collections
import hashlib
import io
import itertools
import json
import pathlib
import re
import tarfile
import zipfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('x86_zip', type=pathlib.Path)
parser.add_argument('aarch64_zip', type=pathlib.Path)
parser.add_argument('--output', type=pathlib.Path, default=pathlib.Path('switch-2385-verification'))
args = parser.parse_args()
root = args.output
inputs = [
    ('x86_64', args.x86_zip,
     '66af6925660f69bc18403dfd45e883c7092bcd9d971b3ef766e4c8fba23b1831', 11241006514, 110937460354),
    ('aarch64', args.aarch64_zip,
     'ea8f84f6b01a4e757e601cfe19e57226dd93552724aff7d304db0d64d9131fe4', 11240468101, 110937460624),
]
expected_ids = set(itertools.product((8, 16, 32, 64), (0, 1), range(5), range(5)))
summaries = []
for arch, path, zip_sha, artifact_id, job_id in inputs:
    assert hashlib.sha256(path.read_bytes()).hexdigest() == zip_sha
    out = root / 'evidence' / arch
    out.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(path) as z:
        rows = json.loads(z.read('results.json'))
        ids = [(r['width'], r['signed'], r['variant'], r['consumer']) for r in rows]
        assert len(rows) == 200 and set(ids) == expected_ids
        raw_log = z.read('probe.log').decode()
        log = raw_log
        removed = {}
        # stdio buffering interleaves test-runner diagnostics inside printf records.
        # Remove only these known complete runner messages, retaining raw log unchanged.
        for pattern in (
            r'generated.error == CODEGEN_ERROR_NONE failed at [^\n]+\n',
            r'eBPF kernel verifier/JIT[^\n]+\n',
            r'TEST_[^\n]+\n',
            r'\[\d+/\d+\][^\n]+\n',
        ):
            log, count = re.subn(pattern, '', log)
            removed[pattern] = count
        validation_pattern = r'SWITCH_VALIDATION width=(\d+) signed=(\d+) variant=(\d+) consumer=(\d+) key=(\d+) error=(\d+)'
        emission_pattern = r'SWITCH_EMIT width=(\d+) signed=(\d+) variant=(\d+) consumer=(\d+) error=(\d+)(?: fallback=(\d+))? bytes=(\d+)'
        validation_rows = [tuple(map(int, v)) for v in re.findall(validation_pattern, log)]
        emission_rows = [tuple(int(v) if v else None for v in e) for e in re.findall(emission_pattern, log)]
        assert len(validation_rows) == len(emission_rows) == 200
        assert set(v[:4] for v in validation_rows) == set(e[:4] for e in emission_rows) == expected_ids
        assert all(v[-1] == 0 for v in validation_rows)
        assert not re.sub(emission_pattern, '', re.sub(validation_pattern, '', log)).strip()
        validations = {v[:4]: v[4:] for v in validation_rows}
        emissions = {e[:4]: e[4:] for e in emission_rows}
        for r, identity in zip(rows, ids):
            width, signed, variant, consumer = identity
            mask = (1 << width) - 1
            key = (1 << width) + 7 if variant == 1 and width < 64 else mask if variant == 2 else (1 << 64) - 1 if variant == 3 else 8 if variant == 4 else 7
            input_value = (-1 if signed else mask) if variant in (2, 3) else 7
            expected = 11 if input_value % (1 << width) == key % (1 << width) else 22
            assert r['key'] == key and r['input'] == input_value and r['expected'] == expected
            assert validations[identity] == (key, 0)
            error, fallback, byte_count = emissions[identity]
            assert fallback == (None if consumer == 4 else 0)
            stem = f'w{width}-s{signed}-v{variant}-c{consumer}'
            name = stem + ('.bc' if consumer == 4 else '.o')
            if r['status'] == 'missing-artifact':
                assert arch == 'x86_64' and consumer == 0 and error == 2 and byte_count == 0 and name not in z.namelist()
            else:
                assert error == 0 and byte_count > 0
                assert hashlib.sha256(z.read(name)).hexdigest() == r['sha256']
                assert r['link_exit'] == 0 and r['exit'] == 0 and not r['stderr']
                assert r['stdout'] == str(r['observed']) + '\n'
                assert r['status'] == ('match' if r['observed'] == expected else 'mismatch')
        assert all(r['status'] == 'match' for r in rows if r['variant'] in (0, 4) or r['consumer'] == 4)
        manifest = [{'path': n, 'bytes': z.getinfo(n).file_size, 'sha256': hashlib.sha256(z.read(n)).hexdigest()} for n in sorted(z.namelist())]
        retained = {n: z.read(n) for n in z.namelist() if not n.endswith(('.o', '.bc', '.exe'))}
        retained['archive-manifest.json'] = (json.dumps(manifest, indent=2) + '\n').encode()
        retained['probe-normalized.log'] = log.encode()
        archive = out / 'hosted-text-evidence.tar.xz'
        with tarfile.open(archive, 'w:xz') as tar:
            for name, data in sorted(retained.items()):
                info = tarfile.TarInfo(name)
                info.size = len(data)
                info.mode = 0o644
                info.mtime = 0
                tar.addfile(info, io.BytesIO(data))
        # Extract only for offline disassembly; never execute downloaded artifacts.
        for name in ('w8-s0-v1-c0.o', 'w8-s0-v1-c1.o', 'w8-s0-v1-c2.o', 'w8-s0-v1-c3.o', 'w8-s0-v1-c4.exe'):
            (out / name).write_bytes(z.read(name))
        summary = {
            'architecture': arch, 'run_id': 37036971037, 'job_id': job_id,
            'artifact_id': artifact_id, 'artifact_zip_sha256': zip_sha,
            'artifact_files': len(manifest), 'retained_text_files': len(retained),
            'text_archive_sha256': hashlib.sha256(archive.read_bytes()).hexdigest(),
            'text_archive_bytes': archive.stat().st_size,
            'source_commit_and_tree': z.read('revisions.txt').decode().splitlines(),
            'compiler_version': z.read('clang.txt').decode(),
            'binary_hashes': z.read('binaries.sha256').decode(),
            'platform': z.read('platform.txt').decode(),
            'validated': len(validation_rows), 'zero_fallback_native_rows': 160,
            'counts': dict(collections.Counter(r['status'] for r in rows)),
            'per_consumer': {str(c): dict(collections.Counter(r['status'] for r in rows if r['consumer'] == c)) for c in range(5)},
            'positive_default_controls_matched': 80, 'llvm_matched': 40,
            'log_messages_removed_for_reassembly': removed,
            'nonmatching_cells': [r for r in rows if r['status'] != 'match'],
        }
        summaries.append(summary)
        (out / 'results.json').write_bytes(z.read('results.json'))
        print(json.dumps({k: v for k, v in summary.items() if k != 'nonmatching_cells'}, indent=2))
(root / 'evidence' / 'verification-summary.json').write_text(json.dumps(summaries, indent=2) + '\n')
