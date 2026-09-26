#!/usr/bin/env python3
"""Hosted-only causal probe; no timing measurements, source dependencies, or defaults changed."""
import hashlib
import json
import os
from pathlib import Path
import resource
import shutil
import subprocess
import sys

PIN = 'ade6ac4b6ecb21f30b61b656439bac476c145e2f'
TREE = '4c5306221fdb22fccc929b55e333163742de17d0'
REL = 'src/buster/lib/compiler/frontend/c/c_gen.c'
OLD = '''    // The declaration's attribute is the general answer -- glibc marks exit,
    // abort and longjmp with it -- and the three names remain because a
    // platform's headers may declare the assertion helpers without one.
    bool noreturn = signature.is_noreturn || string_equal(target->name, S8("abort")) || string_equal(target->name, S8("__assert_fail")) ||
                    string_equal(target->name, S8("__assert_perror_fail"));'''
NEW = '''    // Experimental control: consume the resolved entity's declaration effect.
    // Spelling alone does not establish the behavior of a bound function.
    bool noreturn = signature.is_noreturn;'''
BASE = 'static volatile unsigned seen;\nstatic void abort(void) { seen = 41u; }\n'
CASES = {
    'direct': BASE + 'int main(void) { abort(); return seen != 41u; }\n',
    'renamed': BASE.replace('abort', 'ordinary') + 'int main(void) { ordinary(); return seen != 41u; }\n',
    'parenthesized': BASE + 'int main(void) { (abort)(); return seen != 41u; }\n',
    'macro': BASE + '#define invoke abort\nint main(void) { invoke(); return seen != 41u; }\n',
    'indirect': BASE + 'int main(void) { void (*p)(void) = abort; p(); return seen != 41u; }\n',
    'shadow': BASE.replace('abort', 'ordinary') + 'int main(void) { void (*abort)(void) = ordinary; abort(); return seen != 41u; }\n',
    'conditional': BASE + 'int main(void) { if ((abort(), 1)) return seen != 41u; return 1; }\n',
    'lifetime': BASE + 'int main(void) { unsigned a[3] = {3, 5, 7}; unsigned *p = a; abort(); return seen != 41u || p[0]+p[1]+p[2] != 15u; }\n',
    'value': 'static int abort(int x) { return x + 1; }\nint main(void) { int x = abort(40); return x != 41; }\n',
    'asm_name': 'static volatile unsigned seen;\nstatic void ordinary(void) __asm__("abort");\nstatic void ordinary(void) { seen=41u; }\nint main(void) { ordinary(); return seen != 41u; }\n',
    'attribute': '_Noreturn void _Exit(int);\n__attribute__((noreturn)) void stop(int x) { _Exit(x); }\nint main(void) { stop(0); }\n',
    'specifier': '_Noreturn void _Exit(int);\n_Noreturn void stop(int x) { _Exit(x); }\nint main(void) { stop(0); }\n',
    'redecl': '_Noreturn void _Exit(int);\nvoid stop(int);\n__attribute__((noreturn)) void stop(int x) { _Exit(x); }\nint main(void) { stop(0); }\n',
    'real_abort': 'void abort(void);\nint main(void) { abort(); return 0; }\n',
}
INVALID = {
    'arity': 'static void abort(int x) { (void)x; }\nint main(void) { abort(); return 0; }\n',
    'conflicting': 'static void abort(int);\nstatic int abort(int x) { return x; }\nint main(void) { return 0; }\n',
    'syntax': 'static void abort(void) { return }\nint main(void) { abort(); return 0; }\n',
}
FLAGS = ['-std=gnu17', '-fno-builtin', '-fwrapv', '-fno-strict-aliasing', '-funsigned-char', '-g0', '-O0']
if os.environ.get('GITHUB_ACTIONS') != 'true' or os.environ.get('CI_ENABLED') != 'true':
    raise SystemExit('Refusing execution outside enabled GitHub-hosted correctness job')
resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
root = Path(os.environ['GITHUB_WORKSPACE']).resolve()
out = (root / 'evidence').resolve()
out.mkdir(exist_ok=True)
inputs = out / 'inputs'
inputs.mkdir(exist_ok=True)
for name, source in (CASES | INVALID).items():
    (inputs / (name + '.c')).write_text(source)
records = []

def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()

def run(name, argv, cwd=root, timeout=120):
    directory = out / name
    directory.mkdir(parents=True, exist_ok=True)
    argv = [str(a) for a in argv]
    record = {'name': name, 'argv': argv, 'cwd': str(cwd)}
    with (directory / 'stdout').open('wb') as stdout, (directory / 'stderr').open('wb') as stderr:
        try:
            record['status'] = subprocess.run(argv, cwd=cwd, stdout=stdout, stderr=stderr, timeout=timeout, check=False).returncode
        except subprocess.TimeoutExpired:
            record['status'] = 'timeout'
        except OSError as error:
            record['status'] = 'launch-error'
            record['error'] = str(error)
    (directory / 'command.json').write_text(json.dumps(record, indent=2))
    records.append(record)
    (out / 'commands.json').write_text(json.dumps(records, indent=2))
    print(name, record['status'], flush=True)
    return record['status']

def require(name, argv, cwd=root, timeout=120):
    if run(name, argv, cwd, timeout) != 0:
        raise RuntimeError('Setup failed: ' + name)

def capture(argv, cwd=root):
    return subprocess.check_output(argv, cwd=cwd, text=True).strip()

def selfhost(label, work, ide):
    flags = ['cc', '-Isrc', '-Ibuild/generated', '-DBUSTER_UNITY_BUILD=1', '-DBUSTER_INCLUDE_TESTS=0', '-g', '-v']
    stages = [work / 'build/ide-self', work / 'build/ide-self-stage2']
    first = run(label + '/selfhost-1', [ide] + flags + ['src/buster/apps/ide/ide.c', '-lm', '-o', stages[0]], work, 600)
    result = {'stage1_status': first, 'benchmark': 'not run: correctness-only experiment'}
    if first == 0:
        second = run(label + '/selfhost-2', [stages[0]] + flags + ['src/buster/apps/ide/ide.c', '-lm', '-o', stages[1]], work, 600)
        result['stage2_status'] = second
        if second == 0:
            result.update({'stage1_sha256': digest(stages[0]), 'stage2_sha256': digest(stages[1]), 'equal': digest(stages[0]) == digest(stages[1])})
    (out / label / 'selfhost.json').write_text(json.dumps(result, indent=2))

for tool in ['clang', 'gcc', 'cmake', 'ninja']:
    run('environment/' + tool, [tool, '--version'])
run('environment/uname', ['uname', '-a'])
(out / 'pin.json').write_text(json.dumps({'main': PIN, 'tree': TREE, 'probe_commit': capture(['git', 'rev-parse', 'HEAD']), 'probe_tree': capture(['git', 'rev-parse', 'HEAD^{tree}'])}, indent=2))
results = []
for label in ['baseline', 'candidate']:
    work = Path(os.environ['RUNNER_TEMP']) / ('false-necessity-' + label)
    require(label + '/worktree', ['git', 'worktree', 'add', '--detach', work, PIN])
    assert capture(['git', 'rev-parse', 'HEAD^{tree}'], work) == TREE
    require(label + '/clean', ['git', 'diff', '--exit-code'], work)
    if label == 'candidate':
        source = (work / REL).read_text()
        assert source.count(OLD) == 1, 'Patch anchor changed'
        (work / REL).write_text(source.replace(OLD, NEW))
        require(label + '/diffcheck', ['git', 'diff', '--check'], work)
        (out / 'candidate.patch').write_text(capture(['git', 'diff', '--', REL], work) + '\n')
        require(label + '/stage', ['git', 'add', REL], work)
    (out / label / 'source.json').write_text(json.dumps({'commit_parent': PIN, 'tree': capture(['git', 'write-tree'], work), 'c_gen_sha256': digest(work / REL)}, indent=2))
    driver = Path(os.environ['RUNNER_TEMP']) / ('false-necessity-build-' + label)
    require(label + '/bootstrap', ['clang', '-Isrc', '-Wall', '-Werror', '-Wno-unused-function', '-Wno-unused-variable', '-g', 'build.c', '-o', driver], work)
    require(label + '/generate', [driver, 'generate', '--cc', 'clang', '--ci', '--linker', 'DEFAULT', '-DBUSTER_UNITY_BUILD=OFF'], work)
    require(label + '/build', [driver, 'build', '--config', 'Release', '-t', 'ide'], work, 900)
    ide = work / 'build/Release/ide'
    (out / label / 'compiler.sha256').write_text(digest(ide) + '\n')
    for form in ['ssa', 'memory']:
        for allocator in ['none', 'mir-stack', 'fast', 'quality']:
            common = [ide, 'cc', '-target', 'x86_64-linux'] + FLAGS + ['-fverify-codegen', '-fregister-allocator=' + allocator, '-ffrontend-ssa' if form == 'ssa' else '-fno-frontend-ssa']
            if allocator != 'none':
                common += ['-fno-machine-fallback']
            for name in CASES:
                key = '/'.join([label, 'runtime', form, allocator, name])
                directory = out / key
                directory.mkdir(parents=True, exist_ok=True)
                exe = directory / 'program'
                status = run(key + '/compile', common + [inputs / (name + '.c'), '-o', exe], work)
                execution = run(key + '/execute', [exe], work, 10) if status == 0 else 'not-compiled'
                results.append({'label': label, 'form': form, 'allocator': allocator, 'case': name, 'compile': status, 'execute': execution, 'expected': -6 if name == 'real_abort' else 0})
                (out / 'runtime.json').write_text(json.dumps(results, indent=2))
    for name in INVALID:
        for mode in ['-fsyntax-only', '-c']:
            run(label + '/invalid/' + name + '/' + mode, [ide, 'cc'] + FLAGS + [mode, inputs / (name + '.c'), '-o', out / label / (name + '.o')], work)
    for target in ['x86_64-linux', 'aarch64-linux', 'x86_64-windows', 'aarch64-windows', 'x86_64-macos', 'aarch64-macos']:
        for form in ['ssa', 'memory']:
            for name in ['direct', 'indirect', 'attribute', 'real_abort']:
                key = '/'.join([label, 'targets', target, form, name])
                directory = out / key
                directory.mkdir(parents=True, exist_ok=True)
                obj = directory / 'output.o'
                common = [ide, 'cc', '-target', target] + FLAGS + ['-c', '-fverify-codegen', '-fregister-allocator=fast', '-fno-machine-fallback', '-ffrontend-ssa' if form == 'ssa' else '-fno-frontend-ssa', inputs / (name + '.c'), '-o', obj]
                status = run(key + '/compile', common, work)
                if status == 0:
                    original = digest(obj)
                    repeated = run(key + '/repeat', common, work)
                    (directory / 'determinism.json').write_text(json.dumps({'first_sha256': original, 'second_status': repeated, 'second_sha256': digest(obj) if obj.exists() else None}, indent=2))
    for name in ['direct', 'indirect', 'attribute', 'real_abort']:
        key = label + '/trace/' + name
        directory = out / key
        directory.mkdir(parents=True, exist_ok=True)
        run(key + '/compile', [ide, 'cc', '-target', 'x86_64-linux'] + FLAGS + ['-c', '-fverify-codegen', '-fregister-allocator=fast', '-fno-machine-fallback', '-fbootstrap-trace=' + str(directory / 'trace'), inputs / (name + '.c'), '-o', directory / 'out.o'], work)
    selfhost(label, work, ide)
    run(label + '/test-all', [ide, 'test', '--verbose=1', '--ci=1'], work, 600)
    run(label + '/post-source-diff', ['git', 'diff', '--exit-code'], work)
    run(label + '/post-source-status', ['git', 'status', '--porcelain'], work)
    (out / label / 'post-build-tree.txt').write_text(capture(['git', 'write-tree'], work) + '\n')
for compiler in ['clang', 'gcc']:
    for optimization in ['-O0', '-O2']:
        for name in CASES:
            key = '/'.join(['reference', compiler, optimization[1:], name])
            directory = out / key
            directory.mkdir(parents=True, exist_ok=True)
            exe = directory / 'program'
            status = run(key + '/compile', [compiler] + FLAGS[:-1] + [optimization, inputs / (name + '.c'), '-o', exe])
            if status == 0:
                run(key + '/execute', [exe], timeout=10)
    for name in INVALID:
        run('reference/' + compiler + '/invalid/' + name, [compiler] + FLAGS + ['-fsyntax-only', inputs / (name + '.c')])
for name in ['direct', 'renamed', 'indirect', 'shadow', 'conditional', 'lifetime', 'value']:
    key = 'reference/ubsan/' + name
    directory = out / key
    directory.mkdir(parents=True, exist_ok=True)
    exe = directory / 'program'
    status = run(key + '/compile', ['clang'] + FLAGS + ['-fsanitize=undefined', '-fno-sanitize-recover=all', inputs / (name + '.c'), '-o', exe])
    if status == 0:
        run(key + '/execute', [exe], timeout=10)
summary = {label: {'rows': len(rows := [r for r in results if r['label'] == label]), 'matching_expected': sum(r['compile'] == 0 and r['execute'] == r['expected'] for r in rows)} for label in ['baseline', 'candidate']}
(out / 'summary.json').write_text(json.dumps(summary, indent=2))
manifest = {str(p.relative_to(out)): digest(p) for p in sorted(out.rglob('*')) if p.is_file()}
(out / 'sha256.json').write_text(json.dumps(manifest, indent=2))
print(json.dumps(summary), flush=True)
# A green job is only collection completion. Individual failures remain in the complete ledger.
