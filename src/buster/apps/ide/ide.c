// The `ide` executable: a headless compiler driver (`ide cc` ->
// run_c_compiler -> the compiler_driver_* API in driver.h), test runner
// (`ide test` -> compiler_run_tests -> library_tests), benchmark driver
// (`ide bench`, the BENCH_C_FRONTEND line), fuzz entrypoint, and x86-64
// completion census. The name is retained for build-script compatibility.
// The BUSTER_UNITY_BUILD include block below is the list AGENTS.md's
// module-adding rule appends to; forgetting a module there breaks
// optimized Release builds.

#define BUSTER_USE_GRAPHICS 0

#include <buster/lib/base.h>
#include <buster/lib/entry_point.h>
#include <buster/lib/time.h>
#include <buster/lib/arena.h>
#include <buster/lib/file.h>
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
#include <buster/lib/compiler/assembly/aarch64_alias_projection.h>
#include <buster/lib/compiler/assembly/assembly.h>
#include <buster/lib/compiler/assembly/x86_64_metadata.h>
#include <buster/lib/compiler/assembly/x86_64_completion_census.h>
#include <buster/lib/compiler/ir/ir.h>
#include <buster/lib/compiler/debug/debug.h>
#include <buster/lib/compiler/codegen/machine.h>
#include <buster/lib/compiler/codegen/codegen.h>
#include <buster/lib/compiler/object/object.h>
#include <buster/lib/compiler/jit/jit.h>
#include <buster/lib/compiler/link/link.h>
#include <buster/lib/compiler/wasm/wasm.h>
#include <buster/lib/compiler/gpu/gpu.h>
#include <buster/lib/compiler/llvm/bitcode.h>
#include <buster/lib/compiler/ebpf/ebpf.h>
#include <buster/lib/compiler/driver/driver.h>
#include <buster/lib/integer.h>
#include <buster/lib/string.h>
#include <buster/lib/target.h>
#if BUSTER_INCLUDE_TESTS
#include <buster/tests/test.h>
#endif

#if BUSTER_UNITY_BUILD
#if BUSTER_INCLUDE_TESTS
// Keep the intrinsic vocabulary outside the optnone region used for test
// bodies; otherwise Clang can make production SIMD intrinsics uninlinable.
#include <buster/lib/simd.h>
#if BUSTER_COMPILER_CLANG
#pragma clang attribute push (__attribute__((optnone)), apply_to=function)
#endif
#include <buster/tests/test.c>
#if BUSTER_COMPILER_CLANG
#pragma clang attribute pop
#endif
#endif
#include <buster/lib/arena.c>
#include <buster/lib/integer.c>
#include <buster/lib/os.c>
#include <buster/lib/string.c>
#include <buster/lib/entry_point.c>
#include <buster/lib/target.c>
#include <buster/lib/simd.c>
#include <buster/lib/file.c>
#if BUSTER_ANDROID || BUSTER_IOS
// Mobile platforms own the process lifecycle through the window module even
// though the compiler program itself is headless.
#include <buster/lib/window.c>
#endif
#include <buster/lib/time.c>
#include <buster/lib/float.c>
#include <buster/lib/compiler/frontend/c/c.c>
#include <buster/lib/compiler/assembly/aarch64_encoding.c>
#include <buster/lib/compiler/assembly/aarch64_exact_bridge.c>
#include <buster/lib/compiler/assembly/aarch64_control_semantics.c>
#include <buster/lib/compiler/assembly/aarch64_system_registers.c>
#include <buster/lib/compiler/assembly/aarch64_semantics.c>
#include <buster/lib/compiler/assembly/aarch64_system_semantics.c>
#include <buster/lib/compiler/assembly/aarch64_syntax.c>
#include <buster/lib/compiler/assembly/aarch64_semantic_vm.c>
#include <buster/lib/compiler/assembly/aarch64_direct_simd_semantics.c>
#include <buster/lib/compiler/assembly/aarch64_complex_simd_semantics.c>
#include <buster/lib/compiler/assembly/aarch64_memory_semantics.c>
#include <buster/lib/compiler/assembly/aarch64_alias_projection.c>
#include <buster/lib/compiler/assembly/assembly.c>
#include <buster/lib/compiler/assembly/x86_64_metadata.c>
#include <buster/lib/compiler/assembly/x86_64_completion_census.c>
#include <buster/lib/compiler/ir/ir.c>
#include <buster/lib/compiler/debug/debug.c>
#include <buster/lib/compiler/codegen/machine.c>
#include <buster/lib/compiler/codegen/codegen.c>
#include <buster/lib/compiler/dwarf/dwarf.c>
#include <buster/lib/compiler/codeview/codeview.c>
#include <buster/lib/compiler/pdb/pdb.c>
#include <buster/lib/compiler/object/object.c>
#include <buster/lib/compiler/jit/jit.c>
#include <buster/lib/compiler/link/link.c>
#include <buster/lib/compiler/wasm/wasm.c>
#include <buster/lib/compiler/gpu/gpu.c>
#include <buster/lib/compiler/llvm/bitcode.c>
#include <buster/lib/compiler/ebpf/ebpf.c>
#include <buster/lib/compiler/driver/driver.c>
#include <buster/lib/hash.c>
#endif

typedef enum CompilerCommand
{
    COMPILER_COMMAND_HELP,
    COMPILER_COMMAND_TEST,
    COMPILER_COMMAND_BENCH,
    COMPILER_COMMAND_CC,
    COMPILER_COMMAND_FUZZ,
    COMPILER_COMMAND_X86_64_COMPLETION_CENSUS,
} CompilerCommand;

typedef struct CompilerProgram CompilerProgram;
struct CompilerProgram
{
    ProgramState state;
    SliceString8 cc_arguments;
    SliceString8 fuzz_arguments;
    String8 completion_census_output_path;
    CompilerCommand command;
};

BUSTER_GLOBAL_LOCAL CompilerProgram compiler_state = {0};
BUSTER_V_IMPL ProgramState* program_state = &compiler_state.state;

BUSTER_GLOBAL_LOCAL void compiler_print_usage(void)
{
    string_print(S8("usage:\n"
                    "  ide cc <C compiler options and inputs>\n"
                    "  ide test [--verbose=0|1] [--ci=0|1]\n"
                    "  ide bench\n"
                    "  ide x86_64_completion_census [--output=<path>]\n"
                    "  ide --fuzz <libFuzzer options and corpus paths>\n"));
}

BUSTER_GLOBAL_LOCAL bool compiler_process_common_argument(u64 index)
{
    return buster_argument_process(index) == PROCESS_RESULT_SUCCESS;
}

ProcessResult process_arguments(void)
{
    SliceString8 arguments = program_state->input.arguments;
    compiler_state.command = COMPILER_COMMAND_HELP;
    if (arguments.length <= 1)
    {
        return PROCESS_RESULT_SUCCESS;
    }

    String8 command = arguments.pointer[1];
    if (string_equal(command, S8("help")) || string_equal(command, S8("--help")) || string_equal(command, S8("-h")))
    {
        if (arguments.length != 2)
        {
            compiler_print_usage();
            return PROCESS_RESULT_FAILED;
        }
        return PROCESS_RESULT_SUCCESS;
    }
    if (string_equal(command, S8("cc")))
    {
        compiler_state.command = COMPILER_COMMAND_CC;
        compiler_state.cc_arguments = (SliceString8){.pointer = arguments.pointer + 2, .length = arguments.length - 2};
        return PROCESS_RESULT_SUCCESS;
    }
    if (string_equal(command, S8("test")))
    {
        compiler_state.command = COMPILER_COMMAND_TEST;
        for (u64 index = 2; index < arguments.length; index += 1)
        {
            if (!compiler_process_common_argument(index))
            {
                string_print(S8("test: unsupported option: {S8}\n"), arguments.pointer[index]);
                return PROCESS_RESULT_FAILED;
            }
        }
        return PROCESS_RESULT_SUCCESS;
    }
    if (string_equal(command, S8("bench")))
    {
        compiler_state.command = COMPILER_COMMAND_BENCH;
        for (u64 index = 2; index < arguments.length; index += 1)
        {
            if (!compiler_process_common_argument(index))
            {
                string_print(S8("bench: unsupported option: {S8}\n"), arguments.pointer[index]);
                return PROCESS_RESULT_FAILED;
            }
        }
        return PROCESS_RESULT_SUCCESS;
    }
#if BUSTER_CPU_ARCH_X86_64
    if (string_equal(command, S8("x86_64_completion_census")))
    {
        compiler_state.command = COMPILER_COMMAND_X86_64_COMPLETION_CENSUS;
        for (u64 index = 2; index < arguments.length; index += 1)
        {
            String8 argument = arguments.pointer[index];
            if (string_starts_with_sequence(argument, S8("--output=")))
            {
                if (compiler_state.completion_census_output_path.length)
                {
                    string_print(S8("x86_64_completion_census: --output may only be specified once\n"));
                    return PROCESS_RESULT_FAILED;
                }
                compiler_state.completion_census_output_path = string_slice(argument, S8("--output=").length, argument.length);
                if (!compiler_state.completion_census_output_path.length)
                {
                    string_print(S8("x86_64_completion_census: expected a path after --output=\n"));
                    return PROCESS_RESULT_FAILED;
                }
            }
            else if (!compiler_process_common_argument(index))
            {
                string_print(S8("x86_64_completion_census: unsupported option: {S8}\n"), argument);
                return PROCESS_RESULT_FAILED;
            }
        }
        return PROCESS_RESULT_SUCCESS;
    }
#endif
    if (string_equal(command, S8("--fuzz")))
    {
#if BUSTER_FUZZ_AVAILABLE
        compiler_state.command = COMPILER_COMMAND_FUZZ;
        compiler_state.fuzz_arguments = (SliceString8){.pointer = arguments.pointer + 2, .length = arguments.length - 2};
        return PROCESS_RESULT_SUCCESS;
#else
        string_print(S8("fuzzing is not available in this build\n"));
        return PROCESS_RESULT_FAILED;
#endif
    }

    string_print(S8("unknown command: {S8}\n"), command);
    compiler_print_usage();
    return PROCESS_RESULT_FAILED;
}

BUSTER_GLOBAL_LOCAL ProcessResult compiler_run_tests(void)
{
#if BUSTER_INCLUDE_TESTS
    Arena* arena = arena_create((ArenaCreation){.reserved_size = BUSTER_MB(256)});
    if (!arena)
    {
        return PROCESS_RESULT_FAILED;
    }
    UnitTestArguments arguments = {arena, &default_show};

    ThreadContext* application_context = thread_context_selected();
    ThreadContext* test_context = thread_context_allocate();
    BUSTER_CHECK(application_context != 0 && test_context != 0);
    thread_context_select(test_context);

    u64 position = arena->position;
    BatchTestResult batch = library_tests(&arguments);
    arena->position = position;

    thread_context_release(test_context);
    (void)arena_pool_release_thread();
    thread_context_select(application_context);

    position = arena->position;
    ProcessResult result = batch_test_report(&arguments, batch) ? PROCESS_RESULT_SUCCESS : PROCESS_RESULT_FAILED;
    arena->position = position;
    arena_destroy(arena, 1);
    return result;
#else
    string_print(S8("tests are not included in this build\n"));
    return PROCESS_RESULT_FAILED;
#endif
}

BUSTER_GLOBAL_LOCAL void compiler_sort_u64(u64* values, u32 count)
{
    for (u32 index = 1; index < count; index += 1)
    {
        u64 value = values[index];
        u32 insertion = index;
        while (insertion && values[insertion - 1] > value)
        {
            values[insertion] = values[insertion - 1];
            insertion -= 1;
        }
        values[insertion] = value;
    }
}

BUSTER_GLOBAL_LOCAL ProcessResult compiler_run_benchmarks(void)
{
    String8 path = S8("tests/basic_c_operations.c");
    Arena* source_arena = arena_create((ArenaCreation){.reserved_size = BUSTER_MB(16)});
    Arena* work_arena = arena_create((ArenaCreation){.reserved_size = COMPILER_DRIVER_C_TRANSLATION_UNIT_RESERVED_SIZE});
    if (!source_arena || !work_arena)
    {
        if (source_arena) arena_destroy(source_arena, 1);
        if (work_arena) arena_destroy(work_arena, 1);
        return PROCESS_RESULT_FAILED;
    }
    ByteSlice bytes = file_read(source_arena, path, (FileReadOptions){0});
    if (!bytes.length)
    {
        string_print(S8("C frontend benchmark source load failed: {S8}\n"), path);
        arena_destroy(work_arena, 1);
        arena_destroy(source_arena, 1);
        return PROCESS_RESULT_FAILED;
    }
    String8 source = BYTE_SLICE_TO_STRING(8, bytes);
    c_prewarm();

    enum { BENCHMARK_ITERATIONS = BUSTER_OPTIMIZE ? 30 : 5 };
    u64 durations[BENCHMARK_ITERATIONS] = {0};
    u64 minimum = UINT64_MAX;
    for (u32 iteration = 0; iteration < BENCHMARK_ITERATIONS + 1; iteration += 1)
    {
        u64 position = work_arena->position;
        TimeDataType start = timestamp_take();
        CPreprocessResult preprocess = c_preprocess(work_arena, source,
                                                    (CPreprocessOptions){
                                                        .source_path = path,
                                                        .target = target_native,
                                                        .data_layout = target_data_layout(target_native),
                                                        .expansion_limit = BUSTER_MB(1),
                                                        .include_depth_limit = 64,
                                                        .disable_external_includes = true,
                                                    });
        CParserResult syntax = c_parse_ast(work_arena, preprocess);
        CIRLowerResult lower = c_analyze(work_arena, path, preprocess, syntax, target_native);
        TimeDataType end = timestamp_take();
        bool succeeded = preprocess.error_count == 0 && syntax.diagnostic_count == 0 && lower.program != 0;
        if (preprocess.recovery && preprocess.recovery->spelling_arena)
        {
            arena_destroy(preprocess.recovery->spelling_arena, 1);
        }
        if (preprocess.recovery && preprocess.recovery->token_arena)
        {
            arena_destroy(preprocess.recovery->token_arena, 1);
        }
        arena_set_position(work_arena, position);
        if (!succeeded)
        {
            string_print(S8("C frontend benchmark compilation failed\n"));
            arena_destroy(work_arena, 1);
            arena_destroy(source_arena, 1);
            return PROCESS_RESULT_FAILED;
        }
        if (iteration)
        {
            u64 duration = timestamp_ns_between(start, end);
            durations[iteration - 1] = duration;
            minimum = BUSTER_MIN(minimum, duration);
        }
    }
    compiler_sort_u64(durations, BENCHMARK_ITERATIONS);
    u64 median = durations[BENCHMARK_ITERATIONS / 2];
    u64 million_bytes_per_second = median ? (source.length * 1000) / median : 0;
    string_print(S8("BENCH_C_FRONTEND path={S8} iterations={u32} bytes={u64} min_ns={u64} median_ns={u64} throughput_mb_s={u64}\n"), path,
                 (u32)BENCHMARK_ITERATIONS, source.length, minimum, median, million_bytes_per_second);
    arena_destroy(work_arena, 1);
    arena_destroy(source_arena, 1);
    return PROCESS_RESULT_SUCCESS;
}

#if BUSTER_FUZZ_AVAILABLE
s32 buster_fuzz_test_input(const u8* pointer, size_t size)
{
    if (size <= BUSTER_KB(64) && (pointer || !size))
    {
        object_fuzz_test_input(pointer, size);

        Arena* arena = arena_create((ArenaCreation){.reserved_size = BUSTER_MB(128)});
        if (!arena)
        {
            return 0;
        }
        String8 source = {.pointer = pointer ? (char8*)pointer : S8("").pointer, .length = size};
        CPreprocessResult preprocess = c_preprocess(arena, source,
                                                    (CPreprocessOptions){
                                                        .source_path = S8("fuzz.c"),
                                                        .target = target_native,
                                                        .data_layout = target_data_layout(target_native),
                                                        .expansion_limit = BUSTER_KB(4),
                                                        .include_depth_limit = 8,
                                                        .disable_external_includes = true,
                                                    });
        CParserResult syntax = c_parse_ast(arena, preprocess);
        CIRLowerResult lower = c_analyze(arena, S8("fuzz.c"), preprocess, syntax, target_native);
        if (lower.program)
        {
            for (u32 module_index = 0; module_index < lower.program->module_count; module_index += 1)
            {
                IrValidationResult validation = ir_validate_canonical_module(lower.program, &lower.program->modules[module_index]);
                BUSTER_CHECK(validation.error == IR_VALIDATION_NONE);
            }
        }
        if (preprocess.recovery && preprocess.recovery->spelling_arena)
        {
            arena_destroy(preprocess.recovery->spelling_arena, 1);
        }
        if (preprocess.recovery && preprocess.recovery->token_arena)
        {
            arena_destroy(preprocess.recovery->token_arena, 1);
        }
        arena_destroy(arena, 1);
    }

    return 0;
}
#endif

#if BUSTER_CPU_ARCH_X86_64
BUSTER_GLOBAL_LOCAL void ide_completion_census_manifest_line(Arena* arena, String8* output, String8 key, u64 value)
{
    String8 line = string_format(arena, S8("{S8}={u64}\n"), key, value);
    *output = string_format(arena, S8("{S8}{S8}"), *output, line);
}

BUSTER_GLOBAL_LOCAL void ide_completion_census_manifest_hex_line(Arena* arena, String8* output, String8 key, u64 value)
{
    String8 line = string_format(arena, S8("{S8}=0x{u64:x,no_prefix}\n"), key, value);
    *output = string_format(arena, S8("{S8}{S8}"), *output, line);
}

BUSTER_GLOBAL_LOCAL ProcessResult run_completion_census(void)
{
    Arena* arena = arena_create((ArenaCreation){
        .reserved_size = BUSTER_MB(256),
        .initial_size = BUSTER_MB(16),
        .granularity = BUSTER_MB(2),
        .flags = {.no_pool = 1},
    });
    if (!arena) return PROCESS_RESULT_FAILED;
    u32 form_count = buster_x86_metadata_form_count();
    BusterX86CompletionCensusRecord* records = arena_allocate(arena, BusterX86CompletionCensusRecord, form_count);
    BusterX86CompletionCensusDiagnostic* diagnostics = arena_allocate(arena, BusterX86CompletionCensusDiagnostic, 128);
    BusterX86MetadataCoverageLedgerEntry* ledger = arena_allocate(arena, BusterX86MetadataCoverageLedgerEntry, form_count);
    BusterX86MetadataCoverageAuditResult audit = buster_x86_metadata_coverage_audit(ledger, form_count);
    Target census_target = {
        .cpu_arch = CPU_ARCH_X86_64,
        .cpu_model = CPU_MODEL_INTEL_DIAMOND_RAPIDS,
        .os = OPERATING_SYSTEM_LINUX,
        .cpu_features_explicit = true,
        .cpu_features = target_cpu_features_default(CPU_ARCH_X86_64, CPU_MODEL_INTEL_DIAMOND_RAPIDS),
    };
    BusterX86CompletionCensusResult census = buster_x86_completion_census_run((BusterX86CompletionCensusQuery){
        .arena = arena,
        .target = census_target,
        .records = records,
        .record_capacity = form_count,
        .diagnostics = diagnostics,
        .diagnostic_capacity = 128,
        .run_intel = true,
        .run_att = true,
    });
    u64 ledger_digest = buster_x86_metadata_coverage_digest(ledger, audit.entry_count, form_count);
    string_print(S8("X86_COMPLETION_CENSUS forms={u32} normalized={u32} emitted={u32} blocked={u32} intel_exact={u32} intel_unresolved={u32} att_exact={u32} att_unresolved={u32} structural={u32} ledger_digest=0x{u64:x,no_prefix}\n"),
                 census.scanned_form_count, census.normalized_form_count, census.metadata_emitted_count, census.metadata_blocked_count,
                 census.intel_exact_count, census.intel_unresolved_count, census.att_exact_count, census.att_unresolved_count,
                 census.structural_complete, ledger_digest);
    for (u32 reason_index = 0; reason_index < BUSTER_X86_COMPLETION_CENSUS_SOURCE_REASON_COUNT; reason_index += 1)
    {
        BusterX86CompletionCensusSourceReason reason = (BusterX86CompletionCensusSourceReason)reason_index;
        String8 reason_name = buster_x86_completion_census_source_reason_name(reason);
        if (census.intel_source_reason_counts[reason_index])
            string_print(S8("X86_COMPLETION_CENSUS_REASON dialect=intel reason={S8} count={u32}\n"), reason_name,
                         census.intel_source_reason_counts[reason_index]);
        if (census.att_source_reason_counts[reason_index])
            string_print(S8("X86_COMPLETION_CENSUS_REASON dialect=att reason={S8} count={u32}\n"), reason_name,
                         census.att_source_reason_counts[reason_index]);
    }
    ProcessResult result = census.structural_complete && audit.complete ? PROCESS_RESULT_SUCCESS : PROCESS_RESULT_FAILED;
    if (compiler_state.completion_census_output_path.length)
    {
        String8 manifest = S8("");
        ide_completion_census_manifest_line(arena, &manifest, S8("schema"), 2);
        ide_completion_census_manifest_line(arena, &manifest, S8("structural_complete"), census.structural_complete);
        ide_completion_census_manifest_line(arena, &manifest, S8("records_complete"), census.records_complete);
        ide_completion_census_manifest_line(arena, &manifest, S8("form_partition_complete"), census.form_partition_complete);
        ide_completion_census_manifest_line(arena, &manifest, S8("normalized_partition_complete"), census.normalized_partition_complete);
        ide_completion_census_manifest_line(arena, &manifest, S8("metadata_partition_complete"), census.metadata_partition_complete);
        ide_completion_census_manifest_line(arena, &manifest, S8("required_form_count"), census.required_form_count);
        ide_completion_census_manifest_line(arena, &manifest, S8("form_count"), census.scanned_form_count);
        ide_completion_census_manifest_line(arena, &manifest, S8("record_count"), census.record_count);
        ide_completion_census_manifest_line(arena, &manifest, S8("normalized_count"), census.normalized_form_count);
        ide_completion_census_manifest_line(arena, &manifest, S8("non_normalized_count"), census.non_normalized_form_count);
        ide_completion_census_manifest_line(arena, &manifest, S8("metadata_emitted_count"), census.metadata_emitted_count);
        ide_completion_census_manifest_line(arena, &manifest, S8("metadata_emit_failed_count"), census.metadata_emit_failed_count);
        ide_completion_census_manifest_line(arena, &manifest, S8("canonical_query_failed_count"), census.canonical_query_failed_count);
        ide_completion_census_manifest_line(arena, &manifest, S8("metadata_blocked_count"), census.metadata_blocked_count);
        ide_completion_census_manifest_line(arena, &manifest, S8("policy_excluded_count"), census.policy_excluded_count);
        ide_completion_census_manifest_line(arena, &manifest, S8("source_partition_expected_count"), census.source_partition_expected_count);
        ide_completion_census_manifest_line(arena, &manifest, S8("intel_source_partition_count"), census.intel_source_partition_count);
        ide_completion_census_manifest_line(arena, &manifest, S8("att_source_partition_count"), census.att_source_partition_count);
        ide_completion_census_manifest_line(arena, &manifest, S8("intel_attempted_count"), census.intel_attempted_count);
        ide_completion_census_manifest_line(arena, &manifest, S8("intel_exact_count"), census.intel_exact_count);
        ide_completion_census_manifest_line(arena, &manifest, S8("intel_normalized_relocation_count"), census.intel_normalized_relocation_count);
        ide_completion_census_manifest_line(arena, &manifest, S8("intel_alias_equivalent_count"), census.intel_alias_equivalent_count);
        ide_completion_census_manifest_line(arena, &manifest, S8("intel_policy_rejected_count"), census.intel_policy_rejected_count);
        ide_completion_census_manifest_line(arena, &manifest, S8("intel_different_encoding_count"), census.intel_different_encoding_count);
        ide_completion_census_manifest_line(arena, &manifest, S8("intel_byte_mismatch_count"), census.intel_byte_mismatch_count);
        ide_completion_census_manifest_line(arena, &manifest, S8("intel_relocation_mismatch_count"), census.intel_relocation_mismatch_count);
        ide_completion_census_manifest_line(arena, &manifest, S8("intel_unresolved_count"), census.intel_unresolved_count);
        ide_completion_census_manifest_line(arena, &manifest, S8("att_attempted_count"), census.att_attempted_count);
        ide_completion_census_manifest_line(arena, &manifest, S8("att_exact_count"), census.att_exact_count);
        ide_completion_census_manifest_line(arena, &manifest, S8("att_normalized_relocation_count"), census.att_normalized_relocation_count);
        ide_completion_census_manifest_line(arena, &manifest, S8("att_alias_equivalent_count"), census.att_alias_equivalent_count);
        ide_completion_census_manifest_line(arena, &manifest, S8("att_policy_rejected_count"), census.att_policy_rejected_count);
        ide_completion_census_manifest_line(arena, &manifest, S8("att_different_encoding_count"), census.att_different_encoding_count);
        ide_completion_census_manifest_line(arena, &manifest, S8("att_byte_mismatch_count"), census.att_byte_mismatch_count);
        ide_completion_census_manifest_line(arena, &manifest, S8("att_relocation_mismatch_count"), census.att_relocation_mismatch_count);
        ide_completion_census_manifest_line(arena, &manifest, S8("att_unresolved_count"), census.att_unresolved_count);
        ide_completion_census_manifest_hex_line(arena, &manifest, S8("structural_digest"), ledger_digest);
        ide_completion_census_manifest_line(arena, &manifest, S8("diagnostic_count"), census.diagnostic_count);
        ide_completion_census_manifest_line(arena, &manifest, S8("diagnostic_dropped_count"), census.diagnostic_dropped_count);
        for (u32 class_index = 0; class_index < BUSTER_X86_COMPLETION_CENSUS_CLASS_COUNT; class_index += 1)
        {
            String8 key = string_format(arena, S8("structural_class_{u32}_count"), class_index);
            ide_completion_census_manifest_line(arena, &manifest, key, census.class_counts[class_index]);
            key = string_format(arena, S8("intel_class_{u32}_count"), class_index);
            ide_completion_census_manifest_line(arena, &manifest, key, census.intel_class_counts[class_index]);
            key = string_format(arena, S8("att_class_{u32}_count"), class_index);
            ide_completion_census_manifest_line(arena, &manifest, key, census.att_class_counts[class_index]);
        }
        for (u32 reason_index = 0; reason_index < BUSTER_X86_COMPLETION_CENSUS_SOURCE_REASON_COUNT; reason_index += 1)
        {
            BusterX86CompletionCensusSourceReason reason = (BusterX86CompletionCensusSourceReason)reason_index;
            String8 reason_name = buster_x86_completion_census_source_reason_name(reason);
            String8 key = string_format(arena, S8("intel_source_reason_{S8}_count"), reason_name);
            ide_completion_census_manifest_line(arena, &manifest, key, census.intel_source_reason_counts[reason_index]);
            key = string_format(arena, S8("att_source_reason_{S8}_count"), reason_name);
            ide_completion_census_manifest_line(arena, &manifest, key, census.att_source_reason_counts[reason_index]);
        }
        for (u32 index = 0; index < census.diagnostic_count; index += 1)
        {
            BusterX86CompletionCensusDiagnostic diagnostic = diagnostics[index];
            String8 line = string_format(arena, S8("diagnostic_0x{u64:x,no_prefix}_dialect_{u8}=stable_hash:0x{u64:x,no_prefix},form_id:{u32},dialect:{u8},class:{u8},reason:{u8},kind:{u16},mismatch:{u32},direct:0x{u8:x,no_prefix},source:0x{u8:x,no_prefix}\n"),
                                         diagnostic.stable_hash, diagnostic.dialect, diagnostic.stable_hash, diagnostic.form_id, diagnostic.dialect, diagnostic.classification,
                                         diagnostic.reason, diagnostic.assembly_diagnostic_kind, diagnostic.mismatch_index, diagnostic.direct_byte, diagnostic.source_byte);
            manifest = string_format(arena, S8("{S8}{S8}"), manifest, line);
        }
        bool manifest_written = manifest.length < BUSTER_MB(2) && file_write(compiler_state.completion_census_output_path, BUSTER_SLICE_TO_BYTE_SLICE(manifest));
        if (!manifest_written) result = PROCESS_RESULT_FAILED;
        string_print(S8("X86_COMPLETION_CENSUS_MANIFEST path={S8} bytes={u64} written={u32}\n"), compiler_state.completion_census_output_path,
                     manifest.length, manifest_written);
    }
    arena_destroy(arena, 1);
    return result;
}
#endif

// The source measurement table (see CSourceMetrics), two aggregates side by
// side so include amplification reads off the page: `unique` covers the
// distinct files of the include closure, `lexed` every inclusion, and a
// header with neither #pragma once nor a recognized whole-file #ifndef
// guard is re-read at each one; the block after the table names those files.
#define REPORT_SOURCE_LABEL_WIDTH 26
#define REPORT_SOURCE_VALUE_WIDTH 14
#define REPORT_SOURCE_SHARE_WIDTH 9
#define REPORT_SOURCE_TABLE_WIDTH (REPORT_SOURCE_LABEL_WIDTH + 2 * (REPORT_SOURCE_VALUE_WIDTH + REPORT_SOURCE_SHARE_WIDTH) + 2)

BUSTER_GLOBAL_LOCAL String8 report_source_fill(Arena* arena, char8 character, u64 width)
{
    char8* cell = arena_allocate(arena, char8, width);
    for (u64 index = 0; index < width; index += 1)
    {
        cell[index] = character;
    }
    return (String8){.pointer = cell, .length = width};
}

// Right-aligned into a fixed column. The integer formatter's own width option
// counts digits before grouping inserts its separators, so columns of grouped
// numbers have to be padded on the finished text instead.
BUSTER_GLOBAL_LOCAL String8 report_source_cell(Arena* arena, String8 text, u64 width)
{
    String8 result;
    if (text.length >= width)
    {
        result = text;
    }
    else
    {
        u64 padding = width - text.length;
        char8* cell = arena_allocate(arena, char8, width);
        for (u64 index = 0; index < padding; index += 1)
        {
            cell[index] = ' ';
        }
        if (text.length)
        {
            memcpy(cell + padding, text.pointer, text.length);
        }
        result = (String8){.pointer = cell, .length = width};
    }

    return result;
}

BUSTER_GLOBAL_LOCAL String8 report_source_number(Arena* arena, u64 value, u64 width)
{
    return report_source_cell(arena, string_format(arena, S8("{u64:digit_group}"), value), width);
}

// One tenth of a percent, rounded, without reaching for floating point. A
// share with no parent is blank, and blank in the last column of a row is
// empty rather than padded so no line ends in whitespace.
BUSTER_GLOBAL_LOCAL String8 report_source_share(Arena* arena, u64 value, u64 total, bool trailing)
{
    String8 result;
    if (!total)
    {
        result = trailing ? (String8){0} : report_source_cell(arena, (String8){0}, REPORT_SOURCE_SHARE_WIDTH);
    }
    else
    {
        u64 tenths = (value * 1000 + total / 2) / total;
        result = report_source_cell(arena, string_format(arena, S8("{u64}.{u64} %"), tenths / 10, tenths % 10), REPORT_SOURCE_SHARE_WIDTH);
    }

    return result;
}

BUSTER_GLOBAL_LOCAL String8 report_source_label(Arena* arena, String8 label)
{
    u64 padding = label.length < REPORT_SOURCE_LABEL_WIDTH ? REPORT_SOURCE_LABEL_WIDTH - label.length : 0;
    return string_format(arena, S8("{S8}{S8}"), label, report_source_fill(arena, ' ', padding));
}

// One row of both aggregates. A zero total leaves that share column empty, so
// the same row prints an absolute count or a count with its share of a parent.
BUSTER_GLOBAL_LOCAL void report_source_row(Arena* arena, String8 label, u64 unique_value, u64 unique_total, u64 lexed_value, u64 lexed_total)
{
    string_print(S8("{S8}{S8}{S8}  {S8}{S8}\n"), report_source_label(arena, label), report_source_number(arena, unique_value, REPORT_SOURCE_VALUE_WIDTH),
                 report_source_share(arena, unique_value, unique_total, false), report_source_number(arena, lexed_value, REPORT_SOURCE_VALUE_WIDTH),
                 report_source_share(arena, lexed_value, lexed_total, true));
}

// The preprocessed side has no unique/lexed split, so its rows carry one
// value and its share of the lexed source it came from.
BUSTER_GLOBAL_LOCAL void report_preprocessed_row(Arena* arena, String8 label, u64 value, u64 total)
{
    string_print(S8("{S8}{S8}{S8}\n"), report_source_label(arena, label), report_source_number(arena, value, REPORT_SOURCE_VALUE_WIDTH),
                 report_source_share(arena, value, total, true));
}

// The attribution behind the unique/lexed ratio: every file lexed more than
// once, its lex count, and the bytes its re-reads added to the lexed column,
// largest first. For one unit the re-read total equals the two aggregates'
// scanned-byte difference exactly; the block is absent when nothing was
// re-lexed, so closing an amplification source removes its rows here.
BUSTER_GLOBAL_LOCAL void report_source_amplification(Arena* arena, CSourceFileMetrics const* files, u32 file_count)
{
    u32* order = file_count ? arena_allocate(arena, u32, file_count) : 0;
    u32 repeated = 0;
    u64 total = 0;
    for (u32 index = 0; index < file_count; index += 1)
    {
        if (files[index].lex_count > 1)
        {
            u64 re_read = (files[index].lex_count - 1) * files[index].translated_bytes;
            u32 position = repeated;
            while (position && (files[order[position - 1]].lex_count - 1) * files[order[position - 1]].translated_bytes < re_read)
            {
                order[position] = order[position - 1];
                position -= 1;
            }
            order[position] = index;
            repeated += 1;
            total += re_read;
        }
    }
    if (repeated)
    {
        string_print(S8("\nfiles lexed more than once\n"));
        string_print(S8("{S8}{S8}  path\n"), report_source_cell(arena, S8("lexes"), REPORT_SOURCE_VALUE_WIDTH),
                     report_source_cell(arena, S8("re-read bytes"), REPORT_SOURCE_VALUE_WIDTH));
        for (u32 index = 0; index < repeated; index += 1)
        {
            CSourceFileMetrics const* file = &files[order[index]];
            string_print(S8("{S8}{S8}  {S8}\n"), report_source_number(arena, file->lex_count, REPORT_SOURCE_VALUE_WIDTH),
                         report_source_number(arena, (file->lex_count - 1) * file->translated_bytes, REPORT_SOURCE_VALUE_WIDTH), file->path);
        }
        string_print(S8("{S8}{S8}  re-read in total\n"), report_source_cell(arena, (String8){0}, REPORT_SOURCE_VALUE_WIDTH),
                     report_source_number(arena, total, REPORT_SOURCE_VALUE_WIDTH));
    }
}

BUSTER_GLOBAL_LOCAL void report_source_metrics(Arena* arena, String8 unit, CSourceMetrics unique, CSourceMetrics lexed, CPreprocessedMetrics preprocessed)
{
    string_print(S8("SOURCE {S8}\n"), unit);
    string_print(S8("{S8}{S8}{S8}  {S8}\n"), report_source_label(arena, S8("")), report_source_cell(arena, S8("unique"), REPORT_SOURCE_VALUE_WIDTH),
                 report_source_share(arena, 0, 0, false), report_source_cell(arena, S8("lexed"), REPORT_SOURCE_VALUE_WIDTH));
    string_print(S8("{S8}\n"), report_source_fill(arena, '-', REPORT_SOURCE_TABLE_WIDTH));
    report_source_row(arena, S8("files"), unique.files, 0, lexed.files, 0);
    report_source_row(arena, S8("bytes on disk"), unique.bytes, 0, lexed.bytes, 0);
    report_source_row(arena, S8("physical lines"), unique.lines, 0, lexed.lines, 0);
    string_print(S8("\n"));
    report_source_row(arena, S8("bytes scanned"), unique.translated_bytes, 0, lexed.translated_bytes, 0);
    report_source_row(arena, S8("  code"), c_source_metrics_code_bytes(unique), unique.translated_bytes, c_source_metrics_code_bytes(lexed),
                      lexed.translated_bytes);
    report_source_row(arena, S8("  comment"), unique.comment_bytes, unique.translated_bytes, lexed.comment_bytes, lexed.translated_bytes);
    report_source_row(arena, S8("  whitespace"), unique.blank_bytes, unique.translated_bytes, lexed.blank_bytes, lexed.translated_bytes);
    report_source_row(arena, S8("  literals, within code"), unique.literal_bytes, unique.translated_bytes, lexed.literal_bytes, lexed.translated_bytes);
    string_print(S8("\n"));
    report_source_row(arena, S8("lines scanned"), unique.translated_lines, 0, lexed.translated_lines, 0);
    report_source_row(arena, S8("  code, the sLOC"), unique.code_lines, unique.translated_lines, lexed.code_lines, lexed.translated_lines);
    report_source_row(arena, S8("  comment"), unique.comment_lines, unique.translated_lines, lexed.comment_lines, lexed.translated_lines);
    report_source_row(arena, S8("  blank"), unique.blank_lines, unique.translated_lines, lexed.blank_lines, lexed.translated_lines);
    report_source_row(arena, S8("  both, counted twice"), unique.mixed_lines, unique.translated_lines, lexed.mixed_lines, lexed.translated_lines);
    string_print(S8("\n"));
    report_source_row(arena, S8("comments"), unique.comments, 0, lexed.comments, 0);
    report_source_row(arena, S8("tokens"), unique.tokens, 0, lexed.tokens, 0);
    report_source_row(arena, S8("spliced-away lines"), unique.spliced_lines, unique.lines, lexed.spliced_lines, lexed.lines);
    string_print(S8("\n"));
    string_print(S8("{S8}{S8}{S8}\n"), report_source_label(arena, S8("preprocessed output")), report_source_cell(arena, (String8){0}, REPORT_SOURCE_VALUE_WIDTH),
                 report_source_cell(arena, S8("of lexed"), REPORT_SOURCE_SHARE_WIDTH));
    report_preprocessed_row(arena, S8("  tokens to the parser"), preprocessed.tokens, lexed.tokens);
    report_preprocessed_row(arena, S8("  their spelling bytes"), preprocessed.bytes, c_source_metrics_code_bytes(lexed));
    report_preprocessed_row(arena, S8("  bytes retained"), preprocessed.spelling_bytes, lexed.translated_bytes);
    report_preprocessed_row(arena, S8("  macro expansions"), preprocessed.expansions, 0);
    report_preprocessed_row(arena, S8("  #define directives"), preprocessed.definitions, 0);
}

// The same measurement as the table above, in the form another program reads.
// `-v` is for a human, `-fsource-metrics=<path>` is for a build driver: it
// divides its own instruction count for the compile by these to get the
// throughput series that STEP_INSTRUCTIONS alone cannot express (see
// `self_host_compare_action` in build.c).
//
// A whole file rather than another stdout line, because capturing the
// compiler's stdout would buffer the diagnostics that currently stream as the
// compile runs, and losing those on a failing build costs more than this file.
//
// Keys are `<group>.<CSourceMetrics field name>`, so the format is the struct
// and readers can key off exactly the fields they need. Only ever add keys:
// build.c ignores those it does not know, and an older build.c must keep
// reading a newer compiler's file.
#define SOURCE_METRICS_FILE_VERSION 1

// string_format appends one byte at a time at the arena's current position, so
// consecutive calls land contiguously and a single String8 can be grown to
// cover them all; string_format_z relies on the same property for its
// terminator.
BUSTER_GLOBAL_LOCAL void source_metrics_append_line(String8* text, String8 line)
{
    if (!text->pointer)
    {
        text->pointer = line.pointer;
    }
    text->length += line.length;
}

BUSTER_GLOBAL_LOCAL void source_metrics_append_field(Arena* arena, String8* text, String8 group, String8 key, u64 value)
{
    source_metrics_append_line(text, string_format(arena, S8("{S8}.{S8}={u64}\n"), group, key, value));
}

BUSTER_GLOBAL_LOCAL void source_metrics_append_group(Arena* arena, String8* text, String8 group, CSourceMetrics metrics)
{
    source_metrics_append_field(arena, text, group, S8("files"), metrics.files);
    source_metrics_append_field(arena, text, group, S8("bytes"), metrics.bytes);
    source_metrics_append_field(arena, text, group, S8("translated_bytes"), metrics.translated_bytes);
    source_metrics_append_field(arena, text, group, S8("lines"), metrics.lines);
    source_metrics_append_field(arena, text, group, S8("translated_lines"), metrics.translated_lines);
    source_metrics_append_field(arena, text, group, S8("spliced_lines"), metrics.spliced_lines);
    source_metrics_append_field(arena, text, group, S8("code_lines"), metrics.code_lines);
    source_metrics_append_field(arena, text, group, S8("comment_lines"), metrics.comment_lines);
    source_metrics_append_field(arena, text, group, S8("mixed_lines"), metrics.mixed_lines);
    source_metrics_append_field(arena, text, group, S8("blank_lines"), metrics.blank_lines);
    // Derived from the byte partition, and written out so a reader does not
    // have to know the partition to use the one byte count that is code.
    source_metrics_append_field(arena, text, group, S8("code_bytes"), c_source_metrics_code_bytes(metrics));
    source_metrics_append_field(arena, text, group, S8("comment_bytes"), metrics.comment_bytes);
    source_metrics_append_field(arena, text, group, S8("blank_bytes"), metrics.blank_bytes);
    source_metrics_append_field(arena, text, group, S8("literal_bytes"), metrics.literal_bytes);
    source_metrics_append_field(arena, text, group, S8("comments"), metrics.comments);
    source_metrics_append_field(arena, text, group, S8("tokens"), metrics.tokens);
}

BUSTER_GLOBAL_LOCAL bool write_source_metrics(Arena* arena, String8 path, String8 unit, CSourceMetrics unique, CSourceMetrics lexed,
                                              CPreprocessedMetrics preprocessed)
{
    String8 text = {0};
    source_metrics_append_line(&text, string_format(arena, S8("version={u32}\n"), (u32)SOURCE_METRICS_FILE_VERSION));
    source_metrics_append_line(&text, string_format(arena, S8("unit={S8}\n"), unit));
    source_metrics_append_group(arena, &text, S8("unique"), unique);
    source_metrics_append_group(arena, &text, S8("lexed"), lexed);
    source_metrics_append_field(arena, &text, S8("preprocessed"), S8("tokens"), preprocessed.tokens);
    source_metrics_append_field(arena, &text, S8("preprocessed"), S8("bytes"), preprocessed.bytes);
    source_metrics_append_field(arena, &text, S8("preprocessed"), S8("spelling_bytes"), preprocessed.spelling_bytes);
    source_metrics_append_field(arena, &text, S8("preprocessed"), S8("expansions"), preprocessed.expansions);
    source_metrics_append_field(arena, &text, S8("preprocessed"), S8("definitions"), preprocessed.definitions);
    return file_write(path, BUSTER_SLICE_TO_BYTE_SLICE(text));
}

BUSTER_GLOBAL_LOCAL ProcessResult run_c_compiler(void)
{
    Arena* arena = arena_create((ArenaCreation){
        .reserved_size = COMPILER_DRIVER_C_TRANSLATION_UNIT_RESERVED_SIZE,
    });
    if (!arena)
    {
        return PROCESS_RESULT_FAILED;
    }
    CompilerDriverInvocation invocation = compiler_driver_parse_arguments(arena, compiler_state.cc_arguments);
    CompilerDriverResult compile = compiler_driver_execute_invocation(arena, invocation);
    ProcessResult result = PROCESS_RESULT_SUCCESS;
    if (compile.warning.length)
    {
        string_print(S8("{S8}"), compile.warning);
    }
    if (compile.error != COMPILER_DRIVER_ERROR_NONE)
    {
        string_print(S8("cc: error: {S8}\n"), compile.diagnostic);
        result = PROCESS_RESULT_FAILED;
    }
    else if (!invocation.output_path.length)
    {
        string_print(S8("{S8}"), compile.output);
    }
    if (compile.error == COMPILER_DRIVER_ERROR_NONE && invocation.verbose)
    {
        if (invocation.has_gpu_target)
        {
            String8 target = gpu_target_to_string(arena, invocation.gpu_target);
            GpuOutputFormat format = compile.has_gpu ? compile.gpu.format : GPU_OUTPUT_NONE;
            String8 path = compile.has_gpu ? compile.gpu.path : (String8){0};
            u64 byte_count = compile.has_gpu ? compile.gpu.bytes.length : 0;
            string_print(S8("GPU target={S8} format={S8} path={S8} bytes={u64}\n"), target, gpu_output_format_to_string(format), path,
                         byte_count);
        }
        else
        {
            string_print(S8("TARGET cpu={S8} features={S8}\n"), cpu_model_to_string_os(invocation.target.cpu_model),
                         target_cpu_features_to_string(arena, invocation.target));
        }
    }
    if (compile.source_lexed.files && (invocation.verbose || invocation.source_metrics_path.length))
    {
        String8 unit = invocation.input_count == 1 ? invocation.input_paths[0] : string_format(arena, S8("{u32} inputs"), invocation.input_count);
        if (invocation.verbose)
        {
            report_source_metrics(arena, unit, compile.source_unique, compile.source_lexed, compile.preprocessed);
            report_source_amplification(arena, compile.lexed_files, compile.lexed_file_count);
        }
        // A metrics file that was asked for and could not be written is an
        // error like an unwritable -o: the caller asked for a measurement and
        // would otherwise read a stale file, or none, without being told.
        if (invocation.source_metrics_path.length &&
            !write_source_metrics(arena, invocation.source_metrics_path, unit, compile.source_unique, compile.source_lexed, compile.preprocessed))
        {
            string_print(S8("cc: error: could not write {S8}\n"), invocation.source_metrics_path);
            result = PROCESS_RESULT_FAILED;
        }
    }
    if (compile.error == COMPILER_DRIVER_ERROR_NONE && invocation.verbose && compile.codegen_statistics.function_count)
    {
        string_print(S8("CODEGEN cpu={S8} vector_bits={u32} functions={u32} instructions={u64} values={u64} stack_value_bytes={u64} stack_frame_bytes={u64} "
                        "max_stack_frame_bytes={u32} code_bytes={u64} forwarded_wide_vector_loads={u64} native_vector_operations={u64} "
                        "split_vector_operations={u64} vzeroupper={u64} simd_operations={u64} allocator={S8} fallback_functions={u32}\n"),
                     cpu_model_to_string_os(invocation.target.cpu_model), target_vector_register_size(invocation.target) * 8,
                     compile.codegen_statistics.function_count, compile.codegen_statistics.instruction_count, compile.codegen_statistics.value_count,
                     compile.codegen_statistics.stack_value_bytes, compile.codegen_statistics.stack_frame_bytes,
                     compile.codegen_statistics.maximum_stack_frame_bytes, compile.codegen_statistics.code_bytes,
                     compile.codegen_statistics.forwarded_wide_vector_load_count, compile.codegen_statistics.native_vector_operation_count,
                     compile.codegen_statistics.split_vector_operation_count, compile.codegen_statistics.vzeroupper_count,
                     compile.codegen_statistics.simd_operation_count,
                     codegen_register_allocator_mode_string((CodegenRegisterAllocatorMode)invocation.register_allocator),
                     compile.codegen_statistics.fallback_function_count);
        string_print(S8("CODEGEN_ENCODER exact_attempts={u64} exact_successes={u64} exact_failures={u64}\n"),
                     compile.codegen_statistics.exact_attempts, compile.codegen_statistics.exact_successes, compile.codegen_statistics.exact_failures);
        for (u32 reason = 0; reason <= IR_OPCODE_COUNT; reason += 1)
        {
            if (compile.codegen_statistics.fallback_opcode_counts[reason])
            {
                string_print(S8("CODEGEN_FALLBACK opcode={u32} count={u32}\n"), reason, compile.codegen_statistics.fallback_opcode_counts[reason]);
            }
        }
        if (compile.codegen_statistics.allocator_reload_count || compile.codegen_statistics.allocator_spill_count ||
            compile.codegen_statistics.allocator_copy_count)
        {
            string_print(S8("CODEGEN_ALLOCATOR reloads={u64} spills={u64} boundary_spills={u64} boundary_reloads={u64} boundary_copies={u64} copies={u64} "
                            "rematerializations={u64} pins={u64} splits={u64} scheduled={u64} schedule_kept={u64}\n"),
                         compile.codegen_statistics.allocator_reload_count, compile.codegen_statistics.allocator_spill_count,
                         compile.codegen_statistics.allocator_boundary_spill_count, compile.codegen_statistics.allocator_boundary_reload_count,
                         compile.codegen_statistics.allocator_boundary_copy_count, compile.codegen_statistics.allocator_copy_count,
                         compile.codegen_statistics.allocator_rematerialize_count, compile.codegen_statistics.allocator_pinned_register_count,
                         compile.codegen_statistics.allocator_split_register_count, compile.codegen_statistics.allocator_scheduled_function_count,
                         compile.codegen_statistics.allocator_schedule_kept_count);
        }
        if (compile.codegen_statistics.fallback_verify_count || compile.codegen_statistics.fallback_placement_count ||
            compile.codegen_statistics.fallback_encode_count)
        {
            string_print(S8("CODEGEN_FALLBACK_STAGES verify={u32} placement={u32} encode={u32}\n"), compile.codegen_statistics.fallback_verify_count,
                         compile.codegen_statistics.fallback_placement_count, compile.codegen_statistics.fallback_encode_count);
        }
    }
    arena_destroy(arena, 1);
    return result;
}


ProcessResult entry_point(void)
{
    switch (compiler_state.command)
    {
        case COMPILER_COMMAND_HELP:
            compiler_print_usage();
            return PROCESS_RESULT_SUCCESS;
        case COMPILER_COMMAND_TEST:
            return compiler_run_tests();
        case COMPILER_COMMAND_BENCH:
            return compiler_run_benchmarks();
        case COMPILER_COMMAND_CC:
            return run_c_compiler();
        case COMPILER_COMMAND_FUZZ:
#if BUSTER_FUZZ_AVAILABLE
            return buster_fuzz_run(compiler_state.fuzz_arguments);
#else
            return PROCESS_RESULT_FAILED;
#endif
        case COMPILER_COMMAND_X86_64_COMPLETION_CENSUS:
#if BUSTER_CPU_ARCH_X86_64
            return run_completion_census();
#else
            return PROCESS_RESULT_FAILED;
#endif
    }
    return PROCESS_RESULT_FAILED;
}
