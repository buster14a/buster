from pathlib import Path
from types import SimpleNamespace
import importlib.util
import sys
repo = Path(__file__).parent / 'buster'
spec = importlib.util.spec_from_file_location('diagnostic_equivalence', repo/'tools/diagnostic_equivalence.py')
m = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m)
original = m.compile_once
admitted = 0

def checked(compiler, entry, flags, directory, metrics):
    global admitted
    row = original(compiler, entry, flags, directory, metrics)
    if row['status'] == 0 and '-c' in flags:
        artifact = Path(directory)/'out.o'
        assert artifact.is_file() and artifact.stat().st_size > 0, (compiler, entry['name'], flags)
        admitted += 1
    return row

m.compile_once = checked
base_corpus = m.corpus
source = '''#define CAT(a,b) a##b
_Static_assert(sizeof(int) == 4, "int size");
_Static_assert(_Alignof(char) == 1, "char alignment");
_Alignas(_Alignof(long long)) char global[sizeof(int)];
enum { words = CAT(size,of)(short) + CAT(_Align,of)(char) };
int f(int n) { int vla[n]; return sizeof(vla) + _Alignof(int) + __alignof(int) + __alignof__(int) + words; }
'''

def corpus(scale):
    cases = base_corpus(scale)
    for std in ('c11','c17','c23','gnu17','gnu23'):
        cases.append(m.case('size_alignment_'+std, 'valid', {'main.c':source}, std=std, valid=True))
        for spelling in ('alignof', 'Sizeof', 'sizeof_suffix', '__alignof___'):
            cases.append(m.case('near_keyword_'+std+'_'+spelling, 'valid',
                {'main.c':f'int {spelling}(int x) {{ return x; }}\nint f(void) {{ return {spelling}(3); }}\n'}, std=std))
    return cases

m.corpus = corpus
result = m.evaluate(SimpleNamespace(baseline=str(Path(__file__).parent/'bin/ide-base'),
    candidate=str(Path(__file__).parent/'bin/ide-cand'), baseline_census=None, candidate_census=None,
    output=str(Path(__file__).parent/'evidence/equivalence'), scale=256))
print('Independently admitted nonempty successful objects:', admitted)
sys.exit(result)
