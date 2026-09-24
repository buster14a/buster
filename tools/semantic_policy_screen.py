"""Disposable correctness screen; no performance measurements."""
import hashlib
import json
import pathlib
import subprocess
import sys

ide = str(pathlib.Path(sys.argv[1]).resolve())
out = pathlib.Path(sys.argv[2]).resolve()
out.mkdir(parents=True, exist_ok=True)
large = '9223372036854775808'
expected = '((__int128)9223372036854775808ULL)'
cases = {
    'literal_size': f'int main(void) {{ return sizeof({large}) != 16; }}\n',
    'literal_static': f'static __int128 x = {large}; int main(void) {{ return x != {expected}; }}\n',
    'literal_local': f'int main(void) {{ __int128 x = {large}; return x != {expected}; }}\n',
    'literal_return': f'__int128 f(void) {{ return {large}; }} int main(void) {{ return f() != {expected}; }}\n',
    'literal_enum': f'enum E {{ K = ({large} >> 63) }}; int main(void) {{ return K != 1; }}\n',
    'literal_designator': f'static int x[2] = {{ [({large} >> 63)] = 9 }}; int main(void) {{ return x[1] != 9; }}\n',
    'literal_conditional': f'static __int128 x = 1 ? {large} : 0; int main(void) {{ return x != {expected}; }}\n',
    'literal_explicit_control': f'static __int128 x = {expected}; int main(void) {{ return x != {expected}; }}\n',
    'char_known_control': 'int main(void) { char x = (char)255; return (unsigned)x != 255u; }\n',
    'counts_width': 'int main(void) { unsigned char x = 1; return __builtin_clz(x) != 31; }\n',
    'counts_truncate': 'int main(void) { unsigned long long x = 0x100000001ULL; return __builtin_popcount(x) != 1; }\n',
    'counts_return_size': 'int main(void) { unsigned long long x = 1; return sizeof(__builtin_clzll(x)) != sizeof(int); }\n',
    'counts_conditional': 'int main(void) { unsigned long long x = 1; return (1 ? -1 : __builtin_clzll(x)) >= 0; }\n',
    'counts_enum': 'enum E { K = __builtin_popcount(0x100000001ULL) }; int main(void) { return K != 1; }\n',
    'counts_static': 'static int x = __builtin_popcount(0x100000001ULL); int main(void) { return x != 1; }\n',
    'counts_control': 'int main(void) { unsigned int x = 1; return __builtin_clz(x) != 31 || __builtin_ctz(x) != 0 || __builtin_popcount(x) != 1; }\n',
}
rows = []
def run(argv):
    try:
        p = subprocess.run(argv, stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=180)
        return {'argv': argv, 'exit': p.returncode, 'stdout': p.stdout.decode(errors='replace'), 'stderr': p.stderr.decode(errors='replace')}
    except subprocess.TimeoutExpired:
        return {'argv': argv, 'exit': 'timeout'}

for name, source in cases.items():
    path = out / (name + '.c')
    path.write_text(source)
    for compiler in ['buster', 'clang', 'gcc']:
        executable = out / (name + '-' + compiler)
        argv = [ide, 'cc'] if compiler == 'buster' else [compiler]
        argv += ['-std=gnu11', '-funsigned-char', str(path), '-o', str(executable)]
        row = {'case': name, 'compiler': compiler, 'source_sha256': hashlib.sha256(source.encode()).hexdigest(), 'compile': run(argv)}
        if row['compile']['exit'] == 0:
            row['execute'] = run([str(executable)])
        rows.append(row)
        print(name, compiler, 'compile', row['compile']['exit'], 'execute', row.get('execute', {}).get('exit', 'not-run'), flush=True)
(out / 'screen.json').write_text(json.dumps(rows, indent=2))
