# debug

Run commands and resolve source paths from the repository root. Follow the
[shared agent rules](../../AGENTS.md), including the 9700X lock and `cores`
requirements. [Command index](README.md).

## Module overview

- `src/buster/compiler/debug.c` emits bounded DWARF 5 function names, ranges
  and source lines with scalar/vector twins and explicit late address slots.
  `ExecutableOptions.debug` attaches one unit or several (#395), each written
  after the others at its bases (`DebugOutput.bases`) and finalized at its
  functions' TEXT addresses, with nonallocated debug sections and a regular
  function symbol table; `ExecutableOptions.debug_sections` takes sections a
  producer built whole instead (`link-foreign`'s), with the references the
  layout decides as columns the writer patches on its lanes.
  `test-debug --cross-check` checks original fixtures, as one unit and as two,
  with readelf, GDB and perf, linked by GNU ld, by the native writer and from
  the object writer's objects (`ObjectOptions.debug`) by GNU ld and lld;
  `benchmark-debug` measures validation through address finalization. [The
  debug guide](../../docs/debug.md) records the contract and a function's body's row
  (`DebugFunction.prologue_end`); `streaming.c` makes a unit per 256 functions
  as they compile.
