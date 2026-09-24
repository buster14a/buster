#!/usr/bin/env python3
"""Disposable bounded correctness experiment; no timings or performance verdicts.

C17 6.5.3.4p2: non-VLA sizeof has no operand effects; VLA sizeof evaluates
its operand. C17 6.5.1.1p3: generic control and unselected arms have no effects.
GNU choose_expr selects exactly one expression. GNU statement expressions are
used only with scalar results whose automatic lifetimes do not escape.

Required partial order: each active mark update precedes its return; completion
of the probe full expression precedes the final observations. No order is
required between distinct argument evaluations: every tested call has one
argument, and no observation shares a full expression with the tested update.
All bounds are positive. Pointer updates stay within a two-row live array.
No VLA bound whose effect on sizeof is optional is used. No uninitialized
array element is read. Adding a pure id call or scalar statement-expression
wrapper preserves values and effects here; it does not assert general C
expression substitutability for lvalues, arrays, or temporary lifetimes.
"""
import hashlib
import json
import pathlib
import shutil
import subprocess
import sys

root = pathlib.Path.cwd()
compiler = pathlib.Path(sys.argv[1]).resolve()
out = pathlib.Path(sys.argv[2]).resolve()
out.mkdir(parents=True, exist_ok=True)
records = []
common = ['-std=gnu17', '-fwrapv', '-fno-strict-aliasing', '-funsigned-char']
prelude = '''static volatile unsigned hits;
struct Pair { int x; int y; };
static int mark(void) { hits += 1; return 7; }
static int mark_zero(void) { hits += 1; return 0; }
static unsigned long id(unsigned long value) { return value; }
'''
forms = [
    ('sizeof_call', 'sizeof(mark())', 4, 0),
    ('sizeof_nested_call', 'sizeof(id(mark()))', 8, 0),
    ('sizeof_scalar_literal', 'sizeof((int){mark()})', 4, 0),
    ('sizeof_member_literal', 'sizeof(((struct Pair){mark(), 2}).x)', 4, 0),
    ('sizeof_array_literal', 'sizeof((int[2]){mark(), 2})', 8, 0),
    ('sizeof_statement', 'sizeof(({mark(); 7;}))', 4, 0),
    ('sizeof_call_statement', 'sizeof(id(({mark(); 7;})))', 8, 0),
    ('sizeof_update', 'sizeof(++hits)', 4, 0),
    ('generic_call', '_Generic(mark(), int: 7, default: mark())', 7, 0),
    ('generic_literal', '_Generic((int){mark()}, int: 7, default: mark())', 7, 0),
    ('generic_statement', '_Generic(({mark(); 7;}), int: 7, default: mark())', 7, 0),
    ('choose_true', '__builtin_choose_expr(1, 7, mark())', 7, 0),
    ('choose_false', '__builtin_choose_expr(0, mark(), 7)', 7, 0),
    ('alignof_statement', '__alignof__(({mark(); 7;}))', 4, 0),
    ('generic_sizeof_statement', '_Generic(0, int: sizeof(({mark(); 7;})), default: mark())', 4, 0),
    ('active_call', 'mark()', 7, 1),
    ('active_generic', '_Generic(0, int: mark(), default: 7)', 7, 1),
    ('active_choose', '__builtin_choose_expr(1, mark(), 7)', 7, 1),
    ('active_statement', '({mark(); 7;})', 7, 1),
    ('active_vla_bound', 'sizeof(int[mark()])', 28, 1),
]
cases = []
for name, expression, expected, effects in forms:
    for context in ['initializer', 'argument', 'return', 'condition', 'statement', 'nested_statement']:
        prefix = prelude
        if context == 'initializer':
            body = f'unsigned long value = ({expression});'
        elif context == 'argument':
            body = f'unsigned long value = id({expression});'
        elif context == 'return':
            prefix += f'static unsigned long probe(void) {{ return ({expression}); }}\n'
            body = 'unsigned long value = probe();'
        elif context == 'condition':
            body = f'unsigned long value = 0; if (({expression}) == {expected}ul) value = {expected}ul; else value = {expected + 1}ul;'
        elif context == 'statement':
            body = f'(void)({expression}); unsigned long value = {expected}ul;'
        else:
            body = f'unsigned long value = ({{ unsigned long inner = ({expression}); inner; }});'
        source = prefix + f'int main(void) {{ {body} return (value != {expected}ul) | ((hits != {effects}u) << 1); }}\n'
        cases.append((name + '_' + context, source, 'family'))

vla_operands = [
    ('plain', '*p', 0, 0),
    ('postfix', '*p++', 1, 0),
    ('prefix', '*(++p)', 1, 0),
    ('compound_assignment', '*(p += 1)', 1, 0),
    ('subscript_call', 'p[mark_zero()]', 0, 1),
    ('comma_call', '*(mark(), p)', 0, 1),
    ('conditional_update', '*(1 ? p++ : p)', 1, 0),
    ('statement_pointer', '*({ hits += 1; p; })', 0, 1),
]
for variable in [False, True]:
    declaration = 'int n = 3; int a[2][n]; int (*p)[n] = a;' if variable else 'int a[2][3]; int (*p)[3] = a;'
    for name, operand, offset, effects in vla_operands:
        expected_offset = offset if variable else 0
        expected_effects = effects if variable else 0
        source = prelude + f'int main(void) {{ {declaration} unsigned long value = sizeof({operand}); return (value != 12ul) | ((p != a + {expected_offset}) << 1) | ((hits != {expected_effects}u) << 2); }}\n'
        cases.append((('vla_' if variable else 'fixed_') + name, source, 'array-boundary'))
cases.append(('control_1040', '''static unsigned g;
static unsigned keep(unsigned value) { return value; }
int main(void) { keep((-(g = 2, keep(g = 9)), 4)); return g == 9 ? 0 : 1; }
''', 'known-control'))


def command(argv, log_name):
    try:
        process = subprocess.run(argv, cwd=root, capture_output=True, text=True, timeout=45)
        entry = {'argv': [str(x) for x in argv], 'returncode': process.returncode,
                 'stdout': process.stdout, 'stderr': process.stderr}
    except subprocess.TimeoutExpired as error:
        entry = {'argv': [str(x) for x in argv], 'returncode': 'timeout',
                 'stdout': str(error.stdout), 'stderr': str(error.stderr)}
    (out / (log_name + '.json')).write_text(json.dumps(entry, indent=2) + '\n')
    return entry


identity = {'source_commit': '2e942e80a87666409cf29d3e24a68d322b9e71fd',
            'source_tree': 'f976bfd7cdb8830a8a15d959ef36f0152f1b767d',
            'compiler_sha256': hashlib.sha256(compiler.read_bytes()).hexdigest(),
            'cases': len(cases), 'common_flags': common}
for executable in ['clang', 'gcc']:
    identity[executable] = command([executable, '--version'], executable + '-version')
identity['gcc_target'] = command(['gcc', '-dumpmachine'], 'gcc-target')
identity['clang_target'] = command(['clang', '-print-target-triple'], 'clang-target')
(out / 'identity.json').write_text(json.dumps(identity, indent=2) + '\n')
print('SEMANTIC_PLAN', json.dumps(identity), flush=True)
configs = [
    ('clang_O0', ['clang', '--target=x86_64-linux-gnu', '-O0']),
    ('clang_O2', ['clang', '--target=x86_64-linux-gnu', '-O2']),
    ('gcc_O0', ['gcc', '-m64', '-O0']),
    ('gcc_O2', ['gcc', '-m64', '-O2']),
    ('buster_ssa', [str(compiler), 'cc', '-target', 'x86_64-linux', '-fregister-allocator=fast', '-ffrontend-ssa']),
    ('buster_memory', [str(compiler), 'cc', '-target', 'x86_64-linux', '-fregister-allocator=fast', '-fno-frontend-ssa']),
]
for case_name, source, kind in cases:
    source_path = out / (case_name + '.c')
    source_path.write_text(source)
    case = {'name': case_name, 'kind': kind, 'sha256': hashlib.sha256(source.encode()).hexdigest(), 'results': {}}
    for config, prefix in configs:
        binary = out / (case_name + '-' + config)
        compiled = command(prefix + common + [str(source_path), '-o', str(binary)], case_name + '-' + config + '-compile')
        execution = None
        if compiled['returncode'] == 0:
            execution = command([str(binary)], case_name + '-' + config + '-run')['returncode']
        case['results'][config] = {'compile': compiled['returncode'], 'run': execution}
        if binary.exists():
            binary.unlink()
    refs = [case['results'][key] for key in ['clang_O0', 'clang_O2', 'gcc_O0', 'gcc_O2']]
    case['reference_validated'] = all(value == {'compile': 0, 'run': 0} for value in refs)
    case['buster_pass'] = all(case['results'][key] == {'compile': 0, 'run': 0} for key in ['buster_ssa', 'buster_memory'])
    records.append(case)
    print('SEMANTIC_CASE', json.dumps(case), flush=True)
    if case['reference_validated'] and not case['buster_pass']:
        print('SEMANTIC_DISCREPANCY_SOURCE', case_name, source, flush=True)
        trace = out / (case_name + '-trace')
        trace.mkdir(exist_ok=True)
        command([str(compiler), 'cc', '-target', 'x86_64-linux', '-fregister-allocator=fast', '-ffrontend-ssa',
                 '-fbootstrap-trace=' + str(trace)] + common + ['-c', str(source_path), '-o', str(trace / 'output.o')], case_name + '-trace')
    (out / 'results.json').write_text(json.dumps(records, indent=2) + '\n')
print('SEMANTIC_SUMMARY', json.dumps({'total': len(records),
    'reference_validated': sum(item['reference_validated'] for item in records),
    'buster_pass': sum(item['buster_pass'] for item in records),
    'discrepancies': [item['name'] for item in records if item['reference_validated'] and not item['buster_pass']]}), flush=True)
