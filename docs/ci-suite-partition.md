# Independent Unix CI suites

> Historical suite-level design/evidence. The subsequent internal desktop
> partition and current 23-job completion contract are documented in
> [Desktop combination shards](ci-combination-shards.md). Historical timings
> below are not matched before/after evidence for #333.

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
"$driver" generate --cc clang --config Release --linker DEFAULT -- -DBUSTER_DEBUG_INFO=OFF
"$driver" test_mode_matrix --config Release
"$driver" test_differential --self-test
"$driver" build --config Release -t ide
"$driver" test_differential --ide build/Release/ide --out "$out" --sanitize-oracle --jobs 4
```

The hosted native producer deliberately keeps the non-`--ci` Release policy:
`-O3`, tests enabled, one unity translation unit and object, and frame pointers
enabled. It opts out only of producer debug information. The normal mode setup
and missing-cache recovery pass the same explicit
`-DBUSTER_DEBUG_INFO=OFF` override. Local Release generation remains unchanged
and therefore keeps debug information and frame pointers for profiling.

The profile qualification probe requires invalid input to fail nonzero with a
nonempty diagnostic; the production differential runner separately captures and
compares compiler diagnostics. The native lane keeps raw logs, but it neither
retains the producer executable or a core file nor symbolizes producer program
counters after a crash. On Linux the installed non-sanitized crash handler
reports the fatal signal, fault address and raw program counter before
re-raising; macOS has no corresponding custom handler. The hosted profile
therefore retains frame-pointer unwindability and symbols, while source-line
DWARF remains a local profiling facility rather than retained native-CI failure
evidence.

Do not generate concurrently with another build in the same tree. This local
sequence is a reproduction of successful execution; the workflow additionally
retains independent results after an earlier test failure. Combination and
mobile commands remain in [the CI guide](ci-github-actions.md). To reproduce
the pre-rollout baseline, give the final command another fresh `--out` directory
and replace `--jobs 4` with explicit `--jobs 1`.

## Validation and acceptance

`python3 tests/ci_tools_test.py -v` checks suite ownership, exact runner sets,
independence, command retention, summaries and timing layouts. It executes the
actual aggregate Bash body for all 625 combinations of four dependency groups
across success, failure, cancellation, skipped and missing results. Timing tests
reject absent, duplicate or unsuccessful jobs and absent native suite results.
Workflow blob and runner labels continue to separate measurement cohorts.
`tools/native_producer_profile_test.py` additionally locks the same explicit
producer profile into normal setup and missing-cache recovery and rejects
`--ci` or disabled frame pointers in either path.

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
The differential step now requests four case workers inside each native runner,
bounded by its logical CPU count, `BUSTER_TEST_JOBS`, and single-threaded
policy. The matched #408 qualification is recorded in the [current performance
audit](performance-audits/2026-09-15T072154Z.md); complete-job and workflow
results must still be inspected on the exact submitted PR revision.

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
macOS measurement. The hosted campaign below supplies the matched comparison.

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
no hosted package/upload speedup claim by itself; the matched timing acceptance
is recorded below.

### Hosted timing acceptance (2026-09-12)

The accepted campaign compares candidate `7efd6c7ef203cc2ff590c8cb233ee50d335474ba`
(workflow blob `33dd5020578d02f83d0eaedac36a0c1a55bc73e4`) with control
`c9879553afa56a62d9fbdf93640fc70073f3b22b` (workflow blob
`5a04fce32d3af5fd9f542d3c1db49248db9dafdd`). The control is based directly on
the candidate and changes only the native upload block plus the policy assertion
that validates that deliberate historical layout. Native commands, inputs,
runner labels, exclusions, artifact names and retention are identical.

Each side has three complete, successful, first-attempt 17-job runs. Candidate
runs are [34718208639](https://github.com/buster14a/buster/actions/runs/34718208639),
[34718396947](https://github.com/buster14a/buster/actions/runs/34718396947) and
[34718426123](https://github.com/buster14a/buster/actions/runs/34718426123);
control runs are [34719693093](https://github.com/buster14a/buster/actions/runs/34719693093),
[34719694756](https://github.com/buster14a/buster/actions/runs/34719694756) and
[34719696476](https://github.com/buster14a/buster/actions/runs/34719696476).
All six runs report warm, verified Zig archives on every desktop job. Native
jobs have no cache restore or save step and start from fresh hosted-runner
filesystems. Three earlier control attempts (34718209726, 34718398334 and
34718427915) are excluded because the unchanged policy test rejected the
deliberate unpacked layout; none is pooled into these results.

Times are GitHub's step intervals in seconds, in the linked run order; the value
in parentheses is the median. Artifact sizes are the median of GitHub's final
artifact ZIP metadata.

| Native runner | Unpacked upload samples | Pack + upload samples | Median tail reduction | Median artifact bytes |
| --- | ---: | ---: | ---: | ---: |
| Linux x86-64 (`ubuntu-26.04`) | 24 / 39 / 27 (**27**) | 10 / 11 / 10 (**10**) | **63.0%** | 15,191,739 -> 2,028,783 (**86.6% smaller**) |
| Linux AArch64 (`ubuntu-26.04-arm`) | 29 / 31 / 30 (**30**) | 7 / 8 / 7 (**7**) | **76.7%** | 14,231,001 -> 1,891,471 (**86.7% smaller**) |
| macOS x86-64 (`macos-26-intel`) | 107 / 112 / 149 (**112**) | 33 / 21 / 22 (**22**) | **80.4%** | 15,691,058 -> 2,239,416 (**85.7% smaller**) |
| macOS AArch64 (`macos-26`) | 22 / 42 / 25 (**25**) | 19 / 10 / 11 (**11**) | **56.0%** | 14,648,795 -> 2,052,587 (**86.0% smaller**) |

The complete native-job intervals and queueing remain separate from the changed
tail. They demonstrate hosted workload variance instead of implying that every
second in a job came from packaging.

| Native runner | Control job median | Candidate job median | Control queue median | Candidate queue median |
| --- | ---: | ---: | ---: | ---: |
| Linux x86-64 | 754 s | 851 s | 4 s | 6 s |
| Linux AArch64 | 539 s | 512 s | 7 s | 6 s |
| macOS x86-64 | 1,713 s | 1,470 s | 4 s | 5 s |
| macOS AArch64 | 561 s | 738 s | 7 s | 10 s |

`tools/github_ci_time.py` validated all required jobs and key coverage steps.
The whole-workflow observations likewise include queueing and every runner, not
only the four favorable lanes:

| Variant | Elapsed samples (median) | Execution-span samples (median) | Initial queue samples (median) | Runner-second samples (median) |
| --- | ---: | ---: | ---: | ---: |
| Unpacked control | 1,684 / 1,721 / 1,871 (**1,721**) | 1,681 / 1,718 / 1,868 (**1,718**) | 3 / 3 / 3 (**3**) | 10,821 / 11,728 / 10,600 (**10,821**) |
| Packed candidate | 1,954 / 1,900 / 1,609 (**1,900**) | 1,942 / 1,897 / 1,605 (**1,897**) | 12 / 3 / 4 (**4**) | 11,615 / 11,870 / 11,036 (**11,615**) |

The candidate's whole-workflow medians are higher because independent compiler,
analyzer and simulator work varied; no end-to-end workflow or whole-job speedup
is claimed. The supported conclusion is narrower: on every changed runner label,
the measured package-plus-upload tail is lower than the old upload alone.

Artifact `native-linux-aarch64-34718208639-1` (ID 10304937788) was downloaded
and checked independently. Its ZIP SHA-256
`c2c16e7ad4ef497e002a0a0bb76afd521e7075581aad3c884c9cce9969fabc01`
matches GitHub metadata and contains exactly the archive plus the two direct
summaries. The archive SHA-256 is
`9931f2476349f554ddd00202c95b2bdf73c3f4d744bbd166c5c1a3b72ddedb0b`;
its 59,565 members contain 52,160 regular files. All 17,352 `.argv` members
remain NUL-terminated, both direct summaries match their archived bytes, no
unexpected member types exist, and neither excluded generated output appears.

## Remaining bottlenecks

The differential loop remains the largest independent lane. Bounded native
case-level concurrency now uses isolated arenas, evidence and deterministic
reports while retaining both independent oracles and every configuration
comparison. The oracles run once per case, not once per configuration; caller
object reuse is implemented separately by #412. Evidence packaging is described
above (#409). Internal combination sharding (#333), analyzer partitioning (#92)
and full coverage manifests (#335) remain separately tracked; no issue is
closed by the suite split alone.

## Native runner phase observations

Native correctness ownership is unchanged. `tools/ci_native_observation.py` wraps the existing native commands with identity-bound monotonic phase records and publishes `native-observation.json` through the existing packed artifact. See [Native runner observations](native-runner-observations.md) for schemas, failure retention, and the exact-match broad-degradation classifier.
