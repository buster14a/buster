# Independent Unix CI suites

Related work: [#333](https://github.com/buster14a/buster/issues/333),
[#335](https://github.com/buster14a/buster/issues/335), and
[#92](https://github.com/buster14a/buster/issues/92).
This is a suite-level first slice, not completion of deterministic partitioning
inside the compiler/configuration matrix or the Clang analyzer.

## Evidence and rationale

Inspected main: `06fdf2d09e324778789ec0666a44d8055e32b44d` on 2026-09-10.
Publication base: `e2395299585de664602dd081f220d25d524760d6`; the intervening
commits do not change the edited workflow, helper, tests or CI guide.
The successful first-attempt macOS Intel job
[103010636405](https://github.com/buster14a/buster/actions/runs/34518726778/job/103010636405)
started at 19:09:51 UTC and completed at 19:51:30 UTC, after a five-second
job queue. Its PR head was `c4616ac4b59fe558afd76b8c0a00deeff5938b26`;
this is not substituted for the PR merge revision checked out by the runner.

| Observed work | Seconds | Duration |
| --- | ---: | ---: |
| Whole job | 2,499 | 41m39s |
| Combination matrix | 734 | 12m14s |
| Execution-mode matrix | 141 | 2m21s |
| Native differential matrix | 1,430 | 23m50s |
| Diagnostic artifact upload | 130 | 2m10s |

These are one job's observations, not a matched before/after median. The older
[#333 baseline](https://github.com/buster14a/buster/issues/333#issuecomment-5611168443)
reported a 34m32s whole workflow and a different differential-suite duration;
do not pool changing source or workflow cohorts to manufacture a speedup.

Previously, modes ran only after combinations finished, even though modes
already deleted and regenerated the build tree before compiling its own
Release compiler. Moving that independent work onto another runner removes an
ordering dependency without adding another expensive compiler producer.
Modes and differential stay together: separating those two would either
rebuild their shared compiler or introduce artifact handoff and its dependency.

For the observed test-step durations, the scheduling model changes from
`734 + 141 + 1430 = 2305` seconds to `max(734, 141 + 1430) = 1571` seconds.
The potential removed serial work is 734 seconds, not an observed speedup.
New runner queues, checkout and evidence uploads affect the actual result.

## Ownership and required checks

| Job group | Lanes | Work |
| --- | ---: | --- |
| `test` | 6 | Unchanged complete `test_all_combinations_ci` |
| `native` | 4 Unix | Unchanged modes and native differential, sequential within each lane |
| `mobile` | 3 | Unchanged Android and iOS `--all` suites |
| `lint` | 1 | Workflow lint and approved action references |
| `complete` | 1 | Fail-closed aggregation of all four groups |

The native check names append ` native` to Linux x86-64, Linux AArch64,
macOS x86-64 and macOS AArch64. Their runner labels are unchanged from the
corresponding combination lanes. Native and mobile jobs have no `needs`
dependency on combinations. Every matrix retains `fail-fast: false`.
`CI complete` requires all fifteen jobs including itself, not just the six
legacy desktop names. Failure, cancellation, skips and missing results fail.
The separate `Linux x86-64 bootstrap evidence` check remains separate.

No compiler/configuration row, fixture, allocator, optimization setting,
sanitizer, analyzer, table audit, fuzz policy, self-host check, deadline or
mobile execution policy changes. `build.c` still owns all native build/test
policy. The combination lanes still install verified Zig and Linux mold.
Native lanes need neither: they keep Clang and `--linker DEFAULT` with the
same hosted SDK, CMake, Ninja and execution/oracle-tool discovery as before.
No build tree, compiler binary, SDK state or test verdict is cached or shared.

A mode failure does not suppress the independent differential step. That step
retains its self-test and missing-CMake-cache recovery before building `ide`.
Summaries explicitly require both results. Native evidence moves to its own
run/attempt-qualified `native-*` artifact with unchanged seven-day retention
and program/subject-object exclusions, packed as described in
[native evidence packaging](#native-evidence-packaging). Failed work is never
made optional.

## Reproduction on the matching Unix image

From a fresh checkout with the hosted prerequisites, run the native slice:

```sh
export UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1
export CFLAGS=-Wno-invalid-feature-combination
driver="${RUNNER_TEMP:-/tmp}/buster-build"
out="$(mktemp -d)/differential"
clang -Isrc -Wall -Werror -Wno-unused-function -Wno-unused-variable -g build.c -o "$driver"
"$driver" generate --cc clang --config Release --linker DEFAULT
"$driver" test_mode_matrix --config Release
"$driver" test_differential --self-test
"$driver" build --config Release -t ide
"$driver" test_differential --ide build/Release/ide --out "$out" --sanitize-oracle
```

Do not generate concurrently with another build in the same tree. This local
sequence is a reproduction of successful execution; the workflow additionally
retains independent results after an earlier test failure. Combination and
mobile commands remain in [the CI guide](ci-github-actions.md).

## Validation and acceptance

`python3 tests/ci_tools_test.py -v` checks suite ownership, exact runner sets,
independence, command retention, summaries and timing layouts. It executes the
actual aggregate Bash body for all 625 combinations of four dependency groups
across success, failure, cancellation, skipped and missing results. Timing tests
reject absent, duplicate or unsuccessful jobs and absent native suite results.
Workflow blob and runner labels continue to separate measurement cohorts.

Before claiming a latency win, obtain at least three complete successful
first-attempt runs per variant with equivalent source, coverage and runner
labels. Collect and report verified Zig cache state, per-job queue delay,
workflow critical path, each suite's duration and total runner seconds.
The collector reports initial workflow queue delay; obtain per-job creation
and start timestamps from the Actions jobs API for per-job queue analysis.
Use `tools/github_ci_time.py collect` and `summarize`; retain source/workflow
identity and all failed or cancelled attempts outside the accepted cohort.
The fifteen-job layout is accounted for rather than hiding its added setup.
A passed cache-restore step is not independently verified cache-hit evidence.

This adds four schedulable jobs and can expose macOS Intel capacity limits.
It does not increase concurrency inside a runner or claim lower aggregate cost.
No matched timing cohorts or hosted candidate validation are supplied by this
document; those results must be inspected on the submitted PR revision.

## Native evidence packaging

Refs [#409](https://github.com/buster14a/buster/issues/409). The differential
runner writes one small file per child phase. The retained Intel macOS
evidence from job 103010636405 holds 52,072 files and 9,704,041 payload bytes;
its per-file artifact ZIP was 14,676,739 bytes and took 130 s to upload after
every test had finished. On this layout's first hosted run,
[job 103052446729](https://github.com/buster14a/buster/actions/runs/34531119949/job/103052446729)
still spent 123 s in `Retain native logs` at the end of the longest lane
(25-34 s on the other three native lanes).

After the native summary, `Pack native logs` runs `tools/ci_pack_evidence.py`,
also after failed tests but never after cancellation. The `native-*` artifact
then holds `native-ci-logs.tar.gz` plus copies of `result.json` and
`summary.md`, so the verdict is readable without unpacking. It is uploaded
with `compression-level: 0` because the archive is already compressed.

- **Contents.** Every regular file under `buster-ci/`, byte-exact, including
  empty files, NUL-delimited `.argv` files and dotfiles (the per-file upload
  skipped hidden files). Permission bits, whole-second modification times and
  empty directories are kept; owner names are not. As before, only generated
  `differential/**/program` and `differential/**/subject.o` are left out.
- **Refusals.** A missing tree, an output directory inside or around the tree,
  and any symbolic link or special file fail the step. Files are opened
  without following links.
- **Verification.** The tool re-reads the finished archive and requires the
  exact member set and every file's SHA-256 before renaming it into place.
  Stale or partial archives are removed first and never published.
- **Failure.** A packing failure fails the lane, and `Retain unpacked native
  logs` uploads the original tree with the old exclusions under the same
  artifact name.

Unpack with `tar -xzf native-ci-logs.tar.gz`; files land under `buster-ci/`.

Local check on that complete retained 52,072-file tree (Linux, Python 3.14):
the archive is 898,450 bytes, 93.9% below the ZIP; packing plus verification
took 5.0-5.3 s in warm repeated runs; GNU tar and libarchive `bsdtar` both
extract a tree identical to the source under `diff -r`. This is not a hosted
macOS measurement. Accept the change only after comparing `Pack native logs`
plus `Retain native logs` with the previous upload step in matched runs.

## Remaining bottlenecks

The differential loop is now the largest independent lane. Profile its existing
per-command evidence before changing it. A follow-up should use bounded native
case-level concurrency with isolated arenas, evidence and deterministic reports;
keep both independent oracles and every configuration comparison. The oracles
already run once per case, not once per configuration. Repeated compilation of
the unchanged ABI host translation unit inside candidate links is a separate
work-elimination candidate, requiring full source/flags identity and controls.

Case-level parallelism (#408) is not implemented here; per-case caller-object
reuse is proposed separately in #412. Evidence packaging is described above
(#409). Internal combination sharding (#333), analyzer partitioning (#92) and
full coverage manifests (#335) remain separately tracked; no issue is closed
by the suite split alone.

### Complete evidence publication

The packer stages the archive and both required summaries outside the source
and upload trees. It copies summaries from the same bytes placed in the archive,
verifies the archive, and publishes the complete directory with one rename.
Missing summaries, write/close/verification errors and publication failure do
not leave an uploadable archive or stale success summaries. Original evidence
stays in place. All directory modes and mtimes (including fractional seconds)
are retained, as well as file metadata and bytes; the generated-output exclusion
policy is unchanged.

If packaging fails, `Record native packaging failure` regenerates the fallback
`result.json` and `summary.md` with `pack` required before the original diagnostic
tree is uploaded. Thus passing tests cannot label a packaging failure successful.
Cancellation keeps the existing workflow conditions. This publication fix makes
no hosted package/upload speedup claim; the matched timing acceptance in #409
remains separate.
