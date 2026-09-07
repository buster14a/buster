# Instruction-selection throughput benchmark

`ide bench-select <self-contained-source.c>` measures the production native
x86-64 or AArch64 selector. It does not measure the diagnostic declarative rule
matcher in `machine_select_generated.c`. No timers or counters are added to
ordinary `ide cc` execution, and selection, legalization, feature policy,
placement, and encoding are unchanged.

## Run and reproduce

Build through the repository driver with the same configuration for baseline
and candidate. First reproduce `./build.sh test_self_host --config Release`
and the relevant correctness matrix; this benchmark is not a replacement.

```sh
python3 tools/selection_benchmark.py --self-test
python3 tools/selection_benchmark.py --compiler build/Release/ide --samples 8 --allocators none fast mir-stack quality --check --output build/selection-benchmark/smoke.json
python3 tools/selection_benchmark.py --compiler /absolute/base/ide --candidate /absolute/candidate/ide --samples 12 --allocators none fast mir-stack quality --check --cpu 0 --build-description 'Record commit IDs, host compiler/version, complete flags, configuration and host here' --output build/selection-benchmark/ab.json
```

Choose an allowed logical CPU for `--cpu`; it is optional and requires host
support for affinity. Do not run other builds or timing children concurrently.
The runner alternates A/B order. Even sample counts balance which binary runs
first. Pinning one vCPU does not eliminate VM interference, CPU throttling,
frequency changes, or shared-cache effects.

With no `--source`, the runner generates three deterministic self-checking C
programs: 256 small functions with arities zero through four; a 512-step
unsigned arithmetic chain; and an ABI/memory corpus covering zero through
24 parameters, register and stack arguments, a mixed integer/double aggregate
argument and result, signed extension, indexed memory, and loop/branch forms.
Python computes independent integer checksums modulo 2^64. `--check` executes
them under each requested allocator and requires zero exit status and identical
stdout/stderr bytes. It also exercises malformed command lines, empty and
invalid sources, declaration-only input, and the current 25-argument selector
fallback. Update that last fixture deliberately when the supported ABI limit
changes; do not silently stop testing fallback rejection.

`--source path.c` is repeatable. Inputs must be self-contained: external
include lookup is disabled. The input arena and macro-expansion budget are
64 MiB. Use the ordinary compiler driver to measure full include closures or
other target/PIC settings rather than silently altering the workload to make
this microbenchmark accept it. Custom executable workloads must return zero
for success when `--check` is used.

## What is timed

The command preprocesses, parses, analyzes, prepares/validates canonical IR
through `ir_prepare_canonical_module`, and freezes target ABI records once.
One untimed selection replay verifies every selected machine function and
warms retained IR/metadata. A Release run then takes 30 samples; Debug takes
five. Each sample prepares fresh module type facts and lazy call plans, selects
all lowered functions with `machine_select_validated_canonical_function`, and
rewinds caller-owned function scratch after every function. Module scratch is
rewound after every module. The target is native and position-independent
selection is disabled.

The reported selection interval includes the function walk, small work-count
bookkeeping, selection, lazy call-plan construction, and arena rewind. It
excludes frontend work, canonical preparation/validation, eager ABI-record
preparation, MIR verification, allocation/placement, scheduling, encoding,
object writing, and linking. Therefore it is **warm-IR selector replay**, not
cold whole-compilation latency. In particular, a change to canonical promotion
needs its own timing; do not claim this excludes no compiler work.

`BENCH_SELECT version=1` is one whitespace-separated key/value row:

| Field | Meaning |
| --- | --- |
| `iterations` | Timed inner samples, excluding warmup/verification |
| `functions`, `fallback_functions` | Lowered functions attempted and unsupported attempts |
| `ir_instructions` | Sum of selector-reported selected typed instruction counts |
| `mir_instructions` | Sum of emitted `MachineInstruction` rows before placement/encoding |
| `min_ns`, `median_ns` | Minimum and upper-middle median selection intervals, nanoseconds |
| `prepare_median_ns` | Median eager module-preparation interval, nanoseconds |
| `total_median_ns` | Median of paired preparation + selection intervals, not sum of medians |
| `arena_bytes` | Sum of caller-owned function arena position deltas for one replay |
| `peak_arena_bytes` | Maximum of those function deltas |
| `module_bytes` | Sum of module arena deltas, including lazy plans |

`BENCH_SELECT_TARGET` records architecture, OS, CPU model and target features.
The Python report preserves every inner-summary row, object-compilation wall
time and SHA-256, compiler/source SHA-256, host, affinity, and build description.
It summarizes outer samples using the median of selection medians, divided by
`mir_instructions` to give **nanoseconds per emitted MIR row**. This denominator
is neither encoded ISA instruction count nor host retired instructions; one
MIR row can expand to multiple ISA instructions.

Arena deltas are not allocator-call counts, committed pages, total process
allocation, or peak RSS. They exclude retained frontend IR and any allocation
outside the supplied arenas. Use a separate allocator/RSS profile for those
questions; do not label these fields as total compiler memory usage.

The runner separately starts a fresh `ide cc -g0 -O2` process for each object
compilation and allocator. Its wall time includes startup, preprocessing,
analysis, canonical preparation, selection or fallback, placement, encoding,
object writing, and shutdown, but not linking. It does not drop the OS page
cache. Run `test_self_host` as well for complete compiler executable generation.

## Correctness and CI guardrails

There is no successful timing row for invalid IR, failed MIR verification,
zero functions/instructions, unsupported functions, or unstable replay work.
Declining selection coverage cannot manufacture a lower ns/MIR value. The
runner rejects malformed/version-mismatched reports, duplicate or missing
metrics, changed target/work counts, missing/empty output objects, mutated
inputs/binaries, and changed object bytes. Old output objects are removed
before every invocation. Program output comparison preserves raw bytes and
keeps stdout separate from stderr; replacement UTF-8 decoding is not an oracle.

Object identity is intentionally strict for throughput-only transformations.
An intentional change to selected code needs an independently reviewed
semantic/ABI proof and quality comparison, not a disabled hash check disguised
as a throughput win. This benchmark does not exercise placement/encoder
fallback coverage directly; inspect ordinary verbose `CODEGEN_FALLBACK` and
`CODEGEN_FALLBACK_STAGES` reports as well.

Archive JSON and fail CI on command/test/coverage failures. Do not fail a shared
runner on a percentage threshold derived from one wall-clock run. Before an
optimization is accepted, reproduce on the intended Zen 5 host with identical
build policy, controlled repeated A/B order, all allocator regressions,
self-hosting, applicable sanitizers, unchanged output/coverage, and both local
selector and end-to-end results. Record cycles, retired instructions,
branches/misses and cache counters separately when PMU access is available.
Keep the NONE allocator as a control: it bypasses machine selection, so a
similar apparent gain there is evidence against attributing the whole change
to this selector experiment.
