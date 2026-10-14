// First-party #3670 witnesses. Equality consumes label provenance before logical composition.
// The frozen typeof witness and plain reduction run against independent hosted GCC/Clang
// before Buster's pilot/tree-free and SSA/memory cells. Pointer transport remains refused.
BUSTER_GLOBAL_LOCAL UnitTestResult c_test_label_comparison_logicals(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    String8 sources[] = {
        S8("int main(void) { int a=1,b=0; void *p=(__typeof__((void*)0))&&done;"
           "void *q=(__typeof(void*))&&done; void *r=(typeof((void*)0))&&done;"
           "void *s=(__typeof__((a && b,(void*)0)))&&done;"
           "done: return p!=&&done || q!=&&done || r!=&&done || s!=&&done; }"),
        S8("int main(void) { void *p=&&done,*q=&&done; done: return p!=&&done || q!=&&done; }"),
        S8("int main(void) { void *p=&&done,*q=&&done; done:"
           "return (p!=&&done || q!=&&done) && (p==&&done && q==&&done); }"),
        S8("int main(void) { void *p=&&done; done: return 0 || (((p!=&&done) && 1)); }"),
        S8("int main(void) { void *p=&&done; done: return (((p!=&&done) || 0)) && 1; }"),
        S8("int main(void) { int n=0; void *p=&&done; done:;"
           "int yes=((p==&&done && ++n) && (p==&&done && ++n));"
           "int no=((p!=&&done && ++n) || (p==&&done || ++n));"
           "return yes!=1 || no!=1 || n!=2; }"),
        S8("int saved; int take(int x) { saved=x; return x; }"
           "int main(void) { void *p=&&done; done: return take(p!=&&done || p!=&&done) || saved; }"),
        S8("int saved; int main(void) { void *p=&&done; done:"
           "saved=(p!=&&done || (p!=&&done && 1)); return saved; }"),
    };
    String8 escapes[] = {
        S8("int f(void) { void *p=&&done; done: return p || 0; }"),
        S8("int f(void) { void *p=&&done; done: return p && 1; }"),
        S8("int f(void) { done: return 0 || &&done; }"),
        S8("long f(void) { void *p=&&done; done: return (long)p; }"),
        S8("void *f(void) { void *p=&&done; done: return (p==&&done) ? p : 0; }"),
        S8("void *f(void) { void *p=&&done; done: return (p!=&&done,p); }"),
        S8("void take(void*); void f(void) { void *p=&&done; done: take(p); }"),
        S8("void *saved; void f(void) { void *p=&&done; done: saved=p; }"),
    };
    CPreprocessDialect dialects[] = {C_PREPROCESS_DIALECT_GNU17, C_PREPROCESS_DIALECT_GNU23};
    Target targets[] = {
        {.cpu_arch = CPU_ARCH_X86_64, .os = OPERATING_SYSTEM_LINUX},
        {.cpu_arch = CPU_ARCH_AARCH64, .os = OPERATING_SYSTEM_LINUX},
        {.cpu_arch = CPU_ARCH_X86_64, .os = OPERATING_SYSTEM_WINDOWS},
        {.cpu_arch = CPU_ARCH_AARCH64, .os = OPERATING_SYSTEM_WINDOWS},
        {.cpu_arch = CPU_ARCH_X86_64, .os = OPERATING_SYSTEM_MACOS},
        {.cpu_arch = CPU_ARCH_AARCH64, .os = OPERATING_SYSTEM_MACOS},
    };
#if BUSTER_LINUX && BUSTER_CPU_ARCH_X86_64
    // Independent expected-value runs happen before any Buster acceptance assertion.
    String8 reference_names[] = {S8("gcc"), S8("clang")};
    String8 reference_dialects[] = {S8("-std=gnu17"), S8("-std=gnu2x")};
    for (u32 reference = 0; reference < BUSTER_ARRAY_LENGTH(reference_names); reference += 1)
    {
        TemporalArena temporary = scratch_begin(&arguments->arena, 1);
        String8 compiler = executable_resolve_in_path(temporary.arena, reference_names[reference]);
        BUSTER_TEST(arguments, compiler.length != 0);
        TestReferenceIdentity identity = buster_test_reference_identity(temporary.arena, compiler, 0);
        arguments->show(arguments, S8("TEST_ORACLE_IDENTITY suite=c-frontend fixture=label-comparison-logicals tool={S8} version={S8} target={S8}\n"),
            compiler, identity.version, identity.target);
        for (u32 source = 0; compiler.length && source < BUSTER_ARRAY_LENGTH(sources); source += 1)
        {
            for (u32 dialect = 0; dialect < BUSTER_ARRAY_LENGTH(reference_dialects); dialect += 1)
            {
                String8 input = buster_test_temporary_unique_path(temporary.arena, S8("label-logical-oracle"), S8(".c"));
                String8 output = buster_test_temporary_unique_path(temporary.arena, S8("label-logical-oracle"), S8(".exe"));
                bool written = file_write(input, BUSTER_SLICE_TO_BYTE_SLICE(sources[source]));
                BUSTER_TEST(arguments, written);
                if (written)
                {
                    String8 command[] = {compiler, reference_dialects[dialect], S8("-O0"), input, S8("-o"), output};
                    ProcessSpawnResult spawn = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(command), (SliceString8){0}, (SliceString8){0},
                        (ProcessSpawnOptions){.capture = ((u64)1 << STANDARD_STREAM_OUTPUT) | ((u64)1 << STANDARD_STREAM_ERROR),
                                              .use_process_environment = true});
                    if (BUSTER_REQUIRE(arguments, spawn.handle != 0))
                    {
                        ProcessWaitResult wait = os_process_wait_deadline(temporary.arena, spawn, 30000000);
                        bool built = !wait.timed_out && wait.result == PROCESS_RESULT_SUCCESS;
                        BUSTER_TEST_RAW(arguments, built, BYTE_SLICE_TO_STRING(8, wait.streams[STANDARD_STREAM_ERROR]));
                        if (built)
                        {
                            String8 run[] = {output};
                            ProcessSpawnResult child = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(run), (SliceString8){0}, (SliceString8){0},
                                (ProcessSpawnOptions){.use_process_environment = true});
                            if (BUSTER_REQUIRE(arguments, child.handle != 0))
                            {
                                ProcessWaitResult execution = os_process_wait_deadline(temporary.arena, child, 30000000);
                                BUSTER_TEST(arguments, !execution.timed_out && execution.result == PROCESS_RESULT_SUCCESS);
                                arguments->show(arguments, S8("LABEL_LOGICAL_ORACLE_V1 source={u32} dialect={S8} tool={S8} status={u32} timeout={u32}\n"),
                                    source, reference_dialects[dialect], compiler, execution.platform_status, (u32)execution.timed_out);
                            }
                            BUSTER_TEST(arguments, os_file_delete(output));
                        }
                    }
                    BUSTER_TEST(arguments, os_file_delete(input));
                }
            }
        }
        c_test_scratch_end(temporary);
    }
#endif
    for (u32 source = 0; source < BUSTER_ARRAY_LENGTH(sources) + BUSTER_ARRAY_LENGTH(escapes); source += 1)
    {
        bool positive = source < BUSTER_ARRAY_LENGTH(sources);
        String8 text = positive ? sources[source] : escapes[source - BUSTER_ARRAY_LENGTH(sources)];
        for (u32 dialect = 0; dialect < BUSTER_ARRAY_LENGTH(dialects); dialect += 1)
        {
            for (u32 target = 0; target < BUSTER_ARRAY_LENGTH(targets); target += 1)
            {
                TemporalArena temporary = scratch_begin(&arguments->arena, 1);
                CPreprocessResult tokens = c_preprocess(temporary.arena, text,
                    (CPreprocessOptions){.target = targets[target], .dialect = dialects[dialect]});
                CParserResult syntax = c_parse_ast(temporary.arena, tokens);
                CAnalysisResult semantic = c_analyze_semantics_only(temporary.arena, tokens, syntax);
                BUSTER_TEST(arguments, !tokens.diagnostic_count);
                if (positive)
                {
                    BUSTER_TEST_RAW(arguments, !semantic.diagnostic_count, text);
                    for (u32 form = 0; !semantic.diagnostic_count && form < 2; form += 1)
                    {
                        CIRLowerResult lowered = c_lower_to_ir_with_options(temporary.arena, S8("label-logical.c"), tokens, semantic, targets[target],
                            (CIRLowerOptions){.disable_direct_ssa = form != 0});
                        if (BUSTER_REQUIRE(arguments, !lowered.diagnostic_count && lowered.program && lowered.program->module_count == 1))
                            BUSTER_TEST(arguments, ir_validate_canonical_module(lowered.program, lowered.program->modules).error == IR_VALIDATION_NONE);
                    }
                }
                else
                {
                    bool refused = false;
                    for (u32 row = 0; row < semantic.diagnostic_count; row += 1)
                        refused |= string_first_sequence(semantic.diagnostics[row].message, S8("a label address may not")) != BUSTER_STRING_NO_MATCH;
                    BUSTER_TEST_RAW(arguments, refused, text);
                }
                c_test_scratch_end(temporary);
            }
        }
#if (BUSTER_CPU_ARCH_X86_64 || BUSTER_CPU_ARCH_AARCH64) && !BUSTER_ANDROID && !BUSTER_IOS
        TemporalArena fixture = scratch_begin(&arguments->arena, 1);
        String8 input = buster_test_temporary_unique_path(fixture.arena, S8("label-logical"), S8(".c"));
        if (BUSTER_REQUIRE(arguments, file_write(input, BUSTER_SLICE_TO_BYTE_SLICE(text))))
        {
            String8 standards[] = {S8("-std=gnu17"), S8("-std=gnu23")};
            String8 modes[] = {S8("-fregister-allocator=fast"), S8("-fregister-allocator=quality")};
            for (u32 dialect = 0; dialect < BUSTER_ARRAY_LENGTH(standards); dialect += 1)
            {
                for (u32 pilot = 0; pilot < 2; pilot += 1)
                {
                    for (u32 form = 0; form < 2; form += 1)
                    {
                        for (u32 mode = 0; mode < BUSTER_ARRAY_LENGTH(modes); mode += 1)
                        {
                            TemporalArena temporary = scratch_begin(&fixture.arena, 1);
                            String8 output = buster_test_temporary_unique_path(temporary.arena, S8("label-logical-run"), S8(".exe"));
                            String8 command[] = {pilot ? S8("-fc-ast-pilot") : S8("-fno-c-ast-pilot"), S8("-nostdinc"), standards[dialect], modes[mode],
                                S8("-fverify-codegen"), form ? S8("-fno-frontend-ssa") : S8("-ffrontend-ssa"), S8("-o"), output, input};
                            CompilerDriverResult compiled = compiler_driver_execute_invocation(temporary.arena,
                                compiler_driver_parse_arguments(temporary.arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(command)));
                            if (positive)
                            {
                                BUSTER_TEST_RAW(arguments, compiled.error == COMPILER_DRIVER_ERROR_NONE, compiled.diagnostic);
                                if (compiled.error == COMPILER_DRIVER_ERROR_NONE)
                                {
                                    String8 run[] = {output};
                                    ProcessSpawnResult child = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(run), (SliceString8){0}, (SliceString8){0},
                                        (ProcessSpawnOptions){.use_process_environment = true});
                                    if (BUSTER_REQUIRE(arguments, child.handle != 0))
                                    {
                                        ProcessWaitResult execution = os_process_wait_deadline(temporary.arena, child, 30000000);
                                        BUSTER_TEST(arguments, !execution.timed_out && execution.result == PROCESS_RESULT_SUCCESS);
                                    }
                                    BUSTER_TEST(arguments, !buster_test_executable_delete(arguments, output).v);
                                }
                            }
                            else
                                BUSTER_TEST_RAW(arguments, compiled.error != COMPILER_DRIVER_ERROR_NONE &&
                                    string_first_sequence(compiled.diagnostic, S8("a label address may not")) != BUSTER_STRING_NO_MATCH, compiled.diagnostic);
                            c_test_scratch_end(temporary);
                        }
                    }
                }
            }
            BUSTER_TEST(arguments, os_file_delete(input));
        }
        c_test_scratch_end(fixture);
#endif
    }
    return result;
}
