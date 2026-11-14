# tier1-schedule

Run commands and resolve source paths from the repository root. Follow the
[shared agent rules](../../AGENTS.md), including the 9700X lock and `cores`
requirements. [Command index](README.md).

## Module overview

- `--tier1-schedule=none|post|pre` (with `--tier1`) schedules Tier 1's code
  block by block: `post` after allocation, on each block's machine records
  (`tier1_schedule.c`, applied by `c_check_tier1_schedule.c`), and `pre` before
  selection, on each block's IR instructions (`tier1_schedule_ir.c`). Both are
  measurement options, off by default; `--tier1-report`'s `schedule_blocks` and
  `schedule_moved` count what they did, and `--verify` checks every scheduled
  block against its scalar reference. [The scheduling guide](../../docs/tier1-scheduling.md)
  has the contracts, the 9700X's measurements and #172's decision.
- The operand sink (#2592, `tier1_schedule_ir_sink`) runs on every `--tier1`
  function: a phi's next value moves past the last reader of the value it comes
  from, whose register it then takes. `--tier1-report`'s `sunk` counts its
  expressions; `--verify` checks its order with `tier1_schedule_ir_verify`.
  `test-ir-optimize` runs its IR fixtures (`tier1_sink_ir_test.c`) by both
  paths, which must agree, through the verifier and the independent executor,
  with refusals; `tests/driver/tier1_patterns.c`'s fetch checks Lua's
  `i = *(pc++)` natively. [The allocation guide](../../docs/tier1-allocation.md)
  has the rule and its measurements.
- `test-tier1-schedule [--seed N]` checks the postallocation scheduler's
  scalar/vector twins, dependencies, barriers, flags and the verifier;
  `benchmark-tier1-schedule` times scheduling plus encoding and a synthetic
  generated loop. `test-checker-driver`'s scheduling fixture compiles a program
  by both paths with each variant under `--verify` and runs it against clang's
  build. Use the shared 9700X lock for each build, test and measurement, and
  CPU 2 for timing.
- The measurement: `measure-tier1-programs ... --variant --tier1-schedule=post
  --variant --tier1-schedule=pre` ([the tier1 guide](tier1.md)) for speedtest1's
  and Lua's run time, and `measure-compile --variant --tier1-schedule=post --
  ARGUMENT...` ([the compile-time guide](compile-time.md)) for what each variant
  adds to a compile.
