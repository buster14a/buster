# Expression fragments: the decision (#104–#107)

Issues [#104](https://github.com/buster14a/buster/issues/104) to
[#107](https://github.com/buster14a/buster/issues/107) tested Niles Salter's
alternative to a syntax tree for expressions: an implicit preorder of one-byte
kinds, built as the recognizer reduces by joining short runs of operators into
bounded fragments, with the leaves left in source order. The experiment compared
complete paths, from a unit's preprocessed tokens to the checker's results, with
one semantic consumer whose results had to be identical, against the postorder
the parser emits. The postorder won on every corpus, by 14% to 51% of the whole
path, and the vector parser's postorder (D, the compiler's default) was the
fastest of the four paths. What was measured is an adapter: the checker kept
walking the tree, and the fragments supplied only each operator's operands. The
result is about fragments beside this checker's tree, not about a one-pass
prefix-driven checker or a parser that skips the tree ("What the decision
covers"). By #107's rule the fragment code was then removed;
commit [`6c5b21a1`](https://github.com/buster14a/buster/tree/6c5b21a18eda1dd620322d889295980178a97999/src/buster/compiler)
has all of it, consumer and benchmark included, for anyone who wants to run it
again ("Reproducing"), and [the record](https://github.com/buster14a/buster-performance/tree/cbf9a269b02e21705c21c173f914d36835e28f30/docs/measurements/parser-fragments-107-2026-10-26) has the measurements' logs,
scripts and inputs.

## The four paths

| Variant | Construction | Consumer |
| --- | --- | --- |
| A, postorder | the scalar parser's tree and its postorder (`parse_postorder_sorted`) | the checker's postorder reader, `check_input_expressions` |
| B, prefix | the scalar parser with the fragment sink; each expression flattened into prefix kinds | the fragment reader, over the flat array |
| C, fragments | the same sink | the fragment reader, straight from the fragments' windows |
| D, sorted | the vector parser's parents and depths, ordered by the same counting sort | the postorder reader |

A, B and C share the scalar recognizer: the sink was a second output of its
reductions, which still wrote the tree. D is the vector path, which builds the
same tree. All four gave the checker the same input and got the same graph and
typed stream back.

## What was built

- The builder (#105, [#315](https://github.com/buster14a/buster/pull/315)):
  the sink, behind a compile-time switch that only a unit of its own
  (`main_unit_fragments.c`) turned on. A fragment was a 16-byte header (a 64-bit
  mask over up to 64 positions, a link, counts) and 16, 32 or 48 operator
  bytes; a join copied at most one boundary record and linked otherwise, and an
  identity column kept each position's parser token. A window was decoded by
  two `vpexpandb` and a mask inversion (`c_fragment_interleave`), with a scalar
  twin.
- The readers ([#344](https://github.com/buster14a/buster/pull/344),
  [#358](https://github.com/buster14a/buster/pull/358)): an index of each
  published expression's leaves in source order, and a resumable cursor whose
  events are ENTER, REFERENCE (a type name's or a block's subtree), CHILD,
  REDUCE, LEAVE and ATTACH, from the flattened prefix (B) or the fragments'
  windows (C).
- The consumer (#106) and the measurement (#107), below.

## The consumer (#106)

The checker builds its expression graph in `check_input_expressions`: one walk
of the tree in postorder, which builds each token's graph nodes from its
operands, the values its children in the tree produced. `check_graph_run` then
checks the graph: the operands' conversions, the type rules and the folding. The
fragment reader drove the same walk and the same code for each token, and
changed only where an operator's operands came from. A cursor per published
expression, started at the expression's first fragment token in postorder,
collected the values each child completed in a pending frame: ENTER pushed it,
CHILD appended the child's values, a cast's REFERENCE its type name's, and
REDUCE took the frame at the operator's own place in the walk. A child's values
were those of its envelope, the tree's child of the operator that holds it (a
group around it, the `?` before a middle operand), which the walk had just
built. Cursors nested as expressions do in postorder (an array bound's
expression inside a cast's type name), on one explicit stack, the walk
advancing only the top one, as the visit ledger requires. Variant B flattened
each expression when its cursor started; C decoded windows as it read.

This is an adapter, not the consumer #106 designed. #106 asked for one driver
that reads expressions in preorder and runs each operator's semantic action when
its last child completes, switching between postorder (declarations and
statements) and preorder (expressions) by tagged continuations on one explicit
stack. Here the postorder walk stayed the driver: it visited every token of the
tree, expressions' included, and the fragment cursors ran in step with it. The
checker's semantic actions are written per token of the tree (the next
section), so a driver of #106's kind needs them written again for fragment
kinds; that wasn't built, and neither was a parser that skips the tree for
expressions.

C's constructs that need context before their children (`&&`, `||`, `?:`,
`sizeof`, `_Alignof`, `typeof`, `_Generic`) get it in this checker from the
declaration walk in source order (constant contexts, query types, selections),
before the expressions' walk, not from markers in the postorder; neither reader
had anything to do at ENTER.

### Why the tree stayed

The checker's code for a token reads tokens the fragments leave out. A group's
`)` gets a token node of its own, where an initializer reads a parenthesized
item's value; a statement expression's region hangs on its group's closer; a
designator's index is a value node at its bracket; a builtin's keyword is a
child of its call. So the leaves, and every token the fragments drop, kept
their operands from the tree. The sink also marked an operator that gets a child
in the tree carrying no fragment (`raw_operands`: a builtin's keyword, and the
bracket of GNU's `[0 ... 1] +1`, which the tree makes the prefix `+`'s first
child, why the checker refuses that construct), and that operator took its
operands from the tree too. The fragment reader therefore replaced only the
gathering of an operator's operands, the smallest part of the walk, and
everything else it needed came on top of a tree that every path still built.

### Identity

`test-fragments` checked each unit with the postorder reader, then with each of
12 fragment modes (B and C; the interleave's AVX-512 path and its scalar twin;
16-, 32- and 48-byte payloads), and with the vector path (D).
`check_corpus_equal` compared each with the postorder reader's result: the tree
and the parser's diagnostics, every column of the checker's input and graph
(token nodes, declarations, regions, VLAs, asm operands and the rest), the typed
stream, the initializers and the checked program. The reader also compared each
operator's operands with its children's in the tree as it took them, and had to
read every published expression of a unit without reports.

The comparison stops at the checked program: nothing lowered it. So #106's own
criterion, each function's IR (`check_program_lower`'s output, Tier 0's input)
the same instruction for instruction, and the `-c` objects byte for byte that
the claim on #106 named were not compared, and stay unverified. The
benchmark's `--to-ir` run lowered SQLite on every path without a failure, but
its first pass compares nothing past the checker's typed stream. On the 9700X
and locally:

| Inputs | Units | The checker accepted | Fragment readings | Differences |
| --- | ---: | ---: | ---: | ---: |
| `test-fragments`' fixtures | 45 | 30 | 540 | 0 |
| the parser's snippets | 96 | 62 | 1,152 | 0 |
| `test-checker`'s source fixtures (`--checker-fixtures`) | 747 | 433 | 8,964 | 0 |
| `sqlite3.c` 3.53.4 (53,349 expressions) | 1 | 1 | 12 | 0 |
| the libc corpus | 733 | 733 | 8,796 | 0 |
| the generated corpora (below) | 2 | 2 | 24 | 0 |
| this repository's translation units (locally) | 42 | 2 | 504 | 0 |

A unit the checker doesn't accept still compares: its reports, and its
diagnostic graph up to the first one. The repository's other 40 units stop at
x86 rotate intrinsics the checker doesn't support yet (`_mm512_rol_epi32` in
the encoder, `__builtin_rotateleft8` in clang's `ia32intrin.h`).
`test-checker`'s sections of source fixtures also passed their own 23,444
checks in the fragments' unit.

## The measurement (#107)

`benchmark-parser --expressions all` ran the four paths on the 9700X, each run
one file through one path: `parse_translation_unit`, `check_input` and
`check_graph_run` (the vector schedule, the default), timed alone, with the
preprocessing, the type table and the arenas set up outside the time (the
arenas reused warm; the parser's own columns mapped afresh each call, as in the
compiler). A first pass checked that every path gave every file A's graph, token
nodes, reports and typed stream. Then 21 passes after a warm-up, the paths'
order rotating by pass; each figure below is the median over the passes, then
the median over the processes. Every process ran under the shared lock, pinned
to CPU 2 (governor and energy preference `performance`). The build was tree
`7808e811` (the code of `6c5b21a1`), clang 23.1.1, `build.sh`'s `-O3 -g`,
`build/compiler` sha256 `f34536ca…`. The sink used the 16-byte payload and the
interleave's AVX-512 path. A token is one of the parser's tokens, the unit's
after preprocessing: the expanded tokens #107 names, but for 71 records of the
preprocessor's output among libc's 3.87 million that the parser's tokens leave
out.

The corpora were the frozen inputs of #315's qualification: SQLite 3.53.4's
`sqlite3.c` (706,586 tokens) and the libc corpus (733 files of Zig 0.14.1's
`lib/libc` that clang accepts, 3,867,223 tokens); and two generated ones
(`generate.c` in the record, seed 107): long expressions, 60 functions each
returning one expression of 500 to 8,000 operators, balanced or chained to
either side (700,209 tokens), where bounded fragment joins should help most;
and small functions, 6,000 functions of a few statements each (759,127 tokens).

Cycles per token, and the change from A:

| Corpus | Processes | A, postorder | B, prefix | C, fragments | D, sorted |
| --- | ---: | ---: | ---: | ---: | ---: |
| SQLite | 5 | 883.5 | 1,302.3 (+47.4%) | 1,287.0 (+45.7%) | 757.5 (−14.3%) |
| the libc corpus | 3 | 514.8 | 585.6 (+13.8%) | 584.5 (+13.6%) | 390.9 (−24.1%) |
| long expressions | 3 | 828.3 | 1,249.7 (+50.9%) | 1,241.8 (+49.9%) | 719.2 (−13.2%) |
| small functions | 3 | 879.4 | 1,281.3 (+45.7%) | 1,267.6 (+44.1%) | 769.2 (−12.5%) |

Each path's processes were within 1.1% of each other (0.6% on the real
corpora). Instructions per token, which were the same in every process:

| Corpus | A, postorder | B, prefix | C, fragments | D, sorted |
| --- | ---: | ---: | ---: | ---: |
| SQLite | 2,392.6 | 3,343.2 (+39.7%) | 3,321.7 (+38.8%) | 1,878.1 (−21.5%) |
| the libc corpus | 1,855.2 | 2,169.2 (+16.9%) | 2,167.7 (+16.8%) | 1,312.5 (−29.3%) |
| long expressions | 2,618.4 | 3,894.7 (+48.7%) | 3,868.5 (+47.7%) | 2,173.9 (−17.0%) |
| small functions | 2,483.7 | 3,402.8 (+37.0%) | 3,378.3 (+36.0%) | 1,987.9 (−20.0%) |

Nanoseconds per token:

| Corpus | A, postorder | B, prefix | C, fragments | D, sorted |
| --- | ---: | ---: | ---: | ---: |
| SQLite | 161.2 | 237.0 (+47.0%) | 234.3 (+45.3%) | 141.6 (−12.2%) |
| the libc corpus | 100.2 | 113.4 (+13.1%) | 113.0 (+12.7%) | 80.9 (−19.3%) |
| long expressions | 151.5 | 228.0 (+50.5%) | 226.4 (+49.5%) | 134.4 (−11.3%) |
| small functions | 160.6 | 233.4 (+45.4%) | 230.9 (+43.8%) | 143.4 (−10.7%) |

To the IR (`--to-ir`, one process on SQLite), each run also checked the asm
operands, the initializers and the static data, built the checked program,
selected its reachable functions and lowered them to Tier 0's typed streams:
234 to 235 TSC ticks a token in every path. The whole path took 1,230.7 cycles a
token in A, 1,639.8 in B (+33.2%), 1,633.6 in C (+32.7%) and 1,103.1 in D
(−10.4%).

### Where the time went

The phases in TSC ticks per token (3.8 GHz, where the core runs near 5.5), on
SQLite:

| Phase | A | B | C | D |
| --- | ---: | ---: | ---: | ---: |
| the parse, with the sink for B and C | 86.2 | 141.0 | 140.9 | 48.4 |
| the reader's index and state | — | 57.7 | 57.7 | — |
| the rest of `check_input` | 289.0 | 288.8 | 288.9 | 251.5 |
| the expressions' walk | 138.3 | 312.5 | 302.0 | 138.9 |
| `check_graph_run` | 97.7 | 98.1 | 98.0 | 97.6 |
| total | 611.3 | 898.1 | 887.6 | 536.4 |

And on the libc corpus, whose headers hold few expressions (24,764 among 3.87
million tokens):

| Phase | A | B | C | D |
| --- | ---: | ---: | ---: | ---: |
| the parse, with the sink for B and C | 96.4 | 115.6 | 114.6 | 69.8 |
| the reader's index and state | — | 17.8 | 17.7 | — |
| the rest of `check_input` | 254.3 | 256.5 | 256.8 | 207.2 |
| the expressions' walk | 24.4 | 35.3 | 34.5 | 24.2 |
| `check_graph_run` | 5.1 | 5.2 | 5.2 | 5.0 |
| total | 380.1 | 430.4 | 428.8 | 306.4 |

The generated corpora split the same way: the sink added 68 ticks a token to
long expressions' parse and 45 to small functions', the index 60 and 59, and the
walk 160 and 173.

On SQLite the sink added 55 ticks a token to the scalar parse (64%), and the
reader's index, built from the tree's parents and postorder, 58 more. The prefix
walk cost more than twice the postorder walk: its cursor took 1.92 events a
token, each through the reader's own checks (its visit ledger, owners and
kinds), 85 to 91 ticks an event; the operand check against the tree, which
`test-fragments` runs, was off. The rest of `check_input` and the graph's
checking were the same in A, B and C, as they should be: they read the same
tree and graph. D's tree is the same as A's, yet `check_input` read it 13.0%
faster on SQLite and 18.5% faster on libc: the two parsers leave equal columns
in different places in memory and in the caches, which this measurement didn't
separate.

No reader of these fragments beside this checker's tree walk could have rescued
B or C. The sink and the index alone cost 112.5 ticks a token on SQLite, 18.4%
of A's whole path, and 37.0 on libc, 9.7%, and the walk builds every token's
nodes either way: the fragments replace only the gathering of an operator's
operands, a few loads and stores of the children's values. Even a walk that cost
nothing would leave B and C slower than A on libc, at 392.8 and 391.7 ticks a
token against 380.1, where #107's rule needs both real corpora 3% faster.

### Counters

In dispatch slots, eight a cycle, A took 7,068 a token on SQLite: 2,081 that
dispatched ops that retired, 1,658 the frontend left empty and 2,746 the backend
stalled. B's 10,418 added 807, 530 and 1,851 to those, so most of B's and C's
extra time was the backend waiting. The memory counters say on what: demand
fills from DRAM rose from 1.83 a token in A to 3.06 in B and 3.10 in C (from L2,
4.43 to 5.73), as the sink's per-token records, the index and the walk's state
streamed through memory much larger than the caches (the table below).
Branches were not the cause: 5.2 mispredictions a token in A against 6.4 in B
and 6.3 in C on SQLite (4.4 in D), and 2.1 against 2.5 and 2.4 on libc, while B
executed 42% more branches on SQLite and 17% more on libc. The store-to-load
interlocks (8.3 a token in A, 13.0 in B), the dispatch slots the power cap took
(11.7 and 19.3) and the FP schedulers' stalls (0.6 in both; 10.8 in D, whose
kernels are AVX-512) were small beside the cycles.

Neither of the sink's options changed the outcome. With the interleave's scalar
twin decoding C's windows, C took 45.8% more cycles than A in its process; with
48-byte payloads B and C took 44.8% and 45.4% more, their hot records 4.2 bytes
a token instead of 2.2.

### Memory

On SQLite, A keeps the tree's roles, parents and depths and the postorder with
its subtree sizes: 17 bytes a token. B and C kept all of that and, besides:

| Storage | Bytes per token |
| --- | ---: |
| the sink's per-token syntax record, kinds and expressions | 74.8 |
| its hot records (the 16-byte payload), used | 2.2 |
| its identities of the positions used | 1.9 |
| the reader's index, kept for the walk | 25.0 |
| the index's temporary storage (peak, 9.2 MB) | 13.0 |
| the walk's state (cursors, frames, B's prefix array), allocated per token | 93 to 103 |

The sink's arena took 390 bytes a token, most of them the identity column's
capacity (256 bytes for each possible record), reserved and never touched.
Fragments per expression were 0.896, with 11.2% of a record's 64 positions and
21.8% of its 16 operator bytes used; the deepest nesting of pending frames was
2,568 and of cursors 1.

### D, and a preorder from its parents

D was the fastest path on every corpus: the vector parser's parse takes 44%
less time than the scalar one on SQLite (48.4 ticks a token against 86.2) and
28% less on libc (69.8 against 96.4), and the compiler uses it by default.
#107's D also allowed a preorder by a counting sort of the vector path's
parents, for the prefix reader; it wasn't built. The fragment reader reads
fragment kinds and operator tokens, which a preorder of the tree doesn't have,
and a prefix walk with pending frames costs more than the postorder walk over
the same tree (B and C's walks above), after a sort that costs what the
postorder's does, so it can't beat D's postorder either.

## The decision

#107's rule adopts B or C only if it is faster than A by more than the
run-to-run spread, and by at least 3% on the parse and consumption of both real
corpora, with no increase in retained memory that a later stage pays for, and
with identical results. The results were identical as far as they were
compared, up to the checked program, but B and C took 46% to 47% more cycles
than A on SQLite and 14% more on libc, each path's processes within 0.6% of each
other, and kept more than 100 bytes a token on top of the tree. A stays: the
parser keeps emitting its tree and postorder, which the vector parser (D) builds
fastest.

The fragment sink and its builder, the readers, the interleave kernel, the
consumer, their tests and the benchmark were removed with the fragments' unit:
`test-fragments` and `test-fragment-interleave` left the 9700X job, and the
build lost a translation unit (with the consumer it took 27 s on the 9700X, the
build's third longest).

#107's rule would keep the idea here for a later stage, such as an incremental
compiler's cache, had it given a smaller retained representation at equal
speed. The fragments' own hot storage was small, 4.1 bytes a token on SQLite
(the records and the identities of the operator positions), against the
postorder's 8 (the postorder and the sizes), but it holds only expressions,
without the tree the checker reads, and it was never at equal speed. The idea
would come back with a checker that needs no token the fragments drop (no token
node on a group's closer, no region on a statement expression's, designators
and builtins in fragment terms), so that a parser could skip the tree for
expressions and the fragments would replace work rather than add it.

### What the decision covers

The decision is about the design measured: fragments built as a second output of
the scalar parser, beside its tree, and read beside the checker's tree walk. For
that design, the cost exceeded anything a reader could recover. It doesn't
measure #106's one-pass prefix-driven checker, or a parser that skips the tree
for expressions: both would need the checker's semantic actions written for
fragment kinds, and both stay untested; the paragraph before this section says
what would reopen them, and
[#544](https://github.com/buster14a/buster/issues/544) tracks them. Nor did the
identity criterion of #106 pass in full: the results were identical up to the
checked program, while the lowered IR and the `-c` objects weren't compared.
Keeping A didn't depend on them, since #107's rule asks identical results only
of a variant fast enough to adopt.

## Reproducing

Commit [`6c5b21a1`](https://github.com/buster14a/buster/commit/6c5b21a18eda1dd620322d889295980178a97999)
has the experiment whole. From a checkout of it, on the 9700X, each command
under the shared lock:

```sh
./build.sh
./build/compiler test-fragments
./build/compiler test-fragments --no-fixtures --checker-fixtures
./build/compiler test-fragments --no-fixtures sqlite3.c
./build/compiler test-fragments --no-fixtures --files-from libc.txt
taskset -c 2 ./build/compiler benchmark-parser --expressions all --passes 21 --counters pipeline sqlite3.c
taskset -c 2 ./build/compiler benchmark-parser --expressions all --passes 21 --counters pipeline --files-from libc.txt
```

`--expressions` takes paths by name (`postorder`, `prefix`, `fragments`,
`sorted`, or `all`), `--counters` one set (`pipeline`, `branches`, `memory` or
`stalls`), `--to-ir` adds the checked program and its lowering to each run, and
`--payload 16|32|48` and `--interleave vector|scalar` choose the sink's records
and the window decoder. [The record](https://github.com/buster14a/buster-performance/tree/cbf9a269b02e21705c21c173f914d36835e28f30/docs/measurements/parser-fragments-107-2026-10-26) has the scripts as they ran,
the inputs' identities, the generator of the synthetic corpora and every log.

## Earlier results

The builder's storage on the frozen corpora (#315's qualification, producer
`de9c34bd`, on the 9700X): SQLite (one file, 706,586 parser tokens, 53,349
expressions) and the libc corpus (733 files, 3,867,223 tokens, 24,764
expressions), at each operator payload. Position occupancy is of the 64
positions of a record, operator occupancy of the payload; a virtual singleton
(an expression of one leaf) has no record. Peaks are one unit's.

| Corpus | Operator payload | Fragments/expression | Position occupancy | Operator occupancy | Operator bytes copied/reduction | Next-field bytes written/reduction | Peak live hot bytes |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| SQLite | 16 | 0.895987 | 0.112381 | 0.217869 | 1.333084 | 5.947366 | 1,529,632 |
| SQLite | 32 | 0.876736 | 0.114849 | 0.111326 | 1.682394 | 5.934927 | 2,245,152 |
| SQLite | 48 | 0.873887 | 0.115223 | 0.074459 | 1.932311 | 5.932525 | 2,983,808 |
| libc | 16 | 0.433815 | 0.138981 | 0.277937 | 1.414002 | 5.073985 | 16,864 |
| libc | 32 | 0.400380 | 0.150588 | 0.150574 | 2.126465 | 5.052193 | 25,104 |
| libc | 48 | 0.396665 | 0.151998 | 0.101323 | 2.610765 | 5.047708 | 33,472 |

The 16-byte payload used half the 48-byte one's peak live hot storage for 3% to
9% more records, which is why the measurement used it. The interleave kernel
passed 274,566 routing cases, 260 protected-page cases and 1,649,144 checks
against its scalar twin under AddressSanitizer and UBSan on the 9700X, and the
readers gave the tree's events on every expression of both corpora at every
payload ([the qualification report](https://github.com/buster14a/buster/pull/315#issuecomment-6364241816)).

## Sources and provenance

The operator-fragment construction is described in
[Niles Salter's construction discussion](https://github.com/Validark/Accelerated-Zig-Parser/issues/3#issuecomment-1723146177)
and the [arity followup](https://github.com/Validark/Accelerated-Zig-Parser/issues/3#issuecomment-1728669525).
[Carbon's parser design](https://github.com/carbon-language/carbon-lang/blob/trunk/toolchain/docs/parse.md#bracketing-inside-the-tree)
explains the postorder context-bracketing alternative. These were design
references. The experiment's C implementation and its tests were original
project material; no historical Zig implementation or external fixture was
copied.
