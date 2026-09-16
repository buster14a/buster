from pathlib import Path
import json
import subprocess
import tempfile

HEAD = '3be1d020ca33e9c52bc0afcf198c7ba4904c0c9c'
MAIN = '939041b3d6b4b5c773318a5b494a28a4d9b829f1'
EXPECTED = 'a0459b827c05cf288727c99a2563897eb79bfd36'
def git(*args):
    return subprocess.check_output(['git', *args], text=True).strip()
assert git('rev-parse', 'HEAD') == HEAD
name = 'src/buster/tests/compiler/frontend/c/c_test.c'
files = git('diff', '--name-only', '--diff-filter=U').splitlines()
assert files == [name], files
# The real history has multiple merge bases; Git's virtual base already
# reconciles the landed binary16 predecessor. Preserve main's entire fixture
# file and insert only the independently reviewed BF16 fixture/registration.
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
print(json.dumps({'head': HEAD, 'main': MAIN, 'tree': actual, 'expected_tree': EXPECTED, 'resolution': 'Preserve all main fixtures and splice BF16 fixture plus numeric expansion'}, indent=2))
assert actual == EXPECTED, (actual, EXPECTED)
