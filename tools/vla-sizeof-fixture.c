// sizeof evaluates an operand of VLA type, even when its saved size is known.
// For int a[n][3], a[index++] has fixed row type; for int a[n], it has
// scalar type. Neither operand is evaluated. Every probe observes effects after the
// tested full expression; none prescribes an order for sibling arguments.
BUSTER_GLOBAL_LOCAL UnitTestResult c_test_sizeof_vla_evaluation(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    struct
    {
        String8 setup;
        String8 base;
        u32 size;
        bool evaluated;
    } layouts[] = {
        {S8("int n = 3; int a[2][n];"), S8("a"), 12, true},
        {S8("int n = 3; int a[2][n]; int (*p)[n] = a;"), S8("p"), 12, true},
        {S8("int n = 3; int a[n][3];"), S8("a"), 12, false},
        {S8("int n = 3; int a[n];"), S8("a"), 4, false},
    };
    struct
    {
        String8 index;
        u32 index_after;
        u32 hits_after;
        u32 calls;
        u32 stores;
    } effects[] = {
        {S8("index++"), 1, 0, 0, 1},
        {S8("tick()"), 0, 1, 1, 0},
        {S8("({ index += 1; 0; })"), 1, 0, 0, 1},
    };
    enum { C_SIZEOF_VLA_CONTEXT_COUNT = 6 };
    struct
    {
        String8 body;
        u32 size;
        u32 index_after;
    } neighbors[] = {
        {S8("int n = 3; int a[2][n]; return sizeof(((a[index++])));"), 12, 1},
        {S8("int n = 3; int a[2][n]; n = 7; return sizeof(a[index++]);"), 12, 1},
        {S8("int n = 3; typedef int Row[n]; Row a[2]; return sizeof(a[index++]);"), 12, 1},
        {S8("int n = 3; int a[2][n]; return sizeof(sizeof(a[index++])) != sizeof(sizeof(0));"), 0, 0},
        {S8("int n = 3; int a[2][n]; return 1 + sizeof(a[index++]);"), 13, 1},
        {S8("int n = 3; int a[2][n]; return sizeof(a[index++]) + 1;"), 13, 1},
    };
    enum
    {
        C_SIZEOF_VLA_PROBE_COUNT = BUSTER_ARRAY_LENGTH(layouts) * BUSTER_ARRAY_LENGTH(effects) * C_SIZEOF_VLA_CONTEXT_COUNT +
                                  BUSTER_ARRAY_LENGTH(neighbors),
    };
    u32 expected_calls[C_SIZEOF_VLA_PROBE_COUNT];
    u32 expected_stores[C_SIZEOF_VLA_PROBE_COUNT];
    String8 source = S8("static volatile unsigned index; static volatile unsigned hits;\n"
                        "static unsigned tick(void) { hits += 1; return 0; }\n"
                        "static unsigned long identity(unsigned long value) { return value; }\n");
    String8 checks = S8("int main(void) { unsigned failures = 0; unsigned long value;\n");
    u32 probe_count = 0;
    for (u32 layout = 0; layout < BUSTER_ARRAY_LENGTH(layouts); layout += 1)
    {
        for (u32 effect = 0; effect < BUSTER_ARRAY_LENGTH(effects); effect += 1)
        {
            String8 expression = string_format(arguments->arena, S8("sizeof({S8}[{S8}])"), layouts[layout].base, effects[effect].index);
            String8 bodies[] = {
                string_format(arguments->arena, S8("unsigned long value = {S8}; return value;"), expression),
                string_format(arguments->arena, S8("return identity({S8});"), expression),
                string_format(arguments->arena, S8("return {S8};"), expression),
                string_format(arguments->arena, S8("unsigned long value = 0; if ({S8} == {u32}ul) value = {u32}ul; return value;"),
                              expression, layouts[layout].size, layouts[layout].size),
                string_format(arguments->arena, S8("{S8}; return 0;"), expression),
                string_format(arguments->arena, S8("return ({{ unsigned long value = {S8}; value; }});"), expression),
            };
            BUSTER_TEST(arguments, BUSTER_ARRAY_LENGTH(bodies) == C_SIZEOF_VLA_CONTEXT_COUNT);
            for (u32 context = 0; context < BUSTER_ARRAY_LENGTH(bodies); context += 1)
            {
                u32 size = context == 4 ? 0 : layouts[layout].size;
                u32 index_after = layouts[layout].evaluated ? effects[effect].index_after : 0;
                u32 hits_after = layouts[layout].evaluated ? effects[effect].hits_after : 0;
                expected_calls[probe_count] = (layouts[layout].evaluated ? effects[effect].calls : 0) + (context == 1);
                expected_stores[probe_count] = layouts[layout].evaluated ? effects[effect].stores : 0;
                source = string_format(arguments->arena, S8("{S8}static unsigned long probe_{u32}(void) {{{S8} {S8}}}\n"),
                                       source, probe_count, layouts[layout].setup, bodies[context]);
                checks = string_format(arguments->arena,
                    S8("{S8}index = 0; hits = 0; value = probe_{u32}(); failures |= (value != {u32}ul) | (index != {u32}u) | (hits != {u32}u);\n"),
                    checks, probe_count, size, index_after, hits_after);
                probe_count += 1;
            }
        }
    }
    for (u32 neighbor = 0; neighbor < BUSTER_ARRAY_LENGTH(neighbors); neighbor += 1)
    {
        expected_calls[probe_count] = 0;
        expected_stores[probe_count] = neighbors[neighbor].index_after;
        source = string_format(arguments->arena, S8("{S8}static unsigned long probe_{u32}(void) {{{S8}}}\n"),
                               source, probe_count, neighbors[neighbor].body);
        checks = string_format(arguments->arena,
            S8("{S8}index = 0; hits = 0; value = probe_{u32}(); failures |= (value != {u32}ul) | (index != {u32}u) | (hits != 0);\n"),
            checks, probe_count, neighbors[neighbor].size, neighbors[neighbor].index_after);
        probe_count += 1;
    }
    BUSTER_TEST(arguments, probe_count == C_SIZEOF_VLA_PROBE_COUNT);
    source = string_format(arguments->arena, S8("{S8}{S8}return (int)failures; }}\n"), source, checks);
    Target targets[] = {
        {.cpu_arch = CPU_ARCH_X86_64, .os = OPERATING_SYSTEM_LINUX},
        {.cpu_arch = CPU_ARCH_AARCH64, .os = OPERATING_SYSTEM_LINUX},
        {.cpu_arch = CPU_ARCH_X86_64, .os = OPERATING_SYSTEM_WINDOWS},
        {.cpu_arch = CPU_ARCH_AARCH64, .os = OPERATING_SYSTEM_WINDOWS},
        {.cpu_arch = CPU_ARCH_X86_64, .os = OPERATING_SYSTEM_MACOS},
        {.cpu_arch = CPU_ARCH_AARCH64, .os = OPERATING_SYSTEM_MACOS},
    };
    for (u32 target_index = 0; target_index < BUSTER_ARRAY_LENGTH(targets); target_index += 1)
    {
        for (u32 form = 0; form < 2; form += 1)
        {
            TemporalArena temporary = scratch_begin(&arguments->arena, 1);
            Target target = targets[target_index];
            CPreprocessResult tokens = c_preprocess(temporary.arena, source,
                (CPreprocessOptions){.target = target, .data_layout = target_data_layout(target), .dialect = C_PREPROCESS_DIALECT_GNU17});
            CParserResult syntax = c_parse_ast(temporary.arena, tokens);
            CAnalysisResult semantic = c_analyze_semantics_only(temporary.arena, tokens, syntax);
            bool valid = !tokens.diagnostic_count && !syntax.diagnostic_count && !semantic.diagnostic_count && semantic.analysis_complete;
            if (BUSTER_REQUIRE(arguments, valid))
            {
                CIRLowerResult lowered = c_lower_to_ir_with_options(temporary.arena, S8("sizeof-vla-evaluation.c"), tokens, semantic, target,
                                                                   (CIRLowerOptions){.disable_direct_ssa = form != 0});
                if (BUSTER_REQUIRE(arguments, lowered.program && !lowered.diagnostic_count && lowered.program->module_count == 1))
                {
                    IrModule* module = lowered.program->modules;
                    BUSTER_TEST(arguments, ir_validate_canonical_module(lowered.program, module).error == IR_VALIDATION_NONE);
                    for (u32 probe = 0; probe < probe_count; probe += 1)
                    {
                        String8 name = string_format(temporary.arena, S8("probe_{u32}"), probe);
                        IrFunction* function = c_test_find_ir_function(module, name);
                        if (BUSTER_REQUIRE(arguments, function != 0))
                        {
                            u32 calls = 0;
                            u32 stores = 0;
                            for (u32 instruction_index = 0; instruction_index < function->instruction_count; instruction_index += 1)
                            {
                                IrInstruction* instruction = function->instructions + instruction_index;
                                calls += instruction->opcode == IR_OPCODE_CALL;
                                stores += instruction->opcode == IR_OPCODE_STORE && instruction->volatile_access;
                            }
                            BUSTER_TEST_RAW(arguments, calls == expected_calls[probe] && stores == expected_stores[probe],
                                string_format(temporary.arena, S8("sizeof VLA {S8} target={u32} form={u32}: calls={u32}/{u32} volatile-stores={u32}/{u32}"),
                                              name, target_index, form, calls, expected_calls[probe], stores, expected_stores[probe]));
                        }
                    }
                }
            }
            scratch_end(temporary);
        }
    }
#if (BUSTER_CPU_ARCH_X86_64 || BUSTER_CPU_ARCH_AARCH64) && !BUSTER_ANDROID && !BUSTER_IOS
    String8 input = buster_test_temporary_path(arguments->arena, S8("sizeof-vla-evaluation"), S8(".c"));
    if (BUSTER_REQUIRE(arguments, file_write(input, BUSTER_SLICE_TO_BYTE_SLICE(source))))
    {
        String8 allocators[] = {S8("-fregister-allocator=none"), S8("-fregister-allocator=mir-stack"),
                               S8("-fregister-allocator=fast"), S8("-fregister-allocator=quality")};
        String8 frontends[] = {S8("-ffrontend-ssa"), S8("-fno-frontend-ssa")};
        for (u32 allocator = 0; allocator < BUSTER_ARRAY_LENGTH(allocators); allocator += 1)
        {
            for (u32 form = 0; form < BUSTER_ARRAY_LENGTH(frontends); form += 1)
            {
                TemporalArena temporary = scratch_begin(&arguments->arena, 1);
                String8 output = buster_test_temporary_path(temporary.arena, S8("sizeof-vla-evaluation-run"), S8(".exe"));
                String8 command[] = {S8("-nostdinc"), S8("-std=gnu17"), S8("-fwrapv"), S8("-fno-strict-aliasing"), S8("-funsigned-char"),
                                     allocators[allocator], frontends[form], S8("-fverify-codegen"), S8("-o"), output, input};
                CompilerDriverInvocation invocation = compiler_driver_parse_arguments(temporary.arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(command));
                invocation.reject_machine_fallback = allocator != 0;
                CompilerDriverResult compiled = compiler_driver_execute_invocation(temporary.arena, invocation);
                BUSTER_TEST_RAW(arguments, compiled.error == COMPILER_DRIVER_ERROR_NONE,
                    string_format(temporary.arena, S8("sizeof VLA {S8} {S8}: {S8}"), allocators[allocator], frontends[form], compiled.diagnostic));
                if (compiled.error == COMPILER_DRIVER_ERROR_NONE)
                {
                    String8 run[] = {output};
                    ProcessSpawnResult child = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(run), (SliceString8){0}, (SliceString8){0},
                                                               (ProcessSpawnOptions){.use_process_environment = true});
                    if (BUSTER_REQUIRE(arguments, child.handle != 0))
                    {
                        ProcessWaitResult execution = os_process_wait_deadline(temporary.arena, child, 30000000);
                        BUSTER_TEST_RAW(arguments, !execution.timed_out && execution.result == PROCESS_RESULT_SUCCESS,
                            string_format(temporary.arena, S8("sizeof VLA runtime {S8} {S8}: status={u32} timed_out={u32}"),
                                          allocators[allocator], frontends[form], execution.platform_status, (u32)execution.timed_out));
                    }
                }
                scratch_end(temporary);
            }
        }
    }
#endif
    return result;
}

