# Branch sizing

`src/buster/compiler/branch_sizing.c` chooses the form of each branch inside a
function before the function is encoded: the 2-byte rel8 form (`EB rel8`,
`7x rel8`) where the displacement fits in [−128, 127], and the rel32 form
(`E9 rel32`, `0F 8x rel32`, 3 and 4 bytes longer) where it doesn't
([#144](https://github.com/buster14a/buster-fast/issues/144)). It is what the
roadmap calls branch relaxation, and what `docs/x86-64-encoder.md` ("Labels")
leaves for a pass before encoding. This document records the contract, the
design, the statistics of compiled code that the benchmark's functions follow,
and the Zen 5 measurements behind each choice.

## Contract

Contracts 1 and 2 of
[the coordination comment on #135](https://github.com/buster14a/buster-fast/issues/135#issuecomment-6293200394):

- **Input:** a function's `X86Instruction` records, every branch to a label
  in its rel8 form (`JMP_REL8` or one of the 16 `Jcc_REL8`) with displacement
  0, and two u32 columns: the branch records' indices, ascending, and each
  branch's target, the index of the record its label marks (the record count
  for the function's end). Calls and other symbolic operands are records
  marked `BUSTER_X86_FORM_FIXUP`, which sizing leaves alone, and it never
  changes an index, so `link.c`'s references stay valid.
- **Output:** each branch's final form and displacement in its record, so
  that every rel8 is in range and `x86_validate` passes; each record's offset
  in a u32 column, and the function's length. The caller then encodes the
  records with `x86_encode` (and its fixup ends) and adds their fixups with
  `link_add_code_fixups`.
- **Lengths** come from `x86_encode`'s offsets, so the encoder doesn't change;
  a lengths-only entry point is for later, with #182.

`size_branches` is the vector path and `size_branches_scalar` its twin: the
same phases a branch or a record at a time, the oracle of the tests, with the
same result, rounds included. Like `x86_encode`, both are internal functions of
the code generator: they take what `branch_sizing_validate` accepts and check
nothing about it in their loops, as the owner decided for the encoder in #182
("validation belongs to the tests and to debugging"). `branch_sizing_validate`
checks the record count, then each branch in order (its index below the count
and above the one before, its target at most the count, its form a branch's
rel8 one, without a segment override or the fixup flag), then every record
with `x86_validate`, and reports the first error. The two paths fail only on
what costs nothing to check: more records than `x86_encode` takes, a function
that could pass `INT32_MAX` bytes with every branch long, or no scratch
memory; then the records are as they were.

## Results

On the Ryzen 7 9700X, CPU 2, `benchmark-branch-sizing`'s medians, in cycles per
record (instruction). Each row is a pool of about 262,000 records of generated
functions: shaped like clang's code (see "Compiled code"), tier 0's like -O0's
and optimized ones like -O2's, of the records the row's number says or, for
`mix`, of sqlite3's -O0 function sizes; and chains of 1,000 links. `x86_encode`
is the caller's encode of the same records after sizing, the reference.

| Row | Functions | Long branches | Rounds | `x86_encode` | `size_branches` | `size_branches_scalar` | Sizing ÷ encode | Vector ÷ scalar |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| tier0-mix | 2,814 | 21.8% | 1.42 | 4.02 | **7.63** | 12.08 | 1.90 | 1.58 |
| tier0-40 | 6,554 | 4.9% | 1.20 | 4.35 | 8.91 | 9.87 | 2.05 | 1.11 |
| tier0-250 | 1,049 | 25.9% | 2.11 | 3.26 | 5.71 | 10.67 | 1.75 | 1.87 |
| tier0-2000 | 132 | 32.1% | 2.73 | 3.22 | 5.24 | 10.27 | 1.63 | 1.96 |
| tier0-10000 | 27 | 34.3% | 3.11 | 3.19 | 5.47 | 10.48 | 1.72 | 1.92 |
| optimized-40 | 6,554 | 5.6% | 1.28 | 4.30 | 9.53 | 11.65 | 2.22 | 1.22 |
| optimized-250 | 1,049 | 32.8% | 2.25 | 3.26 | 6.21 | 12.94 | 1.91 | 2.08 |
| optimized-2000 | 132 | 40.5% | 2.92 | 3.23 | 5.70 | 12.61 | 1.77 | 2.21 |
| optimized-10000 | 27 | 42.8% | 3.22 | 3.19 | 5.99 | 12.85 | 1.88 | 2.15 |
| chain-forward | 18 | 100% | 5 | 3.14 | 14.01 | 42.30 | 4.46 | 3.02 |
| chain-backward | 18 | 100% | 5 | 3.14 | 14.06 | 42.63 | 4.47 | 3.03 |

- `size_branches` costs 1.63 to 2.22 times an encode of the same sized
  function (1.90 on the mix), and the first round's `x86_encode`, its lengths
  phase, is 58% of it on the mix: sizing proper costs about 3.2 cycles per
  record there, and 2.0 on functions of 2,000 records.
- The vector path is 1.87 to 2.21 times its scalar twin on functions of 250
  records and more, 1.58 times on the mix, and 1.1 to 1.2 times on functions of
  40, whose costs are the calls' and the gathers' latency more than their work.
- A chain of 1,000 branches, each pushed out of range by the next (a round per
  link), costs 14.0 cycles per record, 3 times less than the scalar twin:
  linear, through the worklist.
- Every result is the test's reference's (relaxation over the records, as an
  assembler does it) and GNU as's: on 404 functions with 98,920 long branches,
  its relaxation of the same functions printed with labels gives the same
  bytes.

## Design

### Branch space

The rounds work on the branches alone. With every branch short, branch k (at
record r_k, aiming at record t_k) has the displacement

    D0_k = offset(t_k) - offset(r_k) - 2

that `x86_encode`'s offsets give (the first round's lengths). With growths g
(0 for a short branch, 3 for a long jmp, 4 for a long jcc) and P their
exclusive prefix sums over the branches, its displacement is

    d_k = D0_k + P[rank_k] - P[k + 1]

where rank_k is the number of branches before record t_k: the growth between
its end and its target, which for a backward branch (rank_k ≤ k) is negative
and holds its own. So a round is one pass over the branches, 16 at a time:
gather P at the ranks, compare the displacements with the rel8 range into a
mask, grow the masked branches (a long branch stays long), and take the next
round's prefix sums with in-register scans (four `valignd` and adds, and the
last lane carried). P is double-buffered: a round reads the one and writes the
other. The offsets change once, at the end.

A displacement's magnitude only grows with the growths in its span, so the
rounds grow a branch only when it is out of range with lengths no longer than
the smallest fixpoint's: every order of growing ends at that fixpoint, the
unique smallest one, which is also GNU as's (it starts every branch short and
grows the ones out of range until none is). Both paths do the same Jacobi
rounds, so the twin agrees with the vector path round for round.

### A short branch's reach, and the worklist

Every branch is at least 2 bytes, so one with 64 branches or more between it
and its target is out of range with all of them short and grows in the first
round. A branch that is short after that has at most 63 branches in its span:
a growth can change only the displacements of the short branches within 63
branches of it, and their displacements are sums of at most 64 growths.

Each round settles the branches whose spans held a branch that grew in the
round before, so a chain of branches each pushed out of range by the next takes
a round per link: quadratic, if the rounds went on. Past
`BUSTER_BRANCH_SIZING_ROUNDS` rounds, `branch_sizing_worklist` grows the rest
one branch at a time: it marks, in a bit per branch, the short branches whose
spans hold a branch the last round grew (found by comparing the two prefix
buffers), takes the lowest marked branch, sums the at most 64 growths in its
span, and if it is out of range grows it and marks its own neighbours. A
verification round follows. Each growth costs a bounded amount of work, so
sizing is linear in the branches whatever the function's shape. The vector
path marks 16 neighbours at a time (their indices and ranks against the grown
branch's, and their growths against zero, as a mask), the scalar path one at a
time, and both mark the same branches.

### Ranks

The ranks are computed only once a branch has grown in the first round: most
small functions settle there, with every displacement D0. The table has an
entry per 32 records, their branch bits in the low half and the number of
branches before them in the high half, so a rank is an entry's count plus a
popcount of its bits below the target's. The vector path builds the bits from
a byte map with `vptestmb` (64 records at a time), gathers each target's entry
as a qword (8 at a time) and counts with `vpopcntd`.

### Offsets

Each record's final offset is its first round's plus P at its rank. The pass
starts at the table entry that holds the first long branch, since the records
before it keep theirs. The vector path takes 16 records at a time: the branches
before them come from the table, the branches among them before each record
from a `vpopcntd` of their branch bits masked per lane (at most 15), and a
`vpermd` looks each one's P up among the 16 from there. No step waits for the
one before.

### Lengths and memory

The first round's `x86_encode` runs `BUSTER_BRANCH_SIZING_CHUNK` (1,024)
records at a time into one scratch buffer, whose bytes are dropped: 15 KB that
stay in L1, where the whole function's could take megabytes of fresh pages.
Each chunk's offsets move by the bytes before it. Every column comes from one
push on the thread's scratch arena (lane.c), cut into cache-line-aligned
columns, before anything is written: a push per column cost small functions
tens of cycles.

## Compiled code

The benchmark and the tests generate functions (`branch_sizing_generate.c`)
until the code generator's exist (#136). Their shapes follow what clang 23
makes of sqlite3 3.53 and Lua 5.4.8 (`clang -c` at `-O0` and `-O2`, read back
with `objdump -d -r`: every direct `jmp` or `jcc` whose target is inside its
function, by the instruction its target is):

| | sqlite3 -O0 | Lua -O0 | sqlite3 -O2 | Lua -O2 |
| --- | ---: | ---: | ---: | ---: |
| Functions | 2,603 | 1,052 | 1,576 | 635 |
| Instructions | 236,385 | 65,216 | 248,719 | 51,740 |
| Instructions per branch | 7.94 | 11.16 | 5.99 | 7.85 |
| jmp | 43.7% | 46.0% | 27.7% | 29.8% |
| Backward | 5.5% | 5.3% | 22.4% | 23.9% |
| Long (rel32) | 22.5% | 16.2% | 35.2% | 28.6% |
| Distance, median (instructions) | 8 | 8 | 17 | 16 |
| Distance, 90th percentile | 102 | 53 | 228 | 127 |
| Distance, 99th percentile | 3,440 | 1,639 | 4,340 | 793 |
| Bytes per instruction | 4.10 | | 4.00 | |

Long branches are rare in small functions and common in large ones: in
sqlite3 at -O0, 2.9% of the branches of functions of 16 to 63 instructions,
21.0% of 64 to 255, 27.5% of 256 to 1,023, 32.6% of 1,024 to 4,095 and 35.1%
past that; at -O2, 3.9%, 26.0%, 40.2%, 44.7% and 56.1%. Half the functions
have at most 41 instructions, but most instructions are in functions of 64 to
1,023.

The generator's compiler-like shape takes a branch every `branch_period`
records, a jmp `jmp_per_mille` times in a thousand, a target within `near`
records (or, `far_per_mille` times in a thousand, within the function's size)
at a distance whose logarithm is about uniform (an octave, then a distance in
it), backward `backward_per_mille` times in a thousand. The benchmark's tier 0
profile (a branch every 9 records, 45% jmp, 5.5% backward, near 64, far 15%)
gives functions of 40, 250, 2,000 and 10,000 records 4.9%, 25.9%, 32.1% and
34.3% long branches, against -O0's 2.9%, 21.0% to 27.5%, 32.6% and 35.1%; its
mix of sqlite3's -O0 function sizes (the table of quantiles in
`branch_sizing_benchmark_sizes`, 90.8 records per function on average, as
sqlite3's) gives 21.8% against 22.5%, at 4.06 bytes per record against 4.10.
The optimized profile (a branch every 7 records, 29% jmp, 23% backward, near
128, far 20%) gives 5.6%, 32.8%, 40.5% and 42.8%, against -O2's 3.9%, 26.0% to
40.2%, 44.7% and 56.1%: close, if a little short on the largest functions.

## Zen 5's costs, measured

Every number here is a median of `benchmark-branch-sizing` on the 9700X, CPU 2,
and every comparison was run three times in turn under the machine's lock
(the same build measures within about 1.5% from run to run, so smaller
differences are noise).

### The benchmark's method

A sample sizes a pool of about 262,000 records of different functions, a group
of at least 2,048 records at a time, around rdpmc reads of this thread's cycles
and instructions (66 cycles a read pair, subtracted). Before each group,
outside the time, the branches' records are put back as generated (or, for the
reference `x86_encode`, as sized, the records the caller encodes; the warm-up
checks that every whole run comes to the sized functions' lengths) and the
group's records and branch columns read, so that they are in cache as a
function the code generator has just written is. The pool is larger than the
branch predictor learns: the first version sized a pool of 33,000 records 13
times a sample, and the scalar path's offsets phase ran three times faster
there (11.5 cycles per branch) than on a pool of 137,000 (33.9), its branches
learnt. A phase's cost is the difference between runs that stop after it and
before it (`branch_sizing_benchmark_through`, which runs `branch_sizing_run`'s
phases in its order, checked to give the same records and offsets).

### The phases

In cycles per branch, and the whole run in cycles per record:

| Phase | tier0-mix vector | scalar | tier0-2000 vector | scalar | optimized-2000 vector | scalar |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| lengths (the first round's `x86_encode`, and the columns) | 39.87 | 39.84 | 28.66 | 28.49 | 22.37 | 22.25 |
| setup (D0 and the first round) | 8.55 | 23.33 | 4.23 | 18.69 | 4.15 | 21.61 |
| ranks | 4.90 | 6.28 | 2.91 | 1.40 | 2.67 | 3.57 |
| rounds | 4.33 | 12.48 | 5.75 | 18.30 | 6.20 | 22.62 |
| offsets | 5.54 | 16.88 | 3.45 | 16.64 | 2.71 | 13.34 |
| write (the records) | 4.46 | 3.01 | 1.92 | 0.53 | 1.96 | 1.13 |
| whole, per record | 7.51 | 11.30 | 5.24 | 9.40 | 5.70 | 12.03 |

A phase's cost here is what it adds in its place, not what it costs alone: the
scalar setup leaves the core idle enough that the scalar ranks phase measures
1.4 cycles per branch after it, though the vector path, given that same
function, takes 5.4 (see "What was tried"). The write phase is the same code on
both paths. The lengths phase is the encode, plus the per-call costs (the
scratch columns, the chunks) that small functions feel: about 10 cycles per
branch over the caller's encode on 40-record functions (about 40 a call), 3.6
on the mix, and none measurable on large ones. The vector path's setup does
three gathers per 16 branches (the branch records' offsets, the targets' and
the forms), the ranks phase two of 8 qwords, and each round one.

### What was tried

The handoff of #144 proposed two alternatives to the vector path's gathers,
which the benchmark keeps as paths of its own (`compress` and `window`), each
checked to give the same records and offsets. Whole runs, in cycles per record:

| Row | vector | compress | window |
| --- | ---: | ---: | ---: |
| tier0-mix | **7.51** | 7.83 | 7.64 |
| tier0-40 | **8.92** | 9.55 | 9.04 |
| tier0-250 | **5.78** | 5.84 | 6.08 |
| tier0-2000 | 5.24 | 5.46 | **5.08** |
| tier0-10000 | 5.49 | 5.65 | **5.26** |
| optimized-40 | **9.54** | 10.14 | 9.70 |
| optimized-250 | **6.22** | 6.50 | 6.57 |
| optimized-2000 | 5.70 | 5.85 | **5.44** |
| optimized-10000 | 6.01 | 6.15 | **5.69** |
| chain-forward | 14.04 | 14.24 | **13.63** |
| chain-backward | 14.09 | 14.29 | **13.69** |

- **compress**: setup takes the branch records' offsets compressed out of the
  offsets column by the branch map (`vpcompressd`, 16 records at a time) in
  place of their gather, building the map for it, which the ranks phase then
  reuses. Building the map in every function costs more than the gather it
  replaces (setup 15.29 against 8.55 cycles per branch on the mix), and the
  ranks phase's saving (2.24 against 4.90) is only for the functions where a
  branch grew. Slower on every row.
- **window**: the rounds after the first take a short branch's P at its rank
  from the 144 prefix sums around its block of 16 (four `vpermi2d` and a
  `vpermd`; its rank is within 63 branches of it) in place of a gather, and a
  last pass gathers the long branches' displacements, which the window doesn't
  reach. It pays where functions take three rounds or more (3% to 5% faster on
  2,000 and 10,000 records) and loses where they settle in two (1.3% to 6%
  slower on 40 and 250 records, 1.8% on the mix): the last pass is a gather
  pass of its own. A dispatch by size could take the gain on large functions;
  not done, for less than 1% on the mix.
- **Masked gathers**: a `vpgatherdd zmm` costs the same 13 cycles or so with
  0, 1, 4, 8 or 16 lanes active (a loop of independent gathers from L1), so
  masking the lanes that don't need one saves nothing; skip the gather or
  replace it.
- **The rank table without the byte map**: building the table's bits as the
  scalar path does (in a register, branch by branch) rather than from a byte
  per record read 64 at a time, with the ranks looked up 16 at a time or one
  at a time, cost the vector path 4.71 and 5.45 cycles per branch in the ranks
  phase on tier0-2000, against the byte map's 2.89 (3% to 4% on the whole).

What was kept, each against the version before it:

| Change | tier0-mix | tier0-40 | tier0-2000 | chain-forward |
| --- | ---: | ---: | ---: | ---: |
| One push for every column, offsets without a carried sum | 8.11 → 7.80 | 9.58 → 8.89 | 5.57 → 5.33 | |
| Offsets by `vpermd` from the table, not a scan | 7.67 → 7.60 | 8.88 → 8.80 | 5.29 → 5.16 | |
| The worklist's marks 16 at a time, the worklist out of line | 7.62 → 7.68 | 8.86 → 8.89 | 5.20 → 5.20 | 54.07 → 15.01 |

The scalar mark ORed into its dirty word once per neighbour, so each waited for
the store before it; gathering the marks in a register first helped one chain
and slowed the other (a 127-iteration loop's code moves with small changes),
so the vector path marks 16 at a time. Inlined into `branch_sizing_run`, that
worklist changed how clang allocated the hot phases' registers, and functions
that never reach the worklist were sized up to 20% slower; out of line, they
are as before.

### Tuning

- **`BUSTER_BRANCH_SIZING_ROUNDS` = 4.** The benchmark's functions shaped like
  compiled code take at most 4 rounds, so from 4 up the limit changes nothing
  for them, and each round before the worklist costs a chain a pass over its
  branches. In the rounds phase, in cycles per branch:

  | Limit | 2 | 3 | 4 | 5 | 6 | 8 |
  | --- | ---: | ---: | ---: | ---: | ---: | ---: |
  | tier0-10000 | 10.25 | 7.32 | 6.97 | 6.97 | 6.96 | 6.97 |
  | optimized-10000 | 10.65 | 8.08 | 7.40 | 7.39 | 7.39 | 7.38 |
  | chain-forward | 138.2 | 141.2 | 145.1 | 148.1 | 152.8 | 159.2 |
  | chain-backward | 139.2 | 142.0 | 146.1 | 149.0 | 153.1 | 160.0 |

  With 2, 25 of tier0-10000's 27 functions reach the worklist; with 3, 2 of
  them.
- **`BUSTER_BRANCH_SIZING_CHUNK` = 1,024.** The lengths phase, in cycles per
  branch:

  | Chunk | 256 | 512 | 1,024 | 2,048 | 4,096 | 16,384 |
  | --- | ---: | ---: | ---: | ---: | ---: | ---: |
  | tier0-10000 | 29.95 | 28.01 | 29.07 | 29.06 | 28.85 | 28.66 |
  | tier0-mix | 40.59 | 40.07 | 39.85 | 39.78 | 39.94 | 39.49 |

  Within the noise from 512 up (the caller's `x86_encode`, the same code in
  every build, moved 1.4% between them), and 256 a little slower on large
  functions. 1,024 keeps the buffer at 15 KB.

## Correctness

`./build/compiler test-branch-sizing` (AGENTS.md, "Branch sizing tests"): every
function sized is one `branch_sizing_validate` accepts; both paths must give
identical records, offsets, lengths, rounds, long branches and evaluations; and
each result must be the reference's (relaxation over the records, each record
re-encoded by `x86_encode_scalar`), pass `x86_validate`, have
`x86_encode_scalar`'s offsets and length, aim every branch at its label's
record, and be long exactly where a rel8 can't reach. The checks cover every
form number, `branch_sizing_validate`'s errors at every place, probes at every
displacement around both rel8 limits over branches that grow or don't, random
functions of every shape (with filler of every form, and with other round
limits), chains of 1 to 1,000 links both ways, every column ending at a
`PROT_NONE` page, and no scratch memory to spare. With `--cross-check`, GNU as
relaxes 404 of the functions printed as source with labels, and its bytes must
be `x86_encode_scalar`'s of the sized records.

## Next steps

- **Real functions**, once the code generator emits them (#136): the
  benchmark's rows for its output and the corpora.
- **A lengths-only entry point** of the encoder, Phase II alone, if it
  measures cheaper than the first round's `x86_encode` (most of sizing's cost),
  with #182's session.
- **The first round's bytes.** Where no branch grows (most small functions),
  they are the final bytes but for the displacements, so the caller's second
  `x86_encode` could become a patch of the rel8 bytes; that changes contract
  1's output.
- **The window rounds for large functions**, behind a dispatch by size (see
  "What was tried").
- **Loop-head alignment** with multi-byte NOPs, which changes offsets and so
  belongs in the same rounds, and the forced-disp32 record bit, both outside
  #144.
