// Record layout against an independent oracle, for every native target.
//
// Buster's two layout engines -- the sizeof/offsetof folding in c_parse.c and
// the IrType layout in c_gen.c -- place members through one authority,
// c_record_layout_place, under the target's TargetRecordLayout. They used to
// hold a copy each of the System V bit-field rule and were tested only against
// each other, so both answered System V on Windows and missed AAPCS64's
// unnamed-container alignment (#1439, #1344, #1318). Agreement between the
// engines is therefore not the oracle here: the expected bytes come from
// Clang alone, through tools/record_layout_oracle.py, and are checked in as
// record_layout_corpus.generated.h.
//
// Each corpus record R contributes three kinds of static data, so both engines
// and the static-initializer writer are observed without executing anything:
//
//   R<i>_fold    {sizeof, _Alignof, offsetof...} folded into enumerators: the
//                parse-side engine.
//   R<i>_img_m   `R x = { .m = <pattern> }`: the bits and bytes member m
//                occupies in the IR layout.
//   R<i>_wrap    `struct { char c; R r; }`: the IR layout's alignment and size.
//
// Entry: record_layout_tests. Map: record_layout_test_symbol finds a probe in
// the canonical object, record_layout_test_clang_corpus compiles the corpus
// for every target through the real driver and compares every probe.
//
// Regenerate the expectations with `tools/record_layout_oracle.py generate`
// and a Clang that knows every target triple; never from Buster's output.
#include <buster/tests/compiler/frontend/c/record_layout_test.h>
#if BUSTER_INCLUDE_TESTS
#include <buster/lib/compiler/driver/driver.h>
#include <buster/lib/file.h>
#include <buster/lib/os.h>
#include <buster/lib/string.h>

typedef struct RecordLayoutExpectation RecordLayoutExpectation;
struct RecordLayoutExpectation
{
    String8 symbol;
    String8 hex;
};

typedef struct RecordLayoutTarget RecordLayoutTarget;
struct RecordLayoutTarget
{
    String8 target;
    String8 clang_triple;
    RecordLayoutExpectation const* expectations;
    u64 expectation_count;
};

#include <buster/tests/compiler/frontend/c/record_layout_corpus.generated.h>

BUSTER_GLOBAL_LOCAL ObjectSymbol* record_layout_test_symbol(ObjectFile* object, String8 name)
{
    ObjectSymbol* result = 0;
    for (u32 index = 0; index < object->symbol_count && !result; index += 1)
    {
        ObjectSymbol* symbol = object->symbols + index;
        String8 symbol_name = symbol->name;
        // Mach-O spells C names with a leading underscore.
        if (symbol_name.length == name.length + 1 && symbol_name.pointer[0] == '_')
        {
            symbol_name = string_slice(symbol_name, 1, symbol_name.length);
        }
        if (string_equal(symbol_name, name))
        {
            result = symbol;
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL u8 record_layout_test_nibble(char8 digit)
{
    return (u8)(digit >= 'a' ? digit - 'a' + 10 : digit - '0');
}

// Whether the probe's bytes in the object are exactly the oracle's.
BUSTER_GLOBAL_LOCAL bool record_layout_test_probe_matches(ObjectFile* object, RecordLayoutExpectation expectation)
{
    ObjectSymbol* symbol = record_layout_test_symbol(object, expectation.symbol);
    u64 length = expectation.hex.length / 2;
    bool matches = symbol && symbol->section < object->section_count && symbol->value <= object->sections[symbol->section].data.length &&
                   length <= object->sections[symbol->section].data.length - symbol->value;
    for (u64 index = 0; matches && index < length; index += 1)
    {
        u8 expected = (u8)(record_layout_test_nibble(expectation.hex.pointer[index * 2]) << 4 | record_layout_test_nibble(expectation.hex.pointer[index * 2 + 1]));
        matches = object->sections[symbol->section].data.pointer[symbol->value + index] == expected;
    }
    return matches;
}

BUSTER_GLOBAL_LOCAL UnitTestResult record_layout_test_clang_corpus(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    TemporalArena temporary = scratch_begin(&arguments->arena, 1);
    String8 source_path = buster_test_temporary_path(temporary.arena, S8("buster-record-layout-corpus"), S8(".c"));
    String8 source = string_join_arena(temporary.arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(record_layout_corpus_lines), false);
    if (BUSTER_REQUIRE(arguments, file_write(source_path, BUSTER_SLICE_TO_BYTE_SLICE(source))))
    {
        for (u64 target_index = 0; target_index < BUSTER_ARRAY_LENGTH(record_layout_targets); target_index += 1)
        {
            RecordLayoutTarget target = record_layout_targets[target_index];
            TemporalArena compile_temporary = arena_begin_temporal(temporary.arena);
            String8 object_path = buster_test_temporary_path(compile_temporary.arena, S8("buster-record-layout-corpus"), S8(".o"));
            String8 command_line[] = {S8("-c"), S8("-target"), target.target, S8("-o"), object_path, source_path};
            CompilerDriverResult compiled = compiler_driver_execute_invocation(
                compile_temporary.arena, compiler_driver_parse_arguments(compile_temporary.arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(command_line)));
            if (BUSTER_REQUIRE(arguments, compiled.error == COMPILER_DRIVER_ERROR_NONE && compiled.has_object))
            {
                u64 mismatch_count = 0;
                for (u64 expectation_index = 0; expectation_index < target.expectation_count; expectation_index += 1)
                {
                    RecordLayoutExpectation expectation = target.expectations[expectation_index];
                    if (!record_layout_test_probe_matches(&compiled.object, expectation))
                    {
                        // A few names locate a regression; the count sizes it.
                        if (mismatch_count < 8)
                        {
                            arguments->show(arguments, S8("record layout: {S8} ({S8}) differs from Clang at {S8}, expected {S8}\n"), target.target,
                                            target.clang_triple, expectation.symbol, expectation.hex);
                        }
                        mismatch_count += 1;
                    }
                }
                BUSTER_TEST(arguments, mismatch_count == 0);
            }
            else
            {
                arguments->show(arguments, S8("record layout: {S8} did not compile the corpus: {S8}\n"), target.target, compiled.diagnostic);
            }
            os_file_delete(object_path);
            scratch_end(compile_temporary);
        }
        os_file_delete(source_path);
    }
    scratch_end(temporary);
    return result;
}

UnitTestResult record_layout_tests(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    BUSTER_TEST_FIXTURE(arguments, record_layout_test_clang_corpus);
    return result;
}
#endif
