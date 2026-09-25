#!/usr/bin/env python3
"""Exploratory correctness only. No production edits or acceptance/performance claim.
The IDE is the hash-pinned exploratory compiler from hosted run 36183831330.
Every command and unsuccessful attempt is retained; independent consumers are used.
"""
import hashlib
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess
import sys

SOURCE = 'ade6ac4b6ecb21f30b61b656439bac476c145e2f'
TREE = '4c5306221fdb22fccc929b55e333163742de17d0'
COMPILER_SHA256 = '57cc0f0ec6876bf8d591ea5eddb663da6cfbb4dc84da00e47bad686eceb20e80'
ide = Path(sys.argv[1]).resolve()
out = Path(sys.argv[2]).resolve()
out.mkdir(parents=True, exist_ok=True)
if hashlib.sha256(ide.read_bytes()).hexdigest() != COMPILER_SHA256:
    raise SystemExit('compiler identity mismatch')
ide.chmod(0o755)
commands = []
results = []

def write(name, text):
    path = out / name
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding='utf-8')
    return path

def run(label, args):
    prefix = out / ('commands/%04d-%s' % (len(commands), label))
    prefix.parent.mkdir(exist_ok=True)
    argv = [str(x) for x in args]
    try:
        p = subprocess.run(argv, cwd=out, text=True, stdout=subprocess.PIPE,
                           stderr=subprocess.PIPE, timeout=60, check=False)
        record = dict(label=label, argv=argv, cwd=str(out), returncode=p.returncode,
                      stdout=p.stdout, stderr=p.stderr, timeout=False)
    except subprocess.TimeoutExpired as e:
        def text(x):
            return x.decode(errors='replace') if isinstance(x, bytes) else (x or '')
        record = dict(label=label, argv=argv, cwd=str(out), returncode=None,
                      stdout=text(e.stdout), stderr=text(e.stderr), timeout=True)
    except OSError as e:
        record = dict(label=label, argv=argv, cwd=str(out), returncode=None,
                      stdout='', stderr=str(e), timeout=False)
    prefix.with_suffix('.json').write_text(json.dumps(record, indent=2))
    prefix.with_suffix('.stdout').write_text(record['stdout'])
    prefix.with_suffix('.stderr').write_text(record['stderr'])
    commands.append(record)
    print(label, 'rc=', record['returncode'], record['stdout'][:350], flush=True)
    return record

def observe(name, stage, record, expected=None):
    ok = record['returncode'] == 0 and (expected is None or record['stdout'].strip() == expected)
    results.append(dict(name=name, stage=stage, passed=ok, command=record['label'],
                        expected_stdout=expected, actual_stdout=record['stdout'],
                        returncode=record['returncode'], timeout=record['timeout']))
    return ok

write('identity.json', json.dumps(dict(source=SOURCE, tree=TREE, compiler_sha256=COMPILER_SHA256,
    reused_from_run=36183831330, reused_artifact=10885452801,
    platform=platform.platform(), machine=platform.machine(),
    transport=os.environ.get('GITHUB_SHA'), run_id=os.environ.get('GITHUB_RUN_ID'),
    evidence_class='new hosted exploratory correctness; reused hash-pinned compiler, not a fresh build or acceptance run'), indent=2))
for tool in ['clang', 'gcc', 'node', 'readelf']:
    run('version-' + tool, [tool, '--version'])

sources = {
 'abi.c': '''extern int read_sc(signed char);\nextern int read_ss(short);\nextern int read_uc(unsigned char);\nextern int read_us(unsigned short);\nint probe_sc(void) { return read_sc(-1); }\nint probe_ss(void) { return read_ss(-1); }\nint probe_uc(void) { return read_uc(255); }\nint probe_us(void) { return read_us(65535); }\nint probe_indirect(int (*p)(signed char)) { return p(-1); }\n''',
 'abi_callee.c': '''int read_sc(signed char x) { return x; }\nint read_ss(short x) { return x; }\nint read_uc(unsigned char x) { return x; }\nint read_us(unsigned short x) { return x; }\n''',
 'abi_main.c': '''extern int printf(const char *, ...);\nextern int read_sc(signed char);\nextern int probe_sc(void), probe_ss(void), probe_uc(void), probe_us(void);\nextern int probe_indirect(int (*)(signed char));\nint main(void) { int a=probe_sc(), b=probe_ss(), c=probe_uc(), d=probe_us(), e=probe_indirect(read_sc); printf("sc=%d ss=%d uc=%d us=%d indirect=%d\\n",a,b,c,d,e); return a!=-1 || b!=-1 || c!=255 || d!=65535 || e!=-1; }\n''',
 'ctor.c': '''int initialized;\n__attribute__((constructor(101))) static void initialize(void) { initialized=37; }\nint probe(void) { return initialized; }\n''',
 'weak.c': '''__attribute__((weak)) int selected=11;\n__attribute__((weak)) int choose(void) { return 11; }\nint probe(void) { return selected+choose(); }\n''',
 'strong.c': '''int selected=29;\nint choose(void) { return 29; }\n''',
 'weak_import.c': '''extern int optional(void) __attribute__((weak));\nint probe(void) { return optional != 0; }\n''',
 'alias.c': '''int target(void) { return 37; }\nint other(void) __attribute__((alias("target")));\nint probe(void) { return other(); }\n''',
 'section.c': '''int payload __attribute__((section("buster_audit")))=37;\nint probe(void) { return payload; }\n''',
 'module_asm.c': '''__asm__(".data\\n.globl from_asm\\nfrom_asm:\\n.quad 37\\n");\nextern unsigned long long from_asm;\nint probe(void) { return from_asm == 37; }\n''',
 'wasm_pointer.c': '''int first(void) { return 7; }\nint probe(void) { int (*volatile p)(void)=first; return p != 0; }\n''',
 'wasm_alignment.c': '''unsigned char skew[1]={1};\nint probe(void) { _Alignas(64) unsigned char object[1]; object[0]=skew[0]; return (int)((unsigned long long)(void *)object & 63); }\n''',
 'wasm_dynamic_alignment.c': '''int leaf(void) { volatile double x=1.0; return (int)((unsigned long long)(void *)&x & 7); }\nint probe(void) { volatile unsigned char *p=__builtin_alloca(1); p[0]=3; int bad=leaf(); return bad+(p[0]!=3)*256; }\n''',
}
for name, text in sources.items():
    write('sources/' + name, text)

base = [str(ide), 'cc', '-target', 'x86_64-linux', '-std=gnu17', '-g0', '-fwrapv', '-fno-strict-aliasing', '-funsigned-char']
ref_flags = ['-std=gnu17', '-fwrapv', '-fno-strict-aliasing', '-funsigned-char', '-fno-pie']
expected_abi = 'sc=-1 ss=-1 uc=255 us=65535 indirect=-1'
for compiler in ['clang', 'gcc']:
    run('abi-callee-' + compiler, [compiler, *ref_flags, '-O2', '-c', 'sources/abi_callee.c', '-o', 'abi-callee-' + compiler + '.o'])
run('abi-main', ['clang', *ref_flags, '-O2', '-c', 'sources/abi_main.c', '-o', 'abi-main.o'])
run('abi-reference-caller', ['clang', *ref_flags, '-O2', '-c', 'sources/abi.c', '-o', 'abi-reference.o'])
run('abi-reference-link', ['clang', '-no-pie', 'abi-reference.o', 'abi-callee-clang.o', 'abi-main.o', '-o', 'abi-reference'])
observe('abi-reference', 'execute', run('abi-reference-run', [out/'abi-reference']), expected_abi)

for mode, flag in [('ssa','-ffrontend-ssa'), ('memory','-fno-frontend-ssa')]:
    stem = 'abi-' + mode
    compiled = run(stem+'-export', [*base, flag, '-emit-llvm', 'sources/abi.c', '-o', stem+'.bc'])
    if not observe(stem, 'export', compiled):
        continue
    run(stem+'-decode', ['clang', '-S', '-emit-llvm', stem+'.bc', '-o', stem+'.ll'])
    for opt in ['0', '2']:
        obj = stem+'-O'+opt+'.o'
        assembled = run(stem+'-consume-O'+opt, ['clang', '-O'+opt, '-c', stem+'.bc', '-o', obj])
        if not observe(stem+'-O'+opt, 'consume', assembled):
            continue
        run(stem+'-assembly-O'+opt, ['clang', '-O'+opt, '-S', stem+'.bc', '-o', stem+'-O'+opt+'.s'])
        for callee in ['clang', 'gcc']:
            exe = stem+'-O'+opt+'-'+callee
            linked = run(exe+'-link', ['clang', '-no-pie', obj, 'abi-callee-'+callee+'.o', 'abi-main.o', '-o', exe])
            if observe(exe, 'link', linked):
                observe(exe, 'execute', run(exe+'-run', [out/exe]), expected_abi)
    native = run(stem+'-native', [*base, flag, '-fregister-allocator=fast', '-c', 'sources/abi.c', '-o', stem+'-native.o'])
    if observe(stem+'-native', 'compile', native):
        linked = run(stem+'-native-link', ['clang','-no-pie',stem+'-native.o','abi-callee-clang.o','abi-main.o','-o',stem+'-native'])
        if observe(stem+'-native', 'link', linked):
            observe(stem+'-native', 'execute', run(stem+'-native-run', [out/(stem+'-native')]), expected_abi)

# Each module-metadata witness has its own TU; one failure cannot mask another.
for name, expected in [('ctor',37), ('weak',58), ('weak_import',0), ('alias',37), ('section',37), ('module_asm',1)]:
    main = 'sources/'+name+'_main.c'
    write(main, 'extern int printf(const char *, ...); extern int probe(void);\nint main(void) { int got=probe(); printf("value=%d\\n",got); return got!='+str(expected)+'; }\n')
    extra = ['sources/strong.c'] if name == 'weak' else []
    for kind in ['reference','native','llvm-ssa','llvm-memory']:
        stem = name+'-'+kind
        if kind == 'reference':
            compiled = run(stem+'-compile', ['clang', *ref_flags, '-O2', '-c', 'sources/'+name+'.c', '-o', stem+'.o'])
        elif kind == 'native':
            compiled = run(stem+'-compile', [*base, '-fregister-allocator=fast', '-c', 'sources/'+name+'.c', '-o', stem+'.o'])
        else:
            flag = '-fno-frontend-ssa' if kind == 'llvm-memory' else '-ffrontend-ssa'
            exported = run(stem+'-export', [*base, flag, '-emit-llvm', 'sources/'+name+'.c', '-o', stem+'.bc'])
            if not observe(stem, 'export', exported):
                continue
            run(stem+'-decode', ['clang', '-S', '-emit-llvm', stem+'.bc', '-o', stem+'.ll'])
            compiled = run(stem+'-consume', ['clang','-O2','-c',stem+'.bc','-o',stem+'.o'])
        if not observe(stem, 'object', compiled):
            continue
        run(stem+'-symbols', ['readelf','-Ws',stem+'.o'])
        sections = run(stem+'-sections', ['readelf','-WS',stem+'.o'])
        if name == 'section':
            results.append(dict(name=stem, stage='section-preserved', passed='buster_audit' in sections['stdout'], command=sections['label']))
        linked = run(stem+'-link', ['clang','-no-pie','-O2',stem+'.o',main,*extra,'-o',stem])
        if observe(stem, 'link', linked):
            observe(stem, 'execute', run(stem+'-run', [out/stem]), 'value='+str(expected))

write('wasm-run.js', '''const fs=require('fs');\nconst m=new WebAssembly.Module(fs.readFileSync(process.argv[2]));\nconst i=new WebAssembly.Instance(m,{});\nconst a=i.exports.probe(), b=i.exports.probe();\nconsole.log('first='+String(a)+' second='+String(b));\nprocess.exit((a===Number(process.argv[3]) && b===Number(process.argv[3]))?0:1);\n''')
for name, expected in [('wasm_pointer',1),('wasm_alignment',0),('wasm_dynamic_alignment',0)]:
    for bits in [32,64]:
        for mode, flag in [('ssa','-ffrontend-ssa'),('memory','-fno-frontend-ssa')]:
            stem=name+'-'+str(bits)+'-'+mode
            compiled=run(stem+'-compile',[ide,'cc','-target','wasm'+str(bits)+'-unknown-unknown','-std=gnu17','-g0',flag,'sources/'+name+'.c','-o',stem+'.wasm'])
            if observe(stem,'compile',compiled):
                args=['node']+(['--experimental-wasm-memory64'] if bits==64 else [])+['wasm-run.js',stem+'.wasm',str(expected)]
                observe(stem,'execute',run(stem+'-run',args),'first='+str(expected)+' second='+str(expected))

write('commands.json',json.dumps(commands,indent=2))
write('results.json',json.dumps(results,indent=2))
failures=[r for r in results if not r['passed']]
write('summary.json',json.dumps(dict(checks=len(results),failed_checks=len(failures),failures=failures,
    note='Failed checks are observations, not a count of independent bugs. Existing Wasm alignment report #1332 is intentionally tested, not re-filed.'),indent=2))
lines=['# Hosted exploratory semantic audit','',f'Source `{SOURCE}`, tree `{TREE}`.',
       f'Compiler SHA-256 `{COMPILER_SHA256}` reused from run 36183831330.',
       '',f'{len(results)} checks; {len(failures)} failed checks. This is not a defect count, acceptance run or performance result.','',
       '| Witness | Stage | Result |','|---|---|---|']
for row in results:
    lines.append('| '+row['name']+' | '+row['stage']+' | '+('PASS' if row['passed'] else 'FAIL')+' |')
write('summary.md','\n'.join(lines)+'\n')
manifest=[]
for path in sorted(out.rglob('*')):
    if path.is_file() and path.name!='SHA256SUMS':
        manifest.append(hashlib.sha256(path.read_bytes()).hexdigest()+'  '+path.relative_to(out).as_posix())
write('SHA256SUMS','\n'.join(manifest)+'\n')
print(json.dumps(dict(checks=len(results),failed_checks=len(failures))))
raise SystemExit(1 if failures else 0)
