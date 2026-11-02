# branch-sizing

Run commands and resolve source paths from the repository root. Follow the
[shared agent rules](../../AGENTS.md), including the 9700X lock and `cores`
requirements. [Command index](README.md).

## Module overview

- `src/buster/compiler/branch_sizing.c` is branch sizing (#144): the rel8 or
  rel32 form of each branch inside a function, chosen before the function is
  encoded, from `x86_measure`'s offsets and rounds over the branches alone
  (`size_branches`, its scalar twin `size_branches_scalar`, and
  `branch_sizing_validate`, the checks the two leave to the code generator, as
  `x86_encode` leaves `x86_validate`'s; `branch_sizing_validate_branches` and
  its twin are those checks but `x86_validate`, the branch columns alone,
  which the C driver's emission runs on Tier 0's valid records, keeping
  `x86_validate` for `--verify`, #532). The scalar path gets its initial
  offsets from `x86_measure_scalar`; neither sizing path retains machine
  code. The former encoded-byte scratch column, chunk-size setting and chunk
  rebasing are removed. `branch_sizing_generate.c` generates
  functions for its tests and benchmark: shaped like clang's -O0 and -O2 code,
  and chains of branches that take a round each. `branch_sizing_test.c` holds
  its tests (`main.c` includes it after `x86_64_encoder_test.c`, whose random
  instructions and printer it takes; `./build/compiler test-branch-sizing`
  runs them) and `branch_sizing_benchmark.c` provides
  `benchmark-branch-sizing`. `docs/branch-sizing.md` explains its design and
  measurements.
## Branch sizing tests

`./build/compiler test-branch-sizing [--seed N] [--count N] [--cross-check]
[--keep]` (about 1.4 s on the 9700X, and 2.5 s with `--cross-check`) checks
`branch_sizing.c`. Every function it sizes must be one `branch_sizing_validate`
accepts, as the code generator's are, and so must both paths of the branch
columns' check (`branch_sizing_validate_branches` and its twin), and both
sizing paths must give identical
results: the records, the offsets, the length, the rounds, the long branches
and the worklist's evaluations. Each result must also be the test's reference's
(relaxation over the records, as an assembler does it, with each record's
length from `x86_encode_scalar`), pass `x86_validate`, have
`x86_encode_scalar`'s offsets and length, aim every branch at its label's
record, and be long exactly where a rel8 can't reach:

- forms: every form number as a branch's record, with and without a segment
  override or the fixup flag: `branch_sizing_validate` accepts `JMP_REL8` and
  the 16 `Jcc_REL8` (found from the form table's opcodes), which grow by 3 and
  4 to the rel32 forms after them, and refuses every other, and so do both
  paths of the columns' check (the vector path's bitmap of the forms against
  the scalar path's list);
- errors, `branch_sizing_validate`'s: a branch's index equal to the one
  before, below it, at the count and past it, a target past the count, a form
  that isn't a branch's, a branch's form with FS, GS or the fixup flag, at
  every place among 40 branches (the first error is the one reported), more
  branches than records, more records than `x86_encode` takes (which both
  paths refuse too), and a record `x86_validate` rejects, after a branch's
  error; both paths of the columns' check must give each but the last, which
  they take;
- boundaries: a probe branch, forward and backward, jmp and jcc, at every
  displacement from 100 to 135 and from −140 to −105 with every branch short,
  over 0 to 3 branches that grow or don't: it must end long exactly when its
  displacement with theirs is out of rel8's range;
- random functions (`--count N`, 1,500 by default) of up to 100,000 records,
  a branch every 1 to 12, at uniform and compiler-like distances, with
  `branch_sizing_generate.c`'s filler or random instructions of every form;
  also with the worklist from rounds 1, 2 and 3 and with no round limit,
  which must give the same records and offsets;
- columns: 4,000 random functions of up to 1,200 records broken 0 to 3 times
  at random (an index, a target, a branch's form, or a record `x86_validate`
  rejects): both paths of the columns' check must give
  `branch_sizing_validate`'s first error, or take the function where only
  `x86_validate` refuses it;
- chains (`branch_sizing_chain`) of 1 to 1,000 links, forward and backward,
  which take a round per link: up to `BUSTER_BRANCH_SIZING_ROUNDS`, then the
  worklist;
- page edges: the records, the branch columns and the offsets each ending at a
  `PROT_NONE` page, for every count up to 70 records at every branch density
  and every branch count up to 40, checked by both paths of the columns'
  check and sized: a read past an end faults, and the command then names the
  case and exits with status 1;
- memory: a function sized with no scratch memory to spare, then 64 bytes
  more at a time until it succeeds: every failure must be
  `BRANCH_SIZING_ERROR_MEMORY` and leave the records and offsets as they were.

`--cross-check`, which needs GNU as, prints 300 random functions, the
boundaries' probes and the chains as GNU as source, each in a section of its
own with a label at every target, and GNU as's relaxation of them (every
branch short at first, then growing the ones out of range) must give the same
bytes as `x86_encode_scalar` of the sized records; an object that can't be read,
or a function left uncompared, fails the check. The files go in a directory
under `$TMPDIR` (or `/tmp`), removed unless `--keep` or a failure. `--seed N`
changes the generated functions. The 9700X job's test step runs it with
`--cross-check`.

## Branch sizing benchmark

`./build/compiler benchmark-branch-sizing [--seed N] [--rounds N] [--only ROW]
[--measure NAME] [--samples N] [--after-scalar MS]` sizes pools of about 262,000
records of generated functions per row: functions of 40, 250, 2,000 and 10,000
records shaped like clang's -O0 code (tier 0's) and -O2 code
(`docs/branch-sizing.md` has the statistics they follow), a mix of the function
sizes of sqlite3 at -O0, and chains of 1,000 links forward and backward. After
checking that every way of sizing them gives the same records and offsets, it
reports medians of 21 samples, in nanoseconds per record with p10/p90 and in
cycles per branch and per record with IPC, of `x86_encode` of each function
(the caller's encode after sizing, the reference), `size_branches`,
`size_branches_scalar`, and `size_encode` (vector sizing followed immediately
by final encoding in the same timed interval),
and each path's phases as the difference between runs that stop after a phase
and before it: the vector path, the scalar path, and the vector path with each
alternative to its gathers measured against it (`compress` and `window`). A sample sizes the pool
in groups of at least 2,048 records, each group's records read into cache and
its branches' records put back outside the time (as generated, or as sized for
the reference encode: every whole run must come to the sized functions'
lengths), around `CLOCK_MONOTONIC_RAW` and reads of this thread's cycles and
instructions (rdpmc where available), with measured read overhead subtracted.
Nanoseconds are reported even when hardware counters are unavailable; cycles
and IPC then read as zero. The default phase table includes both nanoseconds
and cycles per branch. Both encoding rows produce offsets without collecting
fixup ends. The pools are larger than the branch predictor learns (a pool of
33,000 records sized again and again let it learn the scalar path's branches).
`--rounds N` sizes with another round limit, `--only ROW` runs one row, and `--measure NAME`
one measurement (`x86_encode`, `size_branches`, `size_branches_scalar`,
`size_encode`, or a path's run through a phase, as `vector-lengths` or
`vector-setup`) for `--samples N` samples, for perf. Nonzero `--after-scalar MS`
requires one isolated vector measurement and runs its scalar counterpart for
at least MS milliseconds before each recorded sample, outside the timed
interval (at most 1,000 ms; zero disables it). For `size_encode`, that is
scalar sizing followed by scalar encoding. The scalar lengths phase now uses
`x86_measure_scalar`; old scalar sizing timings used the vector encoder and
are not a baseline for the vector optimization. Compare old and new vector
paths with the same harness, and include `size_encode` for the caller's
combined cost. Run it under the 9700X's lock with `taskset -c 2`.
`docs/branch-sizing.md` explains the current method and labels the older
numbers as historical;
[measurement record](https://github.com/buster14a/buster-performance/blob/a00d16e47b6f1090b8a0424ab0dd8cb4e6016713/docs/measurements/x86-lengths-2026-10-23/refinement.md) records the current
Clang 23 implementation, comparison protocol and initial candidate's evidence.
The default 9700X job runs the benchmark after the tests, pinned to one allowed
CPU. For relevant pull requests it also runs three matched process pairs using
the same harness, including the sizing-and-encoding pipeline on one and eight
distinct physical cores. Its measurements are informational; correctness,
report completeness and matching workload identities are checked.
