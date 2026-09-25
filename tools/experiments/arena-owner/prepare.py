#!/usr/bin/env python3
"""Inject only a test entry seam into an isolated exact-source IDE build.
No compiler/runtime/library implementation is edited. Not a build system.
"""
import hashlib
import pathlib
import subprocess

root = pathlib.Path(__file__).resolve().parents[3]
path = root / 'src/buster/apps/ide/ide.c'
original = path.read_bytes()
blob = hashlib.sha1(b'blob ' + str(len(original)).encode() + b'\0' + original).hexdigest()
expected = subprocess.check_output(
    ['git', 'rev-parse', 'ade6ac4b6ecb21f30b61b656439bac476c145e2f:src/buster/apps/ide/ide.c'], cwd=root, text=True).strip()
if blob != expected:
    raise SystemExit(f'refusing unknown IDE source: {blob} != {expected}')
anchor = b'ProcessResult entry_point(void)\n{'
if original.count(anchor) != 1:
    raise SystemExit('IDE entry anchor is not unique')
replacement = b'BUSTER_GLOBAL_LOCAL ProcessResult arena_owner_original_entry_point(void)\n{'
seam = b'''
// Branch-only experiment seam. No production compilation logic is modified.
#include "../../../../tools/experiments/arena-owner/compiler_probe.h"
ProcessResult entry_point(void)
{
    ProcessResult result;
    if (string_equal(os_get_environment_variable(S8("BUSTER_ARENA_OWNER_COMPILER_PROBE")), S8("1")))
    {
        result = arena_owner_compiler_probe();
    }
    else
    {
        result = arena_owner_original_entry_point();
    }
    return result;
}
'''
path.write_bytes(original.replace(anchor, replacement) + seam)
print(f'original_ide_blob={blob}')
print(f'instrumented_ide_sha256={hashlib.sha256(path.read_bytes()).hexdigest()}')
