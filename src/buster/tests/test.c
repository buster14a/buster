// Test registration and the in-process runner. test_descriptors is the
// single table every module test registers in — a new test pair adds its
// row here in registration order, its sources to CMakeLists.txt, and its
// includes below (AGENTS.md). library_tests runs the table, prints the
// TEST_MODULE_TIMING lines the test_timing_summary diagnostic consumes,
// and honors BUSTER_TEST_JOBS. A descriptor marked table_audit runs only
// on the canonical tree per platform (BUSTER_TEST_TABLE_AUDITS, default
// on) — reserve that flag for results that are a pure function of the
// generated tables and repository source.

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
#include <buster/lib/compiler/frontend/c/c.h>
#include <buster/lib/compiler/assembly/aarch64_encoding.h>
#include <buster/lib/compiler/assembly/aarch64_exact_bridge.h>
#include <buster/lib/compiler/assembly/aarch64_control_semantics.h>
#include <buster/lib/compiler/assembly/aarch64_system_registers.h>
#include <buster/lib/compiler/assembly/aarch64_semantics.h>
#include <buster/lib/compiler/assembly/aarch64_system_semantics.h>
#include <buster/lib/compiler/assembly/aarch64_syntax.h>
#include <buster/lib/compiler/assembly/aarch64_semantic_vm.h>
#include <buster/lib/compiler/assembly/aarch64_direct_simd_semantics.h>
#include <buster/lib/compiler/assembly/aarch64_complex_simd_semantics.h>
#include <buster/lib/compiler/assembly/aarch64_memory_semantics.h>
#include <buster/lib/compiler/assembly/assembly.h>
#include <buster/lib/compiler/assembly/x86_64_metadata.h>
#include <buster/lib/compiler/assembly/x86_64_completion_census.h>
#include <buster/lib/compiler/ir/ir.h>
#include <buster/lib/compiler/llvm/bitcode.h>
#include <buster/lib/compiler/dwarf/dwarf.h>
#include <buster/lib/compiler/codeview/codeview.h>
#include <buster/lib/compiler/pdb/pdb.h>
#include <buster/lib/compiler/object/object.h>
#include <buster/lib/compiler/jit/jit.h>
#include <buster/lib/compiler/link/link.h>
#include <buster/lib/compiler/driver/driver.h>

#include <buster/tests/arena_test.h>
#include <buster/tests/hash_test.h>
#include <buster/tests/simd_test.h>
#include <buster/tests/string_test.h>
#include <buster/tests/os_test.h>
#include <buster/tests/file_test.h>
#include <buster/tests/target_test.h>
#include <buster/tests/compiler/frontend/c/c_test.h>
#include <buster/tests/compiler/assembly/aarch64_encoding_test.h>
#include <buster/tests/compiler/assembly/aarch64_exact_bridge_test.h>
#include <buster/tests/compiler/assembly/aarch64_control_semantics_test.h>
#include <buster/tests/compiler/assembly/aarch64_system_registers_test.h>
#include <buster/tests/compiler/assembly/aarch64_semantics_test.h>
#include <buster/tests/compiler/assembly/aarch64_system_semantics_test.h>
#include <buster/tests/compiler/assembly/aarch64_syntax_test.h>
#include <buster/tests/compiler/assembly/aarch64_semantic_vm_test.h>
#include <buster/tests/compiler/assembly/aarch64_direct_simd_test.h>
#include <buster/tests/compiler/assembly/aarch64_complex_simd_test.h>
#include <buster/tests/compiler/assembly/aarch64_memory_semantics_test.h>
#include <buster/tests/compiler/assembly/aarch64_alias_projection_test.h>
#include <buster/tests/compiler/assembly/assembly_test.h>
#include <buster/tests/compiler/assembly/x86_64_metadata_test.h>
#include <buster/tests/compiler/assembly/x86_64_completion_census_test.h>
#include <buster/tests/compiler/ir/ir_test.h>
#include <buster/tests/compiler/llvm/bitcode_test.h>
#include <buster/tests/compiler/codegen/machine_select_test.h>
#include <buster/tests/compiler/codegen/machine_test.h>
#include <buster/tests/compiler/codegen/codegen_test.h>
#include <buster/tests/compiler/codegen/aarch64_stride_test.h>
#include <buster/tests/compiler/debug/debug_test.h>
#include <buster/tests/compiler/dwarf/dwarf_test.h>
#include <buster/tests/compiler/codeview/codeview_test.h>
#include <buster/tests/compiler/pdb/pdb_test.h>
#include <buster/tests/compiler/object/object_test.h>
#include <buster/tests/compiler/jit/jit_test.h>
#include <buster/tests/compiler/link/link_test.h>
#include <buster/tests/compiler/gpu/gpu_test.h>
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
#include <buster/tests/target_test.c>
#include <buster/tests/compiler/frontend/c/c_test.c>
#include <buster/tests/compiler/assembly/aarch64_encoding_test.c>
#include <buster/tests/compiler/assembly/aarch64_exact_bridge_test.c>
#include <buster/tests/compiler/assembly/aarch64_control_semantics_test.c>
#include <buster/tests/compiler/assembly/aarch64_system_registers_test.c>
#include <buster/tests/compiler/assembly/aarch64_semantics_test.c>
#include <buster/tests/compiler/assembly/aarch64_system_semantics_test.c>
#include <buster/tests/compiler/assembly/aarch64_syntax_test.c>
#include <buster/tests/compiler/assembly/aarch64_semantic_vm_test.c>
#include <buster/tests/compiler/assembly/aarch64_direct_simd_test.c>
#include <buster/tests/compiler/assembly/aarch64_complex_simd_test.c>
#include <buster/tests/compiler/assembly/aarch64_memory_semantics_test.c>
#include <buster/tests/compiler/assembly/aarch64_alias_projection_test.c>
#include <buster/tests/compiler/assembly/assembly_test.c>
#include <buster/tests/compiler/assembly/x86_64_metadata_test.c>
#include <buster/tests/compiler/assembly/x86_64_completion_census_test.c>
#include <buster/tests/compiler/ir/ir_test.c>
#include <buster/tests/compiler/llvm/bitcode_test.c>
#include <buster/tests/compiler/codegen/machine_select_test.c>
#include <buster/tests/compiler/codegen/machine_test.c>
#include <buster/tests/compiler/codegen/codegen_test.c>
#include <buster/tests/compiler/codegen/aarch64_stride_test.c>
#include <buster/tests/compiler/debug/debug_test.c>
#include <buster/tests/compiler/dwarf/dwarf_test.c>
#include <buster/tests/compiler/codeview/codeview_test.c>
#include <buster/tests/compiler/pdb/pdb_test.c>
#include <buster/tests/compiler/object/object_test.c>
#include <buster/tests/compiler/jit/jit_test.c>
#include <buster/tests/compiler/link/link_test.c>
#include <buster/tests/compiler/gpu/gpu_test.c>
#include <buster/tests/compiler/driver/driver_test.c>
#if BUSTER_CPU_ARCH_X86_64
#include <buster/tests/x86_64_test.c>
#endif
#endif

#endif

#if BUSTER_INCLUDE_TESTS
typedef struct TestDescriptor TestDescriptor;
typedef enum TestDescriptorParallelKind
{
    TEST_DESCRIPTOR_PARALLEL_NONE,
    TEST_DESCRIPTOR_PARALLEL_AARCH64_DIRECT_SIMD,
    TEST_DESCRIPTOR_PARALLEL_AARCH64_COMPLEX_SIMD,
    TEST_DESCRIPTOR_PARALLEL_AARCH64_MEMORY_SEMANTICS,
} TestDescriptorParallelKind;
struct TestDescriptor
{
    String8 name;
    TestFunction* function;
    bool requires_temporary_root;
    TestDescriptorParallelKind parallel_kind;
    // A whole-table audit: its answer is a function of the generated
    // metadata tables and the source text alone, so it cannot differ between
    // compilers, configurations or optimization levels. The matrix runs it on
    // one canonical tree per platform rather than in all eight to ten, the
    // same carve-out clang_analyze already has. Off means "some other tree in
    // this matrix runs it", never "nobody does".
    bool table_audit;
};

// Table audits run unless the superbuild explicitly says another tree owns
// them. Defaulting to *on* keeps a bare `ide test`, a single-tree build and
// any future runner at full coverage; only the matrix opts a tree out.
BUSTER_GLOBAL_LOCAL bool buster_test_table_audits_enabled(void)
{
    String8 value = os_get_environment_variable(S8("BUSTER_TEST_TABLE_AUDITS"));
    return !value.length || !string_equal(value, S8("0"));
}

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

typedef enum TestId
{
    TEST_ID_ARENA,
    TEST_ID_HASH,
    TEST_ID_SIMD,
    TEST_ID_STRING,
    TEST_ID_OS,
    TEST_ID_FILE,
    TEST_ID_TARGET,
    TEST_ID_C_FRONTEND,
    TEST_ID_AARCH64_ENCODING,
    TEST_ID_AARCH64_EXACT_BRIDGE,
    TEST_ID_AARCH64_CONTROL_SEMANTICS,
    TEST_ID_AARCH64_SYSTEM_REGISTERS,
    TEST_ID_AARCH64_SEMANTICS,
    TEST_ID_AARCH64_SYSTEM_SEMANTICS,
    TEST_ID_AARCH64_SYNTAX,
    TEST_ID_AARCH64_SEMANTIC_VM,
    TEST_ID_AARCH64_DIRECT_SIMD,
    TEST_ID_AARCH64_COMPLEX_SIMD,
    TEST_ID_AARCH64_MEMORY_SEMANTICS,
    TEST_ID_AARCH64_ALIAS_PROJECTION,
    TEST_ID_ASSEMBLY,
    TEST_ID_X86_64_METADATA,
#if BUSTER_CPU_ARCH_X86_64
    TEST_ID_X86_64_COMPLETION_CENSUS,
#endif
    TEST_ID_IR,
    TEST_ID_LLVM_BITCODE,
    TEST_ID_MACHINE_SELECTION,
    TEST_ID_MACHINE,
    TEST_ID_CODEGEN,
    TEST_ID_AARCH64_STRIDE,
    TEST_ID_DEBUG_MODEL,
    TEST_ID_DWARF,
    TEST_ID_CODEVIEW,
    TEST_ID_PDB,
    TEST_ID_OBJECT,
    TEST_ID_JIT,
    TEST_ID_LINK,
    TEST_ID_GPU_PIPELINE,
    TEST_ID_COMPILER_DRIVER,
#if BUSTER_CPU_ARCH_X86_64
    TEST_ID_X86_64,
#endif
    TEST_ID_COUNT,
} TestId;

BUSTER_GLOBAL_LOCAL TestDescriptor test_descriptors[TEST_ID_COUNT] = {
    [TEST_ID_ARENA] = {S8_INITIALIZER("arena_tests"), &arena_tests},
    [TEST_ID_HASH] = {S8_INITIALIZER("hash_tests"), &hash_tests},
    [TEST_ID_SIMD] = {S8_INITIALIZER("simd_tests"), &simd_tests},
    [TEST_ID_STRING] = {S8_INITIALIZER("string_tests"), &string_tests},
    [TEST_ID_OS] = {S8_INITIALIZER("os_tests"), &os_tests, true},
    [TEST_ID_FILE] = {S8_INITIALIZER("file_tests"), &file_tests, !BUSTER_ANDROID && !BUSTER_IOS},
    [TEST_ID_TARGET] = {S8_INITIALIZER("target_tests"), &target_tests},
    [TEST_ID_C_FRONTEND] = {S8_INITIALIZER("c_frontend_tests"), &c_frontend_tests},
    [TEST_ID_AARCH64_ENCODING] = {S8_INITIALIZER("aarch64_encoding_tests"), &aarch64_encoding_tests},
    [TEST_ID_AARCH64_EXACT_BRIDGE] = {S8_INITIALIZER("aarch64_exact_bridge_tests"), &aarch64_exact_bridge_tests},
    [TEST_ID_AARCH64_CONTROL_SEMANTICS] = {S8_INITIALIZER("aarch64_control_semantics_tests"), &aarch64_control_semantics_tests},
    [TEST_ID_AARCH64_SYSTEM_REGISTERS] = {S8_INITIALIZER("aarch64_system_registers_tests"), &aarch64_system_registers_tests},
    [TEST_ID_AARCH64_SEMANTICS] = {S8_INITIALIZER("aarch64_semantics_tests"), &aarch64_semantics_tests},
    [TEST_ID_AARCH64_SYSTEM_SEMANTICS] = {S8_INITIALIZER("aarch64_system_semantics_tests"), &aarch64_system_semantics_tests},
    [TEST_ID_AARCH64_SYNTAX] = {S8_INITIALIZER("aarch64_syntax_tests"), &aarch64_syntax_tests},
    [TEST_ID_AARCH64_SEMANTIC_VM] = {S8_INITIALIZER("aarch64_semantic_vm_tests"), &aarch64_semantic_vm_tests},
    [TEST_ID_AARCH64_DIRECT_SIMD] = {S8_INITIALIZER("aarch64_direct_simd_tests"), &aarch64_direct_simd_tests, false, TEST_DESCRIPTOR_PARALLEL_AARCH64_DIRECT_SIMD},
    [TEST_ID_AARCH64_COMPLEX_SIMD] = {S8_INITIALIZER("aarch64_complex_simd_tests"), &aarch64_complex_simd_tests, false, TEST_DESCRIPTOR_PARALLEL_AARCH64_COMPLEX_SIMD},
    [TEST_ID_AARCH64_MEMORY_SEMANTICS] = {S8_INITIALIZER("aarch64_memory_semantics_tests"), &aarch64_memory_semantics_tests, false, TEST_DESCRIPTOR_PARALLEL_AARCH64_MEMORY_SEMANTICS},
    [TEST_ID_AARCH64_ALIAS_PROJECTION] = {S8_INITIALIZER("aarch64_alias_projection_tests"), &aarch64_alias_projection_tests},
    [TEST_ID_ASSEMBLY] = {S8_INITIALIZER("assembly_tests"), &assembly_tests},
    [TEST_ID_X86_64_METADATA] = {S8_INITIALIZER("x86_64_metadata_tests"), &x86_64_metadata_tests},
#if BUSTER_CPU_ARCH_X86_64
    [TEST_ID_X86_64_COMPLETION_CENSUS] = {S8_INITIALIZER("x86_64_completion_census_tests"), &x86_64_completion_census_tests, false,
                                          TEST_DESCRIPTOR_PARALLEL_NONE, true},
#endif
    [TEST_ID_IR] = {S8_INITIALIZER("ir_tests"), &ir_tests},
    [TEST_ID_LLVM_BITCODE] = {S8_INITIALIZER("llvm_bitcode_tests"), &llvm_bitcode_tests},
    [TEST_ID_MACHINE_SELECTION] = {S8_INITIALIZER("machine_selection_tests"), &machine_selection_tests},
    [TEST_ID_MACHINE] = {S8_INITIALIZER("machine_tests"), &machine_tests},
    [TEST_ID_CODEGEN] = {S8_INITIALIZER("codegen_tests"), &codegen_tests},
    [TEST_ID_AARCH64_STRIDE] = {S8_INITIALIZER("aarch64_stride_tests"), &aarch64_stride_tests},
    [TEST_ID_DEBUG_MODEL] = {S8_INITIALIZER("debug_model_tests"), &debug_model_tests},
    [TEST_ID_DWARF] = {S8_INITIALIZER("dwarf_tests"), &dwarf_tests},
    [TEST_ID_CODEVIEW] = {S8_INITIALIZER("codeview_tests"), &codeview_tests},
    [TEST_ID_PDB] = {S8_INITIALIZER("pdb_tests"), &pdb_tests, !BUSTER_IOS},
    [TEST_ID_OBJECT] = {S8_INITIALIZER("object_tests"), &object_tests},
    [TEST_ID_JIT] = {S8_INITIALIZER("jit_tests"), &jit_tests},
    [TEST_ID_LINK] = {S8_INITIALIZER("link_tests"), &link_tests, !BUSTER_ANDROID && !BUSTER_IOS},
    [TEST_ID_GPU_PIPELINE] = {S8_INITIALIZER("gpu_pipeline_tests"), &gpu_pipeline_tests},
    [TEST_ID_COMPILER_DRIVER] = {S8_INITIALIZER("compiler_driver_tests"), &compiler_driver_tests, true},
#if BUSTER_CPU_ARCH_X86_64
    [TEST_ID_X86_64] = {S8_INITIALIZER("x86_64_tests"), &x86_64_tests},
#endif
};

BUSTER_CT_CHECK(BUSTER_ARRAY_LENGTH(test_descriptors) == TEST_ID_COUNT);

typedef struct TestParallelRecord TestParallelRecord;
struct TestParallelRecord
{
    TestTimingRecord timing;
    char8* output_pointer;
    u64 output_length;
    Arena* output_arena;
    bool completed;
    u8 reserved[7];
};

typedef struct TestParallelState TestParallelState;
struct TestParallelState
{
    TestDescriptor* descriptors;
    u64* eligible_indices;
    TestParallelRecord* records;
    u64 eligible_count;
};

typedef struct TestParallelArguments TestParallelArguments;
struct TestParallelArguments
{
    UnitTestArguments base;
    Arena* output_arena;
};

BUSTER_GLOBAL_LOCAL void test_parallel_show(UnitTestArguments* arguments, String8 format, ...)
{
    TestParallelArguments* parallel_arguments = (TestParallelArguments*)arguments;
    if (!parallel_arguments->output_arena)
    {
        return;
    }
    va_list variable_arguments;
    va_start(variable_arguments, format);
    String8 text = string_format_va(parallel_arguments->output_arena, format, variable_arguments);
    va_end(variable_arguments);
    BUSTER_UNUSED(text);
}

BUSTER_GLOBAL_LOCAL UnitTestResult test_parallel_call(TestDescriptorParallelKind kind, UnitTestArguments* arguments)
{
    switch (kind)
    {
        case TEST_DESCRIPTOR_PARALLEL_AARCH64_DIRECT_SIMD: return aarch64_direct_simd_tests(arguments);
        case TEST_DESCRIPTOR_PARALLEL_AARCH64_COMPLEX_SIMD: return aarch64_complex_simd_tests(arguments);
        case TEST_DESCRIPTOR_PARALLEL_AARCH64_MEMORY_SEMANTICS: return aarch64_memory_semantics_tests(arguments);
        case TEST_DESCRIPTOR_PARALLEL_NONE: break;
    }
    return (UnitTestResult){0};
}

BUSTER_GLOBAL_LOCAL ThreadReturnType test_parallel_lane(void* argument)
{
    TestParallelState* state = (TestParallelState*)argument;
    LaneRange range = lane_range(state->eligible_count);
    for (u64 work_index = range.start; work_index < range.end; work_index += 1)
    {
        u64 descriptor_index = state->eligible_indices[work_index];
        TestDescriptor descriptor = state->descriptors[descriptor_index];
        // Match the test harness's bounded working reservation. It is a
        // lazy/no-reserve mapping; only the initial 64 KiB is committed per
        // active lane.
        Arena* arena = arena_create((ArenaCreation){.reserved_size = BUSTER_MB(256), .initial_size = BUSTER_KB(64)});
        Arena* output_arena = arena_create((ArenaCreation){.reserved_size = BUSTER_MB(1), .initial_size = BUSTER_KB(64)});
        BUSTER_CHECK(arena != 0 && output_arena != 0);
        TestParallelArguments arguments = {.base = {.arena = arena, .show = &test_parallel_show}, .output_arena = output_arena};
        TimeDataType start = timestamp_take();
        UnitTestResult result = test_parallel_call(descriptor.parallel_kind, &arguments.base);
        TimeDataType end = timestamp_take();
        TestParallelRecord* record = &state->records[descriptor_index];
        record->timing = (TestTimingRecord){.index = descriptor_index, .module = descriptor.name, .duration_ns = timestamp_ns_between(start, end), .result = result};
        record->output_pointer = (char8*)arena_buffer_start(output_arena);
        record->output_length = arena_buffer_size(output_arena);
        record->output_arena = output_arena;
        record->completed = true;
        BUSTER_CHECK(arena_destroy(arena, 1));
    }
}

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

void buster_test_error_arguments(UnitTestArguments* arguments, u32 line, String8 function, String8 file_path, String8 format, ...)
{
    TemporalArena scratch = scratch_begin(0, 0);
    va_list variable_arguments;
    va_start(variable_arguments, format);
    String8 message = string_format_va(scratch.arena, format, variable_arguments);
    va_end(variable_arguments);

    arguments->show(arguments, S8("{S8} failed at {S8}:{S8}:{u32}\n"), message, file_path, function, line);
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
BUSTER_GLOBAL_LOCAL bool buster_test_temporary_root_failure_injected;
BUSTER_GLOBAL_LOCAL bool buster_test_temporary_root_failure_self_test_active;
#if BUSTER_INCLUDE_TESTS
BUSTER_GLOBAL_LOCAL bool buster_test_temporary_root_failure_body_called;
#endif
BUSTER_GLOBAL_LOCAL u64 buster_test_temporary_path_call_count;

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

    if (buster_test_temporary_root_failure_injected)
    {
        return false;
    }

    String8 base = buster_test_temporary_base();
    String8 separator = base.length && (base.pointer[base.length - 1] == '/' || base.pointer[base.length - 1] == '\\') ? S8("") : S8("/");
    // The PID separates simultaneous test processes. The serial separates
    // repeated library_tests calls in one process, and the monotonic clock
    // prevents a reused PID from selecting an old root in the same boot.
    u64 process_id = os_get_current_process_id();
    u64 serial = ++buster_test_temporary_root_serial;
    u64 timestamp = timestamp_ns_between((TimeDataType){0}, timestamp_take());
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
    if (buster_test_temporary_root_failure_self_test_active)
    {
        buster_test_temporary_path_call_count += 1;
    }

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

BUSTER_GLOBAL_LOCAL BatchTestResult buster_test_run_descriptors(UnitTestArguments* arguments, TestDescriptor* descriptors, u64 descriptor_count,
                                                                 bool timing_enabled, u64* timing_record_count, u64 descriptor_index_base)
{
    BatchTestResult result = {0};
    for (u64 i = 0; i < descriptor_count; i += 1)
    {
        TestDescriptor descriptor = descriptors[i];
        BatchTestResult result_before_descriptor = result;

        // Another tree in this matrix owns the whole-table audits. Skip
        // without a timing row so the module's per-runner timing series stays
        // a series of real runs rather than one salted with zeroes.
        if (descriptor.table_audit && !buster_test_table_audits_enabled()) continue;

        // A descriptor that can create an artifact must not enter its body
        // until the root has been created and probed. Otherwise a setup
        // failure would turn the empty path returned by the accessor into
        // paths such as /top.txt when the descriptor formats a child path.
        if (descriptor.requires_temporary_root && !buster_test_temporary_root.length && !buster_test_temporary_root_failed)
        {
            buster_test_temporary_root_failed = !buster_test_temporary_root_create();
        }
        if (buster_test_temporary_root_failed)
        {
            result = result_before_descriptor;
            result.unit_test_count += 1;
            break;
        }

        u64 arena_position = arguments->arena->position;
        if (timing_enabled)
        {
            TestTimingRecord timing = test_timing_run_descriptor(arguments, descriptor, descriptor_index_base + i);
            consume_unit_tests(&result, timing.result);
            arena_set_position(arguments->arena, arena_position);
            test_timing_report(arguments, timing);
            *timing_record_count += 1;
        }
        else
        {
            UnitTestResult unit_test_result = descriptor.function(arguments);
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
    return result;
}

BUSTER_GLOBAL_LOCAL BatchTestResult buster_test_run_parallel_descriptors(UnitTestArguments* arguments, TestDescriptor* descriptors, u64 descriptor_count,
                                                                          bool timing_enabled, u64* timing_record_count)
{
    BatchTestResult result = {0};
    u64 eligible_count = 0;
    for (u64 index = 0; index < descriptor_count; index += 1)
    {
        eligible_count += descriptors[index].parallel_kind != TEST_DESCRIPTOR_PARALLEL_NONE &&
                          (!descriptors[index].table_audit || buster_test_table_audits_enabled());
    }
    if (!eligible_count)
    {
        return buster_test_run_descriptors(arguments, descriptors, descriptor_count, timing_enabled, timing_record_count, 0);
    }

    u64 arena_position = arguments->arena->position;
    u64* eligible_indices = arena_allocate(arguments->arena, u64, eligible_count);
    TestParallelRecord* records = arena_allocate(arguments->arena, TestParallelRecord, descriptor_count);
    memset(records, 0, sizeof(*records) * descriptor_count);
    u64 eligible_index = 0;
    for (u64 index = 0; index < descriptor_count; index += 1)
    {
        if (descriptors[index].parallel_kind != TEST_DESCRIPTOR_PARALLEL_NONE &&
            (!descriptors[index].table_audit || buster_test_table_audits_enabled()))
        {
            eligible_indices[eligible_index++] = index;
        }
    }

    TestParallelState state = {
        .descriptors = descriptors,
        .eligible_indices = eligible_indices,
        .records = records,
        .eligible_count = eligible_count,
    };

    // The initial lane is one contiguous, side-effect-free group. Run the
    // serial prefix first and suffix after replay so thread-count-sensitive
    // modules (notably os_tests) never overlap the gang.
    u64 first_eligible = eligible_indices[0];
    u64 last_eligible = eligible_indices[eligible_count - 1];
    BUSTER_CHECK(last_eligible - first_eligible + 1 == eligible_count);
    if (first_eligible)
    {
        BatchTestResult prefix = buster_test_run_descriptors(arguments, descriptors, first_eligible, timing_enabled, timing_record_count, 0);
        result.succeeded_unit_test_count += prefix.succeeded_unit_test_count;
        result.unit_test_count += prefix.unit_test_count;
        result.succeeded_module_test_count += prefix.succeeded_module_test_count;
        result.module_test_count += prefix.module_test_count;
        result.succeeded_external_test_count += prefix.succeeded_external_test_count;
        result.external_test_count += prefix.external_test_count;
        if (buster_test_temporary_root_failed)
        {
            arena_set_position(arguments->arena, arena_position);
            return result;
        }
    }

    compiler_prewarm();
    buster_x86_metadata_prewarm();
    // The metadata and machine suites exercise x86 emission on every host,
    // including AArch64 CI. Prepare the exact-plan tables before their lanes.
    machine_x86_64_exact_prewarm();
    // Every lane in the gang below is an aarch64 suite, and each one queries
    // canonical form validity per encode and per decode.
    buster_aarch64_prewarm();
    buster_aarch64_semantics_prewarm();
    u64 requested_lanes = buster_test_worker_count(eligible_count);
    lane_run(BUSTER_MIN(requested_lanes, eligible_count), &test_parallel_lane, &state);

    for (u64 index = first_eligible; index <= last_eligible; index += 1)
    {
        TestParallelRecord* record = &records[index];
        BUSTER_CHECK(record->completed);
        consume_unit_tests(&result, record->timing.result);
        if (record->output_length)
        {
            arguments->show(arguments, S8("{S8}"), (String8){record->output_pointer, record->output_length});
        }
        if (timing_enabled)
        {
            test_timing_report(arguments, record->timing);
            *timing_record_count += 1;
        }
        BUSTER_CHECK(arena_destroy(record->output_arena, 1));
    }

    if (last_eligible + 1 < descriptor_count)
    {
        BatchTestResult suffix = buster_test_run_descriptors(arguments, descriptors + last_eligible + 1, descriptor_count - last_eligible - 1,
                                                             timing_enabled, timing_record_count, last_eligible + 1);
        result.succeeded_unit_test_count += suffix.succeeded_unit_test_count;
        result.unit_test_count += suffix.unit_test_count;
        result.succeeded_module_test_count += suffix.succeeded_module_test_count;
        result.module_test_count += suffix.module_test_count;
        result.succeeded_external_test_count += suffix.succeeded_external_test_count;
        result.external_test_count += suffix.external_test_count;
    }
    arena_set_position(arguments->arena, arena_position);
    return result;
}

BUSTER_GLOBAL_LOCAL UnitTestResult buster_test_temporary_root_failure_body(UnitTestArguments* arguments)
{
    buster_test_temporary_root_failure_body_called = true;
    String8 path = buster_test_temporary_path(arguments->arena, S8("should-not-run"), S8(""));
    BUSTER_UNUSED(path);
    return (UnitTestResult){1, 1};
}

BUSTER_GLOBAL_LOCAL bool buster_test_temporary_root_failure_self_test(UnitTestArguments* arguments)
{
    TestDescriptor descriptor = {
        .name = S8("temporary_root_failure_self_test"),
        .function = &buster_test_temporary_root_failure_body,
        .requires_temporary_root = true,
    };
    buster_test_temporary_root_failure_injected = true;
    buster_test_temporary_root_failure_self_test_active = true;
    buster_test_temporary_root_failure_body_called = false;
    buster_test_temporary_path_call_count = 0;
    buster_test_temporary_root = (String8){0};
    buster_test_temporary_root_failed = false;
    buster_test_temporary_root_owned = false;
    buster_test_temporary_root_ready = false;

    u64 timing_record_count = 0;
    BatchTestResult test = buster_test_run_descriptors(arguments, &descriptor, 1, false, &timing_record_count, 0);
    bool result = !buster_test_temporary_root_failure_body_called && buster_test_temporary_path_call_count == 0 && timing_record_count == 0 &&
                  test.succeeded_unit_test_count == 0 && test.unit_test_count == 1 && test.succeeded_module_test_count == 0 && test.module_test_count == 0 &&
                  test.succeeded_external_test_count == 0 && test.external_test_count == 0;

    buster_test_temporary_root_failure_injected = false;
    buster_test_temporary_root_failure_self_test_active = false;
    buster_test_temporary_root_failure_body_called = false;
    buster_test_temporary_path_call_count = 0;
    buster_test_temporary_root = (String8){0};
    buster_test_temporary_root_failed = false;
    buster_test_temporary_root_owned = false;
    buster_test_temporary_root_ready = false;
    return result;
}

BatchTestResult library_tests(UnitTestArguments* arguments)
{
    BatchTestResult result = {0};
    if (!arguments || !arguments->arena || !arguments->show)
    {
        result.unit_test_count = 1;
        return result;
    }

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
    buster_test_temporary_root_failure_injected = false;

    if (!buster_test_temporary_root_arena)
    {
        string_print(S8("TEST_TEMPORARY_ROOT_SETUP status=failed reason=arena allocation\n"));
        result.unit_test_count = 1;
        buster_test_temporary_arguments = 0;
        buster_test_temporary_root_arena = 0;
        buster_test_temporary_root_failed = false;
        return result;
    }

    BUSTER_CHECK(buster_test_temporary_root_failure_self_test(arguments));

    bool timing_enabled = program_state != 0 && program_flag_get(PROGRAM_FLAG_VERBOSE);
    if (timing_enabled)
    {
        BUSTER_CHECK(test_timing_self_test(arguments));
    }

    u64 timing_record_count = 0;
    result = buster_test_run_parallel_descriptors(arguments, test_descriptors, BUSTER_ARRAY_LENGTH(test_descriptors), timing_enabled, &timing_record_count);
    // Every descriptor that ran must have reported a timing row. Audits this
    // tree does not own report nothing at all rather than a zero row, so they
    // come out of the expected count instead of out of the invariant.
    u64 expected_timing_records = 0;
    for (u64 index = 0; index < BUSTER_ARRAY_LENGTH(test_descriptors); index += 1)
    {
        expected_timing_records += !test_descriptors[index].table_audit || buster_test_table_audits_enabled();
    }
    BUSTER_CHECK(!timing_enabled || buster_test_temporary_root_failed || timing_record_count == expected_timing_records);

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
