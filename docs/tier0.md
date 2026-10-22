# Tier 0: the typed stream to instruction records

Tier 0 (#136) is the backend's first tier. It turns a function's typed stream into `X86Instruction` records by
expanding a template per entry. A value's register is its depth on a value stack, and the locals live in registers or
frame slots. There is no IR and no liveness analysis. `src/buster/compiler/tier0.c` holds it; `tier0_generate.c`,
`tier0_test.c` and `tier0_benchmark.c` hold the generator, the tests and the benchmark.

## Status

**Done:**

- **The typed stream:** defined here as #133's output, since #133 hasn't started.
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

## The output (contract 1 of #135)

**Records:**

- a prologue: the callee-saved registers the homes or depths use pushed, `sub rsp`, and the parameters moved to their
  homes;
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

  Corpora must confirm it once #133 exists. The table is one array, so the other order is a one-line change to
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
   its incoming slot.

This is what beats clang -O0, which loads and stores every local at every use. The counting loop for `s += i` compiles
to `xor edx, edx; xor ecx, ecx; mov eax, ecx; cmp eax, edi; jge; add edx, ecx; inc ecx; jmp`.

**Templates.** A template is up to four records, one per (kind, class, variant). The variant says what the entry
absorbed (`BUSTER_TIER0_VARIANT_*`); a home-dependent slot holds four templates, one per home case. The records'
fields come from the entry's 32-byte hole block:

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

**Deferred entries** go to `tier0_emit_deferred`, which computes their lengths before the prefix sum and writes their
records at the places it reserved:

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
table on which they could disagree. Positions are a vector prefix sum, and the immediates' choice is one
`x86_choose_forms` over every request.

**The scalar path** (`tier0_compile_scalar`) does the same an entry at a time, with each rule in plain code. It builds
each record field by field from the templates' specifications, not from their compiled bytes, and chooses with
`x86_choose_forms_scalar`.

**Shared:** both paths run the same selection (a slot lookup per entry, with the frame's touches), homes and frame,
measuring and emission. Those passes stay scalar for now, and the measurements below show they are where the time is.

## Tests

`test-tier0` (about 0.8 s, or 2.9 s with `--cross-check`, on the 9700X) generates programs of four shapes: one function
of integers, integers and calls, everything, and larger, 300 each by default. Each program:

1. Every function must pass `tier0_validate`.
2. Both paths must compile it to the same records, branch columns and references.
3. The records must pass `x86_validate` and the branch columns `branch_sizing_validate`.
4. They are sized and encoded, loaded into fresh pages and run in a child process on eight inputs. The result and every
   global's bytes must equal what the test's interpreter of the typed stream gives.

**The generator** (`tier0_generate.c`) writes each program both as typed streams and as C. Its programs have:

- parameters and locals of every class, address-taken locals and pointers to them and to global arrays;
- assignments, compound assignments through pointers and array elements, increments, calls;
- if and else, counted while, for and do loops with break and continue, early returns;
- expressions of every operation over every class, with conversions, `?:`, `&&` and `||` as values, and compares.

Everything in them is defined behavior, and nothing recurses: expressions are built from a worklist, and emitted and
printed by walks with explicit stacks. Their expressions stay within 12 stack slots, as a front end's do.

**The cross-check** (`--cross-check`, 60 programs of the larger shape) writes Tier 0's code as an ELF object through the
link representation and the object writer. clang -O0 compiles the same program as C, and a generated harness links
both, runs them on the same inputs and compares the results and every global. These programs keep to what C defines:
clang folds an out-of-range floating conversion of a constant its own way, so a floating value converts to an integer
only from a short's value.

**Robustness:**

- 24,000 programs over four seeds, 1.1 million checks, passed.
- A mutation of the vector fusion (no x = x op v) failed 160 of 50 programs per shape's checks.

## Measurements

The 9700X, CPU 2, `benchmark-backend`: generated programs' 2,216 functions, 262,894 typed entries, 292,738 records
(1.11 per entry) and 1,107,677 bytes. 3.7% of entries are deferred, most of them calls. Medians of 21 passes:

| Variant | ns per entry | Cycles per entry | Instructions per entry | Cycles per record | IPC |
| --- | --- | --- | --- | --- | --- |
| Vector path | 25.7 | 142.6 | 308.6 | 128.1 | 2.16 |
| Scalar path | 40.7 | 225.8 | 569.6 | 202.8 | 2.52 |
| `size_branches` | | | | 6.55 | 3.08 |
| `x86_encode` | | | | 4.29 | 3.65 |

- **Across states:** the vector path runs the same back to back (141.6 cycles per entry) and after 100 ms of the scalar
  path (143.3). Its throttle reads stay small: it is not power-capped.
- **Against #115's budget:** the budget is about 32 cycles per output token for selection, allocation and encoding.
  These numbers are about five times that; the budget is not met.
- **Where the time goes** (perf on the 7940HS, retired instructions of the vector path):
  - selection, measuring and emission each take about 11%, roughly 30 instructions per entry each;
  - the deferred emitter about 10%: calls, counted once to measure and run again to emit;
  - homes 5%, hole blocks 5%, and the immediates' contexts and choice about 8%;
  - the vector fusion and depths (P0 to P4) 9%, about 28 instructions per entry.

  The next steps are to vectorize selection and measuring, and to count a call's records without emitting them; see
  the follow-ups.

**Generated code against clang -O0** (60 cross-checked programs on the 9700X):

- **Run time:** 0.89 of clang's (0.71 to 0.85 on the 7940HS).
- **Code size:** 1.13 of clang's `.text`. These programs are full of floating constants, which Tier 0 materializes
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
  - `va_arg`, a variadic function's prologue, `long double` and `_Complex`;
  - variable-length arrays, inline assembly, `setjmp` and atomics;
  - `switch` (no kind yet: a compare sequence or a jump table);
  - indirect calls;
  - globals of shared libraries through the GOT (a front end's visibility decision).
- **Vectorizing the shared passes:**
  - selection (the slot lookup, by a register-resident table or gathers) and measuring;
  - counting a call's records without running its emitter;
  - the hole blocks, 8 entries at a time from the P-passes' columns.
- **Floating constants** from a per-unit pool (`GLOBAL` of a pool symbol, the front end's choice); and rbp-relative
  frame operands where they encode shorter.
- **Real corpora:** with #133, the vector and scalar paths on every function of the corpora; contract 6 measured on
  them; c-testsuite (#145); `sqlite3.c` and `speedtest1` (#146); #144's benchmark rows for real functions; the whole
  compile against tcc.
- **Unwind tables (#88)** for the prologue and the epilogue, which follow fixed templates.
