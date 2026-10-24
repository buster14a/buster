# Tier 1 emitted-case measurement preparation

`measure-tier1-model` prepares bounded experiments for the remaining actual
selector cases in [#181](https://github.com/buster14a/buster/issues/181). It
consumes `test-tier1-select --manifest`, accepts only its `origin=plan` rows as
measurement candidates, and reuses the existing
[`x86_zen5_measure.c`](../src/buster/compiler/x86_zen5_measure.c) counter and
loop-count validation. The implementation and fixture construction are original
project code. It changes no model observation or cost column.

The input must be a complete manifest from a passing selector test at the same
model revision. Every row, including excluded expansion/wrapper origins, must
have the expected columns, consecutive per-origin record numbers, legal
baseline x86-64 operands, the exact current model ID and all seven current
column values. A required final producer footer records its schema version and
all three origin counts. Missing/mismatched/duplicate footers and any rows after
the footer fail, including truncation between complete data records. The footer
is `# tier1-manifest-end,version=1,plan=N,expansion=N,wrapper=N`, with actual
decimal counts in place of each `N`. Malformed, truncated, incompatible or
oversized case inventories fail before execution. The limits are 1,023 bytes plus terminator per CSV line
and 4,096 distinct normalized plan cases. This is a trusted local test artifact;
an `origin` label alone does not establish a source-to-machine-code proof.

The report retains the complete input's byte count and FNV-1a hash. Preserve a
separate SHA-256 of the manifest, sources and executable with target results.
Each case retains its first original plan record number, occurrences and full
example record. Cases normalize physical register numbers while preserving
operand aliases, form, immediate bits, address mode, scale and displacement.
High-byte registers and forced zero-displacement LEA bases retain their original
record for grouping and are refused before constructing fixtures. Every original
row is classified before grouping; different eligibility or model IDs cannot
merge into one case.
The generated-loop hashes identify the exact register-renamed executable
bytes. Observations are about those fixtures; the tool neither extrapolates
them to other operand cases nor inserts values into `x86_zen5.c`.

## Frozen initial inventory

The reviewed selector manifest has SHA-256
`11aac91d18c0fccc6af0c8ec89a8629b6fe46d5427d4d0d61d57b8169ee29ff7`,
FNV-1a `96b31110d17ccc5d`, and 1,723,599 bytes. Its origins are 2,593 actual
plan records, 9,358 direct expansion records and 8,940 test-wrapper records.
Only the first group enters this inventory. It contains 570 normalized cases;
563 cases and 2,439 records have unknown model IDs. The three currently observed
columns (GPR latency, throughput and retired macro-ops) are each missing for
those 2,439 records. All 2,593 lack flag latency, load increments, executed
micro-ops and pipe assignments.

| Actual plan family | Unknown records | Unknown cases | Initial experiment scope |
| --- | ---: | ---: | --- |
| Register `mov` (32/64-bit) | 477 | 2 | Eight/four streams and source dependency |
| `mov` immediate (32/64-bit) | 368 | 184 | Eight/four streams; no GPR input latency |
| Register `sub`, `and`, `or`, `xor` (64-bit) | 238 | 4 | Destination/source dependencies and streams |
| Accumulator immediate arithmetic (32-bit) | 263 | 147 | Fixed EAX dependency only |
| Immediate `imul` (32/64-bit) | 443 | 94 | Available GPR dependency paths and streams |
| Immediate `shl` (32/64-bit) | 517 | 92 | Exact counts, including zero; no count extrapolation |
| `lea` (32/64-bit) | 43 | 36 | Exact addressing/alias shapes and available input paths |
| Memory `sub`, loads and stores | 10 | 3 | Refused; needs dedicated memory experiments |
| `ret` | 80 | 1 | Refused; needs a control-flow experiment |

The totals are 566 eligible cases/2,503 records and four refused cases/90
records. Seven eligible cases/154 records already have model IDs and are kept
as controls. The default timing mode visits only eligible unknown cases;
`--only` can select a known control explicitly. A successful verification does
not mean all emitted instructions or cost columns are covered.

The frozen plan fixtures omit planned comparisons, selects and conditional
branches, although separate expansion tests exercise their machine sequences.
They also omit many other legal immediate/address/register combinations.
Future producer coverage must be added to the actual planner fixtures and
given a new manifest identity. These counts cannot establish whole-emitter or
corpus coverage.

## Narrow-integer and terminal inventory

The expanded selector fixtures have manifest SHA-256
`985ba95bea59881b622954bf6622b02804f11337bb202bd4db42ee629d9f0042`,
FNV-1a `87ca02ce9d2a4d3c`, and 1,823,025 bytes. There are 3,849 actual
plan records; expansion and wrapper counts remain 9,358 and 8,940. The new
plan cases exercise byte/word memory, canonical integer conversions and
explicit unreachable. They do not add measured model observations.

The inventory contains 594 normalized cases: 577 eligible cases/3,349 records
and 17 refused cases/500 records. Construction verification passes all 577
eligible cases. Unknown model IDs remain on 586 cases/3,687 records. GPR
latency is missing on 3,695 records; throughput and retired macro-ops on 3,687.
All 3,849 records still lack flag latency, load increments, executed micro-ops
and pipe assignments. These are fixture construction and coverage counts,
not target timing or complete-emitter acceptance.

## Experiments and limits

The safe first slice admits 32/64-bit register arithmetic, moves, comparisons/tests that
only produce flags, and nonfaulting LEA addressing. Memory access, control flow,
flags consumers, decorated instructions, unsupported syntax and implicit
register constraints outside the handled accumulator form are refused. High-byte
operands and byte/word operations are refused; `movzx r32, r/m8` has a 32-bit
destination and remains eligible when its source is a low-byte register. LEA
with a zero displacement forced by RBP/R13 is also refused. Existing selector
register constraints exclude those bases.

Every body contains 240 target instructions. The loop uses R12; R11 and R14
hold fixed inputs; eight caller-saved registers form the streams. The wrapper
preserves R12/R14 and uses no target memory accesses. The body begins at a
64-byte boundary. An experiment is constructed only when its resolved semantic
read/write masks establish the specified GPR dependency. Every record must
retain the original normalized operand case. Both encoders must agree byte for
byte before the mapping becomes executable.

| Mask bit | Experiment | Interpretation |
| --- | --- | --- |
| 0 | `independent-eight` | Eight repeated destination streams |
| 1 | `independent-four` | Four matched destination streams |
| 2 | `chain-destination` | Existing destination read/write dependency |
| 3 | `chain-source` | Distinct source, or LEA base, to the next result |
| 4 | `chain-index` | A distinct LEA index to the next result |
| 5 | `mix-add-four` | Four target streams plus four ADD streams |
| 6 | `mix-imul-four` | Four target streams plus four IMUL streams |

Missing paths are omitted. In particular, a constant move or zero idiom cannot
acquire a GPR latency from a throughput loop. Accumulator-only instructions
cannot be renamed into eight independent accumulator registers, so only their
valid fixed-register dependency experiment runs. Stream counts and register
choices differ from the original twelve/six-stream model harness; the two
harnesses' rows must not be silently combined.

The counter group reports actual core cycles, retired instructions and retired
macro-ops, with multiplexed or unavailable counts rejected. The expected retired
instruction count checks loop construction. A matched empty loop is subtracted
from macro-op and instruction counts, not from cycles. Elapsed nanoseconds
include counter-control overhead. Mixed rows are per target/blocker pair.
Eight streams can remain dependency-limited; comparing them with four streams
and the mixtures is evidence about these blocks, not a proof of peak execution
throughput or physical pipe identity.

This first preparation does not time a flags dependency, a load increment or
executed micro-ops. Those columns and pipes remain explicitly unknown.
Generated-program benefit and compiler cost must still be measured separately.
No full-source corpus, SQLite or `speedtest1` result is represented here.

## Commands and evidence

The separate TU exports `run_tier1_model_measure(argc, argv)`, with the command
name at `argv[0]`, matching the benchmark entry points. These commands are
construction diagnostics and print no timing result:

```sh
./build/compiler test-tier1-model
./build/compiler test-tier1-select --manifest > tier1-plan-manifest.csv
./build/compiler measure-tier1-model --manifest tier1-plan-manifest.csv --list > tier1-model-cases.csv
./build/compiler measure-tier1-model --manifest tier1-plan-manifest.csv --verify
```

`test-tier1-model` runs 22 retained parser, footer and operand-grouping checks,
using a private temporary file under `TMPDIR` (or `/var/tmp`). It executes no
generated program and collects no timings. Its public entry point,
`run_tier1_model_measure_tests(argc, argv)`, receives arguments after the command
name and accepts none. Expected rejection diagnostics appear on standard error.

`--verify` builds and executes every eligible fixture for two iterations,
including known controls, and verifies encoding and dependency construction.
It is allowed on a development host and is never labelled a Zen 5 measurement.
Timing requires the approved 9700X CPUID and brand, working pinned counters and
the shared per-run lock; pin each process to CPU 2. Every target build/test also
requires the lock. Honor any active coordination hold before queuing work.

After freezing and hashing the input and executable, run each selected case in
at least three fresh processes, retaining every output and generated-code hash:

```sh
EXCLUSIVE_OWNER=issue-181-tier1-model ~/buster-fast-bench/exclusive.sh taskset -c 2 ./build/compiler measure-tier1-model --manifest tier1-plan-manifest.csv --only 0 --iterations 20000 --passes 11
```

The initial manifest's small priority set is case 0 (`MOV_R32_RM32`), 280
(`MOV_R64_RM64`), 287 (`SUB_R64_RM64`), 291 (`AND_R64_RM64`), 293
(`OR_R64_RM64`) and 295 (`XOR_R64_RM64`). Then inspect the exact immediate/LEA
examples in the inventory before choosing further cases, including zero shifts
(case 5), immediate-one multiplication (case 4), and base-plus-displacement LEA
(case 2). IDs are manifest-specific; never reuse them against another input
hash. A full no-`--only` run is available once the short screen has been reviewed.

Retain source and executable SHA-256s, CPU/microcode/compiler/governor metadata,
lock ownership, all process rows, complete input/output manifests and generated
code hashes. Compare repeatability, dependency behavior and four/eight-stream
differences before proposing any observation. The original
[2026-10-22 measurements](https://github.com/buster14a/buster-performance/blob/a00d16e47b6f1090b8a0424ab0dd8cb4e6016713/docs/measurements/x86-zen5-2026-10-22/README.md) remain
unchanged. Issue #181 remains open.
