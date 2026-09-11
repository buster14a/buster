#pragma once

// Renumber blocks without changing the graph, instruction chains, or values.
// These fixtures have no address-taken labels, so every block reference is an
// entry, instruction target, predecessor, or block-parameter incoming edge.
BUSTER_GLOBAL_LOCAL void codegen_test_rotate_blocks(Arena* arena, IrFunction* function, u32 rotation)
{
    ir_function_invalidate_cfg(function);
    u32 count = function->block_count;
    IrBlock* original = arena_allocate(arena, IrBlock, count);
    memcpy(original, function->blocks, sizeof(*original) * count);
    for (u32 index = 0; index < count; index += 1)
    {
        u32 destination = (index + rotation) % count;
        function->blocks[destination] = original[index];
        IrBlock* block = function->blocks + destination;
        block->id.value = destination;
        for (IrPredecessor* predecessor = block->first_predecessor; predecessor; predecessor = predecessor->next)
        {
            predecessor->block.value = (predecessor->block.value + rotation) % count;
        }
        for (IrBlockParameter* parameter = block->first_parameter; parameter; parameter = parameter->next)
        {
            for (IrIncoming* incoming = parameter->first_incoming; incoming; incoming = incoming->next)
            {
                incoming->predecessor.value = (incoming->predecessor.value + rotation) % count;
            }
        }
    }
    for (u32 index = 0; index < function->instruction_count; index += 1)
    {
        IrInstruction* instruction = function->instructions + index;
        for (u32 target = 0; target < instruction->target_count; target += 1)
        {
            instruction->targets[target].value = (instruction->targets[target].value + rotation) % count;
        }
    }
    function->entry.value = (function->entry.value + rotation) % count;
}

BUSTER_GLOBAL_LOCAL u64 codegen_test_entry_expected(u32 fixture, u64 first, u64 second)
{
    u64 result = 42;
    if (fixture == 1)
    {
        result = first > second ? first + 11 : second + 29;
    }
    else if (fixture == 2)
    {
        result = second;
        for (u64 index = 0; index < (first & 7); index += 1)
        {
            result = (result + index) ^ 13;
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL UnitTestResult codegen_test_canonical_entry(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    String8 sources[] = {
        S8("unsigned long long probe(void) { goto target; dead: return 7; target: return 42; }"),
        S8("unsigned long long probe(unsigned long long a, unsigned long long b) { unsigned long long v; if(a>b)v=a+11;else v=b+29;return v; }"),
        S8("unsigned long long probe(unsigned long long a, unsigned long long b) { unsigned long long i,v=b;for(i=0;i<(a&7);i++)v=(v+i)^13;return v; }"),
    };
    Target targets[] = {
        {.cpu_arch = CPU_ARCH_X86_64, .cpu_model = CPU_MODEL_BASELINE, .os = OPERATING_SYSTEM_LINUX},
        {.cpu_arch = CPU_ARCH_X86_64, .cpu_model = CPU_MODEL_BASELINE, .os = OPERATING_SYSTEM_WINDOWS},
        {.cpu_arch = CPU_ARCH_X86_64, .cpu_model = CPU_MODEL_BASELINE, .os = OPERATING_SYSTEM_MACOS},
        {.cpu_arch = CPU_ARCH_AARCH64, .cpu_model = CPU_MODEL_BASELINE, .os = OPERATING_SYSTEM_LINUX},
        {.cpu_arch = CPU_ARCH_AARCH64, .cpu_model = CPU_MODEL_BASELINE, .os = OPERATING_SYSTEM_WINDOWS},
        {.cpu_arch = CPU_ARCH_AARCH64, .cpu_model = CPU_MODEL_BASELINE, .os = OPERATING_SYSTEM_MACOS},
        {.cpu_arch = CPU_ARCH_BPFEL, .cpu_model = CPU_MODEL_BASELINE, .os = OPERATING_SYSTEM_LINUX},
    };
    u64 inputs[][2] = {{0, 0}, {1, 2}, {7, 3}, {UINT64_MAX, UINT64_MAX - 1}};
    for (u32 fixture = 0; fixture < BUSTER_ARRAY_LENGTH(sources); fixture += 1)
    {
        for (u32 target_index = 0; target_index < BUSTER_ARRAY_LENGTH(targets); target_index += 1)
        {
            Target target = targets[target_index];
            u8 original_code[4096] = {0};
            u64 original_code_length = 0;
            u32 original_location_count = 0;
            for (u32 permutation = 0; permutation < 3; permutation += 1)
            {
                TemporalArena temporary = arena_begin_temporal(arguments->arena);
                CPreprocessResult tokens = c_preprocess(arguments->arena, sources[fixture],
                    (CPreprocessOptions){.target = target, .data_layout = target_data_layout(target)});
                CParseResult parse = c_parse(arguments->arena, tokens);
                CIRLowerResult lowered = c_lower_to_ir(arguments->arena, S8("entry-layout.c"), tokens, parse, target);
                BUSTER_TEST(arguments, !tokens.error_count && !parse.diagnostic_count && lowered.program && !lowered.diagnostic_count);
                if (lowered.program && !lowered.diagnostic_count)
                {
                    IrProgram* program = lowered.program;
                    IrModule* module = program->modules;
                    IrFunction* function = codegen_test_c_function_find(module, S8("probe"));
                    BUSTER_TEST(arguments, function && function->block_count > 1);
                    IrValidationResult validation = ir_prepare_canonical_module(program, module, false);
                    BUSTER_TEST(arguments, validation.error == IR_VALIDATION_NONE);
                    if (function && function->block_count > 1 && validation.error == IR_VALIDATION_NONE)
                    {
                        u32 rotation = permutation == 2 ? function->block_count - 1 : permutation;
                        codegen_test_rotate_blocks(arguments->arena, function, rotation);
                        BUSTER_TEST(arguments, function->entry.value == rotation);
                        BUSTER_TEST(arguments, ir_validate_canonical_module(program, module).error == IR_VALIDATION_NONE);
                        if (target.cpu_arch == CPU_ARCH_BPFEL)
                        {
                            EbpfArtifact artifact = ebpf_emit_program(arguments->arena, program);
                            BUSTER_TEST(arguments, artifact.success);
                            if (artifact.success)
                            {
                                for (u32 input = 0; input < BUSTER_ARRAY_LENGTH(inputs); input += 1)
                                {
                                    u64 actual = 0;
                                    u64 expected = codegen_test_entry_expected(fixture, inputs[input][0], inputs[input][1]);
                                    bool ran = codegen_test_ebpf_execute(artifact.bytes, inputs[input][0], inputs[input][1], &actual);
                                    if (!ran || actual != expected)
                                    {
                                        arguments->show(arguments, S8("eBPF entry fixture {u32}, entry {u32}, input {u32}: ran={u32}, actual={u64}, expected={u64}\n"),
                                            fixture, rotation, input, (u32)ran, actual, expected);
                                    }
                                    BUSTER_TEST(arguments, ran && actual == expected);
                                }
                            }
                        }
                        else
                        {
                            for (u32 mode = 0; mode < CODEGEN_REGISTER_ALLOCATOR_MODE_COUNT; mode += 1)
                            {
                                CodegenModule code = codegen_generate_canonical_module(arguments->arena, program, module, target,
                                    (CodegenModuleOptions){.register_allocator = (u8)mode, .debug_info = true});
                                if (code.error)
                                {
                                    arguments->show(arguments, S8("native entry fixture {u32}, target {u32}, entry {u32}, allocator {u32}: error {u32}\n"),
                                        fixture, target_index, rotation, mode, (u32)code.error);
                                }
                                BUSTER_TEST(arguments, code.error == CODEGEN_ERROR_NONE);
                                if (!code.error)
                                {
                                    if (mode == CODEGEN_REGISTER_ALLOCATOR_NONE)
                                    {
                                        if (permutation == 0)
                                        {
                                            BUSTER_TEST(arguments, code.code.length <= sizeof(original_code));
                                            if (code.code.length <= sizeof(original_code))
                                            {
                                                original_code_length = code.code.length;
                                                memcpy(original_code, code.code.pointer, code.code.length);
                                            }
                                            original_location_count = code.debug_location_count;
                                        }
                                        else
                                        {
                                            BUSTER_TEST(arguments, code.debug_location_count == original_location_count);
                                            // Moving ID zero to the last ID preserves every
                                            // other block's relative order. Entry-first layout
                                            // must therefore recover the original code exactly,
                                            // including on native targets this host cannot run.
                                            if (permutation == 2)
                                            {
                                                BUSTER_TEST(arguments, code.code.length == original_code_length);
                                                if (code.code.length == original_code_length)
                                                {
                                                    BUSTER_TEST(arguments, !memcmp(code.code.pointer, original_code, original_code_length));
                                                }
                                            }
                                        }
                                    }
                                    for (u32 location = 0; location < code.debug_location_count; location += 1)
                                    {
                                        DebugLocationSeed seed = code.debug_locations[location];
                                        BUSTER_TEST(arguments, seed.start < seed.end && seed.end <= code.code.length);
                                    }
#if !BUSTER_SANITIZE && !BUSTER_ANDROID && !BUSTER_IOS
                                    if (target.cpu_arch == target_native.cpu_arch && target.os == target_native.os)
                                    {
                                        CodegenExecutable executable = codegen_make_executable((CodegenFunction){.code = code.code, .error = code.error});
                                        BUSTER_TEST(arguments, executable.error == CODEGEN_ERROR_NONE);
                                        if (executable.address)
                                        {
                                            CodegenFunctionDescriptor* descriptor = codegen_test_c_descriptor_find(&code, function->symbol);
                                            BUSTER_TEST(arguments, descriptor != 0);
                                            if (descriptor)
                                            {
                                                void* address = (u8*)executable.address + descriptor->code_offset;
                                                CodegenTestFunction0* nullary = 0;
                                                CodegenTestFunction2* binary = 0;
                                                memcpy(&nullary, &address, sizeof(nullary));
                                                memcpy(&binary, &address, sizeof(binary));
                                                for (u32 input = 0; input < BUSTER_ARRAY_LENGTH(inputs); input += 1)
                                                {
                                                    u64 expected = codegen_test_entry_expected(fixture, inputs[input][0], inputs[input][1]);
                                                    u64 actual = fixture ? binary(inputs[input][0], inputs[input][1]) : nullary();
                                                    if (actual != expected)
                                                    {
                                                        arguments->show(arguments, S8("native entry fixture {u32}, entry {u32}, allocator {u32}, input {u32}: actual={u64}, expected={u64}\n"),
                                                            fixture, rotation, mode, input, actual, expected);
                                                    }
                                                    BUSTER_TEST(arguments, actual == expected);
                                                }
                                            }
                                            codegen_release_executable(executable);
                                        }
                                    }
#endif
                                }
                            }
                        }
                    }
                }
                scratch_end(temporary);
            }
        }
    }
    return result;
}
