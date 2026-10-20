# Data-parallel parsing of C on Zen 5 (spike #186)

Measured on 2026-10-20 on the Ryzen 7 9700X (`benchpress`), with Clang 23.1.1 and
`build.sh`'s `-O3 -march=native` (znver5). Every build and run there took the
shared `~/buster-fast-bench/exclusive.sh` lock by itself, and every benchmark was
pinned to CPU 2 (performance governor; its SMT sibling, CPU 10, offline). The
corpus runs are of commit `64b5c31`, the generated cases' of `7edd830`, whose timed
code is the same. The code is `src/buster/compiler/parse_spike.c` and
`parse_spike_benchmark.c` (the `spike-parse` command) on branch
`claude/jolly-turing-7b4673`.

A follow-up the same day, commit `ea729b6` on branch
`claude/frosty-benz-4b10d3`, made K4 and K2 faster: the vector path now runs at
2.1 times the scalar parser's speed on both corpora. "K4 and K2, tuned"
describes it; the tables show its numbers next to the spike's, from a series
that measured `c4a7475` (the spike's timed code) and `ea729b6` side by side.

A second follow-up, on branch `claude/spike-186-vector-k5` (commits `dfd07b4` to
`dc81644`), vectorized K5 and cut K2's round: the vector path now runs at 2.51
times the scalar parser's speed on sqlite3 and 2.32 on libc. "K5 in AVX-512,
and K2's round" describes it and what it tried and dropped, K4's included; the
tables add its numbers, from a series that measured `315b6ef`, `dfd07b4` and
`dc81644` by turns.

A third follow-up, on branch `claude/spike-186-k2-spills` (commits `6f36b69` to
`316d7a5`), took K2's spills out of its block loop, and with them its two speed
states: K2 now takes 3.85 cycles a token on sqlite3 and 3.47 on libc in every
build, and the vector path runs at 2.70 times the scalar parser's speed on
sqlite3 and 2.50 on libc. "K2 in one state" describes it and what it tried and
dropped; the tables add its numbers, from a series that measured `eb43cc7`,
`dc81644` and `316d7a5` by turns.

The question (#186): #15 called the parser inherently sequential. Does
preprocessed C parse faster on one Zen 5 core with branch-free AVX-512 kernels,
which build the tree with nearest-smaller-value searches (Bar-On and Vishkin,
1985), than with a scalar shunting-yard parser?

## Results

Yes, by 1.5 to 1.6 times on real code, by 2.1 times with K4 and K2 tuned, by
2.5 and 2.3 times with K5 in AVX-512 too, and by 2.7 and 2.5 times with K2 in
one state, with the same tree on every token:

| Whole path | sqlite3 ms | cycles/token | instructions/token | IPC | mispredicts/token | libc ms | cycles/token | instructions/token | IPC | mispredicts/token |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Scalar parser | 5.61 | 44.0 | 190.3 | 4.33 | 0.496 | 20.93 | 29.7 | 182.7 | 6.16 | 0.106 |
| Kernels' scalar twins | 13.68 | 107.3 | 374.7 | 3.49 | 1.514 | 47.49 | 67.7 | 361.7 | 5.34 | 0.354 |
| Vector path | 3.48 | 27.3 | 58.0 | 2.13 | 0.170 | 13.70 | 19.6 | 45.5 | 2.32 | 0.032 |
| Vector path, K4 and K2 tuned | 2.67 | 20.9 | 50.8 | 2.43 | 0.157 | 9.71 | 13.9 | 40.6 | 2.92 | 0.029 |
| Vector path, K5 in AVX-512 | 2.27 | 17.7 | 42.9 | 2.43 | 0.086 | 8.94 | 12.8 | 36.3 | 2.83 | 0.016 |
| Vector path, K2 in one state | 2.13 | 16.7 | 40.8 | 2.45 | 0.087 | 8.18 | 11.7 | 34.2 | 2.92 | 0.017 |

sqlite3 is the 3.53.4 amalgamation, 707,153 tokens. libc is 733 files of the
libc corpus, 3,869,223 tokens; the corpus is under "Inputs". Each figure is the
median of 21 interleaved passes; the whole paths' fastest and slowest passes are
within 1.3% of it.

- **The vector path is 1.61 times as fast as the scalar parser on sqlite3 and
  1.51 times on libc**, 2.1 times on both with K4 and K2 tuned, 2.51 and 2.32
  times with K5 in AVX-512, and 2.70 and 2.50 with K2 in one state. It retires
  3.3 to 4 times fewer instructions, at half the IPC: its kernels are
  backend-bound (45% to 60% of dispatch slots). The scalar parser loses its
  slots to the front end and to bad speculation instead (32% and 15% on
  sqlite3, with 0.50 mispredictions a token).
- **Every vectorized kernel beats its scalar twin, by 2.5 to 15 times** (K5 in
  AVX-512 by 9 and 61), but the twins together cost 2.4 times what the scalar
  parser does. Splitting the
  parse into kernels only pays once they are vectorized.
- **The scalar parser is a fair baseline:** inlining, one packed role table,
  sentinels and stacks of entries took it from 53.1 to 44.0 cycles a token on
  sqlite3, and from 36.8 to 29.7 on libc. Its IPC on libc is 6.2.
- **sqlite3 costs more per token than libc on every path**: its function bodies
  branch less predictably than libc's header declarations. It isn't memory: L1
  fills from L3 stay at or below 0.023 a token and from DRAM below 0.003, except
  the counting postorder's 0.03 from DRAM on sqlite3.
- **Two kernels took most of the vector path's time.** K2 (roles) took 35%, at
  3.5 cycles a token per round of its fixpoint. K4 (parents) took 27% to 35%,
  latency-bound inside each 64-token block. Tuned, K2 runs at 5.0 and 4.7 cycles
  a token (from 9.1 and 7.2), in 1.08 and 1.06 rounds a block (from 2.69 and
  2.10), and K4 at 4.5 and 4.0 (from 7.0 and 6.7). In one state, without
  spills, K2 runs at 3.85 and 3.47.
- **K5, the statement stack, came next.** Scalar over the statement-level
  tokens, it took 4.8 cycles a token on sqlite3, nearly K2's size. In AVX-512,
  its owners from a compress and an expand of the events' positions, it takes
  1.8 on sqlite3 and 0.2 on libc.
- **The fallback is small**: 1.44% of sqlite3's tokens and 0.06% of libc's go to
  the scalar parser, a top-level declaration at a time.
- **The generated worst cases bound the losses.** Chains of casts never
  converge in K2's 8 rounds and 200,000 nested parentheses exceed K4's 16-bit
  keys. The kernel falls back to its twin, and the vector path runs at 0.54 and
  0.94 times the scalar parser's speed (0.59 and 0.99 with K4 and K2 tuned,
  0.56 and 1.00 with K5 in AVX-512, 0.63 and 1.05 with K2 in one state).
- **The postorder derived from the parents costs 7.1 cycles a token on sqlite3
  and 6.0 on libc** without a branch on the tree. The obvious walk costs 14.7 on
  sqlite3, because it mispredicts.

"Recommendation for #127" below gives the design to adopt.

## The tree

The parser's output is a parent for every token, punctuation included; a
node's children, in source order, are its operands. C's grammar is written as
operator precedence, so that nearest-smaller-value searches can build the tree:

- Each token has a **role** (84 roles), which gives it a **level**, a **left and
  a right threshold** and a **rule**. Expressions use C's precedence.
  Declarations parse as the expressions they mirror, with the specifiers as
  right-associative prefixes below the comma. Statements sit below everything a
  declaration or an expression contains.
- **Brackets**: `( ) [ ] { }`, `? :` and `case`/`default ... :`. A group's
  closer is its node; its opener is a leaf child of the closer, and the closer's
  role gives the group's level (a call's `)` is a postfix operator).
- **Keys** are (node depth, level), depth first. L(i) is the nearest token before
  i whose key is below i's left threshold; R(i) the nearest after it below i's
  right threshold. Strict or inclusive searches give each level its
  associativity.
- The **parent** is the one of L and R with the greater key, except that an
  opener, terminator or statement head found as L loses to any R. Some roles take
  L, R or their closer instead.
- **Statements**: a statement stack decides which head (if, while, for, switch,
  else, do, labels, case) owns which statement, outside the levels.

Every subtree is then a run of tokens, as in a Cartesian tree. The checks
confirm it on every file: the postorder it implies is a depth-first walk's.

## The scalar parser

`spike_parse_scalar` is an explicit-state parser: grammar modes on a frame
stack (declarations, declarators, expressions, attributes, asm, `_Generic`,
initializers, and the rest), the statement stack, and two precedence stacks.
Those are the L candidates, and the tokens waiting for their R. Each resolves as
tokens arrive, as a shunting-yard parser's operator stack does. Typedef names
are bound by scope in a meaning table with an undo log. It parses every file
here, and its trees keep the invariants `spike_check_tree` checks: every parent
at no greater depth, every opener's parent its closer, no cycle. It is the
oracle and the fallback.

Its tuning, measured on the 9700X before and after on sqlite3:

- force-inlining its 39 helpers;
- one packed table of each role's level, thresholds and rule;
- sentinels at the bottom of both stacks;
- each stack as one array of entries. An R entry carries its L and L's key, so
  resolving reloads neither's role nor depth.

It went from 53.1 to 44.0 cycles a token, and from 215 to 190 instructions.
What remains is the grammar's mode dispatch (about 140 instructions a token)
and the stacks (about 48). A table-driven grammar might shorten it further.

## The kernels

The vector path (`spike_parse_vector`) runs these passes over a file's tokens,
each the AVX-512 form of a scalar twin whose output is the same:

1. **K1, brackets.** Kinds to +1, -1 or 0 with `vpermi2b`, a 6-step prefix sum
   in byte lanes, and each closer's opener by a nearest-smaller search: a doubling
   table of minima searched with `vpermb` within the block, and a 16-depth window
   of the open-opener stack (`vpermd`) across blocks, with a scalar path for
   deeper drops. Since the second follow-up, it also hands K2 a view of each
   block: the enclosing openers as lanes, the depths and the openers still open.
2. **K3, type names.** A scalar pass, in source order, over the typedef
   declarations only, found by a vector search for the keyword. Then every
   identifier is classified against the set of names they declare, with mask
   loops (a gather variant was also measured). A name declared as a typedef in a
   block is flagged for the fallback.
3. **K2, roles.** Each token's role from its kind, its neighbors' kinds, what the
   token before it is (the predecessor's role), and its group's role. The rounds
   run to a fixpoint, at most 8, else the twin takes the block. The fixpoint is
   unique because every dependency points backward. The first guess is each
   kind's commonest role. A comma's, colon's, ellipsis's and closer's group roles
   come from the same round's openers, and a group opened before the block takes
   its role from a by-depth stack window (`vpermb`). The typed state is a carry
   chain on 64-bit masks. A token the rules can't be sure of is flagged. Tuned,
   the rounds start from refined guesses and stop as soon as no lane reads a
   change ("K4 and K2, tuned"). Since the second follow-up, K2 reads K1's view of
   each block and writes each block's statement contexts for K5 ("K5 in
   AVX-512, and K2's round"). Since the third, the block loop runs one round,
   what the kinds decide comes from a pass before the rounds, and the group
   windows stay in registers ("K2 in one state").
4. **K1's second part.** `?`, `case` and `default` matched to their colons per
   depth in a sparse scalar loop over the few such tokens. A colon with no opener
   is a label. Then the node depths by a vector prefix sum.
5. **K4, parents.** L by a forward pass and R by a backward one, both in 64-token
   blocks of byte keys relative to the block's lowest node depth (a block whose
   depths span more than 6 runs as two 16-bit blocks). A doubling search within
   the block, then a binary search, in registers, of the candidate stack's top
   32 entries. Branch-free pops, compressed pushes, and each role's rule with
   `vpermw` tables. Tuned, every block's in-block work runs first, in a loop of
   its own, and the passes keep the stack's top in registers ("K4 and K2,
   tuned").
6. **K5, statements.** A mask of the statement-level tokens, then the statement
   stack over just those. Since the second follow-up, the owners come from a
   compress and an expand of the events' positions, the blocks' owners from a
   stack without branches, and else, do and while from a sparse loop; the
   statement stack is the twin ("K5 in AVX-512, and K2's round").
7. **The fallback.** The scalar parser on every top-level declaration with a
   flagged token.

## Inputs

- **sqlite3**: the 3.53.4 amalgamation (`sqlite3.c`, sha256 `b1dd5d74…`),
  preprocessed on the 9700X by `env -u CPATH clang -E -P`: 2,800,653 bytes and
  707,153 parser tokens, after pragma lines are dropped.
- **libc**: the 2,315 `.c` files of `~/azp-bench/c-tokenizer/corpus/libc` (Zig
  0.14.1's `lib/libc`). 1,345 preprocess standalone the same way; the other 970
  need headers or macros their builds supply. Clang's `-fsyntax-only` accepts 733
  of the 1,345 as C: 537 musl, 169 mingw, 21 wasi, 4 include and 2 glibc. The
  rest use macros their builds define. Those 733 files, 19,283,048 bytes and
  3,869,223 tokens, are the corpus.
- **Generated worst cases** (`--generated N`): one expression of N binary
  operators of every precedence, parentheses nested N deep, N names without
  brackets, N nested calls, N casts of casts, nested ternaries, if-else chains
  and switches, and typedef'd declarations, at N = 200,000.
- **Fixtures**: two sources whose parents were checked by hand: every statement,
  the dangling else, casts, compound literals, designators, bit-fields,
  attributes, nested ternaries.

Each file is lexed by `c_tokenize`, refined (keywords resolved, literals merged,
pragma lines dropped) and interned outside the timing. The language is gnu17,
clang's default: C23's keywords are identifiers there.

## Correctness

On every input, locally and on the 9700X:

- the scalar parser parses every file and every tree keeps the invariants;
- the twins' and the vector path's parents and roles equal the scalar parser's
  on every token;
- every vector kernel's output equals its twin's, each lane-width variant too;
- the postorders derived from the parents equal a depth-first walk's (a mutant
  that swaps two tokens is caught);
- the tuned K4's output equals its twin's with windows of 1, 3 and 8 lanes as
  well as 32, which takes its paths through memory on every input, and its
  window search equals a scalar count on 4,096 random windows at every
  threshold;
- the tuned K2, started from random roles twice per file, ends at its twin's
  roles, so a round called final too early shows;
- the vector K5's parents equal its twin's on every file and on 2,000 random
  statement soups (`--statements N`), and K2's statement contexts equal ones
  worked out from the twin's roles;
- the vector K1's view of each block, which K2 reads, equals one built from the
  twin's depths and enclosing openers.

The fallback: on sqlite3, 18 flagged tokens send 7 top-level declarations,
10,210 tokens (1.44%), to the scalar parser. They come from `_Generic` and
block-scope typedefs (`void_function`, `LOGFUNC_t`). On libc, 51 flagged tokens
send 7 declarations and 2,282 tokens (0.06%): the bodies of musl's `memccpy`,
`memchr`, `memmem`, `memset`, `strlcpy` and `strlen` and wasi's copy of `memset`,
which declare block-scope typedefs such as
`typedef uint32_t __attribute__((__may_alias__)) u32;`. No generated case falls
back.

## Measurements

The benchmark runs 21 interleaved passes after a warm-up. Each pass runs the
three whole paths over every file, one file after the other. Then it runs each
file through every stage in turn on shared arrays, after reading the file's
tokens once, as a compiler's parser would find them after lexing. Counters are
user-space only, read with `rdpmc`. A median sample's cost, 233 cycles, is
subtracted from every stage interval. Besides cycles and instructions, one of
three sets of Zen 5 events is counted (`--counters`). `pipeline` splits the 8
dispatch slots of each cycle as perf's PipelineL1 metrics do. `branches` counts
retired branches, mispredicted ones and taken ones. `memory` counts L1 demand
fills from L2, L3 and DRAM. The NMI watchdog holds one of the 6 counters, so 5
events are all that count without multiplexing, and no run reported a counter
that shared its time. "Other" slots are those the three pipeline events leave,
which is bad speculation here.

### Kernel by kernel

sqlite3:

| Kernel | Twin cycles/token | instructions/token | IPC | Vector cycles/token | instructions/token | IPC | Speedup | Vector slots: retiring/frontend/backend/other % |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| K1 brackets | 7.95 | 19.1 | 2.41 | 2.31 | 3.1 | 1.34 | 3.4x | 17.8/7.9/57.6/16.7 |
| K1 brackets, with K2's views | | | | 2.30 | 3.3 | 1.43 | 3.5x | 19.2/8.1/57.4/15.1 |
| K3 typedef pass | 0.10 | 0.4 | 3.96 | 0.10 | 0.4 | 3.95 | 1.0x | 41.1/29.3/16.1/13.4 |
| K3 classification | 7.84 | 10.9 | 1.40 | 1.41 | 4.5 | 3.19 | 5.5x | 40.5/16.4/33.7/9.4 |
| K2 roles | 32.82 | 128.8 | 3.92 | 9.64 | 19.6 | 2.03 | 3.4x | 27.8/10.7/52.6/8.9 |
| K2 roles, tuned | | | | 5.02 | 13.2 | 2.63 | 6.5x | 37.0/8.6/48.2/6.1 |
| K2 roles, with K1's views | | | | 4.85 | 12.3 | 2.53 | 6.8x | 36.4/5.6/51.1/6.2 |
| K2 roles, in one state | | | | 3.85 | 10.2 | 2.64 | 8.5x | 35.6/6.7/51.5/6.2 |
| K1 second part | 13.60 | 38.1 | 2.80 | 0.88 | 1.9 | 2.10 | 15.4x | 26.2/11.4/48.2/14.1 |
| K1 second part, carry from a register | | | | 0.78 | 1.9 | 2.39 | 17.4x | 29.8/12.7/42.2/15.3 |
| K4 parents | 27.61 | 111.8 | 4.05 | 7.36 | 12.1 | 1.65 | 3.8x | 22.6/1.7/74.8/0.9 |
| K4 parents, tuned | | | | 4.46 | 11.3 | 2.53 | 6.2x | 33.9/4.9/56.2/5.0 |
| K5 statements | 16.57 | 65.0 | 3.92 | 4.91 | 12.6 | 2.58 | 3.4x | 28.2/48.2/3.0/20.6 |
| K5 statements, in AVX-512 | | | | 1.79 | 5.4 | 3.01 | 9.3x | 38.6/19.8/25.7/15.7 |
| Fallback | 1.11 | 3.8 | 3.40 | 1.09 | 3.8 | 3.48 | 1.0x | 39.5/28.3/18.9/13.3 |
| Sum | 107.6 | | | 27.7 | | | 3.9x | |
| Sum, K4 and K2 tuned | | | | 20.0 | | | 5.4x | |
| Sum, K5 in AVX-512 | | | | 16.8 | | | 6.4x | |
| Sum, K2 in one state | | | | 16.0 | | | 6.7x | |

libc:

| Kernel | Twin cycles/token | instructions/token | IPC | Vector cycles/token | instructions/token | IPC | Speedup | Vector slots: retiring/frontend/backend/other % |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| K1 brackets | 4.73 | 19.9 | 4.20 | 1.86 | 3.0 | 1.64 | 2.5x | 21.7/6.8/65.1/6.4 |
| K1 brackets, with K2's views | | | | 1.84 | 3.3 | 1.78 | 2.6x | 23.9/6.3/64.1/5.7 |
| K3 typedef pass | 0.53 | 2.2 | 4.19 | 0.47 | 2.2 | 4.69 | 1.1x | 49.3/30.3/11.0/9.4 |
| K3 classification | 3.20 | 11.1 | 3.46 | 1.19 | 4.3 | 3.62 | 2.7x | 45.5/12.4/36.7/5.4 |
| K2 roles | 23.29 | 125.7 | 5.40 | 7.26 | 16.7 | 2.30 | 3.2x | 31.5/8.0/56.6/3.9 |
| K2 roles, tuned | | | | 4.70 | 12.9 | 2.75 | 5.0x | 38.9/4.7/53.1/2.9 |
| K2 roles, with K1's views | | | | 4.34 | 12.1 | 2.78 | 5.4x | 40.0/4.2/53.0/2.7 |
| K2 roles, in one state | | | | 3.47 | 10.0 | 2.89 | 6.7x | 38.8/5.4/53.7/2.1 |
| K1 second part | 7.68 | 38.2 | 4.97 | 0.69 | 1.7 | 2.52 | 11.1x | 31.7/9.2/58.1/1.0 |
| K1 second part, carry from a register | | | | 0.54 | 1.75 | 3.27 | 14.3x | 41.1/12.2/44.6/2.0 |
| K4 parents | 19.12 | 108.7 | 5.69 | 6.84 | 12.2 | 1.78 | 2.8x | 24.4/1.4/73.8/0.3 |
| K4 parents, tuned | | | | 3.98 | 11.1 | 2.79 | 4.8x | 37.4/1.8/60.3/0.6 |
| K5 statements | 12.15 | 58.5 | 4.82 | 1.57 | 4.5 | 2.87 | 7.7x | 31.9/39.1/15.4/13.6 |
| K5 statements, in AVX-512 | | | | 0.20 | 0.75 | 3.76 | 61x | 45.1/28.4/15.3/11.4 |
| Fallback | 0.36 | 1.3 | 3.54 | 0.35 | 1.3 | 3.61 | 1.0x | 43.8/15.0/39.2/1.9 |
| Sum | 71.1 | | | 20.2 | | | 3.5x | |
| Sum, K4 and K2 tuned | | | | 14.6 | | | 4.9x | |
| Sum, K5 in AVX-512 | | | | 12.9 | | | 5.5x | |
| Sum, K2 in one state | | | | 12.0 | | | 5.9x | |

The stages' sums are within 5% of the whole paths' 107.3 and 27.3 (sqlite3)
and 67.7 and 19.6 (libc). The typedef pass and the fallback are scalar in both
paths. The tuned rows are medians of the follow-up's series ("K4 and K2,
tuned"); their speedups are over the twins above, and their sums are within 5% of
the tuned whole paths' 20.9 and 13.9. The rows "with K2's views", "with K1's
views" and "in AVX-512" are `dc81644`'s, from the second follow-up's series,
and its sums are within 6% of its whole paths' 17.7 and 12.8. The rows "in one
state" and "carry from a register" are `316d7a5`'s, from the third follow-up's
series, and its sums are within 5% of its whole paths' 16.7 and 11.7. K5 in
AVX-512 wins 61 times on libc because most of libc's blocks hold no
statement-level token, and such a block costs it a table lookup and a mask.

### 32 against 64 lanes, and K3's two classifications

| Variant | sqlite3 cycles/token | instructions/token | libc cycles/token | instructions/token |
| --- | ---: | ---: | ---: | ---: |
| K1, 32 lanes of 16 bits | 3.39 | 4.4 | 2.60 | 4.3 |
| K1, 64 lanes of 8 bits | 2.31 | 3.1 | 1.86 | 3.0 |
| K4, 32 lanes of 16 bits | 8.87 | 13.0 | 8.82 | 13.1 |
| K4, 64 lanes of 8 bits (16-bit blocks where depths span more than 6) | 7.36 | 12.1 | 6.84 | 12.2 |
| K4, 64 lanes of 8 bits, the stack's top in registers, in three loops (parts where depths span more than 6) | 4.46 | 11.3 | 3.98 | 11.1 |
| K3, gather (`vpgatherdd`) | 1.26 | 0.9 | 1.38 | 1.4 |
| K3, mask loops | 1.41 | 4.5 | 1.19 | 4.3 |

64-lane blocks win for both searches: by 1.47 and 1.40 times for K1, 1.21 and
1.29 for K4. In byte lanes a block holds twice the tokens for the same permutes,
and only 188 of sqlite3's 11,050 K4 blocks and 39 of libc's 60,795 span too many
depths. K2 works on byte roles and kinds, so it has only 64-lane blocks. K3's
two classifications are within 0.2 cycles: the gather wins on sqlite3, the mask
loops on libc. The gather's single instruction a token is microcoded (a
`vpgatherdd zmm` is 64 ops on Zen 5, hence its IPC of 0.75 to 0.98); the vector
path uses the mask loops.

### The postorder

| Postorder from the parents | sqlite3 cycles/token | instructions/token | mispredicts/token | libc cycles/token | instructions/token | mispredicts/token |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Walk: emit each token with no child after it, then climb (`spike_postorder`) | 14.71 | 19.1 | 0.437 | 4.91 | 18.8 | 0.078 |
| Positions without a branch on the tree (`spike_postorder_sorted`) | 7.05 | 24.8 | 0.000 | 6.01 | 24.9 | 0.002 |

Since every subtree is a run, a token's postorder position is
C(v) + end(v) - v. end(v) is the last token of v's subtree, from v's last child
after it, in a backward pass. C(v) counts the tokens whose subtrees end before v:
an exclusive prefix sum of how many end at each token. The walk is faster on
libc's predictable trees and twice as slow on sqlite3's. Clang turned the
position pass's select into a branch around a load until it was marked
`__builtin_unpredictable`. On sqlite3 the counting pass's scatter is the only
place with DRAM fills (0.03 a token).

### Branches and memory

| Path or stage | sqlite3 mispredicts/token | L2 fills/token | L3 fills/token | DRAM fills/token | libc mispredicts/token | L2 | L3 | DRAM |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Scalar parser | 0.496 | 0.015 | 0.007 | 0.000 | 0.106 | 0.041 | 0.001 | 0.000 |
| Twins | 1.514 | 0.080 | 0.023 | 0.003 | 0.354 | 0.099 | 0.001 | 0.000 |
| Vector path | 0.170 | 0.208 | 0.018 | 0.000 | 0.032 | 0.227 | 0.002 | 0.000 |
| Vector K2 | 0.038 | 0.039 | 0.002 | 0.000 | 0.009 | 0.053 | 0.001 | 0.000 |
| Vector K4 | 0.001 | 0.077 | 0.002 | 0.000 | 0.001 | 0.078 | 0.000 | 0.000 |
| Vector K5 | 0.083 | 0.009 | 0.002 | 0.000 | 0.020 | 0.015 | 0.000 | 0.000 |
| Vector path, K4 and K2 tuned | 0.157 | | | | 0.029 | | | |
| Vector K2, tuned | 0.018 | | | | 0.007 | | | |
| Vector K4, tuned | 0.006 | | | | 0.001 | | | |
| Vector path, K5 in AVX-512 | 0.086 | | | | 0.016 | | | |
| Vector K2, with K1's views | 0.008 | | | | 0.004 | | | |
| Vector K5, in AVX-512 | 0.023 | | | | 0.0015 | | | |
| Vector path, K2 in one state | 0.087 | | | | 0.017 | | | |
| Vector K2, in one state | 0.009 | | | | 0.005 | | | |

At 15 to 20 cycles each, the scalar parser's mispredictions cost sqlite3 about 8
to 10 of its 44 cycles a token. Half of the vector path's come from K5, most of
the rest from K2 and K3. Tuned, K2 mispredicts half as often, since its scalar
loops over openers, tags and casts now visit only the tokens that need them.
The tuned K4 mispredicts more, 0.006 a token on sqlite3 against 0.001, in its
passes' per-block branches: about 0.1 cycles a token. The follow-up didn't count
fills. In AVX-512, K5 mispredicts 0.023 times a token on sqlite3, from 0.084,
and the vector path 0.086, from 0.157; K2 halved its own again, its driver's
loop over the open openers gone. In one state, K2 mispredicts 0.009 times a
token on sqlite3, from 0.008, at the blocks that leave its block loop.

The prefetchers keep up with every pass over whole files, so fills from L3 and
DRAM are negligible even for sqlite3, whose arrays (about 20 MB) are in L3.
Splitting files into chunks that fit in L2 isn't needed for one core.

### Generated worst cases

200,000 operations each:

| Case | Tokens | Scalar cycles/token | Vector cycles/token | Vector speedup | K2 | K4 | K5 | Why |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| Operator chain | 400,031 | 22.6 | 16.9 | 1.33x | 5.29 | 7.16 | 0.07 | |
| Nesting 200,000 deep | 545,369 | 24.4 | 26.0 | 0.94x | 6.59 | 13.20 | 0.22 | depths past K4's 16-bit keys (2,047): K4's twin |
| No brackets | 1,200,026 | 29.6 | 16.8 | 1.77x | 5.25 | 6.81 | 0.07 | the scalar parser mispredicts (0.14 a token) |
| Calls of calls | 440,031 | 32.1 | 15.3 | 2.10x | 5.21 | 7.17 | 0.06 | the scalar parser mispredicts (0.18 a token) |
| Casts of casts | 616,027 | 27.3 | 50.5 | 0.54x | 39.30 | 6.93 | 0.10 | K2 doesn't converge in 8 rounds: the twin does every block |
| Nested ternaries | 1,200,031 | 22.7 | 17.4 | 1.31x | 3.50 | 7.31 | 0.06 | |
| Statements | 630,027 | 27.8 | 25.2 | 1.10x | 7.89 | 7.02 | 6.31 | K5's statement stack is scalar |
| Declarations | 399,987 | 25.8 | 21.1 | 1.22x | 8.79 | 7.15 | 1.25 | |

With K4 and K2 tuned, from the follow-up's series: one run of each build per
case and counter set. The speedups are over the scalar parser's figures above;
its code didn't change.

| Case | Vector cycles/token, spike's code | Tuned | Speedup | K2 | K4 | K2 rounds a block, spike's → tuned | Why |
| --- | ---: | ---: | ---: | ---: | ---: | --- | --- |
| Operator chain | 16.81 | 12.72 | 1.78x | 4.28 | 4.04 | 2 → 1 | |
| Nesting 200,000 deep | 26.46 | 24.74 | 0.99x | 5.63 | 13.21 | 2 → 1 | K4's twin, as before |
| No brackets | 17.88 | 14.86 | 1.99x | 4.43 | 4.14 | 2 → 1 | |
| Calls of calls | 15.12 | 11.10 | 2.89x | 4.41 | 4.02 | 1 → 1 | |
| Casts of casts | 50.25 | 46.31 | 0.59x | 37.49 | 4.08 | the twin | K2's twin, as before |
| Nested ternaries | 17.75 | 15.98 | 1.42x | 4.51 | 4.35 | 1 → 1 | a round costs more |
| Statements | 25.31 | 19.71 | 1.41x | 4.46 | 4.06 | 3 → 1 | K5 (6.34) |
| Declarations | 20.27 | 15.02 | 1.72x | 6.11 | 4.03 | 3 → 2 | |

With K5 in AVX-512, from the second follow-up's series: one run of `315b6ef` and
of `dc81644` per case and counter set, each speedup over its own build's scalar
parser.

| Case | Vector cycles/token, tuned | Now | Speedup, tuned | Now | K2 | K4 | K5, tuned | Now | Why |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| Operator chain | 12.63 | 12.78 | 1.78x | 1.76x | 4.12 | 4.00 | 0.06 | 0.08 | |
| Nesting 200,000 deep | 24.95 | 24.59 | 0.97x | 1.00x | 4.20 | 13.66 | 0.22 | 0.16 | K4's twin, as before; K1 4.86, from 4.17 |
| No brackets | 14.43 | 14.31 | 2.04x | 2.12x | 4.07 | 4.00 | 0.07 | 0.09 | |
| Calls of calls | 11.12 | 11.33 | 2.89x | 2.86x | 4.20 | 4.04 | 0.06 | 0.08 | |
| Casts of casts | 46.66 | 47.93 | 0.58x | 0.56x | 37.86 | 4.05 | 0.11 | 0.65 | K2's twin, as before |
| Nested ternaries | 16.25 | 15.58 | 1.43x | 1.45x | 4.29 | 4.02 | 0.07 | 0.09 | |
| Statements | 19.48 | 16.12 | 1.42x | 1.71x | 4.32 | 4.13 | 6.35 | 2.53 | |
| Declarations | 14.81 | 14.38 | 1.71x | 1.79x | 5.58 | 4.01 | 1.25 | 0.78 | |

With K2 in one state, from the third follow-up's series: one run of `dc81644`
and of `316d7a5` per case and counter set, the faster of the two. The speedups
are over `dc81644`'s scalar parser, whose code is the same in both: `316d7a5`'s
own took 39.0 cycles a token on the calls of calls, against 32.0, the same code
placed 96 bytes earlier than in `428af9a`'s build, where it took 32.1.

| Case | Vector cycles/token, `dc81644` | `316d7a5` | Speedup, `dc81644` | `316d7a5` | K2, `dc81644` | `316d7a5` | Why |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| Operator chain | 12.76 | 11.81 | 1.77x | 1.91x | 3.96 | 3.26 | |
| Nesting 200,000 deep | 24.80 | 23.41 | 0.99x | 1.05x | 4.15 | 3.34 | K4's twin, as before |
| No brackets | 14.40 | 13.07 | 2.08x | 2.29x | 3.96 | 3.32 | |
| Calls of calls | 11.63 | 10.27 | 2.75x | 3.11x | 4.20 | 3.26 | |
| Casts of casts | 48.52 | 42.88 | 0.55x | 0.63x | 38.36 | 33.21 | K2's twin, as before, after seven rounds out of line |
| Nested ternaries | 15.71 | 14.78 | 1.44x | 1.54x | 4.06 | 3.33 | |
| Statements | 16.55 | 15.12 | 1.67x | 1.83x | 4.35 | 3.35 | |
| Declarations | 14.36 | 14.27 | 1.79x | 1.81x | 5.55 | 5.59 | every block takes two rounds |

## K4 and K2, tuned

The follow-up (commit `ea729b6`) changed K4 and K2 only: every other stage, the
twins and the scalar parser are the spike's code. Its numbers come from one
series on the 9700X, measured as the spike's were: builds of `c4a7475` and
`ea729b6` ran by turns, three times each with `--counters pipeline` and with
`--counters branches`, every run under the lock by itself, and each figure is
the median of the three runs' medians.

| Cycles a token | sqlite3, spike | tuned | libc, spike | tuned |
| --- | ---: | ---: | ---: | ---: |
| Scalar parser | 44.00 | 43.62 | 29.74 | 29.02 |
| Vector path | 26.79 | 20.91 | 19.36 | 13.89 |
| Vector path, ms | 3.42 | 2.67 | 13.52 | 9.71 |
| Vector path's speedup over the scalar parser | 1.64x | 2.09x | 1.54x | 2.09x |
| K2 roles | 9.10 | 5.02 | 7.20 | 4.70 |
| K2's rounds a block | 2.69 | 1.08 | 2.10 | 1.06 |
| K4 parents | 7.03 | 4.46 | 6.66 | 3.98 |
| K4's loops: in-block work, left pass, right pass | | 1.38, 1.37, 1.79 | | 1.38, 1.16, 1.51 |
| The spike's K4, in the tuned build | | 6.99 | | 6.71 |

The tuned vector path's runs ranged over 20.73 to 21.12 cycles a token on
sqlite3 and 13.86 to 14.06 on libc, the spike's over 26.75 to 27.67 and 19.33
to 19.58. The spike's K4 runs as fast in the tuned build as in its own, so the
gain is the new design's, not where the code landed. K2 is noisier: single runs
of the same code gave 6.01 and 6.42 cycles a token on sqlite3, so the steps
below, single runs, are good to about 7%.

### K4: the in-block work first, then the stack with its top in registers

The spike's K4 ran each block's whole chain in one iteration of each pass, and
its blocks barely overlapped. The spike put that down to a block's ops filling
the reorder buffer, but the counters say otherwise. In a loop of the blocks'
in-block work alone, `de_dispatch_stall_cycle_dynamic_tokens_part1.fp_sch_rsrc_stall`
counted 49% of the cycles and `de_dispatch_stall_cycle_dynamic_tokens_part2.retq`
(the retire queue full) 0.2%. The first counts dispatch held up because the
96-entry queue in front of the floating-point schedulers is full (AMD's Zen 5
optimization guide, 58455). One block's chain of vector ops fills it while it
waits, so the next block's ops don't reach the schedulers, however much of the
reorder buffer is free. Independent chains overlapped only when they were
interleaved in program order, step by step.

So K4 runs as three loops (`spike_top_parents8_avx512`):

1. `spike_top_prepare8_avx512` does both passes' in-block work for every block:
   the depths relative to the block's lowest, the keys, both doubling tables and
   in-block searches, each pass's candidates for later blocks (the block's
   suffix and prefix minima) and its least key. It stores them in a record of
   six cache lines per block, and works on two blocks at a time, each step for
   both before the next.
2. The left pass and the right pass do only the stack's part, on those records.
   The top of the candidate stack lives in three registers: its 32 nearest
   candidates' 16-bit keys and their positions' low and high halves, nearest
   first. A block's lanes that found no candidate in it search that window (a
   branch-free binary search on its keys, as bytes relative to the block's
   base). The block's least key pops the candidates it hides with a
   `vpcompressw`, and its own go on with a `vpexpandw`, so what a block hands
   the next is a compare, a compress and an expand on registers. The right pass
   then applies each role's rule, with one `vpermw` table for the weak and tie
   bits.

The whole stack is also in memory, nearest first, written by masked stores at
each push. A lane whose candidate lies under the window reads it there by binary
search, and a window that pops have emptied below what the stack holds is
reloaded from it. Neither happens on sqlite3 or libc, whose stacks hold at most
32 candidates before a block; the check's windows of 1, 3 and 8 lanes take
those paths on every input. Blocks whose depths span more than byte keys hold
(227 of the 148,200 the check covers) run as aligned parts of 32 tokens or
fewer that fit, each with its own in-block work.

The steps, each a run of K4 on the 9700X in cycles a token:

| Step | sqlite3 | libc |
| --- | ---: | ---: |
| The spike's K4 | 7.03 | 6.66 |
| The stack's top in registers, the in-block work still in the passes | 6.23 | 5.47 |
| The in-block work in a loop of its own, a block at a time (it alone: 2.95 and 2.95) | 4.82 to 5.22 | 4.30 to 4.32 |
| Two blocks of in-block work at a time (it alone: 1.86 and 1.37) | 4.60 | 4.22 |
| One table for the weak and tie bits, and validity from the keys: the tuned K4 | 4.46 | 3.98 |

The in-block loop alone measured 1.35 to 1.99 cycles a token on sqlite3 from
run to run of the same code, and 1.37 to 1.38 on libc.

What didn't help, each measured and dropped:

- Software pipelining instead of the in-block loop: each pass did the next
  block's in-block work before this block's stack part, in one iteration. 5.12
  and 5.42 cycles a token, against 4.60 and 4.22 for the three loops: the two
  chains sat side by side in the code, not step by step, and didn't overlap.
- Four blocks of in-block work at a time: that loop went from 1.37 to 1.24
  cycles a token on libc, and from 1.86 to 2.34 on sqlite3, the one large file.
- Two streams: each pass split at a top-level `;` or `}` in the file's second
  half, whose key, the lowest there is, hides every candidate before it, with
  the two halves' stack work side by side. 4.89 and 4.09 one after the other in
  each iteration, and 4.79 and 4.03 step by step, against 4.61 and 4.11. Each
  block's stack work waits on a short chain through the window, and a second
  stream didn't shorten that wait.
- A radix search of the window, eight lanes at a time: 4.66 and 4.08, against
  4.61 and 4.11. More ops, the same time. With no window search at all (wrong
  results, timing only) K4 would take 3.57 and 3.04: the search costs about 0.9
  cycles a token.
- Records without thresholds, which the passes recompute from the keys: the
  in-block loop got faster (1.46 against 1.87 on sqlite3) and the passes slower
  by as much. 4.98 and 4.20 with two blocks at a time, 4.64 and 4.09 with four,
  against 4.59 and 4.11.

### K2: refined first guesses, and a round final when no lane reads a change

In the spike, K2's blocks took 2.69 rounds on sqlite3 and 2.10 on libc. Every
block needed a round that changed nothing to confirm the one before, and most
needed another because of their first guesses. The changes, in order, each a run
on the 9700X (the last row is the series'):

| Change | sqlite3 cycles/token | libc cycles/token | Rounds a block |
| --- | ---: | ---: | --- |
| The spike's K2 | 9.10 | 7.20 | 2.69 and 2.10 |
| A round is final when its roles' features equal the last round's: the class each lane gives the token after it, and the few roles a round reads by name | 8.70 | 7.36 | 2.31 and 2.06 |
| Each class's role computed under its mask and the classes OR-ed together, instead of a chain of blends | 8.33 | 6.66 | |
| Final when no lane reads a feature that changed | 7.59 | 6.60 | 1.91 and 1.95 |
| Openers written to the group stacks only when still open at the block's end | 6.62 | 5.94 | |
| The scalar loops for tag keywords and casts only over the tokens that can need them | 6.0 to 6.4 | 5.7 to 6.1 | |
| First guesses refined by the rounds' own rules: the tuned K2 | 5.02 | 4.70 | 1.08 and 1.06 |

The convergence test saves the confirming round. When no lane reads anything
that changed, the next round would compute the same roles, so these are the
fixpoint. A lane reads the features of the token before it; a `(` also the class
of the token two back; typedef names and `{` their opener's features; and
typedef names the typed state before them. A group's commas, colons, closers and
attribute names read their opener as the same round sets it.

The guesses came from counting, in the blocks one round didn't settle, the
tokens whose guess read differently from their final role. The commonest was a
`(` guessed to open a call or a cast where it groups an expression (14,703 on
sqlite3, 224,248 on libc). Then a call's `)` where it closes the head of an
`if`, `while`, `for` or `switch` (9,824 on sqlite3) or a cast (6,960), and on
libc an identifier guessed an operand where it names a tag (7,832). The refined
guesses apply, before the first round, the rules a round applies to `(`, `*`
and the other unary operators, names after `struct`, `union` and `enum`, `{`
and the closers, fed with the plain guesses around them. They change only where
the rounds start, never their result.

| K2 rounds a block | 1 | 2 | 3 | 4 to 8 | mean |
| --- | ---: | ---: | ---: | ---: | ---: |
| sqlite3, spike | 325 | 2,894 | 7,670 | 161 | 2.69 |
| sqlite3, tuned | 10,239 | 783 | 16 | 12 | 1.08 |
| libc, spike | 824 | 54,778 | 3,171 | 2,022 | 2.10 |
| libc, tuned | 57,485 | 3,214 | 2 | 94 | 1.06 |

A round costs more than the spike's: 4.65 and 4.43 cycles a token per round on
the corpora, against 3.4, for the refined guesses and the convergence test. On
the nested ternaries, whose blocks took one round before and after, K2 went from
3.50 to 4.51 cycles a token. The casts of casts still don't converge (one round
per cast) and go to the twin. Taking out the loads of the group stacks, timing
only, to remove their store-to-load conflicts with the stores before them, left
K2 no faster.

Two checks came with the changes. K2 also runs from random roles, twice per
file, and must end at the twin's roles: a convergence test without its
typed-state condition, or without the `(`'s read of the token two back, fails
it on sqlite3 and libc. And K4's window search is compared with a scalar count
on random windows at every threshold, which a search one lane off fails. Both
builds report no difference on every input of
`--check-only --fixtures --generated 200000` with sqlite3 and libc on the 9700X.

## K5 in AVX-512, and K2's round

The second follow-up, on branch `claude/spike-186-vector-k5` (commits `dfd07b4`,
`526c67b`, `eb43cc7` and `dc81644`), vectorized K5 and made K2's round cheaper;
it also tried K4 again and kept nothing. The twins, the scalar parser and the
other kernels are the first follow-up's code, but for K1, which now hands K2 a
view of each block. Its numbers come from one series on the 9700X, measured as
before: builds of `315b6ef`, `dfd07b4` (K5 alone) and `dc81644` ran by turns,
three times each with `--counters pipeline` and with `--counters branches`,
every run under the lock by itself, and each figure is the median of the six
runs' medians.

| Cycles a token | sqlite3, `315b6ef` | `dfd07b4` | `dc81644` | libc, `315b6ef` | `dfd07b4` | `dc81644` |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Scalar parser | 43.60 | 43.93 | 44.59 | 29.09 | 29.17 | 29.67 |
| Vector path | 20.82 | 18.10 | 17.74 | 13.92 | 13.07 | 12.81 |
| Vector path, ms | 2.66 | 2.31 | 2.27 | 9.72 | 9.13 | 8.94 |
| Vector path's speedup over the scalar parser | 2.09x | 2.43x | 2.51x | 2.09x | 2.23x | 2.32x |
| Vector path's instructions a token | 50.8 | 44.7 | 42.9 | 40.6 | 37.9 | 36.3 |
| Vector path's mispredicts a token | 0.157 | 0.094 | 0.086 | 0.029 | 0.018 | 0.016 |
| K5 statements | 4.82 | 1.74 | 1.79 | 1.41 | 0.194 | 0.200 |
| K2 roles | 5.03 | 5.37 | 4.85 | 4.70 | 5.12 | 4.34 |
| K1 brackets | 2.27 | 2.24 | 2.30 | 1.85 | 1.84 | 1.84 |
| K4 parents | 4.46 | 4.47 | 4.45 | 3.97 | 3.94 | 3.94 |
| K1 second part | 0.89 | 0.79 | 0.78 | 0.69 | 0.54 | 0.54 |

The vector path's runs ranged over 17.62 to 18.49 cycles a token on sqlite3
and 12.77 to 12.84 on libc for `dc81644`, and over 20.74 to 21.13 and 13.82 to
13.97 for `315b6ef`. K1's second part is the same code in all three builds; it
ran faster in both new ones. `eb43cc7`, the commit before `dc81644`, whose K2 is
in the fast state ("K2's two speed states"), took 17.59 and 12.50 cycles a token
in a series of its own with `315b6ef` (20.94 and 13.89 there), with K2 at 4.32
and 3.94.

The steps below are runs of the stages alone (`--only stages`), three per
build, by turns with the build before.

### K5: three facts about the statement stack

The spike's K5 found the statement-level tokens with a mask, then ran the twin's
statement stack over them. The stack keeps a level for each block open, and in
each level the chain of heads (if, while, for, switch, else, do, case and
labels) that own the statement being read. That pass was scalar and branchy
(0.08 mispredicts a token on sqlite3), and it cost in proportion to statement
density: 4.8 cycles a token on sqlite3, 1.4 on libc. Three facts about the stack
make it data-parallel (`spike_statements_avx512`). Call the tokens it acts on
*events*: terminators, the `)` of a head, a case's or label's `:`, do, else, and
blocks' openers and closers.

- A head waits only until its level's next event, so a statement's first event
  is owned by the event before it in its level, if that is a head.
- The event before an event in its level is the event before it, except where a
  block that isn't a statement (a statement expression, a nested function body)
  interrupts the level around it: a statement block's events end with its
  closer, an event of the level outside. So the owners are the event lanes'
  positions compressed, shifted up a lane and expanded back, with each block's
  last event carried to the next. A statement block's owner reaches its closer
  through a stack of the blocks open, without a branch on opener or closer: each
  one's slot is the depth before the block plus the popcounts of the openers and
  closers before it, and a store a lane doesn't make goes to a sink slot.
- The rest is which if an else continues and which while ends a do. Both are
  down the chain of heads from the statement that ended before them, an else
  standing for its if. Those events are few, and a sparse loop takes them in
  order, a block behind, so that its loads find the vector part's stores done.
  The terminators after a do-while's `while (...)` go through it too, since that
  while may own them instead.

The events depend on context: a `;` between a struct's braces ends no
statement, and only a while's `)` may close a do. K2 now writes, for each
block, its *statement contexts*: which tokens are in a block (their group, or a
closer's opener's group, is a block) and which `)` close an if's or a while's
head, from the roles it settles on. A block holding the opener or the closer of
a block that isn't a statement goes an event at a time, with a stack of the
levels interrupted (7 of the check's 30,350 blocks with events). A do without
its while, or a do-while's `)` not followed by its `;`, leaves the stack's state
no longer a chain of heads; K5 then sends the file to K4 again and to the twin.
No corpus file does that.

The steps, K5 alone:

| Step | sqlite3 cycles/token | libc cycles/token |
| --- | ---: | ---: |
| The spike's K5: the mask, then the twin's stack over the masked tokens | 4.83 | 1.41 |
| Owners by compress and expand, the blocks' stack, the sparse loop a block behind (a series) | 2.03 | 0.215 |
| The sparse loop takes a terminator only after a while's `)`, while a do waits | 1.92 | 0.226 |
| The blocks' stack without branches: the vector K5 | 1.79 | 0.194 |

With the sparse loop taken out, timing only, the K5 of the third row took 1.71
cycles a token on sqlite3. Of that, the blocks' stack took 0.59, the stores of
the parents and the chains 0.10, the check for blocks that interrupt a level
0.07, and the compress and expand 0.03. The last row takes the branches out
of the stack's loop: each opener's or closer's stores go through a pointer to
its slot or to the sink. Clang turned those selects back into branches, a
mispredicted one per opener and closer, until the sink was a slot of a live
array and the conditions were `__builtin_unpredictable`. K5 now mispredicts
0.023 times a token on sqlite3, from 0.084.

What didn't help:

- Previous events from the blocks' event masks instead of a stored slot per
  event: each block stored its event mask, and only the events whose previous
  event isn't the one before them (closers, a rewritten else or do-while `;`,
  the events of interrupting blocks) kept a slot. That saves a block's four
  masked stores, but the lookups cost more: K5 went to 2.47 and 0.254 cycles a
  token, from 1.80 and 0.191, at 6.0 instructions a token against 5.4 on
  sqlite3.

The checks: K5 is compared with its twin on every file, and on 2,000 random
statement soups (`--statements N`) of heads, statements, blocks, do-whiles,
if-elses, statement expressions and nested function bodies, with an else or a
do out of place now and then. Their misplaced else, do and while and their
interrupting blocks take every rare path: 2,504 blocks went an event at a time,
53,134 events through the sparse loop, and 333 soups to the twin. K2's
statement contexts are compared with ones worked out from the twin's roles on
every input. A K5 whose blocks' stack is a slot off for some closers, one
without its sparse loop, and one that never takes a block an event at a time
each fail the check: on 80, 32 and 1 corpus files, and on 452, 472 and 234 of
the soups.

### K2: K1's view of each block, and the group stacks from registers

The statement contexts cost K2 0.34 and 0.40 cycles a token (5.42 against 5.08
on sqlite3 and 5.19 against 4.79 on libc, in a series of `dfd07b4` and
`315b6ef`). Then:

| Change | sqlite3 cycles/token | libc cycles/token | instructions/token |
| --- | ---: | ---: | --- |
| K2 with the statement contexts | 5.33 | 4.97 | 14.3 and 13.9 |
| A converged block writes its open openers onto the group stacks from its registers, compressed, one store a stack | 5.44 | 4.66 | 13.8 and 13.6 |
| K1 writes each block's view, which K2 loads instead of rebuilding it (K1 0.07 slower) | 5.02 | 4.28 | 12.6 and 12.4 |
| The statement contexts from the last round's group roles: the tuned K2 | 4.84 | 4.24 | 12.5 and 12.3 |

Each row is a build of its own, so each figure carries the band that "K2's two
speed states" describes; the instruction counts don't.

The driver's loop used to reload the open openers' roles, typed states and
keywords as bytes right after the block's 64-byte stores of them, and each such
load waited for those stores to commit. Their depths are consecutive, so a
compress of each register and one store from the first one's depth replace
them. K1's view (`SpikeBracketBlock`) holds what K2's first steps rebuilt from
four dword loads each of depths and enclosing openers: each token's enclosing
opener as a lane of the block, its depth relative to the block's start, and the
openers still open at its end.

What didn't help:

- Recomputing the class masks inside each round instead of keeping them across
  rounds, to free registers: 5.05 and 4.53.
- A constant carry from the block before (timing only): 5.60 against 5.47.
  The round doesn't wait on the block before it; it is throughput-bound, at
  about 340 instructions a round, 134 of them on the two ports that take
  compares into masks, mask moves and cross-lane permutes.

### The rare paths, in AVX-512 too

The changes above first made two generated worst cases slower, through the
paths their blocks take and the corpora's don't. K1's scalar path, for a block
that drops more than 15 depths, built K2's view a token at a time; K2's twin
worked out the statement contexts and the open openers a token at a time.
Nesting 200,000 deep went from 0.95 to 0.87 times the scalar parser's speed,
and casts of casts from 0.58 to 0.47. So:

- K1 builds the view in registers for every block, before it takes its scalar
  path: the view needs nothing from before the block.
- A block that K2's twin takes gets its statement contexts from the same vector
  code as a converged block (`spike_statement_context_avx512`), on the twin's
  roles, and its open openers from K1's view.
- Both rare paths are out of line (`spike_brackets_block_scalar` and
  `spike_roles_twin_block`), and K2's rounds run in a loop of their own that
  calls nothing (`spike_roles_rounds_avx512`). Inlined, the rare paths cost K1
  0.6 instructions a token, and a call in K2's block loop 38 a block of spills.

K1 on nesting went from 8.57 to 4.86 cycles a token (4.17 before the views),
and K2 on casts from 46.91 to 37.86 (37.70 before); the vector path runs at
1.00 and 0.56 times the scalar parser's speed there. The corpora send no block
to K2's twin, so a new check runs K2 with every block in the twin's loop on
every input and compares its statement contexts with the scalar ones: a mutant
that takes the wrong group for half the closers fails it on 624 files, and one
that drops an open opener on 443.

### K2's two speed states

K2's time depends on more than its code. Four builds of the same K2 source,
at 12.5 instructions a token, ran by turns in one window, three runs each: two
took 4.32 and 4.42 cycles a token on sqlite3 (their runs 4.29 to 4.43), the
other two 4.85 and 4.94 (4.63 to 5.56). Unrelated changes had moved K2's
function, to 0x208450 and 0x2084c0 in the slow builds and to 0x208590 and
0x2085a0 in the fast ones. Changes to K2's rare paths alone, which the corpora
never take, flip it the same way: `dc81644`'s build is in the slow state at
12.25 instructions a token, with its function aligned to a page or not.

The slow state loses dispatch slots to the back end (51% to 58%, against 46%)
at the same instruction count, with fewer store-to-load conflicts (0.11
against 0.18 a token, `--counters stalls`), no throttling by the power cap
(under 1% of slots), and more store-to-load forwards (0.78 against 0.48 a
token, `ls_stlf`): about 19 more of a block's loads take their data from a
store through the forwarding path. Each block spills about 23 vectors before
its rounds and reloads them in them. That fits Zen's renaming of stack reloads
through the register file, which is predicted by instruction address, failing
in some layouts, though no counter shows renaming.

With K2's function aligned to a page in each, `315b6ef`, `dfd07b4` and
`eb43cc7` ran K2 at 5.07, 5.47 and 4.23 cycles a token on sqlite3 and 4.69,
4.90 and 3.94 on libc, as in their own layouts, so those three builds compare
like with like. `eb43cc7` is in the fast state, `dc81644` in the slow one, so
the figures of both are given. A K2 that spills less would be less exposed;
until then its figures carry about 12% either way, and its instruction counts
are the stable measure.

The third follow-up removed both states ("K2 in one state"): without the spills
no build of K2 flipped, and the rest of its spread came from its stores to the
group stacks, which split across pages and cost more at some stack positions
than at others.

### K4: depth ranges from K1's second part

The lead the first follow-up left was to have K1's second part, which computes
the node depths, also write each block's lowest and highest and the depths
relative to the lowest, so that K4's in-block loop needn't reduce them. K4 got
faster, and K1's second part slower by more:

| Build | K4 sqlite3 | K1's second part | K4 libc | K1's second part |
| --- | ---: | ---: | ---: | ---: |
| K4 as tuned | 4.46 | 0.78 | 3.94 | 0.53 |
| Depth ranges from K1's second part | 4.08 | 1.52 | 3.58 | 1.31 |
| The same, with K1's second part's running depth from a register instead of reloaded | 4.08 | 1.28 | 3.59 | 1.07 |

That is 0.12 and 0.19 cycles a token more for the two together, so the change
was dropped. The register fix alone left K1's second part as it was (0.78 and
0.53).

## K2 in one state

The third follow-up, on branch `claude/spike-186-k2-spills` (commits `6f36b69`,
`428af9a` and `316d7a5`), took K2's spills out of its block loop, and with them
its two speed states ("K2's two speed states"). The rest of what moved its time
was stores that split across pages. The roles, flags and statement contexts are
the same on every input; 148 of sqlite3's blocks and 515 of libc's that took two
rounds now take one, from fuller first guesses. `316d7a5` changes K1's second
part, which the new builds slowed ("K1's second part's carry"). The numbers
come from one series on the 9700X, measured as before: builds of `eb43cc7`
(K2's fast state), `dc81644` (its slow state), `316d7a5` and `316d7a5` with
K2's function aligned to a page ran by turns, three times each with
`--counters pipeline` and with `--counters branches`, every run under the lock
by itself, and each figure is the median of the six runs' medians.

| Cycles a token | sqlite3, `eb43cc7` | `dc81644` | `316d7a5` | aligned | libc, `eb43cc7` | `dc81644` | `316d7a5` | aligned |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Scalar parser | 44.30 | 44.61 | 44.96 | 43.52 | 29.84 | 29.56 | 29.27 | 28.93 |
| Vector path | 17.55 | 17.84 | 16.67 | 16.66 | 12.48 | 12.76 | 11.72 | 11.67 |
| Vector path, ms | 2.24 | 2.28 | 2.13 | 2.13 | 8.72 | 8.92 | 8.18 | 8.15 |
| Vector path's speedup over the scalar parser | 2.52x | 2.50x | 2.70x | 2.61x | 2.39x | 2.32x | 2.50x | 2.48x |
| Vector path's instructions a token | 43.1 | 42.9 | 40.8 | 40.8 | 36.5 | 36.3 | 34.2 | 34.2 |
| Vector path's mispredicts a token | 0.085 | 0.086 | 0.087 | 0.087 | 0.016 | 0.017 | 0.017 | 0.017 |
| K2 roles | 4.34 | 4.88 | 3.85 | 3.87 | 3.93 | 4.35 | 3.47 | 3.47 |
| K2's six runs | 4.27 to 4.47 | 4.69 to 5.36 | 3.85 to 3.92 | 3.84 to 3.90 | 3.92 to 3.95 | 4.31 to 4.53 | 3.46 to 3.50 | 3.46 to 3.50 |
| K2's instructions a token | 12.53 | 12.25 | 10.18 | 10.18 | 12.33 | 12.08 | 10.00 | 10.00 |
| K1 brackets | 2.27 | 2.25 | 2.35 | 2.32 | 1.85 | 1.83 | 1.86 | 1.85 |
| K3 classification | 1.41 | 1.40 | 1.43 | 1.40 | 1.17 | 1.19 | 1.20 | 1.19 |
| K1 second part | 0.79 | 0.79 | 0.78 | 0.78 | 0.54 | 0.54 | 0.54 | 0.54 |
| K4 parents | 4.46 | 4.45 | 4.47 | 4.47 | 3.95 | 3.94 | 3.95 | 3.95 |
| K5 statements | 1.81 | 1.79 | 1.81 | 1.82 | 0.20 | 0.21 | 0.20 | 0.20 |

K2 takes 3.85 cycles a token on sqlite3 and 3.47 on libc: 11% and 12% less
than in `eb43cc7`'s fast state and 21% and 20% less than in `dc81644`'s slow
one, at 10.2 and 10.0 instructions a token against 12.3 and 12.1. Its six runs
stay within 2% of their median in both builds of `316d7a5`, and its function
aligned to a page changes nothing. It takes 1.06 and 1.05 rounds a block, from
1.08 and 1.06, so a round costs 3.6 and 3.3 cycles a token, against 4.65 and
4.43 tuned and the spike's 3.4. The vector path takes 16.67 and 11.72 cycles a
token, 5% and 6% less than `eb43cc7`'s and 7% and 8% less than `dc81644`'s
(its runs 16.66 to 16.93 and 11.69 to 11.84). Its speedups carry the scalar
parser's spread: the same code took 43.5 to 45.0 cycles a token on sqlite3 from
build to build. The other stages are as they were.

### What K2 kept on the stack

`dc81644`'s block ran its rounds in a loop of their own. What a round reads of
the block and doesn't change, the masks of each lane's kind and its neighbors'
kinds, the guesses, the group windows, was hoisted out of that loop, so all of
it was live across it: each block stored about 20 vectors and 25 masks before
its rounds and loaded them back in each round. The new block
(`spike_roles_block_avx512`) keeps one vector and no mask on the stack:

- **One round, with no loop around it.** The block loop
  (`spike_roles_rounds_avx512`) runs each block's first round and calls nothing.
  A block that a round doesn't settle (663 of sqlite3's 11,050, 2,795 of libc's
  60,795) leaves the loop, and `spike_roles_block_more_avx512`, out of line,
  runs its later rounds from the roles and typed states the first one left.
- **What the kinds decide, in a pass of its own.** Whatever a lane's kind and
  its neighbors' kinds decide is the same in every round, so
  `spike_role_statics_avx512` works it out for 32 blocks at a time before their
  rounds, as four vectors a block: the role where no rule holds, the role where
  the lane's rule holds, the lane's rules as bits, and the facts of a `{`, a
  comma, a colon, an ellipsis and an attribute's or an asm's `(`.
- **Features.** A round reads the token before a lane as a *feature* byte: its
  class in the low nibble and, for the roles the rules name, a code in the high
  one. One lookup in a 128-byte table (`vpermi2b`) on the feature before gives
  which of the lane's rules hold, and `{`, the separators and the attribute
  parens each take one or two more. The end of a round tests one byte vector:
  whether any lane reads a feature that changed.
- **The group stacks' windows in registers.** The 64 entries below the block's
  depth go from block to block in registers (`SpikeGroupWindows`), and since
  `428af9a` to the out-of-line rounds and back. A block still stores its open
  openers onto the stacks, for the twin's blocks.
- **The group stacks on pages** ("Stores across a page", below).

The slots each block stores and loads back in the block loop, in clang 23.1's
assembly for znver5, and K2's time alone (`--only stages`), each figure the
median of three runs by turns with `eb43cc7`, `dc81644` and the step before
(for those two, the range of their medians over the series):

| Step | sqlite3 cycles/token | libc cycles/token | instructions/token, sqlite3 and libc | Slots a block: vectors, masks, general registers |
| --- | ---: | ---: | --- | --- |
| `eb43cc7`, the fast state | 4.27 to 4.38 | 3.93 to 3.97 | 12.53 and 12.33 | 16, 25, 19 |
| `dc81644`, the slow state | 4.72 to 5.01 | 4.33 to 4.49 | 12.25 and 12.08 | 20, 25, 16 |
| One round in the block loop, the later rounds out of line | 4.81 | 4.27 | 12.18 and 11.85 | 12, 20, 23 |
| What the kinds decide as four vectors a block; features, and one lookup on the feature before for the rules that hold | 4.50 | 3.99 | 10.37 and 10.14 | 1, 9, 18 |
| The end of a round tests one byte vector | | | | 1, 9, 10 |
| The four vectors from a pass of their own, 32 blocks ahead of the rounds | 4.69 | 3.85 | 10.26 and 10.07 | 1, 5, 9 |
| `{` by two table lookups | 4.63 | 3.83 | 9.60 and 9.42 | 0, 0, 9 |
| The group stacks' windows in registers | 4.09, and 4.95 to 5.16 in some runs | 3.50 | 10.05 and 9.88 | 1, 0, 9 |
| The group stacks on pages: `6f36b69` | 3.93 | 3.50 | 10.05 and 9.88 | 1, 0, 9 |
| The windows to the out-of-line rounds and back: `428af9a` | 3.88 | 3.47 | 10.18 and 10.00 | 1, 0, 10 |

From the sixth row on, the four vectors come from a loop of their own, which in
`6f36b69` keeps two masks a block on the stack, and in `428af9a` also a general
register. The fourth row ran at 4.50 with its function aligned to a page too: no
build of it flipped. The fifth row wasn't timed alone. The steps with their own
pass were slower than the fourth on sqlite3 and faster on libc until the windows
went into registers. Loading the windows from memory no store touches, or not
storing to the stacks (both timing only, since the roles change, and on libc the
blocks took more rounds), took K2 from 4.64 to 4.22 and 4.20 cycles a token on
sqlite3 with `{` by lookups: a block's loads of the windows overlap the block
before's stores to the stacks only in part, and wait for those to commit.

### Stores across a page

With the windows in registers, K2 took 4.09 cycles a token on sqlite3 in most
runs and 4.95 to 5.16 in some: two of ten runs of one build, with address
randomization, and none of six without it. Under gdb, with randomization off and
the stack moved in steps of 128 bytes by the environment's size, it took 4.08 to
4.13 at 27 of 32 positions, 4.30 at two and 4.68 to 5.18 at three. The group
stacks were where the arena put them, and for sqlite3, depth 0 of the group
roles' stack fell 40 bytes before the end of a page: each block's 64-byte store
of its open openers, near depth 0 in most code, split across two pages. With
that stack 1 KB further on, the 32 positions took 3.90 to 4.04. The fourth
step, in which no build flipped, shows it too: at 4 of the 32 positions it took
4.72 to 4.92, against 4.44 to 4.56 at the rest. Why a split store's cost
depends on where the stack is wasn't settled: it fits the block's few loads
from the stack waiting on a split store whose address they match modulo 4 KB,
but no counter shows it.

So each stack now starts on a page, the second and the third a quarter and a
half of a page further into theirs, so that the three stacks' stores don't match
each other's addresses modulo 4 KB either (`spike_depth_stack_reserve`).
`6f36b69` took 3.92 to 4.00 at the 32 positions and 3.91 to 4.00 in ten runs
with randomization; `428af9a` took 3.85 to 3.92 at 16 positions (256-byte steps)
and 3.82 to 3.93 in ten runs. The stacks on pages alone aren't enough: `{` by
lookups with its windows still loaded from the stacks took 4.37 and 3.81,
against 4.64 and 3.82 without pages and 3.92 and 3.51 with both. Nor were the
split stores the old builds' two states: with stacks on pages, `eb43cc7` took
4.26 to 4.27 in seven of eight runs (4.57 in one) and `dc81644` 4.53 to 4.81
(4.72 to 5.00 without).

### The counters

`--counters stalls`, with the power cap's event swapped for `ls_stlf` (raw
`0x35`), and `--counters memory`, with its three events swapped for the power
cap's throttled slots, the retire queue's dispatch stalls
(`de_dispatch_stall_cycle_dynamic_tokens_part2.retq`) and the ALU tokens'
(constants in `.rodata` only, so `.text` is the same), three runs each of K2:

| K2, a token | sqlite3, `eb43cc7` | `dc81644` | `428af9a` | libc, `eb43cc7` | `dc81644` | `428af9a` |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Store-to-load conflicts | 0.206 | 0.115 | 0.020 | 0.149 | 0.086 | 0.020 |
| Store-to-load forwards | 0.48 | 0.80 | 0.40 | 0.48 | 0.82 | 0.39 |
| Cycles dispatch waits for the FP scheduling queue | 32% | 34% | 24% | 36% | 44% | 25% |
| Cycles dispatch waits for the retire queue | 0.005 | 0.024 | 0.74 | 0.004 | 0.011 | 0.75 |
| Slots the power cap throttles | 0.17 | 0.37 | 0.06 | 0.03 | 0.20 | 0.02 |

The new K2 meets a tenth of the fast state's store-to-load conflicts and a sixth
of the slow state's, and half the slow state's forwards. What holds it now is
the retire queue: a block's round is about 450 instructions, nearly the 448 ops
the reorder buffer holds, so the next block's ops wait for the round's chains of
permutes to retire (0.74 of K2's 3.85 cycles a token on sqlite3). A constant
carry from the block before took only 2% off (4.57 and 3.78 against 4.65 and
3.82, with `{` by lookups, timing only), when that step's K2 already waited on
the retire queue (0.83 cycles a token): the blocks don't wait on each other,
they wait on the buffer. Fewer instructions a block, not fewer dependencies
between blocks, is what would take K2 further.

### K1's second part's carry

In the builds of `6f36b69` and `428af9a`, K1's second part, whose code hadn't
changed, took 0.88 cycles a token on sqlite3 and 0.69 on libc instead of 0.79
and 0.54. Run 20 times in a row (`--only vector-k1-secondary --repeat 20`), it
took 0.72 to 0.76 and 0.65 against 0.61 to 0.66 and 0.49 to 0.50, so it wasn't
what K2 left behind. It met a store-to-load conflict about once a block (0.016
a token, against 0.0003). Nor was it where its arrays were: with the group
stacks allocated as before, so that its arrays were where `eb43cc7` had them,
with them where they were modulo a page, or with every array on a cache line,
it took 0.85 to 0.91 and 0.65 to 0.75. Each block stored its prefix sum and
reloaded the last live lane as a byte, the carry into the next block, and that
reload met the store before its data was ready. `316d7a5` takes the lane with a
`vpermb` and a move to a general register: 0.78 and 0.53 cycles a token,
without the conflicts, and in a build of `eb43cc7` with the same change 0.78
and 0.53 to 0.57. The same change to K1's own carry left K1 no faster (2.30 and
1.94 against 2.30 to 2.49 and 1.88 to 1.94) and `eb43cc7`'s K1 slower on libc
(1.93 against 1.85), so K1 keeps its reload.

### The generated cases

On the generated cases ("Generated worst cases"), K2 takes 3.3 cycles a token
wherever its blocks take one round, from 4.0 to 4.4. The declarations, whose
every block takes two, are where it didn't gain (5.59, against 5.55): their
second round runs out of line, where each round loads 14 vectors kept on the
stack across the rounds. Before `428af9a` handed the windows across, it took
5.87 there.

### What didn't help

- **The round peeled alone.** One round in the block loop, everything else as
  `dc81644`: 12 vectors and 20 masks a block on the stack instead of 20 and 25,
  and still the slow state, 4.81 and 4.27.
- **The four vectors worked out in the block loop**, everything else as
  `6f36b69`: 4.48 and 4.24, against 3.92 and 3.51.
- **One pass over the whole file for them** instead of 32 blocks at a time: 4.72
  and 3.83, against 4.69 and 3.85. Chunks of 16 took 4.75 and 3.85, of 64 4.69
  and 3.83. The 32 blocks' 8 KB stay in L1.
- **Two rounds in the block loop**, only a third out of line, with the windows
  in registers: 16 vectors a block on the stack again, 11.1 instructions a token
  against 10.0, and 3.58 cycles a token on libc against 3.50 (on sqlite3, 4.18
  against 4.11 to 4.92, as the split stores moved it).
- **Every block taken as settled after its first round** (timing only, with the
  windows in registers): 3.43 on libc against 3.50, and 3.96 on sqlite3 against
  4.11 in the runs without the split stores' slow state. The blocks that leave
  the loop cost about 0.1 cycles a token.

The checks are the second follow-up's: K2's roles, flags and statement contexts
against the twin's on every input, from random roles twice per file, and with
every block in the twin's loop. Each of the three commits reports no difference
on every input of `--check-only --fixtures --generated 200000` with sqlite3 and
libc, on the 9700X and locally, with the same rounds per block.

## Where the vector path's time goes

Stage by stage, from the follow-up's series, from the second follow-up's for
`dc81644`, and from the third's for `316d7a5`:

| Cycles a token | sqlite3, spike | tuned | K5 in AVX-512 | K2 in one state | libc, spike | tuned | K5 in AVX-512 | K2 in one state |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| K2 roles | 9.10 | 5.02 | 4.85 | 3.85 | 7.20 | 4.70 | 4.34 | 3.47 |
| K5 statements | 4.85 | 4.83 | 1.79 | 1.81 | 1.41 | 1.42 | 0.20 | 0.20 |
| K4 parents | 7.03 | 4.46 | 4.45 | 4.47 | 6.66 | 3.98 | 3.94 | 3.95 |
| K1 brackets | 2.30 | 2.26 | 2.30 | 2.35 | 1.85 | 1.84 | 1.84 | 1.86 |
| K3 classification | 1.41 | 1.41 | 1.40 | 1.43 | 1.20 | 1.18 | 1.19 | 1.20 |
| Fallback | 1.09 | 1.08 | 1.13 | 1.19 | 0.35 | 0.35 | 0.35 | 0.35 |
| K1 second part | 0.88 | 0.88 | 0.78 | 0.78 | 0.69 | 0.69 | 0.54 | 0.54 |
| K3 typedef pass | 0.10 | 0.10 | 0.11 | 0.10 | 0.47 | 0.47 | 0.50 | 0.49 |
| Vector path | 26.79 | 20.91 | 17.74 | 16.67 | 19.36 | 13.89 | 12.81 | 11.72 |

**K2** was the largest stage, no longer for its rounds: nearly every block
takes one. What one round costs is K2's time: 4.4 to 4.7 cycles a token tuned,
4.5 and 4.1 in the second follow-up's build (4.0 and 3.7 in `eb43cc7`'s, whose
K2 is in the fast state), with about half its slots backend-bound at IPC 2.5
to 2.9, and 3.6 and 3.3 in one state, at IPC 2.6 and 2.9, waiting on the
retire queue ("K2 in one state"). K4 is now the largest stage.

**K5** was the statement stack over the statement-level tokens: scalar, branchy
(0.08 mispredicts a token on sqlite3) and front-end-bound, so it cost in
proportion to statement density, 4.8 cycles a token on sqlite3. In AVX-512 a
block without statement-level tokens costs it a table lookup and a mask, and
one with them a compress, an expand and a few steps without branches: 1.8 on
sqlite3, 0.2 on libc and 2.5 on the statements case, from 4.8, 1.4 and 6.3.

**K4** takes 4.5 and 4.0 cycles a token in its three loops, short of the 3.5
the spike estimated for interleaved blocks. Its in-block loop runs at IPC 2.6
with 60% of its slots backend-bound. The passes wait on each block's window,
whose search costs about 0.9 cycles a token.

**K1, K3 and K1's second part** take 4.5 to 4.7 cycles a token together on
sqlite3: they are passes over bytes with a few permutes each.

The spike's analysis, which the follow-up revised: K4 was 74% backend stalls at
IPC 1.7, with each 64-token block's chain about 140 cycles a pass (the block's
lowest and highest depths by reductions, about 35; the doubling table, about
24; the six-step search, about 48; merging the in-block and window results,
about 12). Removing every push and pop took it from 6.93 to 6.50 cycles a
token, and taking the block's base depth from memory instead of the reductions
from 7.02 to 6.25 (timing only). The spike blamed the 448-entry reorder buffer,
which a block's ~450 ops nearly fill, for the blocks' poor overlap; the counters
point at the floating-point scheduling queue instead ("K4 and K2, tuned"). K2
cost 3.5 cycles a token per round, in 2.7 rounds a block on sqlite3 and 2.1 on
libc.

## Recommendation for #127

Build #127's parser as these kernels, with the scalar parser beside them:

- **Vector**: K1 (brackets, 64 byte lanes), K3's classification (mask loops), K2
  (roles, from refined guesses, final when no lane reads a change, one round in
  the block loop, with what the kinds decide from a pass before the rounds and
  the group windows in registers), K1's second
  part (node depths), and K4 (parents, 64 byte lanes: every block's in-block
  work first, two blocks at a time, then the passes with the stack's top in
  registers, and aligned parts where depths span too much), and K5
  (statements: owners from a compress and an expand of the events' positions,
  the blocks' owners on a stack without branches, a sparse loop for else, do
  and while, from K2's statement contexts). Each wins 2.5 to 15 times over its
  twin, K5 9 to 61 times, and they give the whole path its 1.5 to 1.6 times,
  2.1 with K4 and K2 tuned, 2.3 to 2.5 with K5 in AVX-512, 2.5 to 2.7 with K2
  in one state. Keep each kernel's rare paths out of its hot loop's function:
  inlined, they changed the hot loops' code ("The rare paths, in AVX-512 too").
  Keep a block's work free of inner loops whose invariants the compiler hoists
  onto the stack, and start by-depth stacks on pages: K2's loop of rounds kept
  about 20 vectors and 25 masks a block on the stack, and with its stores split
  across pages, its time depended on the build and the stack ("K2 in one
  state").
- **Scalar**: the typedef pass (0.1 to 0.5 cycles a token, in source order),
  the statement stack (K5's twin, which also takes a file whose do misses its
  while), and the explicit-state parser.
  The parser is the fallback for every top-level declaration with a flagged
  token (0.06% to 1.44% of tokens here), for any K2 block that doesn't converge,
  and for nesting deeper than 2,047. It is also the oracle every kernel is tested
  against, and the place where diagnostics and error recovery live. Keep the
  scalar twins as tests only: as a parser they are 2.4 times slower than it.
- **What the parser emits: both.** The parent and node-depth vectors, with the
  roles, are what the kernels compute and what queries about a node's parent,
  children or nesting want. The postorder comes from them in one branch-free
  pass (`spike_postorder_sorted`, 6 to 7 cycles a token), for #133's
  single-pass type checker. With it the vector path is still 1.28 times as fast
  on sqlite3 (34.3 against 44.0 cycles a token) and 1.16 times on libc (25.6
  against 29.7), against a scalar parser that emits no postorder at all. With K4
  and K2 tuned, 1.57 and 1.49 times (28.0 and 19.9), with K5 in AVX-512, 1.77
  and 1.58 times (25.2 and 18.8), and with K2 in one state, 1.87 and 1.65 times
  (24.0 and 17.7).
  - From K4's R and the depth of its L stack, the positions would be
    end(v) - (the number of v's ancestors before it), with no histogram. That
    would make the postorder nearly free, but it hasn't been measured.
- **Typedef scopes (#128)**: block-scope typedefs are most of the fallback. Exact
  per-identifier binding stacks in K3 would keep those declarations on the
  vector path.

What would close most of the remaining gap to #115's 2 ms for sqlite3 (about 16
cycles a token at 5.5 GHz, against the spike's 27.3, 20.9 tuned, 17.7 with K5
in AVX-512 and 16.7 with K2 in one state):

- K2 with fewer instructions a block: in one state it waits on the retire
  queue, its round about as long as the reorder buffer ("K2 in one state");
- K4's window search (about 0.9 cycles a token);
- K1 and K3's classification (2.3 and 1.4 cycles a token).

The spike's first two leads, interleaving K4's blocks and fewer K2 rounds, are
the first follow-up's: they took 5.9 cycles a token off sqlite3 and 5.5 off
libc. Its leads in turn, a vector K5 and a cheaper K2 round, are the second's:
3.1 and 1.1 more. The second's, a K2 that spills less, is the third's: 0.9 and
0.8 more against `eb43cc7`'s build, 1.2 and 1.0 against `dc81644`'s.

For multicore (#149, #151), top-level declarations start with empty stacks at
depth 0, so files split there, with only the typedef pass ordered.

## Reproduction

On the 9700X, from a checkout of the branch, each command under the lock by
itself (`--check-only` runs the checks alone, here on the fixtures, every
generated case and both corpora):

```sh
EXCLUSIVE_OWNER=me ~/buster-fast-bench/exclusive.sh ./build.sh
EXCLUSIVE_OWNER=me ~/buster-fast-bench/exclusive.sh build/compiler spike-parse --check-only --fixtures --generated 200000 inputs/sqlite3.i inputs/libc-valid
EXCLUSIVE_OWNER=me ~/buster-fast-bench/exclusive.sh taskset -c 2 build/compiler spike-parse --counters pipeline inputs/sqlite3.i
EXCLUSIVE_OWNER=me ~/buster-fast-bench/exclusive.sh taskset -c 2 build/compiler spike-parse --counters branches inputs/libc-valid
EXCLUSIVE_OWNER=me ~/buster-fast-bench/exclusive.sh taskset -c 2 build/compiler spike-parse --generated 200000 --case casts
```

The inputs are preprocessed as described under "Inputs": for each `.c` file,
`env -u CPATH clang -E -P FILE -o FILE.i`, keeping those that
`env -u CPATH clang -fsyntax-only -x c -w` accepts. `--check-only` stops after
the checks. `--no-check --only NAME --repeat N` runs a single stage N times per
file, for `perf record`. `--only stages` times only the stages.
`--statements N` sets how many random statement soups the checks compare K5
on (2,000 by default), and `--counters stalls` counts store-to-load conflicts,
the slots the power cap throttles and the cycles dispatch waits for the
floating-point scheduling queue.

The tuned K4 is the stage `vector-k4`, and its three loops `vector-k4-prepare`,
`vector-k4-left` and `vector-k4-right`; the spike's K4 is `vector-k4-blocks`.
The vector K5 is `vector-k5`, and the spike's K5 `vector-k5-mask`.
The dispatch stalls of one stage, here the in-block loop's:

```sh
EXCLUSIVE_OWNER=me ~/buster-fast-bench/exclusive.sh taskset -c 2 perf stat -e cycles:u,de_dispatch_stall_cycle_dynamic_tokens_part1.fp_sch_rsrc_stall:u,de_dispatch_stall_cycle_dynamic_tokens_part2.retq:u build/compiler spike-parse --no-check --passes 1 --only vector-k4-prepare --repeat 100 inputs/sqlite3.i
```
