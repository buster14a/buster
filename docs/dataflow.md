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

The scalar twin visits the same blocks and successors in the same order,
but processes only `ceil(value_count / 64)` words per row. Whole padding
words remain zero after output initialization; charging the scalar loop for
them would inflate the vector comparison, particularly for short rows.
Its word loops disable Clang loop vectorization so the measured alternative
remains scalar. Both count complete sweeps, including the final sweep that
changes nothing. Comparing iteration counts catches changes to the update
schedule as well as differences in the resulting bits.

Both kernels copy the column bases and dimensions into locals. The bases
are `restrict` because `dataflow_create` allocates disjoint columns and
matrices. Derived successor pointers into `in` are unqualified: a self-loop
must be able to read the same row that the current block updates.

`dataflow_intersects_vector` uses `_mm512_test_epi64_mask` to detect a shared
bit in two padded rows. `dataflow_intersects_scalar` checks the same words
with scalar ANDs. The vector primitive asserts that its row length is a
multiple of eight words and that nonempty rows are 64-byte aligned. An empty
row does not intersect anything.

## Tests and synthetic inputs

`dataflow_test.c` runs through `test-dataflow`. Besides comparing the two
solvers, it checks the liveness equations, zero padding, repeatability and
the least solution on small hand-checkable graphs. A separate reverse-CFG
reachability oracle checks selected values at every block of 257- and
2,049-block graphs; a planted incorrect bit checks the oracle itself.
A handwritten multigraph checks repeated successors, self-edges and exit
edges against the corresponding deduplicated CFG. Guard-page cases put
the last row word immediately before an inaccessible page, including empty
rows pointing at that page. Value counts include 0, 1, 63, 64, 65, 511, 512,
513, 1,024 and 1,025. Padding must be zero again after poisoned output rows
are solved. Invalid CFGs and arena exhaustion are errors.

The shared generator lives in `dataflow_program.c`, included by the tests
and benchmark. It supplies deterministic CFGs and sparse
`use`/`def` rows from a seed for both tests and measurements. It generates
synthetic inputs directly; it does not incorporate source from a corpus or
copy another compiler's implementation. Generator version 2 mixes the initial
seed with the public-domain SplitMix64 finalizer before drawing from the LCG,
so small consecutive seeds do not all start with the same draw. The retained
license and provenance are in `THIRD_PARTY_NOTICES.md`.

## Measuring the alternatives

`benchmark-dataflow` checks scalar/vector results before measuring four
generated functions. It reports 21 interleaved-pass medians and pass ranges,
with elapsed time and hardware cycles per function, and cycles per block per
iteration. Warm calibration repeatedly runs both variants in both orders,
using one common repetition count until every batch lasts at least 4 ms.
Every retained sample must last at least 2 ms. A shorter sample discards the
whole distribution and doubles the repetition count, with bounded retries.
The output includes the shortest retained sample and the number of restarts.
Missing hardware counters are reported as zero; elapsed time is not
relabeled as CPU cycles.

The benchmark includes output initialization in each solve, but excludes
generation and postorder construction. `large-local` retains the original
control: 64 disconnected 64-block regions and mostly local live values.
Its mostly empty chunks make it primarily a row-streaming workload; it does
not model the liveness of one connected function. `large-dispatch` adds a
connected dispatcher with 512 arms, nested back edges, an exit and persistent
values live across arms. Both have thousands of blocks and values, but
neither measures `sqlite3VdbeExec` or another real program. A separate
row-traffic floor has not been measured, so proximity to a bandwidth limit
is not established.

## 9700X results and selection

Measured on 2026-10-20 on `benchpress`, Ryzen 7 9700X, Linux
6.18.50-2-lts, Clang 23.1.1 and glibc 2.44, for PR #194's review revision
with main `c9b1b20` merged in. The
`build.sh` builds at `-O3 -march=native`. Five independent benchmark processes
each ran 21 alternating passes. Each command acquired and released the shared
host lock separately, including each benchmark process:

```sh
EXCLUSIVE_OWNER=astra ~/buster-fast-bench/exclusive.sh ./build.sh
EXCLUSIVE_OWNER=astra ~/buster-fast-bench/exclusive.sh taskset -c 2 ./build/compiler benchmark-dataflow
```

The tables report the median of those five process medians, followed by their
`[min-max]` range. They are not the within-process pass ranges printed by the
command. Hardware core-cycle counters were available in all five processes.

Generator version 2, seed 1:

| CFG | Blocks / values / edges | Padded row bytes | Sweeps | Result digest |
| --- | --- | --- | --- | --- |
| small-diamond | 16 / 127 / 19 | 64 | 2 | `81cdbfe19f97334e` |
| medium-nested | 256 / 1,025 / 295 | 192 | 3 | `5d588c1b6a261367` |
| large-local | 4,096 / 4,097 / 8,206 | 576 | 5 | `08e2c185595a28d1` |
| large-dispatch | 4,096 / 4,097 / 6,546 | 576 | 3 | `e0892cd70ef36780` |

| CFG / implementation | Cycles/function, median [min-max] | Cycles/block/sweep, median [min-max] |
| --- | --- | --- |
| small-diamond / scalar | 518.5 [518.1-525.6] | 16.202 [16.192-16.426] |
| small-diamond / AVX-512 | 451.2 [439.6-463.8] | 14.099 [13.738-14.492] |
| medium-nested / scalar | 71,741.2 [71,551.5-71,810.1] | 93.413 [93.166-93.503] |
| medium-nested / AVX-512 | 20,976.5 [20,936.0-21,007.5] | 27.313 [27.260-27.354] |
| large-local / scalar | 8,346,945.2 [8,344,415.9-8,359,362.2] | 407.566 [407.442-408.172] |
| large-local / AVX-512 | 2,513,589.9 [2,508,434.4-2,515,582.1] | 122.734 [122.482-122.831] |
| large-dispatch / scalar | 5,010,762.8 [4,991,312.6-5,021,098.3] | 407.777 [406.194-408.618] |
| large-dispatch / AVX-512 | 1,492,668.8 [1,489,479.2-1,492,963.9] | 121.474 [121.214-121.498] |

| CFG | Scalar ns/function, median [min-max] | AVX-512 ns/function, median [min-max] |
| --- | --- | --- |
| small-diamond | 93.7 [93.6-95.0] | 81.5 [79.4-83.8] |
| medium-nested | 13,171.7 [13,144.4-13,187.4] | 3,862.5 [3,852.4-3,864.4] |
| large-local | 1,520,286.7 [1,519,406.2-1,529,181.5] | 457,702.9 [456,654.1-458,162.6] |
| large-dispatch | 916,281.2 [912,234.2-918,737.1] | 272,139.2 [271,721.6-272,464.9] |

The small, medium and two large cases used respectively 65,536, 1,024,
16 and 16 complete solves per sample, with no calibration restarts. The
shortest retained sample across all five processes was 3.938 ms; the shortest
small-case sample was 5.195 ms.

`dataflow_liveness` selects AVX-512. Ratios of the cycle medians above are
1.15x, 3.42x, 3.32x and 3.36x. The one-ZMM case has a modest margin:
paired process ratios were 1.13-1.18x, within about 1.1-1.2x. The scalar twin
remains the correctness oracle and benchmark alternative. These figures
replace the earlier 3.58x/5.43x/3.42x claims, which charged the scalar loop for
whole padding words and used one process. Version 2 also changes the
generated inputs, so this comparison does not isolate the gain of each edit.

Inspection of the measured binary confirmed the scalar bound
`(value_count + 63) >> 6` and 64-bit scalar `andn`, OR and XOR operations.
Both kernels load column bases at solve setup instead of reloading
`Dataflow` members in every chunk; some stack-spill reloads remain.
The vector kernel uses aligned 64-byte loads/stores, `vporq` and
`vpternlogq` with immediate `0xF4`. Clang folds the XOR-and-test into
`vpcmpneqq`, with `kmovd` and a scalar OR accumulating changes. Neither
solver uses gathers or scatters.

The default tests passed 17,132 checks. `test-dataflow --seed 7 --count 64`
passed 20,684, and seed 99 with the same count passed 20,668. Both solvers
agreed on all rows and sweep counts. All documented test commands in `AGENTS.md` and
`./build.sh check-call-graph` passed on the same host; the latter reported
8 translation units, 2,218 functions and zero call-graph cycles.

Raw logs, source hashes and toolchain provenance are retained on `benchpress`
in `~/buster-fast-bench/astra-01a158dd-143-review-evidence/`
(`benchmark-import-main-1.txt` through `benchmark-import-main-5.txt`). The measured
executable's SHA-256 is
`ce28f5590af9827515fc21be1bc450c4f2c181d13738ce8b676839d9cebb099a`.

Successor-count specializations and keeping a previous one-ZMM row in a
register remain optional follow-ups. Skipping unchanged blocks, storing rows
in postorder, IR construction and real corpus measurements remain deferred
until #131 provides representative functions. No end-to-end compiler speedup
or measured bandwidth floor is claimed.
