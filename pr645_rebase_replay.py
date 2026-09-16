"""Replay the reviewed PR645 rebase; refuse any unexpected conflict shape."""
from pathlib import Path
import hashlib
import os
import re
import subprocess
import sys

ORIGINAL = 'e6ed9e0a59a18566b9148a8fe1f8f6bbe844473b'
MAIN = 'b8dd9857c0b8b5dcfc4f1e45159cf4155d26b19f'
LEDGER = 'docs/native-retirement-support-v1.tsv'
MATERIALIZER = 'tools/native_retirement_materializer.py'
TEST = 'tools/native_retirement_materializer_test.py'
PATTERN = re.compile(r'^<<<<<<< HEAD\n(.*?)^=======\n(.*?)^>>>>>>>[^\n]*\n', re.M | re.S)
EXPECTED = {
    '7cc5a74b': {LEDGER},
    '4547b557': {
        '.github/workflows/native-retirement-evidence.yml',
        'docs/native-retirement-census.md', LEDGER, 'tests/ci_tools_test.py',
        'tests/differential/clear_cache_host.c', 'tools/differential.c'},
    '64507711': {LEDGER},
    '2dbf2cd7': {'.github/workflows/native-retirement-contract.yml', MATERIALIZER, TEST},
    '9a0d1977': {'docs/agents/testing.md', 'src/buster/lib/file.c',
                 'src/buster/tests/compiler/frontend/c/c_test.c', TEST},
    '094c02b1': {MATERIALIZER},
    '5ae6a265': {MATERIALIZER},
    '320baccd': {LEDGER},
}


def git(*args, check=True):
    return subprocess.run(['git', *args], check=check, text=True,
                          stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                          env={**os.environ, 'GIT_EDITOR': 'true'})


def replace_blocks(path, fn, count=None):
    p = Path(path)
    text, actual = PATTERN.subn(fn, p.read_text())
    assert actual and (count is None or count == actual), (path, actual, count)
    assert not re.search(r'^(<<<<<<<|=======|>>>>>>>)', text, re.M), path
    p.write_text(text)
    git('add', path)


def rehash_ledger():
    def resolve(match):
        left = match[1].rstrip('\n').split('\t')
        right = match[2].rstrip('\n').split('\t')
        assert len(left) == len(right) == 5 and left[:3] == right[:3], (left, right)
        assert left[:3] == ['tests/ci_tools_test.py', 'support-file', 'dependency-only']
        data = Path(left[0]).read_bytes()
        left[3:] = [str(len(data)), hashlib.sha256(data).hexdigest()]
        return '\t'.join(left) + '\n'
    replace_blocks(LEDGER, resolve, 1)


assert git('rev-parse', 'HEAD').stdout.strip() == ORIGINAL
assert not git('status', '--porcelain', '--untracked-files=no').stdout
assert git('merge-base', ORIGINAL, MAIN).stdout.strip() == 'e68e4f62eb67ec8e432fe46277bfe6f9e0e472cf'
result = git('rebase', MAIN, check=False)
seen = []
while result.returncode:
    print(result.stdout, flush=True)
    commit = git('rev-parse', 'REBASE_HEAD').stdout.strip()
    short = commit[:8]
    conflicts = set(git('diff', '--name-only', '--diff-filter=U').stdout.splitlines())
    assert short in EXPECTED and short not in seen, (commit, conflicts)
    assert conflicts == EXPECTED[short], (commit, conflicts, EXPECTED[short])
    seen.append(short)
    if short in ('7cc5a74b', '64507711', '320baccd'):
        rehash_ledger()
    elif short == '4547b557':
        for path in sorted(conflicts - {LEDGER}):
            replace_blocks(path, lambda m: m[2])
        rehash_ledger()
    elif short == '2dbf2cd7':
        replace_blocks('.github/workflows/native-retirement-contract.yml', lambda m: m[1] + m[2], 2)
        git('checkout', '--ours', '--', MATERIALIZER, TEST)
        git('add', MATERIALIZER, TEST)
    elif short == '9a0d1977':
        def testing(match):
            assert match[1].startswith('  compilation and device/simulator execution remain separate CI gates.\n'), match[1][:150]
            return match[2] + match[1].split('\n', 1)[1]
        replace_blocks('docs/agents/testing.md', testing, 1)
        replace_blocks('src/buster/lib/file.c', lambda m:
            '// transfer/close failures; file_read owns padded arena and normalized APK asset\n'
            '// reads; file_map_read and file_map_unmap own optional mappings; file_copy_checked\n'
            '// streams into a staging file beside its destination and publishes it with\n'
            '// os_file_replace.\n', 1)
        def c_test(match):
            assert 'c_test_type_specifier_diagnostics' in match[1]
            assert 'c_test_transparent_union_abi' in match[2]
            return match[1] + '    }\n    return result;\n}\n\n' + match[2]
        replace_blocks('src/buster/tests/compiler/frontend/c/c_test.c', c_test, 1)
        replace_blocks(TEST, lambda m: m[2], 2)
    else:
        git('checkout', '--ours', '--', MATERIALIZER)
        git('add', MATERIALIZER)
    assert not git('diff', '--name-only', '--diff-filter=U').stdout
    result = git('rebase', '--continue', check=False)
print(result.stdout, flush=True)
assert seen == list(EXPECTED), seen
assert git('merge-base', 'HEAD', MAIN).stdout.strip() == MAIN
print('PR645_REBASE_REPLAY_OK ' + git('rev-parse', 'HEAD^{tree}').stdout.strip(), flush=True)
