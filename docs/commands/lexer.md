# lexer

Run commands and resolve source paths from the repository root. Follow the
[shared agent rules](../../AGENTS.md), including the 9700X lock and `cores`
requirements. [Command index](README.md).

## Module overview

- `C_TOKENIZER_NO_DIGIT_SEPARATORS` makes both lexer paths end pp-numbers
  before `'`; the preprocessor sets it in modes before C23, including file
  caches, scratch text and pastes. The standalone lexer's default keeps C23
  separators. `benchmark-lexer --no-digit-separators` measures the flag.
- `src/buster/compiler/c_lexer_test.c` holds the lexer tests. `main.c` includes
  it, and `./build/compiler test-lexer` runs them.
- `src/buster/compiler/c_lexer_benchmark.c` provides the `benchmark-lexer`
  command. The 9700X CI workflow runs it on the repository's C sources by
  default. `docs/c-lexer.md` records the GFNI classifier, the leading-comment
  skip, branch-free chunk state, the pipelined write-out, table-driven
  punctuator kinds, the delimiter scan, chunks read in place, the raw string
  check, numbers without digit separators (`C_TOKENIZER_NO_DIGIT_SEPARATORS`,
  C17's) and their 9700X measurements. `c_lexer_variant.c` is one variant of the
  lexer as a translation unit of its own, which `./build.sh lexer-variant`
  compiles in for `benchmark-lexer` to compare (see [Lexer benchmarks](#lexer-benchmarks)).
## Lexer tests

`./build/compiler test-lexer` needs no corpus. Its checks are ports of the
snippet and random-input checks of `c/c_tokenizer_test.c` in
Accelerated-Zig-Parser, where the lexer comes from. They compare `c_tokenize`
with the scalar lexer token by token, each input twice: with C23's digit
separators, and without them (`C_TOKENIZER_NO_DIGIT_SEPARATORS`, as C17 lexes
numbers, which the preprocessor asks for before C23). Each check prints the
second's counts after the first's. The inputs:

- the boundary snippet after 0 to 1,099 bytes of padding at each of the 64
  misalignments from a cache line: 70,400 placements, none of which may need
  the scalar lexer;
- 200,000 random inputs made of weighted C pieces. `--fuzz N` sets how many,
  and `--seed N` picks another sequence (the same inputs as upstream's
  `--seed N`). The first inputs that differ are saved as `fuzz_failure_N.c` in
  the current directory (`fuzz_failure_no_separators_N.c` without
  separators);
- page edges: every length from 0 to 1,099 bytes, cut as a prefix and as a
  suffix from the repeated snippet, four strings of random inputs (which
  continue the random sequence) and three comment templates, placed to end
  exactly at the end of a readable page and to start exactly at the start of
  one, between `PROT_NONE` guard pages: 35,200 inputs. These also compare
  `c_tokenize_vectorized`, and a read outside the source faults; the command
  then names the input and exits with status 1. Length 0 at a page end points
  at the guard page itself. The chunk loop loads aligned 64-byte lanes, so
  only a lane wholly outside the source can fault; an over-read within the
  source's last lane stays on a readable page and is not caught. The
  leading-comment scan's loads are unaligned and masked to the source, so the
  guard pages check them fully;
- 7,040 comment cases across every source misalignment: closers around the
  leading-comment scan's 64-byte loads and around 512-byte chunk boundaries,
  including closers whose `*` ends a chunk (also a chunk wholly inside the
  comment), a spliced closer, a `/*/` whose `*/` ends the comment, and a
  comment inside a preprocessing directive;
- numbers: 17 pp-numbers (every kind of digit, prefix and suffix, a `.` and
  exponents, and numbers that have separators already) with a `'` at every
  place in them, each before 18 followers (the end of the source, what a
  separator may join, quotes, other literals and comments), with no line
  splice around the `'`, one on either side, or `\r\n` ones on both, placed so
  that the `'` is at byte 0 to 2, 62 to 65 or 509 to 514 of a source that
  starts a chunk: 80,946 cases. Without separators, a case without a splice
  must stay on the vectorized path;
- 20 numbers with a `'` (16 in C and 4 in C++) whose tokens must be the ones
  clang 23.1.3's raw lexer gives them (`-std=c17`, `c23`, `c++11` and
  `c++26`), from both lexers.

`--cplusplus` tokenizes everything as C++, with the C++ snippet and pieces.
`--raw-strings` enables C11's raw prefixes in C; `--raw-strings-c99`
enables `R` and `LR`. Raw literals and near misses are checked around
chunk and page boundaries. Both modes run in the 9700X workflow.
The separate raw-error query is checked on invalid/unterminated literals and
every truncated prefix against a protected page; diagnostic offsets are
physical bytes even when a prefix has line splices.
`--no-digraphs` and `--no-line-comments` tokenize C without digraphs and without
`//` comments (`C_TOKENIZER_NO_DIGRAPHS` and `C_TOKENIZER_NO_LINE_COMMENTS`), as
the preprocessor lexes `-std=c89` (both), C94 (the second) and `-fno-digraphs`
(the first): the same inputs, lexed otherwise, through every check; C++ takes
neither (#288). The command prints each check's counts and first differences,
and exits with status 1 if anything fails. The 9700X job's test step runs C++,
C and C's three other modes. Before adding the C89 modes, the C and C++
runs took about 2.8 s and 3.0 s on the 9700X. For a longer run:

```sh
./build/compiler test-lexer --cplusplus --seed 7 --fuzz 2000000
```

## Lexer benchmarks

`./build/compiler benchmark-lexer [--cplusplus] [--no-digraphs]
[--no-line-comments] [--no-digit-separators] path...` checks each matching source against the scalar
lexer and reports 21 interleaved-pass median throughput, cycles per byte,
instructions per byte and IPC (hardware counters are zero when unavailable).
Paths can be files or directories; `--cplusplus` selects C++ source/header
extensions, and `--no-digraphs` and `--no-line-comments` C's modes, as
`test-lexer` takes them, and
`--no-digit-separators` lexes numbers as C17 does, as the preprocessor does
before C23 (`docs/c-lexer.md`, "Numbers without digit separators"). The default
9700X job runs
`benchmark-lexer src` after the tests, pinned to one allowed CPU. Measurements
are informational: correctness failures fail CI, but throughput values are not
yet compared against a performance baseline. Keep just one compiler executable.

`./build.sh lexer-variant NAME=SOURCE...` builds `build/compiler` with other
lexers in it, each SOURCE a copy of `c_lexer.c` (or its assembly, from
`./build.sh lexer-variant-assembly NAME SOURCE OUTPUT.s`), as
`c_lexer_variant_NAME`. `benchmark-lexer` then also compares each with
`c_tokenize` on every file (a difference fails it), times them in an order
that rotates by pass, and prints each one's change from `c_tokenize`.
Where its code lands moves a variant by up to about 1.5% even when its code
is `c_tokenize`'s (a copy of it as a variant is the comparison's control), and
builds in separate processes differ by about 2%: `docs/profiling.md`
("Comparing two versions") has how to use both.
