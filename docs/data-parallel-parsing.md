# Data-parallel parsing of C on Zen 5 (spike #186)

Measured on 2026-10-20 on the Ryzen 7 9700X (`benchpress`), with Clang 23.1.1 and
`build.sh`'s `-O3 -march=native` (znver5). Every build and run there took the
shared `~/buster-fast-bench/exclusive.sh` lock by itself, and every benchmark was
pinned to CPU 2 (performance governor; its SMT sibling, CPU 10, offline). The
corpus runs are of commit `64b5c31`, the generated cases' of `7edd830`, whose timed
code is the same. The code is `src/buster/compiler/parse_spike.c` and
`parse_spike_benchmark.c` (the `spike-parse` command) on branch
`claude/jolly-turing-7b4673`.

The question (#186): #15 called the parser inherently sequential. Does
preprocessed C parse faster on one Zen 5 core with branch-free AVX-512 kernels,
which build the tree with nearest-smaller-value searches (Bar-On and Vishkin,
1985), than with a scalar shunting-yard parser?

## Results

Yes, by 1.5 to 1.6 times on real code, with the same tree on every token:

| Whole path | sqlite3 ms | cycles/token | instructions/token | IPC | mispredicts/token | libc ms | cycles/token | instructions/token | IPC | mispredicts/token |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Scalar parser | 5.61 | 44.0 | 190.3 | 4.33 | 0.496 | 20.93 | 29.7 | 182.7 | 6.16 | 0.106 |
| Kernels' scalar twins | 13.68 | 107.3 | 374.7 | 3.49 | 1.514 | 47.49 | 67.7 | 361.7 | 5.34 | 0.354 |
| Vector path | 3.48 | 27.3 | 58.0 | 2.13 | 0.170 | 13.70 | 19.6 | 45.5 | 2.32 | 0.032 |

sqlite3 is the 3.53.4 amalgamation, 707,153 tokens. libc is 733 files of the
libc corpus, 3,869,223 tokens; the corpus is under "Inputs". Each figure is the
median of 21 interleaved passes; the whole paths' fastest and slowest passes are
within 1.3% of it.

- **The vector path is 1.61 times as fast as the scalar parser on sqlite3 and
  1.51 times on libc.** It retires 3.3 to 4 times fewer instructions, at half the
  IPC: its kernels are backend-bound (45% to 60% of dispatch slots). The scalar
  parser loses its slots to the front end and to bad speculation instead (32% and
  15% on sqlite3, with 0.50 mispredictions a token).
- **Every vectorized kernel beats its scalar twin, by 2.5 to 15 times**, but
  the twins together cost 2.4 times what the scalar parser does. Splitting the
  parse into kernels only pays once they are vectorized.
- **The scalar parser is a fair baseline:** inlining, one packed role table,
  sentinels and stacks of entries took it from 53.1 to 44.0 cycles a token on
  sqlite3, and from 36.8 to 29.7 on libc. Its IPC on libc is 6.2.
- **sqlite3 costs more per token than libc on every path**: its function bodies
  branch less predictably than libc's header declarations. It isn't memory: L1
  fills from L3 stay at or below 0.023 a token and from DRAM below 0.003, except
  the counting postorder's 0.03 from DRAM on sqlite3.
- **Two kernels take most of the vector path's time.** K2 (roles) takes 35%, at
  3.5 cycles a token per round of its fixpoint. K4 (parents) takes 27% to 35%,
  latency-bound inside each 64-token block.
- **The fallback is small**: 1.44% of sqlite3's tokens and 0.06% of libc's go to
  the scalar parser, a top-level declaration at a time.
- **The generated worst cases bound the losses.** Chains of casts never
  converge in K2's 8 rounds and 200,000 nested parentheses exceed K4's 16-bit
  keys. The kernel falls back to its twin, and the vector path runs at 0.54 and
  0.94 times the scalar parser's speed.
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
   deeper drops.
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
   chain on 64-bit masks. A token the rules can't be sure of is flagged.
4. **K1's second part.** `?`, `case` and `default` matched to their colons per
   depth in a sparse scalar loop over the few such tokens. A colon with no opener
   is a label. Then the node depths by a vector prefix sum.
5. **K4, parents.** L by a forward pass and R by a backward one, both in 64-token
   blocks of byte keys relative to the block's lowest node depth (a block whose
   depths span more than 6 runs as two 16-bit blocks). A doubling search within
   the block, then a binary search, in registers, of the candidate stack's top
   32 entries. Branch-free pops, compressed pushes, and each role's rule with
   `vpermw` tables.
6. **K5, statements.** A mask of the statement-level tokens, then the statement
   stack over just those.
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
  that swaps two tokens is caught).

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
| K3 typedef pass | 0.10 | 0.4 | 3.96 | 0.10 | 0.4 | 3.95 | 1.0x | 41.1/29.3/16.1/13.4 |
| K3 classification | 7.84 | 10.9 | 1.40 | 1.41 | 4.5 | 3.19 | 5.5x | 40.5/16.4/33.7/9.4 |
| K2 roles | 32.82 | 128.8 | 3.92 | 9.64 | 19.6 | 2.03 | 3.4x | 27.8/10.7/52.6/8.9 |
| K1 second part | 13.60 | 38.1 | 2.80 | 0.88 | 1.9 | 2.10 | 15.4x | 26.2/11.4/48.2/14.1 |
| K4 parents | 27.61 | 111.8 | 4.05 | 7.36 | 12.1 | 1.65 | 3.8x | 22.6/1.7/74.8/0.9 |
| K5 statements | 16.57 | 65.0 | 3.92 | 4.91 | 12.6 | 2.58 | 3.4x | 28.2/48.2/3.0/20.6 |
| Fallback | 1.11 | 3.8 | 3.40 | 1.09 | 3.8 | 3.48 | 1.0x | 39.5/28.3/18.9/13.3 |
| Sum | 107.6 | | | 27.7 | | | 3.9x | |

libc:

| Kernel | Twin cycles/token | instructions/token | IPC | Vector cycles/token | instructions/token | IPC | Speedup | Vector slots: retiring/frontend/backend/other % |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| K1 brackets | 4.73 | 19.9 | 4.20 | 1.86 | 3.0 | 1.64 | 2.5x | 21.7/6.8/65.1/6.4 |
| K3 typedef pass | 0.53 | 2.2 | 4.19 | 0.47 | 2.2 | 4.69 | 1.1x | 49.3/30.3/11.0/9.4 |
| K3 classification | 3.20 | 11.1 | 3.46 | 1.19 | 4.3 | 3.62 | 2.7x | 45.5/12.4/36.7/5.4 |
| K2 roles | 23.29 | 125.7 | 5.40 | 7.26 | 16.7 | 2.30 | 3.2x | 31.5/8.0/56.6/3.9 |
| K1 second part | 7.68 | 38.2 | 4.97 | 0.69 | 1.7 | 2.52 | 11.1x | 31.7/9.2/58.1/1.0 |
| K4 parents | 19.12 | 108.7 | 5.69 | 6.84 | 12.2 | 1.78 | 2.8x | 24.4/1.4/73.8/0.3 |
| K5 statements | 12.15 | 58.5 | 4.82 | 1.57 | 4.5 | 2.87 | 7.7x | 31.9/39.1/15.4/13.6 |
| Fallback | 0.36 | 1.3 | 3.54 | 0.35 | 1.3 | 3.61 | 1.0x | 43.8/15.0/39.2/1.9 |
| Sum | 71.1 | | | 20.2 | | | 3.5x | |

The stages' sums are within 5% of the whole paths' 107.3 and 27.3 (sqlite3)
and 67.7 and 19.6 (libc). The typedef pass and the fallback are scalar in both
paths.

### 32 against 64 lanes, and K3's two classifications

| Variant | sqlite3 cycles/token | instructions/token | libc cycles/token | instructions/token |
| --- | ---: | ---: | ---: | ---: |
| K1, 32 lanes of 16 bits | 3.39 | 4.4 | 2.60 | 4.3 |
| K1, 64 lanes of 8 bits | 2.31 | 3.1 | 1.86 | 3.0 |
| K4, 32 lanes of 16 bits | 8.87 | 13.0 | 8.82 | 13.1 |
| K4, 64 lanes of 8 bits (16-bit blocks where depths span more than 6) | 7.36 | 12.1 | 6.84 | 12.2 |
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

At 15 to 20 cycles each, the scalar parser's mispredictions cost sqlite3 about 8
to 10 of its 44 cycles a token. Half of the vector path's come from K5, most of
the rest from K2 and K3. The prefetchers keep up with every pass over whole files, so
fills from L3 and DRAM are negligible even for sqlite3, whose arrays (about
20 MB) are in L3. Splitting files into chunks that fit in L2 isn't needed for one
core.

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

## Where the vector path's time goes

**K4** is latency-bound within each block: 74% of its dispatch slots are
backend stalls, at IPC 1.7. Each 64-token block's chain is about 140 cycles a
pass:

- the block's lowest and highest depths, by reductions (about 35);
- the doubling table (about 24);
- the six-step search (about 48);
- merging the in-block and window results (about 12).

A block's ~450 ops nearly fill the 448-entry reorder buffer, so consecutive
blocks barely overlap. Two experiments on the 9700X, timing only:

- Removing every push and pop, which takes the stack's store-to-load chain
  across blocks out, took K4 from 6.93 to 6.50 cycles a token.
- Taking the block's base depth from memory instead of the reductions took it
  from 7.02 to 6.25.

What remains is the in-block searches. The fix would be two blocks in flight at
once: the in-block work of every block first (blocks are independent there),
interleaved two at a time, then the stack's part. At an IPC of about 3.5, K4
would take about 3.5 cycles a token.

**K2** costs 3.5 cycles a token per round. Its blocks took 2.7 rounds on average
on sqlite3 and 2.1 on libc (sqlite3: 325 blocks in 1 round, 2,894 in 2, 7,670 in
3, 161 in 4 to 6). Better first guesses, from (predecessor kind, kind) pairs
rather than kinds, would save rounds. Casts of casts need one round per cast, so
long chains of them always go to the twin.

**K5** is the statement stack over the statement-level tokens. It is scalar,
branchy (0.08 mispredicts a token on sqlite3) and front-end-bound, so it costs in
proportion to statement density: 4.9 cycles a token on sqlite3, 1.6 on libc,
6.3 on the statements case.

**K1, K3 and K1's second part** take 4.7 cycles a token together on sqlite3:
they are passes over bytes with a few permutes each.

## Recommendation for #127

Build #127's parser as these kernels, with the scalar parser beside them:

- **Vector**: K1 (brackets, 64 byte lanes), K3's classification (mask loops), K2
  (roles), K1's second part (node depths), and K4 (parents, 64 byte lanes with
  16-bit blocks where depths span too much). Use K5's mask of statement-level
  tokens too. Each wins 2.5 to 15 times over its twin, and they give the whole
  path its 1.5 to 1.6 times.
- **Scalar**: the typedef pass (0.1 to 0.5 cycles a token, in source order),
  K5's statement stack over the masked tokens, and the explicit-state parser.
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
  against 29.7), against a scalar parser that emits no postorder at all.
  - From K4's R and the depth of its L stack, the positions would be
    end(v) - (the number of v's ancestors before it), with no histogram. That
    would make the postorder nearly free, but it hasn't been measured.
- **Typedef scopes (#128)**: block-scope typedefs are most of the fallback. Exact
  per-identifier binding stacks in K3 would keep those declarations on the
  vector path.

What would close most of the remaining gap to #115's 2 ms for sqlite3 (about 16
cycles a token at 5.5 GHz, against 27.3 now):

- interleaving K4's blocks (about 3.5 cycles a token);
- fewer K2 rounds (about 3.5 cycles a token for each round saved);
- a vector K5 for statement-dense code.

For multicore (#149, #151), top-level declarations start with empty stacks at
depth 0, so files split there, with only the typedef pass ordered.

## Reproduction

On the 9700X, from a checkout of the branch, each command under the lock by
itself (`--check-only` runs the checks alone, here on the fixtures, every
generated case and sqlite3):

```sh
EXCLUSIVE_OWNER=me ~/buster-fast-bench/exclusive.sh ./build.sh
EXCLUSIVE_OWNER=me ~/buster-fast-bench/exclusive.sh build/compiler spike-parse --check-only --fixtures --generated 200000 inputs/sqlite3.i
EXCLUSIVE_OWNER=me ~/buster-fast-bench/exclusive.sh taskset -c 2 build/compiler spike-parse --counters pipeline inputs/sqlite3.i
EXCLUSIVE_OWNER=me ~/buster-fast-bench/exclusive.sh taskset -c 2 build/compiler spike-parse --counters branches inputs/libc-valid
EXCLUSIVE_OWNER=me ~/buster-fast-bench/exclusive.sh taskset -c 2 build/compiler spike-parse --generated 200000 --case casts
```

The inputs are preprocessed as described under "Inputs": for each `.c` file,
`env -u CPATH clang -E -P FILE -o FILE.i`, keeping those that
`env -u CPATH clang -fsyntax-only -x c -w` accepts. `--check-only` stops after
the checks. `--no-check --only NAME --repeat N` runs a single stage N times per
file, for `perf record`. `--only stages` times only the stages.
