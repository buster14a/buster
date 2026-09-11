# September 11 recovery of stranded branch work

This is **historical evidence and opt-in measurement-tool recovery**, not a
compiler optimization or a claim that the historical findings remain unfixed.
Integration base: `680dcdd6b28d6c4ea3ec8e25f8fe3441ff2c76ea`.

## Recovered contributions

The [September 6 performance follow-up](../../performance-audits/2026-09-06T125122Z.md)
and its evidence were stranded on
`claude/avx512-zen5-compiler-perf-8efb1e`, tip
`8c3b9944ff0ecee0c25d89b312ca005729f51abb`.
[PR 160](https://github.com/buster14a/buster/pull/160) merged into that branch,
not main, after [PR 139](https://github.com/buster14a/buster/pull/139) had merged
an earlier head. This recovery restores the missing 56 files: the audit,
raw evidence, opt-in SIMD harness, and two survey-script repairs. The new
integration commit retains the original author's identity.

The two survey scripts on the integration base are byte-identical to their
pre-follow-up versions. Their original repairs therefore apply without
reconciling intervening implementation edits: include `-lm`, verbosity and
source metrics in the default workload; reject failed workloads and missing
counters; reject incomplete/stale cache-survey captures. These are measurement
validity fixes, not demonstrated changes to compiler throughput. The existing
native throughput harness and its acceptance policy are unchanged.

The [September 7 audit bundle](../2026-09-07/README.md), originally proposed in
closed-unmerged [PR 206](https://github.com/buster14a/buster/pull/206), restores
32 exact files from `a226b93e8b3fb9fa14310c9c101e43bc09812de9`, including
26 report bodies and their original manifest/publication records. Read
[PUBLICATION.md](../2026-09-07/PUBLICATION.md) before the original README's
historical publication instructions. **Do not rerun `publish.py --publish`: the
findings were already filed.** Current issue state and current-source
reproduction must be checked before implementing any old report.

[manifest.json](manifest.json) records the original source commit, Git blob,
SHA-256 and length of all 88 recovered files. All are exact copies, except the
SIMD harness README, which has an explicitly recorded recovery warning prepended;
its original text follows unchanged. Older audit narratives and numeric results
are immutable historical records, not updated current-main claims.

## Deliberately not imported

- The old compiler branch's production changes, old AGENTS.md, and CMake
  workaround are not reinstated. No compiler, build, CI, or test-registration
  file changes in this recovery.
- The recovered integer-literal candidate is not applied. The main history
  already includes merged [PR 372](https://github.com/buster14a/buster/pull/372),
  whose bounded spelling/overflow repair supersedes that candidate's central
  objective. Broader issue 148 work is not closed here.
- The literal-reuse and instrumentation `.patch` files remain inert evidence.
  They are not applied, built or registered. The original zero-hit and negative
  timing observations remain visible.
- Other retained branch payloads are outside this focused recovery; this is not
  a finding that every remaining branch is redundant. No source branch is
  deleted or rewritten by preparing this change.

## Validation on the integration base plus this recovery

Commands run from the repository root:

```sh
python3 tools/branch_miss_survey.py --self-test
python3 tools/cache_miss_survey.py --self-test
python3 tools/new_audit.py --check
```

Both survey self-tests pass. Python syntax checks pass for all six recovered
Python files. Original-content verification covers all 88 files; all 26
September 7 issue-body hashes match the original manifest. A local Markdown
path check finds all 33 original relative link targets. A static probe locates
all six quoted-decoder extraction anchors in current `c_gen.c`; this is not a
kernel runtime test or proof of complete harness compatibility.

The full audit checker fails identically before and after: historical
`2026-08-24T131821Z` lacks the required opening id/parenthetical. Neither that
immutable file nor the checker is modified. The recovered index entry is unique
and placed between the existing September 6 and September 5 entries.

The complete `git diff --check` reports five whitespace warnings inside the
two byte-preserved historical `.patch` files: blank context lines necessarily
start with a space in unified patches. Ordinary recovered scripts/docs have
no whitespace warnings. The original patch bytes are retained rather than
silently rewritten, and no whitespace rule or test is disabled. These are
reported warnings, not a claim that the complete check passed.

No Buster compiler build, SIMD harness runtime, fresh hardware profile,
self-host fixed point or hosted CI was run for this recovery. Historical
measurements are not new Zen 4/Zen 5 acceptance. Publish as a draft until the
current-head applicable checks and opt-in harness compatibility are reviewed.
