# checker-corpus

Run commands and resolve source paths from the repository root. Follow the
[shared agent rules](../../AGENTS.md), including the 9700X lock and `cores`
requirements. [Command index](README.md).

## Checker corpus and semantic mutations

`./build/compiler test-checker-corpus [--clang PATH] [--report NEW.tsv]
[--files-from LIST --root DIR] [--semantic-mutations N [--minimize N]]
[--symbols] [--symbols-minimum N] [--seed N] [--deferred N [--deferred-bindings
stacks|batch|direct]] [--id-order N] [--keep] [preprocessor options]
file-or-directory...`
checks each file as the driver does and compares it with
`clang -fsyntax-only` on the same options: acceptance, the first error's
presumed file and line, and the scalar and vector paths, column by column.
`--symbols` (#129) also writes each accepted file's object as `-c` does and
compares the functions and objects it defines with `clang -O0 -c`'s, with the
names the checked source makes; a difference must have one of four documented
reasons, found from the checked program (`docs/c-lowering.md`, "Against clang
-O0's objects": folded constants, unselected operands, a constant `if`'s
dropped arm, from the model `CheckProgramOptions.folded_ifs`, and a local
`always_inline` function, which clang inlines everywhere), and a file the
backend refuses is counted with its feature.
`--symbols-minimum N` fails the run unless at least N files' objects were
compared: the 9700X job's test step compares eight of the frozen musl `src`
directories so, with the count they compare. `--id-order N` checks each file
with its identifiers' ids in another order (#153, as the driver's `--id-order`
gives them), which the 9700X job does for c-testsuite's programs with
`--deferred 8`. `--semantic-mutations N` then checks N
mutants of the units it found no difference in (#133's last "Done when" item,
`c_check_mutation.c`):

- each unit's base is its `-E` text with line markers, compared as a file
  first; the scalar parser's tree gives the sites;
- each mutant is one edit of a base: a declaration's type changed (a pointer
  added or removed, signedness, width, `const`, a struct for its type), a
  declaration removed (a local, a parameter, a file-scope one, a member), or a
  binary operator's operands or a call's two arguments swapped. Three in four
  edit the unit's own file, the fourth a file it includes, and `--seed N`
  (133) picks them;
- each mutant is checked as a file is, against clang on the same text with
  `-x cpp-output`. The first `--minimize N` (4) differences are minimized and
  saved as `checker_mutation_N.c` in the current directory, and
  `--minimize-mutant FILE...` minimizes saved ones again. A fixed difference
  becomes a fixture in `c_check_mutation_test.c`, which `test-checker` checks
  against clang and through both paths; a difference the checker means to
  keep would go in `check_mutation_intended`, with its reason (none so far).

It prints each kind's sites, mutants, clang's rejections and differences, and
exits with status 1 on any difference. `docs/c-checker.md` ("Corpus comparison
and semantic mutations") has the four corpora's configurations, the campaigns'
counts and the base differences the comparisons found, all fixed (the libc corpus's last two, `fmaxl.c` and
`fminl.c`, by #148's floating comparison builtins; Lua's labels as values since #133 took them). The 9700X job's test step checks 8
mutants of the frozen SQLite source and 96 of the oracle's four musl sources
with seed 133; a run that differs fails it, and `--keep` with `--minimize`
reproduces it locally.
