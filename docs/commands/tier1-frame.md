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
- `-fno-omit-frame-pointer` and `-momit-leaf-frame-pointer` reach both tiers'
  frames (the frame guide's "The frame pointer on request"); `test-checker-driver`'s
  frame pointer fixture walks the rbp chain through them.
- `-fstack-clash-protection` makes both tiers' frames and dynamic allocations
  probe each page (the frame guide's "Stack-clash protection on request");
  `test-checker-driver`'s stack-clash fixture faults on a guard page through
  them where clang -O0's build does, and steps over it without the option.
- `measure-frames [--functions] LABEL=FILE...` (`frame_measure.c`) reads each
  function's frame from an ELF object's or executable's code: its pushes and
  `sub rsp` along the control flow's paths (each instruction's depth its
  predecessors'), at their deepest. A function whose paths join at different
  depths is `unresolved`, and one whose jump table's targets take their
  indirect jump's depth `assumed`. It compares each file's frames with the
  first's over the functions both have, leaving the unresolved out: #177's
  frame sizes against `clang -O2`.
