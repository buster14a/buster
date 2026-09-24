"""Temporary evidence harness; native execution and foreign object checks stay distinct."""
import ast
import hashlib
import json
import pathlib
import re
import subprocess
import sys

ide = str(pathlib.Path(sys.argv[1]).resolve())
out = pathlib.Path(sys.argv[2]).resolve()
out.mkdir(parents=True, exist_ok=True)
text = pathlib.Path('src/buster/tests/compiler/frontend/c/c_test.c').read_text()
fragment = text.split('String8 const c_test_integer_literal_policy_source = S8(', 1)[1].split('\n);', 1)[0]
source = ''.join(ast.literal_eval(s) for s in re.findall(r'"(?:[^"\\]|\\.)*"', fragment))
# Reference compilers have different raw-decimal extension policies. Pin the
# selected signed __int128 type explicitly in both references; preserve all
# unsigned suffixes, explicit casts and operators. This is equivalent-artifact
# evidence, not a claim that their default raw-literal policies match Buster.
def reference_profile(s):
    return re.sub(r'\b(9223372036854775808|18446744073709551615)(?:LL|L)?(?![A-Za-z_0-9])',
                  lambda m: '((__int128)' + m.group(1) + 'ULL)', s)

raw = out / 'literal-policy-raw.c'
normalized = out / 'literal-policy-reference-profile.c'
raw.write_text(source)
normalized.write_text(reference_profile(source))
rows = []

def run(args):
    p = subprocess.run(args, capture_output=True, text=True, timeout=180)
    return {'args': args, 'exit': p.returncode, 'stdout': p.stdout, 'stderr': p.stderr}

for compiler in ['buster', 'gcc', 'clang']:
    for dialect in ['gnu17', 'gnu2x']:
        for optimization in ['-O0', '-O2'] if compiler != 'buster' else ['-ffrontend-ssa', '-fno-frontend-ssa']:
            path = raw if compiler == 'buster' else normalized
            name = compiler + '-' + dialect + '-' + optimization[1:]
            artifact = out / name
            flags = [ide, 'cc'] if compiler == 'buster' else [compiler]
            selected_dialect = 'gnu23' if compiler == 'buster' and dialect == 'gnu2x' else dialect
            flags += ['-std=' + selected_dialect, '-funsigned-char', optimization, str(path), '-o', str(artifact)]
            row = {'kind': 'native execution', 'name': name, 'source_sha256': hashlib.sha256(path.read_bytes()).hexdigest(), 'compile': run(flags)}
            if row['compile']['exit'] == 0:
                row['execute'] = run([str(artifact)])
            rows.append(row)
            print(name, row['compile']['exit'], row.get('execute', {}).get('exit', 'not-run'), flush=True)

layout = '''_Static_assert(sizeof(long) == __SIZEOF_LONG__, "long macro");
_Static_assert(sizeof(void *) == __SIZEOF_POINTER__, "pointer macro");
_Static_assert(sizeof(__int128) == __SIZEOF_INT128__, "128 macro");
_Static_assert(sizeof(2147483648L) == 8, "long candidate");
_Static_assert(sizeof(9223372036854775808) == 16, "wide literal");
__int128 target_literal = 18446744073709551615;
long target_long;
void *target_pointer;
int literal_size(void) { return sizeof(9223372036854775808) != 16; }
'''
raw_layout = out / 'layout-raw.c'
clang_layout = out / 'layout-reference-profile.c'
raw_layout.write_text(layout)
clang_layout.write_text(reference_profile(layout))
for target, clang_target in [('x86_64-linux', 'x86_64-unknown-linux-gnu'), ('aarch64-linux', 'aarch64-unknown-linux-gnu'),
                             ('x86_64-windows', 'x86_64-pc-windows-msvc'), ('aarch64-windows', 'aarch64-pc-windows-msvc'),
                             ('x86_64-macos', 'x86_64-apple-macos11'), ('aarch64-macos', 'arm64-apple-macos11')]:
    for compiler in ['buster', 'clang']:
        artifact = out / (target + '-' + compiler + '.o')
        flags = [ide, 'cc', '--target=' + target] if compiler == 'buster' else ['clang', '--target=' + clang_target]
        path = raw_layout if compiler == 'buster' else clang_layout
        flags += ['-std=gnu17', '-nostdinc', '-ffreestanding', '-c', str(path), '-o', str(artifact)]
        row = {'kind': 'target semantic/layout and object emission ONLY', 'name': target + '-' + compiler, 'compile': run(flags)}
        if row['compile']['exit'] == 0:
            row['object_sha256'] = hashlib.sha256(artifact.read_bytes()).hexdigest()
            row['object_header'] = run(['file', str(artifact)])
        rows.append(row)
        print(row['name'], row['compile']['exit'], 'not executed', flush=True)

(out / 'literal-policy-results.json').write_text(json.dumps(rows, indent=2))
failed = [r for r in rows if r['compile']['exit'] != 0 or ('execute' in r and r['execute']['exit'] != 0)]
print('FAILURES', len(failed), flush=True)
sys.exit(bool(failed))
