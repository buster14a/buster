#pragma once

// Private implementation of `ide bench-select <source.c>`. It measures the
// production validated selector, not the diagnostic rule matcher. The input
// IR is retained; each sample rebuilds module plans and rewinds function
// scratch after every attempt, just as module code generation does. This is
// a warm-IR replay measurement, not a substitute for time to an object file.
// Entry: compiler_run_selection_benchmark. Sampling: compiler_selection_sample.
// No counters or timers are installed in ordinary compiler execution.

typedef struct CompilerSelectionSample CompilerSelectionSample;
struct CompilerSelectionSample
{
    u64 selection_ns;
    u64 prepare_ns;
    u64 instructions;
    u64 typed_instructions;
    u64 arena_bytes;
    u64 peak_arena_bytes;
    u64 module_bytes;
    u32 functions;
    u32 fallbacks;
    u32 verification_failures;
    IrOpcode first_failed_opcode;
};

BUSTER_GLOBAL_LOCAL CompilerSelectionSample compiler_selection_sample(Arena* arena, Arena* module_arena, IrProgram* program, bool verify)
{
    CompilerSelectionSample sample = {.first_failed_opcode = IR_OPCODE_COUNT};
    u64 module_position = module_arena->position;
    u64 position = arena->position;
    for (u32 module_index = 0; module_index < program->module_count; module_index += 1)
    {
        IrModule* ir_module = program->modules + module_index;
        TimeDataType start = timestamp_take();
        MachineSelectionModule* module = machine_select_module_prepare(module_arena, program, target_native);
        TimeDataType prepared = timestamp_take();
        sample.prepare_ns += timestamp_ns_between(start, prepared);
        for (u32 function_index = 0; function_index < ir_module->function_count; function_index += 1)
        {
            IrFunction* function = ir_module->functions + function_index;
            if (function->state == IR_FUNCTION_LOWERED)
            {
                MachineSelectResult selected = machine_select_validated_canonical_function(arena, program, function, target_native, false, module);
                sample.functions += 1;
                if (selected.supported)
                {
                    sample.instructions += selected.function.instruction_count;
                    sample.typed_instructions += selected.selected_typed_instructions;
                    if (verify)
                    {
                        MachineVerifyResult verification = machine_verify_function(&selected.function);
                        if (verification.error != MACHINE_VERIFY_NONE)
                        {
                            sample.verification_failures += 1;
                            string_print(S8("bench-select: MIR verification failed function={S8} error={u32} block={u32} instruction={u32} operand={u32}\n"),
                                         function->name, (u32)verification.error, verification.block, verification.instruction, verification.operand);
                        }
                    }
                }
                else
                {
                    if (!sample.fallbacks)
                    {
                        sample.first_failed_opcode = selected.failed_opcode;
                    }
                    sample.fallbacks += 1;
                }
                u64 bytes = arena->position - position;
                sample.arena_bytes += bytes;
                sample.peak_arena_bytes = BUSTER_MAX(sample.peak_arena_bytes, bytes);
                arena_set_position(arena, position);
            }
        }
        TimeDataType end = timestamp_take();
        sample.selection_ns += timestamp_ns_between(prepared, end);
        sample.module_bytes += module_arena->position - module_position;
        arena_set_position(module_arena, module_position);
    }
    return sample;
}

BUSTER_GLOBAL_LOCAL bool compiler_selection_same_work(CompilerSelectionSample left, CompilerSelectionSample right)
{
    return left.functions == right.functions && left.fallbacks == right.fallbacks && left.instructions == right.instructions &&
           left.typed_instructions == right.typed_instructions && left.arena_bytes == right.arena_bytes &&
           left.peak_arena_bytes == right.peak_arena_bytes && left.module_bytes == right.module_bytes;
}

BUSTER_GLOBAL_LOCAL ProcessResult compiler_run_selection_benchmark(String8 path)
{
    enum { SELECTION_BENCHMARK_ITERATIONS = BUSTER_OPTIMIZE ? 30 : 5 };
    ProcessResult result = PROCESS_RESULT_FAILED;
    Arena* source_arena = arena_create((ArenaCreation){.reserved_size = BUSTER_MB(64)});
    Arena* work_arena = arena_create((ArenaCreation){.reserved_size = COMPILER_DRIVER_C_TRANSLATION_UNIT_RESERVED_SIZE});
    Arena* module_arena = arena_create((ArenaCreation){.reserved_size = BUSTER_GB(1)});
    Arena* function_arena = arena_create((ArenaCreation){.reserved_size = BUSTER_GB(4)});
    CPreprocessResult preprocess = {0};
    if (source_arena && work_arena && module_arena && function_arena)
    {
        ByteSlice bytes = file_read(source_arena, path, (FileReadOptions){0});
        if (bytes.length && (target_native.cpu_arch == CPU_ARCH_X86_64 || target_native.cpu_arch == CPU_ARCH_AARCH64))
        {
            compiler_prewarm();
            preprocess = c_preprocess(work_arena, BYTE_SLICE_TO_STRING(8, bytes),
                                      (CPreprocessOptions){.source_path = path, .target = target_native,
                                                           .data_layout = target_data_layout(target_native), .expansion_limit = BUSTER_MB(64),
                                                           .include_depth_limit = 64, .disable_external_includes = true});
            IrProgram* program = 0;
            if (!preprocess.error_count)
            {
                CParserResult syntax = c_parse_ast(work_arena, preprocess);
                if (!syntax.diagnostic_count)
                {
                    CIRLowerResult lower = c_analyze(work_arena, path, preprocess, syntax, target_native);
                    if (!lower.diagnostic_count)
                    {
                        program = lower.program;
                    }
                }
            }
            bool valid = program != 0;
            if (valid)
            {
                for (u32 index = 0; index < program->module_count; index += 1)
                {
                    if (ir_prepare_canonical_module(program, program->modules + index, false).error != IR_VALIDATION_NONE)
                    {
                        valid = false;
                    }
                }
            }
            if (valid)
            {
                // Freeze ABI records in the retained IR arena, as module
                // code generation does before any selector attempt.
                ir_prepare_program_abi(program, ir_abi_convention_for_target(target_native));
                // Verification is outside the reported sample population. It
                // also warms the IR and target metadata before the replay.
                CompilerSelectionSample reference = compiler_selection_sample(function_arena, module_arena, program, true);
                valid = reference.functions && reference.instructions && !reference.fallbacks && !reference.verification_failures;
                if (valid)
                {
                    u64 durations[SELECTION_BENCHMARK_ITERATIONS];
                    u64 preparations[SELECTION_BENCHMARK_ITERATIONS];
                    u64 totals[SELECTION_BENCHMARK_ITERATIONS];
                    for (u32 iteration = 0; iteration < SELECTION_BENCHMARK_ITERATIONS; iteration += 1)
                    {
                        CompilerSelectionSample sample = compiler_selection_sample(function_arena, module_arena, program, false);
                        valid = valid && compiler_selection_same_work(reference, sample);
                        durations[iteration] = sample.selection_ns;
                        preparations[iteration] = sample.prepare_ns;
                        totals[iteration] = sample.selection_ns + sample.prepare_ns;
                    }
                    if (valid)
                    {
                        compiler_sort_u64(durations, SELECTION_BENCHMARK_ITERATIONS);
                        compiler_sort_u64(preparations, SELECTION_BENCHMARK_ITERATIONS);
                        compiler_sort_u64(totals, SELECTION_BENCHMARK_ITERATIONS);
                        string_print(S8("BENCH_SELECT version=1 iterations={u32} functions={u32} fallback_functions={u32} ir_instructions={u64} "
                                        "mir_instructions={u64} min_ns={u64} median_ns={u64} prepare_median_ns={u64} total_median_ns={u64} "
                                        "arena_bytes={u64} peak_arena_bytes={u64} module_bytes={u64}\n"),
                                     (u32)SELECTION_BENCHMARK_ITERATIONS, reference.functions, reference.fallbacks, reference.typed_instructions,
                                     reference.instructions, durations[0], durations[SELECTION_BENCHMARK_ITERATIONS / 2],
                                     preparations[SELECTION_BENCHMARK_ITERATIONS / 2], totals[SELECTION_BENCHMARK_ITERATIONS / 2],
                                     reference.arena_bytes, reference.peak_arena_bytes, reference.module_bytes);
                        string_print(S8("BENCH_SELECT_TARGET arch={S8} os={S8} model={S8} features={S8}\n"),
                                     cpu_arch_to_string_os(target_native.cpu_arch), operating_system_to_string_os(target_native.os),
                                     cpu_model_to_string_os(target_native.cpu_model),
                                     target_cpu_features_to_string(source_arena, target_native));
                        result = PROCESS_RESULT_SUCCESS;
                    }
                    else
                    {
                        string_print(S8("bench-select: replay changed its work or allocation counts\n"));
                    }
                }
                else
                {
                    // Do not turn declining selection coverage into a faster
                    // ns/MIR result. There is no timing row on this path.
                    string_print(S8("bench-select: invalid workload functions={u32} mir_instructions={u64} fallback_functions={u32} "
                                    "verification_failures={u32} first_failed_opcode={u32}\n"),
                                 reference.functions, reference.instructions, reference.fallbacks, reference.verification_failures,
                                 (u32)reference.first_failed_opcode);
                }
            }
            else
            {
                string_print(S8("bench-select: source or canonical IR validation failed\n"));
            }
        }
        else
        {
            string_print(S8("bench-select: expected a nonempty source and a native x86-64 or AArch64 target\n"));
        }
    }
    if (preprocess.recovery)
    {
        if (preprocess.recovery->spelling_arena) arena_destroy(preprocess.recovery->spelling_arena, 1);
        if (preprocess.recovery->token_arena) arena_destroy(preprocess.recovery->token_arena, 1);
        if (preprocess.recovery->token_shape_arena) arena_destroy(preprocess.recovery->token_shape_arena, 1);
    }
    if (function_arena) arena_destroy(function_arena, 1);
    if (module_arena) arena_destroy(module_arena, 1);
    if (work_arena) arena_destroy(work_arena, 1);
    if (source_arena) arena_destroy(source_arena, 1);
    return result;
}
