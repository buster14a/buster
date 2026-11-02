# preprocessor-fuzz

Run commands and resolve source paths from the repository root. Follow the
[shared agent rules](../../AGENTS.md), including the 9700X lock and `cores`
requirements. [Command index](README.md).

## Module overview

- `src/buster/compiler/preprocessor_fuzz.c` provides bounded output comparison
  and the minimizer for the independent preprocessor test harness.
  `preprocessor_fuzz_generate.c` generates cases, `preprocessor_fuzz_clang.c`
  runs the Clang oracle, and `preprocessor_fuzz_test.c` provides
  `test-preprocessor-fuzz`, the harness's own checks; `main.c` includes all
  four. `preprocessor_fuzz_benchmark.c` provides `benchmark-preprocessor-fuzz`,
  measuring the external test harness. `c_preprocessor_fuzz.c` attaches the
  preprocessor to it: `test-preprocessor --fuzz` in
  [Preprocessor tests](preprocessor.md#preprocessor-tests).
  `docs/preprocessor-fuzz.md` records the contract, the comparison and the
  qualification runs.
## Preprocessor harness tests

`./build/compiler test-preprocessor-fuzz [--fuzz N] [--seed N] [--clang PATH]
[--std c17|c23]` checks the independent tooling for #109. It requires Clang 23
on `PATH`, or the executable selected by `--clang PATH`. The defaults are
64 generated cases, seed 1, `clang` and C17; the 9700X job's test step runs
both C17 and C23. The checks cover:

- deterministic generation, including every one of the 36 construct families
  even when `--fuzz N` is smaller, and repeatability for seeds 1, 7 and 99;
- independent rejection of each invalid `#`/`##` form, with acceptance after
  repairing its sole intentional fault; arithmetic and bitwise predicates
  exercise both branch outcomes, and replacing an expression with zero must
  change the accepted output tokens;
- batched Clang preprocessing against one process per case: the same
  acceptance or rejection, token kinds, spellings and token counts, with
  whitespace ignored. Cases have distinct name prefixes and marker tokens
  delimit their outputs; a rejected batch is rerun one case at a time.
  Accepted cases are also batched separately so rejection fallback cannot
  conceal a batching error;
- isolation of cases, line information and predefined counter behavior.
  `__COUNTER__` cases run alone because its state belongs to the Clang process;
- output comparison, including unequal acceptance, token counts, kinds and
  spellings, whitespace-only changes, and invalid oracle results; and the
  reader of clang's `-dump-tokens` text, on lines as clang 23 writes them
  (escapes, keywords, annotations, and tokens written with line splices, whose
  bytes as written go on to more lines), and the lines it must refuse;
- a planted comparison for the minimizer: delete complete lines, then tokens,
  while preserving the difference, until only the two required identifier
  tokens remain. Saving and reading a `fuzz_failure_N.c` fixture must preserve
  its bytes without overwriting an existing failure; the self-test removes
  its temporary files.

The oracle runs `clang -E -P -undef -nostdinc -std=c17` or `-std=c23` with
`-x c`. It resets each case's source filename and line number identically in
batched and individual runs, and splits the output at its markers, each a
whole identifier that ends its printed line (found by its bytes outside
literals, since a case's output can hold text that doesn't lex back); any
other marker fails the split. A batch's output is tokenized with `c_tokenize`
as the mode lexes. Infrastructure failures are distinct from source
rejection.

This is test tooling outside the compile path and has no new vector path.
These self-tests check the harness alone, with the minimizer on its planted
comparison; `test-preprocessor --fuzz` ([Preprocessor
tests](preprocessor.md#preprocessor-tests)) runs the preprocessor through it.
`docs/preprocessor-fuzz.md` has the harness's contract and its use.

## Preprocessor harness benchmark

`./build/compiler benchmark-preprocessor-fuzz [--clang PATH] [--std c17|c23]
[--seed N]` requires Clang 23 and defaults to `clang`, C17 and seed 1. It
processes 16 generated family-0 cases (object macros and their shared
alias/redefinition scaffold) together, then in separate Clang
processes, checking that every case is accepted and the output byte totals
agree. After one warm-up pass, 21 measured passes alternate which mode runs
first and report median milliseconds per batch and cases per second.

The timing includes generation, launching Clang, temporary-file I/O and
cleanup. It measures the external test harness, not Buster compilation
speed. Run it under the 9700X's lock with `taskset -c 2`; the default 9700X job
runs it after the tests, pinned to one allowed CPU. Measurements are
informational; execution or output-check failures fail the command.
