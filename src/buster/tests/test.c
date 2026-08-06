#include <buster/tests/test.h>

#include <buster/lib/os.h>
#include <buster/lib/arena.h>
#include <buster/lib/string.h>
#include <buster/lib/file.h>
#include <buster/lib/hash.h>
#include <buster/lib/time.h>
#include <buster/lib/system_headers.h>
#if BUSTER_CPU_ARCH_X86_64
#include <buster/lib/x86_64.h>
#endif

#if BUSTER_INCLUDE_TESTS
#include <buster/lib/compiler/frontend/buster/parser.h>
#include <buster/lib/compiler/frontend/buster/analysis.h>
#include <buster/lib/compiler/frontend/c/c.h>
#include <buster/lib/compiler/assembly/aarch64_encoding.h>
#include <buster/lib/compiler/assembly/assembly.h>
#include <buster/lib/compiler/assembly/x86_64_metadata.h>
#include <buster/lib/compiler/ir/ir.h>
#include <buster/lib/compiler/ir/interpreter.h>
#include <buster/lib/compiler/dwarf/dwarf.h>
#include <buster/lib/compiler/codeview/codeview.h>
#include <buster/lib/compiler/pdb/pdb.h>
#include <buster/lib/compiler/object/object.h>
#include <buster/lib/compiler/jit/jit.h>
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
#include <buster/tests/compiler/assembly/aarch64_encoding_test.h>
#include <buster/tests/compiler/assembly/assembly_test.h>
#include <buster/tests/compiler/assembly/x86_64_metadata_test.h>
#include <buster/tests/compiler/ir/ir_test.h>
#include <buster/tests/compiler/ir/interpreter_test.h>
#include <buster/tests/compiler/codegen/machine_test.h>
#include <buster/tests/compiler/codegen/codegen_test.h>
#include <buster/tests/compiler/debug/debug_test.h>
#include <buster/tests/compiler/dwarf/dwarf_test.h>
#include <buster/tests/compiler/codeview/codeview_test.h>
#include <buster/tests/compiler/pdb/pdb_test.h>
#include <buster/tests/compiler/object/object_test.h>
#include <buster/tests/compiler/jit/jit_test.h>
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
#include <buster/tests/compiler/assembly/aarch64_encoding_test.c>
#include <buster/tests/compiler/assembly/assembly_test.c>
#include <buster/tests/compiler/assembly/x86_64_metadata_test.c>
#include <buster/tests/compiler/ir/ir_test.c>
#include <buster/tests/compiler/ir/interpreter_test.c>
#include <buster/tests/compiler/codegen/machine_test.c>
#include <buster/tests/compiler/codegen/codegen_test.c>
#include <buster/tests/compiler/debug/debug_test.c>
#include <buster/tests/compiler/dwarf/dwarf_test.c>
#include <buster/tests/compiler/codeview/codeview_test.c>
#include <buster/tests/compiler/pdb/pdb_test.c>
#include <buster/tests/compiler/object/object_test.c>
#include <buster/tests/compiler/jit/jit_test.c>
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
    {S8_INITIALIZER("aarch64_encoding_tests"), &aarch64_encoding_tests},
    {S8_INITIALIZER("assembly_tests"), &assembly_tests},
    {S8_INITIALIZER("x86_64_metadata_tests"), &x86_64_metadata_tests},
    {S8_INITIALIZER("analysis_tests"), &analysis_tests},
    {S8_INITIALIZER("ir_tests"), &ir_tests},
    {S8_INITIALIZER("ir_interpreter_tests"), &ir_interpreter_tests},
    {S8_INITIALIZER("machine_tests"), &machine_tests},
    {S8_INITIALIZER("codegen_tests"), &codegen_tests},
    {S8_INITIALIZER("debug_model_tests"), &debug_model_tests},
    {S8_INITIALIZER("dwarf_tests"), &dwarf_tests},
    {S8_INITIALIZER("codeview_tests"), &codeview_tests},
    {S8_INITIALIZER("pdb_tests"), &pdb_tests},
    {S8_INITIALIZER("object_tests"), &object_tests},
    {S8_INITIALIZER("jit_tests"), &jit_tests},
    {S8_INITIALIZER("link_tests"), &link_tests},
    {S8_INITIALIZER("compiler_driver_tests"), &compiler_driver_tests},
#if BUSTER_CPU_ARCH_X86_64
    {S8_INITIALIZER("x86_64_tests"), &x86_64_tests},
#endif
};

#if BUSTER_CPU_ARCH_X86_64
BUSTER_CT_CHECK(BUSTER_ARRAY_LENGTH(test_descriptors) == 33);
#else
BUSTER_CT_CHECK(BUSTER_ARRAY_LENGTH(test_descriptors) == 32);
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

BUSTER_GLOBAL_LOCAL String8 buster_test_temporary_root;
BUSTER_GLOBAL_LOCAL u64 buster_test_temporary_root_serial;
BUSTER_GLOBAL_LOCAL Arena* buster_test_temporary_root_arena;
BUSTER_GLOBAL_LOCAL UnitTestArguments* buster_test_temporary_arguments;
BUSTER_GLOBAL_LOCAL bool buster_test_temporary_root_failed;
BUSTER_GLOBAL_LOCAL bool buster_test_temporary_root_owned;
BUSTER_GLOBAL_LOCAL bool buster_test_temporary_root_ready;

BUSTER_GLOBAL_LOCAL String8 buster_test_temporary_base(void)
{
#if BUSTER_WINDOWS
    return S8("build/");
#elif BUSTER_ANDROID
    return buster_android_internal_data_path.length ? buster_android_internal_data_path : S8(".");
#else
    return S8("/tmp/");
#endif
}

BUSTER_GLOBAL_LOCAL bool buster_test_temporary_root_make_directory(String8 path)
{
#if defined(__linux__) || defined(__APPLE__)
    BUSTER_CHECK(!path.pointer[path.length]);
    return mkdir((const char*)path.pointer, 0700) == 0;
#elif defined(_WIN32)
    TemporalArena scratch = scratch_begin(0, 0);
    String16 path_w = string16_from_string8(scratch.arena, path, true);
    bool result = CreateDirectoryW(path_w.pointer, 0) != 0;
    scratch_end(scratch);
    return result;
#else
    BUSTER_UNUSED(path);
    return false;
#endif
}

BUSTER_GLOBAL_LOCAL bool buster_test_temporary_root_create(void)
{
    UnitTestArguments* arguments = buster_test_temporary_arguments;
    Arena* arena = buster_test_temporary_root_arena;
    if (!arguments || !arena)
    {
        string_print(S8("TEST_TEMPORARY_ROOT_SETUP status=failed reason=no active library test run\n"));
        return false;
    }

    String8 base = buster_test_temporary_base();
    String8 separator = base.length && (base.pointer[base.length - 1] == '/' || base.pointer[base.length - 1] == '\\') ? S8("") : S8("/");
    // The PID separates simultaneous test processes. The serial separates
    // repeated library_tests calls in one process, and the monotonic clock
    // prevents a reused PID from selecting an old root in the same boot.
    u64 process_id = os_get_current_process_id();
    u64 serial = ++buster_test_temporary_root_serial;
    u64 timestamp = timestamp_ns_between((TimeDataType)0, timestamp_take());
    buster_test_temporary_root = string_format_z(arena, S8("{S8}{S8}buster-tests-{u64}-{u64}-{u64}"), base, separator, process_id, serial, timestamp);

    // The shared OS helper intentionally has no status return. The harness
    // needs exclusive ownership so a collision can never make teardown delete
    // a foreign directory, so create the leaf with the platform's exclusive
    // directory primitive and diagnose every failure.
    if (!buster_test_temporary_root_make_directory(buster_test_temporary_root))
    {
        arguments->show(arguments, S8("TEST_TEMPORARY_ROOT_SETUP status=failed path={S8} error={EOs}\n"), buster_test_temporary_root,
                        os_get_last_error());
        buster_test_temporary_root = (String8){0};
        return false;
    }
    buster_test_temporary_root_owned = true;

    // Probe the owned root before publishing it to callers. This catches a
    // read-only or otherwise unusable base without returning an invalid path.
    String8 probe_path = string_format_z(arena, S8("{S8}/.root-probe"), buster_test_temporary_root);
    bool probe_contained = string_starts_with_sequence(probe_path, buster_test_temporary_root) && probe_path.length > buster_test_temporary_root.length &&
                           probe_path.pointer[buster_test_temporary_root.length] == '/';
    BUSTER_CHECK(probe_contained);
    OsFileDescriptor* probe = os_file_open(probe_path, (OpenFlags){.read = 1, .write = 1, .create = 1},
                                           (OpenPermissions){.read = 1, .write = 1});
    bool result = probe != 0;
    if (probe)
    {
        result = os_file_close(probe) && result;
        result = os_file_delete(probe_path) && result;
    }

    if (!result)
    {
        arguments->show(arguments, S8("TEST_TEMPORARY_ROOT_SETUP status=failed path={S8} error={EOs}\n"), buster_test_temporary_root,
                        os_get_last_error());
        buster_test_temporary_root_ready = false;
        return false;
    }

    buster_test_temporary_root_ready = true;
    return result;
}

BUSTER_GLOBAL_LOCAL bool buster_test_temporary_root_delete(UnitTestArguments* arguments)
{
    String8 root = buster_test_temporary_root;
    bool result = buster_test_temporary_root_owned && os_directory_delete(root);
    if (!result)
    {
        arguments->show(arguments, S8("TEST_TEMPORARY_ROOT_CLEANUP status=failed path={S8} error={EOs}\n"), root, os_get_last_error());
    }
    buster_test_temporary_root = (String8){0};
    buster_test_temporary_root_owned = false;
    buster_test_temporary_root_ready = false;
    return result;
}

BUSTER_GLOBAL_LOCAL bool buster_test_temporary_component_is_safe(String8 component)
{
    if (string_equal(component, S8(".")) || string_equal(component, S8("..")))
    {
        return false;
    }
    for (u64 index = 0; index < component.length; index += 1)
    {
        if (component.pointer[index] == '/' || component.pointer[index] == '\\')
        {
            return false;
        }
    }
    return true;
}

String8 buster_test_temporary_path(Arena* arena, String8 name, String8 suffix)
{
    // Some tests intentionally exec this binary and exit from a child mode.
    // Create the root lazily so those children do not leave an empty root when
    // they never request a test artifact; the parent still owns final cleanup.
    if (!buster_test_temporary_root.length && !buster_test_temporary_root_failed)
    {
        buster_test_temporary_root_failed = !buster_test_temporary_root_create();
    }
    if (!buster_test_temporary_root_ready)
    {
        return (String8){0};
    }

    BUSTER_CHECK(buster_test_temporary_component_is_safe(name));
    BUSTER_CHECK(buster_test_temporary_component_is_safe(suffix));
    String8 result = string_format_z(arena, S8("{S8}/{S8}-{u64}{S8}"), buster_test_temporary_root, name, os_get_current_process_id(), suffix);
    bool root_contained = string_starts_with_sequence(result, buster_test_temporary_root) && result.length > buster_test_temporary_root.length &&
                          (result.pointer[buster_test_temporary_root.length] == '/' || result.pointer[buster_test_temporary_root.length] == '\\');
    BUSTER_CHECK(root_contained);
    return result;
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
    BUSTER_CHECK(c_test_lex_compact_tables_ready());
    // Keep the root path outside the per-descriptor arena rewind points. A
    // child that exits before requesting a path therefore owns no filesystem
    // root, while a parent run keeps its root alive until teardown.
    buster_test_temporary_arguments = arguments;
    buster_test_temporary_root_arena = arena_create((ArenaCreation){0});
    buster_test_temporary_root = (String8){0};
    buster_test_temporary_root_failed = false;
    buster_test_temporary_root_owned = false;
    buster_test_temporary_root_ready = false;

    bool timing_enabled = program_state != 0 && program_flag_get(PROGRAM_FLAG_VERBOSE);
    if (timing_enabled)
    {
        BUSTER_CHECK(test_timing_self_test(arguments));
    }

    u64 timing_record_count = 0;
    for (u64 i = 0; i < BUSTER_ARRAY_LENGTH(test_descriptors); i += 1)
    {
        BatchTestResult result_before_descriptor = result;
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

        if (buster_test_temporary_root_failed)
        {
            // A failed root makes every later artifact path unusable. Discard
            // the descriptor's downstream path failures and report exactly
            // one deterministic harness failure instead, preserving all
            // aggregate totals completed before this descriptor.
            result = result_before_descriptor;
            result.unit_test_count += 1;
            break;
        }
    }
    BUSTER_CHECK(!timing_enabled || buster_test_temporary_root_failed || timing_record_count == BUSTER_ARRAY_LENGTH(test_descriptors));

    bool temporary_root_succeeded = true;
    if (buster_test_temporary_root.length)
    {
        temporary_root_succeeded = buster_test_temporary_root_delete(arguments);
    }
    temporary_root_succeeded = temporary_root_succeeded && !buster_test_temporary_root_failed;
    if (!temporary_root_succeeded && !buster_test_temporary_root_failed)
    {
        result.unit_test_count += 1;
    }

    arena_destroy(buster_test_temporary_root_arena, 1);
    buster_test_temporary_root_arena = 0;
    buster_test_temporary_arguments = 0;
    buster_test_temporary_root_failed = false;

    return result;
}
#endif
