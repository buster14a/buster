# check-debug

Run commands and resolve source paths from the repository root. Follow the
[shared agent rules](../../AGENTS.md), including the 9700X lock and `cores`
requirements. [Command index](README.md).

## Module overview

- `src/buster/compiler/c_check_debug.c` is the C driver's `-g` (#395): each
  typed entry's code start after branch sizing (`Tier0Code.entry_records`) and
  the token whose lowering wrote it (`CheckProgramLowered.entry_tokens`) make
  `check_program_emit`'s raw rows, the driver maps their tokens to presumed
  lines as the diagnostics place them, bounded per function by its
  declaration's and closing brace's places, and keeps a row per change of line
  in the function's file, and the body's start; each pass a vector path with
  an exact twin (`--scalar`). The lowering and its loop over tasks are
  compiled twice, without the tokens (`check_program_lower`,
  `check_lower_drain`) and with them (`check_program_lower_sources`,
  `check_lower_drain_sources`), so that a compile without `-g` runs the code
  it ran before.
  `-c -g` writes the same unit into the object, relocated.
  `test-check-debug [--cross-check]` checks the twins at `PROT_NONE` pages,
  the lines against `pp_presumed` on fixtures (a computed goto among them)
  and generated programs, that lowering with the tokens changes no typed
  function, and with `--cross-check` the executable through
  `llvm-dwarfdump --verify` and gdb, and the `-c -g` object, the executable's
  unit relocated, linked by `cc` through GNU ld and lld and by `link-foreign`,
  the same way; and the unwind tables (`c_check_unwind_test.c`): a fixture's
  `backtrace()` through a variable-length array's function, a computed goto's,
  one with values live across its call, one of 128-bit integers and two in
  realigned frames (an array aligned to 64, and one aligned to 4096 beside a
  variable-length array, with a base), by default and with each option
  that keeps or drops the tables, by each path, as executables and as objects
  linked three ways; the options' rule against clang's objects of the fixture;
  and gdb's backtrace at every instruction of each function, whose steps must
  reach every instruction of gdb's disassembly of it; and a `SIGILL` handler's
  `backtrace()` from `__builtin_trap()`'s helper, a `ud2` alone, to `main`
  (#523: `unwind_tier0_frame` takes a frameless function's `ud2` for its ret);
  and a `--tier1 --verify` fixture's Tier 1 frames (#177: spill slots alone,
  pushes, two returns, an open frame, a realigned one, open and with two
  returns too, a red-zone leaf, the shapes `--tier1-report`'s counts show and
  each function's FDE rows, by `readelf -wF`), walked by `backtrace()` by each
  path, by `--lanes 1` and 8 into one lane's bytes and linked by GNU ld and lld,
  and by gdb at every instruction.
  [The debug guide](../../docs/debug.md) ("The C driver's `-g`") has the details.

## Optimized values (#1114)

`c_check_debug_tier1_values_test.c` adds the Tier 1 value cross-checks. Its
separate support unit makes object addresses escape and supplies explicit
checkpoints; GDB stops at the checkpoint's exact entry and reads the caller's
frame. Expected values match the fixture's own output, including mutations,
inner/outer shadowing, two loop iterations, aligned objects, preserved load
snapshots and reassigned call results. Exact-entry ABI parameters, later
literal values and a computed straight-line local have separate stops. The
straight-line fixture computes `input * 3 + 2`, uses that result twice, and
requires both the exact intermediate value at its return line and the final
native result. Inlined callee locals must not appear as
caller locals. The compile requires `--tier1 --tier1-strict --verify` and an
actual inline expansion.

The branch and assignment-loop fixtures keep scalars in the optimized value
path without taking their addresses. Both branch arms have a checkpoint and
another at their semantic-phi join: input 10 requires 32 on the left path and
47 on the right. A loop with a parameter bound checks `(turn, value, total)`
as `(0, 6, 0)`, `(1, 13, 6)` and `(2, 28, 19)`, then value 28 and total 47 at
exit. Real scalar reads after the checkpoints keep each value relevant to the
program; neither source names nor stale values from a preceding iteration can
satisfy these positive checks.

The bounded variant set covers default, scalar, pre/post scheduling,
frame-pointer and no-rematerialization native builds, and a `-c -g` object
linked by GNU ld, lld and `link-foreign`. Each native variant compares its
allocated sections and entry point with a compile without `-g` (the build-ID
note is excluded); one/eight-lane builds must be byte-identical. Every debug
image passes through `llvm-dwarfdump --verify`, native execution and GDB.
GDB must include Python support: a small command wrapper records each failed
read and continues to the remaining checkpoints. The harness still requires
every positive value, no read-error marker, successful GDB exit and the final
completion marker, so a failed read cannot turn into a passing partial run.
The scalar entry and literal checkpoints may resolve to the same PC after
scheduling. The script compares the current PC with GDB's resolved source-line
locations before continuing, so both value checks still run at that stop.

The ordinary `test-check-debug` path includes byte-range grouping, malformed
and overlapping intervals, encoded zero-width ranges, patchable entry shifts,
and arena rollback. `c_check_debug_tier1_parameters_test.c` is included after
the Tier 1 producer so its machine fixtures can call the real helpers. Its
hand-specified PC tables cover narrow/full register copies, floating carriers,
partial writes, calls, diamonds, loop backedges, unreachable code and final
record permutations. Its shared-object fixtures check complete versus partial
initialization, exact source-object identity, lifetime pairing, foreign
overlap, indexed writes, delayed openings, early closings and source gaps on
both scalar/vector paths. Legal motion across an original fallthrough block
boundary may shrink or remove a shared home without rejecting the program;
malformed block metadata still fails transactionally.
`c_check_debug_tier1_bindings_test.c` runs the same parameter tables through
the sparse binding constructor and publisher, plus explicit private-row
spill/reload, call, partial-overwrite and addressable-row cases. Assignment
tables require full RHS completion, reject an interleaved foreign result
overwrite, and forbid older source origins from reviving stale values.
The freshness fixture also walks a concrete backedge twice, with old phi and
RHS copies in private frame rows. Both rows must disappear at the new entry,
the current phi must select its checked register, and the next RHS stays
unavailable until its definition completes, while the source origin remains
inside the same witnessed window. Reexecuting the definition independently
must also remove its older same-ID spill.
Full-emission certificate mutation tests live in
`test-tier1-emit` ([the certificate command guide](tier1-certificate.md)).
