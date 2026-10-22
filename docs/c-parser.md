# The C parser

`src/buster/compiler/c_parser.c` parses one translation unit (#127): the
preprocessor's token stream (`c_preprocessor.c`) to a tree, with diagnostics for
what C's grammar rejects. It is the design spike #186 chose, built in full: the
spike's kernels in AVX-512 with their scalar twins, the scalar parser as the
fallback, oracle and home of the diagnostics, and a pass of checks the spike
didn't have, which lets the vector path take only code the scalar parser would
accept with the same tree. `compiler file.c` runs the preprocessor and the
parser; nothing is written yet.

On the 9700X the vector path parses `sqlite3.c` (707,153 tokens) in 3.58 ms,
27.9 cycles a token, 2.35 times as fast as the scalar parser (65.4), and the
libc corpus at 21.5 cycles a token, 2.33 times as fast (49.6); with the
postorder and its subtree sizes, 41.7 and 32.9. Both paths build the same tree
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
message (68 messages, `BUSTER_PARSE_MESSAGES`), and after an error skips to the
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
  where what may follow it differs (118 classes: a constant, a string, a type
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

## Correctness

`test-parser` (AGENTS.md, "Parser tests") compares the two paths on every unit
it reads, the tree's invariants and postorder, and each kernel with its twin,
on snippets checked against clang, generated inputs, the driver's cases and
failures, corpora, and mutations of them checked against clang.

On the 9700X, at `a70d9a3`, with `--clang`:

- the tests without files: the 174 snippets, the four fixtures, 2,000 random
  streams, the snippets at every block alignment (11,136 inputs), nine kinds of
  nesting 100,000 levels deep and a run of 100,000 attributes, and the driver's
  cases and its failures (5.1 s);
- `sqlite3.c` (3.53.4, 707,153 tokens, 1.6 s), the 733 files of the libc corpus
  that clang accepts (3,869,223 tokens, 8.7 s) and the repository's 19
  translation units (12,622,522 tokens, 6.9 s): every file accepted, as clang
  accepts it, with both paths' trees and every kernel's twin agreeing.

The merge of main after it (`c16fa32`) brought other modules' changes, among
them two more translation units and the preprocessor's file cache; locally, the
21 units (13,596,016 tokens) pass the same way.

Locally, on a Ryzen 9 7940HS, at `a70d9a3`: Lua 5.4.8's 34 files (572,263
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
fails on (29 of them), while the unchanged parser passes.

The kernels' checks put every array in an arena followed by 4 GB and its own
size of address space that may not be touched, so that a load past an array, or
through an index that wrapped below 0, faults. And `main.c` built alone with
`-fsanitize=address,undefined` (the other units' benchmarks stubbed out) runs
the tests without files, Lua, `sqlite3.c`, the libc corpus and 3,000 mutations
each of Lua and libc without a report, at the same head.

## Measurements

Measured on 2026-10-22 on the Ryzen 7 9700X (`benchpress`), at `a70d9a3`,
with Clang 23.1.1 and `build.sh`'s `-O3 -march=native`, each run under the
shared lock by itself and pinned to CPU 2. `benchmark-parser` reports the
medians of 21 interleaved passes after a warm-up, per token, from tokens the
preprocessor made beforehand (AGENTS.md, "Parser benchmark"). The parsers'
fastest and slowest passes (the scalar parser's, the twins' and the vector
path's) are within 0.9% of the median on `sqlite3.c`, 1.7% on libc and 3.6% on
`main.c`; the input's conversion, a fraction of a millisecond, varies more (its
slowest pass on `sqlite3.c` took 19% more than its median). The inputs:
`sqlite3.c` 3.53.4 with the system's headers (707,153 tokens), the 733 files of
the libc corpus that clang accepts (Zig 0.14.1's `lib/libc`, as #186 chose
them; 3,869,223 tokens), and `main.c` with clang's predefines for
`-march=native` (1,706,197 tokens).

| Whole path | `sqlite3.c` ms | cycles/token | instructions/token | libc ms | cycles/token | instructions/token | `main.c` ms | cycles/token | instructions/token |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| The input's conversion | 0.13 | 1.0 | 1.5 | 1.39 | 2.0 | 2.2 | 0.47 | 1.5 | 1.5 |
| Its scalar twin | 1.83 | 14.3 | 41.7 | 5.87 | 8.4 | 42.7 | 3.43 | 11.2 | 41.6 |
| Scalar parser | 8.43 | 65.4 | 291.8 | 35.07 | 49.6 | 276.9 | 18.66 | 60.1 | 290.4 |
| Kernels' scalar twins | 21.47 | 166.7 | 646.4 | 80.57 | 114.3 | 640.2 | 43.14 | 139.1 | 644.8 |
| Vector path | 3.58 | 27.9 | 60.9 | 15.04 | 21.5 | 55.1 | 9.31 | 30.2 | 60.6 |
| Postorder, passes, with sizes | 1.77 | 13.8 | 50.3 | 7.96 | 11.4 | 50.4 | 4.34 | 14.1 | 50.3 |
| Postorder, walk, without sizes | 1.95 | 15.2 | 19.0 | 3.73 | 5.3 | 18.7 | 3.45 | 11.2 | 19.3 |

- **The vector path is 2.35 times as fast as the scalar parser on `sqlite3.c`,
  2.33 times on libc and 2.00 times on `main.c`**, at IPC 2.2, 2.6 and 2.0,
  backend-bound (51% to 59% of dispatch slots), against the scalar parser's
  4.5 to 5.6. It mispredicts 0.149 branches a token on `sqlite3.c` and 0.035
  on libc, the scalar parser 0.517 and 0.119.
- **The scalar parser costs more than the spike's**, 65.4 cycles a token on
  `sqlite3.c` against 44.0, and 292 instructions a token against 190: it takes
  the whole grammar now, with clang's diagnostics and recovery and C's scopes,
  where the spike's parsed what its corpora held. #244's reviews' fixes cost it
  1.2 cycles and 3.3 instructions a token on `sqlite3.c` (64.2 and 288.5 at
  `bfeee15`).
- **The vector path costs more than the spike's**, 27.9 cycles a token on
  `sqlite3.c` against 16.7, and 21.5 on libc against 11.7: K6, which the spike
  didn't have, takes 9.3 and 9.0, and K3's classification, which now also flags
  a typedef name used before its first declarator, 1.2 and 0.7 more. K6 takes
  0.1 to 0.7 cycles a token more than at `bfeee15`, with the first review's
  check of function bodies (a scalar scan back from each body).
- **The postorder's passes with the subtree sizes cost 11.4 to 14.1 cycles a
  token**, twice the spike's passes without sizes (7.1 and 6.0): the sizes take
  two passes more, the first children forward and the starts. Without sizes the
  walk costs 5.3 on libc and 11.2 on `main.c`, where its branches predict well,
  and 15.2 on `sqlite3.c`, where it mispredicts 0.45 branches a token.
- The kernels' twins are tests only: they cost 2.3 to 2.6 times the scalar
  parser.

Stage by stage, in cycles a token, with the spike's third follow-up's for the
kernels it had:

| Stage | `sqlite3.c` | spike | libc | spike | `main.c` |
| --- | ---: | ---: | ---: | ---: | ---: |
| K1 brackets | 2.24 | 2.35 | 2.21 | 1.86 | 2.43 |
| K3 typedef pass | 0.11 | 0.10 | 0.54 | 0.49 | 0.20 |
| K3 classification | 2.67 | 1.43 | 1.90 | 1.20 | 2.85 |
| K2 roles | 3.89 | 3.85 | 3.51 | 3.47 | 5.04 |
| K1's second part | 0.86 | 0.78 | 0.61 | 0.54 | 1.58 |
| K4 parents | 4.53 | 4.47 | 3.99 | 3.95 | 4.98 |
| K5 statements | 1.84 | 1.81 | 0.23 | 0.20 | 1.33 |
| K6 checks | 9.34 | | 9.00 | | 9.59 |
| Fallback | 1.54 | 1.19 | 0.42 | 0.35 | 1.48 |
| Vector path | 27.87 | 16.67 | 21.52 | 11.72 | 30.18 |

**K6** is now the largest stage, at IPC 1.7 to 2.0 with 61% to 66% of its
dispatch slots backend-bound: each block's fixed point of group types and its
three nearest-smaller searches, as K4's blocks were before the spike's
follow-ups. Its twin costs 46 to 55 cycles a token.

## What's left

- #115's budget for the parser, 2 ms for `sqlite3.c` (about 16 cycles a token):
  the vector path takes 3.58 ms, and with the input's conversion and the
  postorder 5.49 ms. K6 (9.3 cycles a token, its check of function bodies a
  scalar scan back from each, which masks over the 64 tokens before a body
  would replace) and the postorder's sizes (the passes' 13.8 against the walk's
  15.2 without sizes) come first, then K3's classification, and then the
  spike's leads: K2's round and K4's window search.
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
