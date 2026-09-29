# Optimization-effort diagnostic pilot

This is isolated research for the optimization-effort question at
`8f67df736f13d4edc055110a7a6d619a00a22eaf`. It changes neither a production
source nor the default FAST allocator. Python uses only the standard library;
fixture code is C. The experiment contains no timing or runtime search.

The frozen deterministic candidate is narrowly scoped to explicitly requested
QUALITY allocation: after its existing valid FAST baseline, return that same
baseline when `spill_count == 0 && reload_count == 0`. Those exact placement
facts imply that all weighted per-value traffic is zero and the existing heap
is empty. This is a proof-directed skip, not a prediction of execution hotness.
The existing return site is reused; no return or alternate allocator is added.
QUALITY's prepass, loop closure, and FAST baseline are still paid.

The skipped allocations request exactly
`4 * max(I, 1) + 16 * max(V, 1) + 24 * 4096` payload bytes, where `I` is the MIR
instruction count and `V` the virtual register count. This covers instruction
weights, u64 traffic, two u32 per-value arrays, and the bounded heap. Alignment,
arena chunk capacity, peak live storage, and RSS are separate unknowns. Source
and existing machine tests establish the interval's 24-byte layout; each
diagnostic variant also contains a compile-time check.

Design inputs are a low-pressure leaf and a high-pressure call control. Before
any experiment, `manifest.json` freezes those and five held-out families:
calls, nested loops (including zero-trip inputs), switch tables, volatile
memory, and vectors. The decision rule is fixed by the source proof and is not
adjusted using any fixture result. Switch-table functions may contribute zero
observer records because QUALITY already bypasses the instrumented path.
Absence of records is not counted as absence of work or a new candidate skip.

Generate source variants only into separate diagnostic clones:

```sh
python3 tests/optimization_effort_probe/patch.py \
  --source /pristine/src/buster/lib/compiler/codegen/register_allocator_quality.c \
  --variant baseline --output /baseline-clone/src/buster/lib/compiler/codegen/register_allocator_quality.c
python3 tests/optimization_effort_probe/patch.py \
  --source /pristine/src/buster/lib/compiler/codegen/register_allocator_quality.c \
  --variant candidate --output /candidate-clone/src/buster/lib/compiler/codegen/register_allocator_quality.c
```

`--diff` emits a unified patch instead of a source variant. The source digest
must match the frozen revision. The output must differ from the input path.
Build provenance and self-host fixed-point validation belong to the external
build-driver run; they are not silently claimed by this harness.

After externally building and freezing the two observer compilers:

```sh
python3 tests/optimization_effort_probe/run.py \
  --baseline /baseline/ide --candidate /candidate/ide \
  --host-cc clang --output /artifacts/optimization-effort
```

If the ordinary trusted-producer compiler is available, add `--trusted /trusted/ide`.
This additionally requires each observer baseline object to match the ordinary
compiler byte for byte, without using either compiler for timings.

For each fixture the harness compiles strict verified QUALITY objects with the
allocator option last, requires complete object-byte identity, parses the
per-invocation records, and host-links the fixture to a separate C driver.
Each executable must match 384 independently Clang-compiled fixture results
and their checksum. The driver does not become an input to Buster. The record
order within a translation unit identifies invocations; MIR supplies no source
function name to this observer. No durations are recorded.

The held-out experiment passes only when object identity, independent execution,
baseline/candidate fact identity, candidate skip-rule identity, and baseline
empty-heap proof all pass, with at least one positive skip across the complete
corpus. Raw commands and stdout/stderr are retained with SHA-256 identities.
A changed fixture digest fails before compilation. Nonzero compilation or
link/execute results remain failed experiments in `report.json`.

The pilot supports a bounded implementation packet only if the external run
also preserves focused machine/IR/driver tests and self-host fixed point.
It does not establish whole-compiler speed, RSS improvement, useful lifetime
runtime benefit, or a universal optimization policy. Trusted uninstrumented
compiler throughput and self-built compiler performance remain distinct,
unmeasured evidence classes. In particular, an observer compiler is ineligible
for throughput acceptance because printing changes its cost.
