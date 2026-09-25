#!/usr/bin/env python3
"""Hosted-only differential/census experiment. Does not measure performance."""
from pathlib import Path
import argparse, difflib, hashlib, json, os, shutil, signal, subprocess, sys

PIN = 'd9e736e2cf08804a2c603d153e9fb608f18c5c49'
TREE = 'ae9f274b6768b023fe6e0ef80a161e14b290e0a2'
CFILE = 'src/buster/lib/compiler/frontend/c/c_source.c'


def digest(path):
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        for b in iter(lambda: f.read(1 << 20), b''):
            h.update(b)
    return h.hexdigest()


def instrument(path, variant):
    original = path.read_text()
    text = original
    include = '#include <buster/lib/compiler/frontend/c/c_source_metrics_internal.h>'
    assert text.count(include) == 1
    text = text.replace(include, include + '\n#include <stdio.h> // Disposable census observer only.')
    anchor = '    CPreprocessSourceFrame* source_frame = &root_frame;'
    assert text.count(anchor) == 1
    text = text.replace(anchor, '    u64 rep_rows = 0, rep_lines = 0, rep_max = 0;\n' + anchor)
    anchor = '            if (expansion_ok)\n            {\n                c_preprocess_process_expanded_line('
    assert text.count(anchor) == 1
    text = text.replace(anchor, '            if (expansion_ok)\n            {\n                rep_rows += line_output_count;\n                rep_lines += 1;\n                rep_max = BUSTER_MAX(rep_max, line_output_count);\n                c_preprocess_process_expanded_line(')
    end = text.index('\nBUSTER_C_SHARED bool c_parse_auto_type_word')
    tail = text.rfind('    return result;\n}', 0, end)
    assert tail > text.index('CPreprocessResult c_preprocess(')
    if variant == 'flat':
        tail = text.rfind('    if (root_output_arena)\n    {\n        // Do not retain', 0, end)
        assert tail >= 0
        reserve = '(root_output_arena ? root_output_arena->reserved_size : 0)'
        committed = '(root_output_arena ? root_output_arena->os_position : 0)'
    else:
        reserve = committed = '0'
    report = '''    if (getenv("BUSTER_REP_CENSUS"))
    {
        fprintf(stderr, "ROOT_OUTPUT variant=%s rows=%llu lines=%llu max_line=%llu final_tokens=%llu node_size=%llu token_size=%llu root_reserved=%llu root_committed=%llu tu_cursor=%llu\\n",
                "@VARIANT@", (unsigned long long)rep_rows, (unsigned long long)rep_lines,
                (unsigned long long)rep_max, (unsigned long long)output_count,
                (unsigned long long)sizeof(CPreprocessTokenNode), (unsigned long long)sizeof(CPpToken),
                (unsigned long long)@RESERVE@, (unsigned long long)@COMMITTED@, (unsigned long long)arena->position);
    }
'''.replace('@VARIANT@', variant).replace('@RESERVE@', reserve).replace('@COMMITTED@', committed)
    text = text[:tail] + report + text[tail:]
    path.write_text(text)
    return ''.join(difflib.unified_diff(original.splitlines(True), text.splitlines(True), fromfile='a/' + CFILE, tofile='b/' + CFILE))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--work', type=Path, required=True)
    ap.add_argument('--frozen', type=Path, required=True)
    ap.add_argument('--out', type=Path, required=True)
    ap.add_argument('--prototype', type=Path, required=True)
    ap.add_argument('--build-driver', type=Path, required=True)
    ap.add_argument('--sanitize', action='store_true')
    a = ap.parse_args()
    work, frozen, out = a.work.resolve(), a.frozen.resolve(), a.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    binaries = out.parent / (out.name + '-binaries')
    binaries.mkdir(exist_ok=True)
    logs = out / 'attempts'; logs.mkdir(exist_ok=True)
    inputs = out / 'inputs'; inputs.mkdir(exist_ok=True)
    records, checks = [], []
    env = os.environ.copy()
    env.pop('BUSTER_REP_CENSUS', None)
    env['BUSTER_TEST_JOBS'] = '2'
    env['CMAKE_BUILD_PARALLEL_LEVEL'] = '2'

    def check(name, condition, **data):
        checks.append(dict(name=name, passed=bool(condition), **data))
        (out / 'checks.json').write_text(json.dumps(checks, indent=2))
        print('CHECK', name, 'PASS' if condition else 'FAIL', flush=True)
        return bool(condition)

    def run(name, argv, cwd=work, extra=None, required=False, timeout=900):
        argv = list(map(str, argv)); base = logs / name
        e = dict(env); e.update(extra or {})
        with open(str(base) + '.stdout', 'wb') as stdout, open(str(base) + '.stderr', 'wb') as stderr:
            process = subprocess.Popen(argv, cwd=cwd, env=e, stdout=stdout, stderr=stderr, start_new_session=True)
            try:
                status = process.wait(timeout=timeout)
            except subprocess.TimeoutExpired:
                os.killpg(process.pid, signal.SIGKILL); process.wait(); status = 124
        record = dict(name=name, argv=argv, cwd=str(cwd), exit=status, timeout_seconds=timeout,
                      environment_overrides=extra or {}, stdout_sha256=digest(str(base)+'.stdout'), stderr_sha256=digest(str(base)+'.stderr'))
        records.append(record)
        with (out / 'attempts.jsonl').open('a') as f:
            f.write(json.dumps(record) + '\n')
        print('ATTEMPT', name, status, flush=True)
        if status:
            print(Path(str(base)+'.stderr').read_text(errors='replace')[-2500:], flush=True)
        if required and status:
            raise RuntimeError(name + ' failed; retained attempt is authoritative')
        return status

    def same(name, left, right):
        return check(name, Path(left).exists() and Path(right).exists() and digest(left) == digest(right),
                     left=str(left), right=str(right), left_sha256=digest(left) if Path(left).exists() else None,
                     right_sha256=digest(right) if Path(right).exists() else None)

    cases = []
    for n in (0,1,2,15,16,31,32,63,64,65,255,256,257,4095,4096,4097,65535,65536,65537):
        name = 'rows-' + str(n)
        text = '#define D(x) x,x\nint a[] = {0' + ',D(1)'*n + '};\nint main(void){return sizeof(a)/sizeof(a[0]) != ' + str(1+2*n) + ';}\n'
        (inputs / (name+'.c')).write_text(text)
        cases.append((name, n <= 4097, 0))
    extra_cases = {
        'plain': 'int main(void){return 0;}\n',
        'empty': '#define EMPTY(x)\nEMPTY(1)\nEMPTY(EMPTY(2))\nint main(void){return 0;}\n',
        'nested': '#define D(x) ((x)+(x))\n#define I(x) x\n#define RAW(x) #x\n#define BAD(x,y) x+y\n#define CAT(a,b) a##b\n#define SELF SELF\n#define S(x) RAW(x)\n#define V(x,...) (x+__VA_ARGS__)\nint CAT(ma,in)(void){char *a=RAW(BAD(1));char*b=S(SELF);return I(D(D(2)))!=8 || V(1,2)!=3 || a[0]!=66 || b[0]!=83;}\n',
        'pragma': '#define P(x) _Pragma(#x)\nP(pack(push,1)) struct A { char a; int b; }; P(pack(pop))\n#define SZ sizeof(struct A)\nint main(void){return SZ!=5;}\n',
        'provenance': '#define ID(x) x\n#line 72 "logical-root.c"\nint main(void){return ID(__LINE__)!=72;}\n',
        'failure': '#define ONE 1\nint good=ONE;\n#line 77 "logical-root-error.c"\n#error intentional-root-output-diagnostic\n',
        'repeated': '#define V(x) ((x)+1)\n'+''.join('int v'+str(i)+'=V('+str(i)+');\n' for i in range(2048))+'int main(void){return v2047!=2048;}\n',
        'literal': '#define BIG "'+'a'*65536+'"\nchar s[]=BIG;\nint main(void){return sizeof(s)!=65537;}\n',
    }
    for name, text in extra_cases.items():
        (inputs/(name+'.c')).write_text(text); cases.append((name, name != 'failure', 1 if name=='failure' else 0))
    (out / 'inputs.json').write_text(json.dumps({p.name:digest(p) for p in inputs.glob('*.c')}, indent=2))
    config = 'Debug' if a.sanitize else 'Release'
    build_dir = 'build/rep-sanitizer' if a.sanitize else 'build'
    driver = a.build_driver.resolve()
    def build_args(command, *args):
        return [driver, command, '--build-directory', build_dir, *args]
    run('source-pin', ['git','rev-parse','HEAD','HEAD^{tree}'], required=True)
    identity = (logs/'source-pin.stdout').read_text().splitlines()
    assert identity == [PIN, TREE], identity
    configure = build_args('generate','--config',config,'--cc','clang','--ci','--linker','DEFAULT','--no-fuzz')
    if a.sanitize: configure += ['--sanitize']
    run('configure', configure, required=True, timeout=900)
    if not a.sanitize:
        (frozen/'build').mkdir(exist_ok=True)
        shutil.copytree(work/'build/generated', frozen/'build/generated', dirs_exist_ok=True)
    baseline_source = (work/CFILE).read_text()
    for variant in ('base','flat'):
        (work/CFILE).write_text(baseline_source)
        if variant == 'flat':
            run('apply-prototype',[sys.executable,a.prototype,work,out],required=True)
        (out/(variant+'-clean-source.c')).write_text((work/CFILE).read_text())
        observer = instrument(work/CFILE, variant)
        (out/(variant+'-observer.patch')).write_text(observer)
        (out/(variant+'-observed-source.c')).write_text((work/CFILE).read_text())
        run(variant+'-diff-check',['git','diff','--check'],required=True)
        run(variant+'-source-blob',['git','hash-object',CFILE],required=True)
        run(variant+'-build', build_args('build','--config',config,'-t','ide'),required=True,timeout=1800)
        compiler = binaries/(variant+'-ide')
        shutil.copy2(work/build_dir/config/'ide',compiler)
        (out/(variant+'-binary.sha256')).write_text(digest(compiler)+'\n')
        if a.sanitize:
            check(variant+'-sanitizer-full-suite', run(variant+'-test-all',build_args('build','--config',config,'-t','test_all'),timeout=1800)==0)
            continue
        for name, execute, expected in cases:
            src = inputs/(name+'.c')
            status = run(variant+'-'+name+'-pp',[compiler,'cc','-E',src],extra={'BUSTER_REP_CENSUS':'1'})
            check(variant+'-'+name+'-pp-status',status==expected)
            if execute:
                obj = out/(name+'.o')
                obj.unlink(missing_ok=True)
                status = run(variant+'-'+name+'-object',[compiler,'cc','-g','-c',src,'-o',obj])
                check(variant+'-'+name+'-object-status', status==0)
                if status==0: shutil.copy2(obj,out/(variant+'-'+name+'.o'))
                exe = binaries/(name+'-program')
                status = run(variant+'-'+name+'-link',['clang',obj,'-o',exe]) if obj.exists() else 1
                check(variant+'-'+name+'-native',status==0 and run(variant+'-'+name+'-run',[exe])==0)
                if variant=='base':
                    oracle = binaries/(name+'-oracle')
                    status=run(name+'-clang',['clang','-std=gnu17',src,'-o',oracle])
                    check(name+'-clang-native',status==0 and run(name+'-clang-run',[oracle])==0)
        args=['cc','-Isrc','-Ibuild/generated','-DBUSTER_UNITY_BUILD=1','-DBUSTER_INCLUDE_TESTS=0','-g']
        obj=out/'unity.o'; obj.unlink(missing_ok=True)
        status=run(variant+'-frozen-unity',[compiler,*args,'-c','src/buster/apps/ide/ide.c','-o',obj],cwd=frozen,extra={'BUSTER_REP_CENSUS':'1'},timeout=1200)
        check(variant+'-frozen-unity-status',status==0)
        if status==0: shutil.copy2(obj,out/(variant+'-unity.o'))
        stage1,stage2=binaries/(variant+'-stage1'),binaries/(variant+'-stage2')
        s1=run(variant+'-self-stage1',[compiler,*args,'src/buster/apps/ide/ide.c','-lm','-o',stage1],timeout=1200)
        s2=run(variant+'-self-stage2',[stage1,*args,'src/buster/apps/ide/ide.c','-lm','-o',stage2],timeout=1200) if s1==0 else 1
        check(variant+'-self-status',s1==0 and s2==0)
        if s1==0 and s2==0: same(variant+'-fixed-point',stage1,stage2)
        check(variant+'-release-full-suite',run(variant+'-test-all',build_args('build','--config',config,'-t','test_all'),timeout=1800)==0)
        if variant=='flat':
            for name,execute,expected in cases:
                same(name+'-pp-exact',logs/('base-'+name+'-pp.stdout'),logs/('flat-'+name+'-pp.stdout'))
                def diagnostics(which):
                    return b''.join(line for line in (logs/(which+'-'+name+'-pp.stderr')).read_bytes().splitlines(keepends=True) if not line.startswith(b'ROOT_OUTPUT '))
                check(name+'-diagnostics-exact',diagnostics('base')==diagnostics('flat'))
                if execute: same(name+'-object-exact',out/('base-'+name+'.o'),out/('flat-'+name+'.o'))
            same('frozen-unity-object-exact',out/'base-unity.o',out/'flat-unity.o')
    (work/CFILE).write_text(baseline_source)
    run('restore-source',['git','diff','--exit-code'],required=True)
    rows=[]
    for p in logs.glob('*.stderr'):
        for line in p.read_text(errors='replace').splitlines():
            if line.startswith('ROOT_OUTPUT '): rows.append(dict(attempt=p.stem,record=line))
    (out/'census.json').write_text(json.dumps(rows,indent=2))
    summary=dict(source=PIN,tree=TREE,checks=len(checks),failures=[c['name'] for c in checks if not c['passed']],census_records=len(rows),performance='NOT MEASURED',instrumentation='invocation-local counters; reporting enabled only for named census launches')
    (out/'summary.json').write_text(json.dumps(summary,indent=2))
    print(json.dumps(summary,indent=2),flush=True)
    for row in rows:
        if 'frozen-unity' in row['attempt']: print(json.dumps(row),flush=True)
    return 1 if summary['failures'] else 0

if __name__=='__main__':
    sys.exit(main())
