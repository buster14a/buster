# emission

Run commands and resolve source paths from the repository root. Follow the
[shared agent rules](../../AGENTS.md), including the 9700X lock and `cores`
requirements. [Command index](README.md). The event protocol it resolves is
[emission-metadata](emission-metadata.md)'s.

## Module overview

- `src/buster/compiler/emitted_code.c` is #1056's tier-neutral emitted function,
  `EmittedCode`, which `check_program_emit` consumes (its code, rows and unwind
  path no longer consume a `Tier0Code`; `--verify`'s comparison with
  `unwind_tier0` still reads the records as one):
  records, branch columns, references, labels, optional source events (-g) and
  the producers' frame events. Tier 0's code adapts to it (`emitted_from_tier0`:
  its published frame as events at record boundaries) and Tier 1's publishes it
  (`check_tier1_compile`, its frame emitter's own events); `emitted_unwind`
  resolves the events through the final layout after branch sizing, and
  `unwind.c` writes the fixed-frame CFI where they spell a fixed frame (the same
  bytes as before) and a row program otherwise (`BUSTER_UNWIND_ROWS`,
  `unwind_rows_scalar`), e.g. a probe loop's CFA in r11. `emitted_verify_frames`
  proves the events against the records and their control flow (a dataflow of
  register values as CFA offsets, entry values and save slots, checked at every
  reached record; a rule that fails past a form `x86_semantics.c` doesn't cover,
  such as `monitorx`'s leaf's, unproven rather than refuted, which the driver
  keeps), with `--verify` and in the tests. `emitted_code_test.c`
  supplies `test-emitted-code [--seed N] [--count N] [--cross-check] [--keep]`:
  row programs against the fixed writer for every descriptor, Tier 0's programs
  and the Tier 1 planner's every flag against the previous readers byte for byte,
  mutants refuted, `emitted_code_limits_test.c`'s near-2 GB frames with wide CFA values (#1149), compile-only
  `-c`/`--verify` coverage, overflow-safe verifier tracking and
  `emitted_unwind_same`'s resource answer, uncovered forms' verdicts and
  `--verify -c` of `monitorx`'s
  and `mwaitx`'s leaves, and with `--cross-check` a probe frame natively under
  gdb at every instruction.
