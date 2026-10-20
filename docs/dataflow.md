# Liveness and interference on bitsets

`dataflow.c` implements the part of [#143](https://github.com/buster14a/buster-fast/issues/143)
that needs no IR: a backward liveness fixpoint over supplied `use` and `def`
rows, and an intersection test between two rows. The caller supplies dense IDs
for values that can be live across blocks. Constructing those rows from IR
instruction columns, including conflict detection for repeated IDs, and
measuring actual corpus functions wait for #131.

## Representation and lifetime

`Dataflow` stores a CFG as successor offsets and targets, with 32-bit block
and value IDs. Every block has a `use`, `def`, `in` and `out` row. The row
width rounds the value count up to a multiple of 512 bits, and rows start on
64-byte boundaries. Padding belongs to the allocation and stays zero.
For zero values, there are no row words and the kernels perform no row
loads or stores.

`dataflow_create` allocates the columns and rows from a caller-owned `Arena`.
Allocation failure leaves the arena at its old position and returns no
partially usable graph. The caller fills the successors and input rows,
then calls `dataflow_postorder`. It validates the successor representation
and constructs a postorder with an explicit DFS stack. A forest walk covers
disconnected blocks and components without exits too. The temporary stack
and marks use the supplied scratch arena; they do not become graph state.

The solve functions require this prepared CFG and zero padding in both
input matrices. They clear `in` and `out` before solving, so repeated calls
compute the least fixpoint from the same starting state.

## The two kernels

For each block in postorder:

```text
out[block] = OR(in[successor] for each successor)
in[block]  = use[block] | (out[block] & ~def[block])
```

The AVX-512 loop handles eight 64-bit words at once. It ORs successor rows,
then computes the transfer with `_mm512_ternarylogic_epi64(use, out, def,
0xF4)`. An XOR with the previous input row and a mask test detect changes.
Every vector access is inside the allocated padded row.

The scalar twin visits the same blocks, successors and row words, in the
same order. Its word loops disable Clang loop vectorization so the measured
alternative remains scalar. Both count complete sweeps, including the final
sweep that changes nothing. Comparing iteration counts catches changes to
the update schedule as well as differences in the resulting bits.

`dataflow_intersects_vector` uses `_mm512_test_epi64_mask` to detect a shared
bit in two padded rows. `dataflow_intersects_scalar` checks the same words
with scalar ANDs. An empty row does not intersect anything.

## Tests and synthetic inputs

`dataflow_test.c` runs through `test-dataflow`. Besides comparing the two
solvers, it checks the liveness equations, zero padding, repeatability and
the least solution on small hand-checkable graphs. Guard-page cases put
the last row word immediately before an inaccessible page, including empty
rows pointing at that page. Invalid CFGs and arena exhaustion are errors.

The shared generator lives in `dataflow_benchmark.c` and is linked into the
single compiler executable. It supplies deterministic CFGs and sparse
`use`/`def` rows from a seed for both tests and measurements. It generates
synthetic inputs directly; it does not incorporate source from a corpus or
copy another compiler's implementation.

## Measuring the alternatives

`benchmark-dataflow` checks scalar/vector results before measuring small,
medium and large generated functions. It reports 21 interleaved-pass
medians, with elapsed time and hardware cycles per function, and cycles
per block per iteration. Missing hardware counters are reported as zero;
elapsed time is not relabeled as CPU cycles.

The benchmark includes output initialization in each solve, but excludes
generation and postorder construction. Its largest case has thousands of
blocks and values. That describes its scale only: it is not a measurement
of `sqlite3VdbeExec` or any other real program.

## 9700X results and selection

Measured on 2026-10-20 on `benchpress`, Ryzen 7 9700X, Linux
6.18.50-2-lts, Clang 23.1.1 and glibc 2.44, from main
`1bc8ca0e18452c80841f783f9d139bea39180d43` plus this module. The unchanged
`build.sh` builds at `-O3 -march=native`. Each command acquired and released
the shared host lock separately:

```sh
EXCLUSIVE_OWNER=astra ~/buster-fast-bench/exclusive.sh ./build.sh
EXCLUSIVE_OWNER=astra ~/buster-fast-bench/exclusive.sh taskset -c 2 ./build/compiler benchmark-dataflow
```

Seed 1, 21 alternating passes, hardware cycles available:

| CFG | Blocks / values / edges | Padded row bytes | Sweeps | Scalar cycles/function | AVX-512 cycles/function | Scalar cycles/block/sweep | AVX-512 cycles/block/sweep |
| --- | --- | --- | --- | --- | --- | --- | --- |
| small diamond | 16 / 127 / 19 | 64 | 2 | 1,528.7 | 426.7 | 47.773 | 13.334 |
| medium nested loops | 256 / 1,025 / 295 | 192 | 3 | 131,698.7 | 24,241.2 | 171.483 | 31.564 |
| large random | 4,096 / 4,097 / 8,145 | 576 | 5 | 9,833,086.5 | 2,878,893.5 | 480.131 | 140.571 |

Elapsed medians were respectively 280.3 / 78.3 ns, 23,934.1 / 4,415.5 ns
and 1,796,249.5 / 525,445.8 ns per function (scalar / AVX-512).
The runs repeated each complete solve 5,264, 435 and 4 times per sample.
Result digests were `1f037a002e547a1a`, `97749a1bcdfffe56` and
`0f5ac66bd44fa436`, respectively.

`dataflow_liveness` selects AVX-512: it uses 3.58, 5.43 and 3.42 times fewer
cycles on these three inputs. The scalar twin remains the correctness oracle
and benchmark alternative. This choice applies to the supplied bitset solve;
IR construction and end-to-end compiler speed remain unmeasured.

Inspection of the measured binary confirmed 64-bit scalar register operations
in the scalar transfer and successor loops. The vector copy uses aligned
64-byte loads/stores, `vporq` and `vpternlogq` with immediate `0xF4`.
Clang folds the XOR-and-test into `vpcmpneqq`, with `kmovd` and a scalar OR
accumulating changes. Its row loop advances eight words at a time with a
bounded remainder for unrolled paths; neither solver uses gathers or scatters.

The default tests passed 12,688 checks; `test-dataflow --seed 7 --count 64`
passed 15,481. Both solvers agreed on all rows and sweep counts. All existing
smoke tests and `./build.sh check-call-graph` passed on the same host, with
the latter reporting zero call-graph cycles.
