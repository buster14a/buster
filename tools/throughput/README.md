# Reproducible compiler-throughput experiments

This measures **Buster's implementation**, not the running time of its output.
The harness is native C with no third-party dependencies. `build.c` owns tool
and compiler construction; the standalone utility owns experimental design,
process measurement, deterministic inputs, raw data and statistical replay.
The existing compiler correctness matrix and canonical `test_self_host` gate
are unchanged. A timing result is not a substitute for either.

Dedicated Linux acceptance hosts: see [qualification and exclusive runs](DEDICATED.md)
for the opt-in machine observations, shared lease, frozen-source A/A procedure
and the distinction between software validation and physical-host acceptance.

## Start here

From the repository root, after building two uninstrumented Release compilers:

```sh
./build.sh bench_throughput self-test
./build.sh bench_throughput run --baseline /absolute/base/ide --candidate /absolute/candidate/ide --output build/throughput-comparison --baseline-id BASE_COMMIT --candidate-id HEAD_COMMIT
./build.sh bench_throughput compare --output build/throughput-comparison
```

On GitHub-hosted machines, bootstrap `build/build` with the image's Clang as
`.github/workflows/compiler-throughput.yml` does, then use `./build/build` in
place of `./build.sh`. Canonical local bootstrapping still uses TCC. On Windows
use `build.ps1` from a configured native developer shell.
The tool itself supports Linux, macOS and Windows; native harness tests run on
all three. Hardware counters currently have a Linux implementation only.

`bench_throughput self-test --sanitize` builds and runs the same native suite
with AddressSanitizer and UndefinedBehaviorSanitizer. Sanitizer construction
is also owned by `build.c`, including the Linux CI invocation.

`bench_throughput self-test` runs both the statistical/hash tests and native
integration tests, including fixed-seed corpus hashes, rejected sample paths,
deliberately invalid result bundles and timed-out children. Their expected error
diagnostics are not compiler failures.

A direct standalone build is useful when diagnosing the harness:

```sh
clang -Isrc -DBUSTER_SINGLE_THREADED=1 -std=c11 -O2 -Wall -Wextra -Werror -fwrapv -fno-strict-aliasing -funsigned-char tools/throughput/throughput.c tools/throughput/shared.c -lm -o build/throughput
```

On Windows, replace `-lm` with `-lws2_32` and add
`-Wno-microsoft-enum-forward-reference`; Psapi uses its source-level import
pragma. Compile `tests.c` in place of `throughput.c` for the native test runner. No compiler is built inside a timed observation.
Do not build, test, profile or run another benchmark concurrently on the
measurement host. Use a new output directory for every run; existing results
are not silently overwritten. Comparison never downloads historical numbers
from a different runner and never automatically updates a golden baseline.

### Real-source preflight and hosted functional admission

The opt-in `check-workload` command validates a real-source descriptor and an
already staged input tree without compiling, linking, executing or measuring
anything:

```sh
./build.sh bench_throughput check-workload tools/throughput/workloads/cjson-1.7.19.workload \
  --source-root /absolute/cjson-staging \
  --compiler /absolute/ide \
  --evidence /absolute/oracle.log \
  --evidence-outcome pass
```

Version-1 descriptors remain accepted for durable preflight receipts. The
version-2 descriptors for cJSON 1.7.19, Lua 5.4.8 and SQLite 3.53.4 bind their exact
upstream identities, complete staged-file inventory, aggregate tree hash,
requested translation-unit bytes, dependency/resource/sysroot/SDK/environment
identities, target/ABI/features, C lowerings, PIC and allocator modes,
operations, artifacts, oracle and separate ordered object, compile-link and
runtime argv templates. The
preflight independently hashes the descriptor, every declared input, the
complete tree, compiler binary and caller-supplied oracle evidence, and repeats
the identities and exact templates in its receipt.
Missing, changed, duplicated or undeclared inputs; unsafe paths; malformed
templates; missing evidence; and unknown evidence outcomes are hard failures.

This command deliberately emits `scope=descriptor-and-input-preflight`,
`admitted=false`, `performed_work=null` and `fresh_admission_required=true`.
The evidence outcome is limited to `pass`, `failed`, `inconclusive` or
`unavailable`; recording a failed oracle never turns it into a pass. In
particular, the Lua descriptor preserves run 34727853592's missing-readline
failure. A new qualification must run the existing build.c oracle after
installing the declared native dependencies before Lua can be admitted.

The branch-only `throughput-real-source.yml` workflow stages these exact roots,
runs the existing cJSON/Lua/SQLite oracles, and always writes the preflight and
outcome evidence. For each freshly passing oracle, `admit-workload` then verifies
parsed dependency, compiler-resource, system-header, SDK, sanitized-environment
and runtime-library manifests; compiles every declared source/generated file to
an object; independently compile-links the declared source list; executes that
artifact; and revalidates all identities before emitting a receipt. Expanded
absolute argv, working directories, process results, metrics and artifact hashes
are retained. Failed attempts retain their partial evidence but emit no success
receipt.

Each receipt admits only the explicitly performed Linux x86-64 baseline CPU,
direct-SSA, non-PIC, fast-allocator cells for object, compile-link and runtime
operations. The broader frontend/PIC/allocator lists remain requested descriptor
metadata, not performed work. This is hosted functional admission only: it does
not enlarge the default CI corpus, run timing samples, qualify a dedicated host,
or establish A/A or A/B performance acceptance. Those remain separate work.

## Shared library boundary

`build.c::bench_throughput_add` constructs both executables with `shared.c`,
which links the existing arena, integer, string, OS, file, hash and time modules.
It does not link compiler, target-detection, application-entry or UI modules.
The desktop combination matrix runs the native integration suite on each host.

Experiment arrays and frozen-tree entries have independent arena lifetimes;
short-lived paths use scratch arenas. Paths and unsigned integers use bounded
`String8` APIs. `os_path_absolute_lexical` supports not-yet-created outputs;
`os_make_directory_attempt` preserves owner-only POSIX directory creation.
Checked streaming reads distinguish EOF from failure. `file_copy` now uses the
same recoverable transfers, and SHA-256 lives in the shared hash module with
known-answer and chunk-boundary tests. `buster_hash_64` is unchanged.

The harness retains corpus recipes, file/line census, sorted frozen-tree
identity, statistical/replay rules, JSON/CSV/Markdown serialization and the
cooperative dedicated-host lease. Report serialization still uses buffered
stdio and its existing decimal formats (including `%.17g`), preserving schema
bytes and close-time error detection. This is experiment output policy; common
binary copying and hashing use shared file IO.

`tp_process` still owns the measurement lifecycle: suspended Windows children
and kill-on-close jobs; POSIX process groups, pre-exec counter attachment,
blocking `wait4`, signal deadlines and descendant cleanup; affinity, per-child
CPU/RSS and diagnostics. The ordinary spawn/wait API does not provide these
contracts. Windows argv conversion now uses the shared UTF-16 command builder.
Timestamp calls stay immediately before process creation and immediately after
the native wait, with conversion through `timestamp_ns_between`. Path/argument
allocation and report/hash work stay outside that interval. No change selects
or rebuilds a different baseline/candidate executable.

## Workload contract

All synthetic files record a specified seed and SHA-256 digest. The original
six recipes use xorshift32; the recovered recipes below enumerate indices. There
are no system includes, timestamps, random device reads, undefined signed
arithmetic, or unbounded runtime loops in these generated sources. Functions
have external linkage; volatile globals and opaque calls prevent the principal
work from disappearing merely because it is unused.

| Case | What it stresses | `ci` profile |
|---|---|---|
| `tiny_startup` | New-process startup, metadata initialization, one compile and object write | One five-line function/file |
| `large_function` | One large body, expression/IR traversal and allocator scaling | 1,024 dependent statements |
| `many_functions` | Per-function setup and teardown | 512 small definitions |
| `symbol_table` | Long common-prefix names, declarations and permuted lookups | 2,048 referenced volatile globals |
| `control_flow` | Diamonds, loops, switches and joins | 64 functions, 16 diamonds each |
| `backend_pressure` | Live ranges, opaque calls, call clobbers, operand constraints and emission | 64 functions, 32 live values, eight call/update rounds |

`smoke` divides the non-tiny counts by eight. `full` multiplies them by eight.
`--scale N` multiplies again (1–64). `--seed N` changes the specified seed.
The metadata/CSV report exact bytes, lines and source definitions, rather than
estimating functions by counting braces. Full profiles can expose genuine
pathological compiler scaling; a timeout is a failed experiment, not a sample
that may be deleted to make the compiler look fast.

### Optional macro-expansion and aggregate-ABI inputs

The default remains the six cases above, with identical generated bytes and
the same predeclared CI guard. `--workload NAME` is repeatable: the first
selection replaces the default set, later selections add to it, and duplicates
are ignored. `default` selects the original six; `all` selects all eight.
Jobs always follow the table order, then the two cases below, regardless of
option order. Selecting one case produces the same source as selecting it
alongside others. Unknown or empty names are errors.

| Optional case | Recipe | `smoke` definitions | `ci` definitions | `full` definitions |
|---|---|---:|---:|---:|
| `macros` | Token-paste `CAT`, nested `TWICE`/`STEP`, one function per `MAKE(n)` | 256 | 2,048 | 16,384 |
| `aggregate-abi` | Distinct `struct S<n> { long x,y; }` types, each passed and returned by value | 128 | 1,024 | 8,192 |

Counts multiply by `--scale N`. Macro source has six header/definition lines
plus one line per expansion; aggregate source has one header line plus one
line per struct/function pair. Function counts describe definitions **after
preprocessing**, including macro-generated definitions. `long` retains the
target's native width: the aggregate is 16 bytes on LP64 and 8 on Windows
LLP64. This workload exercises aggregate argument/return lowering, not an
exhaustive ABI correctness oracle.

```sh
# Generate only the recovered cases at their original population sizes.
./build.sh bench_throughput generate --output build/recovered-inputs --profile ci --workload macros --workload aggregate-abi
# Compile matched A/B inputs in every allocator mode, requiring equal artifacts.
./build.sh bench_throughput run --baseline /base/ide --candidate /candidate/ide --output build/recovered-comparison --profile ci --workload macros --workload aggregate-abi --mode all --pairs 2 --warmups 1 --no-guard --require-identical-output
# Add the recovered cases to the ordinary corpus for a diagnostic experiment.
./build.sh bench_throughput run --baseline /base/ide --candidate /candidate/ide --output build/all-workloads --workload all --no-guard
```

Use the same Clang-built compiler for A and B first to qualify collection.
Two pairs are a compilation/determinism smoke check, not performance evidence.
`--artifact assembly` also supports these inputs. No generated program is
executed: source generation, compilation, output hashing and raw-sample replay
use the existing native harness. Metadata adds the ordered `workloads` array;
each job still records its mode, artifact kind, source SHA-256, source bytes,
physical lines and defined functions. Raw CSV repeats the work denominators
and artifact hash for every observation. Result schema 2 and input schema 1
remain unchanged; existing schema-2 bundles remain readable.

Any nondefault workload set requires `--no-guard` for `run`. Optional results
cannot silently replace or enlarge the predeclared CI family. Promoting a
recovered case into that family requires a separate measured decision covering
per-mode completion, runtime/resource budgets, noise and statistical-family
effects on the CI runner. This change makes no compiler speedup claim.

Provenance: [issue #346](https://github.com/buster14a/buster/issues/346), from
the *Optimize Compiler Allocations* recovery. The verified
[allocation transport manifest](https://github.com/buster14a/buster/blob/7b0e23ea24cc3233a91a67386122f85fdb6b1f7b/.github/workflows/allocation-transport.yml)
identifies `.github/allocation-transport/part0` through `part3`; concatenating
and decompressing them yields patch SHA-256
`9793a8a47882ff875a1966ee6f2919f73fc8ca88ef07d2a5ea337413c9753271`.
Its `tools/allocation_census.py::corpus` supplies both recipes and the original
2,048/1,024 populations. The native port adds the standard identity comment
and profile scaling; macro functions use unsigned arithmetic, and aggregate
addition converts through `unsigned long` before conversion back to `long`,
avoiding signed overflow while preserving the two-`long` ABI shape. The seed
is recorded in the comment but does not randomize these enumerated recipes.
The obsolete Python compiler/benchmark runner is not restored. Native tests
pin independently reconstructed source hashes, bytes, lines and definitions
for all three profiles, plus selection invariance and scaled counts.

Capacity is derived as eight workloads × four allocator modes + two optional
self-host stages = 34 jobs. The selected cross product is checked before any
job write, and job/first-observation records use one heap allocation each,
sized to the selection, instead of growing the Windows stack. Replay storage
is also bounded and heap-backed; at 34 jobs and the maximum 256 pairs it uses
`34 * 2 * 2 * (256 + 3) * sizeof(TpRow)` bytes for timing/probe records
(11,553,472 bytes with a 328-byte row), plus small order/name buffers.
`throughput-tests` reports the host's actual record sizes. Tests enumerate
every nonempty workload/mode subset with and without the stage pair and
reject undersized capacities before touching storage.

```sh
./build.sh bench_throughput generate --output build/throughput-inputs --profile full --seed 20260907 --scale 2
./build.sh bench_throughput run --baseline /base/ide --candidate /candidate/ide --output build/throughput-smoke --profile smoke --mode all --pairs 2 --warmups 1 --no-guard
```

Default modes are `none`, `mir-stack`, `fast` and `quality`, kept as **separate
series**. `--mode` selects one mode or `all`. Ordinary workloads use `cc -c
-g0 -O0 -fregister-allocator=MODE`. `--flag ARG` appends a common compiler
argument; it may not override the operation, output, metrics path or allocator.
The selected allocator is appended after every optimization flag because
Buster processes options in order and `-O` options reset that selection.
Keep target, optimization, debug and feature flags identical across A/B and
retain all include/SDK dependencies used by additional flags. The exact argv
and working directory of every invocation are recorded without shell parsing.

Tiny startup is **warm-filesystem, fresh-process latency**, not a cold disk
cache experiment. Every invocation is a new compiler process, including every
warmup and sample. The harness does not drop host caches, disable security
controls, change the governor or attempt privileged machine configuration.

### Assembly-output experiments

`--artifact object|assembly` selects the final artifact for the selected ordinary
workload classes. The default is unchanged: `-c` with a `.o` output. Assembly
mode uses `-S` with a `.s` output and adds `/assembly` to each ordinary series
name in `jobs.tsv` and the comparison summaries. Each metadata job explicitly
records `object`, `assembly` or `executable`. Source generation, source hashes,
mode selection, pair counts and statistical thresholds are unchanged.

```sh
./build.sh bench_throughput run --baseline /base/ide --candidate /candidate/ide --output build/throughput-assembly --artifact assembly --mode all --require-identical-output
./build.sh bench_throughput compare --output build/throughput-assembly
```

Use the same Clang-built compiler for both inputs first to validate A/A
collection. Then compare frozen baseline/candidate compilers on an idle runner.
For dedicated 9700X calibration, also retain the
[same-source cross-build controls](DEDICATED.md#same-source-cross-build-controls)
so build-root sensitivity is visible beside immutable-binary A/A noise.
The existing tiny-startup case retains the small-output control. For the
many-function scaling series, `--profile ci --scale 2`, `4`, `8` and `16`
produce 1,024, 2,048, 4,096 and 8,192 definitions respectively. Every run still
includes the default six workload classes unless `--workload` selects another
set; use a different output directory at each
scale. Large cases must finish under the existing deadline, not be discarded
or assigned a larger timeout merely to obtain a passing result.

This times the whole compiler process through final assembly-file generation,
including any printer index/sort construction, scratch management and writes.
It does not isolate printer time or establish a speedup by itself. Existing
`--pmu` and allocation options reuse the same assembly command through separate
diagnostic replays; their output must match the ordinary timing artifact.
Retain both assembly and ordinary object comparisons. Hardware requirements,
paired uncertainty, source/binary hashes and unavailable-counter rules remain
those documented below. An absent regression verdict is not proof of a win.

With frozen self-host inputs, stage 1/2/3 remain executable compiler builds:
assembly mode applies only to ordinary jobs. `--flag -S`, `--flag -c` and
`--flag -E` remain forbidden. No assembler or generated program runs inside the
timing samples. Exact A/B assembly hashes do not replace independent assembler
checks or the compiler's full, mode, sanitizer, platform and self-host gates.
Crafted empty/tied-symbol/relocation cases and printer phase attribution remain
separate correctness/profiling work for #116; no new compiler timing hooks or
parallel measurement framework are introduced here.

## Measurements and their limits

`wall_seconds` spans process launch through wait completion. User and system
CPU seconds come from the operating system; `cpu_seconds` is their sum. Output
size is the produced file size, not a guessed `.text` size. Source throughput
uses actual lexed bytes/lines from `-fsource-metrics`; functions/second uses
known generated source definitions, not an assertion that all functions were
emitted. Self-hosting leaves function throughput unavailable and uses actual
lexed lines instead.

Peak RSS is the OS child high-water measurement: `wait4` on Unix (KiB converted
to bytes on Linux; bytes on macOS), and `PeakWorkingSetSize` on Windows. It is
**not** reserved address space, a sum of allocation sizes, or simultaneous
process-tree RSS. The resource APIs' child accounting semantics differ; do not
compare RSS across operating systems. Integration tests allocate/touch/free a
large buffer, then run a smaller child, checking that peaks are not a cumulative
maximum of previous trials. Compiler subprocess groups/jobs are bounded and
cleaned up on failure or timeout. All compiler work must finish before its
process exits; orphaned helpers are not valid benchmark work.

Linux `--cpu N` pins each child to one permitted logical CPU. `--cpu auto`
selects the first permitted CPU (also supported on Windows); CI uses this policy. Arrange an idle
physical core and idle SMT sibling for dedicated Zen 5 acceptance work; affinity
alone does not reserve either. An unsupported or invalid affinity request fails
instead of silently becoming unpinned. Without the option, affinity is inherited.
The bundle records CPU model/microcode, kernel, effective cpuset, cgroup quota,
SMT/governor information where readable, relevant environment values and binary
hashes. Missing platform facts are not invented. Revision labels are supplied
provenance, not a cryptographic proof of the source used to build a binary.
Retain build commands, compiler versions and CMake caches beside local bundles;
the CI workflow uploads both build caches and compile command databases.

### Hardware counters: separate diagnostic replays

```sh
./build.sh bench_throughput run --baseline /base/ide --candidate /candidate/ide --output build/throughput-pmu --cpu 4 --pmu
```

Choose a CPU allowed by your host rather than copying `4` blindly. The collector
uses `perf_event_open`, so no `perf` executable is required. It requests cycles,
retired instructions, branches, branch misses, cache references and cache misses.
These are Linux generic **user-space** events, inherited by descendants, not a
claim that generic `cache-misses` is an L1D, L2, instruction-cache or Zen 5 IBS
measurement. Counters are attached before exec, enabled on exec, and accepted
only when running for at least 90% of enabled time. Accepted values are scaled;
raw availability errors and scheduling fractions remain in `capabilities.jsonl`.
`--require-pmu` makes unavailable/insufficiently scheduled events an error.

Counter setup and reading are **not on the guarded timing path**. Three separate
replays per variant/case are retained in `telemetry.csv` and summarized under
`telemetry` in `summary.json`. These are diagnostic medians, not adequately
powered independent regression tests. Permission-denied, unsupported and
multiplexed-out observations are `NA`/`null`, never fabricated zero counts.

### Allocation requests: opt-in instrumented compilers

```sh
./build.sh generate --build-directory build/alloc-probe --ci -- -DBUSTER_BENCH_ALLOCATIONS=ON -DBUSTER_INCLUDE_TESTS=OFF
./build.sh build --build-directory build/alloc-probe --config Release -t ide
./build.sh bench_throughput run --baseline /base/ide --candidate /candidate/ide --allocation-baseline /base-instrumented/ide --allocation-candidate /candidate-instrumented/ide --output build/throughput-allocations
```

`BUSTER_BENCH_ALLOCATIONS` is OFF by default and preprocessing removes the hook
from normal allocation paths. The diagnostic build counts **calling-thread arena
bump requests and requested bytes**, including repeated allocations after arena
rewinds, excluding alignment padding. These are not libc `malloc` counts, OS
reservation/commit counts, peak live allocation, or all-thread totals. With opt-in `-fcompile-jobs=N`, these calling-thread keys exclude worker
bumps and must not be labeled whole-compiler allocation totals. The separate
exit census aggregates worker records; its snapshot and teardown scope differ. The metrics snapshot precedes report
formatting so the report does not count itself. Zero-size requests count as calls.

The two new fields are `allocation.arena_calls` and `allocation.arena_bytes` in
the existing extensible source-metrics file. Separate probes must emit these
fields and must produce exactly the same output hash as their uninstrumented
counterpart. Instrumented compilers are rejected as timing baselines/candidates.
For an old revision without the hook, backport only this diagnostic hook into a
separate probe build; do not measure that modified build as the timing baseline.

### Canonical construction census in allocation probes

The same `BUSTER_BENCH_ALLOCATIONS` build emits `ir_construction.*` fields
in `-fsource-metrics`. Use the existing allocation-probe arguments above;
normal timing compilers must remain uninstrumented. Raw metric files are
retained in the native harness artifacts even though its allocation summary
contains only arena calls/bytes. There is no second measurement runner.

`version=1` identifies the construction vocabulary. `overflowed=1` invalidates
the census: counters saturate rather than wrapping. Counts are cumulative
**calling-thread** totals since process start, including failed attempts
that reached each hook. They are not all-lane aggregates. The compiler is
currently serial; future parallel compilation must explicitly aggregate
lane-owned results. The snapshot precedes report formatting and resets
nothing. No extra arena storage or whole-function row stream is retained.

- `block_appends`, `value_appends`, `instruction_appends` count successful
  checked-builder appends, including later-retracted rows; direct writes
  to arrays do not count as appends.
- `*_grows`, `*_rows_copied`, `source_rows_cleared` count builder growth
  and copied/cleared rows, not capacity bytes or peak resident memory.
- `function_starts`, `body_tokens`, `parameters`, `initial_*_slots` count
  C body-lowering starts and initial estimates, once per started function.
- `ssa_finish_calls`, `ssa_finish_failures`, `before_ssa_*_rows`,
  `after_ssa_*_rows`, `final_*_slots` describe frontend finish entry/exit,
  including failed finish calls. Earlier failures have no exit snapshot.
  This is not general canonical finalization or later promotion.
- `place_load_retractions`, `place_atomic_load_retractions`,
  `ssa_read_retractions`, `place_repair_steps` count recovered memory
  loads, SSA read aliases, and previous-instruction traversal steps.
- `cfg_*` count shared native edge-builder calls/failures, scratch `u32`
  slots, target visits, attempted unique source/target pairs, parameter
  visits, incoming nodes examined including matches, and emitted copy
  sources. Direct/non-native consumers do not call this builder: zero
  means no work at this hook, not absence of all CFG work.
- `operand_slots_appended` sums appended rows' operand counts. It does
  not count unique operands or repeated downstream decoding passes.

The additive direct-SSA census for #447 separates work inside `c_ir_ssa_*`:

| Fields (under `ir_construction.ssa_`) | Counted work |
| --- | --- |
| `slot_lookups`, `slot_probes`, `slot_inserts` | Current-value queries, examined hash slots (including terminal slots and insertion retries), and new keys. |
| `slot_grows`, `slot_grow_visits`, `slot_rehash_probes`, `slot_clear_bytes` | Table allocations, old-capacity slots scanned, rehash probes, and bytes explicitly zeroed. |
| `finish_slot_grows`, `finish_slot_probes` | Subsets after predecessor offsets are installed, including propagation; exclude earlier lowering and CFG discovery. |
| `cfg_target_visits`, `reachable_target_visits` | Targets examined by the two predecessor passes and by reachability, respectively. |
| `pending_visits`, `pending_predecessor_visits`, `forward_steps` | Pending propagation queue entries, incoming predecessor rows materialized, and single-predecessor path steps. |
| `simplify_passes`, `simplify_block_visits`, `simplify_empty_block_visits`, `simplify_parameter_visits`, `simplify_incoming_visits` | Fixed-point sweeps and visited blocks/parameters/incoming rows, including revisits and the initial active-block census. Empty-block visits are a subset of block visits. |
| `value_scratch_bytes`, `value_clear_bytes`, `replacement_rows` | Value-count-sized table allocation requests, explicit memset bytes for those tables, and identity-map initialization rows. These exclude block-sized scratch, restoration tails, and sparse slots. |
| `initialization_work_visits`, `live_work_visits` | Values popped from the definite-initialization and live-parameter queues. |
| `remap_value_rows`, `remap_instruction_rows`, `remap_operand_slots`, `remap_incoming_visits` | Rows visited by the three value compaction passes and final instruction/operand/incoming remapping. |

These share the existing saturation, calling-thread and failed-attempt rules.
They do not add timers, histograms, per-function storage or a reporting switch.
Explicit clear bytes exclude ordinary map writes and allocator-internal clears.

The `validation_*` and `preparation_*` fields attribute the canonical boundary.
`validation_calls` counts complete module-verifier entries. The ownership fields
count the preliminary function scan, published-CFG checks, lowered functions,
blocks, instruction-chain steps and owner-map clear bytes. The remaining fields
count globals/relocations and their overlap pairs, aliases, initializers, value
and provenance visits, block parameters and incoming values, instruction,
operand, target and result checks, opcode-operation checks, conversions,
calls/fixed arguments, provenance-bearing opcodes and terminator checks.
`preparation_*_validations` separates the four verifier call sites: uncertified
input, changed promotion output, certified FAST input, and changed FAST output.
The other preparation fields count lowered functions offered to promotion,
FAST, and dense-CFG publication. Successful validation therefore normally
visits each instruction once for ownership and once for opcode semantics; those
visits prove different invariants and are not a measured redundancy. Counters
record work reached before the first validation error, so compare successful
inputs when using totals as complete populations.

Initial estimates and finish populations cover different failure boundaries;
do not subtract them blindly on invalid sources. Counts do not measure
append latency, phase times, retained arena memory, or all-consumer cost.
Reuse per-site allocation diagnostics and uninstrumented paired experiments.
A smaller count is not a speedup. Normal builds preprocess recording calls
away and retain neither counter storage nor reporting API. Row layout,
IDs, source/label provenance, and arena lifetimes are unchanged.

## Frozen-source self-host stages

```sh
./build.sh bench_throughput run --baseline /base/ide --candidate /candidate/ide --output build/throughput-self-host --profile full --self-host-root /frozen/buster --self-host-generated /frozen/buster/build/generated
```

Configure a frozen worktree once to obtain generated headers. Both compilers
compile **the same frozen unity source and headers**, not their respective
moving source trees. Stage 1 measures each trusted host compiler. Stage 2 uses
the stage-1 outputs as compilers and is a separate series. Full source/header
trees are hashed before/after; symlinks and special files are refused. System
headers/SDKs must also remain fixed and their build provenance must be retained.

A frozen source tree may differ from either original compiler revision. The
appropriate executable fixed point is then **stage 2 == stage 3**, checked for
each variant after measurements. This is deliberately distinct from Buster's
canonical own-source `test_self_host`; the latter remains unchanged and required
for compiler changes. Repeated outputs within every series must be identical.
`--require-identical-output` additionally requires A/B byte equality; leave this
off for intentional generated-code changes and run correctness tests separately.

With allocation probes, the harness also builds a separately instrumented
frozen stage-1 compiler outside timing trials, then uses it to measure stage-2
allocation requests. Instrumentation is not inferred from the parent compiler.
The instrumented frozen source must contain the hook. Unix self-host commands
include `-lm`; macOS SDK/framework and Windows system-include arguments must be
supplied to match the canonical platform command in `docs/agents/build.md`.

## Statistical comparison and the CI guard

Each of two **predeclared rounds** contains at least 20 adjacent A/B pairs per
workload/mode. Within two-pair blocks, one pair is AB and the other BA, with the
first order chosen by the recorded seed. Workload order is also shuffled. Two
warmups per variant/case precede timing. All raw observations remain; no
best-of-N selection, outlier trimming, selective cancellation or rerun-until-green.

The exact one-sided paired sign test asks whether a majority of pairs crosses
**both** a relative and an absolute practical margin:

* Wall time: candidate exceeds baseline by 15% **and** 2 milliseconds.
* Peak RSS: candidate exceeds baseline by 20% **and** 16 MiB.

Bonferroni corrects the nominal family alpha of 1% over every workload/mode and
both guarded metrics. The same metric/case must be significant in **both rounds**
to fail. Medians, median paired ratios, relative median absolute deviations and
conservative distribution-free median-ratio confidence intervals are retained.
The sign-test error model assumes independent pairs; shared-host contention and
serial correlation can violate that model. These nominal levels are not a
universal false-positive guarantee. Run A/A experiments on the intended runner
before tightening thresholds or adjudicating small changes.

A broad interval or a one-round-only signal is **inconclusive**, reported as a
warning rather than equivalence or a hard CI failure. `--no-guard` explicitly
marks small smoke experiments as diagnostic. Standalone utility exit statuses: 0 = no confirmed
regression (inspect warnings), 1 = confirmed substantial regression, 2 = invalid
or incomplete experiment. A missing/failed compiler, timeout, stale artifact,
missing metrics, nondeterminism, changing inputs, or corrupted result bundle can
never be discarded into a passing timing result.

CI builds both immutable PR revisions on one runner, sequentially, using the
same Clang/build-driver policy and generated workload. It runs all 24 synthetic
case/mode combinations with two 20-pair rounds. It does not cache compilers,
reuse another machine's timing baseline, weaken existing tests, or run untrusted
code under `pull_request_target`. Permissions are read-only and checkout
credentials are not persisted. Full self-host/PMU/allocation investigations are
explicit local or dedicated-runner experiments, not hidden costs in every PR.
The weekly run is an A/A health check; manual dispatch accepts a baseline ref.

### Native-retirement statistics (version 1)

`retirement_stats.h` is a separate opt-in method for the approved
`native-retirement-performance-v1` contract. It does not change the ordinary CI
guard above. A caller must freeze the two family-partition sizes, exactly two
equal rounds, an even sample count of 60--256 pairs per round, at least 100,000
resamples, and one 64-bit seed before collecting data. The three fixed analysis
scopes are `round-1`, `round-2`, and `pooled`, matching
`native_retirement_performance_binding.py:STATISTICAL_SCOPES`. Replay rejects an
unfrozen plan, missing or extra ratios, a round with a different count, a
shortened resample workspace, zero, missing or non-finite input, and values
beyond its declared caps. The surrounding evidence validator remains
responsible for proving that the declaration preceded collection, deriving the
counts from the immutable manifest, assigning every member exactly one stable
index, and invoking every declared member exactly once.

Input for one family member is cell-major `[cell][round][pair]` paired
candidate/baseline ratios. Every two adjacent pairs are an indivisible AB/BA
block. For that block, the statistic is the equal-weight geometric mean of both
pair ratios and every included cell; this gives each required cell and both
orientations the same fixed weight. A round estimate is the median of its block
statistics. The pooled estimate is the median of both equal-sized round strata.
A slice or overall aggregate precomputes these block statistics in
`O(cells * pairs)` time, then each bootstrap replicate selects whole blocks in
`O(pairs)` time, uses one selected block across all cells, and retains both
ratios. Round analyses draw only from their round. Pooled analysis draws within
each round independently and combines the two strata without changing their
equal weight. Thus replay is
`O(cells * pairs + resamples * pairs + resamples * log(resamples))` rather than
`O(cells * resamples * pairs)`, with one resample workspace independent of the
cell count. No observation is trimmed, replaced or selected by its result.
The published point estimate in each scope is therefore a median of declared
block geometric means. This is the version-1 estimand, not an algebraic claim
that a median commutes with a geometric mean: on crossed or skewed cells it can
differ from the geometric mean of per-cell medians. Equal fixed cell weight is
provided inside every block statistic, before the predeclared median operation;
the evidence must publish that block-statistic median for each round and pooled
scope rather than silently substituting the other nonlinear ordering.

The complete declared family contains all wall-time, RSS and generated-runtime
overall aggregates, required slices and cells. It is split before measurement
into an aggregate/slice bootstrap partition and an exact-cell partition. Each
partition receives half of family alpha 0.05. Within a partition of `N` members,
each upper or lower bound in each of the three scopes uses one-sided tail alpha
`0.05 / (12 * N)`: two partitions times two directions times three scopes times
`N` members. The union bound is therefore at most 0.025 per partition and 0.05
over both partitions. This stronger weighted Bonferroni construction never
silently omits cells while retaining enough Monte Carlo tail resolution for the
bounded bootstrap partition. Resampled statistics use at least 100,000 draws,
are sorted, and use inverse empirical CDF/type 1: quantile `p` is
`sorted[ceil(p * resamples) - 1]`, clamped to the available ranks. The bootstrap
partition is capped at 80 members per scope, which leaves at least 5.208 expected
draws in each corrected tail at 100,000 resamples. The frozen binding axes have
at most 25 explicit members per metric (one aggregate, 12 targets, one
`baseline` CPU, four allocators, two frontends, two PIC modes, and three
artifact stages), hence at most 75 across the three variable metrics.

Both `N` values are per-scope cardinalities derived without producer totals.
For each named scope, bootstrap `N` is the count of that scope's aggregate and
slice entries in `statistical_family.members`; exact-cell `N` is the sum of that
scope's three variable-metric `cell_counts`. The immutable binding repeats the
same member identities across all three scopes. If independently derived scope
counts differ, or either plan count differs from them, version-1 replay is
invalid. This makes the extra factor of two in `12 * N` unambiguously the two
predeclared partitions, rather than an accidentally duplicated scope or tail.

Cells use a separate exact distribution-free order-statistic interval, matching
`stats.h`'s binomial construction but applying it to the independent two-pair
block statistic. It consumes no resample workspace and has no Monte Carlo
resolution limit. Round intervals use the round blocks directly. The pooled
point estimate is the median of both equal-size strata, but its interval does
not assume identical round distributions: each component round uses half the
pooled tail allocation and the pooled interval envelopes the two component
intervals. An equal-mixture median lies between its component medians, so this
inner union bound is distribution-free and costs no more than the declared
pooled tail allocation. An unattainable finite endpoint is honestly reported as
zero or infinity. The exact partition supports 300,000 metric cells per scope.
The current full census replay retains 78,912 canonical rows, of which 72,672
are compiler eligible and 6,240 are untimed. The 100,000-cell cap therefore
leaves explicit room for admitted real-workload rows,
and the 300,000-member cap is exactly three variable metrics for that maximum
population in each scope. These caps do not reduce resamples or authorize excluding a
required cell. The caller derives the bootstrap count from one scope's explicit
aggregate/slice members and the cell count by summing that scope's
variable-metric `cell_counts`; a mismatch or a larger final manifest is invalid
evidence and requires a reviewed version change, not truncation.

A pass requires both rounds and pooled upper bounds at or below the applicable
limit; a regression requires both rounds and pooled lower bounds above it. All
other valid results, including round disagreement, a broad interval, or an
infinite endpoint, are inconclusive. Any validation failure is invalid. Exact
code-section byte ratios remain outside this sampled method.

Replay and ordering use SplitMix64 with the constants and coordinate mapping in
`tp_retirement_seed`. Bounded choices use rejection sampling rather than modulo
bias. `tp_retirement_block_schedule` defines each cell's first-pair AB/BA
orientation and separate Fisher-Yates cell orders for the two pairs from one
`(seed, round, block, cell_count)` tuple; each cell takes the opposite
orientation in the second pair. The schedule and bootstrap domains are stable
named protocol constants. Bootstrap streams are separately derived from `(seed,
bootstrap-domain, metric, family member, round-or-pooled)`. The native harness
test fixes independently generated nonconstant-data draw, pooled-statistic, and
quantile answers and exercises unchanged data, true regressions, broad
intervals, disagreeing rounds, both family corrections, aggregates, malformed
declarations, fixed sample counts, and supported caps. This software test is not
dedicated-host A/A admission and issues no #36 performance verdict.

### Native-retirement invocation evidence

`retirement_execution.h` supplies the native cursor and bounded invocation
encoder for the existing performance binding's execution transcript. The cursor
uses `tp_retirement_block_schedule` directly, includes two warmups per variant,
and exhausts both rounds of compiler invocations before the native-runtime
campaign. The service supplies the independently authenticated eligible-row
projection in canonical census order. The cursor copies that sparse row-ID map
and the runtime subset at initialization; it schedules only eligible compiler
rows while retaining their original IDs in the invocation transcript and
result-input records. A duplicate, unsorted, or out-of-range ID is rejected.
A failed commit
permanently invalidates the attempt; there is no skip or resume operation. The
complete population's result-input capacity permits at most 254 pairs per round,
so this collection boundary rejects 256 even though the statistics kernel can
analyze that count for a smaller population. It does not change statistical
limits, family construction, or decisions.

Linux `tp_process_observe` captures a fresh child's PID and `/proc/PID/stat`
start token while that child is waiting for launch permission. It records the
same monotonic interval used for wall time, and retains ordinary wait status,
timeout and RSS evidence. Other Unix platforms reject requested process
observations as unsupported. The existing `tp_process` entry point retains its
ordinary throughput behavior and does not read process identity. Diagnostic PMU
collection remains separate.

`retirement_measurement.h` provides the Linux observation boundary for an
already verified service plan. It hashes a read-only executable once before
timing, retains its open descriptor and metadata identity, and launches that
descriptor with `fexecve`. The command digest covers canonical ASCII JSON with
the keys `argv`, `cwd`, and `environment`; arguments and sorted, unique
`NAME=value` entries have a combined 64 KiB bound. There are at most 256
arguments and 128 environment entries. The inherited environment is excluded.
The supplied cwd descriptor must match the named working directory; its source
tree still requires the service's independent immutable-closure verification.

The optional `TpProcessInputs` path uses the existing process observer with PMU
disabled, an empty service-owned log, explicit environment and null stdin.
Linux `close_range(CLOSE_RANGE_CLOEXEC)` prevents unrelated supervisor handles
from reaching the child, including handles the caller forgot to mark CLOEXEC.
A kernel that cannot perform that operation fails the invocation. This does
not install the service sandbox or acquire its lease.

Before compiler launch, the artifact must be absent from the service-opened
private output directory. Afterwards, the producer opens it relative to that
descriptor without following links. Runtime output is read from the actual
child's log descriptor. Both paths hash a regular, single-link file, bounded to
1 GiB, with identity/size/metadata checks around the read. The observed digest
must equal the independently prepared oracle. After timing, an independent
reader in `retirement_artifact.h` parses each compiler artifact and checks the
code count and digest against the frozen plan. Correct whole-file hashes cannot
authorize incorrect code metrics. This structural check does not establish
semantic correctness or native-runtime eligibility.

The reader handles little-endian x86-64/AArch64 ELF64 objects and executables,
COFF objects, PE32+ images and Mach-O64 objects and images. It counts ELF
`SHF_EXECINSTR`, COFF/PE code or executable sections, and Mach-O instruction or
symbol-stub sections. Code digests concatenate payloads in ascending file-offset
order. PE file-alignment padding beyond a nonzero virtual size is excluded;
headers, relocations and data sections are excluded in every format. Zero code
bytes remain an explicit empty-code fact. Unsupported formats, missing section
tables, truncated/overflowing ranges, overlapping payloads or metadata, and
executable zero-fill sections fail closed. Parsing has a 1 GiB artifact limit
and 65,535-section limit, uses bounded iteration without recursion, and reads
descriptor input into owned memory so truncation cannot fault a mapping. Census
validation uses the same reader and additionally checks object format and CPU
against the declared target before accepting a supported row.

Only successful execution and output verification advance the attached sample
collector. `TpRetirementMeasurementResult` retains the process identity, wait
status, timeout, observed output digest/size and failure stage for the service's
failure recorder. A failure poisons the attempt. The helper does not delete
logs or artifacts; the service must retain failures, retire successful scratch
files before reuse, and seal evidence durably. It also owns cancellation,
descendant absence proof and quiet-phase scheduling; these local observations
are not authenticated service receipts.

The native regression runs a complete one-row fixture through 488 fresh
compiler/runtime child processes (two warmups and two 60-pair rounds per
variant), then writes 120 numeric records. These deterministic fixture
programs are functional tests, not compiler-performance measurements. Python
independently checks canonical command hashes, every output identity, the
schedule, process instances, and all numeric joins. Failure controls cover
nonzero exit, timeout, wrong/missing compiler and runtime output, stale output,
symlinks/hard links, changed binaries, command/cwd mismatch, inherited handles,
ambient environment, and retry after failure.
Artifact cases also reject incorrect code sizes/digests and malformed output
whose whole-file hash nevertheless matches the supplied oracle. Format tests
cover both architectures, every truncated fixture prefix, reversed section
order, overlap, empty code, PE padding and the actual host test executable.
Independent Python checks decode the saved fixtures without this C reader.
For a parsed zero-byte candidate section, the invocation retains the empty
SHA-256 and the paired numeric record retains `0` against a positive baseline.
If the baseline has zero code bytes, both parsed code observations remain in
the invocation transcript but the numeric code ratio is absent because it has
no denominator. Native fixtures exercise both cases with actual child output.

The encoder emits the existing canonical JSONL invocation schema in at most
8,192 bytes. It checks successful child status, required hashes, exact interval
agreement, compiler RSS, and code-section applicability before emitting bytes.
Nanosecond serialization uses integer operations and a bounded decimal domain;
missing runtime RSS is `null`. The native regression fixture is read unchanged
by the production Python transcript validator, including the sample join:

```sh
./build.sh bench_throughput self-test
python3 -W error::ResourceWarning tools/throughput/retirement_execution_test.py build/throughput-tool-tests
./build.sh bench_throughput self-test --sanitize
python3 -W error::ResourceWarning tools/throughput/retirement_execution_test.py build/throughput-tool-tests-sanitized
```

`TpRetirementTranscript` couples a successful checked write to advancement of
that cursor. Shards contain 32,768 records, except for the last shard, and are
bounded to 64 MiB each and 4,096 shards overall. A shortened intermediate shard,
overlapping interval, duplicate observation, write/flush error, or premature
completion permanently invalidates the transcript. A descriptor is returned
only after the shard's stream flush succeeds. The caller owns file creation,
fsync, no-replace publication and final immutable revalidation; a returned
SHA-256 descriptor establishes local byte integrity, not receipt authority.
After complete collection, `tp_retirement_transcript_receipt` writes the
canonical bounded invocation receipt once, joining the frozen plan and context
digests to every published shard descriptor. A failed or repeated write poisons
the attempt. Its returned digest needs separate, authenticated publication by
the service; the result bundle cannot supply its own trust anchor.

These primitives are not an admitted service recipe or an authenticated receipt.
The service must still own the immutable plan, launch isolation, independent
output checks, shard publication, lifecycle and receipt authority. The
`native-retirement-performance-v1` descriptor remains blocked until that complete
producer is integrated; `validate-buster-v1` continues to produce smoke evidence.

## Result bundle

`metadata.json` contains schema, workload/binary hashes, flags, selected environment
and scope declarations. `jobs.tsv` names series. `samples.csv` contains all timing
observations; `telemetry.csv` contains PMU (`round=0`) and allocation (`round=1`)
replays, never ordinary timing rounds. `commands.jsonl` records exact commands;
`capabilities.jsonl` records process status and PMU errors/fractions. Compiler logs,
metrics and last outputs live in `artifacts/`. Per-sample output hashes remain
in CSV even though the last output file is reused.

A completed run seals the six primary machine-readable evidence files with
SHA-256 in `complete.txt`. Comparison rechecks the seal, strict row counts,
unique pair slots/order positions, numeric validity, invariant workload units
and repeat output hashes, then regenerates `summary.json` and `summary.md`.
These two reports are derived outputs, not retained evidence: comparison removes
old reports before validation and discards newly written reports on validation
or stream failure. A failed replay therefore cannot reuse an earlier verdict or
publish a partial Markdown result. Failure to remove either report is diagnosed
and comparison fails without a verdict; consumers must still honor the exit
status, particularly after filesystem errors or interruption. Raw evidence and
`complete.txt` are not removed by report cleanup. Valid comparisons retain both
reports, including when the result is a confirmed regression (exit status 1).
The seal detects accidental corruption; it is not a signature against a party
who can edit both files and hashes. Incomplete runs retain diagnostics but have
no valid completion marker. CI publishes the summary and keeps raw evidence for
14 days, including failures; archive important acceptance bundles elsewhere.

## Per-site allocation reports

The same `BUSTER_BENCH_ALLOCATIONS` recorder also supports an optional
[allocation census](../../docs/allocation-census.md). Set
`BUSTER_ALLOCATION_CENSUS=1` for a separate diagnostic invocation and validate
its saved stderr with the offline reader. This exit report includes worker and
cleanup traffic, whereas source metrics retain the original calling-thread
pre-formatting snapshot. Keep each process log separate and do not substitute
census timings for the normal uninstrumented series.


## Result schema 2: process diagnostics (#345)

The generated-input format remains **schema 1**: changing result columns does
not alter the six workload files, PRNG sequence, flags, modes or default guard
family. The result bundle is **schema 2**. Its CSV files append `minor_faults`,
`major_faults`, `voluntary_context_switches` and `involuntary_context_switches`
after `output_sha256`. These are optional exact unsigned integer counts; `NA`
means unavailable, whereas `0` is an observed zero. Negative, fractional,
malformed and overflowing counts are rejected. Raw values retain all 64 bits;
summary medians use the same floating-point presentation as other metrics.

Linux copies `ru_minflt`, `ru_majflt`, `ru_nvcsw` and `ru_nivcsw` from the
**existing per-invocation `wait4` result**, after stopping the wall clock. No
extra process, timer, PMU event, polling loop or allocation observer is enabled
in a timing trial. These are child usage counts, not the harness's cumulative
children totals. The operating system may include descendants whose resource
usage the child waited for; this is not live process-tree aggregation. macOS
and Windows currently leave these four observations unavailable. In particular,
a Windows total fault count is not invented as a minor/major split.

Both `samples.csv` and the independent PMU/allocation `telemetry.csv` retain
the counts. `summary.json` includes their timing medians and separate replay
medians with availability counts; an incomplete diagnostic series has a null
median rather than silently selecting its available subset. An unavailable OS
counter does not invalidate an otherwise complete allocation probe. Missing
allocation metrics still invalidate an explicitly requested allocation probe.
The existing output-hash, source-work and bundle-completion checks apply.

**These fields never gate performance.** The two rounds, adjacent pairs,
15%/2 ms wall margin, 20%/16 MiB RSS margin, exact sign test, Bonferroni family,
warmups, pair minimum and inconclusive status are unchanged. Synthetic tests
increase all four diagnostics by twelve orders of magnitude without changing
a guard decision. They are evidence for investigating scheduling or paging,
not grounds for deleting inconvenient samples or attributing a speedup.

The new reader rejects a schema-1 result with an explicit version diagnostic;
it does not reinterpret old column positions. Keep the original harness to
replay an old bundle, or collect a fresh experiment with schema 2. Do not edit
an old completion manifest to claim compatibility.

The native regression suite checks exact zero/UINT64_MAX/unavailable replay,
invalid counters, legacy rejection, optional probe completeness, and unchanged
corpus hashing. On Linux it compares one allocation-and-sleep child's `wait4`
counts with an independently obtained `getrusage(RUSAGE_CHILDREN)` delta,
without another child between snapshots. Other platforms check unavailability.
The existing Linux harness job additionally runs the suite under ASan/UBSan:

```sh
clang -std=c11 -O2 -g -Wall -Wextra -Werror -Wpedantic \
  -fwrapv -fno-strict-aliasing -funsigned-char \
  -fsanitize=address,undefined -fno-sanitize-recover=all \
  tools/throughput/tests.c -lm -o build/throughput-tests-sanitized
ASAN_OPTIONS=halt_on_error=1:detect_leaks=1 \
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
  build/throughput-tests-sanitized build/throughput-tool-tests-sanitized
```

This closes a collection gap, not physical-hardware acceptance. Hosted runner
labels, generic PMU availability and these OS counters do not establish Zen 5
identity, physical isolation, instruction throughput or a compiler speedup.
#46/#128 still require verified physical Zen 5 experiments and separate Zen 4
nonregression. #346's optional macro/aggregate workloads remain separate; no
preprocessing/debug-specific case or default corpus expansion is added here.


### Native-retirement paired numeric samples

`retirement_samples.h` extends the invocation writer with an explicitly attached
`TpRetirementSamples` collector. Initialize it with a fresh exclusive seekable
spool, per-row workspace, and immutable code/runtime applicability. All
invocations, including warmups, must advance through its append operation;
advancing the transcript independently invalidates collection. Runtime
applicability must match the cursor's copied row map. Caller flags are not an
admission surface: the eventual installed recipe must derive these facts from
the independently verified full support population.

Collection preserves the approved execution order, but replay requires numeric
samples in row/round/pair order. A private 72-byte-per-pair spool bridges these
orders without allocating the entire experiment. It stores exact nanoseconds,
RSS, code bytes, runtime and variant order as little-endian integers. It is
scratch storage, **not a second published result schema**. Per-row in-memory
compiler/runtime hash states bind observed values to the actual exported bytes;
positive-value mutation, including a mutation after a shard boundary within the
same row, prevents successful completion. RAM is proportional to rows, while
the bounded spool is proportional to rows times rounds times pairs.

Export begins only after the complete invocation transcript finishes. Each
numeric shard contains 32,768 canonical existing result-input records, except
for the final short shard. Optional metrics are omitted exactly when
inapplicable; unavailable mandatory observations never become zero. Descriptors
are emitted only after a successful flush, and an ordered descriptor digest
binds the complete shard inventory. The writer emits full-cap 16,777,216-record
manifest partitions, with only the final partition shortened. Each partition
retains the result reader's 16 GiB bound; the complete collector rejects more
than 39,518,208 records. No statistical threshold or pinned statistics source
changes.

The native tests write both transcript and numeric sample fixtures. The Python
replay imports the C-written numeric records through the production sample
consumer, joins them to every authenticated invocation, and verifies the
manifest through the production no-follow result reader on POSIX. A real
32,768-record boundary, deterministic second collection, copied applicability,
partial collection, bypass, stale state, missing metrics, spool mutation,
truncation, nonempty destinations, descriptor replacement and buffered disk-full
failures are covered. The commands above run this coverage in the existing
native/sanitized harness lanes; Windows does not claim the POSIX-only result
reader gate.

These are collection primitives, not service admission. The caller still owns
exclusive file identities, correctness and output-oracle gates, live quiet-phase
coordination, cancellation, durable publication and independent rehash/replay.
Ordinary throughput does not use this collector. In particular, its spool and
transcript writes are not proof of a quiet-phase retention policy: the installed
recipe must implement and validate that integration before admission. The
performance descriptor remains blocked; neither synthetic fixtures nor an
integrity-only manifest establishes a performance verdict.

### QUALITY scratch/work census

The existing `BUSTER_BENCH_ALLOCATIONS=ON` diagnostic compiler also emits
`quality_census.version=1` and `quality_census.*` integer fields in each
`-fsource-metrics` file. The native runner already retains these files beside
its artifacts during the separate `--allocation-baseline` / `--allocation-candidate`
replays and requires their object bytes to match the uninstrumented subjects.
Unknown additive keys do not change its timing or allocation schema. See
[QUALITY census](../../docs/quality-scratch-census.md) for exact meanings,
scope, exclusions and interpretation. These diagnostic runs are not latency,
RSS or hardware-counter acceptance evidence.
