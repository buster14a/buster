# The C parser

`src/buster/compiler/c_parser.c` parses one translation unit (#127): the
preprocessor's token stream (`c_preprocessor.c`) to a tree, with diagnostics for
what C's grammar rejects. It is the design spike #186 chose, built in full: the
spike's kernels in AVX-512 with their scalar twins, the scalar parser as the
fallback, oracle and home of the diagnostics, and a pass of checks the spike
didn't have, which lets the vector path take only code the scalar parser would
accept with the same tree. `compiler file.c` runs the preprocessor and the
parser; nothing is written yet.

On the 9700X the vector path parses `sqlite3.c` (707,153 tokens) in 3.54 ms,
27.6 cycles a token, 2.37 times as fast as the scalar parser (65.4), and the
libc corpus at 21.5 cycles a token, 2.37 times as fast (50.6); with the
postorder and its subtree sizes, 41.1 and 32.9. Both paths build the same tree
on every file of `sqlite3.c`, the libc corpus, Lua and this repository, which
they accept as clang does, and agree with clang on 22,200 mutations of them.
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
  at `k`), what #133's single-pass type checker reads. `ParseOptions.tree_only`
  leaves these two pointers null; the zero-initialized default produces them.
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
log. A declarator's name is bound once its declarator is complete (at its `=`,
`,`, `;`, body or list's end), and an enumeration constant once its enumerator
is (at its `,` or `}`), where C begins their scopes: in
`typedef int T; void f(void) { int T[sizeof((T)0)]; }` and in
`enum { T = sizeof((T)0) }` the cast still sees the typedef. What the
declarator makes the name is the derivation it applies first, going out from
the name past groups without a `*`, as clang's
`Declarator::isFunctionDeclarator` decides it: `void (f(void)) {}` and
`int (*g(void))[3] {...}` are definitions, `int (*p)(int) {` isn't, and only a
declaration's one declarator may have a body. That function's parameters' scope
ends at their list's `)`, as clang's prototype scope does, so that the rest of
the declarator and its attributes see what they hid
(`int (*f(int T))[sizeof((T)0)] {...}`); the list keeps what it bound, which a
body binds again, after the function's own name. A K&R definition's identifier
list is the name's own, through the declarator's groups and whatever suffixes
its return type has (`int (f(x)) int x; {...}`, `int (*g(y))[3] int y; {...}`),
in C17 and before (#154), and clang reads one only
where its first identifier is followed by `,` or `)`; its parameter
declarations, as clang's `ParseKNRParamDeclarations` reads them, are in a scope
of their own, which the body ends, and each declares one of the list's names,
once, with `register` at most and no initializer. Elsewhere an identifier
that isn't a type name, where a type would be, is what clang's
`ParseImplicitInt` makes it: the declarator's name with an implicit int if what
follows may follow a name, and else an unknown type name, as a parameter's
always is in C23; and at file scope a declarator without specifiers (`(x);`,
`*p;`) has an implicit int too. Selection and iteration statements are blocks, and so is
each of their substatements (C 6.8.4p3, 6.8.5p5): what a condition declares
ends with its statement, and what a then-statement or a do's body declares ends
with it. `__builtin_offsetof`'s member designator and `_Static_assert`'s
message have grammars of their own, as clang reads them: members' names,
whatever the ordinary names mean, `.` and subscripts; and string literals. So
do an asm statement's sections, as clang's `ParseAsmStatement` reads them: the
template, the outputs and inputs (each a symbolic name in `[...]`, a constraint
and its operand in parentheses), the clobbers and asm goto's labels, all of
them string literals but the operands and the names, which are names whatever
the ordinary names mean, as `&&label`'s is, and nothing continues them
(`asm("" + 1)` is an error); asm goto takes all four colons and a label at
least, a top-level asm block only its template, and an asm label after a
declarator only string literals. A C23 attribute list's items are a name,
perhaps in a scope (`gnu::unused`) and with an argument clause, a balanced run
of tokens, separated by commas; its items and a GNU attribute list's may be
empty, the last one too.

Each frame keeps the index of the frame its declarators' names belong to and
what a comma at its top level is, so that no token costs more for what is open
around it: 320,000 nested parentheses, ternaries with commas in them, or
attributes in a row, parse in time linear in their count. Nesting is bounded by
memory, not by the native stack: 100,000 levels of every kind of group parse.

It rejects what C's grammar rejects, at the token clang names, with clang's
message (76 messages, `BUSTER_PARSE_MESSAGES`), and after an error skips to the
end of the top-level declaration and goes on. It reports one syntax error per
top-level declaration, the first, after the errors clang gives without
changing how it parses (a missing type specifier, an unknown type name); clang
may report more. Its places follow
clang's rules, among them: a missing `;` after the token before it; an
unbalanced closer at the end of the file; after a tag's body, the tokens clang's
`isValidAfterTypeSpecifier` allows (a `:` before a bit-field's width or a
`_Generic` association's value), and none on a new line; a missing `,` or `]`
in a C23 attribute list after the token before it.

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
   identifier classified with mask loops and dense per-name heads. Sparse
   activation records distinguish names before, inside and after each block,
   with visibility beginning after the complete declarator. Nested and sibling
   aliases restore their previous heads. An ordinary name hiding a typedef
   still goes to the exact scalar fallback when K2 finds its declarator.
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
  where what may follow it differs (119 classes: a constant, a string, a type
  keyword, a typedef name, `*`, `sizeof`, `goto`, a label's name, `->`, which
  designators don't take, ...);
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

The class refinement now groups virtual kinds with identical results for all
seven refining roles. There are 28 distinct eight-byte rows (slot zero is
unused), addressed through a 128-byte kind-to-row map. A 256-byte joint table
replaces the former seven 128-byte lookups and their seven compare/blends on
every block. The typed and name-dependent refinements still follow it. The
initializer asserts the 32-row limit before filling the compact table.

This reduces the static tables by 640 bytes. It retains K6's existing
64-token input contract, scalar rules, inactive-lane handling and frame handoff;
it introduces no per-token metadata or extra pass. `test-parser` exhaustively
compares every role and virtual kind (including unused kind slots), with both
typed states and alternating typed lanes, against the scalar classifier.
The existing whole-kernel checks still exercise guarded tails and
transitions between vector and scalar blocks.

A function's body is checked apart (`parse_check_body`, the same in both): its
declaration is read back from the `{` to the item before it, and a declarator
group or a comma at the body's depth flags it, as the tables can't tell what the
declarator makes its name, nor see the comma. Bodies are few, so this costs a
short scan each.

What the checks don't take, and so leave to the scalar parser wherever it is:
asm statements and file-scope asm (an asm label after a declarator passes),
`_Generic`, K&R parameter lists, storage classes in parameters, C23's `[[...]]`
attributes, a typedef name after an `_Alignas(...)`, qualifiers in a parameter's
array declarator (`char *const argv[__restrict]`), and function definitions
whose declarator has a group (`void (*signal(int, void (*)(int)))(int) {`).

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
| `sqlite3.c` | 10 | 10,395 | 1.47% | `_Generic`, from glibc's `strchr` and `memchr` macros; typedef names declared in a block (`void_function`, `sqlite3LocaltimeType`); definitions whose declarator has a group (`sqlite3OsDlSym`) |
| libc corpus (733 files) | 142 | 7,827 | 0.20% | asm statements; `__restrict` in glibc's parameters' array declarators; block-scope typedef names (musl's string functions); definitions whose declarator has a group (`signal`'s kind) |
| `main.c` | 47 | 22,088 | 1.29% | asm statements (clang's intrinsics headers, the compiler's own); `_Alignas(64)` before a typedef name; `__restrict` in glibc's `spawn.h`; a block's variable named `va_list` |

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

The producer `parse_translation_unit` has two output contracts. By default it
produces the tree, diagnostics, postorder and subtree sizes described above.
With `ParseOptions.tree_only`, it produces the same token stream, tree and
diagnostics, and leaves `postorder` and `sizes` null. The tree has `count` byte
roles and `count` 32-bit parents and depths, in disjoint, 64-byte-aligned
columns with `BUSTER_PARSE_TOKEN_PADDING` entries of padding on either side.
The parser owns the columns and diagnostics until its next parse or release;
the full contract's traversal columns have `count` entries of `uint32_t`, with
the same alignment and padding. Rendering diagnostics also needs the originating
preprocessor and intern table. Empty
units and erroneous units follow the same success and diagnostic rules in both
modes. Each parse clears the optional output pointers before processing its
new unit.

`run_parse_command` requests the tree-only contract because its consumer,
`parse_print_diagnostics`, uses the tokens and diagnostics. This avoids building
two output columns and five temporary columns, about 28 bytes per token before
padding, plus their construction and teardown. The default API and
`benchmark-parser` still produce the traversal that #130/#133 will consume.
Code-generation throughput awaits those consumers' integration; the available
whole-process measurement is the current syntax-checking driver.

### Driver measurements, 2026-10-23

On the locked Ryzen 7 9700X, CPU 2 (performance governor, SMT sibling offline),
Clang 23.1.1's normal `build.sh` builds of main `1f1e116` and candidate `1b9312c`
ran in five alternating rounds. Each run measured 21 fresh compiler processes
after one warm-up process; the figures are medians of the five run medians.
Each run acquired and released `exclusive.sh` independently through `ab`.

| Complete driver workload | Main ms | Tree-only ms | Time reduction | Units/s, main → tree-only |
| --- | ---: | ---: | ---: | ---: |
| `sqlite3.c`, vector path | 24.465 | 21.973 | 10.18% | 40.88 → 45.51 |
| Frozen compiler `main.c`, vector path | 64.406 | 57.149 | 11.27% | 15.53 → 17.50 |
| `sqlite3.c`, `--scalar` | 42.520 | 40.071 | 5.76% | 23.52 → 24.96 |

The five run medians ranged from 24.438–24.538 and 21.959–22.015 ms for SQLite,
and 64.369–64.491 and 57.067–57.256 ms for the compiler unit. The improvement
exceeds the observed process spread and remained after merging current main.
These are startup, preprocessing, parsing and cleanup together, with
`-fsyntax-only`; the compiler unit also used `-march=znver5 -fwrapv
-funsigned-char` and its frozen `src` include directory. The common inputs were
SQLite SHA-256 `b1dd5d74ec7f29055a6684fa06fb3c2f6821c87dd38f9a458dfd2e8a1db28189`
and the source tree of `4be02a8`. Both versions read the same paths and bytes.

The initial fixed-period `perf record` profiles of `4be02a8` on the 9700X, with
source-line attribution, identified the postorder's child-index, subtree-bound
and output passes. Local `hot-loops` inspection was diagnostic. Inlining placed their stage samples under
`parse_benchmark_time` and their driver samples under `run_parse_command`.
Earlier experiments fused passes, used vector pointer jumping/gather/scatter,
or accumulated subtree sizes during a walk. None improved the whole driver;
one fused version improved the isolated stage while slowing the driver about
3%. A short macro-copy experiment reduced instructions without a useful time
gain. Those implementations were discarded. The retained change omits output
the actual driver consumer does not use.

The matched candidate passed `test-parser --clang` (230 snippets, 14,720 block
alignments, 100,000-level nesting, driver checks and allocation-failure cleanup),
the SQLite unit (706,586 tokens), and all 733 accepted libc corpus files
(3,867,223 tokens), with zero mismatches. Every checked unit also compared
tree-only and full parsing through both scalar and vector paths, including all
diagnostic fields, while the full paths retained the independent postorder and
subtree-size oracle. The call-graph check found no unallowed cycles.

Reproduction data, the fresh-process C timing helper, input manifests, profiles
and logs are retained at `~/buster-fast-bench/codex-opt297/`. The matched builds
are `current-base` and `current-final`; `driver-current-final.txt` records every
sample and `ab`'s rotating order. Run the helper beside each `build/compiler`,
and pass those helper executables as `ab`'s trees, with the common driver
arguments and the usual per-run lock.

## Correctness

`test-parser` (AGENTS.md, "Parser tests") compares the two paths on every unit
it reads, the tree's invariants and postorder, and each kernel with its twin,
on snippets checked against clang, generated inputs, the driver's cases and
failures, corpora, and mutations of them checked against clang.

On the 9700X, at `d59d035`, with `--clang`:

- the tests without files: the 202 snippets, the four fixtures, 2,000 random
  streams, the snippets at every block alignment (12,928 inputs), nine kinds of
  nesting 100,000 levels deep and a run of 100,000 attributes, and the driver's
  cases and its failures (6.8 s);
- `sqlite3.c` (3.53.4, 707,153 tokens, 1.8 s), the 733 files of the libc corpus
  that clang accepts (3,869,223 tokens, 9.1 s) and the repository's 21
  translation units (13,619,726 tokens, 7.6 s): every file accepted, as clang
  accepts it, with both paths' trees and every kernel's twin agreeing.

Locally, on a Ryzen 9 7940HS, at `a04f6c0` (the parser as at `d59d035`, whose
merge of main brought other modules' changes): Lua 5.4.8's 34 files (572,263
tokens) with `--clang` as well, the fixtures, and mutations of the corpora,
checked against clang and through both paths, none with a mismatch:

| Mutations checked against clang | Lua 5.4.8 | `sqlite3.c` | libc |
| --- | ---: | ---: | ---: |
| Copies (seeds) | 7,500 (32 to 36) | 1,200 (22, 33 to 35) | 13,500 (32 to 40) |
| clang accepts them (so must the parser) | 1,486 | 106 | 3,058 |
| clang's first error is a parse issue (the parser must fail on its line) | 4,739 | 945 | 8,028 |
| clang's first error is another kind (either is right) | 1,275 | 149 | 2,414 |
| Mismatches | 0 | 0 | 0 |

Through both paths, without clang: 5,000 copies of Lua's files, 5,000 of
libc's and 1,000 of `sqlite3.c`, of which the scalar parser rejected 3,747,
3,555 and 833, with no difference between the paths in any. The mutations
found three divergences older than #244's reviews, which the parser now
follows clang in: `}` right after a tag's body, a comma inside a declarator's
group, and a declarator without specifiers at file scope. And each fix of
#244's second review and of its follow-up, undone, is a mutant `test-parser`
fails on (29 of them), and so is each of its third review's (23), while the
unchanged parser passes.

The kernels' checks put every array in an arena followed by 4 GB and its own
size of address space that may not be touched, so that a load past an array, or
through an index that wrapped below 0, faults. And `main.c` built alone with
`-fsanitize=address,undefined` (the other units' benchmarks stubbed out) runs
the tests without files, Lua, `sqlite3.c`, the libc corpus and 3,000 mutations
each of Lua and libc without a report, at the same head.

## Measurements

Measured on 2026-10-22 on the Ryzen 7 9700X (`benchpress`), at `d59d035`,
with Clang 23.1.1 and `build.sh`'s `-O3 -march=native`, each run under the
shared lock by itself and pinned to CPU 2. `benchmark-parser` reports the
medians of 21 interleaved passes after a warm-up, per token, from tokens the
preprocessor made beforehand (AGENTS.md, "Parser benchmark"). The parsers'
fastest and slowest passes (the scalar parser's, the twins' and the vector
path's) are within 1.2% of the median on `sqlite3.c`, 5.5% on libc (the twins'
slowest pass) and 4.4% on `main.c`; the input's conversion, a fraction of a
millisecond, varies more (its slowest pass on `sqlite3.c` took 46% more than
its median). The inputs:
`sqlite3.c` 3.53.4 with the system's headers (707,153 tokens), the 733 files of
the libc corpus that clang accepts (Zig 0.14.1's `lib/libc`, as #186 chose
them; 3,869,223 tokens), and `main.c` with clang's predefines for
`-march=native` (1,831,315 tokens).

| Whole path | `sqlite3.c` ms | cycles/token | instructions/token | libc ms | cycles/token | instructions/token | `main.c` ms | cycles/token | instructions/token |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| The input's conversion | 0.15 | 1.2 | 1.5 | 1.36 | 1.9 | 2.2 | 0.76 | 2.3 | 1.5 |
| Its scalar twin | 1.74 | 13.6 | 41.7 | 5.39 | 7.7 | 42.7 | 3.68 | 11.1 | 41.6 |
| Scalar parser | 8.38 | 65.4 | 296.7 | 35.72 | 50.6 | 282.4 | 19.90 | 59.6 | 295.9 |
| Kernels' scalar twins | 21.02 | 163.9 | 646.5 | 80.38 | 114.0 | 640.2 | 46.77 | 140.0 | 645.4 |
| Vector path | 3.54 | 27.6 | 61.0 | 15.07 | 21.5 | 55.2 | 10.34 | 31.2 | 61.2 |
| Postorder, passes, with sizes | 1.73 | 13.5 | 50.3 | 7.93 | 11.4 | 50.4 | 4.58 | 13.8 | 50.3 |
| Postorder, walk, without sizes | 1.92 | 15.0 | 19.0 | 3.69 | 5.3 | 18.7 | 3.80 | 11.5 | 19.3 |

- **The vector path is 2.37 times as fast as the scalar parser on `sqlite3.c`,
  2.37 times on libc and 1.92 times on `main.c`**, at IPC 2.2, 2.6 and 2.0,
  backend-bound (50% to 59% of dispatch slots), against the scalar parser's
  4.5 to 5.6. It mispredicts 0.149 branches a token on `sqlite3.c` and 0.035
  on libc, the scalar parser 0.510 and 0.127.
- **The scalar parser costs more than the spike's**, 65.4 cycles a token on
  `sqlite3.c` against 44.0, and 297 instructions a token against 190: it takes
  the whole grammar now, with clang's diagnostics and recovery and C's scopes,
  where the spike's parsed what its corpora held. #244's reviews' fixes cost it
  1.2 cycles and 8.2 instructions a token on `sqlite3.c` (64.2 and 288.5 at
  `bfeee15`), the third's 4.9 of those instructions and none of the cycles
  (65.4 and 291.8 at `a70d9a3`); on libc, 5.5 instructions and 1.0 cycle a
  token (49.6 and 276.9), 0.5 in the branch counters' run.
- **The vector path costs more than the spike's**, 27.6 cycles a token on
  `sqlite3.c` against 16.7, and 21.5 on libc against 11.7: K6, which the spike
  didn't have, takes 9.4 and 9.0, and K3's classification, which now also flags
  a typedef name used before its first declarator, 1.2 and 0.7 more. K6 takes
  0.1 to 0.7 cycles a token more than at `bfeee15`, with the first review's
  check of function bodies (a scalar scan back from each body).
- **The postorder's passes with the subtree sizes cost 11.4 to 13.8 cycles a
  token**, twice the spike's passes without sizes (7.1 and 6.0): the sizes take
  two passes more, the first children forward and the starts. Without sizes the
  walk costs 5.3 on libc and 11.5 on `main.c`, where its branches predict well,
  and 15.0 on `sqlite3.c`, where it mispredicts 0.44 branches a token.
- The kernels' twins are tests only: they cost 2.2 to 2.5 times the scalar
  parser.

Stage by stage, in cycles a token, with the spike's third follow-up's for the
kernels it had:

| Stage | `sqlite3.c` | spike | libc | spike | `main.c` |
| --- | ---: | ---: | ---: | ---: | ---: |
| K1 brackets | 2.30 | 2.35 | 1.98 | 1.86 | 2.28 |
| K3 typedef pass | 0.10 | 0.10 | 0.54 | 0.49 | 0.21 |
| K3 classification | 2.67 | 1.43 | 1.89 | 1.20 | 2.93 |
| K2 roles | 3.91 | 3.85 | 3.54 | 3.47 | 5.26 |
| K1's second part | 0.83 | 0.78 | 0.61 | 0.54 | 1.65 |
| K4 parents | 4.46 | 4.47 | 3.96 | 3.95 | 5.22 |
| K5 statements | 1.85 | 1.81 | 0.22 | 0.20 | 1.37 |
| K6 checks | 9.38 | | 8.95 | | 9.64 |
| Fallback | 1.51 | 1.19 | 0.43 | 0.35 | 1.56 |
| Vector path | 27.62 | 16.67 | 21.54 | 11.72 | 31.18 |

**K6** is now the largest stage, at IPC 1.7 to 2.0 with 60% to 66% of its
dispatch slots backend-bound: each block's fixed point of group types and its
three nearest-smaller searches, as K4's blocks were before the spike's
follow-ups. Its twin costs 46 to 54 cycles a token.

### Cloud K6 refinement experiment (2026-10-23)

The joint class table was checked against `1f1e116` on an AVX-512 cloud AMD
EPYC 9V74, with Ubuntu Clang 18.1.3 and the repository's `-O3 -march=native
-fwrapv -funsigned-char -fno-strict-aliasing` flags, plus `-mmwaitx` for the
cloud build. These are exploratory measurements on a different processor;
9700X sustained and after-scalar measurements remain pending.

Both versions read the same frozen `main.c` and included sources, preprocessed
once by Clang 18 with `-E -P -march=native -mmwaitx -I<source>/src`. The resulting
input has 2,155,660 parser tokens and SHA-256
`24bbf3dd765eb8e1d84a9d9ba23304b6d1846b902214c6e28c0f2cb85d706270`.
The existing `benchmark-parser` harness ran pinned to CPU 2, one invocation at
a time. Three processes per version used `--only vector-k6 --passes 7
--repeat 8 -nostdinc` for K6. Two complete paired results were retained for the
whole vector path with `--only vector --passes 11 -nostdinc`; the incomplete
third invocation is excluded. The K6 row shows the median of three medians and
their range, and the whole-path rows show each complete process's median, in
nanoseconds per token; hardware counters were unavailable. The
[K6 raw output and reproduction commands](measurements/simd-301-2026-10-23/parser-k6.txt)
and [whole-path raw output](measurements/simd-301-2026-10-23/parser-whole.txt)
retain the experiment's individual results.

| Path | Baseline | Joint class table |
| --- | ---: | ---: |
| K6, median (range) | 3.228 (3.211–3.804) | 3.050 (3.039–3.202) |
| Whole vector path, process 1 | 11.044 | 10.833 |
| Whole vector path, process 2 | 10.455 | 10.391 |

The K6 median is 5.5% lower. The whole-path ranges overlap, so this does not
establish a whole-parser throughput gain. Each run checked the vector outcome
and tree against the scalar parser; all kept the same fallback of 55 top-level
declarations and 31,386 tokens. Assembly inspection of the untyped refinement
found three table permutations in place of seven and two blends in place of
seven. A separate experiment packing `ends` into 32 columns did not improve
the combined variant consistently and was removed. Replacing the byte-search
saturating arithmetic with masked compares produced identical assembly and
was also discarded.

Seven fresh syntax-driver processes per version also accepted the same input
without diagnostics; all 14 exit statuses were zero. Their elapsed times varied
widely (baseline 0.231–0.299 s, joint table 0.248–0.357 s), with mixed paired
direction, so these runs establish no end-to-end timing effect. The
[driver raw output and commands](measurements/simd-301-2026-10-23/parser-driver.txt)
isolate the parser change over the same frozen source as the kernel runs.

## What's left

- #115's budget for the parser, 2 ms for `sqlite3.c` (about 16 cycles a token):
  the vector path takes 3.54 ms, and with the input's conversion and the
  postorder 5.42 ms. K6 (9.4 cycles a token, its check of function bodies a
  scalar scan back from each, which masks over the 64 tokens before a body
  would replace) and the postorder's sizes (the passes' 13.5 against the walk's
  15.0 without sizes) come first, then K3's classification, and then the
  spike's leads: K2's round and K4's window search.
- The checks' misses on valid code, which cost only the fallback's time:
  qualifiers in a parameter's array declarator (glibc's headers), a typedef
  name after `_Alignas(...)` (K2 takes it for a declarator's name), and asm
  statements, whose operand lists the checks don't model.
- The parser's diagnostics don't show the preprocessor's expansion notes yet
  ("expanded from macro"): #229's `pp_diagnose_output` renders them for an
  output token, which each diagnostic's token gives.
- Ordinary declarations that shadow typedef names still use the scalar
  fallback; K3 now handles unshadowed block-scope aliases directly (#128).
- Multicore (#149, #151): top-level declarations start with empty stacks at
  depth 0, so a unit splits there, with only the typedef pass ordered.
- `test-parser` builds a fresh intern table, preprocessor and parser for every
  unit it parses, and its generated inputs' units cost it about 2 million page
  faults, most of its time.

## Boundary audit #298

Audit pin: `42630114aeb0553c3e2fabfd7fe9bc84b0ce6126`;
[scope and evidence](simd-boundary-audit.md). Source:
[input columns/conversion](https://github.com/buster14a/buster/blob/42630114aeb0553c3e2fabfd7fe9bc84b0ce6126/src/buster/compiler/c_parser.c#L430),
[vector orchestration](https://github.com/buster14a/buster/blob/42630114aeb0553c3e2fabfd7fe9bc84b0ce6126/src/buster/compiler/c_parser.c#L11109),
[unit publication](https://github.com/buster14a/buster/blob/42630114aeb0553c3e2fabfd7fe9bc84b0ce6126/src/buster/compiler/c_parser.c#L11390).

| Producer → consumer | Actual representation and completion |
| --- | --- |
| PP output → `parse_tokens_from_output` → all parser paths | u8 grammar kinds/flags, u32 names and PP source indices; pragma source/position columns. Keywords merge, pragma lines leave as events, `::` becomes two tokens sharing a PP identity. |
| K1 brackets → K3 typedef candidates → K2 roles → secondary K1 | Bracket matches/depths, candidate type-name information, roles and secondary matches are separate dependencies. K3 at this pin is not the later [#320](https://github.com/buster14a/buster/pull/320) scoped-binding producer. |
| Roles/depths → K4 nearest-smaller parents → K5 statements | Contiguous token columns and block summaries, including cross-block search state. A subtree is a source-contiguous range; source order and statement attachments are semantic. |
| Kernel checks → top-level fallback → final tree | Flagged top-level declarations are reparsed by the scalar parser. Parent/role/depth output and diagnostics become usable only after fallback/completion; counts report actual deferred work. |
| Tree → `parse_postorder_sorted` → `check_input` | u32 postorder token permutation and subtree sizes on an error-free full parse. Checker requires both and retains the tree, tokens and PP identities. |
| Tree/diagnostics → `run_parse_command` / `parse_print_diagnostics` | `tree_only` deliberately omits postorder/sizes; diagnostics resolve PP source indices and macro history. Tests/benchmarks are additional tree/postorder consumers. |

Parser token columns allocate for up to twice the PP token count before
conversion, plus **128 initialized entries before and after** the logical
range. Kinds use END sentinels and other padding is zero. Token, parent and key
limits are bounded by `BUSTER_PARSE_MAX_TOKENS` (1 << 30). Do not extend this
padding guarantee to every unrelated scratch/side array. Parser-owned arenas
outlive checker adaptation; PP/intern/cache storage outlives spelling and
error rendering. A parse with diagnostics/fatal status is not valid semantic
input even if some tree columns were filled.

The logical unit and top-level declaration fallback boundaries are independent
of 64-token/16-lane kernels. Cross-block brackets, typedef state, statement
ownership and nearest-smaller searches prevent treating arbitrary chunks as
independent parses. Postorder is published only after the complete parent tree;
changing partitions must preserve parents, roles, depths, ordering and source
mapping. Pragmas remain events, not removable noise for future checking.

Observed costs include PP-to-parser copying/remapping and full-tree postorder
construction. [#335](https://github.com/buster14a/buster/issues/335) compares the first with an optional producer-aware path;
#104–#107/#315 own the fragment alternatives to the second. Neither may change
checker semantics, source identity or move uncounted reconstruction downstream.
Parser-derived bindings proposed in [#320](https://github.com/buster14a/buster/pull/320) and environment/extension branches
#319/#324 are **not** main-branch outputs at this pin; #336 tracks shared checker
consumption after integration.

Disposition: keep full postorder for actual checking, retain the already
measured elimination for the diagnostics-only driver. The driver comparison
above is complete fresh-process syntax checking; parser-kernel results are not
that measurement. `test-parser --clang` checks kernel twins, independent
postorder, alignments/depth/recovery and tree-only equivalence. [#126](https://github.com/buster14a/buster/issues/126)/[#128](https://github.com/buster14a/buster/issues/128)/[#133](https://github.com/buster14a/buster/issues/133)
retain corpus and source-checker acceptance, including memory and fallback
costs over real units, rather than assuming a fast kernel satisfies the handoff.
