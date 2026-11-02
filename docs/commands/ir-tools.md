# ir-tools

Run commands and resolve source paths from the repository root. Follow the
[shared agent rules](../../AGENTS.md), including the 9700X lock and `cores`
requirements. [Command index](README.md).

## Module overview

- `ir_parse.c` reads the shared printer format through bounded scalar/vector
  paths. `test-ir-parse` checks canonical `.ir` fixtures and diagnostics;
  `test-ir-optimize` runs the pass fixtures in that form.
  `test-ir-reduce` checks deterministic structural reduction, exact failure classes,
  allocation rollback and canonical replay; `reduce-ir` writes a new reproducer
  bundle without requiring the C frontend. See [IR reduction](../../docs/ir-reduce.md).
  `interpreter.c` and `ir_interpreter.c` execute typed streams and shared IR
  with explicit arena frames, the IR's calls with by-value aggregates' records
  and variadic definitions' anchors among them (#788); `interpreter_native.c`
  supplies the SysV host boundary and `interpreter_link.c` materializes data
  atoms. Their commands are
  `test-interpreter`, `test-interpreter-native`, `test-interpreter-link`,
  `test-ir-interpreter`, and `interpret-ir`. [The tools guide](../../docs/ir-tools.md)
  records the API, text grammar and frontend-dependent acceptance. Run builds
  and tests through the approved 9700X's shared per-run lock.
