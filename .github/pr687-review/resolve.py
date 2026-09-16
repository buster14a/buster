from pathlib import Path
import json
import os
import subprocess
import tempfile

HEAD = '3be1d020ca33e9c52bc0afcf198c7ba4904c0c9c'
MAIN = '939041b3d6b4b5c773318a5b494a28a4d9b829f1'
EXPECTED = 'ea98242479b74fc8cff07b908797e364b36a22a9'
def git(*args):
    return subprocess.check_output(['git', *args], text=True).strip()
assert git('rev-parse', 'HEAD') == HEAD
name = 'src/buster/tests/compiler/frontend/c/c_test.c'
files = git('diff', '--name-only', '--diff-filter=U').splitlines()
assert files == [name], files
# Preserve main's entire fixture file and insert only the BF16 fixture and
# registration; the virtual merge base handles the landed predecessor.
main = subprocess.check_output(['git', 'show', MAIN + ':' + name], text=True)
head = subprocess.check_output(['git', 'show', HEAD + ':' + name], text=True)
start = 'BUSTER_GLOBAL_LOCAL UnitTestResult c_test_bfloat16_type'
end = 'BUSTER_GLOBAL_LOCAL UnitTestResult c_test_float_integer_constants'
a = head.index(start)
b = head.index(end, a)
assert start not in main
with tempfile.TemporaryDirectory() as directory:
    function = Path(directory) / 'function.c'
    function.write_text(head[a:b])
    subprocess.run(['patch', '--fuzz=0', str(function), str(Path(__file__).with_name('numeric.patch'))], check=True)
    at = main.index(end)
    main = main[:at] + function.read_text() + main[at:]
registration = '    BUSTER_TEST_FIXTURE(arguments, c_test_float16_type);\n'
assert main.count(registration) == 1
main = main.replace(registration, registration + '    BUSTER_TEST_FIXTURE(arguments, c_test_bfloat16_type);\n')
Path(name).write_text(main)
subprocess.run(['git', 'add', '-A'], check=True)
assert not git('ls-files', '-u')
subprocess.run(['git', 'diff', '--cached', '--check'], check=True)
actual = git('write-tree')
evidence = Path(os.environ['RUNNER_TEMP']) / 'pr687-evidence'
# Preserve the complete exact candidate delta for independent review before
# the source PR can be advanced; this workflow only writes a temporary branch.
(evidence / 'candidate.patch').write_bytes(subprocess.check_output(['git', 'diff', '--cached', '--binary', MAIN]))
(evidence / 'candidate-objects.txt').write_text(git('diff', '--cached', '--raw', '--no-abbrev', MAIN) + '\n')
print(json.dumps({'head': HEAD, 'main': MAIN, 'tree': actual, 'expected_tree': EXPECTED, 'resolution': 'Preserve main fixtures and insert expanded BF16 fixture; exact delta retained for independent review'}, indent=2))
assert actual == EXPECTED, (actual, EXPECTED)
