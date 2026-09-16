from pathlib import Path
import hashlib
import sys

root = Path(sys.argv[1]).resolve()
source_path = root / 'src/buster/lib/compiler/frontend/c/c_source.c'
header_path = root / 'src/buster/lib/compiler/frontend/c/c_source_internal.h'
test_path = root / 'src/buster/tests/compiler/frontend/c/once_test.c'
expected = {source_path: 'c3b5256ec8a17c108a94d00ec2b2de223014af5f', header_path: 'adda34f4edeab4309357c51990cd0447f0419ca9', test_path: '0a86bac60917c642eba26e191ec016d3a7211f38'}
for path, sha in expected.items():
    data = path.read_bytes()
    actual = hashlib.sha1(b'blob ' + str(len(data)).encode() + b'\0' + data).hexdigest()
    if actual != sha:
        raise SystemExit(f'Unexpected source revision for {path}: {actual}')

def replace_once(text, old, new):
    if text.count(old) != 1:
        raise SystemExit(f'Expected one occurrence: {old[:100]!r}')
    return text.replace(old, new, 1)

source = source_path.read_text()
header = header_path.read_text()
tests = test_path.read_text()
identity_start = source.index('typedef struct CIncludeFileIdentity CIncludeFileIdentity;')
identity_end = source.index('typedef struct CPreprocessSourceFrame CPreprocessSourceFrame;', identity_start)
identity = source[identity_start:identity_end]
source = source[:identity_start] + source[identity_end:]
records_start = source.index('// One identity record drives all three ways')
records_end = source.index('// The table stays at most half full.', records_start)
records = source[records_start:records_end]
source = source[:records_start] + source[records_end:]
records = replace_once(records, '    u32 count;\n    u32 capacity;\n', '    u32 count;\n    u32 capacity;\n#if BUSTER_INCLUDE_TESTS\n    // Actual slot examinations, including reinsertion while growing.\n    u64 probe_count;\n#endif\n')
header = replace_once(header, '// Private seam for source-map ordering and scratch-lifetime regressions.', '// Private source-map and once-file table contracts. Test hooks are absent\n// from tests-disabled builds; file identity remains separate from spelling.')
header = replace_once(header, '#if BUSTER_INCLUDE_TESTS\n', identity + records + '#if BUSTER_INCLUDE_TESTS\n')
header = replace_once(header, 'BUSTER_F_DECL void c_test_source_map_sort', 'BUSTER_F_DECL CIncludeFileStatus c_test_include_file_entry(CIncludeFileTable* table, CIncludeFileIdentity identity, String8 spelling, CIncludeFileEntry** entry_out);\nBUSTER_F_DECL bool c_test_include_file_table_grow(CIncludeFileTable* table);\nBUSTER_F_DECL void c_test_source_map_sort')
probe_macro = '''// Test instrumentation observes the real probe loops without adding state or
// work to tests-disabled compiler builds.
#if BUSTER_INCLUDE_TESTS
#define C_INCLUDE_FILE_SLOT_HASH(table, entries, slot) ((table)->probe_count += 1, (entries)[slot].hash)
#else
#define C_INCLUDE_FILE_SLOT_HASH(table, entries, slot) ((entries)[slot].hash)
#endif

'''
source = replace_once(source, '// The table stays at most half full.', probe_macro + '// The table stays at most half full.')
source = replace_once(source, 'while (entries[slot].hash)', 'while (C_INCLUDE_FILE_SLOT_HASH(table, entries, slot))')
source = replace_once(source, 'while (table->entries[slot].hash && !found)', 'while (!found && C_INCLUDE_FILE_SLOT_HASH(table, table->entries, slot))')
source = replace_once(source, 'while (table->entries[slot].hash)', 'while (C_INCLUDE_FILE_SLOT_HASH(table, table->entries, slot))')
source = replace_once(source, '// Zero marks an empty table entry.\n    return result | 1;', '// Zero marks an empty table entry. Preserve every bit of nonzero hashes\n    // instead of forcing all home buckets odd.\n    return result ? result : 1;')
wrappers = '''#undef C_INCLUDE_FILE_SLOT_HASH

#if BUSTER_INCLUDE_TESTS
CIncludeFileStatus c_test_include_file_entry(CIncludeFileTable* table, CIncludeFileIdentity identity, String8 spelling,
                                            CIncludeFileEntry** entry_out)
{
    return c_include_file_entry(table, identity, spelling, entry_out);
}

bool c_test_include_file_table_grow(CIncludeFileTable* table)
{
    return c_include_file_table_grow(table);
}
#endif

'''
source = replace_once(source, 'BUSTER_C_INTERNAL void c_include_file_diagnostic(', wrappers + 'BUSTER_C_INTERNAL void c_include_file_diagnostic(')
tests = replace_once(tests, '#include <buster/lib/file.h>', '#include <buster/lib/compiler/frontend/c/c_source_internal.h>\n#include <buster/lib/hash.h>\n#include <buster/lib/file.h>')
tests = tests.replace('c_once_test_lookup_work_bound', 'c_once_test_duplicate_imports')
old_comment = '''    // There are exactly two identity queries for every leaf. The output and
    // lexed-file rows are a deterministic work bound: all duplicate imports
    // must be suppressed, leaving one lex of each generated leaf. A probe
    // counter would tighten this oracle when the private table exposes one.
'''
tests = replace_once(tests, old_comment, '''    // Each leaf is imported twice. This fixture proves suppression and one
    // lex per file; c_once_test_table_probe_scaling measures actual probes.
''')
addition = Path(__file__).with_name('once_table_tests.c').read_text()
tests = replace_once(tests, 'UnitTestResult c_once_tests(UnitTestArguments* arguments)', addition + 'UnitTestResult c_once_tests(UnitTestArguments* arguments)')
tests = replace_once(tests, '    BUSTER_TEST_FIXTURE(arguments, c_once_test_failures);', '''    BUSTER_TEST_FIXTURE(arguments, c_once_test_failures);
    BUSTER_TEST_FIXTURE(arguments, c_once_test_table_hashes);
    BUSTER_TEST_FIXTURE(arguments, c_once_test_table_probe_scaling);
    BUSTER_TEST_FIXTURE(arguments, c_once_test_table_allocation_failure);
    BUSTER_TEST_FIXTURE(arguments, c_once_test_table_identity);''')
source_path.write_text(source)
header_path.write_text(header)
test_path.write_text(tests)
