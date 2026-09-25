#!/usr/bin/env python3
"""Second bounded experiment. Retains, rather than overwrites, previous attempts.
Only executed by the separate hosted workflow. Produces the full effective script.
"""
import ast
import os
from pathlib import Path

original_path = Path(__file__).with_name('architecture_boundary_probe.py')
source = original_path.read_text()

def replace(old, new):
    global source
    if source.count(old) != 1:
        raise RuntimeError('experiment anchor mismatch: ' + old[:100])
    source = source.replace(old, new, 1)

# Keep the corrected common oracle. Clang object-size precision is recorded,
# not forced to agree with GCC's sentinel through side-effecting operands.
replace('    for label, expression, expected, expected_hits in rows:\n',
        '    for label, expression, expected, expected_hits in rows:\n        if "__builtin_object_size" in expression:\n            expected = None\n')
replace('\n\ndef fixture(path, rows):', '\n\nNEW.append(CONTROLS.pop())\n\ndef fixture(path, rows):')

# The original local repair remains a deliberately failing competitor. The
# structural candidate also preserves its boundary when a call is rediscovered.
extra = '''    if structural:
        text = once(text,
            "(!indirect && c_ir_prepared_control_expression_contains(builder, index)) || c_ir_prepared_call_find(builder, callee_start))",
            "(!indirect && c_ir_prepared_control_expression_contains(builder, index)))")
        anchor = "        if (c_ir_lazy_operand_scan_deferred(lazy) || comma_sequence_depth != UINT32_MAX)"
        replacement = """        CIrPreparedCall* existing_call = c_ir_prepared_call_find(builder, callee_start);
        if (existing_call)
        {
            // A hit means this call is already recorded, not that its operand
            // belongs to this scan. Preserve the owned span on replay too.
            // Do not consume an enclosing call while scanning its callee.
            if (!indirect && preparation_owner != C_IR_BUILTIN_PREPARATION_CALLER &&
                existing_call->open_index == index + 1 && existing_call->close_index < end)
            {
                index = existing_call->close_index;
            }
            continue;
        }
""" + anchor
        text = once(text, anchor, replacement)
'''
replace('    return text\n\n\n# Side effects', extra + '    return text\n\n\n# Side effects')

canonical = r'''BUSTER_GLOBAL_LOCAL UnitTestResult c_test_architecture_preparation_replay(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    String8 source = S8("static volatile unsigned hits; static char buffer[32];"
                       "static unsigned mark(void) { hits += 1; return 7; }"
                       "static unsigned long identity(unsigned long x) { return x; }"
                       "unsigned long probe_0(void) { return __builtin_constant_p((hits = mark(), 7)); }"
                       "unsigned long probe_1(void) { return __builtin_object_size((hits = mark(), buffer), 0); }"
                       "unsigned long probe_2(void) { return __builtin_constant_p((hits = 1, 7)); }"
                       "unsigned long probe_3(void) { return identity(__builtin_constant_p((hits = mark(), 7))); }"
                       "unsigned long probe_4(void) { return __builtin_choose_expr(1, mark(), (hits = 1, 7)); }"
                       "unsigned long probe_5(void) { return (hits = 1, mark()); }");
    u32 expected_calls[] = {0, 0, 0, 0, 1, 1};
    u32 expected_stores[] = {0, 0, 0, 0, 0, 1};
    for (u32 layout = 0; layout < 6; layout += 1)
    {
        Target target = target_native;
        target.cpu_arch = layout & 1 ? CPU_ARCH_AARCH64 : CPU_ARCH_X86_64;
        target.os = layout < 2 ? OPERATING_SYSTEM_LINUX : layout < 4 ? OPERATING_SYSTEM_WINDOWS : OPERATING_SYSTEM_MACOS;
        for (u32 form = 0; form < 2; form += 1)
        {
            TemporalArena temporary = scratch_begin(&arguments->arena, 1);
            CPreprocessResult tokens = c_preprocess(temporary.arena, source,
                (CPreprocessOptions){.target = target, .data_layout = target_data_layout(target), .dialect = C_PREPROCESS_DIALECT_GNU17});
            CParseResult parsed = c_parse(temporary.arena, tokens);
            CIRLowerResult lowered = c_lower_to_ir_with_options(temporary.arena, S8("preparation-replay.c"), tokens, parsed, target,
                (CIRLowerOptions){.disable_direct_ssa = form != 0});
            if (BUSTER_REQUIRE(arguments, !tokens.diagnostic_count && !parsed.diagnostic_count && !lowered.diagnostic_count && lowered.program))
            {
                IrModule* module = lowered.program->modules;
                BUSTER_TEST(arguments, ir_validate_canonical_module(lowered.program, module).error == IR_VALIDATION_NONE);
                for (u32 probe = 0; probe < BUSTER_ARRAY_LENGTH(expected_calls); probe += 1)
                {
                    String8 name = string_format(temporary.arena, S8("probe_{u32}"), probe);
                    IrFunction* function = c_test_find_ir_function(module, name);
                    if (BUSTER_REQUIRE(arguments, function != 0))
                    {
                        u32 calls = c_test_ir_direct_call_count(lowered.program, function, S8("mark"));
                        u32 stores = 0;
                        for (u32 index = 0; index < function->instruction_count; index += 1)
                        {
                            IrInstruction* instruction = function->instructions + index;
                            stores += instruction->opcode == IR_OPCODE_STORE && instruction->volatile_access;
                        }
                        String8 observation = string_format(temporary.arena,
                            S8("replay layout={u32} form={u32} {S8}: mark={u32}/{u32} stores={u32}/{u32}"),
                            layout, form, name, calls, expected_calls[probe], stores, expected_stores[probe]);
                        BUSTER_TEST_RAW(arguments, calls == expected_calls[probe], observation);
                        BUSTER_TEST_RAW(arguments, stores == expected_stores[probe], observation);
                    }
                }
            }
            scratch_end(temporary);
        }
    }
    return result;
}

'''
insert = '\nTEST = Path("src/buster/tests/compiler/frontend/c/c_test.c")\nCANONICAL = ' + repr(canonical) + '''

def install_canonical(root):
    text = (root / TEST).read_text()
    text = once(text, 'UnitTestResult c_frontend_tests(UnitTestArguments* arguments)', CANONICAL + 'UnitTestResult c_frontend_tests(UnitTestArguments* arguments)')
    anchor = '    BUSTER_TEST_FIXTURE(arguments, c_test_choose_expr_evaluation);'
    text = once(text, anchor, anchor + '\n    BUSTER_TEST_FIXTURE(arguments, c_test_architecture_preparation_replay);')
    (root / TEST).write_text(text)
    git(root, 'add', str(TEST))

'''
replace('\ndef main():', insert + '\ndef main():')
replace("    baseline_binary = configure_build(base, 'baseline')\n    fixed_point(base, baseline_binary, 'baseline')",
        "    install_canonical(base)\n    RESULTS['baseline_test_tree'] = git(base, 'write-tree')\n    (E / 'baseline-test.patch').write_text(git(base, 'diff', '--cached') + '\\n')\n    baseline_binary = configure_build(base, 'baseline')\n    RESULTS['baseline_fixed_point'] = 'Original exact pin passed in retained run 36183459094; not repeated on test-only tree.'")
replace("        git(root, 'add', str(GEN))", "        install_canonical(root)\n        git(root, 'add', str(GEN))")
# Strict MIR execution is separate from the none/canonical backend control.
replace("'-fregister-allocator=' + mode, '-fverify-codegen', E /", "'-fregister-allocator=' + mode, '-fverify-codegen', *([] if mode == 'none' else ['-fno-machine-fallback']), E /")
# The previous invalid -S/-emit-llvm attempts remain in the old archive. Obtain
# raw canonical evidence through the registered test, not mislabeled LLVM text.
line = "            run(variant + '-' + group + '-llvm', [binary, 'cc', '-std=gnu17', '-S', '-emit-llvm', E / (group + '.c'), '-o', E / (variant + '-' + group + '.ll')], root)\n"
replace(line, '')
# Negative cases must still be diagnosed; record rather than erase all outputs.
negative = '''        for negative_name, body in (
            ('unknown', 'int f(void) { return __builtin_constant_p(missing_name); }'),
            ('member', 'struct P { int x; }; int f(void) { struct P p; return __builtin_constant_p(p.missing); }'),
            ('object-mode', 'int f(void) { return __builtin_object_size((void*)0, 9); }')):
            path = E / ('negative-' + negative_name + '.c')
            path.write_text(body + '\\n')
            name = variant + '-negative-' + negative_name
            RESULTS[name] = run(name, [binary, 'cc', '-std=gnu17', '-c', path, '-o', E / (name + '.o')], root)
'''
replace("        if variant in ('baseline', 'structural'):", negative + "        if variant in ('baseline', 'structural'):")
# Observe lookup hits without changing skip semantics, in the final diagnostic
# builds only. Four object comparisons distinguish instrumentation effects.
trace = '''    helper = """BUSTER_C_INTERNAL CIrPreparedCall* c_ir_research_prepared_hit(CIntegerIrBuilder* builder, u32 token_index, u32 end)
{
    CIrPreparedCall* result = c_ir_prepared_call_find(builder, token_index);
    if (result && (result->builtin_constant_p || result->builtin_object_size))
    {
        string_print(S8("ARCH_REPLAY token={u32} open={u32} close={u32} end={u32} emitted={u32}\\n"),
            token_index, result->open_index, result->close_index, end, (u32)result->emitted);
    }
    return result;
}

"""
    declaration = 'BUSTER_C_INTERNAL bool c_ir_prepare_calls_discover(CIntegerIrBuilder* builder, u32 start, u32 end, u32** emission_order_out,'
    text = once(text, declaration, helper + declaration)
    text = once(text, 'c_ir_prepared_call_find(builder, callee_start)', 'c_ir_research_prepared_hit(builder, callee_start, end)')
'''
# Second return text in file belongs to instrument(), addressed by following def.
replace('    return text\n\n\nTEST', trace + '    return text\n\n\nTEST')
# The intentionally retained local repair has six known failures. Do not call it
# successful. The new structural candidate alone must pass every behavioral gate.
replace("('local-new-', 'structural-new-', 'local-controls-', 'structural-controls-')", "('structural-new-', 'structural-controls-')")
replace("    if bad:\n", "    if RESULTS.get('structural_suite') != 0 or not RESULTS.get('structural_fixed_point', {}).get('equal'):\n        bad.append('structural-suite-or-fixed-point')\n    for name in ('unknown', 'member', 'object-mode'):\n        if RESULTS.get('structural-negative-' + name) != RESULTS.get('baseline-negative-' + name) or RESULTS.get('structural-negative-' + name) == 0:\n            bad.append('diagnostic-' + name)\n    if bad:\n")

ast.parse(source)
evidence = Path(os.environ['ARCH_EVIDENCE'])
effective = evidence / 'experiment.py'
effective.write_text(source)
# Fail closed on every source transformation before spending a compiler build.
namespace = {'__name__': 'preflight'}
exec(compile(source.split('\ntry:\n    main()')[0], str(effective), 'exec'), namespace)
base = Path(os.sys.argv[1])
text = (base / namespace['GEN']).read_text()
for structural in (False, True):
    candidate = namespace['repair'](text, structural)
    namespace['instrument'](candidate)
namespace['instrument'](text)
exec(compile(source, str(effective), 'exec'), {'__name__': '__main__'})
