from pathlib import Path
import os
import subprocess

SOURCE = '6d5ccfb16df8791b1d454c30e426e206e40e20ee'
TREE = '6c713c2fafbd23f27f701f37eeefbdfe08f53c79'
def git(*args):
    return subprocess.check_output(['git', *args], text=True).strip()
assert git('rev-parse', 'HEAD') == SOURCE
path = Path('src/buster/lib/compiler/frontend/c/c_gen.c')
text = path.read_text()
old = '        {S8("__builtin_huge_val"), S8("huge_val")},\n'
assert text.count(old) == 1
path.write_text(text.replace(old, '        {S8("__builtin_inf"), S8("huge_val")},\n' + old))
subprocess.run(['git', 'add', str(path)], check=True)
subprocess.run(['git', 'diff', '--cached', '--check'], check=True)
assert git('write-tree') == TREE
out = Path(os.environ['RUNNER_TEMP']) / 'pr687-evidence'
(out / 'repair.patch').write_bytes(subprocess.check_output(['git', 'diff', '--cached', SOURCE]))
print('Verified exact one-line repair of missing double infinity builtin mapping:', TREE)
