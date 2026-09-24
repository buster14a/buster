"""Task-local semantic experiment; no random generation or performance claims."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys

out = Path(os.environ['RUNNER_TEMP']) / 'compound-evidence'
out.mkdir(exist_ok=True)
# name, object type, initial value, operator, RHS type, RHS, exact expected value.
rows = [
    ('int_mul_fraction', 'int', '5', '*', 'double', '1.5', '7'),
    ('int_div_fraction', 'int', '7', '/', 'double', '2.5', '2'),
    ('int_add_fraction', 'int', '-2', '+', 'double', '1.5', '0'),
    ('int_sub_fraction', 'int', '2', '-', 'double', '1.5', '0'),
    ('byte_div_wide', 'unsigned char', '200', '/', 'int', '256', '0'),
    ('byte_mod_wide', 'unsigned char', '200', '%', 'int', '257', '200'),
    ('uint_div_wide', 'unsigned int', '12', '/', 'unsigned long long', '0x100000002ULL', '0'),
    ('signed_div_unsigned', 'int', '-9', '/', 'unsigned int', '2u', '2147483643'),
    ('bool_sub_integer', '_Bool', '1', '-', 'int', '2', '1'),
    ('float_sub_double', 'float', '1.0f', '-', 'double', '0x1.000001p0', '-0x1p-24'),
    ('same_type_control', 'int', '5', '*', 'int', '2', '10'),
    ('narrow_result_control', 'unsigned char', '254', '+', 'int', '2', '0'),
    ('double_control', 'double', '5.0', '*', 'double', '1.5', '7.5'),
]
contexts = ['initializer', 'argument', 'return', 'condition', 'statement', 'nested_statement']
forms = ['direct', 'place', 'expanded']
manifest = []
for row, typ, initial, op, rtyp, rhs, expected in rows:
    for context in contexts:
        for form in forms:
            setup = ''
            if form == 'direct':
                expr = '(value ' + op + '= right())'
            elif form == 'place':
                expr = '(*place() ' + op + '= right())'
            else:
                # Valid only for these non-atomic scalars: RHS touches disjoint
                # counters, not value; pointer formation and RHS commute.
                setup = 'volatile ' + typ + '* p = place(); '
                expr = '(*p = *p ' + op + ' right())'
            if context == 'initializer': body = 'double result = ' + expr + '; return result;'
            elif context == 'argument': body = 'return identity(' + expr + ');'
            elif context == 'return': body = 'return ' + expr + ';'
            elif context == 'condition': body = 'double result = -999; if (' + expr + ' == (' + expected + ')) result = (' + expected + '); return result;'
            elif context == 'statement': body = expr + '; return 0;'
            else: body = 'return ({ double result = ' + expr + '; result; });'
            observed = '0' if context == 'statement' else expected
            source = ('static volatile ' + typ + ' value;\n'
                'static volatile ' + rtyp + ' rhs_value;\n'
                'static volatile unsigned lhs_hits;\n'
                'static volatile unsigned rhs_hits;\n'
                'static volatile ' + typ + '* place(void) { lhs_hits += 1; return &value; }\n'
                'static ' + rtyp + ' right(void) { rhs_hits += 1; return rhs_value; }\n'
                'static double identity(double x) { return x; }\n'
                'static double probe(void) { ' + setup + body + ' }\n'
                'int main(void) { value = ' + initial + '; rhs_value = ' + rhs + '; '
                'double result = probe(); '
                'return (result != (' + observed + ')) | ((value != (' + expected + ')) << 1) '
                '| ((lhs_hits != ' + ('0' if form == 'direct' else '1') + 'u) << 2) '
                '| ((rhs_hits != 1u) << 3); }\n')
            name = row + '_' + context + '_' + form
            (out / (name + '.c')).write_text(source)
            manifest.append(dict(name=name, row=row, context=context, form=form, expected=expected,
                                 sha256=hashlib.sha256(source.encode()).hexdigest()))
(out / 'cases.json').write_text(json.dumps(manifest, indent=2))
common = ['-std=gnu17', '-fwrapv', '-fno-strict-aliasing', '-funsigned-char']
configs = [
    ('clang_O0', ['clang', '--target=x86_64-linux-gnu', '-O0']),
    ('clang_O2', ['clang', '--target=x86_64-linux-gnu', '-O2']),
    ('gcc_O0', ['gcc', '-m64', '-O0']),
    ('gcc_O2', ['gcc', '-m64', '-O2']),
    ('clang_sanitize', ['clang', '--target=x86_64-linux-gnu', '-O1', '-fsanitize=address,undefined', '-fno-sanitize-recover=all']),
    ('gcc_sanitize', ['gcc', '-m64', '-O1', '-fsanitize=address,undefined', '-fno-sanitize-recover=all']),
]
for mode in ['ssa', 'memory']:
    configs.append(('buster_' + mode, ['build/Release/ide', 'cc', '-target', 'x86_64-linux', '-O0', '-g0', '-fregister-allocator=fast', '-fverify-codegen', '-ffrontend-ssa' if mode == 'ssa' else '-fno-frontend-ssa']))

def run(argv, stem):
    try:
        p = subprocess.run(argv, stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=45,
            env=dict(os.environ, ASAN_OPTIONS='halt_on_error=1:detect_leaks=1', UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1'))
        result = dict(argv=argv, returncode=p.returncode, stdout=p.stdout.decode(errors='replace'), stderr=p.stderr.decode(errors='replace'))
    except subprocess.TimeoutExpired as exc:
        result = dict(argv=argv, returncode=None, timeout=True, stdout=(exc.stdout or b'').decode(errors='replace'), stderr=(exc.stderr or b'').decode(errors='replace'))
    (out / (stem + '.json')).write_text(json.dumps(result, indent=2))
    return result['returncode']

summary = []
for case in manifest:
    for config, compiler in configs:
        stem = case['name'] + '-' + config
        executable = out / (stem + '.exe')
        compiled = run(compiler + common + [str(out / (case['name'] + '.c')), '-o', str(executable)], stem + '-compile')
        executed = run([str(executable)], stem + '-run') if compiled == 0 else None
        summary.append(dict(case=case['name'], config=config, compile=compiled, run=executed))
        executable.unlink(missing_ok=True)
(out / 'summary.json').write_text(json.dumps(summary, indent=2))
counts = {}
for config, unused in configs:
    selected = [r for r in summary if r['config'] == config]
    counts[config] = dict(total=len(selected), passed=sum(r['compile'] == 0 and r['run'] == 0 for r in selected), compile_failed=sum(r['compile'] != 0 for r in selected))
print(json.dumps(counts, indent=2), flush=True)
(out / 'counts.json').write_text(json.dumps(counts, indent=2))
# Baseline Buster discrepancies are the object of the experiment, not passes.
# Fail on any invalid independent-reference expectation.
sys.exit(any(r['compile'] != 0 or r['run'] != 0 for r in summary if not r['config'].startswith('buster_')))
