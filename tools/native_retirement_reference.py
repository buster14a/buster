#!/usr/bin/env python3
"""Retain independent Clang object controls for unresolved direct-backend rows.

The direct backend is frozen and predates MIR-only operations. Its failed raw
rows stay intact. This supplement is bound to the exact shard, input closure,
target and invocation; validation never clears a candidate failure.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def command(original, row, compiler, output):
    result = [str(compiler)]
    index = 2  # Buster's executable and cc subcommand.
    while index < len(original):
        argument = original[index]
        if argument in {'-v', '-fverify-codegen', '-fmachine-fallback', '-ffrontend-ssa', '-fno-frontend-ssa'} or argument.startswith('-fregister-allocator='):
            pass
        elif argument == '-U__GNUC__':
            # Clang's MSVC target omits GNU compatibility macros, but supports
            # the attributes and builtins used by the MinGW GNU header branch.
            result.extend(['-D__GNUC__=4', '-D__GNUC_MINOR__=2', '-D__GNUC_PATCHLEVEL__=1'])
        elif argument == '-fPIC' and ('windows' in row['target'] or row['target'].startswith('x86_64-unknown-uefi')):
            # The driver rejects the ELF switch for COFF. Its cc1 PIC level
            # retains the requested position-independent code-generation mode.
            result.extend(['-Xclang', '-pic-level', '-Xclang', '2'])
        elif argument.startswith('-mcpu='):
            cpu = argument.split('=', 1)[1]
            x86 = row['target'].startswith('x86_64-')
            if cpu == 'baseline':
                cpu = 'x86-64' if x86 else 'generic'
            result.append(('-march=' if x86 else '-mcpu=') + cpu)
        elif argument == '-target':
            index += 1
            target = original[index]
            if target.endswith('-uefi'):
                # UEFI is a freestanding ABI, not an OS recognized by Clang.
                # x64 uses LLP64/MS ABI; ARM64 uses LP64/AAPCS64 (ELF control).
                target = 'x86_64-pc-windows-msvc' if target.startswith('x86_64-') else 'aarch64-none-elf'
                result.extend(['-ffreestanding', '-fshort-wchar', '-D__UEFI__=1'])
            result.extend(['-target', target])
        elif argument == '-o':
            index += 1
            result.extend(['-o', str(output)])
        else:
            result.append(argument)
        index += 1
    if row['fixture'] == 'tests/basic_c_asm_goto_range.c':
        result.append('-DBUSTER_CLANG_RANGE_ORACLE=1')
    return result


def object_valid(data, target):
    valid = False
    arm = target.startswith('aarch64-')
    if 'apple' in target:
        valid = len(data) >= 32 and struct.unpack_from('<I', data)[0] == 0xfeedfacf and struct.unpack_from('<I', data, 4)[0] == (0x100000c if arm else 0x1000007) and struct.unpack_from('<I', data, 12)[0] == 1
    elif 'windows' in target or (target.endswith('-uefi') and not arm):
        valid = len(data) >= 20 and struct.unpack_from('<H', data)[0] == (0xaa64 if arm else 0x8664) and struct.unpack_from('<H', data, 2)[0] > 0 and struct.unpack_from('<H', data, 16)[0] == 0
    else:
        valid = len(data) >= 64 and data[:6] == b'\x7fELF\x02\x01' and struct.unpack_from('<HH', data, 16) == (1, 183 if arm else 62)
    return valid


def create(directory, compiler):
    import native_retirement_contract as contract
    directory = Path(directory).resolve()
    report = contract.validate(directory)
    output = directory / 'reference-supplement'
    output.mkdir()
    frozen = output / 'clang.exe'
    shutil.copyfile(compiler, frozen)
    frozen.chmod(0o555)
    environment = {'LANG': 'C', 'LC_ALL': 'C', 'TZ': 'UTC'}
    # Explicitly retained for local, unpacked Clang diagnostics; hosted CI uses
    # the system dynamic loader and has no additional library search path.
    if os.environ.get('LD_LIBRARY_PATH'):
        environment['LD_LIBRARY_PATH'] = os.environ['LD_LIBRARY_PATH']
    version = subprocess.run([str(frozen), '--version'], env=environment, capture_output=True, check=True)
    (output / 'version.txt').write_bytes(version.stdout)
    if b'clang version' not in version.stdout:
        raise ValueError('reference compiler is not Clang')
    results = []
    for key, outcome in report['outcomes'].items():
        if outcome['side'] != 'baseline' or not outcome['reference_failure']:
            continue
        row = report['selected_row_records'][str(outcome['row'])]
        prefix = output / row['group']
        artifact = prefix.with_suffix('.o')
        argv = command(contract.read_argv(directory / row['argv_evidence']), row, frozen, artifact)
        prefix.with_suffix('.argv.json').write_text(json.dumps(argv) + '\n')
        try:
            process = subprocess.run(argv, env=environment, capture_output=True, timeout=30)
            status, stdout, stderr = process.returncode, process.stdout, process.stderr
        except subprocess.TimeoutExpired as error:
            status, stdout, stderr = 124, error.stdout or b'', error.stderr or b''
        prefix.with_suffix('.stdout').write_bytes(stdout)
        prefix.with_suffix('.stderr').write_bytes(stderr)
        data = artifact.read_bytes() if artifact.exists() else b''
        results.append({'row': row['row'], 'group': row['group'], 'status': status,
                        'object_bytes': len(data), 'object_sha256': hashlib.sha256(data).hexdigest(),
                        'stdout_sha256': digest(prefix.with_suffix('.stdout')),
                        'stderr_sha256': digest(prefix.with_suffix('.stderr')),
                        'oracle': 'portable-bit-test' if row['fixture'] == 'tests/basic_c_asm_goto_range.c' else 'clang-object'})
    manifest = {'schema': 1, 'directory': str(directory), 'compiler_sha256': digest(frozen),
                'version_sha256': digest(output / 'version.txt'), 'environment': environment,
                'census_manifest_sha256': digest(directory / 'manifest.txt'),
                'rows_identity_sha256': report['rows_identity_sha256'],
                'input_ledger_sha256': report['input_ledger_sha256'], 'results': results}
    (output / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    failed = sum(record['status'] != 0 or record['object_bytes'] == 0 for record in results)
    print(f'REFERENCE_SUPPLEMENT shard={directory.name} controls={len(results)} failed={failed}')
    return failed


def validate(report):
    import native_retirement_contract as contract
    directory = Path(report['directory'])
    output = directory / 'reference-supplement'
    manifest = json.loads((output / 'manifest.json').read_text())
    assert manifest['schema'] == 1
    assert manifest['compiler_sha256'] == digest(output / 'clang.exe')
    assert manifest['version_sha256'] == digest(output / 'version.txt')
    assert b'clang version' in (output / 'version.txt').read_bytes()
    assert manifest['census_manifest_sha256'] == digest(directory / 'manifest.txt')
    assert manifest['rows_identity_sha256'] == report['rows_identity_sha256']
    assert manifest['input_ledger_sha256'] == report['input_ledger_sha256']
    assert all(manifest['environment'].get(key) == value for key, value in {'LANG': 'C', 'LC_ALL': 'C', 'TZ': 'UTC'}.items())
    assert set(manifest['environment']) <= {'LANG', 'LC_ALL', 'TZ', 'LD_LIBRARY_PATH'}
    expected = {str(item['row']) for item in report['outcomes'].values() if item['side'] == 'baseline' and item['reference_failure']}
    records = manifest['results']
    assert len(records) == len(expected) and {item['row'] for item in records} == expected, 'incomplete reference supplement'
    resolved = set()
    recorded = Path(manifest['directory'])
    for record in records:
        row = report['selected_row_records'][record['row']]
        assert record['group'] == row['group']
        prefix = output / row['group']
        argv = json.loads(prefix.with_suffix('.argv.json').read_text())
        oracle_root = recorded / 'reference-supplement'
        assert argv == command(contract.read_argv(directory / row['argv_evidence']), row,
                               oracle_root / 'clang.exe', oracle_root / (row['group'] + '.o')), 'reference argv mismatch'
        oracle = 'portable-bit-test' if row['fixture'] == 'tests/basic_c_asm_goto_range.c' else 'clang-object'
        assert record['oracle'] == oracle
        for stream in ['stdout', 'stderr']:
            assert digest(prefix.with_suffix('.' + stream)) == record[stream + '_sha256']
        artifact = prefix.with_suffix('.o')
        data = artifact.read_bytes() if artifact.exists() else b''
        assert len(data) == record['object_bytes'] and hashlib.sha256(data).hexdigest() == record['object_sha256']
        if record['status'] == 0 and object_valid(data, row['target']):
            resolved.add(row['group'])
    return resolved, digest(output / 'manifest.json')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directories', nargs='+', type=Path)
    parser.add_argument('--clang', default='clang')
    args = parser.parse_args()
    compiler = shutil.which(args.clang)
    if compiler is None:
        parser.error('Clang executable not found')
    failures = sum(create(directory, compiler) for directory in args.directories)
    raise SystemExit(1 if failures else 0)


if __name__ == '__main__':
    main()
