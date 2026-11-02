# lowering

Run commands and resolve source paths from the repository root. Follow the
[shared agent rules](../../AGENTS.md), including the 9700X lock and `cores`
requirements. [Command index](README.md).

## Selective lowering

`./build/compiler test-lowering --clang` compares selective function/object
emission planning with an independent graph oracle and Clang object symbols
on original, manually modeled C fixtures. `benchmark-lowering` measures
synthetic graph preparation and vector/scalar selection; use the shared 9700X
lock and pin timings to CPU 2.

On the source path, `check_program_select` (`c_check_program_select.c`) gives
the planner the checked program's definitions, roots and evaluated references,
and `-c` and the direct path lower what it selects. `c_lowering_symbols.c`
compares an object's functions and objects with `clang -O0 -c`'s: `test-checker`
on fixtures of each kind of definition (functions, objects and string literals),
`test-c-testsuite --cross-check` on c-testsuite and `test-checker-corpus
--symbols` on any corpus. Every correctness run compiles the compiler's own
`main.c` with `build.sh`'s flags for a plain `-march=znver5` build, by `-c` and
by `--lower-all` (`--verify`, and the scalar path's `--lower-all` object the
vector path's byte for byte), and compares its symbols with clang's
(`test-checker-corpus --symbols-minimum 1`), so that it keeps compiling both
ways. `benchmark-lowering-source [--passes N]
[preprocessor options] file...` measures what lowering only the referenced
definitions saves against `--lower-all`, after checking, with each file's
functions and objects defined, lowered and skipped. It times warm selection,
lowering and object construction in memory, with unwind generation and
verification disabled; it excludes checking and file writing. A file the checker rejects,
or that is refused by name (`CHECK_PROGRAM_UNSUPPORTED`,
`CHECK_DATA_UNSUPPORTED`), is left out; any other failure, a timed run that
lowers other than its warm-up, and a run that measures no file fail the command
(`test-checker` checks each, `c_lowering_source_benchmark_test.c`). Run it under
the 9700X's lock with `taskset -c 2`. [The lowering guide](../../docs/c-lowering.md) records the
contract, the comparisons' results and the completed `main.c` measurement.
