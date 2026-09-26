#!/usr/bin/env python3
"""Temporary, exact-source experiment; never changes external workload inputs."""
import argparse
import hashlib
import pathlib

CASES = [
    ('mark(1, 0), mark(2, 1)', 1, 12),
    ('(void)mark(1, 1), mark(2, 0)', 0, 12),
    ('mark(1, 1), mark(2, 0), mark(3, 1)', 1, 123),
    ('((mark(1, 1), mark(2, 0)))', 0, 12),
    ('0 && mark(1, 1), mark(2, 1)', 1, 2),
    ('1 || mark(1, 1), mark(2, 0)', 0, 2),
    ('mark(1, 0) && mark(2, 1), mark(3, 1)', 1, 13),
    ('mark(1, 1) || mark(2, 0), mark(3, 0)', 0, 13),
    ('mark(1, 0), mark(2, 0) || mark(3, 1)', 1, 123),
    ('mark(1, 1), mark(2, 1) && mark(3, 0)', 0, 123),
    ('flag ? mark(1, 1), mark(2, 0) : mark(3, 1)', 0, 12),
    ('flag ? mark(1, 1) : mark(2, 0), mark(3, 1)', 1, 13),
    ('0 ? mark(1, 1) : mark(2, 0), mark(3, 1)', 1, 23),
    ('indirect(1, 1), indirect(2, 0)', 0, 12),
    ('sizeof(mark(1, 1)), mark(2, 1)', 1, 2),
    ('slot = mark(1, 0), mark(2, 1)', 1, 12),
    ('mark(1, 0) && mark(2, 1)', 0, 1),
    ('mark(1, 1) || mark(2, 0)', 1, 1),
    ('pair(1, 2)', 1, 0),
    ('mark(1, 1), (flag ? mark(2, 0) : mark(3, 1))', 0, 12),
]
PRELUDE = ('static volatile unsigned trace; static int slot;\n'
           'static int mark(int digit, int value) { trace = trace * 10u + (unsigned)digit; return value; }\n'
           'static int (*indirect)(int, int) = mark;\n'
           'static int pair(int a, int b) { return a + b; }\n')

def make_source():
    definitions = [PRELUDE]
    checks = ['int main(void) { unsigned failed = 0; int value;\n']
    for row, (expression, truth, trace) in enumerate(CASES):
        for context in range(6):
            probe = row * 6 + context
            if context == 0:
                body = 'int value = 0; if (%s) value = 1; return value;' % expression
            elif context == 1:
                body = 'int value = 0; while (%s) { value = 1; break; } return value;' % expression
            elif context == 2:
                body = 'int value = 0; do { value += 1; if (value == 2) break; } while (%s); return value - 1;' % expression
            elif context == 3:
                body = 'int value = 0; for (; %s;) { value = 1; break; } return value;' % expression
            elif context == 4:
                body = 'return (%s) ? 1 : 0;' % expression
            else:
                body = 'return !!(%s);' % expression
            definitions.append('int probe_%d(int flag) { %s }\n' % (probe, body))
            checks.append('trace = 0; slot = 9; value = probe_%d(1); failed |= value != %d; failed |= trace != %du;\n' % (probe, truth, trace))
            if row == 15:
                checks.append('failed |= slot != 0;\n')
    return ''.join(definitions + checks + ['return failed != 0; }\n'])

FIXTURE_TEMPLATE = r'''
// C 6.5.17 sequences the complete left operand before the right operand;
// conditions test the final value. The comma is not short-circuiting, and
// a comma in the middle operand of ?: is not a root comma. Keep argument
// separators and unevaluated sizeof as controls, not ordering assertions.
BUSTER_INTERNAL UnitTestResult c_test_comma_condition_evaluation(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    String8 source_text = S8(
@SOURCE@
    );
    Target targets[] = {target_native, target_native, target_native, target_native, target_native, target_native};
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(targets); index += 1)
    {
        targets[index].cpu_arch = index & 1 ? CPU_ARCH_AARCH64 : CPU_ARCH_X86_64;
        targets[index].os = index < 2 ? OPERATING_SYSTEM_LINUX : index < 4 ? OPERATING_SYSTEM_WINDOWS : OPERATING_SYSTEM_MACOS;
    }
    for (u32 target_index = 0; target_index < BUSTER_ARRAY_LENGTH(targets); target_index += 1)
    {
        for (u32 form = 0; form < 2; form += 1)
        {
            Arena* conflicts[] = {arguments->arena};
            TemporalArena temporary = scratch_begin(conflicts, BUSTER_ARRAY_LENGTH(conflicts));
            Target target = targets[target_index];
            CPreprocessResult tokens = c_preprocess(temporary.arena, source_text,
                (CPreprocessOptions){.target = target, .data_layout = target_data_layout(target), .dialect = C_PREPROCESS_DIALECT_GNU17});
            CParseResult parsed = c_parse(temporary.arena, tokens);
            CIRLowerResult lowered = c_lower_to_ir_with_options(temporary.arena, S8("comma-condition.c"), tokens, parsed, target,
                (CIRLowerOptions){.disable_direct_ssa = form != 0});
            if (BUSTER_REQUIRE(arguments, !tokens.diagnostic_count && !parsed.diagnostic_count && !lowered.diagnostic_count && lowered.program))
            {
                IrModule* module = lowered.program->modules;
                BUSTER_TEST(arguments, ir_validate_canonical_module(lowered.program, module).error == IR_VALIDATION_NONE);
                for (u32 probe = 0; probe < 6; probe += 1)
                {
                    IrFunction* function = c_test_find_ir_function(module, string_format(temporary.arena, S8("probe_{u32}"), probe));
                    if (BUSTER_REQUIRE(arguments, function != 0))
                    {
                        BUSTER_TEST(arguments, c_test_ir_direct_call_count(lowered.program, function, S8("mark")) == 2);
                    }
                }
            }
            scratch_end(temporary);
        }
    }
#if (BUSTER_CPU_ARCH_X86_64 || BUSTER_CPU_ARCH_AARCH64) && !BUSTER_ANDROID && !BUSTER_IOS
    String8 source_path = buster_test_temporary_path(arguments->arena, S8("comma-condition-runtime"), S8(".c"));
    if (BUSTER_REQUIRE(arguments, file_write(source_path, BUSTER_SLICE_TO_BYTE_SLICE(source_text))))
    {
        String8 modes[] = {S8("-fregister-allocator=none"), S8("-fregister-allocator=mir-stack"), S8("-fregister-allocator=fast"), S8("-fregister-allocator=quality")};
        String8 frontends[] = {S8("-ffrontend-ssa"), S8("-fno-frontend-ssa")};
        String8 optimizations[] = {S8("-O0"), S8("-O2")};
        for (u32 mode = 0; mode < BUSTER_ARRAY_LENGTH(modes); mode += 1)
        {
            for (u32 form = 0; form < BUSTER_ARRAY_LENGTH(frontends); form += 1)
            {
                for (u32 optimization = 0; optimization < BUSTER_ARRAY_LENGTH(optimizations); optimization += 1)
                {
                    Arena* conflicts[] = {arguments->arena};
                    TemporalArena temporary = scratch_begin(conflicts, BUSTER_ARRAY_LENGTH(conflicts));
                    String8 output = buster_test_temporary_path(temporary.arena, S8("comma-condition-run"), S8(".exe"));
                    String8 command[] = {S8("-nostdinc"), S8("-std=gnu17"), modes[mode], frontends[form], optimizations[optimization], S8("-fverify-codegen"), S8("-o"), output, source_path};
                    CompilerDriverInvocation invocation = compiler_driver_parse_arguments(temporary.arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(command));
                    invocation.reject_machine_fallback = mode != 0;
                    CompilerDriverResult compiled = compiler_driver_execute_invocation(temporary.arena, invocation);
                    BUSTER_TEST_RAW(arguments, compiled.error == COMPILER_DRIVER_ERROR_NONE, compiled.diagnostic);
                    if (compiled.error == COMPILER_DRIVER_ERROR_NONE)
                    {
                        String8 run[] = {output};
                        ProcessSpawnResult child = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(run), (SliceString8){0}, (SliceString8){0},
                            (ProcessSpawnOptions){.use_process_environment = true});
                        if (BUSTER_REQUIRE(arguments, child.handle != 0))
                        {
                            ProcessWaitResult execution = os_process_wait_deadline(temporary.arena, child, 30000000);
                            BUSTER_TEST_RAW(arguments, !execution.timed_out && execution.result == PROCESS_RESULT_SUCCESS,
                                string_format(temporary.arena, S8("comma-condition {S8} {S8} {S8}: status={u32} timed_out={u32}"),
                                    modes[mode], frontends[form], optimizations[optimization], execution.platform_status, (u32)execution.timed_out));
                        }
                    }
                    scratch_end(temporary);
                }
            }
        }
    }
#endif
    return result;
}

'''

HELPER = r'''// A controlling expression still obeys comma precedence. Only commas outside
// delimiter groups and the middle operand of ?: belong to its root. The full
// expression machine already owns their ordered evaluation and result type.
BUSTER_C_INTERNAL bool c_ir_condition_has_root_comma(CIntegerIrBuilder* builder, u32 start, u32 end)
{
    bool found = false;
    while (start < end && c_token_is_punctuator(&builder->preprocess.tokens[start], C_PUNCTUATOR_LEFT_PARENTHESIS) &&
           !c_ir_group_is_statement_expression(builder, start, end) &&
           c_ir_matching_delimiter_cached(builder, start, end, C_PUNCTUATOR_LEFT_PARENTHESIS, C_PUNCTUATOR_RIGHT_PARENTHESIS) == end - 1)
    {
        start += 1;
        end -= 1;
    }
    u32 conditional_depth = 0;
    for (u32 index = start; index < end; index += 1)
    {
        CToken token = builder->preprocess.tokens[index];
        CIrGroupScan scan = c_ir_scan_delimiter_group(builder, &index, end);
        if (scan == C_IR_GROUP_SCAN_UNCLOSED)
        {
            break;
        }
        if (scan == C_IR_GROUP_SCAN_SKIPPED)
        {
            continue;
        }
        if (c_token_is_punctuator(&token, C_PUNCTUATOR_QUESTION))
        {
            conditional_depth += 1;
        }
        else if (conditional_depth && c_token_is_punctuator(&token, C_PUNCTUATOR_COLON))
        {
            conditional_depth -= 1;
        }
        else if (!conditional_depth && c_token_is_punctuator(&token, C_PUNCTUATOR_COMMA))
        {
            found = true;
            break;
        }
    }
    return found;
}

'''
DISPATCH = r'''        if (c_ir_condition_has_root_comma(builder, task.start, task.end))
        {
            // Split the sequence before ?: or &&/||. In particular, a false
            // left operand cannot suppress the final comma operand, and a
            // deferred right-hand call must not enter the arithmetic core.
            frame->as.condition.leaf_true_block = task.true_block;
            frame->as.condition.leaf_false_block = task.false_block;
            frame->stage = (u8)C_IR_LOWER_STAGE_CONDITION_CHILD;
            if (!c_ir_lower_frame_push(builder, (CIrLowerFrame){
                                                    .kind = C_IR_LOWER_FRAME_EXPRESSION,
                                                    .as.expression = {.start = task.start, .end = task.end},
                                                }))
            {
                c_ir_lower_frame_finish(builder, false, IR_VALUE_ID_INVALID);
            }
            return;
        }
'''

def replace_once(text, old, new):
    if text.count(old) != 1:
        raise RuntimeError('non-unique or stale patch anchor: ' + old[:100])
    return text.replace(old, new, 1)

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('stage', choices=['tests', 'fix'])
    parser.add_argument('root', type=pathlib.Path)
    parser.add_argument('evidence', type=pathlib.Path)
    args = parser.parse_args()
    args.evidence.mkdir(parents=True, exist_ok=True)
    source = make_source()
    (args.evidence / 'family.c').write_text(source)
    if args.stage == 'tests':
        import json
        path = args.root / 'src/buster/tests/compiler/frontend/c/c_test.c'
        if hashlib.sha256(path.read_bytes()).hexdigest() != '3ca1bee2a6b019a853c2e4f9ae2a846637d82aa3b0a14e333d611db3b44c0f7c':
            raise RuntimeError('test source is not the pinned baseline')
        literals = '\n'.join('        ' + json.dumps(line) for line in source.splitlines(keepends=True))
        fixture = FIXTURE_TEMPLATE.replace('@SOURCE@', literals)
        text = replace_once(path.read_text(), 'UnitTestResult c_frontend_tests(UnitTestArguments* arguments)\n', fixture + 'UnitTestResult c_frontend_tests(UnitTestArguments* arguments)\n')
        text = replace_once(text, '    BUSTER_TEST_FIXTURE(arguments, c_test_conditional_comma_assignment);', '    BUSTER_TEST_FIXTURE(arguments, c_test_conditional_comma_assignment);\n    BUSTER_TEST_FIXTURE(arguments, c_test_comma_condition_evaluation);')
        path.write_text(text)
    else:
        path = args.root / 'src/buster/lib/compiler/frontend/c/c_gen.c'
        if hashlib.sha256(path.read_bytes()).hexdigest() != '864e146cae683832846b00d26a44bd918fa3350b114e879175c34a54ce4e8fae':
            raise RuntimeError('production source is not the pinned baseline')
        text = replace_once(path.read_text(), 'BUSTER_C_INTERNAL void c_ir_lower_condition_step(CIntegerIrBuilder* builder)\n', HELPER + 'BUSTER_C_INTERNAL void c_ir_lower_condition_step(CIntegerIrBuilder* builder)\n')
        anchor = '        u32 conditional_start = 0;\n        u32 conditional_question = 0;\n        u32 conditional_colon = 0;\n        u32 conditional_end = 0;\n        if (c_ir_root_conditional(builder, task.start, task.end,'
        text = replace_once(text, anchor, DISPATCH + anchor)
        path.write_text(text)

if __name__ == '__main__':
    main()
