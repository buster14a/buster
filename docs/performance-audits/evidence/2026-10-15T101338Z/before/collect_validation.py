import hashlib
import json
import os
from pathlib import Path
import platform
import subprocess

ROOT = Path(__file__).resolve().parent
REPO = ROOT / 'buster'
OUT = ROOT / 'validation-evidence-final'
OUT.mkdir(exist_ok=False)
BIN = ROOT / 'validate-probe-coverage'

def run(args, **kwargs):
    return subprocess.run(args, check=True, capture_output=True, text=True, **kwargs).stdout

def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()

def count_at(data, function, line, column):
    candidates = []
    for f in data['data'][0]['functions']:
        if f['name'].split(':')[-1] != function:
            continue
        for region in f['regions']:
            a, b, c, d, count, file_id, _, kind = region
            if kind == 0 and f['filenames'][file_id].endswith('/ir/ir.c') and (a, b) <= (line, column) < (c, d):
                candidates.append(((c - a) * 100000 + d - b, count, region))
    if not candidates:
        raise RuntimeError(f'no covering code region for {function}:{line}:{column}')
    candidates.sort()
    return {'count': candidates[0][1], 'region': candidates[0][2]}

cases = [(s, n) for s in ['aggregate', 'indirect', 'cast', 'splice'] for n in [256, 512, 1024, 2048, 4096]]
cases += [(s, 17) for s in ['aggregate-reversed', 'aggregate-bad', 'indirect-reversed', 'indirect-bad', 'cast-reversed']]
results = []
for shape, n in cases:
    stem = OUT / f'{shape}-{n}'
    env = dict(os.environ, LLVM_PROFILE_FILE=str(stem) + '.profraw')
    stdout = run([str(BIN), shape, str(n)], cwd=REPO, env=env)
    stem.with_suffix('.stdout.json').write_text(stdout)
    observation = json.loads(stdout)
    assert observation['ok'], observation
    run(['llvm-profdata', 'merge', '-sparse', str(stem) + '.profraw', '-o', str(stem) + '.profdata'])
    exported = run(['llvm-cov', 'export', str(BIN), '-instr-profile=' + str(stem) + '.profdata',
                    'src/buster/lib/compiler/ir/ir.c'], cwd=REPO)
    stem.with_suffix('.coverage.json').write_text(exported)
    data = json.loads(exported)
    observation['observed'] = {
        'aggregate_prior_field_comparisons': count_at(data, 'ir_validate_instruction_operation', 6679, 17),
        'indirect_forward_comparisons': count_at(data, 'ir_validate_instruction_operation', 6842, 17),
        'indirect_reverse_comparisons': count_at(data, 'ir_validate_instruction_operation', 6851, 17),
        'value_marker_tests': count_at(data, 'ir_validate_function_walk', 7295, 17),
        'row_validator_calls': count_at(data, 'ir_validate_instruction_row', 6910, 5),
        'cast_membership_queries': count_at(data, 'ir_label_provenance_contains', 3220, 5),
        'cast_membership_linear_comparisons': count_at(data, 'ir_label_provenance_contains', 3224, 13),
    }
    if shape in ['aggregate', 'aggregate-reversed']:
        assert observation['observed']['aggregate_prior_field_comparisons']['count'] == n * (n - 1) // 2
    if shape in ['indirect', 'indirect-reversed']:
        assert observation['observed']['indirect_forward_comparisons']['count'] == n * n
        assert observation['observed']['indirect_reverse_comparisons']['count'] == n * n
    if shape == 'splice':
        assert observation['observed']['row_validator_calls']['count'] == 1
        assert observation['observed']['value_marker_tests']['count'] == n
        observation['source_derived_marker_initialization_bytes'] = observation['rows'] + observation['values']
    if shape in ['cast', 'cast-reversed']:
        assert observation['observed']['cast_membership_queries']['count'] == n
        assert observation['observed']['cast_membership_linear_comparisons']['count'] == n * (n + 1) // 2
    results.append(observation)
    print(shape, n, {k: v['count'] for k, v in observation['observed'].items()}, flush=True)

source = REPO / 'src/buster/lib/compiler/ir/ir.c'
summary = {
    'revision': run(['git', 'rev-parse', 'HEAD'], cwd=REPO).strip(),
    'compiler': run(['clang', '--version']).splitlines()[0],
    'host': platform.platform(),
    'cpu': run(['lscpu']),
    'evidence_class': 'LLVM execution-region work counts on synthetic valid canonical IR; instrumented nanoseconds are diagnostic only',
    'production_changes': False,
    'license': 'Buster first-party license unselected; SPDX NOASSERTION, per LICENSES/README.md',
    'sha256': {str(p.relative_to(ROOT)): digest(p) for p in [BIN, ROOT / 'validate_probe.c', ROOT / 'collect_validation.py', source]},
    'cases': results,
}
(OUT / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n')
