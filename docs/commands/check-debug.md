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
