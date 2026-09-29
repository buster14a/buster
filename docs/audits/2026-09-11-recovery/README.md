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

## Later disposition: SIMD harness removed from main

[Issue 1591](https://github.com/buster14a/buster/issues/1591) removed the
recovered `tools/bench_pr139_simd/` harness (seven files, 40,207 bytes) from
the current tree. No build, CMake, CI workflow, release path or registered
test invoked it. Its exact bytes stay in main history:

- [`ed99d3deca479e9eb2db1331103ddeba734e4fb7`](https://github.com/buster14a/buster/tree/ed99d3deca479e9eb2db1331103ddeba734e4fb7/tools/bench_pr139_simd)
  is the directory's last change. Its tree,
  `322158565697bbc79e1329c421b10107553835ed`, is unchanged up to the removal
  and holds the six recovered files plus the `RECOVERY.md` status note.
- `f88704caec3c00819510c219d609e34639c3f681` first carried the recovered
  files on main. Their original source remains
  `8c3b9944ff0ecee0c25d89b312ca005729f51abb`.
- [manifest.json](manifest.json) and the recovery audit's
  [recovered-files.json](../../performance-audits/evidence/2026-09-11T131903Z/recovered-files.json)
  still record every file's original blob, SHA-256 and length.

To replay the harness, check out `ed99d3de` in an isolated worktree and follow
its README there. The [recovery audit](../../performance-audits/2026-09-11T131903Z.md)
check-only commands run from that tree too. Do not copy the harness back into
the source tree.

The three kernels it isolated keep differential coverage in registered tests.
None of the harness's timing machinery was promoted, and `./build.sh
bench_throughput` remains the throughput launcher:

- `c_test_string_literal_decode_differential` (`c_frontend_tests`) holds
  `c_ir_decode_quoted` and `c_ir_count_quoted` to
  `c_ir_decode_quoted_reference`, with escapes slid across window boundaries
  and a grammar fuzz.
- `c_test_intern_scan_by_shape` (`c_frontend_tests`) holds identifier
  selection in `c_symbols_intern_tokens` to the scalar definition at every
  lane of the window.
- `x86_64_metadata_tests` first checks every decoded metadata blob against
  the generated accessors (`buster_x86_metadata_test_flat_decode_matches_generated`),
  through whichever chunk decoder the build selected: AVX-512 VBMI or scalar.

The [September 6 audit](../../performance-audits/2026-09-06T125122Z.md) and
its [evidence README](../../performance-audits/evidence/2026-09-06T125122Z/README.md)
each linked the harness by relative path. Those two link targets now point at
the harness README in `ed99d3de`, and nothing else in either file changed.
Their current blobs, `77a3eec1d0c06adb9e147cd57e3c423c337d61ab` and
`034d40b33eb7388e8478b1f29e24e40b198ca9ee`, therefore differ from the
manifest's `287b7f233c3f8729273d5158044426cfef132e4b` and
`3315e5035a34b9fee77554219b0de45525701a1f`. Those original bytes remain
retrievable at `f88704ca`.
