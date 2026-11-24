# Raw-IR inter-block liveness

`src/buster/compiler/ir_dataflow.c` connects the shared `IrFunction` to #143's
existing `dataflow.c` solvers. It does not replace the selected-use analysis in
`tier1_liveness.c`, change a register allocator, or implement native phi moves.
Selection can eliminate or introduce machine uses; raw IR liveness and selected
machine liveness are different interfaces.

## Interface and representation

Include `ir_dataflow.c` after `lane.c`, `ir.c` and `dataflow.c`.

- `ir_dataflow_prepare` validates the function and builds the owned maps, CFG,
  use/def matrices and outgoing phi records. It is AVX-512 alone (#2924); its
  scalar twin `ir_dataflow_prepare_scalar` went once the vector path won every
  class of function (#2950, below).
- `ir_dataflow_solve` computes live-in/live-out and returns the underlying
  solver's sweep count, including its final stable sweep.
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

Cross-block classification compares each real operand with its block's
instruction range, two compares against `block_offsets` and no memory access:
the blocks' instructions are contiguous, so an operand is local exactly when it
falls in its block's range. A masked gather of each operand's block cost the
smallest functions' preparation more than the scalar twin's lookups (#2950).
Dense IDs use mask popcounts, prefix ranks and compressed stores. The one gather
left reads cross-block operands' dense IDs, only in a batch that has one: a
`zmm` gather is 64 uops whatever its mask. A batch of phi inputs gathers once,
for both the inputs from other blocks, which join the predecessor's use row, and
the dense values that replace the original IDs. Bit updates take the first
active ID's **64-bit word**, combine every active ID's bit in that word with one
masked reduction of both halves, store it with one ordinary 64-bit OR and retire
those IDs, until none is active. A batch with no active ID does nothing.
`VPCONFLICTD`, which grouped the words before #2950, cost more than it saved
for the few IDs of a block's phi inputs. There is no hardware scatter and no
`uint32_t*` alias of the bitset storage. Until #2924 a scalar twin enumerated
individual operands and used ordinary scalar bit ORs.

## Qualification

`test-ir-source` now checks dataflow for **every accepted function** in its
lowered and promoted forms (a scalar-promoted third went with promotion's twin,
#2924). Its existing text checks are
unchanged. The normal source gate reports files left out by the existing C
producer separately; it does not label them as tested functions. The existing
correctness manifest's c-testsuite subset, SQLite and musl invocations therefore
exercise dataflow on the same declared corpus and forms.

For each form, two preparations at different places in the output arena must
agree on dense maps, CFG/postorder, phi records and use/def, and, solved, on
in/out and sweep counts; until #2924 the second was the scalar twin's. An independently
written opcode-based checker reconstructs dense membership, ordinary uses,
edge-specific phi uses, reverse-instruction transfer equations, and zero padding.
It does not use the adapter's operand-mask tables or phi CSR for those checks.
A layout check reads the rest from the IR: each block's successors are its
terminator's distinct targets in their first order, the postorder holds every
block once, and each predecessor's phi records are the pairs naming it, in
block, phi and pair order, with their dense values and target blocks. dataflow's
sweep search ([`dataflow.md`](dataflow.md#the-sweep-search)) checks every live
bit and the exact sweep count.

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
For each function it reports `solve` and `prepare-solve`; until #2924 it
reported the scalar twin's beside them. The latter includes structural verification, CFG and
dense-map construction, allocation, use/def construction and solving. Neither
operation includes frontend preparation, IR lowering, SSA promotion, printing,
or correctness oracles.

The benchmark requires readable hardware CPU-cycle and instruction counters.
It never labels TSC ticks or missing counters as CPU cycles. A warm-up estimates
batch repetition counts. Twenty-one passes alternate the two operations' order. The output includes every raw batch, medians in cycles
per function and cycles per block/sweep, the actual sweep count, matrix bytes,
retained bytes, dense-value count and phi-use count. For `prepare-solve`, the
block/sweep normalization includes setup; it is not a pure solver metric.

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

## Measured result on the 9700X

The first target run is
[`dataflow-143-2026-10-31`](https://github.com/buster14a/buster-performance/tree/75397eb4cf2aa8e821ccec708599b9f508958c50/docs/measurements/dataflow-143-2026-10-31):
code commit `787d1071`, the frozen c-testsuite subset, SQLite 3.53.2 and musl
1.2.5 units, CPU 2 under the shared lock. Every run printed `qualified=1`,
`left_out=0`, `dataflow_failed=0` and `vector_host=1`.

- **Solve** was faster in aggregate on SQLite (1.32×) and musl (1.09×), and at
  parity on c-testsuite (1.002×). The median per-form ratio was mixed (musl
  0.83, SQLite 0.95, c-testsuite 1.04; below 1 favors the vector path), so the
  gain is not uniform per function.
- **Prepare-solve** was slower than its scalar twin in aggregate on all three
  corpora (0.83×, 0.81× and 0.79×), with median per-form ratios of 1.29, 1.33
  and 1.40. This run does not support selecting the vector construction; the
  existing solver dispatch is unchanged. The adapter keeps both paths for
  correctness and future consumers.

The record covers the adapter's two operations, not whole-compiler time:
frontend preparation, IR lowering, SSA promotion and output are outside every
sample. It has one process for c-testsuite and two for SQLite and musl; its
README lists the host, manifests, checksums and complete raw logs.

### #2950: the vector paths win every class

#2924's gate (main `8bd7f27ec`, sqlite3.c's 7,806 forms and `edges.c`, CPU 2
under the lock) found the vector paths faster in total but slower in three
classes of functions: solves with phis (1.12 to 1.15 times the scalar twin's
cycles at 3 to 128 blocks), the solver at 33 to 128 blocks with 64 values or
fewer (1.10), and the preparation of one- and two-block functions (1.09 to
1.27). #2950 changed four things:
- the solver keeps the previous block's row in a register (`dataflow.md`, "The
  previous block's row");
- the operands' locality is two compares with the block's range, not a gather;
- the bit updates group by word without `VPCONFLICTD`;
- a batch gathers only when it has an operand from another block, and a phi
  batch gathers once.

With those (run 6 of buster-performance's
`scalar-paths-removal-2924-2026-11-17/dataflow-2950/`), the vector path takes
0.586 of the scalar twin's cycles to solve and 0.604 to prepare and solve,
summed over every form. Every class of block count, phis and row width wins the
solve (0.72 to 0.95). The preparation of one- and two-block functions with no
value across blocks is the one class it loses, by 9% (134 forms, about 50 cycles
each). That is the IR's own checks: the scalar preparation runs
`ir_check_rows` and `ir_check_graph`'s scalar twins, which are faster than
their vector paths on functions that small. With both preparations on the
vector checks, isolating dataflow's own code, every class wins (0.71 to 0.993;
0.856 in total).
