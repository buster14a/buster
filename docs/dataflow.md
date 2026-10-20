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
relabeled as CPU cycles. It also gives the throttle per block per iteration:
the dispatch slots that Zen 5's per-core power cap takes, which PMCx1A0's
undocumented umask 0x08 counts (raw event `0x1000008a0`, #199).
`--only scalar` or `--only avx512` times one solver alone, back to back, and
`--after-scalar MS` runs complete scalar solves of the workload, untimed, for
MS ms before every AVX-512 sample: "The power cap's state" below has why.

The benchmark includes output initialization in each solve, but excludes
generation and postorder construction. Both timed paths call explicit
`noinline` wrappers, so Clang cannot inline only the vector solver into the
repetition loop. `large-local` retains the original
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
with main `2c6ec00` merged in and matching non-inlined benchmark wrappers. The
`build.sh` builds at `-O3 -march=native`. Twenty independent benchmark processes
each ran 21 alternating passes. Each command acquired and released the shared
host lock separately, including each benchmark process:

```sh
EXCLUSIVE_OWNER=astra ~/buster-fast-bench/exclusive.sh ./build.sh
EXCLUSIVE_OWNER=astra ~/buster-fast-bench/exclusive.sh taskset -c 2 ./build/compiler benchmark-dataflow
```

The tables report the median of those twenty process medians, followed by their
`[min-max]` range. They are not the within-process pass ranges printed by the
command. For this even process count, the median averages the middle two
values. Hardware core-cycle counters were available in all twenty processes.

Generator version 2, seed 1:

| CFG | Blocks / values / edges | Padded row bytes | Sweeps | Result digest |
| --- | --- | --- | --- | --- |
| small-diamond | 16 / 127 / 19 | 64 | 2 | `81cdbfe19f97334e` |
| medium-nested | 256 / 1,025 / 295 | 192 | 3 | `5d588c1b6a261367` |
| large-local | 4,096 / 4,097 / 8,206 | 576 | 5 | `08e2c185595a28d1` |
| large-dispatch | 4,096 / 4,097 / 6,546 | 576 | 3 | `e0892cd70ef36780` |

| CFG / implementation | Cycles/function, median [min-max] | Cycles/block/sweep, median [min-max] |
| --- | --- | --- |
| small-diamond / scalar | 521.35 [517.1-541.5] | 16.2920 [16.161-16.922] |
| small-diamond / AVX-512 | 448.70 [446.1-508.8] | 14.0225 [13.939-15.901] |
| medium-nested / scalar | 69,457.20 [69,407.9-72,335.3] | 90.4390 [90.375-94.187] |
| medium-nested / AVX-512 | 20,976.90 [20,922.9-21,404.5] | 27.3140 [27.243-27.870] |
| large-local / scalar | 8,359,363.25 [8,327,271.6-8,467,912.8] | 408.1720 [406.605-413.472] |
| large-local / AVX-512 | 2,506,395.60 [2,503,280.9-2,586,904.8] | 122.3825 [122.231-126.314] |
| large-dispatch / scalar | 4,919,843.05 [4,894,620.6-4,955,895.0] | 400.3775 [398.325-403.312] |
| large-dispatch / AVX-512 | 1,485,373.75 [1,482,552.5-1,528,228.9] | 120.8800 [120.650-124.368] |

| CFG | Scalar ns/function, median [min-max] | AVX-512 ns/function, median [min-max] |
| --- | --- | --- |
| small-diamond | 94.30 [93.6-98.4] | 81.05 [80.6-91.9] |
| medium-nested | 12,800.55 [12,771.2-13,323.3] | 3,873.55 [3,864.2-3,948.0] |
| large-local | 1,523,340.90 [1,516,581.6-1,542,258.5] | 456,429.65 [455,657.2-470,406.1] |
| large-dispatch | 900,102.75 [893,469.7-906,036.4] | 271,313.90 [270,539.9-278,814.2] |

The small, medium and two large cases used respectively 65,536, 1,024,
16 and 16 complete solves per sample, with no calibration restarts. The
shortest retained sample across all twenty processes was 3.947 ms; the shortest
small-case sample was 5.271 ms.

`dataflow_liveness` selects AVX-512. Ratios of the cycle medians above are
1.16x, 3.31x, 3.34x and 3.31x. The one-ZMM case has a modest margin:
paired process ratios were 1.025-1.208x. The worst vector process took
508.8 cycles against its paired scalar's 521.7; none of these twenty
processes reversed the comparison. The scalar twin
remains the correctness oracle and benchmark alternative. These figures
replace the earlier 3.58x/5.43x/3.42x claims, which charged the scalar loop for
whole padding words and used one process. Version 2 also changes the
generated inputs, so this comparison does not isolate the gain of each edit.

The small-case margin is not a guarantee for every process. The
[independent re-review of b9a6c66/7793927](https://github.com/buster14a/buster-fast/pull/194#pullrequestreview-5586061231)
reported AVX-512 process medians of 437.8-1,029.1 cycles against scalar
518.4-534.0, including one process where vector was only 0.51x as fast.
Five processes can miss this slower tail. The review's counter probes
associated it with loads blocked by older stores and reduced forwarding;
some output rows also share their low 12 address bits with CFG columns.
Those observations identify a performance risk in the current layout and
per-sweep output stores, not a correctness failure. Matching call boundaries
does not establish that the tail is eliminated. Store-once or one-ZMM
specializations remain optional follow-ups.

The same dataflow source also moved by 2-3% between builds in that review.
Differences of a few percent should be treated as build/layout noise, not
evidence of an algorithmic improvement.

Inspection of the measured binary confirmed direct calls to both benchmark
wrappers, with each solver body compiled inside its wrapper. The scalar
kernel uses the bound `(value_count + 63) >> 6` and 64-bit scalar `andn`,
OR and XOR operations.
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
9 translation units, 2,267 functions and zero call-graph cycles.

Raw logs, source hashes and toolchain provenance are retained on `benchpress`
in `~/buster-fast-bench/astra-01a158dd-143-rereview-evidence/`
(`benchmark-1.txt` through `benchmark-20.txt`). The measured
executable's SHA-256 is
`0668d939b9b66b081f78da29181adea5fe383bf5d1b1e078cec5755cf3330bc4`.

Successor-count specializations and keeping a previous one-ZMM row in a
register remain optional follow-ups. Skipping unchanged blocks, storing rows
in postorder, IR construction and real corpus measurements remain deferred
until #131 provides representative functions. No end-to-end compiler speedup
or measured bandwidth floor is claimed.

## The power cap's state

A compiler runs liveness after lighter phases, and on Zen 5 that changes how
fast the AVX-512 solver runs (#199). Code that draws less than about 17 W per
core (scalar phases, the vectorized lexer, even a spin or a sleep) lets the
9700X's firmware boost the clock from about 5.31 to 5.52 GHz over 30 to
300 ms. At that clock a heavy vector loop would draw more than the core's cap
of about 21.5 W, and a fast throttle withholds dispatch slots from it until the
firmware has stepped the clock down to what the loop sustains, 5 to 20 ms
later. Run on its own, the AVX-512 solver draws about 20 W and holds the clock
at 5.34 to 5.44 GHz (#199, and the table below), so it is such a loop.

`benchmark-dataflow` measures it in three states: alone, back to back
(`--only avx512`), where the clock settles; after 100 ms of complete scalar
solves of the same workload before each sample (`--after-scalar 100`), as
after a compiler's lighter phases; and by default, where its samples follow the
scalar ones (6 to 25 ms each) in alternate passes, a mix of the two. Measured on
2026-10-20 on `benchpress` (the same kernel, Clang and glibc as above), five
processes of each state, each under its own lock, `taskset -c 2`; the medians
of the process medians, AVX-512 solver:

| CFG | State | ns/function | Cycles/block/sweep | GHz | Throttle/block/sweep |
| --- | --- | ---: | ---: | ---: | ---: |
| small-diamond | alone | 81.2 | 14.06 | 5.54 | 0.15 |
| | default | 83.0 | 14.37 | 5.53 | 0.17 |
| | after 100 ms | 81.1 | 14.05 | 5.54 | 0.16 |
| medium-nested | alone | 3,869.5 | 27.12 | 5.38 | 4.7 |
| | default | 3,857.0 | 27.22 | 5.42 | 23.4 |
| | after 100 ms | 4,280.9 | 30.76 | 5.52 | 80.5 |
| large-local | alone | 451,232.6 | 119.67 | 5.43 | 12.9 |
| | default | 457,412.9 | 122.72 | 5.49 | 130.2 |
| | after 100 ms | 475,444.3 | 128.29 | 5.53 | 239.4 |
| large-dispatch | alone | 270,330.2 | 119.77 | 5.44 | 11.8 |
| | default | 272,109.8 | 121.63 | 5.49 | 121.6 |
| | after 100 ms | 286,803.0 | 129.21 | 5.54 | 268.5 |

After the scalar solves, the three larger CFGs take 5% to 11% longer: 7% to
13% more cycles at a 2% to 3% higher clock, with the throttle taking 80 to 270
slots per block per sweep, against 5 to 13 alone. The small diamond's rows are
one ZMM register each, and its solver draws too little to be capped. 300 ms
measured the same as 100 ms (two processes, within 0.6%), and so did 30 ms
(within 1%): a sample of 4 to 7 ms is too short for the clock to come down, so
the boost carries over from one to the next. From a settled clock, 30 ms of
scalar code barely boosts it and 100 ms gives most of the effect (#199), so
use 100 to 300 ms. The results above are the default state's.

The scalar twin draws too little to be capped (a few slots per block per sweep
at most, in every state), so after scalar code the AVX-512 solver's lead shrinks
by its own slowdown: against main's build's scalar twin (five processes:
13,332.5, 1,540,493.9 and 922,677.6 ns per function), it is 3.1 to 3.2 times
as fast on the three larger CFGs after scalar code and 3.4 times alone, and
1.15 times on the small diamond either way. AVX-512 stays the selection.

This change's build measures the scalar twin differently from main's. Clang
emits the benchmark's driver before the solver wrappers, and the driver grew,
which moved the wrappers 304 bytes, from 16 bytes into a 64-byte line to its
start. The AVX-512 solver measures the same in both builds, but the scalar
twin's large-dispatch solves take 415 to 466 cycles per block per sweep over
twenty processes, against main's 410 to 411 over five; its medium-nested ones
take about 2% more and its large-local ones about 1% fewer. An earlier build of
this change, with the wrappers 16 bytes in, measured 399 to 411, and one with
them aligned to 64 bytes measured as this build does. Read the scalar twin's
numbers from a build together with its layout.
