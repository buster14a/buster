# Raw-IR inter-block liveness

`src/buster/compiler/ir_dataflow.c` connects the shared `IrFunction` to #143's
existing `dataflow.c` solvers. It does not replace the selected-use analysis in
`tier1_liveness.c`, change a register allocator, or implement native phi moves.
Selection can eliminate or introduce machine uses; raw IR liveness and selected
machine liveness are different interfaces.

## Interface and representation

Include `ir_dataflow.c` after `lane.c`, `ir.c` and `dataflow.c`.

- `ir_dataflow_prepare` and `ir_dataflow_prepare_scalar` validate the function and
  build identical owned maps, CFG, use/def matrices and outgoing phi records.
- `ir_dataflow_solve` and `ir_dataflow_solve_scalar` compute live-in/live-out and
  return the underlying solver's sweep count, including its final stable sweep.
- `IrDataflow.ids` maps original instruction IDs to dense IDs, with
  `BUSTER_IR_NONE` for ordinary block-local values. `values` is the inverse map,
  ordered by original ID. Every phi input is an edge use and receives a bit,
  including a value defined in that predecessor and a self-loop phi input.

The `Dataflow` member retains the existing 64-byte alignment and 512-bit padded
width. All padding is zero. The four matrices occupy
`4 * block_count * row_words * sizeof(uint64_t)` bytes. A function with no
inter-block values has zero row width and null matrix pointers, not a dummy word
per block. Mapping, CFG and sparse phi arrays are reported separately through
`arena_bytes`, which includes their allocation/alignment overhead.

`phi_offsets` groups outgoing phi uses by predecessor. `phi_values` contains dense
IDs and `phi_targets` contains the corresponding successor IDs. Duplicates are
retained: two phis can use the same value, and two different outgoing edges can
consume it. A consumer must not treat the union live-out of a block as a
successor-specific phi argument list.

Input, output and scratch storage must be disjoint. Output and scratch must be
different arenas. Failed preparation preserves the output mark and result;
scratch is returned on both success and failure. Like the shared IR verifier,
the API checks logical extents and assumes the published pointer extents are
readable. It checks operand types, local definition-before-use and exact phi
predecessors, but does not itself prove cross-block dominance. A consumer needing
that proof must first use the existing IR dominance validation.

Both paths explicitly reject more than `INT32_MAX` instructions with
`IR_ERROR_LIMIT`: the vector path's indexed loads use signed 32-bit indices.
There is no silent truncation or unsigned-to-negative gather. Allocation failure
returns `IR_ERROR_MEMORY`; no arbitrary smaller matrix budget is imposed.

## Phi semantics without another dense matrix

Let `U[B]` be ordinary upward-exposed uses, `D[B]` definitions, and `P[B,S]` the
incoming values of the phis in successor `S` on edge `B -> S`. Define
`P[B]` as their union over the actual outgoing edges. The desired equations are:

```text
OUT[B] = union(IN[S] for successors S) | P[B]
IN[B]  = U[B] | (OUT[B] & ~D[B])
```

Preparation stores `USE[B] = U[B] | (P[B] & ~D[B])`. The existing solver runs
unchanged with its ordinary successor union:

```text
OUT0[B] = union(IN[S] for successors S)
IN[B]   = USE[B] | (OUT0[B] & ~D[B])
```

Substitution gives exactly the desired equation for `IN`. After convergence,
the wrapper publishes `OUT[B] = OUT0[B] | P[B]`. This does not require another
fixpoint: the solver reads successor `IN`, never successor `OUT`. It also handles
unreachable components, backedges, self-phis and parallel phi swaps. Phi inputs
are never inserted into the successor's ordinary use row.

A repeated solve clears `IN` through the existing solver, overwrites `OUT0`, and
restores phi edge uses again. Consumers must use the wrapper, not call the raw
solver and assume its temporary `OUT0` already contains phi arguments.

## Batch construction

The vector path reads fixed operand columns in groups of sixteen instructions,
using the IR's required/optional operand tables. Predicates, symbols, slots,
address descriptors, labels and switch metadata are not value operands. Optional
address indices, return values and aggregate call-result destinations participate
only when present. Indirect callees and all call arguments are real operands.
Variable call slices and interleaved phi predecessor/value pairs have masked tails.

Cross-block classification uses masked indexed loads only for real operands.
Dense IDs use mask popcounts, prefix ranks and compressed stores. Bit updates use
`VPCONFLICTD` on **64-bit-word indices**, mask out inactive predecessors, combine
each word's contributions and perform one ordinary 64-bit OR store per group.
There is no hardware scatter and no `uint32_t*` alias of the bitset storage.
The scalar twin enumerates individual operands and uses ordinary scalar bit ORs.

The existing measured fixpoints, their selected dispatch and intersection
primitives are unchanged. Introducing an adapter is not evidence that every new
construction kernel is faster than its scalar twin; target measurements must
establish that separately.

## Qualification

`test-ir-source` now checks dataflow for **every accepted function** in its
lowered, vector-promoted and scalar-promoted forms. Its existing text checks are
unchanged. The normal source gate reports files left out by the existing C
producer separately; it does not label them as tested functions. The existing
correctness manifest's c-testsuite subset, SQLite and musl invocations therefore
exercise dataflow on the same declared corpus and forms.

For each form, scalar and vector preparation/solving must agree on dense maps,
CFG/postorder, phi records, use/def/in/out, and sweep counts. An independently
written opcode-based checker reconstructs dense membership, ordinary uses,
edge-specific phi uses, reverse-instruction transfer equations, and zero padding.
It does not use the adapter's operand-mask tables or phi CSR for those checks.

The raw fixtures run only under `--dataflow-self-test`; ordinary source
invocations run the per-function checks above and do not repeat them, so a
corpus run pays for its own functions alone. The self-tests additionally compare
against a byte-per-original-value fixpoint
with reverse numeric block order, not the implementation's dense bitsets or
postorder. Fixtures cover critical edges, parallel phi swaps, random CFGs,
loop chains, nested loops, unreachable regions, duplicate successors, indirect
calls, metadata lookalikes, mixed fixed operands, integer/float/SIMD/mask values,
64/512-bit boundaries, duplicate same-word updates, poisoned repeated solves,
empty/local-only functions, malformed phis, and transactional allocation failures.
Input columns and phi/call slices are also placed immediately before inaccessible
pages and made read-only.

Run these commands on an authorized runner, not the local desktop:

```sh
./build/compiler test-ir-source --dataflow-self-test
./build/compiler test-ir-source tests/dataflow/edges.c
```

The source smoke file is original test input, not a substitute for the frozen
corpus or the independent synthetic graph fixtures. A successful self-test is
not a claim that the frozen corpus or target measurements have run.

## Measuring the source functions

The optional `--dataflow-benchmark` flag measures the same accepted source forms.
For each function it reports the scalar and host-vector paths for both
`solve` and `prepare-solve`. The latter includes structural verification, CFG and
dense-map construction, allocation, use/def construction and solving. Neither
operation includes frontend preparation, IR lowering, SSA promotion, printing,
or correctness oracles.

The benchmark requires readable hardware CPU-cycle and instruction counters.
It never labels TSC ticks or missing counters as CPU cycles. A warm-up estimates
batch repetition counts, matched between the two paths. Twenty-one passes rotate
the four variants' order. The output includes every raw batch, medians in cycles
per function and cycles per block/sweep, the actual sweep count, matrix bytes,
retained bytes, dense-value count and phi-use count. For `prepare-solve`, the
block/sweep normalization includes setup; it is not a pure solver metric.
`vector_host=0` explicitly means the vector entry used the baseline scalar
fallback; such a run is not Zen 5 vector evidence.

On the 9700X, hold the shared exclusive lock for the complete run and pin to CPU 2:

```sh
EXCLUSIVE_OWNER=issue-143-ir-dataflow ~/buster-fast-bench/exclusive.sh taskset -c 2 ./build/compiler test-ir-source --dataflow-benchmark tests/dataflow/edges.c
```

Then repeat with the **same frozen source lists and preprocessing options as the
correctness manifest**. Do not replace rejected files with a more convenient
subset. Benchmark mode fails if any requested source is left out, any lowering
or promotion fails, any oracle disagrees, any allocation/counter fails, or no
function is tested. Raw records are provisional until the command exits zero and
prints `qualified=1`; a later rejected file invalidates the command's qualification.

Record the exact compiler commit, host/toolchain/affinity, commands, source
manifest, complete stdout/stderr and exit status. Publish raw measurements in
`buster14a/buster-performance`, not this repository's `docs/measurements`. The
implementation and recipe alone do not establish #143's final performance
acceptance. The PR/issue must cite the actual target run and explain its scope
before closing that requirement or changing the solver's measured selection.
