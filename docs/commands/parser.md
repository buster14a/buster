# parser

Run commands and resolve source paths from the repository root. Follow the
[shared agent rules](../../AGENTS.md), including the 9700X lock and `cores`
requirements. [Command index](README.md).

## Module overview

- `src/buster/compiler/c_parser.c` is the parser (#127): the preprocessor's
  token stream to a tree, a parent, a role and a node depth for every token,
  and for a unit without errors its postorder and the tree's columns
  (`parse_tree_columns`, #186/#544: each token's position in the postorder,
  subtree end, first child and next sibling; with `ParseOptions.sizes` each
  subtree's size), which the checker's reader derives itself from the C
  driver's `tree_only` parse. The
  vector path builds it with kernels in AVX-512, each with an exact scalar
  twin, and a pass of checks that flags what C's grammar doesn't allow, whose
  top-level declarations go to the scalar parser; the scalar parser alone
  (`--scalar`) builds the same tree and gives the diagnostics, at clang's
  places with clang's messages. It also holds the driver. `docs/c-parser.md`
  explains the design and its 9700X measurements. `c_parser_test.c` holds its
  tests (`main.c` includes it; `./build/compiler test-parser` runs them) and
  `c_parser_benchmark.c` provides `benchmark-parser`. Salter's operator
  fragments, the alternative to the postorder that #104–#107 measured end to
  end beside the checker's tree walk, lost and were removed: [the
  decision](../../docs/c-parser-fragments.md).
## Parser tests

`./build/compiler test-parser [--clang [PATH]] [--mutations N] [--seed N]
[--streams N] [--reports N] [--keep] [options] [file or directory...]`
(about 7 s on the 9700X without files, with `--clang`) checks `c_parser.c`; `--clang` needs
clang 23 on `PATH`, or the one PATH names. Each unit goes through both paths,
the vector path with its fallback and the scalar parser alone, which must read
the same tokens and give the same outcome, and on a unit without errors the same
tree: the same parent, node depth and role on every token. The tree must keep
its invariants (every parent another token or the root, at no greater depth;
every opener's parent its closer, after it; no cycle), and its postorder and
subtree sizes must be a depth-first walk's, whose in-order walk gives the tokens
back in order, and so must both paths' tree's columns and their twin's
(`parse_tree_links`): each token's position, subtree end, first child and next
sibling, and the sentinels. Then each kernel against its scalar twin, from the twins'
outputs before it: K1 (depths, enclosing openers and each block's view), K3's
classification, K2 (roles, flags, typed states and statement contexts, also
from random roles and with every block in the twin's loop), K1's second part,
K4 (with the stack's top in registers and with windows of 1, 3 and 8 lanes),
K5, and K6 (flags and classes, also with every second and every third block
left to the twin); and the twins together with the fallback, whose tree must be
the scalar parser's. Those checks put every array in an arena followed by 4 GB
and its own size of address space that may not be touched, so that a load past
an array, or through a 32-bit index that wrapped below 0, faults. On:

- snippets: 202 of valid and invalid code, each with the first error clang gives
  (or none), which the parser must give at the same line and column with the
  same message; `--clang` checks each expectation against clang too, which
  must be a parse issue but for the snippets marked as the errors clang's
  semantic analysis gives where its parser took a declaration as ours does (an
  unknown type name, a missing type specifier, a type name where an operand
  would be), or as code only its semantic analysis rejects, which the parser
  takes; and the oracle itself: clang given an option it doesn't know must
  count as no answer, and an error in a header a file includes must reject the
  file;
- fixtures: five trees checked by hand, parent by parent, among them #127's
  comma case (`f(a ? b, c : d, e)` has two arguments), its cast case (`(T) * p`
  is a cast where `T` names a type and a product where it doesn't), a
  declarator's spine (`(*a[3])(double)`: array, pointer, function) and computed
  gotos (`goto *p ? p : p`: the `*` the goto's child and the whole expression
  its own), which the vector path must build without its fallback;
- generated inputs, without files: `--streams N` random streams of C's tokens
  (2,000; half with balanced brackets), the snippets after 0 to 63 file-scope
  `;`, at every block alignment, and nine kinds of nesting 100,000 levels deep
  (commas in nested ternaries among them) and a run of 100,000 attributes, which
  mustn't grow the native stack, nor make a token cost more for what is open
  around it;
- a borrowed scratch (`ParseOptions.scratch`, #528), without files: units in
  turn on one parser for each path, whose arena holds the caller's data below
  its length and dirty pages above it (each parse's temporaries, and a pattern
  before each parse): the fixtures' sources, valid code of which the vector
  path sends some declarations to the scalar parser, an error, an unclosed
  brace and 64 random streams. Each must parse as on a fresh parser, tokens,
  tree, postorder, diagnostics and statistics alike, and leave the arena's
  length and the caller's data as they were;
- the driver, without files: seventeen cases' exit statuses and exact messages
  (a valid, an empty and an invalid file, the last with `--scalar` too, an
  `#error`, a missing header, a missing file, a directory, a sparse file past
  the preprocessor's size limit, no file, an unknown option, GNU's keywords
  turned off two ways, two errors after a macro's expansion, which the
  expansion history places and the driver records only for its diagnostics,
  and a warning with code after it and alone, which the driver prints by
  preprocessing the unit again), 16 runs of each, after which the address
  space and the descriptors must be as they were; an invalid
  `SOURCE_DATE_EPOCH` for `parse` and for `-E`, which must print the
  preprocessor's setup's fatal error alone;
  then every reservation and mapping of a run on a valid and on an invalid file
  and on the two warnings' files (whose replays' reservations among them)
  failed in turn, in children whose address space may grow a page less than a
  run reached: the last fails, then, a page below the peak that run reached,
  the one before it, down to the first. Each run must exit with status 1 having
  said why in a fatal error, and leave nothing behind;
- files: every `.c` file under each path, preprocessed with -E's options, must
  parse; with `--clang`, the parser must accept exactly what `clang
  -fsyntax-only` with the same options accepts (our side reads clang's
  predefines for target options from `--predefines`);
- mutations (`--mutations N`, with files): N copies of the files' -E text with
  a token deleted, duplicated or swapped with the next. With `--clang`, a copy
  clang accepts must parse, and one whose first error is a parse issue must fail
  with its first error on the same line; without, both paths must agree as on
  any file.

`--explain` tallies instead why the checks sent valid code to the scalar parser,
by signature, with an example of each, and `--print-tree FILE [FIRST LAST]`
prints each token's role, node depth and parent by both paths, with the
differences marked. `--seed N` changes the random inputs and the mutations,
`--reports N` how many differences are printed (200), and `--keep` keeps the
files in the directory the command makes under `$TMPDIR` (or `/tmp`). On the
9700X, `sqlite3.c`, the 733 files of the libc corpus that clang accepts and the
repository's translation units pass with `--clang`, and so does Lua 5.4.8
locally; `docs/c-parser.md` has the corpora and the mutations' counts. The 9700X job runs
it without files with `--clang`, and on the repository's translation units without:
the build has compiled each with clang, with `-Wall -Werror`, so clang accepts them.

## Parser benchmark

`./build/compiler benchmark-parser [--passes N] [--only NAME|stages] [--repeat N]
[--counters pipeline|branches|memory|stalls] [options] file...` preprocesses
each file with -E's options and turns its tokens into the parser's outside the
timing, checks that the vector path gives the scalar parser's outcome and tree,
and times `--passes` interleaved passes (21) after a warm-up: the whole paths
over every file (the input's conversion and its twin, the scalar parser, the
twins together and the vector path), then each stage per file on shared arrays,
after the stages before it (each kernel and its twin, K4's three loops, the
fallback, the tree's columns with and without the subtree sizes, their twin from
that postorder, and the postorder by a walk). It reports
medians per token of time, cycles (with the fastest and slowest pass) and
instructions, the events of the `--counters` set, and the share of top-level
declarations and tokens the fallback took. `--only` times one path or stage
(`stages` for every stage), and a stage named alone runs `--repeat` times per
file, for perf. Run it under the 9700X's lock with `taskset -c 2`;
`docs/c-parser.md` has the numbers for `sqlite3.c`, the libc corpus and `main.c`.
The default 9700X job runs it on `main.c`, pinned to one allowed CPU; like the
others, its measurements are informational.
