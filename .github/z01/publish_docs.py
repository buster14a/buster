import json
import os
import pathlib
import subprocess

repo = 'repos/buster14a/buster'
parent = '2f5f9be468f154006dd923ce57afec84985e3459'
main = 'ef99ec72cf0f17c8818a1d4b4c106fd5908b5eae'
ref = '/git/refs/heads/astra/z01-248-lazy-syntax-diagnostics'
evidence = pathlib.Path(os.environ['RUNNER_TEMP']) / 'z01-census'
evidence.mkdir(parents=True, exist_ok=True)

def api(endpoint, data=None, method='POST'):
    args = ['gh', 'api', repo + endpoint]
    if data is not None:
        args += ['--method', method, '--input', '-']
    run = subprocess.run(args, input=None if data is None else json.dumps(data), text=True, capture_output=True, check=True)
    return json.loads(run.stdout)

def unchanged():
    return api('/git/ref/heads/main')['object']['sha'] == main and api(ref)['object']['sha'] == parent

if not unchanged():
    raise SystemExit('Main or the owned source branch advanced; review required')
# Record the real baseline failure; this is not an acceptance gate or suppression.
baseline_check = subprocess.run(['python3', 'tools/new_audit.py', '--check'], text=True, capture_output=True)
(evidence / 'baseline-audit-check.txt').write_text(baseline_check.stdout + baseline_check.stderr)
(evidence / 'baseline-audit-status.txt').write_text(str(baseline_check.returncode) + '\n')
print(baseline_check.stdout + baseline_check.stderr, end='')

census = pathlib.Path('docs/compiler-throughput-census.md')
census.write_text(pathlib.Path('../payload/.github/z01/census.md').read_text())
with census.open('a') as out:
    out.write('''
## Publication-time ownership update

After the initial inventory, PR #372 added `c_parser_validate_integer_token`,
which calls `c_parser_diagnostic`. Its parser/header/frontend-test patches and
empty discussion were read before this documentation publication. The two
changes are compatible but need an explicit integration edit: thread the
existing `Arena*` through the new validator and both syntax-walk call sites,
then forward it to `c_parser_diagnostic`. Retain the integer diagnostic identity,
both regression helpers and both registrations. Both PRs insert a helper after
`c_test_parser_body_frame_storage`, so a textual conflict is possible even
though the assertions test different contracts. Do not drop either helper or
restore eager storage to resolve that conflict. No change to PR #372's branch
is made here; the combined revision still needs all checks.

PR #370 adds qualified-parameter coverage in the shared frontend test file.
PR #371 owns the more detailed canonical-pass prerequisite map and cleanup
work counters; reuse its `docs/middle-end-pass-map.md` when integrated rather
than creating another middle-end measurement stack. PR #369 extends the
machine-metadata ownership map. The other newly opened PRs #364-#368 concern
bootstrap, workflow, metamorphic, PIC-fixture and unwind work. These ownership
notes are a snapshot, not locks or assertions that future changes are disjoint.

The first docs publication, run 34424310196, stopped on the existing audit
checker error: `2026-08-24T131821Z` does not open with its ID and a parenthetical.
The later publication records the baseline checker result and keeps the strict
checker as a failing post-publication gate. It does not modify the old immutable
audit or call the repository-wide check green. Documentation can be reviewed
while that pre-existing failure remains explicit.
''')
root = pathlib.Path('docs/performance-audits')
before = set(root.glob('*.md'))
subprocess.run(['python3', 'tools/new_audit.py', '--platform', 'Source census; hosted CI; physical Zen 5 unverified', 'Z01 compiler census and first-error syntax storage (#128, #248)'], check=True)
added = set(root.glob('*.md')) - before
if len(added) != 1:
    raise SystemExit('Expected exactly one newly minted audit')
audit = added.pop()
with audit.open('a') as out:
    out.write('''
Baseline: `ef99ec72cf0f17c8818a1d4b4c106fd5908b5eae`.
Production/test commit: `2f5f9be468f154006dd923ce57afec84985e3459`.
The [whole-compiler census](../compiler-throughput-census.md) records actual
production symbols, population definitions, ownership, historical profiles,
negative experiments and the current measurements still unavailable.

## Change and classification

Verified unused clean-path allocation; throughput benefit not established.
`c_parse_ast` previously reserved `token_count + 1` syntax diagnostic rows
before finding an error. `c_parser_diagnostic` now receives the existing parse
arena and allocates that same fixed-capacity contiguous array on first error.
No row size, diagnostic cap, order, location, semantic diagnostic builder,
SIMD primitive, target feature, canonical/MIR representation or concurrency
change is involved. A successful parse eliminates one logical allocation of
`(token_count + 1) * sizeof(CDiagnostic)` bytes. This is not a physical-bandwidth,
RSS, zeroing or elapsed-time measurement. Dense-error storage growth and the
separate semantic diagnostic array remain outside this bounded #248 slice.

## Regression and hosted evidence

`c_test_parser_diagnostic_storage` checks empty/clean inputs, null/no-token
guards, 1/16/17/64/257 ordered errors, truncated syntax, exact messages and
locations, and arena-owned semantic handoff lifetime. The existing body-frame
storage bound is tightened. CI verifies all three resulting source blob hashes.

[Experiment 34423619331](https://github.com/buster14a/buster/actions/runs/34423619331)
reported success for baseline Release/self-host, the tests-only expected-red
check, and candidate Release/mode/self-host/native paired smoke steps. At the
last inspection before this audit was minted, the strict sanitizer step was
still running. Refer to the exact job and retained raw evidence for subsequent
results; neither publication success nor an earlier green revision establishes
final-head acceptance. All builds/tests run in CI with one build/test/matrix
worker and sequential generation. No compiler build/test ran locally.

The full ordinary PR platform/configuration, representative native throughput
and final-head self-host gates remain required. Physical Zen 5/Zen 4 acceptance,
PMU counters, current per-stage wall shares, disassembly and compiler code-size
verdicts are unavailable here. Hosted smoke output identity/nonregression is
not evidence of a representative physical Zen 5 speedup.

## Budgets, dependencies and limitations

Require exact compiler outputs and diagnostics, no extra retained diagnostic
rows on errors and no syntax-diagnostic storage on success; record actual RSS
and code size separately. Reuse the existing native paired harness and separate
allocation observer. The census's historical Callgrind shares are not current
wall shares and do not establish a global best optimization.

PR #372's new integer-validation caller needs the existing arena threaded
through its helper when both changes are integrated. Preserve both test helpers
and registrations, then validate the combined revision. No other contributor
branch is modified. Reuse #371's canonical-pass map and #369's metadata map.

The unchanged audit checker reports the malformed opener in the historical
`2026-08-24T131821Z` entry. The baseline result is retained and the strict
post-publication check remains failing; this audit does not suppress the error,
rewrite the old immutable entry or claim repository-wide lint success.
''')
paths = ['PERFORMANCE_AUDITS.md', str(census), str(audit)]
subprocess.run(['git', 'diff', '--check'], check=True)
expected = {
    'src/buster/lib/compiler/frontend/c/c_parse.c': '848de5ef421fe286ca6ec8c5a05f28959f04db3f',
    'src/buster/lib/compiler/frontend/c/c.h': '014ea548e06980c8e5f4594ea62f405903031f46',
    'src/buster/tests/compiler/frontend/c/c_test.c': 'e3436dd6702014572988eaaccf04ea71995d22e0',
}
for path, sha in expected.items():
    if subprocess.check_output(['git', 'hash-object', path], text=True).strip() != sha:
        raise SystemExit('Unexpected source change: ' + path)
entries = []
for path in paths:
    text = pathlib.Path(path).read_text()
    blob = api('/git/blobs', {'content': text, 'encoding': 'utf-8'})
    entries.append({'path': path, 'mode': '100644', 'type': 'blob', 'sha': blob['sha']})
    destination = evidence / path
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_text(text)
base_tree = api('/git/commits/' + parent)['tree']['sha']
tree = api('/git/trees', {'base_tree': base_tree, 'tree': entries})
commit = api('/git/commits', {'message': 'docs: map compiler work and record the #248 storage experiment', 'tree': tree['sha'], 'parents': [parent]})
if not unchanged():
    raise SystemExit('Main or owned source branch advanced during publication')
api(ref, {'sha': commit['sha'], 'force': False}, 'PATCH')
(evidence / 'published-sha.txt').write_text(commit['sha'] + '\n')
(evidence / 'audit-path.txt').write_text(str(audit) + '\n')
with open(os.environ['GITHUB_STEP_SUMMARY'], 'a') as out:
    out.write('Published docs-only commit: ' + commit['sha'] + '\n\nAudit: ' + str(audit) + '\n\nThe strict audit-format gate remains separate and is not asserted green.\n')
