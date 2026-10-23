# Tier 0: the typed stream to instruction records

Tier 0 (#136) is the backend's first tier. It turns a function's typed stream into `X86Instruction` records by
expanding a template per entry. A value's register is its depth on a value stack, and the locals live in registers or
frame slots. There is no IR and no liveness analysis. `src/buster/compiler/tier0.c` holds it; `tier0_generate.c`,
`tier0_test.c` and `tier0_benchmark.c` hold the generator, the tests and the benchmark.

## Status

**Done:**

- **The lowered typed stream:** the backend contract defined here. #133's
  [semantic expression stream](c-checker.md) now exists, but the producer of
  these machine-class and executable control-flow entries remains to be built.
- **Both paths:**
  - `tier0_compile`, the vector path;
  - `tier0_compile_scalar`, its twin and oracle.
- **The interfaces to the existing modules:**
  - calls through #137's locations;
  - branch columns for #144's `size_branches`;
  - `LinkReference`s for `link_add_code_fixups`;
  - the immediates' shortest forms through #221's `x86_choose_forms`.
- **`test-tier0`,** with a cross-check against clang -O0.
- **`benchmark-backend`.**

**Waits for #133, #145 and #146:**

- the c-testsuite programs;
- the corpora and `sqlite3.c`;
- `speedtest1`;
- the whole compile's time against tcc's.

Until then, generated programs (below) stand in for #133's streams and for #142's random programs.

## The typed stream (#133 → #136)

**A function** is three columns over its `count` entries, in evaluation order (`Tier0Function` in `tier0.c` has the
exact rules):

- `kinds`: `BUSTER_TYPED_*`, below 64;
- `classes`: `BUSTER_TYPED_CLASS_*`, the machine class;
- `values`: 64 bits each.

Beside them are three tables:

- **Locals,** parameters first, each with a class and flags (`BUSTER_TYPED_LOCAL_ADDRESS_TAKEN`).
- **The parameters' locations** at entry, and the result's.
- **Calls:** each call's callee `SymbolId`, its arguments' classes and locations, whether it is variadic, and its
  `AbiCall`.

The ABI's locations come from `abi_assign`, which the front end runs once per unit over every signature. Calls are
vectorized across the unit that way, and Tier 0 needs no type table.

**Expressions** are in postorder, so each entry's last operand is the entry before it. The kinds are:

- leaves: `CONST`, `LOCAL`, `LOCAL_ADDRESS`, `GLOBAL`, `GLOBAL_ADDRESS`, `DUP`;
- unary: `NEG`, `NOT`, `CONVERT`, `LOAD`;
- binary: `ADD` to `SHR`, `PTR_ADD` (with the element's size), and the compares `EQ` to `GE`;
- stores: `STORE` and `SET` (through a pointer, with and without a value), `STORE_LOCAL` and `SET_LOCAL`,
  `STORE_GLOBAL` and `SET_GLOBAL`;
- `CALL` and `DISCARD`.

**The checker makes explicit** what C leaves implicit:

- every conversion (`CONVERT`);
- pointer arithmetic's scale (`PTR_ADD`);
- `!x` as `x == 0`;
- a statement's assignment as `SET_*`, not `STORE_*` then `DISCARD`;
- `x op= v` on a local as `LOCAL x, v, op, SET_LOCAL x`.

**Control flow** is branches and jumps, whose values are target entry indices; the entry count is the function's end:

- `BRANCH_FALSE` and `BRANCH_TRUE` pop an integer.
- `JUMP` goes to its target.
- `JUMP_VALUE` carries an arm's value of `?:`, or a value's `&&` and `||`, to the arm after it.
- `RETURN` returns at statement level.

The producer lowers conditions to jumps, the short-circuit operators straight to their targets. Wherever control
arrives at a target, the stacks hold the same values; `tier0_validate` checks that, as WebAssembly's validator does.

**The memory contract:**

- Tier 0 reads `kinds` and `classes` 64 entries at a time and `values` 8 at a time, with masked loads. It never reads
  past `count`, at page ends either.
- The columns need no padding or alignment.
- The output's records come from the caller's arena: the records, plus four for the vector expansion's last 64-byte
  store.
- Its passes' columns come from the thread's scratch arenas.
- A failure (an unsupported entry, a depth past the tables, no memory) pops what it pushed, and reports the entry.

**What Tier 0 derives itself,** under #298's producer–consumer rule:

- **The labels** (a bitset of targets): one pass over the control entries the vector path finds 64 at a time. A
  producer's bitset would cost the same to build and adds a column to keep consistent.
- **Each entry's operand class:** a shifted load of `classes`.

The rest of what fusion needs is in the stream. A unit-wide API, one stream for every function of a unit, would keep
the 64-entry batches full across small functions. That is a follow-up, measured once #133 produces real units.

## Incoming variadic frames

`Tier0Function.incoming_variadic` optionally borrows the `AbiCall` produced by
`abi_assign` for the callee's named arguments only, with `count == named_count`
and `variadic = true`. Its GP count includes a hidden result pointer where
applicable. `stack_bytes` ends the named stack arguments, including their
internal alignment; it is not rounded to an outgoing area's final alignment.
A null pointer retains the ordinary frame.

The optional prologue saves six GP argument registers and all 128 bits of eight
XMM argument registers into a distinct 176-byte incoming area before moving
parameters. Unaligned baseline SSE stores preserve the full vector slots
without reading AL or changing floating-point status. The incoming area never
shares the outgoing call-preservation area.

Three mutually exclusive local roles require `ADDRESS_TAKEN`, class U64 and a
nonparameter local. `VA_LIST` reserves exactly 24 bytes; `REGISTER_SAVE_AREA`
aliases the incoming save area; `OVERFLOW_ARG_AREA` aliases entry RSP plus the
return address and the named-only `stack_bytes`. The anchors require incoming
metadata. These descriptors support only `LOCAL_ADDRESS`; ordinary local
loads and stores are rejected. Existing varargs recipes receive the resulting
pointers and apply each requested argument's own overflow alignment.

Both compilation paths reject malformed role/ABI metadata and frame or
incoming displacements outside signed 32-bit addressing. This bounded profile
requires the ordinary 16-byte stack alignment. Shared `AbiVaList` and save-area
constants are unchanged. Source production and Tier 1 support remain separate;
the typed-to-IR consumer must reject these roles until it supports their layout.

`test-tier0-variadic-frame` prepares actual Clang-compiled callers of generated
variadic callees with GP/FP exhaustion, named stack arguments, real va_start /
va_copy / va_arg recipes, intervening calls and full vector saves. It compares
both Tier 0 paths and both encoders, guarded output edges, 24-byte list storage
and independent ordinary-function byte fixtures. Run builds and tests only on
the approved 9700X, each through its own shared lock. This description records
the test contract; qualification results are reported with the exact revision.

## The output (contract 1 of #135)

**Records:**

- a prologue: the callee-saved registers the homes or depths use pushed, `sub rsp`, and the parameters moved to their
  homes (a stack parameter with a register home loaded from its incoming slot);
- the body;
- the epilogue.

**Every branch** is `JMP_REL8` or a `Jcc_REL8` with displacement 0. Two columns beside the records give each branch's
record index (ascending) and its target's; the function's end is the epilogue's first record. Both are what
`branch_sizing_validate` accepts.

**Symbolic fields** (`[rip + global]`, `call f`) are records with `BUSTER_X86_FORM_FIXUP` and a `LinkReference`:
`PC32` or `CALL`, the target `SymbolId` and addend 0, in record order.

## The design

**Fusion.** A leaf that is its consumer's last operand is the entry right before it, so every fusion is a property of
a window of entries. The windows:

| Window | Becomes |
| --- | --- |
| a constant before an operation that takes an immediate | `op r, imm` (the immediate fits: 32 bits, or sign-extended 32 for 8-byte operations) |
| a local before an operation that takes r/m | `op r, home` (a register or `[rsp + slot]`) |
| a local and a constant before `imul` or a compare | `imul r, home, imm`, `cmp home, imm` |
| two locals before a compare | `cmp x, y` (with x or y a register; both in memory go through a scratch) |
| a pointer addition before a load | `[base + index * scale]` (scale 1, 2, 4 or 8) or `[base + c * size]` |
| a pointer addition and a constant or a local before a store | `mov [base + index * scale], imm/home` |
| a compare before a branch | `cmp …; jcc`, with no `setcc` |
| `LOCAL x, v, op, SET_LOCAL x` | `op home_x, v`: `inc ecx`, `add edx, ecx` |

A target ends every window: a jump arriving there brings a value the template wouldn't know.

**Depths.** There are two value stacks: integers and pointers, and floating values. Each entry's effect on each stack
is a table lookup, with its absorbed entries' effects folded in. The depths are the effects' prefix sums. The integer
depth table is caller-saved first, rax, rcx, rdx, rsi, rdi and r8 to r11, then the callee-saved rbx, r12 to r15 and
rbp for the rare deeper expressions. The floating one is xmm0 to xmm15.

- **Contract 6 (rax first, not rdi):**
  - a call's result and the return value are at depth 0 where they arrive and leave;
  - division's dividend and quotient are rax's at depth 0;
  - a shift's count at depth 1 is cl;
  - a leaf function's parameters stay in rdi, rsi and the others while its expressions stay shallow.

  Corpora must confirm it once #133 produces backend function records. The table is one array, so the other order is a one-line change to
  measure.
- **Values live across a call** are exactly the depths below its arguments. The call saves the caller-saved ones to
  the frame's save area and restores them; depths in callee-saved registers survive it by themselves.
- **Deeper expressions:** past 15 integer or 16 floating values live at once, the function is refused
  (`TIER0_ERROR_DEPTH`). Spilling deep values is the issue's deferred case, not written yet (see the follow-ups).

**Homes.**

1. A parameter whose register the body never touches stays where it arrives.
2. The other locals, most referenced first, take the caller-saved registers no entry touches: neither the depths
   reached, nor a template's fixed registers, nor anything if the function calls.
3. Then they take the callee-saved registers, for a local referenced at least twice (a push and a pop).
4. Floating locals take free XMM registers.
5. Address-taken locals and the rest get 8-byte frame slots, rsp-relative (no frame pointer). A stack parameter keeps
   its incoming slot, unless it took a register, which the prologue loads from that slot. No home is a register a
   parameter arrives in, so the prologue's moves and loads never overwrite one another's sources.

This is what beats clang -O0, which loads and stores every local at every use. The counting loop for `s += i` compiles
to `xor edx, edx; xor ecx, ecx; mov eax, ecx; cmp eax, edi; jge; add edx, ecx; inc ecx; jmp`.

**Templates.** A template is up to four records, one per (kind, class, variant). The variant says what the entry
absorbed (`BUSTER_TIER0_VARIANT_*`); a home-dependent slot holds four templates, one per home case. A pointer
addition's element size picks its own: `lea` for 1, 2, 4 and 8, `imul r, r, imm32` up to 2^31 − 1, and past that
(`WIDE_SCALE`) the size in the next register, as a constant is, then a 64-bit `imul`. The records' fields come from
the entry's 32-byte hole block:

- K, the constant;
- A, the fused local's home;
- B, the entry's own local's home, or a compare's first local's;
- the registers at depth −2 to 0 of each stack.

`tier0_initialize` builds 1,569 templates from the encoder's forms and compiles each into one cache line. A byte the
template fixes is that byte; a hole's is its index in the hole block. Expansion is copy and patch:

1. one load of the line;
2. one `vpermb` from the hole block;
3. one blend by the template's mask;
4. one 64-byte store at the entry's place, overwritten past its records by the next entry.

**Deferred entries** go to `tier0_emit_deferred`, which writes their records at the places measuring reserved. The
vector path's measuring counts a call's records with `tier0_count_deferred`, from its arguments' locations and the
cycles of its parallel move, without emitting it; the scalar path runs the emitter to count, which checks the count:

- Calls: saves, stack arguments, the register arguments by a parallel move (cycles broken with `xchg`), al for a
  variadic call, the call, the result moved, the restores.
- Division and shifts by a register at depths where rax, rdx or rcx hold other values (through two fixed frame slots).
- The immediates: every record with an immediate takes the shortest form that keeps its value and the flags its
  template reads (`x86_choose_forms`): imm8, the accumulator's form, `xor` for zero, `test r, r` for `cmp r, 0`, `inc`
  and `dec`.

## The two paths

**The vector path** finds the fusions, effects and depths 64 entries at a time:

1. **P0:** each value's predicates as bitsets (fits an imm32, a shift count, zero, a SIB scale, its product with the
   entry before fits, equal to the value three back).
2. **P1:** raw effects, leaf fusions and the entries' own variants, by `vpermb` lookups of kind and class tables and a
   256-byte (row, class) property table. A kind's row groups the kinds with the same properties; at most 16 rows, two
   `vpermi2b` and a blend.
3. **P2:** the compound fusions (addresses, a branch's compare, x = x op v) from P1's variants of the entries before.
4. **P3:** each entry's absorber by one pointer-jump step, and the effects folded into it.
5. **P4:** the depths, by in-register byte prefix sums.

The property tables are built from the template slots, so both paths follow one table; `tier0_initialize` refuses a
table on which they could disagree.

The passes after fusion work 8 entries at a time, on a 64-bit **word** per entry: everything they read of its template
(the template's index, length, required depth and reaches, its branch, reference and choice records, the choice's
live flags and row, and the offsets of K, its scale, A and B). A slot's word also says whether it depends on the
homes, and whether its four cases' words differ in the template alone (uniform).

1. **Selection** gathers each entry's word from its slot's (`vpgatherdq`, from the slot table's 32,768 words). The
   depth checks are masks, the depths each stack reaches lane maxima, and only the deferred entries' touches (a call's
   saves, division's fixed registers) are taken an entry at a time.
2. **Homes:** the references' count takes the referring entries of 64 at a time as a mask (`vpshufbitqmb` of their
   kinds in the four kinds' bitset), then a count per set bit, without a branch per entry. The rest works per local,
   and both paths share it.
3. **Measuring** resolves a home-dependent word to its homes' case. A local's home comes from four registers by
   `vpermt2q` for up to 32 locals, or by a gather past that. A uniform slot's case is added to the template; another's
   word is gathered. The lengths come from the words, or a deferred entry's from its count, and the positions are
   their prefix sum in registers.
4. **Emission** computes the hole blocks of 8 entries in vectors. K, the scale and the locals of A and B come from a
   `vpermt2q` over this block's values and the last block's; A and B from the homes' registers; the registers around
   each depth from two 32-entry windows by `vpermt2d`. Four `vpermt2q` interleave them into four registers of two
   blocks each. Then every lane of the block is expanded, in order and without a branch: the template's line, 32 added
   to its hole indices for the second block of a register, one `vpermb`, one blend and one 64-byte store. A lane of an
   absorbed or deferred entry, or past the last, stores too; the later lanes, the deferred entries and the epilogue
   write over it. The choice requests are compressed into their columns (`vpcompress`), their contexts read from the
   expanded records. The branches, then the references and the deferred entries, follow in entry order by their masks'
   bits.

The immediates' choice is one `x86_choose_forms` over every request.

**The scalar path** (`tier0_compile_scalar`) does the same an entry at a time, with each rule in plain code. Selection
looks each entry's slot up, measuring counts a deferred entry's records by running its emitter, and emission builds
each record field by field from the templates' specifications, not from their compiled bytes. It chooses with
`x86_choose_forms_scalar`.

## Tests

`test-tier0` (about 0.8 s, or 2.9 s with `--cross-check`, on the 9700X) generates programs of four shapes: one function
of integers, integers and calls, everything, and larger, 300 each by default. Each program:

1. Every function must pass `tier0_validate`.
2. Both paths must compile it to the same records, branch columns and references.
3. The records must pass `x86_validate` and the branch columns `branch_sizing_validate`.
4. They are sized and encoded, loaded into fresh pages and run in a child process on eight inputs. The result and every
   global's bytes must equal what the test's interpreter of the typed stream gives.

**The generator** (`tier0_generate.c`) writes each program both as typed streams and as C. Its programs have:

- parameters and locals of every class, address-taken locals and pointers to them and to global arrays; one function
  in five has 7 to 12 parameters, past the argument registers;
- assignments, compound assignments through pointers and array elements, increments, calls;
- if and else, counted while, for and do loops with break and continue, early returns;
- expressions of every operation over every class, with conversions, `?:`, `&&` and `||` as values, and compares;
- pointer arithmetic as a U64 value, with element sizes for each of its templates: 1 to 8, imul's imm32, and sizes past
  it to 2^64 − 8.

Everything in them is defined behavior, and nothing recurses: expressions are built from a worklist, and emitted and
printed by walks with explicit stacks. Their expressions stay within 12 stack slots, as a front end's do.

**The cross-check** (`--cross-check`, 60 programs of the larger shape) writes Tier 0's code as an ELF object through the
link representation and the object writer. clang -O0 compiles the same program as C, and a generated harness links
both, runs them on the same inputs and compares the results and every global. These programs keep to what C defines:
clang folds an out-of-range floating conversion of a constant its own way, so a floating value converts to an integer
only from a short's value. A floating global's elements compare as their bytes, but any two NaNs are the same: C leaves
a NaN's sign and payload to the implementation, and clang folds a constant 0.0 / 0.0 to +NaN where `divsd` makes −NaN.

**Robustness:**

- 19,200 programs over six seeds, 902,424 checks, passed.
- #303's review found two bugs the generator missed: a stack parameter's register home never loaded, and an element
  size past imul's imm32 truncated. Its programs now have both (stack parameters, and pointer arithmetic of every
  size), and before the fixes they failed dozens of runs against the interpreter.
- Mutations, all caught:
  - the vector fusion without x = x op v: 160 failed checks, on 50 programs per shape;
  - and on the passes after fusion, 200 programs per shape: emission's second blocks read as the first, 800;
  - a uniform case not added for half the lanes: 304;
  - a call's count one too high past 9 records: 29;
  - the accumulator's context inverted: 244;
  - a local's references miscounted: 749.

## Measurements

The 9700X, CPU 2, `benchmark-backend`, medians of 21 passes. Before and after vectorizing the passes after fusion,
the two builds run in turn, twice each, on the generator's programs before it gained stack parameters and pointer
arithmetic: 2,216 functions, 262,894 typed entries, 292,738 records (1.11 per entry) and 1,107,677 bytes, 3.7% of
the entries deferred, most of them calls:

| Variant | ns per entry | Cycles per entry | Instructions per entry | Cycles per record | IPC |
| --- | --- | --- | --- | --- | --- |
| Vector path | 15.5 | 85.7 | 189.4 | 77.0 | 2.21 |
| Vector path, before | 25.7 | 142.4 | 308.6 | 127.9 | 2.17 |
| Scalar path | 40.7 | 225.7 | 571.9 | 202.7 | 2.54 |
| `size_branches` | | | | 6.6 | 3.0 |
| `x86_encode` | | | | 4.4 | 3.6 |

On the current generator's programs (2,008 functions, 262,874 typed entries, 303,116 records, 1.15 per entry, 3.6%
deferred), the 9700X job measured #303's head at 89.2 cycles per entry for the vector path (77.4 per record, 196.7
instructions per entry) and 233.2 for the scalar one (202.2 per record), with `size_branches` at 6.5 and `x86_encode`
at 4.3 cycles per record. The power cap's count (#199) was 2.5 per record for the vector path, against the scalar
path's 11.1.

- **The passes after fusion:** vectorizing selection, measuring and emission, and counting calls without emitting
  them, took the vector path from 142.4 to 85.7 cycles per entry (1.66 times as fast), and 2.63 times as fast as the
  scalar path.
- **Against #115's budget:** the budget is about 32 cycles per output token for selection, allocation and encoding.
  The vector path is still about 2.7 times that; the budget is not met.
- **Where the time goes** (perf on the 9700X, each pass kept out of line), about, in cycles per entry:
  - emission 15, measuring 12 (counting calls 3 of it), selection 10 (the gather 1.3);
  - the call emitter 9, homes 9 (sorting the locals 3), applying the chosen forms 6;
  - fusion and depths (P0 to P4) 14, the frame and the prologue 6.

**Generated code against clang -O0** (60 cross-checked programs on the 9700X):

- **Run time:** 0.79 of clang's (0.70 to 0.78 on the 7940HS, over 1,310 programs of six seeds).
- **Code size:** 1.11 of clang's `.text`. These programs are full of floating constants, which Tier 0 materializes
  through a general register (up to 15 bytes) where clang loads them from `.rodata` (not counted in its `.text`).
  They are also full of constant arithmetic, which #133 will fold before Tier 0 sees it.
- **rsp-relative frame operands** take a SIB byte that clang's rbp-relative ones don't.

## Follow-ups

- **Spilling deep values:** expressions past the depth tables are refused (`TIER0_ERROR_DEPTH`). The issue defers
  them to the scalar path, with frame slots.
- **Uncovered entries:** these are refused (`TIER0_ERROR_UNSUPPORTED`) or not yet in the stream:
  - conversions between U64 and floating values;
  - aggregates and struct copies;
  - bit-fields;
  - general `va_arg` classes beyond the scalar builtin recipes, `long double` and `_Complex`;
  - variable-length arrays, inline assembly, `setjmp` and atomics;
  - `switch` (no kind yet: a compare sequence or a jump table);
  - indirect calls;
  - globals of shared libraries through the GOT (a front end's visibility decision).
- **The rest of the time:** the call emitter (an entry at a time), sorting the locals by references (an insertion
  sort that mispredicts), applying the chosen forms (`x86_choice_instruction` per request), and selection's gather
  from a table larger than L1.
- **Floating constants** from a per-unit pool (`GLOBAL` of a pool symbol, the front end's choice); and rbp-relative
  frame operands where they encode shorter.
- **Real corpora:** with #133, the vector and scalar paths on every function of the corpora; contract 6 measured on
  them; c-testsuite (#145); `sqlite3.c` and `speedtest1` (#146); #144's benchmark rows for real functions; the whole
  compile against tcc.
- **Unwind tables (#88)** for the prologue and the epilogue, which follow fixed templates.

## Boundary audit #298

Audit pin: `42630114aeb0553c3e2fabfd7fe9bc84b0ce6126`;
[scope and evidence](simd-boundary-audit.md).
[The implementation's five phases and contracts](https://github.com/buster14a/buster/blob/42630114aeb0553c3e2fabfd7fe9bc84b0ce6126/src/buster/compiler/tier0.c#L4)
are real, but the statement that [#133](https://github.com/buster14a/buster/issues/133) produces its input describes the intended
integration: at this pin the complete source producer does not exist. The
actual current producer is `tier0_generate` and the tests' manually built
`Tier0Function` fixtures. `CheckStream` is not a drop-in replacement.

| Producer → consumer | Implemented or missing contract |
| --- | --- |
| Checked C/graph → ordered typed function/ABI metadata | Missing source bridge, [#133](https://github.com/buster14a/buster/issues/133)/[#136](https://github.com/buster14a/buster/issues/136)/[#145](https://github.com/buster14a/buster/issues/145). Must lower control flow, short-circuiting and explicit conversions and retain source identity. |
| `Tier0Function` → fusion → stack depths → homes/frame | Implemented. Byte kind/class columns plus u64 value; local/call tables and ABI locations. Labels stop fusion; stack prefix sums describe execution order. |
| Fusion/depth/home columns → templates/deferred sizes → record offsets | Implemented count/prefix placement. Calls/fixed-register exceptional entries compute demand before emission, so variable output has disjoint ranges. |
| Template/hole blocks → expanded records → `x86_choose_forms` | Implemented direct 16-byte record emission. Scalar twin constructs fields independently; immediate shortening must preserve value and live flags. |
| Final records/branch columns → `size_branches` → `x86_encode` | Implemented generated-program path. Branch indices/targets stay in record coordinates until sizing; fixup markings/references retain record order. |
| Encoded bytes/fixup ends + `LinkReference` → `link_add_code_fixups` | Implemented object/executable test path; symbol identity/addends are carried, not rediscovered from instruction bytes. |

A logical batch is one complete function; kernels classify 64 entries and
read u64 values eight at a time with bounded masks. Input columns need natural
alignment, no padding; caller storage remains immutable. Output arena records
include **four extra records** for the expansion's final full store, separate
from logical record count. Scratch is thread-local, and allocation/unsupported/
depth failure restores its pushed storage. Do not hand padding records to a
consumer as real instructions. Initialization of encoder/semantics/templates
precedes use and concurrent readers.

The typed producer already knows machine classes, pointer scale, explicit
conversion, branch targets and ABI placement; omitting them would force
reconstruction. Labels are deliberately derived from control entries and the
last-operand class from a shifted load. Keeping an extra label column requires
a measured benefit and a consistency rule. Small-function occupancy motivates
a unit-batch alternative, but batching cannot fuse across function boundaries,
mix value stacks or hide per-function setup. Whole-function validation remains
the trusted-input prerequisite; malformed streams are not handled by hot loops.

Disposition: keep direct machine-record output and explicit variable-output
placement. `test-tier0 --cross-check` compares exact scalar/vector records,
branch columns and references, then sizes/encodes/executes them against its
independent interpreter and Clang-generated fixtures. This validates the
synthetic producer/consumer path, not actual C source lowering.
`benchmark-backend` separately times compilation, sizing and encoding; do not
sum its rows into a claimed measured total. [#136](https://github.com/buster14a/buster/issues/136)/[#145](https://github.com/buster14a/buster/issues/145) require source-driven
small/large full path measurements, ABI/initialization/copy/arena costs,
partial-batch/fallback counts and generated code quality. Shared IR/SSA and
Tier 1 remain [separately tracked branch integrations](simd-boundary-audit.md#pending-stage-integration).
