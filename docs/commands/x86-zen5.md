# x86-zen5

Run commands and resolve source paths from the repository root. Follow the
[shared agent rules](../../AGENTS.md), including the 9700X lock and `cores`
requirements. [Command index](README.md).

## Module overview

- `src/buster/compiler/x86_zen5.c` is the Zen 5 processor model (#181): a row of
  the 9700X's own measurements for every case (form and operand kind) Tier 0
  and Tier 1 emit, in `x86_zen5_rows.c`, deduplicated into byte IDs, with the
  scalar/AVX-512 cost-column lookups.
- `measure-x86-zen5` (`x86_zen5_measure.c`) measures the cases on the approved
  9700X and prints the rows: `--table` the whole rows file, `--check` whether a
  measurement reproduces the committed rows, `--sample N` every Nth case,
  `--only FORM/KIND` one case, `--immediate N` and `--decorations N` a fixture's
  immediate or EVEX decorations in place of its own (the sweep).
- `test-x86-zen5` checks the lookups, each record's operand kind, the table's
  completeness, and builds, validates and runs every case's fixtures that the
  host allows; `benchmark-x86-zen5 [--only scalar|vector] [--after-scalar MS]`
  times the lookups.
- test-tier0, test-tier1-select, test-tier1-emit, test-tier1-masks and
  test-tier1-pipeline check that every record they emit has a complete row.

Each build, test and measurement takes the shared host lock separately. Pin
measurements to CPU 2:

```sh
EXCLUSIVE_OWNER=NAME ~/buster-fast-bench/exclusive.sh taskset -c 2 ./build/compiler measure-x86-zen5 --table
EXCLUSIVE_OWNER=NAME ~/buster-fast-bench/exclusive.sh taskset -c 2 ./build/compiler measure-x86-zen5 --check
EXCLUSIVE_OWNER=NAME ~/buster-fast-bench/exclusive.sh taskset -c 2 ./build/compiler measure-x86-zen5 --only IMUL_R64_RM64_IMM8/REGISTER --immediate 127
```

[The model guide](../x86-zen5.md) has the columns, the harness's method and
assumptions, the coverage and how to add a case, and the 9700X record.
