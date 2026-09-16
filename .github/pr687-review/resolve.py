from pathlib import Path
import json
import re
import subprocess
import tempfile

HEAD = '3be1d020ca33e9c52bc0afcf198c7ba4904c0c9c'
MAIN = '939041b3d6b4b5c773318a5b494a28a4d9b829f1'
EXPECTED = 'a0459b827c05cf288727c99a2563897eb79bfd36'
def git(*args):
    return subprocess.check_output(['git', *args], text=True).strip()
assert git('rev-parse', 'HEAD') == HEAD
assert git('merge-base', HEAD, MAIN) == 'e68e4f62eb67ec8e432fe46277bfe6f9e0e472cf'
files = git('diff', '--name-only', '--diff-filter=U').splitlines()
assert len(files) == 10, files
pattern = re.compile(r'^<<<<<<< HEAD\n(.*?)^=======\n(.*?)^>>>>>>>[^\n]*\n', re.M | re.S)
report = []
for name in files:
    p = Path(name)
    index = [0]
    def resolve(m):
        index[0] += 1
        ours, theirs = m.group(1), m.group(2)
        if name.endswith('/c_gen.c') and index[0] in (4, 5, 6, 7, 8):
            selected = theirs
            reason = 'Preserve main live-limb and pointer rational API; avoid duplicate half literal branch'
        elif name.endswith('/c_test.c'):
            assert index[0] == 1
            selected = ours + theirs
            reason = 'Retain bfloat16 and live-limb regression registrations'
        else:
            selected = ours
            reason = 'Retain BF16 additions over already-landed Float16 predecessor'
        report.append({'file': name, 'conflict': index[0], 'reason': reason})
        return selected
    p.write_text(pattern.sub(resolve, p.read_text()))
assert len(report) == 42, len(report)
p = Path('src/buster/tests/compiler/frontend/c/c_test.c')
s = p.read_text()
marker = '// `_Float16`: the type the LLVM 18 FP16 resource headers declare, and the\n'
starts = [m.start() for m in re.finditer(re.escape(marker), s)]
assert len(starts) == 2
end = s.index('BUSTER_GLOBAL_LOCAL UnitTestResult c_test_bfloat16_type', starts[1])
s = s[:starts[1]] + s[end:]
a = s.index('BUSTER_GLOBAL_LOCAL UnitTestResult c_test_bfloat16_type')
b = s.index('BUSTER_GLOBAL_LOCAL UnitTestResult c_test_float_integer_constants', a)
with tempfile.TemporaryDirectory() as directory:
    function = Path(directory) / 'function.c'
    function.write_text(s[a:b])
    subprocess.run(['patch', '--fuzz=0', str(function), str(Path(__file__).with_name('numeric.patch'))], check=True)
    s = s[:a] + function.read_text() + s[b:]
p.write_text(s)
subprocess.run(['git', 'add', '-A'], check=True)
assert not git('ls-files', '-u')
subprocess.run(['git', 'diff', '--cached', '--check'], check=True)
actual = git('write-tree')
assert actual == EXPECTED, (actual, EXPECTED)
print(json.dumps({'head': HEAD, 'main': MAIN, 'tree': actual, 'resolutions': report}, indent=2))
