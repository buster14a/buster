# unwind

Run commands and resolve source paths from the repository root. Follow the
[shared agent rules](../../AGENTS.md), including the 9700X lock and `cores`
requirements. [Command index](README.md).

## Module overview

- `src/buster/compiler/unwind_tier0.c` derives fixed-frame CFI descriptions
  from valid Tier 0 records after branch sizing (`unwind_tier0`, an opt-in
  consumer that does not repeat generic instruction validation), or reads them
  from the frame Tier 0 published, with no pass over the body
  (`unwind_tier0_frame`, #395: frame pointers' too, which the analyzer
  refuses, and realigned frames', whose `and rsp` and base's `mov` it checks;
  `unwind_tier0_frame_tails` with a function's tail calls' teardowns, #147,
  each remembered and restored around its jump, and Tier 1's early returns'
  as such teardowns before their rets, #177);
  `unwind.c` emits original CIE/FDE contributions with scalar/vector
  twins, a pair per function or with `UnwindOutput.shared` one CIE for the
  batch, and for Tier 1's open frames (`UnwindFunction.open`, #177) no epilogue
  at the end, each return a tail, and row programs (#1056: resolved frame rows,
  `unwind_rows_scalar`, the fixed writer's bytes for a fixed shape's rows).
  `check_program_emit` writes the C driver's unwind tables from each function's
  frame events (`emitted_code.c`, the [emission guide](emission.md)), one
  EH_FRAME atom of them after the last function. `test-unwind --cross-check`
  checks independent frame-state oracles, protected tails, the published
  frame against the analyzer and actual Tier 0 code, a frame pointer's and
  realigned frames' among it, with native GDB/perf consumers. `benchmark-unwind` charges
  analysis/emission to the existing Tier 0, sizing and encoding chain,
  including per-function arena cleanup, and the driver's published frame
  (`--only published`). [The unwind guide](../../docs/unwind.md) records the trusted
  private-slot contract, Link fixups and pending target-host qualification.
  Use the shared 9700X lock per command and CPU 2 for timing.
