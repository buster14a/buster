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
  `test-tier1-frame` checks independent pressure/layout/prologue oracles, and
  that both colorers give identical slots and offsets (#2606);
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
  Its two-page-aligned frame and dynamic outgoing call area must use Tier 1
  under `--verify` (#1450). `test-tier1-emit` also executes outgoing-area
  probes on a controlled stack, checking preserved arguments, exact release,
  intermediate and final partial guard pages, and bounded output capacity.
- `measure-frames [--functions] LABEL=FILE...` (`frame_measure.c`) reads each
  function's frame from an ELF object's or executable's code: its pushes and
  `sub rsp` along the control flow's paths (each instruction's depth its
  predecessors'), at their deepest. A function whose paths join at different
  depths is `unresolved`, and one whose jump table's targets take their
  indirect jump's depth `assumed`. It compares each file's frames with the
  first's over the functions both have, leaving the unresolved out: #177's
  frame sizes against `clang -O2`.

## Lifetime planning on deep trees (#1355)

`check_program_lifetime_blocks` reads immutable parent/role columns and
memoizes each token's nearest enclosing block, resetting at function bodies.
It then links local declarations in token order. A second memoized pass counts
enclosing blocks with locals and sums those counts at jump tokens to reserve
controls. Each path stops at a cached answer and retraces only its previously
unknown nodes; each node is filled once per pass. Total planning work is linear
in the tokens, including a chain with many returns. With no blocks containing
locals the second pass is omitted. No retained parser postorder is required:
the C driver uses tree-only parsing, and parent token IDs may precede or follow
their children.

The planner borrows the existing `count + 1`-entry `lifetime_stamps` scratch
column, disjoint from its inputs and retained `lifetime_first`/`lifetime_next`
outputs. It uses the column first for nearest-block facts, then for counts.
`check_program_lifetime_entered` initializes it again before bypass stamps,
so no planning fact escapes into goto analysis and no extra allocation is
introduced. Unknown facts are `NONE`; a resolved missing scope is `count`,
and resolved depth counts include zero.

`test-checker` includes `c_check_program_lifetime_test.c`: small forests with
permuted token IDs and no retained postorder compare complete local lists and
capacity against independent ancestor walks, including body boundaries and
excluded declarations. Two 16,384-level fixtures, with and without locals,
use closed-form expectations. The existing driver frame/lifetime fixture
retains native semantic coverage. Goto/case bypass walks and the control writer
are unchanged and may still have depth-dependent work. Approved-host
measurements of #1355's source chain remain required before claiming the
issue's scaling or wall-time acceptance.

