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
  carries as `lifetime` markers. The CFG consumer propagates live exits in
  groups of 512 slots before coloring (`tier1_emit_cfg_lifetimes`, described
  under "Slot sharing between objects" in the frame guide). `test-tier1-emit`
  checks shared and separate rows natively and checks 2,049 loop-local slots'
  intervals with 384 KiB of scratch. The `test-checker-driver` lifetime
  fixture checks frame sizes with and without lifetime markers and runs the
  compiled programs against clang.
  `test-tier1-frame` checks independent pressure/layout/prologue oracles,
  preparation's class numbers and event order against their definition, that
  the ordered coloring pass's colors are a pointer-jumping oracle's (#2699), and
  the default run's plans against a frozen digest (#2924: the scalar twin is
  gone, and with it `--only` and `--after-scalar`);
  `benchmark-tier1-frame` measures preparation, coloring, layout and total cost
  (`--count N` at a frame's size: a function's frames are small, and the rank
  sort takes up to 32 objects).
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
  frame sizes against `clang -O2`. Its frame-pointer column counts any `mov
  rbp, rsp`, so a Tier 1 function without a frame pointer that recomputes a
  frame address at rsp into rbp (rsp's register copy, #3149) reads as setting
  one; its frame and state are unchanged.

## Lifetime planning on deep trees (#1355)

Only a nested block can own lifetime entries. If the token roles contain no
`PARSE_ROLE_CLOSE_BLOCK`, the checker skips the lifetime columns and walks.
Parameters and locals in the function body's own scope live for the whole function.

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

The planner borrows the `count + 1`-entry `lifetime_nearest` scratch column,
disjoint from its inputs and retained `lifetime_first`/`lifetime_next`
outputs. It uses the column first for nearest-block facts, then for counts.
`check_program_lifetime_entered` initializes it again, so no planning fact
escapes into the jumps' analysis. Unknown facts are `NONE`; a resolved missing
scope is `count`, and resolved depth counts include zero.

The blocks a jump enters past their opening brace
(`check_program_lifetime_entered`) no longer walk from each label or case to
the stamped ancestors of its jump, which was quadratic in an else-if chain's
depth. A block's tokens are one run from its `{` to its `}`, so a jump enters
a block with locals around its target exactly when its source (the goto, the
case's switch, the asm goto's statement; no token for a label whose address is
taken or a case without its switch) lies outside that run, below the target's
function body. A memoized pass (`check_program_lifetime_nearest`, the
planner's kind) gives each token its innermost block with locals. Each jump
folds its source into its target's block in a second `count + 1` scratch
column, `lifetime_sources`: forward jumps (source before target) as the least
source, then backward ones as the greatest. A sweep from the last token back
meets each block at its `{`, after every block inside it, passes its least
source outward and leaves the block out when that source precedes the `{`; a
sweep from the first token meets each block at its `}` and leaves it out when
its greatest source follows the `}`. The jumps' scan and both sweeps read 64
tokens' kinds, roles and address marks a step as byte masks (the last chunk's
lanes past the unit masked off) and visit only the set lanes: the gotos, the
labels whose address is taken and the braces, the backward sweep's from the
highest lane down. The work is linear in the tokens and the jumps, and the
blocks left out are the old walks' exactly.

The same pass then leaves in `lifetime_nearest` each token's innermost block
that kept its lifetimes, past function bodies, and the control writer's ends
for a break, continue or goto (`check_control_lifetimes_left`) follow that
chain (`CheckControlWriter.lifetime_nearest`): they visit the blocks they end
and the one where they stop, nothing for a jump with no such block around it.
A goto still stops at the body's closing brace or at the first such block that
holds its label. A break or continue stops at the first of its loop or switch
(the ancestor whose stop or repeat is its label) and the body's closing brace;
the first jump to each label finds that ancestor by the old walk, and the
writer keeps it by label (`CheckControlWriter.lifetime_stops`, a label's room
in the controls arena under `--tier1`, which `check_control_label` clears as
each body numbers its labels from 0 again), since every jump to a label is
inside that label's loop or switch. The chain ends at the first block with
lifetimes at or above the stop, so the entries and their order are the old
walk's.

`CheckProgramOptions.lifetime_work` counts the walks' steps: the parent paths'
climbs and retraces (two a token at most in each of the two passes), the walks
to a break's or continue's loop or switch (one a label), and the writer's
walks along the chain (`CheckControlWriter.lifetime_steps`, the ends a jump
makes and one more). `test-checker` includes `c_check_program_lifetime_test.c`:
small forests with permuted token IDs and no retained postorder compare
complete local lists and capacity against independent ancestor walks,
including body boundaries and excluded declarations. Two 16,384-level
fixtures, with and without locals, use closed-form expectations. 600 random
forests whose heads come before their statements, with gotos into and out of
blocks, computed gotos, labels whose address is taken, cases of their switch,
another token or none, asm gotos, statement expressions' blocks and functions
inside blocks, compare the blocks left out with the old stamped walks, kept in
the test as the oracle, and each body's jumps, in a random order, with the old
writer walk, its labels numbered by `check_control_label`; the parent paths
take four steps a token at most, and a jump to a label whose stop is kept
walks only the blocks it ends and one more. Six generated sources (goto out of
and into else-if chains' blocks, cases in nested blocks with locals, labels
whose address is taken, asm gotos, and breaks and continues of for, while, do
and switch at depth), at 40 arms and at 1,024, and a source of several
functions (loops whose labels have the same numbers, the second inside a block
with locals; breaks, continues and gotos in statement expressions, nested
ones, a for's condition and increment and a do's condition; cases in nested
blocks with locals; computed gotos) compare the checked program's
`lifetime_first` and every jump's lifetime ends with the same walks, and bound
`lifetime_work` by eight steps a token, one a jump and one an end. The
existing driver frame/lifetime fixture retains native semantic coverage.

The step counts are deterministic and host-independent. At 1,024 arms
(`test-checker --part 5/8` prints them), the new walks' steps against the old
walks' (main's, kept in the test as its oracles, which count their steps):

| source | tokens | new steps | old steps |
| --- | ---: | ---: | ---: |
| goto out of the arms | 37,905 | 154,692 | 4,212,736 |
| goto into other arms' blocks | 46,104 | 185,440 | 4,543,453 |
| cases in nested blocks | 64,527 | 264,252 | 4,224,000 |
| asm goto into other arms | 39,951 | 159,804 | 1,349,852 |
| breaks and continues | 142,361 | 596,081 | 5,295,107 |

The new count is about four a token, the parent paths', which is more than
the old walks' on short chains (the 40-arm breaks' source: 23,393 steps, the
old walks 10,043). From 1,024 arms to 2,048 (the same sources) it doubles,
where the old one quadruples (the gotos out: 309,316 steps, the old walks
16,814,080). No timing here is the issue's acceptance: approved-host (9700X,
Release, under the lock) measurements of #1355's source chains remain
required before claiming the issue's scaling or wall-time acceptance,
comparing main's and this planner's `check-program` stage under
`-w -c --tier1 --time-stages` on else-if chains of 4,000 to 32,000 arms that
leave each arm's block by goto, break or continue, reach a case down the
chain, take a label's address or leave by asm goto.

Two parts stay scalar, by their order: the memoized parent paths, whose every
step loads the address of the next (pointer jumping would read every token
once per doubling of the depth, against two reads a token here), and each
block's fold at its brace, which reads the folds of the blocks inside it. The
coding style asks for a part that stays scalar to say why with measurements
on the 9700X; those measurements (the share of `check-program` in
`check_program_lifetime_nearest` and in the folds, on the chains above) are
also still to take. Unchanged and still depth-dependent:
`check_control_leave`'s walk for blocks with variable-length arrays (which
runs without `--tier1` too) and `check_control_loop`'s for a break or
continue with cleanups to run. The break chain's `check-input` stage
(`check_input_resolve_range`, quadratic in the chain's breaks) and the Tier 1
backend's `tier0` stage are outside this planner.

