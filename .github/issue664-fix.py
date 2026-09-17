from pathlib import Path
import csv
import hashlib
import io
import subprocess

BASE = '636282c3feb696ed8f3b7f1363b230e5c34caa23'
assert subprocess.check_output(['git', 'rev-parse', 'HEAD'], text=True).strip() == BASE

# The rejected target must start from an absent path even though earlier
# positive target rows deliberately reuse the test helper's deterministic name.
path = Path('src/buster/tests/compiler/llvm/bitcode_test.c')
text = path.read_text()
start = text.index('BUSTER_GLOBAL_LOCAL UnitTestResult llvm_bitcode_test_uefi_boundary')
end = text.index('\nBUSTER_GLOBAL_LOCAL UnitTestResult ', start + 1)
body = text[start:end]
needle = '        CompilerDriverInvocation invocation = compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(command));\n'
assert body.count(needle) == 1
body = body.replace(needle, '        BUSTER_TEST(arguments, os_file_delete(output));\n' + needle)
text = text[:start] + body + text[end:]
path.write_text(text)

# Keep this ordinary-C fixture compatible with repository-wide object censuses;
# its ABI assertions are intentionally active only for the AArch64 target rows.
path = Path('tests/basic_c_aarch64_abi_contract.c')
text = path.read_text()
old = '''#if !defined(__aarch64__)
#error "AArch64 control fixture"
#endif
_Static_assert(sizeof(void *) == 8 && _Alignof(void *) == 8, "AArch64 pointer layout");'''
new = '''#if defined(__aarch64__)
_Static_assert(sizeof(void *) == 8 && _Alignof(void *) == 8, "AArch64 pointer layout");'''
assert text.count(old) == 1
text = text.replace(old, new)
needle = '''#endif
int aarch64_abi_contract(void)
'''
assert text.count(needle) == 1
text = text.replace(needle, '''#endif
#endif
int aarch64_abi_contract(void)
''')
path.write_text(text)

# Do not claim a development oracle that was not actually executed.
path = Path('docs/uefi-aarch64-abi.md')
text = path.read_text()
old = '''* The independent compile reference used during development was Clang 17.0.0,
  revision `10999b6d034fe318f3d56c83bddb6572593a8bb0`, with
  `--target=aarch64-none-elf -ffreestanding -fshort-wchar -D__UEFI__`.
  This is an AAPCS64 C/layout and assembler reference, **not** a released Clang
  UEFI target or a firmware execution result. The x86-64 control uses
  `--target=x86_64-unknown-windows -ffreestanding -D__UEFI__`.
'''
new = '''* No released-Clang AArch64 UEFI target was used as an oracle. The accepted
  contract comes from AAPCS64 and the EDK2 interface definitions above, then is
  checked by Buster's structural and pinned firmware-execution regressions.
  `aarch64-none-elf` is deliberately not presented as a UEFI execution result.
'''
assert text.count(old) == 1
path.write_text(text.replace(old, new))

# Refresh exactly the modified/new test inventory rows. A broader difference
# means the frozen contract moved unexpectedly and must be reviewed separately.
manifest = Path('docs/native-retirement-support-v1.tsv')
rows = {}
with manifest.open(newline='') as stream:
    reader = csv.DictReader(stream, delimiter='\t')
    assert reader.fieldnames == ['path', 'role', 'compile_obligation', 'bytes', 'sha256']
    for row in reader:
        assert row['path'] not in rows
        rows[row['path']] = row
tracked = subprocess.check_output(['git', 'ls-files', '-z', 'tests']).split(b'\0')
tracked = sorted(item.decode() for item in tracked if item)
tracked_set = set(tracked)
existing_set = set(rows)
new_paths = {
    'tests/basic_c_aarch64_abi_contract.c',
    'tests/basic_c_llvm_uefi_varargs.c',
    'tests/uefi_abi_contract.h',
}
assert tracked_set - existing_set == new_paths, tracked_set - existing_set
assert existing_set - tracked_set == set(), existing_set - tracked_set
changed_existing = set()
for name, row in rows.items():
    data = Path(name).read_bytes()
    digest = hashlib.sha256(data).hexdigest()
    if int(row['bytes']) != len(data) or row['sha256'] != digest:
        changed_existing.add(name)
    row['bytes'] = str(len(data))
    row['sha256'] = digest
assert changed_existing == {'tests/basic_c_uefi.c', 'tests/uefi_boot.c'}, changed_existing
for name in sorted(new_paths):
    data = Path(name).read_bytes()
    if name.endswith('.c'):
        role = 'subject'
        obligation = 'supported-object-zero-fallback'
    else:
        role = 'support-file'
        obligation = 'dependency-only'
    rows[name] = {
        'path': name,
        'role': role,
        'compile_obligation': obligation,
        'bytes': str(len(data)),
        'sha256': hashlib.sha256(data).hexdigest(),
    }
out = io.StringIO(newline='')
writer = csv.DictWriter(out, fieldnames=['path', 'role', 'compile_obligation', 'bytes', 'sha256'], delimiter='\t', lineterminator='\n')
writer.writeheader()
for name in sorted(rows):
    writer.writerow(rows[name])
manifest.write_text(out.getvalue())
print('updated issue 664 test isolation, documentation, and five inventory rows')
