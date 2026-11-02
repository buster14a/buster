# dataflow

Run commands and resolve source paths from the repository root. Follow the
[shared agent rules](../../AGENTS.md), including the 9700X lock and `cores`
requirements. [Command index](README.md).

## Module overview

- `src/buster/compiler/dataflow.c` provides aligned bitset liveness and
  interference with AVX-512 and scalar implementations, independent of IR.
  `dataflow_program.c` supplies the shared synthetic CFG generator;
  `dataflow_test.c` provides `test-dataflow` and `dataflow_benchmark.c`
  provides `benchmark-dataflow`.
  `docs/dataflow.md` records the contracts and 9700X measurements.
## Dataflow tests

`./build/compiler test-dataflow [--seed N] [--count N]` checks the IR-independent
liveness and interference primitives in `dataflow.c`:

- generated CFGs: chains, diamonds, self-loops, nested loops, irreducible
  two-entry loops, several exits, random local components, and connected
  dispatch loops. Every shape runs with 0, 1, 2, 3, 8, 17, 65, 257 and
  2,049 blocks at each of 0, 1, 63, 64, 65, 511, 512, 513, 1,024 and 1,025
  values, then `--count` more seeds and sizes per shape (24 by default). The
  same seed must reproduce the columns and input rows. The postorder must
  visit every block once, and every row must be 64-byte aligned and padded
  to 512 bits;
- the AVX-512 and scalar fixpoints: identical `in` and `out` rows and iteration
  counts, both liveness equations satisfied, and all padding bits zero.
  Solving again over poisoned output must reproduce the result and iteration
  count, including zero padding after the poisoned rerun. Small graphs also
  use an independent per-value reachability check: a value is live when a
  path reaches its use before its definition. Graphs of 257 and 2,049 blocks
  independently check selected boundary and seed-derived values at every
  block with a reverse-reachability oracle;
- handwritten graphs: disconnected cycles, a self-loop without a use, a value
  both used and defined in one block, every combination of the transfer
  function's three input bits, the exact postorder of a chain, and repeated
  successor/self/exit edges compared with a deduplicated graph;
- interference: empty rows and every bit position in rows of 512, 1,024 and
  1,536 bits, with disjoint and intersecting inputs, checked by both paths;
- page edges: each matrix ends at a `PROT_NONE` page for every boundary value
  count above, with zero-size rows pointing at the inaccessible page itself.
  Both solvers and the interference check must stay within the rows, and the
  poisoned bytes before each matrix must remain unchanged;
- failure handling: insufficient arena capacity leaves the arena and result
  unchanged, successful allocations zero the rows, malformed successor
  offsets or targets leave the postorder and scratch arena unchanged, and
  missing scratch memory and an edge in an empty CFG are rejected.

`--seed N` changes the generated inputs (1 by default). The command reports
the check and failure counts and exits with status 1 on any failure. The
9700X job's test step runs it.

## Dataflow benchmark

`./build/compiler benchmark-dataflow [--seed N] [--after-scalar MS]
[--only scalar|avx512]` compares the scalar and AVX-512 fixpoints on four
generated functions: a small diamond CFG with
16 blocks and 127 values, nested loops with 256 blocks and 1,025 values, and
random local components and a connected dispatch loop, each with 4,096 blocks
and 4,097 values. The local-components case is a streaming control, not a model
of a large real function. `--seed N` selects the inputs (1 by default); generator
version 2 mixes the seed before generating topology and rows.

Each workload first checks identical rows and iteration counts, then reports
21 interleaved-pass medians and minimum/maximum ranges with alternating
implementation order. Both paths use the same repetition count, calibrated
with warm batches; every retained sample lasts at least 2 ms. Each timed solve
includes the rows its solver clears; generation, allocation and postorder are
outside the timed region. Both paths use direct calls through matching
`noinline` benchmark wrappers, each starting a 64-byte line, as do the AVX-512
solver's two loops in `dataflow.c` (for one-vector rows and the rest), which
its wrapper jumps to, so that edits to the driver don't move the solvers' loops
within lines (the scalar twin's numbers once depended on it: `docs/dataflow.md`,
"Where the scalar twin's code falls"). The output gives nanoseconds, cycles and instructions
per function, cycles per block per iteration, IPC, and the throttle per block
per iteration (PMCx1A0's undocumented umask 0x08, which #199 reads as the
dispatch slots Zen 5's per-core power cap takes). Hardware counter values
are zero when unavailable. A scalar/vector disagreement fails the command.
Run at least five independent processes, releasing the host lock between
them, when recording a comparison; process-median ranges expose variation
that one process's 21 passes may miss.

The AVX-512 solver is heavy enough for the power cap, so measure it in both
states, as the encoder's benchmark does (#199): `--only avx512` times it alone,
back to back, and `--after-scalar MS` runs complete scalar solves of the
workload, untimed, for MS ms (100 to 300) before every AVX-512 sample, the state
it runs in after a compiler's lighter phases. Without either, its samples follow
the scalar ones in alternate passes and run partly throttled.
`docs/dataflow.md` has the three states' numbers.

Run it under the 9700X's lock with `taskset -c 2`. `docs/dataflow.md` records
the contracts, measurements and implementation choice. The default 9700X job
runs it after the tests, pinned to one allowed CPU; like the other benchmarks,
its measurements are informational. Building use/def rows from IR columns and
running the corpora remain outside this module's current scope and wait for
#131.
