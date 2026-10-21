# The C parser

`src/buster/compiler/c_parser.c` parses one translation unit (#127): the
preprocessor's token stream (`c_preprocessor.c`) to a tree, with diagnostics for
what C's grammar rejects. It is the design spike #186 chose, built in full: the
spike's kernels in AVX-512 with their scalar twins, the scalar parser as the
fallback, oracle and home of the diagnostics, and a pass of checks the spike
didn't have, which lets the vector path take only code the scalar parser would
accept with the same tree. `compiler file.c` runs the preprocessor and the
parser; nothing is written yet.

On the 9700X the vector path parses `sqlite3.c` (707,153 tokens) in 3.48 ms,
27.1 cycles a token, 2.37 times as fast as the scalar parser (64.2), and the
libc corpus at 21.4 cycles a token, 2.29 times as fast (48.9); with the
postorder and its subtree sizes, 41.0 and 32.7. Both paths build the same tree
on every file of `sqlite3.c`, the libc corpus, Lua and this repository, which
they accept as clang does, and agree with clang on 4,300 mutations of them.
"Measurements" has the tables; #115's budget for the parser, 2 ms for
`sqlite3.c`, isn't met yet ("What's left").

## What it reads and what it gives

The input (`parse_tokens_from_output`, with a scalar twin) is the
preprocessor's output, its keywords already resolved by language mode. The
parser's kinds (`ParseKind`, 83) keep the lexer's punctuators and merge the
rest by what the grammar needs: any number, any character constant, any string
literal, and keywords by what they mean (`int` and `long` parse alike). A token
also keeps its `IdentId`, the preprocessor token it came from (for
diagnostics) and its flags. Pragma lines leave the stream as events at their
position, and `::` splits into two colons, as C's grammar has no `::`.

The output, in `Parser`:

- **The tree**: for every token, punctuation included, its parent (the token
  whose node is its parent, or `BUSTER_PARSE_ROOT` for the node of a top-level
  declaration), its role (`ParseRole`, 84: what the token is to the grammar)
  and its node depth.
- **The postorder**, for a unit without errors: the token at each position, and
  each position's subtree size (the subtree is the `sizes[k]` positions ending
  at `k`), what #133's single-pass type checker reads.
- **Diagnostics**: clang's message at clang's place for the first error of
  each top-level declaration, and `fatal` for what stopped the parse where no
  diagnostic says it (out of memory, or past the parser's token limit).
- **Statistics**: the share of top-level declarations and tokens the vector
  path left to the scalar parser.

What the tree guarantees, which the later passes may rely on:

- every subtree is a run of tokens, so a node's subtree is
  `[start(v), end(v)]` in source order, and its postorder position is
  `C(v) + end(v) - v`, where `C(v)` counts the tokens whose subtree ends before
  `v`;
- a node's children, in source order, are its operands in order;
- every parent is another token or the root, at no greater node depth; every
  opener's parent is its closer, after it; following parents always reaches
  the root;
- both paths give the same tree, the same parents, node depths and roles, on
  every token of every unit without errors (a function declarator's
  parentheses and a call's compare as one role: they have the same tree).

## The tree

C's grammar is written as operator precedence, so that nearest-smaller-value
searches build the tree (Bar-On and Vishkin, 1985):

- Each token has a role, which gives it a **level** (higher binds tighter), a
  left and a right **threshold** and a **rule**. Expressions use C's
  precedence. Declarations parse as the expressions they mirror, with the
  specifiers as right-associative prefixes below the comma (`int *p, q` is
  `int` applied to `*p, q`). Statements sit below everything a declaration or
  an expression contains.
- **Brackets**: `( ) [ ] { }`, `? :` and `case`/`default ... :`. A group's
  closer is its node, and its opener a leaf child of the closer. The closer's
  role gives the group's level: a call's `)` is a postfix operator whose left
  operand is the callee, a cast's `)` a prefix operator, a grouping's `)` an
  operand, a struct body's `}` a specifier.
- **Keys** are (node depth, level), depth first. L(i) is the nearest token
  before i whose key is below i's left threshold; R(i) the nearest after it
  below i's right threshold. Strict or inclusive searches give each level its
  associativity. An opener's key is (its depth, `PARSE_LEVEL_OPENER`): no
  search at its own depth sees it, and every search from inside its group
  stops at it.
- The **parent** is the one of L and R with the greater key, except that an
  opener, a terminator or a statement head found as L loses to any R. Some
  roles take L (a qualifier after `*`, a string literal after a string
  literal), R (`if`, `struct`, `__attribute__`: keywords before what they
  introduce) or their closer (openers).
- **Statements**: a statement's node is its last token (`;`, or a compound
  statement's `}`), but a statement with a head (if, while, for, switch, a
  label, case, default, else, do) has the head as its node. A statement stack
  decides which head owns which statement, outside the levels.

So `if (a) x = b * c; else y = -z;` is:

```text
) [the if's head] <- if, (, a, ;1, else
;1 <- =1 <- x, *   (* <- b, c)
else <- ;2 <- =2 <- y, -   (- <- z)
```

## The scalar parser

`parse_scalar` is an explicit-state parser without recursion: grammar modes on
a frame stack (declarations, declarators, expressions, initializers,
attributes, asm, `_Generic` and the builtins that take a type), the statement
stack, and two precedence stacks, the L candidates and the tokens waiting for
their R, which resolve as tokens arrive, as a shunting-yard parser's operator
stack does. Typedef names are bound by scope in a meaning table with an undo
log. Nesting is bounded by memory, not by the native stack: 100,000 levels of
every kind of group parse.

It rejects what C's grammar rejects, at the token clang names, with clang's
message (60 messages, `BUSTER_PARSE_MESSAGES`), and after an error skips to the
end of the top-level declaration and goes on. It reports one error per
top-level declaration, the first; clang may report more. Its places follow
clang's rules, among them: a missing `;` after the token before it; an
unbalanced closer at the end of the file; after a tag's body, the tokens clang's
`isValidAfterTypeSpecifier` allows, and none on a new line.

It is the fallback for what the vector path flags, the oracle every kernel is
tested against, and the parser of a unit with errors: the vector path sends the
whole unit to it when the fallback finds one, so that its diagnostics are the
scalar parser's alone. `--scalar` (with the preprocessor's scalar twins) runs
it alone.

## The vector path

`parse_vector` runs these passes over the unit's tokens, each in AVX-512 with
an exact scalar twin (the twins together are `parse_twins`):

1. **K1, brackets.** Kinds to +1, -1 or 0 with `vpermi2b`, a prefix sum in byte
   lanes, and each closer's opener by a nearest-smaller search within a block
   and a window of the open openers across blocks. It also hands K2 a view of
   each block: the enclosing openers, the depths and the openers still open.
   A unit whose brackets don't balance goes to the scalar parser.
2. **K3, type names.** A scalar pass in source order over the typedef
   declarations alone, found by a vector search for the keyword, then every
   identifier classified against the set of names they declare, with mask
   loops. A typedef name used before its first declarator, or declared in a
   block, is flagged.
3. **K2, roles.** Each token's role from its kind, its neighbors' kinds, the
   role before it and its group's role, in rounds to a fixpoint from refined
   guesses (at most 8; the block loop runs one), with what the kinds decide
   from a pass before the rounds and the group stacks in registers. It writes
   each block's statement contexts for K5.
4. **K1's second part.** `?`, `case` and `default` matched to their colons per
   depth (a colon without an opener is a label), then the node depths by a
   prefix sum.
5. **K4, parents.** L by a forward pass and R by a backward one, in blocks of
   64 byte keys relative to the block's lowest node depth (a block whose depths
   span more runs as two blocks of 16-bit keys): every block's in-block work
   first, then the passes, with the candidate stack's top in registers. Nesting
   past 2,047 goes to the scalar parser.
6. **K5, statements.** The owners from a compress and an expand of the
   statement events' positions, the blocks' owners from a stack without
   branches, and else, do and while from a sparse loop. A unit it can't decide
   alone (a `do` without its `while`) goes to its twin, the statement stack.
7. **K6, the checks** (below).
8. **The fallback** (below).

"Recommendation for #127" in #186's report
([`docs/data-parallel-parsing.md`](https://github.com/buster14a/buster/blob/e8960316b9c414c0fa340f1d7c0ee54a695bfc08/docs/data-parallel-parsing.md)
on branch `claude/spike-186-k2-spills`) has each kernel's design and the
measurements behind it. The kernels here are those, ported, with the changes
the checks needed (an attribute after `*` is a qualifier, so that
`int * __attribute__((x)) p` keeps every subtree a run) and the fixes their
tests found (K1's imbalance in its out-of-line blocks, K5's do-while).

### K6, the checks

The kernels build the tree C's grammar gives well-formed code; K6 checks that
the code is well-formed, so that every top-level declaration the vector path
keeps is one the scalar parser accepts with the same tree. Each token gets:

- a **class**: its role, refined by its kind, its typed state or its name
  where what may follow it differs (117 classes: a constant, a string, a type
  keyword, a typedef name, `*`, `sizeof`, `goto`, a label's name, ...);
- a **mode**: its group's type (a file, a block, a struct or enum body, a
  parameter list, a call's arguments, a declarator in parentheses, a
  subscript, an array declarator, an initializer, `for`'s clauses, an
  attribute, a builtin's arguments, 27 in all) and its part of its item (its
  start, a declaration's specifiers and declarators, an initializer, a
  bit-field's width, a statement): 49 modes.

A token passes if its class may follow the class before it (`follows`, 128 by
128 classes), may appear in its mode (`allows`), may end its group when the
next token closes it (`ends`), and doesn't skip the name a declarator needs.
Besides: a label's name must start its statement; `for`'s parentheses hold two
`;`; a builtin has its number of arguments; only an array declarator's `[ ]`
may be empty; a statement expression's block is the whole of its parentheses,
in a function; a closer must match its opener's kind; after a tag's body come
only the tokens clang allows there; nothing postfix follows `&&label`; and a `*`
outside declarators is followed by an operand. A token that fails is flagged,
with the reasons, which `test-parser --explain` tallies.

The twin (`parse_check_scalar`) keeps a frame per node depth. The AVX-512
kernel (`parse_check_avx512`) works in blocks of 64 tokens whose depths span at
most 63, with a byte key per token: 4 times its depth above the block's lowest,
plus a rank (0 for an item's start, 1 for an `=`, `,` or bit-field `:`, 2
otherwise). One table of window minima serves
three nearest-smaller searches: the enclosing opener, the item's start and the
part's start. Group types come from a fixed point started from an expression
guess, in at most 16 rounds; a block that needs more, or spans more depths,
goes to the twin, and the twin's frames carry from block to block. The check
tables are built from the twin's own functions (`parse_check_vector_init`), so
the two can't disagree on a rule.

What the checks don't take, and so leave to the scalar parser wherever it is:
asm statements and file-scope asm (an asm label after a declarator passes),
`_Generic`, K&R parameter lists, storage
classes in parameters, C23's `[[...]]` attributes, a typedef name after an
`_Alignas(...)`, and qualifiers in a parameter's array declarator
(`char *const argv[__restrict]`).

### The fallback

The scalar parser parses each top-level declaration with a flagged token in
place (`parse_fallback_item`), with the file-scope typedef names declared
before it visible, and its tree joins the vector path's. The whole unit goes to
the scalar parser instead when the brackets don't balance, K5 can't decide its
statements, the nesting is too deep, or a fallback declaration has an error,
runs out of memory or declares a file-scope typedef name again as an ordinary
identifier, which changes how the rest of the unit parses.

On the corpora, the fallback takes these declarations, all valid code (the
benchmark's counts; `test-parser --explain` says why for each):

| Unit | Declarations | Tokens | Share | Why |
| --- | ---: | ---: | ---: | --- |
| `sqlite3.c` | 7 | 10,210 | 1.44% | `_Generic`, from glibc's `strchr` and `memchr` macros; typedef names declared in a block (`void_function`, `sqlite3LocaltimeType`) |
| libc corpus (733 files) | 132 | 7,500 | 0.19% | asm statements; `__restrict` in glibc's parameters' array declarators; block-scope typedef names (musl's string functions) |
| `main.c` | 38 | 19,972 | 1.23% | asm statements (clang's intrinsics headers, the compiler's own); `_Alignas(64)` before a typedef name; `__restrict` in glibc's `spawn.h`; a block's variable named `va_list` |

## The postorder

`parse_postorder_sorted` derives the postorder from the parents in passes
without a branch on the tree: each token's subtree end, from its last child
after it, backward; how many subtrees end at each token; an exclusive prefix
sum; and each token's position, `C(v) + end(v) - v`. With the sizes, each
token's subtree start comes from its first child before it, forward, and its
position gets `end(v) - start(v) + 1`. `parse_postorder` is the walk the spike
measured the passes against (each token with no child after it, then the climb
through the ancestors it ends), without the sizes: it mispredicts where the tree
is irregular, as on `sqlite3.c`, and costs less where its branches predict, as
on libc ("Measurements"). The benchmark times both.

## The driver

`compiler [-fsyntax-only] [-E's options] [--scalar] file.c`
(`run_parse_command`) preprocesses the file with -E's options and parses it,
printing the preprocessor's and the parser's diagnostics as clang does
(`file:line:column: error: message`), and exits with status 0 for a unit
without errors and 1 otherwise. A file that can't be read is reported with the
reason (missing, a directory, past the preprocessor's size limit, not a regular
file, out of memory). Every reservation that fails is reported as a fatal
error, and the reservations stop at the first that fails and are released. The
compiler's own sources need clang's predefines for their target options
(`--predefines`, from `clang -dM -E -march=native ...`), as for -E.

## Correctness

`test-parser` (AGENTS.md, "Parser tests") compares the two paths on every unit
it reads, the tree's invariants and postorder, and each kernel with its twin,
on snippets checked against clang, generated inputs, the driver's cases and
failures, corpora, and mutations of them checked against clang.

On the 9700X, at `bfeee15`, with `--clang`:

- the tests without files: the 94 snippets, the four fixtures, 2,000 random
  streams, the snippets at every block alignment (6,016 inputs), the seven
  nestings 100,000 levels deep, and the driver's cases and its failures
  (3.8 s);
- `sqlite3.c` (3.53.4, 707,153 tokens, 1.1 s), the 733 files of the libc corpus
  that clang accepts (3,869,223 tokens, 8.4 s) and the repository's 18
  translation units (11,705,844 tokens, 6.0 s): every file accepted, as clang
  accepts it, with both paths' trees and every kernel's twin agreeing.

Locally, on a Ryzen 9 7940HS: Lua 5.4.8's 34 files (572,263 tokens) with
`--clang` as well, the fixtures, and mutations of the corpora, checked against
clang and through both paths, none with a mismatch:

| Mutations checked against clang | Lua 5.4.8 | `sqlite3.c` | libc |
| --- | ---: | ---: | ---: |
| Copies | 1,000 | 300 | 3,000 |
| clang accepts them (so must the parser) | 204 | 25 | 688 |
| clang's first error is a parse issue (the parser must fail on its line) | 603 | 243 | 1,771 |
| clang's first error is another kind (either is right) | 193 | 32 | 541 |
| Mismatches | 0 | 0 | 0 |

Through both paths, without clang: 20,000 copies of Lua's files, 20,000 of
libc's and 1,000 of `sqlite3.c`, of which the scalar parser rejected 14,840,
14,167 and 861, with no difference between the paths in any.

The kernels' checks put every array in an arena followed by 4 GB and its own
size of address space that may not be touched, so that a load past an array, or
through an index that wrapped below 0, faults. And `main.c` built alone with
`-fsanitize=address,undefined` (the other units' benchmarks stubbed out) runs
the tests without files, Lua, `sqlite3.c`, the libc corpus and 3,000 mutations
each of Lua and libc without a report.

## Measurements

Measured on 2026-10-22 on the Ryzen 7 9700X (`benchpress`), at `bfeee15`,
with Clang 23.1.1 and `build.sh`'s `-O3 -march=native`, each run under the
shared lock by itself and pinned to CPU 2. `benchmark-parser` reports the
medians of 21 interleaved passes after a warm-up, per token, from tokens the
preprocessor made beforehand (AGENTS.md, "Parser benchmark"). The parsers'
fastest and slowest passes (the scalar parser's, the twins' and the vector
path's) are within 1.1% of the median on `sqlite3.c`, 2.1% on libc and 3.7% on
`main.c`; the input's conversion, a fraction of a millisecond, varies more (its
slowest pass on `main.c` took twice its median). The inputs: `sqlite3.c` 3.53.4 with the
system's headers (707,153 tokens), the 733 files of the libc corpus that clang
accepts (Zig 0.14.1's `lib/libc`, as #186 chose them; 3,869,223 tokens), and
`main.c` with clang's predefines for `-march=native` (1,623,295 tokens).

| Whole path | `sqlite3.c` ms | cycles/token | instructions/token | libc ms | cycles/token | instructions/token | `main.c` ms | cycles/token | instructions/token |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| The input's conversion | 0.13 | 1.0 | 1.5 | 1.29 | 1.8 | 2.2 | 0.49 | 1.7 | 1.5 |
| Its scalar twin | 1.69 | 13.2 | 40.7 | 5.18 | 7.4 | 41.7 | 3.08 | 10.5 | 40.6 |
| Scalar parser | 8.21 | 64.2 | 288.5 | 34.45 | 48.9 | 269.5 | 16.94 | 57.5 | 291.7 |
| Kernels' scalar twins | 20.80 | 162.6 | 641.2 | 79.31 | 112.7 | 634.8 | 39.72 | 134.7 | 637.2 |
| Vector path | 3.48 | 27.1 | 59.3 | 14.94 | 21.4 | 54.6 | 8.71 | 29.6 | 57.6 |
| Postorder, passes, with sizes | 1.78 | 13.9 | 50.3 | 7.92 | 11.3 | 50.4 | 4.61 | 15.7 | 50.3 |
| Postorder, walk, without sizes | 1.91 | 14.9 | 19.0 | 3.84 | 5.5 | 18.7 | 3.18 | 10.8 | 19.3 |

- **The vector path is 2.37 times as fast as the scalar parser on `sqlite3.c`,
  2.29 times on libc and 1.94 times on `main.c`**, at IPC 2.2, 2.6 and 1.9,
  backend-bound (52% to 59% of dispatch slots), against the scalar parser's
  4.5 to 5.5. It mispredicts 0.139 branches a token on `sqlite3.c` and 0.034
  on libc, the scalar parser 0.510 and 0.122.
- **The scalar parser costs more than the spike's**, 64.2 cycles a token on
  `sqlite3.c` against 44.0, and 288 instructions a token against 190: it takes
  the whole grammar now, with clang's diagnostics and recovery, where the
  spike's parsed what its corpora held.
- **The vector path costs more than the spike's**, 27.1 cycles a token on
  `sqlite3.c` against 16.7, and 21.4 on libc against 11.7: K6, which the spike
  didn't have, takes 8.6 and 8.9, and K3's classification, which now also
  flags a typedef name used before its first declarator, 1.2 and 0.7 more.
- **The postorder's passes with the subtree sizes cost 11.3 to 15.7 cycles a
  token**, twice the spike's passes without sizes (7.1 and 6.0): the sizes take
  two passes more, the first children forward and the starts. Without sizes the
  walk costs 5.5 on libc and 10.8 on `main.c`, where its branches predict well,
  and 14.9 on `sqlite3.c`, where it mispredicts 0.43 branches a token.
- The kernels' twins are tests only: they cost 2.3 to 2.5 times the scalar
  parser.

Stage by stage, in cycles a token, with the spike's third follow-up's for the
kernels it had:

| Stage | `sqlite3.c` | spike | libc | spike | `main.c` |
| --- | ---: | ---: | ---: | ---: | ---: |
| K1 brackets | 2.27 | 2.35 | 1.85 | 1.86 | 2.22 |
| K3 typedef pass | 0.11 | 0.10 | 0.53 | 0.49 | 0.20 |
| K3 classification | 2.64 | 1.43 | 1.89 | 1.20 | 2.79 |
| K2 roles | 3.90 | 3.85 | 3.53 | 3.47 | 4.97 |
| K1's second part | 0.86 | 0.78 | 0.61 | 0.54 | 1.46 |
| K4 parents | 4.49 | 4.47 | 3.92 | 3.95 | 4.80 |
| K5 statements | 1.85 | 1.81 | 0.22 | 0.20 | 1.36 |
| K6 checks | 8.60 | | 8.88 | | 8.95 |
| Fallback | 1.47 | 1.19 | 0.42 | 0.35 | 1.25 |
| Vector path | 27.14 | 16.67 | 21.37 | 11.72 | 29.65 |

**K6** is now the largest stage, at IPC 1.7 to 2.0 with 65% to 67% of its
dispatch slots backend-bound: each block's fixed point of group types and its
three nearest-smaller searches, as K4's blocks were before the spike's
follow-ups. Its twin costs 43 to 51 cycles a token.

## What's left

- #115's budget for the parser, 2 ms for `sqlite3.c` (about 16 cycles a token):
  the vector path takes 3.48 ms, and with the input's conversion and the
  postorder 5.38 ms. K6 (8.6 cycles a token) and the postorder's sizes (the
  passes' 13.9 against the walk's 14.9 without sizes) come first, then K3's
  classification, and then the spike's leads: K2's round and K4's window search.
- The checks' misses on valid code, which cost only the fallback's time:
  qualifiers in a parameter's array declarator (glibc's headers), a typedef
  name after `_Alignas(...)` (K2 takes it for a declarator's name), and asm
  statements, whose operand lists the checks don't model.
- The parser's diagnostics don't show the preprocessor's expansion notes yet
  ("expanded from macro"): #229's `pp_diagnose_output` renders them for an
  output token, which each diagnostic's token gives.
- Block-scope typedef names, which K3 flags: exact per-identifier binding
  stacks in K3 (#128) would keep those declarations on the vector path.
- Multicore (#149, #151): top-level declarations start with empty stacks at
  depth 0, so a unit splits there, with only the typedef pass ordered.
- `test-parser` builds a fresh intern table, preprocessor and parser for every
  unit it parses, and its generated inputs' units cost it about 2 million page
  faults, most of its time.
