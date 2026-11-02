# tier1-schedule

Run commands and resolve source paths from the repository root. Follow the
[shared agent rules](../../AGENTS.md), including the 9700X lock and `cores`
requirements. [Command index](README.md).

## Module overview

- `tier1_schedule.c` provides an opt-in postallocation machine-record scheduler.
  `test-tier1-schedule` checks scalar/vector ordering, barriers and model coverage;
  `benchmark-tier1-schedule` measures scheduling plus encoding and a synthetic
  generated loop. [The scheduling guide](../../docs/tier1-scheduling.md) records the
  API and the source-corpus evidence still needed for #172/#181. Use the shared
  9700X lock for each build/test/measurement and CPU 2 for timing.
