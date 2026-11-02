# tier1-frame

Run commands and resolve source paths from the repository root. Follow the
[shared agent rules](../../AGENTS.md), including the 9700X lock and `cores`
requirements. [Command index](README.md).

## Module overview

- `tier1_frame.c` provides interval slot coloring and explicit frame plans for
  #177, whose steps are Tier 0's frame protocol (callee-saved registers pushed,
  one subtraction, with a frame pointer `push rbp; mov rbp, rsp` first and a
  realignment and its R15 base after the subtraction), whose native emitter
  (`tier1_frame_emit`) writes the frame's events with its records (#1056: each
  epilogue bracketed, a probe loop's CFA in r11), which the C driver's unwind
  tables resolve (the frame guide's "Unwind tables"; `test-check-debug
  --cross-check` unwinds a `--tier1` fixture, by `backtrace()` and by gdb at every
  instruction, and with `-g`).
  Its frame objects share slots by their blocks' lifetimes, which `--tier1` asks
  the checked program for (`c_check_program_lifetime.c`: a block a jump enters
  past its opening keeps its objects' whole-function lifetimes) and the IR
  carries as `lifetime` markers, colored by the CFG consumer over `dataflow.c`'s
  fixpoint on the reversed CFG (`tier1_emit_cfg_lifetimes`; the frame guide's
  "Slot sharing between objects"); `test-tier1-emit` checks shared and separate
  rows natively, and `test-checker-driver`'s lifetime fixture smaller frames
  than `--tier1-no-lifetimes`' against clang.
  `test-tier1-frame` checks independent pressure/layout/prologue oracles;
  `benchmark-tier1-frame` measures preparation, coloring, layout and total cost.
  `test-tier1-frame-execution` runs native prologues/epilogues, ABI preservation,
  variadic saves and guarded page probes. [The frame guide](../../docs/tier1-frame.md)
  records reservations before allocation and pending corpus evidence. Take the shared 9700X lock per
  build/test/measurement and pin timing to CPU 2.
