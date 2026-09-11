# Canonical preparation and sparse-work census

This is a source-level map of main `ef99ec72cf0f17c8818a1d4b4c106fd5908b5eae`
plus the parameter-work counters described below. It is not a profile or a
claim that the historical pipeline proposed in [#40](https://github.com/buster14a/buster/issues/40)
already exists. The subject is execution of the trusted host-built compiler,
not generated-program runtime. Pressure-reduction policy belongs to
[#56](https://github.com/buster14a/buster/issues/56); CFG ownership belongs to
[#38](https://github.com/buster14a/buster/issues/38).

## Implementation update

The map below preserves the promotion-census context. The separately selectable
folding, address normalization, DCE and parameter cleanup now exist in
[`ir_fast.c`](../src/buster/lib/compiler/ir/ir_fast.c); their order, purity,
certification, deterministic limits and adoption gate are specified in
[the FAST pipeline contract](canonical-fast-pipeline.md). They remain opt-in
pending paired end-to-end acceptance; #371's observability was not itself
implementation of those transformations.

## Current order, prerequisites and invalidation

`compiler_driver_execute_c_single` lowers C and calls
`ir_prepare_canonical_module`, then
selects the native, Wasm64, eBPF or LLVM-bitcode consumer. There is no separate
pass-manager schedule implementing all the folding/DCE/address-normalization
steps proposed in #40. Folding during C construction and target selection must
not be mistaken for an independently scheduled canonical optimization pass.

| Population / owner | Prerequisites and work | Published facts / invalidation |
| --- | --- | --- |
| Frontend SSA, `c_ir_ssa_finish` in `c_gen.c` | Complete terminator edges; sparse current values and per-owner operation journals. Resolve pending reads with single-entry proofs or predecessor walks; classify initialization and restore excluded owners; simplify trivial parameters to a fixed point; prune unused parameter cycles from instruction-operand roots; remap values/operands. | Canonical rows, complete predecessors, source/local metadata and producer certification. A later write revokes the single-entry-definition proof; escaping owners are wholly restored, not partially promoted. Its replacement and reachability state is builder-local. |
| Preparation boundary, `ir_prepare_canonical_module` | Validate unless the caller explicitly trusts the producer. Process only lowered functions, only when promotion is enabled and the module is not complete. | Reset and publish `IrLocalPromotionStatistics` once. A mutated module must clear `local_promotion_complete` before another preparation. A repeated untrusted call can still run validation even though it does not repeat promotion. |
| Discovery, `ir_promote_function` | The conservative producer opcode summary can prove absence of LOCAL. Otherwise scan instruction rows until discovery completes or a barrier is found. Label metadata, unsafe calls and the existing effect barriers retain memory form. | LOCAL ownership/type eligibility is temporary. `IR_OPCODE_SUMMARY_TRACKED` is the query authority; unknown summaries scan normally. Mutating instructions must maintain or invalidate the summary, never invent a negative proof. |
| Classification / block-local promotion | Index LOCAL results, then visit linked instruction rows and operands once to build per-local events and reject escaping uses. Visit each eligible event prefix to prove store-before-load in each block. | Eligible block-local owners need neither CFG construction nor global liveness. Removing their events changes value aliases and marks instructions for the one later compaction. |
| Global promotion, `ir_promote_cfg`, `ir_promote_global` | Lazily build one deduplicated predecessor index from terminators and check/materialize predecessor lists. Reuse O(blocks + edges) scratch for each global candidate. Reset block arrays, propagate may-uninitialized state forward, prune live-in state backward, assign join parameters and forward single-predecessor definitions. | CFG is reused because promotion does not change terminator targets or block IDs. Ordered incoming lists follow the established predecessor order. Effects, live/bad flags and entry/exit definitions belong to one local only. Queues are monotone within each walk; this is already sparse dataflow, not a blocks-by-locals resident matrix. |
| Parameter cleanup, `ir_promote_compact` | Enter only after at least one local was promoted. Flatten load replacements and examine newly inserted parameters until a full sweep removes none. Pre-existing parameters remain in the lists but are not removal candidates here. | Every changing sweep removes at least one parameter, ensuring termination. Replacements can make an earlier parameter trivial on a later sweep, so a single pass is not equivalent. Keep self-edge handling and predecessor identity. |
| Dense publication, remainder of `ir_promote_compact` | Build instruction/next/value maps; traverse surviving block links; flatten aliases before overwriting values; copy retained rows and one operand pool; remap parameters, extras, source records and builder local maps. | Value and instruction IDs change once. Temporary indexes/aliases cannot escape scratch. Rebuild `opcode_summary`. Shared operand slices are copied, not remapped repeatedly in place. Label-bearing functions were excluded before this transformation. |
| Native consumers | Validated/certified canonical IR supplies machine selection. Fresh certified MIR can bypass another general verifier; explicit verification still checks it. Placement is stack, FAST or QUALITY. | Selection produces target-specific MIR and side tables; no new permanent middle IR is needed. QUALITY alone may schedule after an initial placement spills/reloads; a moved schedule pays a second placement and is retained only by the existing traffic/callee-save policy. This patch does not expand that expensive path. |

The driver uses certification only when neither bootstrap tracing nor explicit
code-generation verification requires the general check. For transformed
producer-certified IR, the separate validation-boundary question remains
[#294](https://github.com/buster14a/buster/issues/294). Do not remove validation
because a second call looks redundant: its trust preconditions differ.

## Repeated work and candidate classification

Already implemented: direct SSA instead of transient local/load/store rows;
entry-definition forwarding; the LOCAL-summary no-work guard; per-local event
lists; lazy shared CFG construction; reusable per-local scratch; final batched ID
remapping. These are not new optimization opportunities merely because an old
issue describes their absence.

The shared global path still resets and scans block-sized arrays for each
eligible non-block-local owner. Parameter cleanup still visits every block each
sweep, and the frontend has its own parameter fixed point. Dense publication
visits instructions, values and side data for different obligations. Their work
is not interchangeable: packing, alias flattening, source remapping and operand
ownership must be charged before proposing fusion.

The new counters close a **verified observability gap** in shared parameter
cleanup. A changed-parameter worklist, reverse-use index, compact candidate-block
list, bitset classification or SIMD batch remains an **untested hypothesis**.
First establish the actual population, sweep/removal ratio and complete compile
cost with default direct SSA and the memory-form reference reported separately.
A zero shared-pass census does not imply that frontend SSA did no work.

Historical losses remain relevant: #38's saved incoming-cursor experiment
reports small-join regressions despite a large synthetic inspection reduction;
#128 records rejected scheduler experiments and rejects dense liveness merely
to obtain SIMD work. Their unretrieved original payloads are not current-main
measurements. The direct-SSA audit
[`2026-09-08T230935Z`](performance-audits/2026-09-08T230935Z.md) already exploited
entry definitions, while the newest baseline audit is
[`2026-09-09T163649Z`](performance-audits/2026-09-09T163649Z.md). Neither establishes
Zen 5 acceptance for this census.

## Counter contract

The existing `IR_LOCAL_PROMOTION` line is unchanged. Verbose successful
compilation additionally prints:

```text
IR_LOCAL_PROMOTION_WORK parameter_sweeps=N block_visits=N parameter_visits=N incoming_visits=N
```

`IrLocalPromotionStatistics.parameter_sweeps` counts complete fixed-point
sweeps, including the final unchanged sweep. `parameter_block_visits` counts
all blocks in every such sweep, including blocks without parameters.
`parameter_visits` counts actual visits to parameter nodes, including retained
pre-existing nodes. `parameter_incoming_visits` counts actual iterations of the
triviality check, including self references and stopping after the first
conflicting value; pre-existing parameters perform no such incoming walk.
These are work counts, not unique-row counts or CPU instructions.

No compaction means four zero counters. Compaction without parameters still
has one sweep and one visit per block. For one compacted function with B blocks,
S sweeps and R removed parameters, `block_visits == B*S` and `1 <= S <= R+1`.
The sum over functions does not give a maximum per-function iteration count.
Use existing `removed_parameters` as useful changes, `inserted_parameters` as
new candidates, and instruction/value before/after fields as occupied-row
changes. Ratios with a zero denominator are unavailable, not zero cost.

Counts are accumulated with the other promotion statistics across translation
units. Repeated preparation of an unchanged complete module preserves them.
They cover shared parameter simplification only: initialization queues, frontend
SSA, canonical validation, machine scheduling, phase time and allocation traffic
are not newly measured. In particular, existing `CODEGEN`/allocator counters do
not become per-middle-end pressure attribution merely by being printed nearby.

Implementation cost is four u64 fields in the existing cold module/result
statistics, two additions per sweep and two additions per visited parameter /
incoming entry. Canonical and machine hot rows, scratch allocation and pass
order are unchanged. Counts are collected even without `-v`; printing is
verbose-only. Collection is not assumed free: normal-build A/B comparisons
must assess its overhead before treating this draft as accepted.

## Existing measurement system, separate diagnostic replay

Use [the native throughput tool](../tools/throughput/README.md), its frozen
sources, recorded commands and raw paired evidence; do not substitute a new
microbenchmark. With absolute immutable compiler paths and fresh output paths:

```sh
./build.sh bench_throughput run --baseline /base/ide --candidate /candidate/ide --output build/z09-throughput --baseline-id BASE_SHA --candidate-id HEAD_SHA --require-identical-output
./build.sh bench_throughput compare --output build/z09-throughput
```

The normal compiler series must not pass `-v`. Replay a recorded compile command
outside the timed series with `-v` and a separate output path to obtain the
census; preserve input, target, flags and allocator order. Replay both frontend
settings separately rather than pooling the default path with the slower
memory-form reference. A simple diagnostic invocation is:

```sh
/candidate/ide cc -c -g0 -O0 -fno-frontend-ssa -fno-target-local-promotion -fregister-allocator=fast -v tests/basic_c_operations.c -o build/z09-promotion-diagnostic.o
```

Use existing source-metrics/PMU/allocation probes for their separate scopes.
The native harness's frozen self-host input measures trusted host-built
compilers in stage 1 and self-built compilers in stage 2; the latter is not a
substitute for the former. Preserve exact A/B outputs for this observational
change, and keep ordinary self-host and all supported mode/correctness gates.
Hosted synthetic guard success is not a physical Zen 5 measurement or proof
of a small overhead being zero. Missing hardware/counters remain unavailable.
