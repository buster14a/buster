#include <buster/tests/test.h>

#include <buster/lib/os.h>
#include <buster/lib/arena.h>
#include <buster/lib/string.h>
#include <buster/lib/file.h>
#include <buster/lib/hash.h>
#include <buster/lib/time.h>
#if BUSTER_CPU_ARCH_X86_64
#include <buster/lib/x86_64.h>
#endif

#if BUSTER_INCLUDE_TESTS
#include <buster/lib/compiler/frontend/buster/parser.h>
#include <buster/lib/compiler/frontend/buster/analysis.h>
#include <buster/lib/compiler/frontend/c/c.h>
#include <buster/lib/compiler/assembly/assembly.h>
#include <buster/lib/compiler/assembly/x86_64_metadata.h>
#include <buster/lib/compiler/ir/ir.h>
#include <buster/lib/compiler/ir/interpreter.h>
#include <buster/lib/compiler/dwarf/dwarf.h>
#include <buster/lib/compiler/codeview/codeview.h>
#include <buster/lib/compiler/pdb/pdb.h>
#include <buster/lib/compiler/object/object.h>
#include <buster/lib/compiler/link/link.h>
#include <buster/lib/compiler/driver/driver.h>
#include <buster/lib/ide_document.h>

#include <buster/tests/arena_test.h>
#include <buster/tests/hash_test.h>
#include <buster/tests/simd_test.h>
#include <buster/tests/string_test.h>
#include <buster/tests/os_test.h>
#include <buster/tests/file_test.h>
#include <buster/tests/ide_document_test.h>
#include <buster/tests/window_test.h>
#include <buster/tests/rendering_test.h>
#include <buster/tests/ui_test.h>
#include <buster/tests/target_test.h>
#include <buster/tests/compiler/frontend/buster/parser_test.h>
#include <buster/tests/compiler/frontend/buster/analysis_test.h>
#include <buster/tests/compiler/frontend/c/c_test.h>
#include <buster/tests/compiler/assembly/assembly_test.h>
#include <buster/tests/compiler/assembly/x86_64_metadata_test.h>
#include <buster/tests/compiler/ir/ir_test.h>
#include <buster/tests/compiler/ir/interpreter_test.h>
#include <buster/tests/compiler/codegen/codegen_test.h>
#include <buster/tests/compiler/debug/debug_test.h>
#include <buster/tests/compiler/dwarf/dwarf_test.h>
#include <buster/tests/compiler/codeview/codeview_test.h>
#include <buster/tests/compiler/pdb/pdb_test.h>
#include <buster/tests/compiler/object/object_test.h>
#include <buster/tests/compiler/link/link_test.h>
#include <buster/tests/compiler/driver/driver_test.h>

#if BUSTER_CPU_ARCH_X86_64
#include <buster/tests/x86_64_test.h>
#endif

#if BUSTER_UNITY_BUILD
#include <buster/tests/arena_test.c>
#include <buster/tests/hash_test.c>
#include <buster/tests/simd_test.c>
#include <buster/tests/string_test.c>
#include <buster/tests/os_test.c>
#include <buster/tests/file_test.c>
#include <buster/tests/ide_document_test.c>
#include <buster/tests/window_test.c>
#include <buster/tests/rendering_test.c>
#include <buster/tests/ui_test.c>
#include <buster/tests/target_test.c>
#include <buster/tests/compiler/frontend/buster/parser_test.c>
#include <buster/tests/compiler/frontend/buster/analysis_test.c>
#include <buster/tests/compiler/frontend/c/c_test.c>
#include <buster/tests/compiler/assembly/assembly_test.c>
#include <buster/tests/compiler/assembly/x86_64_metadata_test.c>
#include <buster/tests/compiler/ir/ir_test.c>
#include <buster/tests/compiler/ir/interpreter_test.c>
#include <buster/tests/compiler/codegen/codegen_test.c>
#include <buster/tests/compiler/debug/debug_test.c>
#include <buster/tests/compiler/dwarf/dwarf_test.c>
#include <buster/tests/compiler/codeview/codeview_test.c>
#include <buster/tests/compiler/pdb/pdb_test.c>
#include <buster/tests/compiler/object/object_test.c>
#include <buster/tests/compiler/link/link_test.c>
#include <buster/tests/compiler/driver/driver_test.c>
#if BUSTER_CPU_ARCH_X86_64
#include <buster/tests/x86_64_test.c>
#endif
#endif

#endif

#if BUSTER_INCLUDE_TESTS
typedef struct TestDescriptor TestDescriptor;
struct TestDescriptor
{
    String8 name;
    TestFunction* function;
};

typedef struct TestTimingRecord TestTimingRecord;
struct TestTimingRecord
{
    u64 index;
    String8 module;
    u64 duration_ns;
    UnitTestResult result;
};

BUSTER_GLOBAL_LOCAL UnitTestResult test_timing_self_test_pass(UnitTestArguments* arguments)
{
    BUSTER_UNUSED(arguments);
    return (UnitTestResult){2, 2};
}

BUSTER_GLOBAL_LOCAL UnitTestResult test_timing_self_test_fail(UnitTestArguments* arguments)
{
    BUSTER_UNUSED(arguments);
    return (UnitTestResult){1, 2};
}

BUSTER_GLOBAL_LOCAL TestTimingRecord test_timing_run_descriptor(UnitTestArguments* arguments, TestDescriptor descriptor, u64 index)
{
    TimeDataType start = timestamp_take();
    UnitTestResult result = descriptor.function(arguments);
    TimeDataType end = timestamp_take();
    u64 duration_ns = timestamp_ns_between(start, end);

    return (TestTimingRecord){
        .index = index,
        .module = descriptor.name,
        .duration_ns = duration_ns,
        .result = result,
    };
}

BUSTER_GLOBAL_LOCAL bool test_timing_self_test(UnitTestArguments* arguments)
{
    TestDescriptor descriptors[] = {
        {S8("self_test_first"), &test_timing_self_test_pass},
        {S8("self_test_failed"), &test_timing_self_test_fail},
        {S8("self_test_last"), &test_timing_self_test_pass},
    };
    TestTimingRecord records[BUSTER_ARRAY_LENGTH(descriptors)] = {0};
    u64 arena_position = arguments->arena->position;
    u64 record_count = 0;
    for (u64 i = 0; i < BUSTER_ARRAY_LENGTH(descriptors); i += 1)
    {
        records[record_count] = test_timing_run_descriptor(arguments, descriptors[i], i);
        record_count += 1;
    }
    arena_set_position(arguments->arena, arena_position);

    bool result = record_count == BUSTER_ARRAY_LENGTH(descriptors);
    result = result && string_equal(records[0].module, S8("self_test_first"));
    result = result && string_equal(records[1].module, S8("self_test_failed"));
    result = result && string_equal(records[2].module, S8("self_test_last"));
    result = result && records[0].index == 0 && records[1].index == 1 && records[2].index == 2;
    result = result && records[0].result.succeeded_test_count == 2 && records[0].result.test_count == 2;
    result = result && records[1].result.succeeded_test_count == 1 && records[1].result.test_count == 2;
    result = result && records[2].result.succeeded_test_count == 2 && records[2].result.test_count == 2;
    result = result && records[1].result.test_count - records[1].result.succeeded_test_count == 1;
    return result;
}

BUSTER_GLOBAL_LOCAL void test_timing_report(UnitTestArguments* arguments, TestTimingRecord record)
{
    u64 failed_test_count = record.result.test_count - record.result.succeeded_test_count;
    String8 status = unit_test_succeeded(record.result) ? S8("pass") : S8("fail");
    arguments->show(arguments,
                    S8("TEST_MODULE_TIMING index={u64} module={S8} duration_ns={u64} passed={u64} failed={u64} assertions={u64} status={S8}\n"),
                    record.index, record.module, record.duration_ns, record.result.succeeded_test_count, failed_test_count,
                    record.result.test_count, status);
}

BUSTER_GLOBAL_LOCAL TestDescriptor test_descriptors[] = {
    {S8_INITIALIZER("arena_tests"), &arena_tests},
    {S8_INITIALIZER("hash_tests"), &hash_tests},
    {S8_INITIALIZER("simd_tests"), &simd_tests},
    {S8_INITIALIZER("string_tests"), &string_tests},
    {S8_INITIALIZER("os_tests"), &os_tests},
    {S8_INITIALIZER("file_tests"), &file_tests},
    {S8_INITIALIZER("ide_document_tests"), &ide_document_tests},
    {S8_INITIALIZER("window_tests"), &window_tests},
    {S8_INITIALIZER("rendering_tests"), &rendering_tests},
    {S8_INITIALIZER("ui_tests"), &ui_tests},
    {S8_INITIALIZER("target_tests"), &target_tests},
    {S8_INITIALIZER("parser_tokenizer_tests"), &parser_tokenizer_tests},
    {S8_INITIALIZER("parser_expression_tests"), &parser_expression_tests},
    {S8_INITIALIZER("parser_result_tests"), &parser_result_tests},
    {S8_INITIALIZER("parser_file_tests"), &parser_file_tests},
    {S8_INITIALIZER("c_frontend_tests"), &c_frontend_tests},
    {S8_INITIALIZER("assembly_tests"), &assembly_tests},
    {S8_INITIALIZER("x86_64_metadata_tests"), &x86_64_metadata_tests},
    {S8_INITIALIZER("analysis_tests"), &analysis_tests},
    {S8_INITIALIZER("ir_tests"), &ir_tests},
    {S8_INITIALIZER("ir_interpreter_tests"), &ir_interpreter_tests},
    {S8_INITIALIZER("codegen_tests"), &codegen_tests},
    {S8_INITIALIZER("debug_model_tests"), &debug_model_tests},
    {S8_INITIALIZER("dwarf_tests"), &dwarf_tests},
    {S8_INITIALIZER("codeview_tests"), &codeview_tests},
    {S8_INITIALIZER("pdb_tests"), &pdb_tests},
    {S8_INITIALIZER("object_tests"), &object_tests},
    {S8_INITIALIZER("link_tests"), &link_tests},
    {S8_INITIALIZER("compiler_driver_tests"), &compiler_driver_tests},
#if BUSTER_CPU_ARCH_X86_64
    {S8_INITIALIZER("x86_64_tests"), &x86_64_tests},
#endif
};

#if BUSTER_CPU_ARCH_X86_64
BUSTER_CT_CHECK(BUSTER_ARRAY_LENGTH(test_descriptors) == 30);
#else
BUSTER_CT_CHECK(BUSTER_ARRAY_LENGTH(test_descriptors) == 29);
#endif
#endif

bool unit_test_succeeded(UnitTestResult result)
{
    return result.succeeded_test_count == result.test_count;
}

void consume_unit_tests(BatchTestResult* batch, UnitTestResult unit_test)
{
    batch->succeeded_unit_test_count += unit_test.succeeded_test_count;
    batch->unit_test_count += unit_test.test_count;
    batch->succeeded_module_test_count += unit_test_succeeded(unit_test);
    batch->module_test_count += 1;
}

void consume_external_tests(BatchTestResult* batch, ProcessResult result)
{
    batch->succeeded_external_test_count += result == PROCESS_RESULT_SUCCESS;
    batch->external_test_count += 1;
}

void buster_test_error(u32 line, String8 function, String8 file_path, String8 format, ...)
{
    TemporalArena scratch = scratch_begin(0, 0);
    va_list variable_arguments;
    va_start(variable_arguments, format);
    String8 message = string_format_va(scratch.arena, format, variable_arguments);
    va_end(variable_arguments);

    string_print(S8("{S8} failed at {S8}:{S8}:{u32}\n"), message, file_path, function, line);
    scratch_end(scratch);

    if (is_debugger_present())
    {
        os_fail();
    }
}

String8 buster_test_temporary_path(Arena* arena, String8 name, String8 suffix)
{
#if BUSTER_WINDOWS
    String8 prefix = S8("build/");
#elif BUSTER_ANDROID
    String8 prefix = buster_android_internal_data_path.length ? buster_android_internal_data_path : S8(".");
    String8 separator = prefix.pointer[prefix.length - 1] == '/' ? S8("") : S8("/");
    return string_format_z(arena, S8("{S8}{S8}{S8}-{u64}{S8}"), prefix, separator, name, os_get_current_process_id(), suffix);
#else
    String8 prefix = S8("/tmp/");
#endif
    return string_format_z(arena, S8("{S8}{S8}-{u64}{S8}"), prefix, name, os_get_current_process_id(), suffix);
}

void default_show(UnitTestArguments* arguments, String8 format, ...)
{
    BUSTER_UNUSED(arguments);
    TemporalArena scratch = scratch_begin(0, 0);
    va_list variable_arguments;
    va_start(variable_arguments, format);
    String8 string = string_format_va(scratch.arena, format, variable_arguments);
    va_end(variable_arguments);

    if (string.length)
    {
        os_file_write(os_get_stdout(), BUSTER_SLICE_TO_BYTE_SLICE(string));
    }

    scratch_end(scratch);
}

bool batch_test_succeeded(BatchTestResult test)
{
    bool unit_result = test.succeeded_unit_test_count == test.unit_test_count;
    bool module_result = test.succeeded_module_test_count == test.module_test_count;
    bool external_result = test.succeeded_external_test_count == test.external_test_count;

    bool result = unit_result && module_result && external_result;
    return result;
}

bool batch_test_report(UnitTestArguments* arguments, BatchTestResult test)
{
    arguments->show(arguments, S8("[{u64}/{u64}] Unit tests\n"), test.succeeded_unit_test_count, test.unit_test_count);
    arguments->show(arguments, S8("[{u64}/{u64}] Module tests\n"), test.succeeded_module_test_count, test.module_test_count);
    arguments->show(arguments, S8("[{u64}/{u64}] External tests\n"), test.succeeded_external_test_count, test.external_test_count);
    return batch_test_succeeded(test);
}

#if BUSTER_INCLUDE_TESTS
u64 buster_test_worker_count(u64 requested)
{
#if BUSTER_SINGLE_THREADED
    BUSTER_UNUSED(requested);
    return 1;
#else
    u64 result = BUSTER_MAX(requested, (u64)1);
    String8 jobs_text = os_get_environment_variable(S8("BUSTER_TEST_JOBS"));
    if (jobs_text.length)
    {
        IntegerParsingU64 parsed = string8_parse_u64_decimal(jobs_text.pointer);
        if (parsed.length == jobs_text.length && parsed.value)
        {
            result = BUSTER_MIN(result, parsed.value);
        }
    }
    return result;
#endif
}

BatchTestResult library_tests(UnitTestArguments* arguments)
{
    BatchTestResult result = {0};
    // Some test modules intentionally leave a resident lane gang available
    // for later work on their selected context. Fill every compiler-global
    // read-only table before the first module can create those workers.
    compiler_prewarm();
    bool timing_enabled = program_state != 0 && program_flag_get(PROGRAM_FLAG_VERBOSE);
    if (timing_enabled)
    {
        BUSTER_CHECK(test_timing_self_test(arguments));
    }

    u64 timing_record_count = 0;
    for (u64 i = 0; i < BUSTER_ARRAY_LENGTH(test_descriptors); i += 1)
    {
        u64 arena_position = arguments->arena->position;
        if (timing_enabled)
        {
            TestTimingRecord timing = test_timing_run_descriptor(arguments, test_descriptors[i], i);
            consume_unit_tests(&result, timing.result);
            arena_set_position(arguments->arena, arena_position);
            test_timing_report(arguments, timing);
            timing_record_count += 1;
        }
        else
        {
            UnitTestResult unit_test_result = test_descriptors[i].function(arguments);
            consume_unit_tests(&result, unit_test_result);
            arena_set_position(arguments->arena, arena_position);
        }
    }
    BUSTER_CHECK(!timing_enabled || timing_record_count == BUSTER_ARRAY_LENGTH(test_descriptors));

    return result;
}
#endif
