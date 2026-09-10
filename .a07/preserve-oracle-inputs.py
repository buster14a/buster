"""One-off audit repair and independent pre-publication census controls."""
from pathlib import Path
import os, subprocess, sys

if sys.argv[1] == '--preflight':
    out = Path(os.environ['RUNNER_TEMP']) / 'a07-preflight'
    out.mkdir(exist_ok=True)
    cases = [
        ('stream-valid', 'vmovntdqa zmm0, zmmword ptr [rax]', True, 'intel'),
        ('stream-invalid', 'vmovntdqa zmm0, dword ptr [rax]', False, 'intel'),
        ('ambiguous-intel', 'vcvtpd2ps xmm0{k1}, [rax+64]', False, 'intel'),
        ('ambiguous-att', 'vcvtpd2ps 64(%rax), %xmm0{%k1}', False, 'att'),
    ]
    for name, instruction, valid, dialect in cases:
        source = out / (name + '.s')
        source.write_text(('.intel_syntax noprefix\n' if dialect == 'intel' else '') + '.text\n' + instruction + '\n')
        for tool in ['gas', 'clang']:
            obj = out / (name + '.' + tool + '.o')
            command = ['as', '--64', str(source), '-o', str(obj)] if tool == 'gas' else ['clang', '--target=x86_64-unknown-linux-gnu', '-c', str(source), '-o', str(obj)]
            print('+', ' '.join(command), flush=True)
            result = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
            print(result.stdout, end='', flush=True)
            if (result.returncode == 0) != valid:
                raise SystemExit(f'{name}/{tool}: independent acceptance disagreement')
            if valid:
                before = obj.read_bytes()
                binary = out / (name + '.' + tool + '.bin')
                subprocess.run(['objcopy', '--dump-section', f'.text={binary}', str(obj), str(out / (obj.name + '.copy'))], check=True)
                assert obj.read_bytes() == before
                assert binary.read_bytes() == bytes.fromhex('62 f2 7d 48 2a 00')
                print(name, tool, binary.read_bytes().hex(' '), flush=True)
else:
    p = Path(sys.argv[1])
    s = p.read_text()
    old = "    command = [str(arg) for arg in args]\n    print('+', ' '.join(command), flush=True)"
    new = """    command = [str(arg) for arg in args]
    # objcopy without an outfile destructively rewrites its input, including
    # empty symbol tables. Preserve the actual original Buster object for
    # the independent full-object repeat comparison and later linking.
    observed = Path(command[-1]) if command[0] == 'objcopy' else None
    original = observed.read_bytes() if observed is not None else None
    if observed is not None:
        command.append(str(out / ('objcopy-' + observed.name)))
    print('+', ' '.join(command), flush=True)"""
    assert s.count(old) == 1
    s = s.replace(old, new)
    old = '    return completed.stdout'
    new = """    if observed is not None and observed.read_bytes() != original:
        raise RuntimeError(f'oracle extraction changed input {observed}')
    return completed.stdout"""
    assert s.count(old) == 1
    s = s.replace(old, new)
    old = 'for dialect, lines in sources.items():'
    new = """sources['intel'].append('vmovntdqa zmm0, zmmword ptr [rax]')
expected['intel'].extend(bytes.fromhex('62 f2 7d 48 2a 00'))

for dialect, lines in sources.items():"""
    assert s.count(old) == 1
    s = s.replace(old, new)
    old = 'negative = [\n'
    assert s.count(old) == 1
    s = s.replace(old, old + "    'vmovntdqa zmm0, dword ptr [rax]',\n")
    old = "asm = out / 'interop.s'"
    new = """feature_source = out / 'feature-disabled.s'
feature_source.write_text('.text\\nvcvtps2pd 64(%rax),%zmm0{%k1}\\n')
run([ide,'cc','-c','-march=x86-64',feature_source,'-o',out/'feature-disabled.o'], reject=True)
results['feature_disabled_rejected'] = True

asm = out / 'interop.s'"""
    assert s.count(old) == 1
    s = s.replace(old, new)
    # Preserve reached-stage evidence even if a later independent check fails.
    old = "sources = {'att': [], 'intel': []}"
    new = """import atexit
atexit.register(lambda: (out / 'partial-results.json').write_text(json.dumps(results, indent=2) + '\\n'))

sources = {'att': [], 'intel': []}"""
    assert s.count(old) == 1
    p.write_text(s.replace(old, new))
