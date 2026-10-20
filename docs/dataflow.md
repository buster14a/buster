# Liveness and interference on bitsets

`dataflow.c` implements the part of [#143](https://github.com/buster14a/buster/issues/143)
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
Every vector access is inside the allocated padded row. Rows of one vector,
up to 512 values, take a loop of their own without the word loop, which
keeps its pointers and counters in registers. The two loops are separate
functions, so that Clang compiles each alone ("Where the AVX-512 solver's
frame falls" below has why).

The scalar twin visits the same blocks and successors in the same order,
but processes only `ceil(value_count / 64)` words per row. Whole padding
words remain zero after output initialization; charging the scalar loop for
them would inflate the vector comparison, particularly for short rows.
Its word loops disable Clang loop vectorization so the measured alternative
remains scalar, and its edge loop isn't unrolled ("Where the scalar twin's
code falls" below has why). Both count complete sweeps, including the final
sweep that changes nothing. Comparing iteration counts catches changes to the
update schedule as well as differences in the resulting bits.

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
PMCx1A0's umask 0x08 (raw event `0x1000008a0`), which AMD doesn't document
and #199 reads, from its controls, as the dispatch slots that Zen 5's per-core
power cap takes.
`--only scalar` or `--only avx512` times one solver alone, back to back, and
`--after-scalar MS` runs complete scalar solves of the workload, untimed, for
MS ms before every AVX-512 sample: "The power cap's state" below has why.

The benchmark includes output initialization in each solve, but excludes
generation and postorder construction. Both timed paths call explicit
`noinline` wrappers, so Clang cannot inline only the vector solver into the
repetition loop, and each wrapper starts a 64-byte line, so that the driver's
size doesn't decide where its loops fall in lines. `large-local` retains the
original control: 64 disconnected 64-block regions and mostly local live values.
Its mostly empty chunks make it primarily a row-streaming workload; it does
not model the liveness of one connected function. `large-dispatch` adds a
connected dispatcher with 512 arms, nested back edges, an exit and persistent
values live across arms. Both have thousands of blocks and values, but
neither measures `sqlite3VdbeExec` or another real program. A separate
row-traffic floor has not been measured, so proximity to a bandwidth limit
is not established.

## 9700X results and selection

Measured on 2026-10-20 on `benchpress`, Ryzen 7 9700X, Linux
6.18.50-2-lts, Clang 23.1.1 and glibc 2.44, for the build whose AVX-512
solver takes rows of one vector in a loop of their own ("Where the AVX-512
solver's frame falls"), whose scalar twin doesn't unroll its edge loop and
whose wrappers start 64-byte lines ("Where the scalar twin's code falls").
The `build.sh` builds at `-O3 -march=native`. Twenty independent benchmark
processes each ran 21 alternating passes, taking turns with twenty of #204's
build, which ran one-vector rows through the general loop. Each command
acquired and released the shared host lock separately, including each
benchmark process:

```sh
EXCLUSIVE_OWNER=claude-vector-stack ~/buster-fast-bench/exclusive.sh ./build.sh
EXCLUSIVE_OWNER=claude-vector-stack ~/buster-fast-bench/exclusive.sh taskset -c 2 ./build/compiler benchmark-dataflow
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
| small-diamond / scalar | 450.00 [449.3-462.9] | 14.0625 [14.039-14.467] |
| small-diamond / AVX-512 | 335.30 [334.8-436.9] | 10.4790 [10.461-13.654] |
| medium-nested / scalar | 57,402.50 [57,247.9-60,695.4] | 74.7430 [74.542-79.031] |
| medium-nested / AVX-512 | 20,905.45 [20,793.3-21,332.7] | 27.2210 [27.075-27.777] |
| large-local / scalar | 7,203,865.75 [7,195,344.4-7,219,687.4] | 351.7510 [351.335-352.524] |
| large-local / AVX-512 | 2,488,878.65 [2,484,487.4-2,512,491.0] | 121.5270 [121.313-122.680] |
| large-dispatch / scalar | 4,221,975.25 [4,216,003.9-4,236,411.5] | 343.5850 [343.099-344.760] |
| large-dispatch / AVX-512 | 1,485,458.15 [1,483,054.1-1,527,434.9] | 120.8865 [120.691-124.303] |

| CFG | Scalar ns/function, median [min-max] | AVX-512 ns/function, median [min-max] |
| --- | --- | --- |
| small-diamond | 81.30 [81.1-83.6] | 60.50 [60.4-78.9] |
| medium-nested | 10,552.55 [10,495.8-11,138.5] | 3,855.20 [3,848.8-3,928.6] |
| large-local | 1,312,028.60 [1,309,915.9-1,318,461.2] | 454,105.55 [453,034.8-458,828.8] |
| large-dispatch | 771,656.80 [770,510.3-775,758.9] | 271,677.70 [271,168.1-279,211.2] |

The small, medium and two large cases used respectively 131,072 or 65,536
(eighteen and two processes), 1,024 or 2,048 (nine and eleven), 16 and 16
complete solves per sample, with no calibration restarts. The shortest
retained sample across all twenty processes was 3.923 ms; the shortest
small-case sample was 5.146 ms. #204's build, in the same turns, took 455.85
[445.8-513.5] cycles per function on the small diamond with AVX-512, and
within 0.2% of these medians on the three larger CFGs, whose loop compiles to
the same instructions.

`dataflow_liveness` selects AVX-512. Ratios of the cycle medians above are
1.34x, 2.75x, 2.89x and 2.84x. On the three larger CFGs AVX-512 is 2.69 to
2.90 times as fast in every paired process. On the small diamond, whose rows
are one ZMM register each, the one-vector loop takes 26% fewer cycles than
#204's build did in the same turns, and AVX-512 is now 1.03 to 1.38 times as
fast as the scalar twin in every paired process, where #204's build measured
alike (paired ratios of 0.88-1.05, the scalar twin ahead in 9 of 20). The
scalar twin remains the correctness oracle and benchmark alternative. These
figures replace #204's 1.01x/2.75x/2.89x/2.85x, and before them #194's
1.16x/3.31x/3.34x/3.31x, whose scalar twin unrolled its edge loop by 8 and
took 16% to 21% more cycles (521.35, 69,457.20, 8,359,363.25 and 4,919,843.05
per function), and the 3.58x/5.43x/3.42x claims, which charged the scalar loop
for whole padding words and used one process. Version 2 also changes the
generated inputs, so this comparison does not isolate the gain of each edit.

The small case also varied by process with AVX-512. The
[independent re-review of b9a6c66/7793927](https://github.com/buster14a/buster/pull/194#pullrequestreview-5586061231)
reported process medians of 437.8-1,029.1 cycles against 518.4-534.0 for the
scalar twin then, including one process where vector was only 0.51x as fast,
and #204's twenty processes took 445.4-653.9. Five processes can miss such a
tail. Most of it followed where the solver's stack frame fell in its page,
through the slots that the general loop reloads in every block, which the
one-vector loop doesn't have ("Where the AVX-512 solver's frame falls"). The two processes
above 337 here, at 436.2 and 436.9, are in a state that follows the data
instead: some of the small diamond's out rows share their low 12 address bits
with its CFG columns, as the review found. It is a performance risk of this
layout, not a correctness failure.

The same dataflow source also moved by 2-3% between builds in that review.
The scalar twin's part of that was where its code fell in 64-byte lines,
which the edge loop's change removes. The AVX-512 solver measures within 0.2%
across builds when they take turns, but up to 2% apart in series run minutes
apart. Differences of a few percent should be treated as build/layout noise,
not evidence of an algorithmic improvement.

Inspection of the measured binary confirmed direct calls to both benchmark
wrappers. The scalar solver's body is compiled inside its wrapper. The
AVX-512 wrapper only tests the row width and the block count, then jumps,
as a tail call, to one of the solver's two functions, for one-vector rows or
the rest, each starting a 64-byte line. The scalar kernel uses the bound
`(value_count + 63) >> 6` and 64-bit scalar `andn`, OR and XOR operations.
Its word loop is unrolled by 4 around four copies of the edge loop, which
takes one edge per iteration, with a loop for the last words after it.
Both kernels load column bases at solve setup instead of reloading
`Dataflow` members in every chunk. The loop for one-vector rows keeps the
bases and its counters in registers, with no stack access in its block loop;
the general function's instructions are those of #204's wrapper, which store
two stack slots and reload about ten in every block. The vector kernel uses
aligned 64-byte loads/stores, `vporq` and `vpternlogq` with immediate `0xF4`.
Clang folds the XOR-and-test into `vpcmpneqq`, with `kmovb` or `kmovd` and a
scalar OR accumulating changes. Neither solver uses gathers or scatters.

The default tests passed 17,132 checks. `test-dataflow --seed 7 --count 64`
passed 20,684, and seed 99 with the same count passed 20,668. Both solvers
agreed on all rows and sweep counts. All documented test commands in
`AGENTS.md` and `./build.sh check-call-graph` passed on the same host; the
latter reported 11 translation units, 3,064 functions and zero call-graph
cycles.

Raw logs are retained on `benchpress` in `~/buster-fast-bench/layout-claude/vs/`
(`final-final.txt` for the twenty processes above and #204's twenty,
`final-tests.txt` for the tests, `final-states.txt` for "The power cap's
state", and `s1.txt` to `s14.txt`, `s13-sp.txt` and `one-*.txt` for "Where
the AVX-512 solver's frame falls"). The measured executable's SHA-256 is
`a409c4c1937d7d102bd2240396f1c564e3a762e4274eab35a898cad7b4c00ad9`. #204's
logs are in `~/buster-fast-bench/layout-claude/`, and #194's in
`~/buster-fast-bench/astra-01a158dd-143-rereview-evidence/`.

Successor-count specializations and keeping a previous one-ZMM row in a
register remain optional follow-ups. Skipping unchanged blocks, storing rows
in postorder, IR construction and real corpus measurements remain deferred
until #131 provides representative functions. No end-to-end compiler speedup
or measured bandwidth floor is claimed.

## Where the scalar twin's code falls

#201 found that the same scalar twin measured differently in two builds: with
its wrapper 16 bytes into a 64-byte line (main's build), large-dispatch took
399 to 411 cycles per block per sweep, and at a line start (#201's), 415 to
466, varying between processes. Clang emits the benchmark's driver before the
wrappers, so the driver's size decided where the solver's code fell in lines;
the code itself was the same byte for byte.

Measured on 2026-10-20 on `benchpress` (the kernel, Clang and glibc above),
every run under its own lock, `taskset -c 2`, in cycles per block per sweep,
with two tools. An experiment harness, outside the tree, holds byte-identical
copies of the wrapper at chosen offsets from a page start and times them in
turns in one process: sixteen placements, each offset in a line at four places
in a page. And the benchmark itself was built with its driver padded in a
branch that never runs, which moves the wrappers by as much. The harness, its
logs and the builds' logs are on `benchpress` in
`~/buster-fast-bench/layout-claude/`.

The sensitive code was what Clang made of the edge loop: unrolled by 8, with a
remainder loop. Blocks have a few successors, so nearly every word ran the
remainder: the word loop's latch and header (79 bytes), a jump to a loop of one
edge at a time 252 bytes on (45 bytes with its setup), and a jump back. A word
of a block with one successor took 30 instructions, one of them a 12-byte nop,
and two taken jumps, all from the op cache. In the harness, at a line start, a
chain CFG (every block with one successor) took 375 to 377 against 337 to 338
at the other offsets, medium-nested 111.8 against 90.4 to 92.2, and
large-dispatch 415 to 471 against 398 to 409; the small diamond, two words per
row, didn't depend on the place.

At a line start the jump's target, a 5-byte `lea`, begins 4 bytes before a
line ends, and the header's first instruction straddles another line boundary.
There the op cache misses about once per word (71 times per block per sweep,
against 1.2 at 16 bytes in and 0.3 at 48), the op queue is empty two to five
times as often (25 to 64 cycles per block per sweep, against 12 to 14), and
dispatch waits on the front end for 410 to 1,130 slots per block per sweep,
against 120 to 300. The decoders deliver about one op per block per sweep
wherever the code is, and mispredicts rise only from 0.93 to 1.22 per block per
sweep, so neither is the cause. At 32 bytes in the op cache misses as often,
at little cost. At
a line start the front end also settles into one of several states
(large-dispatch takes about 416, 422, 436, 443, 461 to 465 or 471), which last
hundreds of milliseconds, change rarely within a process, and differ between
processes even without ASLR: under `setarch -R`, with the same code, data and
stack addresses, one process took 422 and three 443. The cost also depends on
the code around the loop: medium-nested takes 111.8 at every line start the
harness tried, but 96 in #201's build, at the same offset in its page.

The stack's position isn't behind those states, but it is behind the small
diamond's differences between processes (16.3 to 17.7 in the harness): without
ASLR, moving the solver's frame down 16 bytes at a time over 4 KB, frames
moved 1,376 to 1,680 bytes took up to 15% longer, wherever the code was, with
more loads blocked by older stores they can't forward from
(`ls_bad_status2.stli_other`, 1.23 against 1.67 per block per sweep). ASLR
places the stack in 16-byte steps, so about one process in thirteen lands
there.

The edge loop isn't unrolled now (`unroll(disable)` in its pragma), and Clang
unrolls the word loop by 4 instead, which runs four words of a block with one
successor per taken branch. In the harness, at all sixteen placements,
large-dispatch took 342.8 to 344.8, medium-nested 74.8 to 75.3 (and 79.0 at
every placement in one process), large-local 351.5 to 353.4 (one placement had
a second state at 358 in three of five processes), and the small diamond 14.1
to 14.7, by process rather than place; in the stack's window it takes up to 8%
longer. What didn't work: `aligned(64)` on the old wrapper fixes it at a line
start, the slow place; `[[clang::code_align(64)]]` on the loops aligns blocks
off the hot path; and disabling both loops' unrolling makes Clang unroll the
block loop by 2, which still varies by 2.8% with the place.

Both wrappers also start a 64-byte line now (`aligned(64)`). With the edge loop
not unrolled, the four offsets in a line still differed by up to 0.6%
(large-dispatch 343.4 to 345.6 in the benchmark); aligned, the driver moves
the wrappers only by whole lines. In the benchmark, five processes per build,
the scalar twin:

| CFG | Unrolled by 8, wrapper 0 / 16 / 32 / 48 bytes into a line | Not unrolled and aligned, every build |
| --- | --- | ---: |
| small-diamond | 16.1-16.4 / 16.2-16.7 / 16.2-16.7 / 16.2-16.5 | 14.0-15.0 |
| medium-nested | 95.8-96.6 / 93.2-94.1 / 90.5-94.6 / 90.5-94.2 | 74.3-75.0 |
| large-local | 409.6-410.6 / 407.1-408.2 / 406.4-412.1 / 407.2-409.0 | 351.7-353.1 |
| large-dispatch | 415.5-465.1 / 397.8-406.7 / 405.1-408.4 / 398.5-399.0 | 343.4-344.6 |

The first builds are #201's with its driver padded by 0, 16, 32 and 48 bytes.
The others are this change's on #201's head as built, with its driver padded
by 16, 32, 48, 304 (as much as #201's driver grew) and 1,000 bytes, which moved
the wrappers 0, 0, 64, 320 and 1,024 bytes, and with a line printing the
process ID added to the driver's header, which didn't move them; medium-nested
also took 76.2 and 78.9 in one process each. The AVX-512 solver measured alike
in all of them. Rebased onto main, whose changes to the units before this one
moved the wrappers 0x82800 bytes, the change measured the same again ("9700X
results and selection").

## Where the AVX-512 solver's frame falls

On the small diamond, each process's AVX-512 passes stayed within a few cycles
of their median, but the medians of processes spread much wider: 445.4 to
653.9 cycles per function over #204's twenty processes, and 437.8 to 1,029.1
in #194's re-review. Most of that spread followed where the solver's stack
frame fell in its page, and the rest the data's layout (the last paragraph
below).

Measured on 2026-10-20 on `benchpress` (the kernel, Clang and glibc above),
every run under its own lock, `taskset -c 2`, in cycles per function, with the
harness of "Where the scalar twin's code falls" given copies of the AVX-512
wrapper too. Each copy compiles to the benchmark wrapper's instructions and
solves a flow laid out as the benchmark lays out its second, so every address
has the same low 12 bits. Without ASLR (`setarch -R`), every process had the
same addresses, and the frame moved down 16 bytes per sample across 4 KB,
starting at two places. The harness, its logs and the builds' logs are on
`benchpress` in `~/buster-fast-bench/layout-claude/vs/`.

The general loop keeps part of its state on the stack: in every block it
stores the block's place in the postorder and a row pointer, and reloads ten
slots. The small diamond's four matrices are 1 KB each, so every slot shares
its low 12 bits with one of their rows. At the 134 of the 256 places where a
slot shares them with an in or out row, which the sweep stores, the solve took
456.3 to 478.1 cycles (each place's median over eight sweeps), with twice as
many loads blocked by an older store they can't forward from
(`ls_bad_status2.stli_other`, a median of 1.18 against 0.56 per block per
sweep). Where the slots share them only with use or def rows, which the sweep
only loads, it took 453.7 to 456.8.

At places of the first kind some processes also fall into slower states: about
500, 600, 670 to 720 and 800 to 1,117 cycles, which last until the frame moves
away and change little within a process. With the frame held at one place for
a run, 23 of 102 processes at nine places of the first kind were in one, and
none of 38 at two places of the second, though every process of a build had
the same addresses. In the 670-to-720 state, blocked loads and forwarded ones
(`ls_stlf`) both fall, to 0.4 and 3.4 per block per sweep from 1.1 and 4.2, as
if a load waited for a store to commit instead of trying it; in the slowest
states both rise.

The benchmark shows the same. `main` aligns its frame to 64 bytes, so the
wrappers' frames take one of 64 places in a page under ASLR. Sixty-four
processes ran under `perf record --user-regs=sp`, sampling every 20 million
cycles, which gave each process's place. The 35 whose slots shared low bits
with in or out rows took 458.6 cycles (the median; 450.6 to 942.7, four above
480), and the 29 others 446.5 (445.3 to 513.6, one above 480).

Rows of one vector now take a loop without the word loop, which keeps every
pointer and counter in registers, with no stack access in its block loop. It
is a function of its own, and so is the general loop: inlined into one
function with it, the general loop compiled with other registers and stack
slots, and medium-nested took 3.6% more cycles after 100 ms of scalar code
(31.82 against 30.72 cycles per block per sweep, in five processes of each
build taking turns, and again in another five). Neither its block loop nor
its edge loop is unrolled. Clang unrolls the edge loop by 8 otherwise, whose
remainder loop then takes a register and leaves a base to reload in every
block, and the solve took 364.7 to 374.3 cycles in the harness, depending on
the place again; alone in its function, Clang unrolls the block loop by 2,
which leaves `postorder`'s base on the stack. As committed, the solve took
343.5 to 345.6 cycles at every place (four sweeps), and 341.6 to 344.7 with the
function at each of the four places of a 64-byte line. In the benchmark,
sixty-four processes took a median of 335.4 cycles where the general loop's
slots would share low bits with in or out rows (28 processes) and 335.6 where
they wouldn't (36).

What remains follows the data instead. The benchmark lays each flow's CFG
columns out before its four matrices, so that out rows 12 to 15, which every
sweep stores first, sit exactly 4 KB past `successor_offsets`, `postorder`
and `successors`, as #194's re-review found. With the one-vector loop, 5 of
92 harness processes ran at 445 to 451 cycles, at places of both kinds; with
the columns moved to share their low 12 bits with use rows only, or the
matrices moved to a page start, the loop took 312 to 322 cycles, and none of
68 processes was slower. The benchmark's processes fall into it too: 6 of the
sixty-four above, at 436.0 to 463.4 and at places of both kinds, and 2 of the
twenty in "9700X results and selection". Built with `dataflow_create`
allocating the matrices before the columns, whose low 12 bits then fall among
use rows', twenty processes of the loop as first written, inlined beside the
general one, took 320.05 [317.5-321.8] cycles, none slower, and the scalar
twin 427.30 [426.6-438.1], against 338.15 and 452.00 for the same loop and
today's order in the same turns. That order would just move the alias to
other sizes, though: whether a function's columns share low bits with a row
the sweep stores depends on its sizes and on where its arena stands, so the
layout stays as it was.

## The power cap's state

A compiler runs liveness after lighter phases, and on Zen 5 that changes how
fast the AVX-512 solver runs ([#199's
report](https://github.com/buster14a/buster/issues/199#issuecomment-6297314071)
has the controls behind this section's mechanism, and says which parts are
inferred). Code that draws less than about 17 W per
core (scalar phases, the vectorized lexer, even a spin or a sleep) lets the
9700X's firmware boost the clock from about 5.31 to 5.52 GHz over 30 to
300 ms. At that clock a heavy vector loop would draw more than the core's cap
of about 21.5 W. The core holds at the cap while the loop loses dispatch slots,
which #199 reads as a fast throttle withholding them, until the firmware has
stepped the clock down to what the loop sustains, 5 to 20 ms later. The
throttle column below is that reading of PMCx1A0's umask 0x08 (see "Measuring
the alternatives"). Run on its own, the AVX-512 solver draws about 20 W and
holds the clock at 5.34 to 5.44 GHz (#199, and the table below), so it is such
a loop.

`benchmark-dataflow` measures it in three states: alone, back to back
(`--only avx512`), where the clock settles; after 100 ms of complete scalar
solves of the same workload before each sample (`--after-scalar 100`), as
after a compiler's lighter phases; and by default, where its samples follow the
scalar ones (6 to 25 ms each) in alternate passes, a mix of the two. Measured on
2026-10-20 on `benchpress` (the same kernel, Clang and glibc as above) with
#201's build, five processes of each state, each under its own lock,
`taskset -c 2`; the medians of the process medians, AVX-512 solver:

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
by its own slowdown. With the build whose scalar twin doesn't unroll its edge
loop (five processes of each state, by turns: the scalar twin alone 82.3,
10,416.3, 1,301,694.2 and 762,853.5 ns per function, the AVX-512 solver alone
80.5, 3,862.5, 452,158.8 and 271,813.6, and after 100 ms 82.1, 4,258.9,
478,179.4 and 293,561.9), AVX-512 is 2.4 to 2.7 times as fast on the three
larger CFGs after scalar code and 2.7 to 2.9 times alone, and as fast as the
scalar twin on the small diamond. AVX-512 stays the selection.

#201's build measured the scalar twin differently from main's because the
driver before it grew; "Where the scalar twin's code falls" has why, and the
change that removed it. Taking turns with #201's build, the AVX-512 solver
alone measures within 0.2% of it, and after 100 ms of the new, faster scalar
twin it takes up to 1% more cycles (large-dispatch 130.3 against 129.2), so
the table above stands. In the default state its samples now follow shorter
scalar ones, and they are throttled less and run up to 1% faster: over the
twenty processes in "9700X results and selection", large-dispatch took 120.7
cycles per block per sweep with 70 slots of throttle, and large-local 121.7
with 101.

With the AVX-512 solver's loop for one-vector rows (five processes of each
state, taking turns with #204's build), the small diamond takes 60.5 ns per
function (10.48 cycles per block per sweep) alone and 60.5 ns (10.47) after
100 ms of scalar code, against 83.2 and 82.7 ns for #204's build, with 0.05
slots of throttle per block per sweep. It is still too light to be capped, so
it is 1.34 times as fast as the scalar twin in every state. The three larger
CFGs, whose loop compiles as before, took within 0.2% of #204's build alone,
and 30.71, 129.24 and 130.84 cycles per block per sweep after 100 ms against
31.37, 130.84 and 132.62; in that state either build varies by about 2% from
one set of five processes to the next.
