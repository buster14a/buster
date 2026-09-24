#!/usr/bin/env python3
"""Prepare two source-only commits; never merge or update another writer's ref."""
from pathlib import Path
import hashlib
import os
import subprocess

BASE = '744a948f41291b4238d1755b1507678799f1206d'
BRANCH = 'codex/choose-expr-evaluation-20260924-astra'
GEN = 'src/buster/lib/compiler/frontend/c/c_gen.c'
TEST = 'src/buster/tests/compiler/frontend/c/c_test.c'


def git(*args):
    return subprocess.check_output(['git', *args], text=True).strip()


def replace_once(text, old, new):
    assert text.count(old) == 1, (old[:100], text.count(old))
    return text.replace(old, new, 1)


fixture = Path('.github/choose-fixture.txt').read_text()
fixture = replace_once(fixture,
    '                        BUSTER_TEST_RAW(arguments, c_test_ir_direct_call_count(lowered.program, function, S8("mark")) == cases[probe / CONTEXT_COUNT].calls, name);\n'
    '                        BUSTER_TEST_RAW(arguments, c_test_ir_direct_call_count(lowered.program, function, S8("other")) == 0, name);\n'
    '                        BUSTER_TEST_RAW(arguments, stores == cases[probe / CONTEXT_COUNT].stores, name);',
    '                        u32 calls = c_test_ir_direct_call_count(lowered.program, function, S8("mark"));\n'
    '                        u32 discarded = c_test_ir_direct_call_count(lowered.program, function, S8("other"));\n'
    '                        String8 observation = string_format(temporary.arena,\n'
    '                            S8("{S8}: mark={u32}/{u32} other={u32}/0 volatile-stores={u32}/{u32}"),\n'
    '                            name, calls, cases[probe / CONTEXT_COUNT].calls, discarded, stores, cases[probe / CONTEXT_COUNT].stores);\n'
    '                        BUSTER_TEST_RAW(arguments, calls == cases[probe / CONTEXT_COUNT].calls, observation);\n'
    '                        BUSTER_TEST_RAW(arguments, discarded == 0, observation);\n'
    '                        BUSTER_TEST_RAW(arguments, stores == cases[probe / CONTEXT_COUNT].stores, observation);')
assert git('ls-remote', 'origin', 'refs/heads/' + BRANCH).split()[0] == BASE
subprocess.run(['git', 'checkout', '--detach', BASE], check=True)
assert git('hash-object', GEN) == '60bf69ed970fc43d84fa3f7198aac5d1410b0eb9'
assert git('hash-object', TEST) == '23dfa416dd92cfb1fb50c61496be275c1eedaeb1'
text = Path(TEST).read_text()
text = replace_once(text, 'UnitTestResult c_frontend_tests(UnitTestArguments* arguments)\n', fixture + 'UnitTestResult c_frontend_tests(UnitTestArguments* arguments)\n')
text = replace_once(text, '    BUSTER_TEST_FIXTURE(arguments, c_test_unevaluated_call_arity_diagnostics);',
    '    BUSTER_TEST_FIXTURE(arguments, c_test_unevaluated_call_arity_diagnostics);\n    BUSTER_TEST_FIXTURE(arguments, c_test_choose_expr_evaluation);')
Path(TEST).write_text(text)
subprocess.run(['git', 'diff', '--check'], check=True)
subprocess.run(['git', 'add', TEST], check=True)
assert git('diff', '--cached', '--name-only') == TEST
subprocess.run(['git', 'commit', '-m', 'test: cover selected and discarded choose-expression effects (#1097)'], check=True)
test_sha = git('rev-parse', 'HEAD')
text = Path(GEN).read_text()
old = '''        if (index + 1 < frame->as.prepare_control.end &&
            c_token_is_punctuator(&builder->preprocess.tokens[index + 1], C_PUNCTUATOR_LEFT_PARENTHESIS) &&
            c_ir_token_builtin_kind(builder, builder->preprocess.tokens[index]) == C_SYMBOL_BUILTIN_GENERIC)
        {
            u32 close = c_ir_matching_delimiter_cached(builder, index + 1, frame->as.prepare_control.end, C_PUNCTUATOR_LEFT_PARENTHESIS,
                                                       C_PUNCTUATOR_RIGHT_PARENTHESIS);
            if (close == UINT32_MAX)
            {
                builder->failure_token_index = index;
                builder->failure_message = S8("unterminated _Generic selection");
                c_ir_lower_frame_finish(builder, false, IR_VALUE_ID_INVALID);
                return;
            }
            frame->as.prepare_control.index = close + 1;
            continue;
        }
'''
new = '''        if (index + 1 < frame->as.prepare_control.end &&
            c_token_is_punctuator(&builder->preprocess.tokens[index + 1], C_PUNCTUATOR_LEFT_PARENTHESIS))
        {
            CSymbolBuiltin builtin = c_ir_token_builtin_kind(builder, builder->preprocess.tokens[index]);
            if (builtin == C_SYMBOL_BUILTIN_GENERIC || builtin == C_SYMBOL_BUILTIN_CHOOSE_EXPR)
            {
                // The selection owns preparation of its chosen expression.
                // Preparing a group here would execute a discarded arm.
                u32 close = c_ir_matching_delimiter_cached(builder, index + 1, frame->as.prepare_control.end, C_PUNCTUATOR_LEFT_PARENTHESIS,
                                                           C_PUNCTUATOR_RIGHT_PARENTHESIS);
                if (close == UINT32_MAX)
                {
                    builder->failure_token_index = index;
                    builder->failure_message = builtin == C_SYMBOL_BUILTIN_GENERIC ? S8("unterminated _Generic selection") :
                                                                                     S8("unterminated __builtin_choose_expr selection");
                    c_ir_lower_frame_finish(builder, false, IR_VALUE_ID_INVALID);
                    return;
                }
                frame->as.prepare_control.index = close + 1;
                continue;
            }
        }
'''
text = replace_once(text, old, new)
text = replace_once(text, '            .deferred_calls = builtin_generic,', '            .deferred_calls = builtin_generic || builtin_choose_expr,')
text = replace_once(text,
    '        // _Generic likewise owns the lowering of its selected expression.\n',
    '        // _Generic and __builtin_choose_expr own their selected expression.\n'
    '        // Deferred preparation lets that expression prepare its own calls.\n')
text = replace_once(text, '        if (builtin_generic || builtin_object_size || builtin_constant_p)\n',
    '        if (builtin_generic || builtin_choose_expr || builtin_object_size || builtin_constant_p)\n')
Path(GEN).write_text(text)
subprocess.run(['git', 'diff', '--check'], check=True)
subprocess.run(['git', 'add', GEN], check=True)
assert git('diff', '--cached', '--name-only') == GEN
subprocess.run(['git', 'commit', '-m', 'c: defer choose-expression preparation to the selected arm (#1097)'], check=True)
head = git('rev-parse', 'HEAD')
tree = git('rev-parse', 'HEAD^{tree}')
assert set(git('diff', '--name-only', BASE, head).splitlines()) == {GEN, TEST}
assert git('ls-remote', 'origin', 'refs/heads/' + BRANCH).split()[0] == BASE
subprocess.run(['git', 'push', 'origin', 'HEAD:refs/heads/' + BRANCH], check=True)
with open(os.environ['GITHUB_OUTPUT'], 'a') as stream:
    stream.write(f'head={head}\ntest_head={test_sha}\ntree={tree}\n')
print('CHOOSE_SOURCE_IDENTITIES', BASE, test_sha, head, tree, flush=True)
