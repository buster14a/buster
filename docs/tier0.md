# Tier 0: the typed stream to instruction records

Tier 0 (#136) is the backend's first tier. It turns a function's typed stream into `X86Instruction` records by
expanding a template per entry. A value's register is its depth on a value stack, and the locals live in registers or
frame slots. There is no IR and no liveness analysis. `src/buster/compiler/tier0.c` holds it; `tier0_generate.c`,
`tier0_test.c` and `tier0_benchmark.c` hold the generator, the tests and the benchmark.

## Status

**Done:**

- **The lowered typed stream:** the backend contract defined here, which #133's
  producer (`c_check_program_lower.c`, [the checker guide](c-checker.md)'s "Backend handoff") writes from checked C.
- **Both paths:**
  - `tier0_compile`, the vector path;
  - `tier0_compile_scalar`, its twin and oracle.
- **Objects in the frame:** local arrays, structs and unions at their alignments, to 16.
- **Indirect calls,** through `r11`.
- **Block copies** (`COPY`) of up to 256 bytes, a struct's assignment.
- **Structs and unions passed and returned by value** (#137), in registers, on the stack and through a hidden pointer,
  from #137's locations ("Structs and unions by value", below), and **variadic functions' incoming frames** (#356).
- **Variable-length arrays:** allocations on the stack (`ALLOCA`) and the stack's position saved and restored
  (`STACK_SAVE`, `STACK_RESTORE`), in a function with a frame pointer.
- **Symbols reached otherwise than directly** (#136's item 4): through their GOT slots (a shared library's data, and a
  function's address that may be one) and thread-locals by local-exec and initial-exec, from an optional access column.
- **Long double** (#136's item 8): the x87's 80-bit values, in memory, passed on the stack and returned in st0, and
  structs returned in x87 registers ("Long double", below).
- **128-bit integers** (#136): `__int128`'s and `unsigned __int128`'s classes, I128 and U128, whose values live in the
  slots long double's do ("128-bit integers", below). #133's producer doesn't lower them yet.
- **Conversions between U64 and the floating classes** (#136's item 5), which SSE has no instruction for before
  AVX-512 ("Conversions between U64 and floating values", below).
- **Values past the depth tables** (#136's item 6): more than 15 integer (14 with a frame pointer) or 16 floating
  values at once, among them calls of up to 64 arguments, in frame slots ("Deep values", below).
- **The interfaces to the existing modules:**
  - calls through #137's locations;
  - branch columns for #144's `size_branches`;
  - each typed entry's first record (`Tier0Code.entry_records`, the epilogue's after the last), when the producer asks
    for it (`Tier0Function.entry_records`; a compile without it allocates and copies nothing for it), which with branch
    sizing's offsets says where each entry's code starts: the C driver's source rows for `-g` (#395,
    `c_check_debug.c`);
  - `LinkReference`s for `link_add_code_fixups`;
  - the immediates' shortest forms through #221's `x86_choose_forms`.
- **`test-tier0`,** with a cross-check against clang -O0.
- **`benchmark-backend`.**

- **c-testsuite (#145):** all 220 of its programs compile through #133's producer and Tier 0, link by the direct path
  and run right, with both paths' executables the same bytes and every record passing `x86_validate`
  ([the c-testsuite guide](c-testsuite.md)).

**Waits for #133, #145 and #146:**

- the corpora and `sqlite3.c`;
- `speedtest1`;
- the whole compile's time against tcc's.

Until then, generated programs (below) stand in for #142's random programs.

## The typed stream (#133 → #136)

**A function** is three columns over its `count` entries, in evaluation order (`Tier0Function` in `tier0.c` has the
exact rules):

- `kinds`: `BUSTER_TYPED_*`, below 64;
- `classes`: `BUSTER_TYPED_CLASS_*`, the machine class;
- `values`: 64 bits each.

Beside them are three tables, and optional parts for aggregates and for the symbols' access:

- **Locals,** parameters first, each with a class and flags (`BUSTER_TYPED_LOCAL_ADDRESS_TAKEN`). Two optional columns
  make some of them **objects** in the frame: arrays, structs and unions.
  - `local_sizes`: an object's size in bytes, 0 for a value's local.
  - `local_alignments`: its alignment's log2, at most 4 (16 bytes).

  An object:
  - is a local of class VOID with `ADDRESS_TAKEN`, and a parameter only when it is a struct or union passed by value
    ("Structs and unions by value", below);
  - is reached only through `LOCAL_ADDRESS`: its elements and members are loads and stores through that address
    (`PTR_ADD`, `LOAD`, `SET`), as a front end lowers `a[i]` and `s.m`;
  - with the others, takes at most `BUSTER_TIER0_MAX_OBJECT_BYTES` (1 GB), so that every frame displacement fits a disp32.

  A function without objects leaves both columns 0.
- **The parameters' locations** at entry, and the result's.
- **Calls:** each call's callee `SymbolId`, its arguments' classes and locations, whether it is variadic, and its
  `AbiCall`. A callee of 0 makes the call indirect: the stream pushes the address it calls (U64) before its
  arguments, and the `CALL` pops it after them.
- **Aggregates by value,** three optional parts: the arguments' sizes, the objects that receive the calls' results,
  and the function's own result's size ("Structs and unions by value", below).
- **The symbols' access** (optional, see "Symbols outside the image" below): `symbol_access[s]`, how the code reaches
  `SymbolId` s, for each of `symbol_count` symbols. Without it every symbol is direct.

The ABI's locations come from `abi_assign`, which the front end runs once per unit over every signature. Calls are
vectorized across the unit that way, and Tier 0 needs no type table. They name the convention's registers alone, and
Tier 0 takes no others: a call's arguments and a function's parameters in rdi, rsi, rdx, rcx, r8, r9 and xmm0 to xmm7,
none in a register another argument of the call or another parameter takes, and results in rax, rdx, xmm0 and xmm1.
The emitters rely on it: the prologue sets a frame pointer's rbp and copies through rax and xmm15 before the parameters
move, and doesn't save a register a parameter stays in; a call sets al and calls through r11; the epilogue loads the
result before it pops the callee-saved registers.

**Expressions** are in postorder, so each entry's last operand is the entry before it, and both paths read that
operand's class from the entry before (its stack, and the entry's template key and variant). `tier0_validate` refuses
an entry whose last operand has another class. A value left under a store of its class, as an increment's `DUP` leaves
the old one under its `SET`, has it too, and is compiled right. A call reads its arguments' classes from the calls
table. The kinds are:

- leaves: `CONST`, `LOCAL`, `LOCAL_ADDRESS`, `GLOBAL`, `GLOBAL_ADDRESS`, `DUP`;
- unary: `NEG`, `NOT`, `CONVERT`, `LOAD`;
- binary: `ADD` to `SHR`, `PTR_ADD` (with the element's size), and the compares `EQ` to `GE`;
- stores: `STORE` and `SET` (through a pointer, with and without a value), `STORE_LOCAL` and `SET_LOCAL`,
  `STORE_GLOBAL` and `SET_GLOBAL`;
- `CALL` and `DISCARD`;
- `COPY`: `value` bytes, 1 to 256, from the address on top to the one under it, the ranges the same or apart (a
  struct's assignment). A front end copies more through `memcpy`, as clang -O0 does past 16 stores of 16 bytes.
- the stack, for variable-length arrays, each U64 with no value:
  - `ALLOCA` pops a byte count, rounds it up to 16, takes it from the stack and pushes the allocation's address, a
    multiple of 16;
  - `STACK_SAVE` pushes the stack's position, and `STACK_RESTORE` pops one, releasing what was allocated after it.

  A producer saves the position into a local before a block's first array, and restores it from there at the block's
  end and before every jump out of the arrays' scope (a loop's next iteration is one), as clang does. A return needs
  none: the epilogue releases the frame. That a restored position is an earlier save's is the producer's to keep.

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
- `JUMP_INDIRECT` pops an address (U64) and goes there: GNU C's `goto *p`, at statement level. Its targets are the
  function's labels whose addresses the code takes (`&&label`), an optional column, `label_entries` (`label_count`
  entry indices, each a statement's start or the count). Tier 0 reports each one's first record
  (`Tier0Code.label_records`); the front end defines the label's symbol there, after branch sizing, and an address of
  a label is a `GLOBAL_ADDRESS` of it. Each label is a target in the fusion's windows, reached with nothing on the
  stacks.

The producer lowers conditions to jumps, the short-circuit operators straight to their targets. Wherever control
arrives at a target, the stacks hold the same values; `tier0_validate` checks that, as WebAssembly's validator does.
A label of `label_entries` counts as reached with empty stacks before the first entry, since a `JUMP_INDIRECT` may
arrive at any time; and a `JUMP_INDIRECT` must leave the stacks empty, in a function with a label.

**The memory contract:**

- Tier 0 reads `kinds` and `classes` 64 entries at a time and `values` 8 at a time, with masked loads. It never reads
  past `count`, at page ends either.
- The columns need no padding or alignment.
- The output's records come from the caller's arena: the records, plus four for the vector expansion's last 64-byte
  store.
- Its passes' columns come from the thread's scratch arenas.
- A failure (an unsupported entry, a depth past the tables, no memory) pops what it pushed, and reports the entry.

**What Tier 0 derives itself,** under #298's producer–consumer rule:

- **The labels** (a bitset of targets): one pass over the control entries the vector path finds 64 at a time, and the
  labels of `label_entries`. A producer's bitset would cost the same to build and adds a column to keep consistent.
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
and independent ordinary-function byte fixtures. Every fixture also holds an
object aligned to 16 (#362), whose bytes must survive the lists, the save area
and the call, at a multiple of 16, apart from the lists, the canaries and the
save area and below the overflow area. Run builds and tests only on
the approved 9700X, each through its own shared lock. This description records
the test contract; qualification results are reported with the exact revision.

## Structs and unions by value (#137)

A front end evaluates a struct or a union as its address, and so does the typed stream: an aggregate's value on the
integer stack is its address (U64). Three optional parts of `Tier0Function` say what passes by value, 0 for a function
without aggregates:

- **Arguments:** `argument_sizes[a]`, argument a's bytes, nonzero for an aggregate (class U64), to 64 KB
  (`BUSTER_TIER0_MAX_ARGUMENT_BYTES`). The call reads the bytes where its location says: copied into the outgoing
  stack area (STACK), its eightbytes loaded into registers (REGISTERS, or SPLIT: one in a register and one in an 8-byte
  slot), or nothing (IGNORE). An empty aggregate has size 0 here: it passes as the value its address is, which no
  callee reads.
- **Parameters:** a parameter that is an object is an aggregate passed by value. The prologue stores its register
  parts into its home. One on the stack whole keeps its incoming bytes as its home when its slot is a multiple of its
  alignment (the incoming area starts at a multiple of 16); the prologue copies the others' slots into homes of their
  own: a misaligned whole one, the high eightbyte alone in a slot, and a SPLIT one's slot.
- **Calls' results:** `call_result_objects[c]`, 1 + the local index of the object that receives call c's aggregate
  result, or 0. A result in registers is stored into the object after the call; for one through a hidden pointer, the
  object's address goes in rdi. The call's class is VOID: its result is the object, which the stream reads through
  `LOCAL_ADDRESS`.
- **The function's own result:** `result_size`, its bytes when it returns in registers, else 0. Every `RETURN` then
  returns the address of the bytes (U64), from which the epilogue loads the result registers while the frame they may
  be in is still there, and control reaches the function's end only through a `RETURN`. A result through a hidden
  pointer is the front end's: the pointer is parameter 0 (U64, in rdi), the bytes are copied there, and each `RETURN`
  returns the pointer (U64), which rax must hold; here too, control reaches the end only through a `RETURN`.

**Parts.** The registers come from `abi_assign`'s locations alone: eightbyte k (at 0 and 8, or the high one alone with
`BUSTER_ABI_FLAG_HIGH`) in `registers[k]`, a general or an XMM register of the convention's (above), each part in a
register of its own, an XMM one taking all 16 bytes when it is a 16-byte aggregate's only part. A part loads only the
aggregate's own bytes, which may end at a page's end:

| Part | Its load |
| --- | --- |
| a general register's 1, 2, 4 or 8 bytes, an XMM register's 4, 8 or 16 (exact) | one load of its width, zero-extended |
| a general register's high part of 3, 5, 6 or 7 bytes (overlap) | the 8 bytes that end where it ends, shifted right past those before it |
| the rest (temporary): a general register's only part of 3, 5, 6 or 7 bytes, an XMM register's of 1, 2, 3, 5, 6 or 7 | 8 bytes from a red-zone temporary, its bytes copied there first |

A store from a register (a parameter's arrival, a call's result) writes its whole width, 8 bytes or 16, which an
object's frame space, its size rounded up to 8, has room for.

**At a call,** the parts load after every value they would overwrite has moved: the XMM moves first, then the XMM
parts; then the general moves at once, an address that a move or a part would overwrite moved first into the register
of its aggregate's last general part, where it waits; then each aggregate's general parts, the one in the register that
holds its address last. Without XMM parts, both kinds of moves go at once, as a call without aggregates has them. An
aggregate on the stack, and a temporary part's bytes, are copied after the values' stores, through each stack's first
free register (the copies' scratch, which selection counts as reached). The vector path's measuring counts the records
as it counts every call's (`tier0_count_deferred`), from the parts, the copies' sizes and the waits.

**At no cost to the rest.** A function without aggregates compiles as it did before them. Its calls take the emitter's
and the counter's loops over the values alone: each is specialized on whether the function has `argument_sizes`
(`tier0_emit_call` and `tier0_count_call`, always inlined, the flag a constant at each use). The aggregates' placement
and moves (`tier0_place_aggregates`, `tier0_aggregate_moves`) and the result's loads (`tier0_load_result`) are passes
of their own, out of line, and so are the optional parts' checks (`tier0_frame_input_valid`, once per function), whose
code, inline, changed clang's code for the paths' shared passes. Selection takes the copies' scratch in a pass of its
own (`tier0_select_scratch`), and the
passes over the parameters leave out what only objects need without one among them (`Tier0Frame.object_parameters`).
[Measurements](#measurements) has what this saves on code without aggregates.

**Validation.** `tier0_validate`, and both compile paths through `tier0_frame_input_valid`, refuse aggregates' metadata
no emitter could follow (the compile paths check a function's calls when it has `argument_sizes` or
`call_result_objects`, and its parameters when it has `local_sizes` or an incoming variadic area; without them its calls
and parameters are the typed stream's, which `tier0_validate` checks, as the compile paths' precondition): a location
whose kind, registers or slot doesn't fit the size (`tier0_parts_valid`), a register outside the convention's or one
that another part, another argument of the call or another parameter takes, a slot outside its call's stack area or an
incoming one past int32, a call's stack alignment past 16, a result object that isn't an object with room for the
registers' whole widths, a hidden pointer without an object or where an argument takes rdi, a function whose hidden
result pointer isn't parameter 0 in rdi, and an aggregate result of more than two registers. A function returning an
aggregate, in registers or through the hidden pointer, must leave rax an address at every `RETURN`, which must be of
U64, and mustn't reach its end without one. `test-tier0`'s aggregate and parameter errors break each rule (below).

The producer (`c_check_program_lower.c`, [the checker guide](c-checker.md)) gives every call site with an aggregate
result an object of its own, a function with a hidden result pointer its parameter 0, and an aggregate parameter's
object its alignment, to 16; `docs/calling-convention.md` has the cross-check of C that this compiler compiles against
clang's and GCC's.

## Long double (#136's item 8)

A `long double` is the x87's 80-bit extended value in 16 bytes, which the System V convention passes in memory (on the
stack) and returns in st0; a struct of one returns in st0, and a struct of a `_Complex long double` in st0 and st1
(#137's X87 results). Its class is F80 (`BUSTER_TYPED_CLASS_F80`), whose values Tier 0 keeps in memory:

- **A value** takes an integer depth, as an address does, and lives in that depth's 16-byte slot of the frame
  (`Tier0Frame.memory_area`: a slot per integer depth after the fixed slots, in a function with long double values). An entry
  loads its operands from their slots onto the x87 stack, computes, and stores its result into its slot, leaving the x87
  stack empty, so that nothing lives there across entries, calls or joins. Every F80 entry is deferred: emitted by
  `tier0_emit_x87` and counted by `tier0_count_x87`, an entry at a time, as calls are.
- **Its locals and parameters are objects** of 16 bytes aligned to 16, and its globals are reached through their
  addresses: there is no `LOCAL`, `STORE_LOCAL`, `SET_LOCAL`, `GLOBAL`, `STORE_GLOBAL` or `SET_GLOBAL` of F80, and a
  front end reads one with `LOAD` and writes it with `STORE` or `SET`, the address first. A parameter's home is its
  incoming stack slot, whose place is a multiple of 16.
- **Its constants** are an optional column of the function's (`x87_constants`, `x87_constant_count` values of two words:
  the significand, then the sign and exponent), which `CONST F80` indexes. A constant is three immediate stores into its
  slot.
- **A function says whether it has long double values** (`Tier0Function.x87`: an entry of class F80, or a long double
  result), which its producer knows as it writes them. Only such a function has the slots, and only its compile looks
  for long double's entries: selection, measuring and the deferred entries' emitter are each written once with the flag
  a constant (`tier0_select_entries`, `tier0_select_vector_entries`, `tier0_measure_vector_entries` and
  `tier0_emit_deferred_entry`), the instance without long doubles inline where the code was before and the one with them
  out of line. A function without long doubles then compiles with the code Tier 0 had before them, but for a test of the
  flag at each of those uses (#423's measurements, below). A struct result in x87 registers, which goes to its object,
  takes no slot: any function's calls handle one.
- **The kinds:** `CONST`, `DUP`, `NEG` (`fchs`), `LOAD`, `STORE`, `SET`, `ADD`, `SUB`, `MUL` and `DIV` (`faddp` and the
  others on st(1) and st(0)), the compares (`fucomip`, the operands swapped for `<` and `<=`, with clang's results for
  unordered operands: true for `!=` alone), `CONVERT` to and from every class, `CALL`, `DISCARD`, `JUMP_VALUE` and
  `RETURN`.
- **Conversions:** from an integer by `fild` of it widened to 64 bits, an unsigned 64-bit one past 2^63 corrected by
  adding 2^64; to an integer by `fistp` with the rounding control set to truncation and restored (`fnstcw`, `fldcw`), an
  unsigned 64-bit one past 2^63 by subtracting 2^63 first and setting the top bit after, as clang's code does; and to and
  from float and double through the red zone.
- **Calls:** an F80 argument is copied from its slot to its place in the outgoing area (`fld`, `fstp`); a call's F80
  result is stored from st0 into the slot of its depth, and a struct result in x87 registers into its object, part k at
  16 · k. A function's F80 result is the value at depth 0 its `RETURN` leaves, which the epilogue loads into st0; a
  function returning a struct in x87 registers loads its parts from the address its `RETURN` gives.

Each truncation restores the control word it changed, which the convention has callee-saved, and MXCSR is untouched.
`tier0_validate` refuses a local of the class that isn't an object, a global read or stored as an F80 value, a constant
past the column, an integer operation of the class or a branch on it, an F80 argument anywhere but its 16 bytes of the
outgoing area at a multiple of 16, a `RETURN` of F80 from a function whose result isn't one long double in st0 and of
another class from one whose is, a function returning a `_Complex long double` (st0 and st1, which no class holds),
a call whose x87 result neither an F80 class nor a result object takes, and an F80 entry or a long double result in a
function without `x87`.

## 128-bit integers (#136)

`__int128` and `unsigned __int128` are Tier 0's I128 and U128 classes (`BUSTER_TYPED_CLASS_I128` and `_U128`, 12 and
13). Like F80, they are **memory classes** (`tier0_class_memory`): a value takes an integer depth and lives in that
depth's 16-byte slot (`Tier0Frame.memory_area`, long double's `x87_area` before them), its low half at 0 and its high
half at 8, so that calls and the arms of `?:` need nothing of their own for it.

- **Their locals and parameters are objects** of 16 bytes, and their globals are reached through their addresses, as
  long double's are: a front end reads one with `LOAD` and writes it with `STORE` or `SET`, the address first.
- **The kinds:** `CONST` (its `value` the low 64 bits, sign-extended for I128 and zero-extended for U128: a front end
  builds a wider constant from halves), `DUP`, `NEG`, `NOT`, `CONVERT` (to and from the classes I8 to U64, the low bits
  kept and an integer sign- or zero-extended as its class says; between I128 and U128, nothing), `LOAD`, `STORE` and
  `SET` (16 bytes at the address), `ADD`, `SUB`, `MUL`, `AND`, `OR`, `XOR`, `SHL` and `SHR` (by a count of a class I8 to
  U64, below 128; `SHR` arithmetic for I128), the six compares, `DISCARD` and `JUMP_VALUE`.
- **Their code,** every entry deferred (`tier0_emit_int128`, counted by `tier0_count_int128`): each half through a
  scratch register, the high halves with the carry (`adc`, `sbb`); `MUL` as the cross products' low halves (`imul`)
  added to the high half of the low halves' whole product (`mul`); a shift as `shld` or `shrd` and a shift of the other
  half by `cl`, then `test cl, 64` and two `cmovne`s for a count of 64 or more; `EQ` and `NE` as the halves' differences
  ORed, the ordered compares as `cmp` and `sbb`; `DISCARD` and `JUMP_VALUE` nothing, the values staying in their slots.
  One plan (`tier0_int128_plan`) gives each entry its scratch registers, from the depth registers its operands leave free
  (a 128-bit operand is in its slot), and whether rax and rdx (`MUL`) or rcx (a shift) are saved in the fixed slots,
  which they are where a value below the operands holds them. Both selections' tops and the emitter read it, so that the
  paths can't drift apart.
- **The front end's, not Tier 0's,** as clang's code has them: division and remainder (libgcc's `__divti3`,
  `__udivti3`, `__modti3` and `__umodti3`), conversions to and from the floating classes (`__floattidf`, `__fixdfti`
  and the rest), and calls. An argument of the classes is a 16-byte object passed by its address (`argument_sizes` 16),
  a call's result comes back into its result object, and a function returns one by its object's address (`result_size`
  16): two INTEGER eightbytes each time, as the convention passes `__int128`.
- **A function says whether it has 128-bit values** (`Tier0Function.int128`: an entry of the classes), as `x87` says it
  for long double. Either gives the function the memory classes' slots and the copies of selection, measuring and the
  deferred entries' emitter that look for their entries (`tier0_memory_function`), so that other code compiles as it
  did: on the 9700X, `benchmark-backend`'s vector path took 169.82 instructions per entry against main's 169.81, and the
  scalar path 571.32 against 573.73 (#464's job).
- **The slots' limit,** which long double's leaves already reached: both selections refuse a memory class's value at a
  depth past the 15 slots (`slot_top`, `TIER0_ERROR_DEPTH`). Before, a stack of 15 long double constants wrote past
  the slot area.

`tier0_validate` refuses a local of the classes that isn't an object, a global read as a value, `DIV` and `MOD` of them,
a conversion between them and a floating class, a call's argument or result of them, a `RETURN` of them, and an entry
of the classes in a function without `int128`. `interpreter.c` and `ir_lower.c` refuse them, as they refuse F80.

## Conversions between U64 and floating values (#136's item 5)

SSE converts between floating values and signed integers alone (AVX-512's `vcvtusi2sd` and `vcvttsd2usi` are past the
baseline). Tier 0's `CONVERT` from U64 to F32 or F64, and from those to U64, is deferred (`tier0_emit_u64_convert`), with
no branch, in one of two forms:

- **the register form**, in the operand's register, the result's and the stacks' first free ones, with no memory;
- **the red zone form**, with no register but the operand's and the result's, its scratch values in red zone
  temporaries, where the register form's free registers are past their tables.

`tier0_u64_convert_registers` chooses, from the stacks' depths before the conversion, d (integer) and f (floating), and
the integer table's depths (15, or 14 with a frame pointer):

| Conversion | Register form | Red zone form |
| --- | --- | --- |
| to F32 or F64 (the operand at d − 1, the scratch at d) | d < 15, or d < 14 with a frame pointer | d = 15, or 14 with one |
| to U64 (the result at d, the scratch at d + 1; the operand at f − 1, the scratch at f) | d + 1 < 15 (or 14) and f < 16 | d + 1 = 15 (or 14), or f = 16 |

So every depth with room for the conversion's own values takes it, as it did before the register form (#429's review:
the register form alone refused a conversion to U64 with 14 integers or 16 floating values below it, and one from U64
with its operand in the integer table's last register). The two forms of a direction have as many records, so that
measuring needn't know which a conversion takes. Each selection counts the conversion's result in its stacks' tops,
and the register form's scratch registers where it takes the conversion, which a frame pointer decides only once the
selection has seen every entry: it keeps the scratch's highest depths both with and without one, and takes the right
one at the end. Neither form takes a home's variant, so no local fuses into one.

- **To F32 or F64 from U64:** a signed conversion of the value where its top bit is clear, and otherwise of the value
  halved with its lowest bit kept, `(x >> 1) | (x & 1)`, which rounds as the value does (made in the integer stack's
  free register; `cmovns` chooses which), then the result's exponent raised by the value's top bit, which doubles it
  exactly where it was halved. The result's register is zeroed first (`xorps`), so that `cvtsi2sd` doesn't wait for the
  value it held. The red zone form halves the value in its own register, from its copy in a temporary, and raises the
  exponent in the result's bits in another. 14 records, where clang -O0 branches between the two conversions.
- **To U64 from F32 or F64:** one signed truncation, of the value less a bias: 2^63 where the value is at least that
  (`cmovae` from one compare with 2^63, which the integer stack's register past the result's and the floating stack's
  free one hold, or a temporary), 0 below it. The top bit comes back from the compare's result, which `setae` keeps (in
  that register's low byte, or a temporary's), shifted into place by 63, which keeps its lowest bit alone, and `xor`ed
  in. 11 records. The subtraction is exact (Sterbenz's lemma from 2^63, and of 0 below it), and the truncation's operand
  is below 2^63. So a value in U64's range raises the invalid exception never and inexact only where it has a fraction,
  as C's conversion does. (#429's first sequence truncated both ways and chose, which raised invalid from 2^63 on, and
  inexact for a value like 1.0.) A value out of U64's range, which C leaves undefined, gives the signed truncation's
  result with its top bit flipped.

#429's first versions had the red zone form alone: on the 9700X, a loop of the four conversions (#429's review) took 2.1
times clang -O0's time, 25.7 cycles an iteration against 12.2, as each conversion's loads waited for the stores before
them. So it takes only the depths where the register form has no room.

## Deep values (#136's item 6)

A stack's values fit its depth table up to 15 integer ones (14 in a function with a frame pointer, whose rbp is the
table's last) and 16 floating ones. Past that, they live in the frame's deep slots, 8 bytes per depth of each stack,
value d in slot d (`Tier0Frame.deep_area`). Selection finds such a function as before, by the depths its entries'
registers reach (`TIER0_ERROR_DEPTH`), and `tier0_select_deep` takes it on from there, shared by both paths: code within
the tables doesn't pay for it.

**Where values are.** At each entry's start, a stack's values are where its depth says: all in their registers up to the
table's count, all in their slots past it. Every way into a label agrees, since the stacks there have the same depths
whichever way control arrives.

**Deep entries.** An entry is deep on a stack when its start and its exit (a `JUMP_VALUE`'s at its target, which its
value is on) are on the two sides of that line, or when it uses the stack's registers where the stack is past it or
reaches past the table (a template's reach, as selection reads it, or a deferred emitter's). A deep entry is deferred,
in both paths (`tier0_emit_deep`):

1. if the stack's values were in their registers, it stores them into their slots;
2. it loads the values it reads (its window: 2 at most, below its depth, from the template's register sources, or the
   values a template for one depth reads through its fixed registers, as division's dividend in rax) from their slots
   into the stack's first registers;
3. its own records run at that depth, which the depth columns hold for it from here on: its template's, home case and
   choice of immediate forms included, or its deferred emitter's (division, shifts, copies, the stack's entries and the
   conversions between U64 and the floating classes), which then finds rax, rdx and rcx where a template for depth 2
   does. A conversion takes the form selection chose at its depths before its window's (`tier0_u64_convert_registers`),
   so that its registers are those selection counted: a deep one to U64 is always the red zone's, and one from U64 the
   registers' only where it is deep on the floating stack alone with room at its integer depth;
4. its result goes from its register at that depth into its slot;
5. if its exit is within the table, the stack's values are loaded back into their registers;
6. a template's branch, its last record, comes after them, which keep the flags its compare set: both ways out
   arrive with the stacks where their depths say.

A call deep on either stack (`tier0_emit_deep_call`) stores both stacks' registers into their slots if they were there,
copies each argument on the stack from its slot through rax into the outgoing area, loads each in a register straight
from its slot (after the stack ones), its callee into r11, makes the call as `tier0_emit_call` does (al, the hidden
pointer, the result object), stores the result into its slot and loads back the stacks within the tables. Nothing below
its arguments needs saving: it is in the slots.

A deep function's deep code takes the depth registers below the tables, which its depths reach anyway: no other
register, and never a frame pointer's rbp.

**Refused** (`TIER0_ERROR_DEPTH`, now its meaning): a function with values of the memory classes (long double's and the
128-bit integers') whose stacks pass the tables, since such a value's slot is its integer depth's; a call with aggregate arguments where its copies' scratch registers
are past the tables; and a stack deeper than `BUSTER_TIER0_MAX_DEPTH`, 240, which the depth columns' bytes would wrap
past (a stack rises by one entry at a time at most, so it shows there first).

**Cost.** Deep code is longer: each deep entry's loads and stores, and the registers' stores and loads where an entry
crosses the line. `test-tier0`'s deep shape (below) has about 2% of its entries deep, and its cross-check's code is 1.33
times clang -O0's (1.16 for the larger shape's), with run times about the same.

## Symbols outside the image

`GLOBAL`, `GLOBAL_ADDRESS`, `STORE_GLOBAL` and `SET_GLOBAL` name a symbol, and `Tier0Function.symbol_access` says how the
code reaches it (`BUSTER_TIER0_ACCESS_*`):

| Access | For | Code |
| --- | --- | --- |
| `DIRECT` | a symbol in the image | `[rip + symbol]`, a `PC32` |
| `GOT` | one that may be a shared library's | `mov r, [rip + slot]` (`LOAD_GOT`), then `[r]` |
| `LOCAL_EXEC` | a thread-local of the image's | `fs:[offset]` (`TPOFF32`); its address `mov r, fs:[0]; add r, offset` |
| `INITIAL_EXEC` | a thread-local that may be another's | `mov r, [rip + slot]` (`GOTTPOFF`), then `fs:[r]`; its address `add r, fs:[0]` |

- **Why the GOT:** the direct path makes no copy relocations (#84), so a shared library's data, `stdout` among it, is
  reached only through a GOT slot the loader fills: `GLOB_DAT`, or `TPOFF64` for a thread-local's offset. So is a
  function's address that may be a shared library's, since a `PC32` to one is refused, as lld refuses it in a PIE. Calls
  are unaffected: a call of a function outside the image goes through the linker's stub.
- **Relaxation:** the slot's load is the `mov r64` that linkers rewrite. `executable.c`, GNU ld and lld turn a `LOAD_GOT`
  into a `lea` where the symbol turns out to be in the image, and the system linkers turn a `GOTTPOFF` into local-exec in
  an executable.
- **The column is per unit:** one array serves every function. The producer (`check_program_emit`) fills it from what
  the unit defines, as clang's `-fPIE` does: a symbol the unit doesn't define through the GOT, and a thread-local by
  local-exec where the unit defines it and by initial-exec where it doesn't.
- **How:** fusion keys an entry of a symbol that isn't direct by one of Tier 0's own kinds, past the stream's
  (`BUSTER_TIER0_KIND_GOT`, `_LOCAL_EXEC` and `_INITIAL_EXEC`, four each, in the global kinds' order:
  `tier0_access_kind`). Their templates load the slot into R0, the register free above the operand (beside a floating
  one too), then make the access through it. The vector path finds a block's global entries by compares, reads their
  symbols' accesses from the column (they are few, as the calls are), and keys each access's lanes by a `vpermb` of its
  table; without the column it does nothing. A template's reference kind and field are in its summary (`fixup_kind`,
  `fixup_field`), so a thread-local's address takes its `TPOFF32` in an immediate.
- **No fusions:** these kinds take no fused local or constant, whose value would need a second free register. A constant
  stored through the GOT could be an immediate, but its properties would be a row past the vector fusion's sixteen,
  which every function's lookups would pay for.
- **Validation:** `tier0_validate` refuses a global entry whose symbol is past the column, or whose access is past the
  last.

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
`PC32` or `CALL`, or for a symbol outside the image `LOAD_GOT`, `TPOFF32` or `GOTTPOFF`, the target `SymbolId` and
addend 0, in record order. An indirect call is `call r11`, with none, and an indirect jump `jmp` through the address's
register.

**The labels' records** (`label_records`, one per entry of `label_entries`): the first record of each label whose
address the code takes, from the positions both paths compute; equal in both, as the tests compare.

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
- **Deeper expressions:** past 15 integer or 16 floating values live at once, the values go to frame slots ("Deep
  values", below).

**Homes.**

1. A parameter whose register the body never touches stays where it arrives.
2. The other locals, most referenced first, take the caller-saved registers no entry touches: neither the depths
   reached, nor a template's fixed registers, nor anything if the function calls.
3. Then they take the callee-saved registers, for a local referenced at least twice (a push and a pop).
4. Floating locals take free XMM registers.
5. Address-taken locals and the rest get 8-byte frame slots (a `VA_LIST` 24 bytes), rsp-relative (rbp-relative with
   a frame pointer, below), after a variadic function's 176-byte save area. A stack parameter keeps its incoming slot, unless it took a
   register, which the prologue loads from that slot. No home is a register a parameter arrives in, so the prologue's
   moves and loads never overwrite one another's sources.
6. Objects come after the slots, in the locals' order:
   - each at a multiple of its alignment, taking its size rounded up to 8;
   - with an object aligned to 16, the frame keeps rsp a multiple of 16, as a function that calls does.

   The stack parameters' places and the overflow area's anchor count from the frame's final size, objects included,
   and every displacement is checked in 64 bits before it narrows to a disp32.

   The vector path finds the objects 16 locals at a time as a mask of nonzero sizes, as it finds the slots.

This is what beats clang -O0, which loads and stores every local at every use. The counting loop for `s += i` compiles
to `xor edx, edx; xor ecx, ecx; mov eax, ecx; cmp eax, edi; jge; add edx, ecx; inc ecx; jmp`.

**The frame pointer.** A function with any of the stack's kinds has one, which both paths derive from its entries in
selection:

- the prologue pushes rbp first and sets it to rsp, and rbp leaves the depth table (14 integer depths) and the homes;
- an allocation moves rsp, so every slot, object and call's save is addressed from rbp (`Tier0Frame.base` and `bias`):
  the homes' displacements less the frame's size and the pushes after rbp's;
- a call's stack arguments still go at rsp: the outgoing area is rounded up to 16 and stays below the allocations, so
  `ALLOCA` is `add r, 15; and r, -16; sub rsp, r; lea r, [rsp + outgoing]` (`mov` without an outgoing area). rsp
  stays a multiple of 16, at every call too;
- `STACK_SAVE` and `STACK_RESTORE` are a `mov` from rsp and to it;
- the epilogue sets rsp back from rbp (`lea rsp, [rbp - 8 * pushes]`, or `mov rsp, rbp`), past whatever the body
  allocated, before its pops and `pop rbp`.

Structs and unions by value (below) need nothing more: the prologue reads their incoming slots from rsp before anything
is allocated, their homes and a call's result objects are addressed from rbp as the other homes are, a call copies them
into the outgoing area at rsp and its red-zone temporaries are below rsp, and the epilogue loads a result's registers
before it releases the frame. A function without them keeps the rsp-relative frame, record for record.

**Templates.** A template is up to four records, one per (kind, class, variant). The variant says what the entry
absorbed (`BUSTER_TIER0_VARIANT_*`); a home-dependent slot holds four templates, one per home case. A pointer
addition's element size picks its own: `lea` for 1, 2, 4 and 8, `imul r, r, imm32` up to 2^31 − 1, and past that
(`WIDE_SCALE`) the size in the next register, as a constant is, then a 64-bit `imul`. The records' fields come from
the entry's 32-byte hole block:

- K, the constant;
- A, the fused local's home;
- B, the entry's own local's home, or a compare's first local's;
- the registers at depth −2 to 0 of each stack.

`tier0_initialize` builds 1,951 templates from the encoder's forms and compiles each into one cache line. A byte the
template fixes is that byte; a hole's is its index in the hole block. Expansion is copy and patch:

1. one load of the line;
2. one `vpermb` from the hole block;
3. one blend by the template's mask;
4. one 64-byte store at the entry's place, overwritten past its records by the next entry.

**Deferred entries** go to `tier0_emit_deferred`, which writes their records at the places measuring reserved. The
vector path's measuring counts a call's records with `tier0_count_deferred`, in one pass from its last argument back
(an argument's register is its stack's depth less the arguments after it), from their locations and the cycles of its
parallel move, without emitting it; the scalar path runs the emitter to count, which checks the count:

- Calls: saves, stack arguments, the register arguments by a parallel move, al for a variadic call, the call, the
  result moved, the restores. An indirect call's callee joins the parallel move, into r11, which no argument and not
  al takes; then `call r11`. The moves' sources are distinct registers, as are their destinations, so they form
  paths and cycles, resolved on bitmasks: a move whose destination no other move still reads goes first, and what is
  left are cycles, each of n moves taking n − 1 `xchg`s. With rax first in the depth table and rdi first in the ABI's
  order, a call makes cycles of two alone; a test of the resolver's own covers every length.
- Division and shifts by a register at depths where rax, rdx or rcx hold other values (through two fixed frame slots).
- Copies, through their stacks' first free register (an XMM one from 16 bytes, a general one below), which selection
  counts as reached, so that no local takes it as its home:
  - from 16 bytes, a load and a store with `movups` per 16, the last 16 ending at the end;
  - below, two of a general register's 8, 4, 2 or 1 bytes, the second ending at the end (one when that is all).

  Overlapping pieces copy what they would apart, the ranges being the same or apart. `rep movsb` would copy any size
  in a few records, but `x86_semantics.c` has no row for it, and `unwind_tier0.c` refuses a function with an
  instruction of unknown effects.
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
   depth checks are masks, the depths each stack reaches lane maxima, the kept templates' words are ORed together
   (their fixed rdx) and the unsupported lanes collected, so that a block takes no branch but on a deferred entry,
   whose touches (a call's saves, division's fixed registers) are taken an entry at a time. The first unsupported
   entry is looked for only when there is one, in a compile that fails.
2. **Homes:** the references' count takes the referring entries of 64 at a time as a mask (`vpshufbitqmb` of their
   kinds in the four kinds' bitset), then a count per set bit, without a branch per entry. The scalar path then sorts
   the locals by references and gives them registers in that order, greedily. The greedy loop gives the k-th local of
   a kind that takes a register (not in one yet, its address not taken, referenced) the k-th free register of its
   kind: the caller-saved registers in depth order, then the callee-saved ones to a local referenced twice or more
   (those come first in the order), or the XMM registers. So for up to 64 locals the vector path needs no order:
   each such local's rank among its kind's, counted by vector compares 16 locals at a time, picks its register from
   the free registers' list (`vpermd`), and its home is a masked store. The frame's slots go to a mask of the locals
   that need one, from the homes, the references and the flags, 16 at a time, and the prologue's records are counted
   rather than emitted twice.
3. **Measuring** resolves a home-dependent word to its homes' case. Which of up to 64 locals are in registers is one
   mask, of which a lane's case is a shift (`vpsrlvq`); past 64 locals, a local's home comes from four registers by
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

The immediates' choice is one `x86_choose_forms` over every request, and each chosen form is applied from a byte per
form (its immediate's size, and whether it takes no operands or its destination twice) to the record's two 8-byte
halves, without a branch.

**The scalar path** (`tier0_compile_scalar`) does the same an entry at a time, with each rule in plain code. Selection
looks each entry's slot up, homes sort the locals, measuring counts a deferred entry's records by running its emitter,
and emission builds each record field by field from the templates' specifications, not from their compiled bytes. It
chooses with `x86_choose_forms_scalar` and applies each choice with `x86_choice_instruction`.

## Tests

`test-tier0` (about 0.8 s, or 2.9 s with `--cross-check`, on the 9700X, before the records' shape) generates programs of
six shapes: one function of integers, integers and calls, everything, larger, larger with structs and unions by value
(#137), and larger past the depth tables, 300 each by default. Each program:

1. Every function must pass `tier0_validate`.
2. Both paths must compile it to the same records, branch columns, references and entries' first records, which must
   ascend within the records; and a compile without the entries' first records must make the same code and none of
   them.
3. The records must pass `x86_validate` and the branch columns `branch_sizing_validate`.
4. They are sized and encoded, loaded into fresh pages and run in a child process on eight inputs, which an alarm stops
   after 10 s (a frame laid out wrong can make code loop). The result and every global's bytes must equal what the
   test's interpreter of the typed stream gives.

Before the programs, checks of their own:

- **moves:** 20,000 random sets of argument moves, resolved by the call emitter's resolver and run on a register file:
  every destination must get its source's value, every other register keep its own, and the records be as many as
  the moves less the cycles. The general registers' sets have paths and cycles of every length, though a call makes
  cycles of two alone, and the XMM registers' only paths, as a call's floating arguments only move down.
- **applies:** the vector path's application of the chosen forms against the scalar path's
  (`x86_choice_instruction`), on 2,000 batches of random records, each with a candidate form of a random row or
  `X86_FORM_NONE` (which keeps the record) and a random value.
- **errors:** both paths must refuse the same streams alike: an entry without a template (`NOT` of a double, which
  `tier0_validate` refuses too) at 200 places among 64-entry blocks, before another (the first entry reported), and
  14 to 17, 32, 239 and 240 values on the integer stack at once, past the 15 depths from 17 on (the last constant is
  the first add's immediate), which must compile alike, and 241 and 250, past `BUSTER_TIER0_MAX_DEPTH`. And both must
  compile 40 locals set from constants their stores absorb, from INT32_MIN on: past 32 locals emission gathers the
  homes, and an absorbed lane mustn't look its value up as a local's.
- **objects:**
  - `tier0_validate` must refuse each break of an object's rules at its locals: a class, no `ADDRESS_TAKEN`, a
    parameter, aligned to 32, sizes without alignments, a `LOCAL` of it, and past `BUSTER_TIER0_MAX_OBJECT_BYTES`.
    It must take exactly the limit.
  - 2,000 random frames, each of up to 24 locals: objects of 1 to 300 bytes at every alignment to 16, 8-byte values,
    some address-taken, and `VA_LIST`s of 24 bytes. Both paths compile them, and must agree. Each local's place, from
    its `LOCAL_ADDRESS`'s `lea`, must be a multiple of its alignment (8 but for objects), inside the frame and apart
    from every other local's bytes. Under an object aligned to 16, rsp must be a multiple of 16.
- **unsigned floating:** the conversions between U64 and the floating classes (`tier0_emit_u64_convert`), each as a
  function of its parameter, compiled by both paths into the same records, encoded by both encoders and run on 22
  named values under each of MXCSR's four rounding modes: from U64 around 2^53, 2^63 and 2^64, where the halving's kept
  bit decides the rounding (ties to even and just past them), and to U64 from -0.5, -0.0 and values below 2^63, at it
  and up to the largest below 2^64, each against C's conversion under the same mode. Each call's MXCSR invalid and
  inexact flags must be those of C's conversion of the same value (compiled with `FENV_ACCESS`), and every conversion
  to U64 runs again with invalid unmasked, where a spurious one would raise SIGFPE. The generator's programs have these
  conversions too (`Tier0ProgramOptions.unsigned_floating`, in the shapes with floating values; to U64 only where C
  defines it, in the clang cross-check's).
- **unsigned floating limits:** the same conversions at the stacks' limits (#429's review), each as a function of x and
  `out` that holds integers and doubles below the conversion, stores their sums at `out[0]` and `out[1]` and returns
  the conversion: from U64 with x in the integer table's last register but one and in its last, without a frame
  pointer and with one (14 depths), and with the result in xmm15; to U64 with the result in the table's last register
  but one and in its last, with and without a frame pointer, with x in xmm14 and in xmm15, and with both limits at once.
  Both paths must compile each alike in the form the check's own rule gives (the red zone form's records are those at
  negative displacements from rsp), the prologue must push the register form's scratch where it is callee-saved, and
  each call, as the unsigned floating check makes them, must give C's conversion and its flags and both sums.
- **deep** (`tier0_deep_test.c`, #136's item 6): nine functions of one U64 parameter whose stacks fill their tables,
  then cross the line with an entry of each kind the generated programs reach rarely or never: a value pushed past it
  and dropped back to it (a `DISCARD`, which uses no register), `?:` whose condition, branch and arms' values cross it,
  a compare fused into its branch that drops two values across it, 17 floating values (through U64 conversions, one of
  them dropped across the floating line), division by a local (whose dividend only rax reads) and by a value, `%`, and
  shifts by a register past it, a frame pointer's 14 values and two past them, both stacks crossing at one conversion,
  rsp moved past a frame pointer's line (#434's review): rsp saved, two allocations stored through and loaded back,
  rsp restored, every entry a deep one while the values below are in their slots, which rbp addresses; and a conversion
  to U64 deep on the floating stack alone at integer depth 13, whose register form's scratch would be rbp, which
  nothing else in it reaches. Each must compile by both paths into the same records with deep entries, a frame pointer
  exactly in the cases that have one, write no callee-saved register that its prologue doesn't push (by
  `x86_instructions_effects_scalar`), be encoded alike by both encoders, and give this file's model of it natively on
  six inputs.
- **copies:**
  - `tier0_validate` must refuse a copy of no bytes or past 256, of a class, or from or to a constant, and take 256.
  - a copy of 1, 3, 8, 15, 16, 40 and 256 bytes above every count of 0 to 13 integer and 0 to 16 floating values.
    Both selections must count its scratch register among the frame's clobbers, or find the function too deep where
    its stack has none left, which the deep slots then take, and both paths must compile it alike. The values are then folded in postorder, the
    doubles onto a zero pushed above them, which takes one more floating depth: 16 doubles are too deep whatever the
    copy.
- **operands:** `tier0_validate` must refuse each of nine streams at the entry whose last operand has another class
  than the entry before it (a binary operation, a compare and a `CONVERT` after a store of another class, a `DISCARD`
  after a `COPY`, a `COPY`'s source, a returned value, one from under a call that returns nothing, a branch's
  condition and an arm's value), and take 16 others:
  their versions in postorder, a store that pushes, `DUP`, values left under a store of their class (an increment's
  old value), and calls, whatever is under their arguments.
- **variadic calls** (#363's review): two callers call a probe through a pointer, variadically, with six integers and
  three doubles past a named first one. The probe, records of the test's own, stores what it sees: the argument
  registers, rax, r11 and the vector registers. Its al must be 3, r11 its own address and the arguments the call's:
  - with the callee in rax, under arguments whose moves are 2-cycles (rcx and rdi, rdx and rsi), so that al is set
    only once the callee has gone to r11;
  - with it in rdi, under four integers and two doubles saved around the call, so that the moves run through r11
    (rbx to r8 to rdi to r11 to rcx) and the vector registers (xmm4 to xmm2 to xmm0). The caller returns what the
    values below make of the result, which must come back right.

  Both paths must compile each alike, and each encoder's bytes run in a child.
- **aggregates** (#137), at locations written by hand: 13 fixtures of what `abi_assign` makes rarely or never on its
  own and of the parts' edges (each kind of general and XMM part, both kinds in one aggregate, the high eightbyte alone
  in each kind of register and on the stack, SPLIT both ways in both kinds, an ignored aggregate, aggregates on the stack
  whole, in place and in a slot misaligned for them, and results in rax and rdx, xmm0 and xmm1, both mixed orders, the
  high eightbyte alone in each kind, a vector, 5 and 6 bytes, and through a hidden pointer), each run natively both
  ways:
  - **aggregate calls:** Tier 0's caller, by both paths, passes the aggregates' bytes from pages that end or start at a
    `PROT_NONE` page to a probe, records of the test's own, which records every argument register and its stack slots:
    each value and part must be where its location says, and a result the probe returns must land in its object;
  - **aggregate functions:** Tier 0's function, by both paths, whose parameters a driver passes from the same bytes,
    copies each parameter's bytes out, each aggregate's address after them (a multiple of its alignment), and returns
    the result in its registers or through the hidden pointer, which must come back in rax;
  - **aggregate errors:** 57 breaks of the fixtures' metadata, each refused by `tier0_validate` with its rule, or as
    malformed metadata by both compile paths too: among them a function with a result size that reaches its end by
    falling off it, by a jump and by a branch, a jump that takes a value to the end, a function with a hidden result
    that returns nothing or falls off its end, an aggregate's two parts in one register, a register two parameters
    take, and a part, a value or a result in a register outside the convention's (rax, xmm8, rbx), as an argument and
    as a parameter.
- **parameter errors:** a function of values alone, which no optional part makes the compile paths check: two
  parameters in rdi, one in rbx and one in xmm8, which `tier0_validate` must refuse, and an F64 in rdx, which doesn't
  fit its class.
- **stack:**
  - `tier0_validate` must refuse, at the entry, an allocation of another class, with a value or of a U32, a save of
    another class or with a value, and a restore of another class or with a value, and take a save restored.
  - 14 and 15 integer values at once must compile with a frame pointer and without, by both paths alike: with one,
    the 15th goes to its deep slot (rbp, the table's last depth, is the frame's).
  - a function of the test's own (`tier0_test_stack_entry`) allocates 64 KB and up 1,000 times in a loop, each in a
    scope of its own: the position saved, the allocation's first and last words written, its address modulo 16 added
    in, a call with stack arguments made (whose outgoing area mustn't overlap the allocation), the words read back and
    the position restored. Without its restores it would take 64 MB of stack. Both paths must compile it alike, with
    `push rbp` in its prologue, and it must run in a child on eight inputs with the interpreter's result and the
    expected one.
- **labels** (#133's labels as values, `tier0_label_test.c`):
  - `tier0_validate` must refuse, at the entry, an indirect jump to an I64, of class I64, with a value, with a value left
    under it and in a function without labels, a label past the end, the labels' count without their entries and a
    label that control reaches with a value, and take the stream beside them.
  - a function of the test's own dispatches on its argument's base-4 digits through a table of five labels' addresses,
    one label going on to another through the table, as an interpreter's loop does. Both paths must compile it alike,
    labels' records included, in ascending order; both encoders must give the same bytes; and it must run in a child
    on 12 inputs, the table filled from the labels' records and the records' offsets after branch sizing, with this
    file's results.
- **x87** (#136's item 8, `tier0_x87_test.c`), long double compiled by both paths into the same records, encoded by
  both encoders into the same bytes and run natively in a child, called from C through function pointers, so that the
  file's C (clang's code) is the oracle of the convention for its arguments, parameters and results, and its x87
  arithmetic the oracle of their values:
  - **rules:** each break of the class's rules (above) refused at its entry by `tier0_validate`, and the streams beside
    them taken;
  - **functions** of long double parameters and results called from C: two on the stack around an integer in a
    register, a struct of one returned in st0, a call of a C function whose such struct comes back into its object, long
    doubles passed to a variadic C function, and one kept in its slot across a call;
  - **boundaries:** each conversion with a sequence of its own on named cases: to `unsigned long` at 0 and 1, below 2^63
    by 1 and by a half, at it, past it by 1 and at 10^19, and at its largest value; to `long` and `int` at their limits
    and on fractions of either sign; and back from `unsigned long` (2^63 ± 1 and the largest two values among them) and
    `long`. Each must give C's conversion of the value, every case in range, and each call, made under rounding upward,
    must truncate and leave the control word as it was;
  - **programs:** 300 random functions (`--count`) of statements that each write a value into 16 bytes of an output (a
    long double; an integer, a float or a double converted from one; a compare's result), from inputs of every class,
    through constants, arithmetic, `?:`, `DUP`, `STORE`'s kept value, conversions both ways (unsigned 64-bit ones past
    2^63 too) and calls of C functions through their addresses, against the check's own stack machine over the stream.
    The programs must reach every kind and conversion of the class. A program past the depth tables is counted, not
    compiled.
- **int128** (`tier0_int128_test.c`), the 128-bit integers compiled by both paths into the same records, encoded by both
  encoders into the same bytes and run natively in a child, called from C through function pointers, so that the file's
  C (clang's code of `__int128`) is the oracle of their values and of the convention for them:
  - **rules:** each break of the classes' rules (above) refused at its entry by `tier0_validate`;
  - **slots:** a stack of a memory class's values up to the last slot, which runs, and one past it, which both paths
    refuse, for the 128-bit integers and for long double;
  - **boundaries:** for each class, its six compares, `SHR` by 0, 1, 63, 64, 65 and 127, and `SHR` and `SHL` by variable
    counts, on every pair of ten boundary values (0, 1, -1, the extremes, 2^63, 2^64 and their neighbours), against the
    check's stack machine and against C's own operators;
  - **functions** of 128-bit parameters and results, in register pairs and on the stack (where no pair is left), called
    from C on boundary values, and functions that call C functions of them;
  - **programs:** 300 random functions (`--count`) of statements that each write a value into 16 bytes of an output (a
    128-bit integer of either class, an integer of a class I8 to U64 converted from one, a compare's result), through
    constants (low halves past a sign-extended imm32 among them), arithmetic, shifts by constant and variable counts,
    `?:`, `DUP`, `STORE`'s kept value and conversions both ways, under live values at the depths where `MUL` saves rax
    and rdx and a shift rcx, against the check's own stack machine over the stream. A program counts toward the
    coverage only once both paths compiled it, it ran and every output matched, and then only the entries the stack
    machine ran: those must reach, for each class apart (their signed and unsigned code differs), every kind, every
    conversion to and from it, each of the saves and both arms of a `?:`, in bitmaps of their own.

  Every function must compile: an error of both paths alike fails the check, but a depth past the slots in a random
  program, which is counted.
- **access:** `tier0_validate` must refuse a global entry whose symbol is past the access column, and an access past
  the last, and take the last. Then GLOBAL, STORE_GLOBAL and SET_GLOBAL of a local's value, SET_GLOBAL of a constant,
  and GLOBAL_ADDRESS, of every class, by each access. Both paths must compile each alike to valid records with one
  reference to the symbol, of the access's kind, in the displacement but for a thread-local's address of the image's
  (an immediate), with an FS override exactly for a thread-local. A direct symbol's code must be the code without the
  column, and only a direct symbol takes a stored constant as an immediate.

**The generator** (`tier0_generate.c`) writes each program both as typed streams and as C. Its programs have:

- parameters and locals of every class, address-taken locals and pointers to them and to global arrays; one function
  in five has 7 to 12 parameters, past the argument registers;
- assignments, compound assignments through pointers and array elements, increments, calls;
- if and else, counted while, for and do loops with break and continue, early returns;
- expressions of every operation over every class, with conversions, `?:`, `&&` and `||` as values, and compares;
- pointer arithmetic as a U64 value, with element sizes for each of its templates: 1 to 8, imul's imm32, and sizes past
  it to 2^64 − 8;
- with the `objects` option (the shapes everything and larger), up to three objects a function:
  - local arrays of 2 to 16 elements and structs of 2 to 5 members laid out as C lays them out, some `_Alignas(16)`;
  - elements and members read, written (an element also by `+=`) and pointed into;
  - each object's address modulo its alignment as a value (`(unsigned long)&v & 15`), 0 wherever the object is placed
    right;
  - twins (an object of the layout of the one before) and copies between them, of all their bytes or a prefix, as
    statements and before call arguments (a `COPY` above the arguments before it). C leaves the order of a call's
    arguments unspecified, and another argument may read the copy's destination, so a call with a copy is written in
    C as a block that evaluates its arguments into temporaries in order, the copy a statement at its place;
  - at each function's end, every element's and member's bits XORed into a global (padding left out), so that a
    byte any store or copy puts wrong shows in the globals.

  A program without objects is the one the same seed gave before they existed;
- with the `vlas` option (the shapes everything and larger), now and then a variable-length array of 2, 4 or 8
  elements of a class, plus a generated value's low two bits: `T v[(e & 3) + n]`, its size kept in a local as
  `sizeof v` and its address in another:
  - its first n elements written, read, pointed into and folded into the globals at its scope's end;
  - in a loop's construct, the stack's position saved before the construct's first array and restored at its end and
    before every `break` and `continue` out of it, as a producer does; at the function's top, nothing, the return
    releasing them;
  - its size (`sizeof v`) and its address modulo 16 as values.

  The interpreter allocates them from an area of its own, 16 bytes at a time, and checks that every restored position
  is one saved since the frame began. A program without the option is the one the same seed gave before it;
- with the `indirect` option (the shapes with calls), a third of the calls indirect: through locals that hold an
  earlier function's address (C calls through them), or through the function's address itself:
  - four functions in ten take an earlier function's type, so that a type has several functions;
  - statements give a pointer one function of its type or another by a condition (`p = c ? f : g`), so that a call
    through it reaches whichever it holds when it runs;
  - the interpreter calls the function an address names, once its tag and its type are checked against the call's (its
    result's class and each parameter's, apart from the generator's choice of candidates), and a call's work budget is
    its costliest candidate's.

- with the `access` option (every shape but one function's), the symbols' access column: two globals in five direct,
  one through the GOT, one a thread-local of the image's and one a thread-local through the GOT (`_Thread_local` in
  C), and half the functions' addresses through the GOT. The interpreter is unchanged, since the access changes how a
  symbol is reached and nothing it means. The native run gives every symbol a GOT slot (an address, or a thread-local's
  offset from the thread pointer) in its data pages, then the thread-locals' block and the thread pointer at its end,
  whose first 8 bytes hold its own address as the C library's thread control block does. Each run copies the
  thread-locals' first values into the block, points FS at the thread pointer for the entry's call alone, and copies
  them back beside the other globals, whose bytes it compares with the interpreter's.
- with the `deep` option (the sixth shape), values past the depth tables: one function in four has 13 to 32
  parameters, whose callers evaluate each argument over the ones before it on the stacks, and expressions have room
  for 40 values at once. With the default count, at least one entry in a hundred must be deep (about 2% are). A program
  without the option is the one the same seed gave before it;
- with the `records` option (the fifth shape, #137), structs and unions passed and returned by value, from a catalog
  that `tier0_tables_open` builds in the type table and classifies: byte arrays of 1 to 17 bytes, whose parts load in
  every way and the largest of which goes on the stack; floats and doubles, mixed with integers both ways round; unions;
  a member aligned to 16 (one part of 16 bytes); the empty struct; and records past 16 bytes, on the stack and returned
  through a hidden pointer. Functions take records as parameters and return them, calls pass the scope's objects of
  them and their results land in objects of their own. With the default count, the programs must reach every kind of
  part, location and result the catalog makes (`tier0_test_coverage`: all but an ignored argument with bytes and an XMM
  part through a temporary, which need unnamed bit-fields and halves, as the fixtures have them), and functions with a
  frame pointer (the shape's variable-length arrays) that pass, take or return records.

Everything in them is defined behavior, and nothing recurses: expressions are built from a worklist, and emitted and
printed by walks with explicit stacks. Their expressions stay within 12 stack slots, as a front end's do, but with the
`deep` option.

**The cross-check** (`--cross-check`, 60 programs of the larger shape) writes Tier 0's code as an ELF object through the
link representation and the object writer. clang -O0 compiles the same program as C, and a generated harness links both,
runs them on the same inputs and compares the results and every global. clang compiles them in this repository's own
dialect, `-fwrapv` (signed arithmetic wraps, as Tier 0's does) and `-fno-strict-aliasing` (the objects' checksums read
floating parts' bits through integer pointers). These programs keep to what that defines: clang folds an out-of-range
floating conversion of a constant its own way, so a floating value converts to an integer only from a short's value. A
floating global's elements compare as their bytes, but any two NaNs are the same: C leaves a NaN's sign and payload to
the implementation, and clang folds a constant 0.0 / 0.0 to +NaN where `divsd` makes −NaN, and `a + -b` into `a - b`,
whose NaN keeps `b`'s sign where Tier 0's flips it. The objects' checksums treat NaNs alike too: a floating part's bits
are masked by whether it holds a number (`& -(unsigned long)(x == x)`), and a union's floating member is written only
with an integer's value, so that no union holds a NaN whose bits its other members would read. The masks take no
random draw: every seed gives the program it gave before them, but for the masks' entries. Tier 0's object defines the
direct globals and the thread-locals of the image's (in `.tdata`), and a shared library generated beside it defines
the ones Tier 0 reaches through the GOT, so that the executable's slots for them are the loader's to fill: `GLOB_DAT`,
and `TPOFF64` for a thread-local's offset. The library is GNU as source, which `as` and `ld -shared` link in a few
milliseconds a program, where `clang -shared` of the same data as C took tens: in the 9700X job's test step, `test-tier0
--cross-check` took 5.8 s with clang's libraries and 4.6 s with these, against 4.2 s before it had any.

Then as many programs of the records shape are linked **mixed**: their even functions Tier 0's and their odd ones
clang's (clang's own copies of the even ones made weak), so that calls pass and return structs and unions by value
between the two compilers both ways; each must give what clang's own build gives. And as many of the deep shape, as the
larger shape's are.

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
  - a local's references miscounted: 749;
  - the ranks' tie-break inverted: 778; callee-saved registers for locals referenced once: 686, or left out of the
    frame's pushes: 614;
  - a call's argument registers one depth off in its count: 551; a stack parameter's load left out of the prologue's
    count: 161, or given a frame slot: 565;
  - a chosen form's destination not taken twice: 800; an imm16 written as an imm32: 390;
  - a cycle's `xchg` not passing its value on, which only the moves test reaches: 189 of its rounds;
  - and on the objects, 300 programs per shape:
    - objects not aligned: 968 of the objects test's checks and 27 programs' runs (an address modulo 16 that is 8);
    - the frame not kept a multiple of 16 for them: 697 and 2;
    - objects overlapping: the objects test, and runs that crash, loop until the alarm, or differ;
    - the vector path giving objects slots too, or the scalar path: 1,951 and 599 (the paths disagree);
    - each rule of the validator's for objects left out: its break taken;
    - a `VA_LIST`'s 24 bytes (#356's frames) taken as a value's 8 by either path or by both: 1,184 of the objects
      test's checks each;
  - and on copies:
    - the last 16-byte piece not pulled back to end at the copy's end, so that it copies past it: 17 programs' runs,
      once their objects' bits are folded into a global, and none before;
    - the second piece of a copy below 16 bytes left out: 101; the vector path's count without the last 16 bytes' load
      and store: 69;
    - the scratch left out of selection, general or XMM, on either path: 952 and 714 of the copies check;
    - the validator's size limits left out: their breaks taken.
  - and on indirect calls:
    - the callee's register one depth off: the paths disagree and runs crash;
    - its move left out of the vector path's count: 767; the call's depths, the emitter's: 767;
    - the callee left out of the effects: 713; or of the validator: 764 (valid streams refused);
    - the interpreter calling the generator's own choice, not the function the pointer holds: 29 programs' runs;
    - the generator's candidates of one arity but other parameters' or result's classes: 3 and 6 programs, whose
      calls the interpreter refuses. Checking the arity alone, as it did, the first passed and the second crashed
      the test.
    - al set before the moves have put the callee in r11, or not at all for an indirect call: the variadic calls'
      child crashes (its callee in rax overwritten), and their probe sees al 0. The programs, whose calls aren't
      variadic, see neither.

    Counting the callee as a live depth in selection gives a frame one slot larger, which no check sees, as it
    changes no value.
  - and on the aggregates (#137), by the fixtures alone (`--count 2`):
    - the high eightbyte alone on the stack taken as an aggregate whole: aggregate calls and functions of the high,
      split and ignored fixture;
    - a parameter's misaligned incoming slot taken as its home: the general parts fixture's parameter 6;
    - a general register's part of 3, 5, 6 or 7 bytes taken as exact, one load of its width (one byte): the general
      parts fixture's arguments 1 to 3;
    - the epilogue's copy of a temporary part left out: the result's high eightbyte in xmm0.
  - and absorbed lanes looking their value up as a local, past 32 locals (on main): the errors check's 40 locals,
    whose stray gather faults.
  - and on the operands' classes:
    - the rule left out: the operands check's nine refused streams taken;
    - the class compared with the entry's own, not the entry before's: 120 failed checks, the generated programs'
      streams refused among them.
  - and on variable-length arrays, 300 programs per shape (on the cloud VM):
    - `STACK_RESTORE` emitted as a move that leaves rsp: 1, the stack check's run, which overflows the stack;
    - allocations rounded to 8, not 16: 7 (addresses modulo 16 that are 8);
    - an allocation's address at rsp, over the outgoing area: 67 (calls' stack arguments written over it);
    - the homes left rsp-relative with a frame pointer: 549;
    - the epilogue releasing the frame by its size, not from rbp: 551.

    And on the producer's control stream (`c_check_program.c`), checked by `test-checker`'s programs: no restore at
    a block's end, none before a jump out of a block, or none before a `goto` back above an array's declaration. In
    each case the programs whose arrays would then take 80 MB of stack fail, on both paths.
  - and on deep values (#136's item 6), each a change to `tier0.c` with the backend's unit built at -O1:
    - one register fewer stored into its slot on entry: 347; one fewer loaded back on exit: 346;
    - every value of a window loaded from one slot: the deep shape's runs crash and differ;
    - integer results not stored into their slots: 185;
    - the branch before the loads back: 24;
    - a deep call's arguments one slot off: 41; its result one slot off: 112; floating values not stored around it: 57;
    - the vector path's count one record more past 9: 227 (the paths differ);
    - no choice of immediate forms in deep entries: 8;
    - a template for one depth's window without its fixed registers: the test crashes;
    - crossing the line not counted as deep: 11, the deep check's dropped values on both stacks (no generated program
      drops one there);
    - a window that only fixed registers read not counted as using the stack: 3, the deep check's division by a local,
      which found that bug in the first version;
    - the deep slots addressed from rsp (#434's review): the deep check's case with rsp moved, on all six inputs, and
      the deep shape's runs; the frame pointer's case without a move passes;
    - a deep variadic call's al left at 0 (#434's review): test-checker's `variadic-calls-past-the-depth-tables`, on
      both paths, whose callee clang compiles; test-tier0 passes;
    - a deep conversion's form chosen at its window's depths: 5, the deep check's conversion past the floating line,
      whose register form overwrote rbp, x's home there.
  - and on the 128-bit integers (#464, under Intel SDE): `add` for `adc` in `ADD`'s high half: 18 of the int128
    check's; the saves of rax and rcx left out: 185. And on its coverage (#464's review): a program refused for its
    depth counted toward it, or `JUMP_VALUE` sharing its bit with a conversion from I64, let the check pass without
    them; with the fixes, leaving out the programs that save rcx, or not recording `JUMP_VALUE`, fails it. And `sar 62`
    for `sar 63` in a signed `SHR`'s fill fails the boundaries of I128 alone; not counting I128's `SHR` fails its
    coverage; and an arena too small for every 50th program fails the check, where both paths refusing a function alike
    had passed it. A `?:` whose condition is the constant 1 fails it too (each class's arms), where counting the whole
    stream had passed the arm its run skipped.
- **Every stream source against the operands' rule:** a scratch build validated every function each compile path
  took, besides the validations the sources make, and printed every refusal. The sources were:
  - the generator's programs, with and without the cross-check;
  - `test-tier0`'s own streams;
  - #356's variadic frames;
  - the builtin lowering's recipes and helpers, the atomic RMW helpers' on the 9700X (their oracles need its Clang);
  - the unwind tests and benchmarks;
  - `benchmark-backend`;
  - #357's producer (`test-checker`) and #322's adapter, on #357's branch.

  Two tests' own streams broke the rule, and are rewritten in postorder here:
  - the copies check's;
  - one of the atomic RMW helpers' callers, which returned a value from under a call that returns nothing. It compiled
    right only because the return was the stream's last entry, the stack effect it was given being one short.

  A strict postorder rule (the entry before must push) would also have refused 110 of #357's streams, where a
  post-increment's old value is left under a store of its class, which both paths compile right.

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
- **The second round** (the call emitter's moves on bitmasks, the homes by ranks, the frame's slots by masks, the
  prologue counted, the chosen forms applied from a byte per form, selection without per-block branches, measuring's
  register mask), against #303's head on the current programs, run in turn three times: the vector path from 87.7 to
  76.5 cycles per entry back to back (196.7 to 163.4 instructions, 15.8 to 13.8 ns), from 89.3 to 78.4 in the full
  benchmark and to 78.7 after 100 ms of the scalar path, its power cap's count 2.1 per record against the head's 3.1.
  The scalar path, which shares the call emitter, went from 233.2 to 226.2 (589.0 to 559.8 instructions).
- **The chosen forms applied on the records' halves** (the compiler had split the fields into byte loads and
  stores): 75.6 to 74.8 cycles per entry, 163.5 to 162.0 instructions, in four rounds run in turn, each 1.0 to 1.2%.
- **Aggregates by value (#137)** at no cost to code without them. On these programs, which have no records, the 9700X
  job measured the vector path at 182.2 instructions per entry (84.1 cycles, 15.18 ns) when the aggregates' code was
  inline in every call's emission and count, in selection's loop and in the passes over the parameters, against main's
  166.8 (78.6 cycles, 14.19 ns), and the scalar path at 604.6 instructions against 588.6. Specialized and out of line
  ("At no cost to the rest" above), the vector path took 168.8 instructions per entry (79.2 cycles, 14.28 ns) against
  main's 166.8 (78.1 and 78.9 cycles, 14.10 and 14.23 ns, in its two runs before), and the scalar path, after #407's
  fusion, 574.5 against 572.8 (230.8 cycles against 230.2 and 232.7). The cycles' differences are within main's own
  spread; the 2 instructions per entry left on the vector path are each call's setup and each function's checks of its
  optional columns. #400's last head (`42dd119`), whose stricter checks of the parameters' registers don't run on these
  programs, measured 169.22 and 576.86 instructions per entry against main's 166.77 and 572.82 at `04dcadb`: inline
  in the paths' shared passes, the checks' code changed clang's code for the passes. With the checks out of line
  (#411), the job measured 169.31 and 574.35, the cycles (80.27 and 232.97, against 80.11 and 229.83) within the runs'
  spread. Under Intel SDE, no variant of the passes tried brought the vector path back to the 0.5 instructions per entry
  fewer that `ac4ccbd`'s inlining happened to give.
- **Long double (#136's item 8)** at no cost to code without it (#423's review). On these programs, which have no long
  double, the 9700X measured `6c33fae`, whose x87 checks were inline in both selections' loops, in the call's emitter and
  count and in the deferred entries' dispatch, against main's `7c428ce` in four rounds in alternating order: the vector
  path at 171.56 instructions per entry (81.0 to 81.9 cycles) against 169.31 (79.2 to 79.6), and alone 79.5 cycles
  against 77.8 and 78.0; the scalar path at 580.49 instructions against 574.35, its cycles (231.4 to 231.8 against 232.5
  to 232.8) within the runs' spread. The checks themselves cost little: on the 7940HS, removing the four groups took the
  vector path from 170.66 to 168.62 instructions per entry against main's 168.46, and each alone part of the way, as
  their code changed clang's for the passes around them. With the flag (`Tier0Function.x87`) and the long doubles' code
  out of line ("Long double" above), the 7940HS measures 169.01 and 575.97 instructions per entry against main's 168.46
  and 574.25: a test of the flag at each use, and clang's code for the shared passes, which moves by about that much with
  any change to them. The objects of c-testsuite's other 219 programs (both paths) and of the cross-check's 180 programs
  are the same bytes as main's.
- **Against #115's budget:** the budget is about 32 cycles per output token for selection, allocation and encoding.
  The vector path is still about 2.3 times that; the budget is not met.
- **Where the time goes** after the second round (perf on the 7940HS, each pass kept out of line), as shares of the
  vector path's time:
  - emission 21%, fusion and depths (P0 to P4) 16%, selection 13% (the slot gather the largest part), measuring 10%;
  - the call emitter and its count 14% together, for the 3.6% of entries that are deferred;
  - the immediates' choice (`x86_choose_forms`) and applying it 11%;
  - the homes, the frame, the prologue and the per-function setup 15%.

**Generated code against clang -O0** (the cross-check's 60 programs for each of seeds 1, 2, 3, 5, 7 and 11, on the
9700X, each run alone under its lock and pinned to CPU 2):

- **Run time:** 0.93 to 0.95 of clang's (0.86 to 0.92 on the 7940HS). The programs have had objects, indirect calls
  and copies since #362 to #364, and every object's bytes are read into a global as each function ends. An object
  lives in memory for both compilers, so Tier 0's register homes gain less on such code: before them, its run time
  was 0.79 of clang's (0.70 to 0.78 on the 7940HS).
- **Code size:** 1.10 to 1.11 of clang's `.text`. These programs are full of floating constants, which Tier 0 materializes
  through a general register (up to 15 bytes) where clang loads them from `.rodata` (not counted in its `.text`).
  They are also full of constant arithmetic, which #133 will fold before Tier 0 sees it.
- **rsp-relative frame operands** take a SIB byte that clang's rbp-relative ones don't.

## Follow-ups

- **Deep values' refusals:** a function with values of the memory classes past the depth tables, and a call with
  aggregate arguments there (`TIER0_ERROR_DEPTH`). The memory classes' would take their slots by true depth in the deep
  emitters; the
  aggregates', their copies through a deep scratch register. And deep code that hovers around the tables' edge stores
  and loads every register at each crossing: a stack of 15 integer values that takes and drops one a few times pays
  about 30 records each time, where storing and loading the one register the entry needs would do.
- **Uncovered entries:** these are refused (`TIER0_ERROR_UNSUPPORTED`) or not yet in the stream:
  - objects aligned past 16, which need the frame realigned (a frame pointer now exists for variable-length arrays);
  - bit-fields need no entry of their own: the checker's lowering reads and writes the bytes that hold a field's bits
    as pieces of 8, 4, 2 and 1 bytes, with shifts and masks (`docs/c-checker.md`, "Bit-fields"). It refuses a field
    whose bits are spread over more than 8 bytes (a packed one), with `CHECK_PROGRAM_UNSUPPORTED`;
  - `_Complex` values, and `va_arg` of them (the scalar classes, long double, and structs and unions have recipes);
  - inline assembly, `setjmp` and atomics;
  - `switch` (no kind yet: a compare sequence or a jump table).
- **The access column's lookups** are an entry at a time for the global entries of a block. A gather from a column
  padded for it would take a block's at once, if real code shows them to cost.
- **The rest of the time:** emission, which is about at its design's cost (a line, a `vpermb`, a blend and a store
  per entry); the call emitter, an entry at a time; and selection's gather from a table larger than L1.
- **The immediates' choice** costs about 7 cycles per entry: the application about 5 (its loads of the records more
  than its arithmetic), `x86_choose_forms` about 2 (3.6 cycles per request, batched), the requests' columns about 1.
  27% of the entries make a request; 6% of the requests are static (a branch on a local's `cmp home, 0`), so deciding
  those per template at initialization would save under 1%; and choosing in emission's blocks of 8 would run the
  chooser's candidates on 8 lanes for about 2 requests, more than the batch costs. What would remove the requests is
  deciding each form at selection, from the constant's value class (P0 has its predicates) and whether its register
  is rax (its depth's), with templates keyed by them.
- **Floating constants** from a per-unit pool (`GLOBAL` of a pool symbol, the front end's choice); and rbp-relative
  frame operands where they encode shorter.
- **Real corpora:** with #133, the vector and scalar paths on every function of the corpora; contract 6 measured on
  them; `sqlite3.c` and `speedtest1` (#146); #144's benchmark rows for real functions; the whole compile against
  tcc. c-testsuite (#145) runs.
- **Unwind tables (#88):** done outside this file. #345's `unwind_tier0.c` derives each function's CFI from its sized
  records, the fixed prologue and epilogue (`docs/unwind.md`). It refuses a function with a frame pointer, whose body
  writes rsp (an allocation, or the epilogue's `mov rsp, rbp`): those functions' CFI would follow rbp. It also refuses a
  function with long double code, whose x87 forms `x86_semantics.c` has no rows for yet.

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
| Entries' first records + branch sizing's offsets → source rows | Implemented for the C driver's `-g` (#395): `Tier0Code.entry_records` are the measured positions both paths emit from, which `check_program_emit` turns into byte offsets after sizing and `c_check_debug.c` into DWARF rows. |

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
reconstruction. Labels are deliberately derived from control entries (but those
an indirect jump reaches, which only the producer knows) and the
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
