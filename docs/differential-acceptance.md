# Differential campaign acceptance (#142)

This records the existing October 31, 2026 Ryzen 7 9700X evidence for
[#142](https://github.com/buster14a/buster/issues/142), audited on November 1.
It is not a new campaign or a test of a later `main` revision. The harness
landed in [#331](https://github.com/buster14a/buster/pull/331); the real C
`--tier1` driver and its follow-up landed in
[#970](https://github.com/buster14a/buster/pull/970) and
[#972](https://github.com/buster14a/buster/pull/972). The source-driver blocker
in the original harness documentation no longer applies.

The [harness guide](differential.md) defines the commands, comparison, retained
artifacts, failure semantics and reduction protocol. This document accounts
for the recorded compiler runs; it does not replace those contracts.

## Immutable inputs and evidence

The evidence repository revision is
`1d3f174114cb24d6e4d876e3c8361ee884990050`. Its
[campaign description][record], [9700X command][command],
[per-case ledger][ledger] and [build receipts][receipts] are the sources for
this accounting. Raw measurements remain in `buster14a/buster-performance`.

| Input | Recorded value |
| --- | --- |
| Buster revision used for the audited ledger | `2fa641250f4f0d571b5d50669af0da6a9c6a28aa` |
| Buster executable SHA-256 | `a60218744c3a3e1facf4b4bf5bc72077f222a3fd2709f2a9325a4f05f49e55a2` |
| Host | Ryzen 7 9700X; each two-seed batch under the shared lock, pinned to CPU 2 |
| Reference compiler | Clang 23.1.1, `-O0` and `-O2` |
| Csmith | `0cdc710315cfee9035e22ef4363ca479270d1934` (2.4.0) |
| YARPGen | `1adb290f453505838f3aa33dc571f292b0c1810f` (2.0), C mode, hash oracle |
| Seeds | 3000 through 3039 inclusive, separately for each generator |
| Buster modes | Default Tier 0; `--tier1 -fno-asynchronous-unwind-tables` |
| Bounds | 30,000 ms per process; default capture and source limits |

The ledger's Git blob is `41bcc8d08e0c3fb60268f6a4ddd49f9d5e49100b`.
It has exactly one row for each of the 80 generator/seed pairs. The audit
checked those identities, outcome counts and the original two-seed batch
boundaries; it did not regenerate or execute the inputs.

## Recorded counts

A `passed` row means both Buster modes and both Clang modes compiled, ran with
nonempty output, and agreed byte for byte. It is one generated program, not
four independent programs. A failed row contributes no accepted comparison
at either Buster tier.

| Generator | Attempted programs | Tier 0 accepted comparisons | Tier 1 driver accepted comparisons | Failed cases |
| --- | ---: | ---: | ---: | ---: |
| Csmith | 40 | 35 | 35 | 5 |
| YARPGen | 40 | 31 | 31 | 9 |
| Total | 80 | 66 | 66 | 14 |

The original command ran 20 separate batches, each containing two consecutive
seeds from each generator. Eight of those batches have every case marked
`passed`: **3000–3001, 3004–3005, 3012–3013, 3016–3017, 3018–3019,
3030–3031, 3034–3035 and 3036–3037**. Thus the ledger also identifies a bounded
cohort of **16 Csmith and 16 YARPGen programs at each Buster tier**, drawn from
whole original batches with no failed cases or output mismatches. This cohort
is identified retrospectively, not a newly executed or prospectively selected
sample. The other 12 batches contain at least one failed case; the wider
80-program campaign must not be described as 80/80 passing.

`--tier1` is the real mixed-tier C driver: eligible functions use Tier 1 and
unsupported functions fall back to Tier 0. These counts qualify that mode,
not an all-functions-Tier-1 compiler. The campaign description separately
records local function coverage (1,378 of 1,524 Csmith functions and 133 of 200
YARPGen functions compiled by Tier 1), demonstrating use of the pipeline;
those local totals are not 9700X function counts. Local scalar campaigns,
SQLite tests, harness fixtures and generator-only controls are also separate
evidence and are not added to the table above.

## Failure dispositions and regression boundary

All 14 failed ledger rows name a Clang step as their first failure. The
campaign author's diagnosis in the pinned description is:

| Generator and seeds | First recorded failure | Recorded diagnosis |
| --- | --- | --- |
| Csmith 3008, 3024, 3032 | `clang-o0-run:timeout` | Reference execution exceeded 30 seconds |
| Csmith 3023, 3039 | `clang-o0-compile:output-limit` | Reference compilation exceeded the diagnostic capture limit |
| YARPGen 3003, 3006, 3010, 3015, 3021, 3023, 3024, 3026, 3028 | `clang-o0-compile:exit` | Reference links exceeded the small code model |

These are retained failures, not skipped successes or proof of a Buster bug.
The harness's `failure.signature` records only the first failure, although it
runs all selected modes after generation. Consequently this ledger alone
cannot establish the later Buster outcomes for a reference-blocked case. No
claim that those 14 cases passed Buster, or that they have a valid four-way
oracle, is needed or made here. The pinned publication contains the ledger,
command and receipts, not every per-case capture; the diagnoses above are
attributed to the original campaign report rather than a new inspection of
those captures.

No Buster defect requiring a reduced source regression is reported by this
record. No generated C or synthetic replacement regression is imported for
this acceptance update. A later reference-valid Buster failure must use the
existing replay/reduction protocol and gain a source-level regression with
expected output, seed, tool revisions and failing/fixed compiler revisions.
A reference failure must be investigated separately, never waived by changing
the harness's pass condition. The interpreter adapter remains
[#176](https://github.com/buster14a/buster/issues/176), not a prerequisite
silently added to the four native modes of #142.

## Bounded strict requalification

For a new qualification, build the chosen Buster revision on the approved host
under its shared lock, and set `BUSTER`, `CLANG`, `CSMITH`, `CSMITH_INCLUDE` and
`YARPGEN` to the actual absolute paths. Set `BUSTER_REVISION` and
`GENERATOR_REVISIONS` to the revisions actually installed, and `OUTPUT` to an
artifact directory outside the source checkout. For a historical replay,
restore the recorded revisions, headers and environment instead of relabeling
a current build as the measured one.

The following four-program batch uses the existing harness, adds retained
Tier 1 coverage reports, and does not suppress any failure:

```sh
EXCLUSIVE_OWNER=issue-142 EXCLUSIVE_TITLE='Csmith/YARPGen both-tier qualification' EXCLUSIVE_LINK=https://github.com/buster14a/buster/issues/142 "$HOME/buster-fast-bench/exclusive.sh" taskset -c 2 "$BUSTER" test-differential --buster "$BUSTER" --tier0-default --tier1-arg --tier1 --tier1-arg -fno-asynchronous-unwind-tables --tier1-arg --tier1-report --csmith "$CSMITH" --csmith-include "$CSMITH_INCLUDE" --yarpgen "$YARPGEN" --clang "$CLANG" --generator both --seed 3000 --count 2 --timeout-ms 30000 --revision "$BUSTER_REVISION" --generator-revision "$GENERATOR_REVISIONS" --output "$OUTPUT"
```

Accept this invocation only on exit 0 and a matching `summary.json`: four
programs passed, zero failed, both generators represented twice and both tiers
selected. Inspect the saved `buster-tier1-compile.stderr` reports for actual
Tier 1 function coverage, not merely successful selection of a flag. A missing
tool/preflight is unavailable, not a pass. Any compilation, execution, output,
cleanup or artifact failure remains a failure, even when it occurs in Clang.
Do not use `|| true`, count a completed loop as success, or discard failed
inputs. Repeat independently bounded batches to extend coverage, releasing
the shared lock between them; CI's runner hooks already hold that lock and
must not acquire it a second time.

Retain the complete new campaign directories, including successful and failed
sources, `input.json`, `replay.args`, command JSON/stdout/stderr, version probes,
provenance, per-case results and the campaign summary. Preserve the exact
external runtime/header installation as well. Publish durable evidence through
`buster14a/buster-performance` with an immutable link from the campaign report;
do not commit raw campaign captures to this source repository. This command is
a rerun recipe, not a claim that requalification was executed in the audit.

[record]: https://github.com/buster14a/buster-performance/blob/1d3f174114cb24d6e4d876e3c8361ee884990050/docs/measurements/tier1-cfg-allocation-140-2026-10-31/step5/README.md
[command]: https://github.com/buster14a/buster-performance/blob/1d3f174114cb24d6e4d876e3c8361ee884990050/docs/measurements/tier1-cfg-allocation-140-2026-10-31/step5/9700x/queue-d.sh
[ledger]: https://github.com/buster14a/buster-performance/blob/1d3f174114cb24d6e4d876e3c8361ee884990050/docs/measurements/tier1-cfg-allocation-140-2026-10-31/step5/9700x/head-2fa64125/campaign-cases.tsv
[receipts]: https://github.com/buster14a/buster-performance/blob/1d3f174114cb24d6e4d876e3c8361ee884990050/docs/measurements/tier1-cfg-allocation-140-2026-10-31/step5/9700x/receipts.txt
