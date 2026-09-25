#!/usr/bin/env python3
"""Hosted, bounded semantic confirmation; only stdlib and installed compilers.

Produces C witnesses and preserves every source, argv, diagnostic, binary and
raw return status. Does not time workloads or reinterpret failed attempts.
"""
import hashlib
import json
import pathlib
import subprocess
import sys

compiler = pathlib.Path(sys.argv[1]).resolve()
out = pathlib.Path(sys.argv[2]).resolve()
out.mkdir(parents=True, exist_ok=True)
common = ['-std=gnu17', '-fwrapv', '-fno-strict-aliasing', '-funsigned-char', '-g0']
prefix = '''extern int printf(const char *, ...);
static volatile int input;
static volatile int hits;
static int mark(void) { hits += 1; return input; }
'''
cases = {}
def add(name, body, expected=0, hit_count=0, extra='', group='logical-eligibility'):
    cases[name] = {'group': group, 'source': prefix + extra + '\nint main(void) {\n'
        + 'int actual = 0;\n' + body + '\n'
        + f'printf("actual=%d expected=%d hits=%d expected_hits=%d\\n", actual, {expected}, hits, {hit_count});\n'
        + f'return (actual != ({expected})) | ((hits != ({hit_count})) << 1);\n}}\n'}
for name, expr in [
    ('call_and', 'mark() && 0'), ('call_or', 'mark() || 1'),
    ('volatile_and', 'input && 0'), ('volatile_or', 'input || 1'),
    ('nested_logic', '(mark() && 0) || 1'),
    ('arithmetic_consumer', '(mark() && 0) + 9'),
    ('cast_consumer', '(unsigned char)(mark() || 1)'),
    ('comma_left', '(mark(), 7) && 0')]:
    add(name, f'actual = __builtin_constant_p({expr});')
add('local_volatile', 'volatile int local = 0; actual = __builtin_constant_p(local && 0);')
add('indirect_call', 'int (*fn)(void) = mark; actual = __builtin_constant_p(fn() && 0);')
add('predicate_return', 'actual = query();', extra='static int query(void) { return __builtin_constant_p(mark() && 0); }\n')
add('macro_fallback', 'actual = PICK(mark() && 0);', 1, 1,
    '#define PICK(e) (__builtin_constant_p(e) ? 0 : ((void)(e), 1))\n')
add('choose_expr_fallback', 'actual = __builtin_choose_expr(__builtin_constant_p(mark() && 0), 0, ((void)(mark() && 0), 1));', 1, 1)
for name, expr in [('volatile_conditional', 'input ? 7 : 9'), ('volatile_not', '!input'),
                   ('volatile_double_not', '!!input')]:
    add(name, f'actual = __builtin_constant_p({expr});', group='truth-normalization')
add('conditional_const_zero', 'actual = folded;', 4,
    extra='static const int zero = 0;\nstatic int folded = zero ? 3 : 4;\n', group='truth-normalization')
add('conditional_const_nonzero', 'actual = folded;', 3,
    extra='static const int one = 1;\nstatic int folded = one ? 3 : 4;\n', group='truth-control')
add('not_const_zero', 'actual = folded;', 1,
    extra='static const int zero = 0;\nstatic int folded = !zero;\n', group='truth-control')
add('runtime_const_zero', 'static const int zero = 0; actual = zero ? 3 : 4;', 4, group='truth-control')
add('runtime_volatile_conditional', 'actual = input ? 7 : 9;', 9, group='truth-control')
add('runtime_volatile_not', 'actual = !input;', 1, group='truth-control')
add('conditional_call_control', 'actual = __builtin_constant_p(mark() ? 7 : 9);', group='truth-control')
add('direct_call_control', 'actual = __builtin_constant_p(mark());', group='eligibility-control')
add('comma_control', 'actual = __builtin_constant_p((mark(), 7));', group='eligibility-control')
add('direct_volatile_control', 'actual = __builtin_constant_p(input);', group='eligibility-control')
add('short_circuit_and_control', 'actual = __builtin_constant_p(0 && mark());', 1, group='eligibility-control')
add('short_circuit_or_control', 'actual = __builtin_constant_p(1 || mark());', 1, group='eligibility-control')
add('runtime_call_and_control', 'actual = mark() && 0;', 0, 1, group='eligibility-control')
add('runtime_call_or_control', 'actual = mark() || 1;', 1, 1, group='eligibility-control')
add('literal_conditional_control', 'actual = folded;', 4,
    extra='static int folded = 0 ? 3 : 4;\n', group='truth-control')
# Verbatim minima, separately compiled and executed, with no observer wrapper.
cases['minimal_predicate'] = {'group':'logical-eligibility', 'source':
    'static volatile int value;\nint main(void) { return __builtin_constant_p(value && 0); }\n'}
cases['minimal_static_conditional'] = {'group':'truth-normalization', 'source':
    'static const int zero = 0;\nstatic int folded = zero ? 3 : 4;\nint main(void) { return folded != 4; }\n'}
cases['minimal_not'] = {'group':'truth-normalization', 'source':
    'static volatile int value;\nint main(void) { return __builtin_constant_p(!value); }\n'}

sources = out/'sources'; sources.mkdir(exist_ok=True)
for name, case in cases.items():
    path = sources/(name+'.c'); path.write_text(case['source'])
    case['sha256'] = hashlib.sha256(path.read_bytes()).hexdigest()
(out/'cases.json').write_text(json.dumps(cases, indent=2)+'\n')
profiles = []
for cc, target in [('clang', ['--target=x86_64-linux-gnu']), ('gcc', ['-m64'])]:
    for mode in ['O0','O2','san']:
        flags = ['-O1','-fsanitize=address,undefined','-fno-sanitize-recover=all'] if mode=='san' else ['-'+mode]
        profiles.append((f'{cc}-{mode}', [cc, *target, *common, *flags]))
for allocator in ['none','mir-stack','fast','quality']:
    for frontend in ['ssa','memory']:
        for opt in ['O0','O2']:
            args=[str(compiler),'cc','-target','x86_64-linux',*common,'-'+opt,'-fverify-codegen',
                  '-fregister-allocator='+allocator, '-ffrontend-ssa' if frontend=='ssa' else '-fno-frontend-ssa']
            if allocator!='none': args.append('-fno-machine-fallback')
            profiles.append((f'buster-{allocator}-{frontend}-{opt}', args))

def invoke(argv, stem, limit):
    timed_out=False
    try:
        p=subprocess.run(argv,stdout=subprocess.PIPE,stderr=subprocess.PIPE,timeout=limit,check=False)
        code,stdout,stderr=p.returncode,p.stdout,p.stderr
    except subprocess.TimeoutExpired as exc:
        code,stdout,stderr,timed_out=None,exc.stdout or b'',exc.stderr or b'',True
    pathlib.Path(str(stem)+'.stdout').write_bytes(stdout)
    pathlib.Path(str(stem)+'.stderr').write_bytes(stderr)
    data={'argv':argv,'returncode':code,'timeout':timed_out}
    pathlib.Path(str(stem)+'.json').write_text(json.dumps(data,indent=2)+'\n')
    return data

rows=[]
for name,case in cases.items():
    source=sources/(name+'.c')
    for profile,args in profiles:
        d=out/'matrix'/name/profile; d.mkdir(parents=True)
        binary=d/'program'
        built=invoke([*args,str(source),'-o',str(binary)],d/'compile',120)
        ran=invoke([str(binary)],d/'run',15) if built['returncode']==0 else None
        row={'case':name,'group':case['group'],'profile':profile,'compile':built,'run':ran}
        rows.append(row)
        with (out/'results.jsonl').open('a') as f: f.write(json.dumps(row)+'\n')
        print(name,profile,built['returncode'],None if ran is None else ran['returncode'],flush=True)
(out/'summary.json').write_text(json.dumps(rows,indent=2)+'\n')
# Canonical-IR observation through existing bitcode export, decoded independently.
exports=[]
for name in ['minimal_predicate','minimal_static_conditional','minimal_not','macro_fallback',
             'choose_expr_fallback','conditional_call_control','runtime_call_and_control','comma_control']:
    for frontend in ['ssa','memory']:
        d=out/'ir'/name/frontend; d.mkdir(parents=True)
        bc=d/'module.bc'; ll=d/'module.ll'
        args=[str(compiler),'cc','-target','x86_64-linux',*common,'-O0','-fverify-codegen',
              '-ffrontend-ssa' if frontend=='ssa' else '-fno-frontend-ssa','-emit-llvm',str(sources/(name+'.c')),'-o',str(bc)]
        built=invoke(args,d/'export',120)
        decoded=invoke(['clang','-S','-emit-llvm','-x','ir',str(bc),'-o',str(ll)],d/'decode',120) if built['returncode']==0 else None
        exports.append({'case':name,'frontend':frontend,'export':built,'decode':decoded})
(out/'exports.json').write_text(json.dumps(exports,indent=2)+'\n')
# This is an observation run: mismatches deliberately leave the job red.
failures=sum(r['compile']['returncode']!=0 or r['run']['returncode']!=0 for r in rows)
print('rows',len(rows),'nonpassing',failures,flush=True)
sys.exit(1 if failures else 0)
