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
- **Objects in the frame:** local arrays, structs and unions at their alignments, to 2^30: past 16 bytes, in a
  realigned frame ("Realigned frames", below), and values that must live in memory, read and written as objects.
- **Indirect calls,** through `r11`.
- **Block copies** (`COPY`) of up to 256 bytes, a struct's assignment.
- **Structs and unions passed and returned by value** (#137), in registers, on the stack and through a hidden pointer,
  from #137's locations ("Structs and unions by value", below), and **variadic functions' incoming frames** (#356).
- **Variable-length arrays:** allocations on the stack (`ALLOCA`) and the stack's position saved and restored
  (`STACK_SAVE`, `STACK_RESTORE`), in a function with a frame pointer, or in a realigned frame with a base (r15); and
  the frame's address (`FRAME_ADDRESS`, #148's `__builtin_frame_address(0)`), its frame pointer.
- **Tail calls** (#147's musttail): a `TAIL_CALL` calls its row as the function's return, its arguments where the
  function's own arrived, the frame released before a jump ("Tail calls", below).
- **Symbols reached otherwise than directly** (#136's item 4): through their GOT slots (a shared library's data, and a
  function's address that may be one) and thread-locals by local-exec and initial-exec, from an optional access column.
- **Long double** (#136's item 8): the x87's 80-bit values, in memory, passed on the stack and returned in st0, and
  structs returned in x87 registers ("Long double", below).
- **128-bit integers** (#136): `__int128`'s and `unsigned __int128`'s classes, I128 and U128, whose values live in the
  slots long double's do ("128-bit integers", below), which #133's producer lowers `__int128` to ([the checker
  guide](c-checker.md)'s "Backend handoff").
- **Vectors** (#136): GNU C's vector types and the intrinsics' `__m128` to `__m512i`, one class, VECTOR, whose values
  live in 64-byte slots, and machine operations (the `X86` kind's rows, which name their registers by role), for a
  target with AVX-512 ("Vectors", below).
- **Conversions between U64 and the floating classes** (#136's item 5), which SSE has no instruction for before
  AVX-512 ("Conversions between U64 and floating values", below).
- **Values past the depth tables** (#136's item 6): more than 15 integer (14 with a frame pointer) or 16 floating
  values at once, among them calls of at least 127 arguments and calls with aggregate arguments, in frame slots
  ("Deep values", below).
- **The interfaces to the existing modules:**
  - calls through #137's locations;
  - branch columns for #144's `size_branches`;
  - each typed entry's first record (`Tier0Code.entry_records`, the epilogue's after the last), when the producer asks
    for it (`Tier0Function.entry_records`; a compile without it allocates and copies nothing for it), which with branch
    sizing's offsets says where each entry's code starts: the C driver's source rows for `-g` (#395,
    `c_check_debug.c`);
  - the frame (`Tier0Code.frame_saved`, `frame_size`, `frame_pointer`, `frame_alignment_log2` and `frame_base`: the
    callee-saved registers the prologue pushed, in `tier0_callee_saved`'s order, the bytes it reserved below them,
    whether it pushed rbp first and pointed it at the push, the alignment it rounded rsp down to after the
    reservation and whether it then copied rsp into r15), which `unwind_tier0_frame` reads with the prologue's and
    epilogue's records for the C driver's unwind tables (#395, [the unwind guide](unwind.md)): the prologue is the
    code's first records and the epilogue's release, pops and ret its last, and each tail call's jump
    (`Tier0Code.tail_records`) comes right after the same release and pops;
  - `LinkReference`s for `link_add_code_fixups`;
  - the immediates' shortest forms through #221's `x86_choose_forms`.
- **`test-tier0`,** with a cross-check against clang -O0.
- **`benchmark-backend`,** on generated programs, and **`benchmark-backend-source`,** on checked functions' typed streams
  of C files, `sqlite3.c`'s among them ("Measurements", below), compiled for the target their options give (`-march`
  and the `-m` features), as `-c` compiles them (#1041).
  The source benchmark admits the reachable functions through typed emission.
  Static-data publication and object-section placement, including assembled
  file-scope atoms' section descriptors, are outside its validation scope.
  For example, a valid function stream under a named `.note.*` section can be
  measured even when whole-object publication explicitly refuses that section.
  The driver and corpus object checks establish whole-object eligibility.

- **c-testsuite (#145):** all 220 of its programs compile through #133's producer and Tier 0, link by the direct path
  and run right, with both paths' executables the same bytes and every record passing `x86_validate`
  ([the c-testsuite guide](c-testsuite.md)).

**Measured on SQLite,** which compiles now: Tier 0's own costs on `sqlite3.c`'s functions ("Measurements", below),
and the whole compile's time and the generated code's run time and size against clang -O0's and tcc's, on its shell
([#136's measurement](https://github.com/buster14a/buster/issues/136#issuecomment-6421100260)) and on `speedtest1`,
SQLite's own benchmark ("Measurements", below).

Generated programs (below) stand in for #142's random programs.

## The typed stream (#133 → #136)

**A function** is three columns over its `count` entries, in evaluation order (`Tier0Function` in `tier0.c` has the
exact rules):

- `kinds`: `BUSTER_TYPED_*`, below 64;
- `classes`: `BUSTER_TYPED_CLASS_*`, the machine class;
- `values`: 64 bits each.

Each function may have up to 16,777,216 entries and 16,777,216 locals, including parameters and lowering temporaries
(`BUSTER_TIER0_MAX_ENTRIES`, `BUSTER_TIER0_MAX_LOCALS`). Local identities and homes use 32-bit indices; #622 removed
the former 65,536-local cap. Available arena memory and representable frame displacements still bound compilation.
Per-entry instruction counts use 32 bits, including a call's complete aggregate copies. Both paths accumulate the
prologue/body/epilogue total in 64 bits and refuse output before allocation when its record identities, including
emission padding, cannot fit 32 bits. Available arena memory remains a separate bound.
Calls permit up to 239 arguments (`BUSTER_TIER0_MAX_CALL_ARGUMENTS`), within the 240-depth storage bound. An indirect
call also holds its callee, so 238 simple arguments fit that bound; expressions inside arguments can need additional
live values. Named, variadic and indirect calls of 127 mixed scalar/aggregate arguments are covered by native and
Clang cross-checks. Beyond the argument bound, the C driver names the 239-argument limit.
The C driver names an exhausted entry or local limit separately from insufficient lowering storage. Its
`test-checker-driver` regression compiles 32,768 blocks with 65,537 locals through both paths, compares the objects,
and runs their code against an unsigned arithmetic loop and clang.

Beside them are three tables, and optional parts for aggregates and for the symbols' access:

- **Locals,** parameters first, each with a class and flags (`BUSTER_TYPED_LOCAL_ADDRESS_TAKEN`). Two optional columns
  make some of them **objects** in the frame: arrays, structs and unions.
  - `local_sizes`: an object's size in bytes, 0 for a value's local.
  - `local_alignments`: its alignment's log2, at most 30 (`BUSTER_TIER0_MAX_OBJECT_ALIGNMENT_LOG2`, 1 GB); past 4 (16
    bytes), its function's frame is realigned ("Realigned frames", below).

  An object:
  - is a local of class VOID with `ADDRESS_TAKEN`, and a parameter only when it is a struct or union passed by value
    ("Structs and unions by value", below), or a front end's value that must live in memory (a long double's, a 128-bit
    integer's, one aligned past 8 bytes);
  - is reached through `LOCAL_ADDRESS`: its elements and members are loads and stores through that address
    (`PTR_ADD`, `LOAD`, `SET`), as a front end lowers `a[i]` and `s.m`. `LOCAL`, `STORE_LOCAL` and `SET_LOCAL` of a
    class may name one too, of at least the class's bytes (a memory class's slot's 16: `tier0_value_object`), whose
    first bytes they read or write, its home a memory operand as a slot's is: a front end's value that must live in
    memory, which it then reads and writes as it would a value's local;
  - with the others, takes at most `BUSTER_TIER0_MAX_OBJECT_BYTES` (1 GB), each counted as its size rounded up to 16
    and, aligned past 16, its alignment less 16, so that every frame displacement fits a disp32.

  A function without objects leaves both columns 0.
- **The parameters' locations** at entry, and the result's.
- **Calls:** each call's callee `SymbolId`, its arguments' classes and locations, whether it is variadic, and its
  `AbiCall`. A callee of 0 makes the call indirect: the stream pushes the address it calls (U64) before its
  arguments, and the `CALL` pops it after them. An optional column, `call_tails`, marks the rows that `TAIL_CALL`s
  call ("Tail calls", below).
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
  - `ALLOCA` pops a byte count, rounds it up to 16 (to the frame's alignment in a realigned frame, so that rsp keeps
    it), takes it from the stack and pushes the allocation's address, a multiple of 16;
  - `STACK_SAVE` pushes the stack's position, and `STACK_RESTORE` pops one, releasing what was allocated after it;
  - `FRAME_ADDRESS` pushes the frame's address, its frame pointer: where the prologue's push saved the caller's rbp,
    under the return address, as clang -O0's frame has it.

  A producer saves the position into a local before a block's first array, and restores it from there at the block's
  end and before every jump out of the arrays' scope (a loop's next iteration is one), as clang does. A return needs
  none: the epilogue releases the frame. That a restored position is an earlier save's is the producer's to keep.
  `__builtin_alloca` is an `ALLOCA` of its own, which a block's restore releases where clang's `stackrestore` does.

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
- `TAIL_CALL` calls its row of the calls table as the function's return, at statement level ("Tail calls", below).
- `JUMP_INDIRECT` pops an address (U64) and goes there: GNU C's `goto *p`, at statement level. Its targets are the
  function's labels whose addresses the code takes (`&&label`), an optional column, `label_entries` (`label_count`
  entry indices, each a statement's start or the count). Tier 0 reports each one's first record
  (`Tier0Code.label_records`); the front end defines the label's symbol there, after branch sizing, and an address of
  a label is a `GLOBAL_ADDRESS` of it. Each label is a target in the fusion's windows, reached with nothing on the
  stacks. A switch's jump table (#785) is a `JUMP_INDIRECT` that reaches only its own table's labels, which two
  more optional columns say, `table_count` tables of them: `label_tables` gives each label its table (1 + t for
  table t's, 0 for a label whose address the code takes, table t's labels after table t − 1's) and `table_jumps`
  each table's `JUMP_INDIRECT`, its entry's index, in increasing order. Any other `JUMP_INDIRECT` is a `goto *`,
  which reaches only the labels of no table. `tier0_validate` checks the columns in one pass over the tables and
  one over the labels, by both of its paths; Tier 0's code is the same with them as without, taking every label for
  a target of every `JUMP_INDIRECT` all the same.

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
incoming displacements outside signed 32-bit addressing. The incoming area is
aligned to 16, or to a named argument's alignment past it, which raises it (a
realigned frame's incoming places are rbp's). Shared `AbiVaList` and save-area
constants are unchanged. Source production and Tier 1 support remain separate;
the typed-to-IR consumer must reject these roles until it supports their layout.

`test-tier0-variadic-frame` prepares actual Clang-compiled callers of generated
variadic callees with GP/FP exhaustion, named stack arguments, real va_start /
va_copy / va_arg recipes, intervening calls and full vector saves. It compares
both Tier 0 paths and both encoders, guarded output edges, 24-byte list storage
and independent ordinary-function byte fixtures. Every fixture also holds an
object aligned to 16 (#362) or past it (to 32, 64, 128 and 4096, in a realigned
frame), whose bytes must survive the lists, the save area and the call, at a
multiple of its alignment, apart from the lists, the canaries and the save area
and below the overflow area. Run builds and tests only on
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
  alignment, to 16 (the incoming area starts at a multiple of 16); the prologue copies the others' slots into homes of
  their own: a misaligned whole one, the high or the low eightbyte alone in a slot, a SPLIT one's slot, and one aligned past 16,
  into the realigned frame, since its caller aligns its slot only as far as its type's own alignment goes (an aligned
  typedef passes as its base).
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
register of its own, an XMM one taking all 16 bytes when it is a 16-byte aggregate's only part. A result whose
`part_bytes` is its size, fewer than 8 bytes, alone in one register (an `_Atomic` vector of 4 bytes, which LLVM returns
as its value in xmm0, #137) is such a part too, an eightbyte's (`tier0_aggregate_result_valid`). A STACK location with
`BUSTER_ABI_FLAG_HIGH` or `BUSTER_ABI_FLAG_LOW` is one part too, the high or the low eightbyte alone, in its 8-byte
slot: a copy of the aggregate's bytes would run into the next argument's slot (#137). A part loads only the
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
incoming one past int32, a call's stack alignment past 2^30, a result object that isn't an object with room for the
registers' whole widths, a hidden pointer without an object or where an argument takes rdi, a function whose hidden
result pointer isn't parameter 0 in rdi, and an aggregate result of more than two registers. A function returning an
aggregate, in registers or through the hidden pointer, must leave rax an address at every `RETURN`, which must be of
U64, and mustn't reach its end without one. `test-tier0`'s aggregate and parameter errors break each rule (below).

The producer (`c_check_program_lower.c`, [the checker guide](c-checker.md)) gives every call site with an aggregate
result an object of its own, a function with a hidden result pointer its parameter 0, and an aggregate parameter's
object its alignment, whatever it is (past 16, its function's frame is realigned, and a call's outgoing area aligned
past 16 realigns its caller's); `docs/calling-convention.md` has the cross-check of C that this compiler compiles
against clang's and GCC's.

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
  addresses: there is no `GLOBAL`, `STORE_GLOBAL` or `SET_GLOBAL` of F80, and a front end reads a global with `LOAD` and
  writes it with `STORE` or `SET`, the address first. `LOCAL`, `STORE_LOCAL` and `SET_LOCAL` of F80 name an object of
  at least 16 bytes (`tier0_memory_object`), whose first 10 they read or write, an `fld` and an `fstp` between it and
  the value's slot: a front end's locals of the class, and its temporaries of it, objects of their own. A parameter's
  home is its incoming stack slot, whose place is a multiple of 16.
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
  long double's are: `LOCAL`, `STORE_LOCAL` and `SET_LOCAL` of them copy an object's first 16 bytes to or from the
  value's slot through a scratch register (the depth register the value's slot leaves free), and a global is read with
  `LOAD` and written with `STORE` or `SET`, the address first.
- **The kinds:** `CONST` (its `value` the low 64 bits, sign-extended for I128 and zero-extended for U128: a front end
  builds a wider constant from halves), `DUP`, `LOCAL`, `STORE_LOCAL`, `SET_LOCAL`, `NEG`, `NOT`, `CONVERT` (to and from the classes I8 to U64, the low bits
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
- **Past the slots,** a memory class's value at a depth past the integer table's 15 (`slot_top`) takes a function to
  `tier0_select_deep`, which gives the slots as many depths as its stack reaches ("Deep values", below). Before #780,
  both selections refused such a function (`TIER0_ERROR_DEPTH`); before that, a stack of 15 long double constants wrote
  past the slot area.

`tier0_validate` refuses a local of the classes that isn't an object, a `LOCAL`, `STORE_LOCAL` or `SET_LOCAL` of them
of anything but an object of at least 16 bytes (a value's local, a smaller object, a `va_list`), a global read as a
value, `DIV` and `MOD` of them,
a conversion between them and a floating class, a call's argument or result of them, a `RETURN` of them, and an entry
of the classes in a function without `int128`. `interpreter.c` and `ir_lower.c` refuse them, as they refuse F80.

## Vectors (#136)

GNU C's vector types and the intrinsics' (`__m64`, `__m128`, `__m256i`, `__m512d` and the rest) are one class, VECTOR
(`BUSTER_TYPED_CLASS_VECTOR`, 14), of 4, 8, 16, 32 or 64 bytes. Like the memory classes, a value takes an integer depth and
lives in that depth's slot (`Tier0Frame.vector_area`, 64 bytes per depth), its bytes from the slot's first, so that
calls and the arms of `?:` need nothing of their own for it. The class doesn't carry the width (no class number is
left below the vector path's 16: the masks took the last one): each entry that makes or moves one has its own, and
`tier0_validate` follows each depth's width as a class (`BUSTER_TIER0_STACK_VECTOR`), so that a value of other bytes than
an entry's is refused, not miscompiled.

- **The kinds:** `CONST` (`value` an index into `Tier0Function.vector_constants`, 64 bytes each, with each one's bytes in
  `vector_constant_bytes`), `DUP`, `LOAD`, `STORE` and `SET` (`value` the vector's bytes at the address), `LOCAL`,
  `STORE_LOCAL` and `SET_LOCAL` (of an object of the vector's bytes, all of which they read or write), `DISCARD` and
  `JUMP_VALUE`. Their operations are `X86` entries, below. Their globals are reached through their addresses, as long
  double's are.
- **Their code,** every entry deferred (`tier0_emit_vector_value`, counted by `tier0_count_vector_value`), through zmm16,
  which no value of the floating stack's is in: `vmovdqu64` of the value's bytes between memory (the address, the
  object) and zmm16 (EVEX's `vmovd` and `vmovq` of 4 and 8 bytes), and of all 64 between zmm16 and a slot, whose bytes
  past the value's are then unspecified, as a narrow integer's register's are; a constant of zeros by `vpxord` and a
  store, any other by its words as immediates (`mov qword` of a sign-extended imm32, or two `mov dword`; a constant of
  4 bytes its word whole, of which its bytes are the low half); `DISCARD` and `JUMP_VALUE` nothing.
- **Calls pass and return them as aggregates,** as `__int128`'s: an argument is an object passed by its address
  (`argument_sizes` its bytes), a call's result comes back into its result object, and a function returns one by its
  object's address (`result_size`), each in one vector register whole as the convention places it (an XMM, YMM or ZMM
  register: `tier0_parts_of`'s parts of 16, 32 and 64 bytes, which a struct of one vector takes too; one of 4 or 8 bytes
  an SSE eightbyte's part in an XMM register's first bytes, as a struct of floats is), or on the stack. A parameter is
  an object the prologue stores its register in.
- **A function says whether it has vectors** (`Tier0Function.vectors`: VECTOR values or `X86` entries), which gives it
  the vectors' slots and the copies of selection, measuring and the deferred entries' emitter that look for their
  entries (`tier0_memory_function`), as `x87` and `int128` do. Tier 0's own records for them are EVEX ones, so a front
  end gives it to a unit whose target has AVX-512 (F, VL, BW and DQ), as the intrinsics of 64-byte vectors need.

**Machine operations (`X86`).** An `X86` entry's `value` indexes `Tier0Function.operations`, rows (`Tier0Operation`) of
up to 8 records each with their operands' classes (integers, floating values and VECTORs, each VECTOR one's bytes) and
their result's, which the entry pops and pushes as `CALL` pops its arguments. A row's records name registers by role
(`BUSTER_TIER0_ROLE_*`), never by number, and Tier 0 places each role at the entry (`tier0_operation_placement`):

- `VECTOR(j)`, the row's vector register j, which holds VECTOR operand j (loaded from its slot at its width, the
  register's bytes past the operand's zero, so that a form that reads it wider reads zeros: an XMM form reads one of 4
  or 8 bytes, as clang's code of `__m64` does) when the records start, and
  whose register 0 holds the VECTOR result (stored into the result's slot whole) when they end; the others are free. The
  VECTOR registers are the floating stack's free ones from its depth on (one further past a floating result's where no
  floating operand takes its place), so a VEX form (which names registers below 16) reaches them where the floating
  stack leaves room, and an EVEX one past it, to 31; a row whose VEX forms would reach past 16 takes the deep slots, as
  an integer depth past the table does ("Deep values", below). The selections count the registers the operands' loads
  write and the result's store reads among a row's, whatever its records name, so that no floating home is kept in one;
- `FLOATING(j)` and `FLOATING_RESULT`, a floating operand's XMM register and the floating result's;
- `INTEGER(j)`, `INTEGER_RESULT` and `SCRATCH(k)`, an integer operand's register (an address among them), the integer
  result's, and up to four free ones, the depth registers past the entry's. An operand's is in a field no wider than its
  class, whose register's bits past the class's are unspecified: a base or an index a 64-bit integer's, a 32-bit field a
  32- or 64-bit one's, so a front end extends a narrower operand first (`CONVERT`);
- `MASK(j)`, `MASK_RESULT` and `MASK_SCRATCH(k)` in an opmask register's field, and as an EVEX opmask decoration
  (`BUSTER_TIER0_DECORATION_MASK(j)` and `_MASK_SCRATCH(k)`, by decorations' bits 3 and 4, which a placed record has
  zero): a mask operand's register, the mask result's and up to four free ones, on the mask stack ("Masks", below);
- or the opmask registers by number, k0 to k7, in a row without masks, around which Tier 0 saves the masks that the
  registers it names hold.

`tier0_operation_valid` checks each row: VEX and EVEX forms, their operands in their fields (no implicit register:
`vzeroupper`, `vmaskmovdqu`, `mulx` and the string compares are refused, as are pairs and blocks of registers but
VP2INTERSECT's opmask pair, two of the opmask registers no value keeps), and the legacy forms
`tier0_operation_legacy_forms` lists, for the x86 builtins VEX and EVEX have no form of (#148: `mov`, the flags' tests'
`setcc`, `and` and `or` of bytes and `movzx`, `adc`, `sbb`, `neg`, `crc32`, `rdrand`, the FS and GS bases, the
non-temporal and direct stores, the cache lines', `fxsave` (not `fxrstor`, which writes the XMM registers floating
values live in), the shadow stack's, SHA's and SSE4A's, whose XMM registers are below 16 as VEX's are, and the
extensions' system instructions: RAO-INT's atomic operations, `cldemote`, `serialize`, UINTR's, `ptwrite`, `umonitor`,
MOVRS's loads, RTM's `xend` and `xtest`, TSXLDTRK's, ENQCMD's and USER_MSR's register forms); a VEX form without
operands only where its effects name no register either (`tilerelease`, not `vzeroupper` or `vzeroall`); no branch,
symbol or RIP-relative address, each field's role of its class (an integer operand's no narrower than its field, the
integer result's in a field of 32 or 64 bits), and each record `x86_validate` takes with its roles placed. So a row
reads and writes its operands, its result, its scratch registers, the opmask registers, the flags and memory through its
addresses, and nothing else Tier 0 keeps; which forms a unit's target allows is the front end's to decide (#180's
policy), as the instruction set of its rows is.

`tier0_validate` refuses a vector entry in a function without `vectors`, a local of the class that isn't an object, a
`LOCAL`, `STORE_LOCAL` or `SET_LOCAL` of anything but an object of 4, 8, 16, 32 or 64 bytes, a `LOAD`, `STORE` or `SET` of
other bytes or of a value of other bytes, a constant past `vector_constants` or of other bytes, an `X86` entry past
the operations, of another class than its row's result or of an operand of another class or width, the arms of a
`?:` of two widths, a call's argument or result of the class, `RETURN` of it, the kinds vectors don't take, and a
vector operand of an entry of another class, a `CONVERT`'s or a compare's among them (their arithmetic, compares and
conversions are `X86` entries). `interpreter.c` and `ir_lower.c` refuse the class and the kind.

## Masks (#179)

AVX-512's opmasks are a class of their own, MASK (`BUSTER_TYPED_CLASS_MASK`, 15, the last one below the vector path's
16), of 1, 2, 4 or 8 bytes (8, 16, 32 or 64 bits), the width each entry's, which `tier0_validate` follows from depth to
depth as a vector's (`tier0_stack_mask`). Unlike a vector, a mask lives in a register: it takes a depth of a third stack,
the mask stack, beside the integer and the floating ones, whose depths 0 to 6 are k1 to k7 (`tier0_mask_register`, the
opmask registers an EVEX form may take as a write mask) and whose deeper ones live in their 8-byte frame slots
(`Tier0Frame.mask_area`), its bits in the low bits and zeros past them, in a register or a slot.

- **The depths.** A mask takes neither the integer nor the floating stack (`tier0_stack`'s 0), so that those stacks'
  effects and depths don't change; its own stack's depth before each entry is a column of its own
  (`Tier0Entries.mask_depths`), each entry's effect from its shape's operands and result of the class or its row's
  (`tier0_mask_effect`), their exclusive prefix sums 64 entries at a time (`tier0_vector_mask_depths`, an `X86` entry's
  effect from its row, as the calls' are) with the scalar twin `tier0_mask_depths_scalar`, made only for a function
  that says it has masks. No mask entry absorbs or is absorbed: no template of the class has a fused variant. The stack
  is bounded as the others are (`BUSTER_TIER0_MAX_DEPTH`): both paths refuse a function an entry of which finds 240
  masks or more on it (`TIER0_ERROR_DEPTH`, which the C driver reports as more values live at once than Tier 0's deep
  slots take), since the column is of bytes. The vector path's sums are bytes too, the depths modulo 256 (a block's own
  sum among them, which rows of many mask operands take past an int8's range), exact below 256; and as an entry pushes
  one mask at most, the first entry past the bound finds it exactly, so that the highest byte refuses what the twin
  refuses.
- **The kinds:** `CONVERT` to a mask from an integer class (its value's low bits, as C converts it to the unsigned
  integer of the mask's width: a narrower one extended first by its signedness, `tier0_mask_extension`) and from one to
  an integer class (as C converts that unsigned integer), `value` the mask's bytes either way; `LOAD`, `STORE` and `SET`
  (`value` the bytes); `LOCAL`, `STORE_LOCAL` and `SET_LOCAL` of an object of the mask's size; `AND`, `OR`, `XOR` and
  `NOT` (`value` their masks' bytes); `DUP`, `DISCARD` and `JUMP_VALUE`; and `X86` entries' operands and results. A
  constant is an integer's `CONST` converted. Nothing passes or returns one: the convention passes nothing in the opmask
  registers.
- **Their code,** every entry deferred (`tier0_emit_mask`, counted by `tier0_count_mask`) but `DISCARD` and
  `JUMP_VALUE`, whose masks stay where their depths are: `kmov` of the mask's width between its register and a general
  register or memory (`kmovb`, `kmovw`, `kmovd`, `kmovq`, each of which zeroes the register past it), `kand`, `kor`,
  `kxor` and `knot` of its width, `kmovq` between registers. A mask past k7 is computed in k0, the class's scratch, which
  no value takes, and moved from or to its slot whole; `AND`, `OR` and `XOR` of two masks in their slots take k1 too,
  saved into slot 0 and loaded back around them.
- **Calls save them.** Every opmask register is caller-saved, so a call (and a deep one, `tier0_emit_deep_call`) saves
  each mask in a register into its slot and loads it back after (`tier0_saved_masks`); the masks past k7 are in their
  slots already.
- **Rows take them by role.** An `X86` row's mask operands, result and scratch registers are `MASK(j)`, `MASK_RESULT` and
  `MASK_SCRATCH(k)`, placed by `tier0_operation_mask_plan` from the mask stack's depth m before it: its n mask operands at
  depths m − n to m − 1, the result at the first one's depth (m with none, as `INTEGER_RESULT` is placed), its scratch
  registers past them. Where all of those are k1 to k7 each is its depth's register and the row needs nothing more;
  past them the row borrows the stack's first registers (its operands from k1 on, its result k1, its scratch past them),
  saving their masks into their slots before it and loading them back after, with its operands moved in first and its
  result moved to its place last (`tier0_emit_operation_masks`). A row without masks may name opmask registers by number
  as its scratch, and Tier 0 saves the masks those registers hold around it; a row with masks names k1 to k7 by role
  alone. A row's mask result must have zeros past its bytes, as the forms that write a width give. In EVEX's opmask
  decoration, the roles are bits 3 and 4 beside the index (`BUSTER_TIER0_DECORATION_MASK(j)` and
  `BUSTER_TIER0_DECORATION_MASK_SCRATCH(k)`), which every 64-bit-mode record has zero: only the 16- and 32-bit modes put
  a segment override in bits 3 to 5 (`BUSTER_X86_DECORATION_SEGMENT`), and 64-bit mode's FS and GS overrides are a
  form's (`BUSTER_X86_FORM_FS` and `_GS`), which no row takes. Placing a record clears the role's bits and keeps the rest,
  zeroing and an `_ER` form's rounding in bits 5 and 6, so that what a 32-bit record would read as FS's override
  (0x28) is `MASK(0)` rounding down.
- **A function says whether it has masks** (`Tier0Function.masks`), which gives it the mask stack's column and its
  slots (as many as the stack reaches, and at least one per register, which a borrowing row saves); it has `vectors`
  too, on the EVEX path (`vector_bytes` 0): its records' `kmovb` and `kmovq` need AVX-512 DQ and BW.

`tier0_validate` refuses a mask entry in a function without `masks`, masks without `vectors` or on a vector transport
without AVX-512, a local of the class that isn't an object, a `LOCAL`, `STORE_LOCAL` or `SET_LOCAL` of anything but an
object of 1, 2, 4 or 8 bytes, a `LOAD`, `STORE` or `SET` of other bytes or of a mask of other bytes, logic of masks of
other bytes than its value's, a conversion to a mask from anything but an integer and from one to anything but an
integer or of other bytes than its value, a mask's constant, call, return, branch or compare, an integer's entry of
a mask (`ADD` among them), a row's operand of another width, and the arms of a `?:` of two widths; and in a row
(`tier0_operation_valid`), a mask role of an operand that isn't a mask, `MASK_RESULT` without a mask result, a
decoration's role of another kind (both of its bits) or past the scratch registers, opmask registers by number in a row
with masks, and mask operands and scratch registers past the seven a borrowing row has (and, as in any row, a form with
a segment override). `interpreter.c` runs mask entries (but rows and objects, which it doesn't model), each mask's bits
in its value's low word and its bytes in the high word;
`ir_lower.c` refuses a function with masks, since the IR's `m8` to `m64` have no conversion to or from an integer
(`docs/ir-simd.md`), so that `--tier1` leaves it to Tier 0.

The front end's AVX-512 builtins are the class's first user (`check_x86_lower_masks`, `c_check_program_x86.c`): a row
whose mask operands its records move into opmask registers from their integers, and whose mask result one moves out,
takes and gives them in the class instead, the moves Tier 0's conversions at C's integer boundary
(`check_lower_x86_row_builtin`), where the unit's target has AVX-512 BW and DQ.

`test-tier0`'s masks check (`tier0_mask_test.c`) runs random programs over the class natively, on both paths and both
encoders, linked with a callee whose masks fill k1 to k7, against a stack machine of its own and, for the programs
without rows or objects, the typed interpreter: masks of every width loaded, converted from every integer class and
from calls' results and back, in frame objects, under `DUP`, `STORE`'s kept value and `?:`, chains past k7 (where the
logic meets masks in their slots, the stack up to 32 deep), calls with masks live and past k7, rows by role (with
scratch registers, from and to integers, past k7) and by number. A zero-masked vector move whose decoration is a role,
and a `vaddps` rounding down beside `MASK(0)` (the decoration 0xa8, FS's override and zeroing in a 32-bit record), run
under no live mask and under seven, against C. At the bound, 239 masks live at once compile alike and run against C
and the interpreter, 240, 256 and 257 are refused alike, and 239 summed by rows of seven mask operands, one block of 64
entries of which ends 182 masks below its start (past an int8's range), compile alike and run against C. And each of
the class's rules is refused, with
its valid neighbours taken: the 16- and 32-bit modes' six segment overrides' bits as decorations, and 64-bit mode's FS
and GS forms, among them, with the encoder's rule that no form a row may have takes bits 3 or 4 in 64-bit mode.

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
The compact stack effects are byte deltas modulo 256, so deep exit depths use the same byte arithmetic as the
depth columns. In particular a call with more than 128 arguments still reloads a live prefix and its result.

For an aggregate argument, its saved value is the address of its bytes. Before populating ABI registers, the deep
call copies whole stack arguments, SPLIT stack parts and odd-size temporary parts through RAX with R10/XMM0 scratch.
Then it reloads each aggregate address and loads its GP/XMM register parts. The indirect callee, hidden result pointer
and variadic vector count are installed after this setup. These scratch registers cannot overwrite a live stack
value, which is already in its slot. Guarded native fixtures run every hand-made aggregate location with both stacks
past their tables, through both encoders, including hidden results and an indirect callee.

A deep function's deep code takes the depth registers below the tables, which its depths reach anyway: no other
register, and never a frame pointer's rbp.

**The memory classes and vectors** (#780). Their values take integer depths but live in slots of their own, which keep
a value at its true depth whatever the deep slots hold: as many depths of them as a deep function's stack reaches
(`Tier0Frame.memory_depths`, `tier0_select_deep`'s), and the table's 15 otherwise. An entry of theirs
(`tier0_memory_uses`) uses a stack's registers where it reads an integer or a floating operand from its register, or
writes a register (the tops `tier0_memory_tops` and `tier0_vector_tops` give at its depths). One that uses none, as long
double's arithmetic and a vector's constant do, runs past the tables where it is, naming its slots by its own depths. A
deep one's window is every operand it has on the stack, those in slots among them: its records run at the window's
depths, which keep their distances to its values' slots, with a frame whose memory and vector areas start the window's
base (its true depth less its window's) slots on (`tier0_emit_deep_deferred`). Its window loads the operands in slots
into registers too, which reads nothing it uses and costs a record each. A deep call copies a long double argument from
its slot at its true depth into its place by `fld` and `fstp`, and stores a long double result from st0 into its
depth's slot; the 128-bit integers and vectors pass as aggregates, their addresses deep values like any other. A deep
entry of theirs whose own registers would still pass the tables at its window's depths is refused
(`TIER0_ERROR_DEPTH`) rather than placed past them.

**Refused** (`TIER0_ERROR_DEPTH`, now its meaning): a stack deeper than `BUSTER_TIER0_MAX_DEPTH`, 240, which the depth
columns' bytes would wrap past (a stack rises by one entry at a time at most, so it shows there first). Before #780, a
function with values of the memory classes or vectors whose stacks passed the tables was refused too: stb_image's
`_mm_set1_epi8`, whose call of `_mm_set_epi8` has 16 arguments and returns a vector, among them.

**Cost.** Deep code is longer: each deep entry's loads and stores, and the registers' stores and loads where an entry
crosses the line. `test-tier0`'s deep shape (below) has about 2% of its entries deep, and its cross-check's code is 1.33
times clang -O0's (1.16 for the larger shape's), with run times about the same.

## Realigned frames (#136)

An object aligned past 16 bytes, or a call whose outgoing area is (`AbiCall.stack_alignment_log2` past 4: a struct or
union aligned past 16 on its stack), needs a frame whose rsp is aligned past the 16 bytes the convention keeps.
`tier0_frame_alignment` finds the most any of them needs, once per function, before selection: every object's (an
aggregate parameter that keeps its incoming bytes rather than living in the frame is aligned to 16 at most, so it
changes nothing) and every call's area. The vector path's twin, `tier0_frame_alignment_vector`, reads the objects'
alignments 16 locals at a time, under the mask of the locals with sizes, and the calls' 4 at a time, each one's byte
of the 16 an `AbiCall` takes. Past 16 bytes, the frame is realigned to it (`Tier0Frame.alignment_log2`, at most 2^30,
the objects' limit):

- **The prologue:** rbp pushed and pointed at the push, as a frame pointer's; the callee-saved pushes; the reservation
  (`sub rsp, size`); then `and rsp, -alignment` (imm8 to 128 bytes, imm32 past it). Rounding after the reservation
  takes at most the alignment less 8 more bytes of stack, where rounding first would take that much again in the
  reservation's own rounding. The frame's places are at their offsets from rsp, each at a multiple of its alignment, so
  the size needs no rounding of its own.
- **The frame's places** (slots, objects, the call save area, the fixed, deep and memory classes' slots, the incoming
  variadic save area) are rsp's, with no bias (`Tier0Frame.base`).
- **With the stack's entries** (`ALLOCA`, `STACK_SAVE`, `STACK_RESTORE` and `FRAME_ADDRESS`: `Tier0Frame.based`), they are r15's: the
  prologue pushes r15 with the callee-saved registers and sets it to rsp after the `and`, and r15 leaves the depth table
  and the homes. r15 is the table's last register but rbp, so the stack keeps the table's first 13 depths, as a frame
  pointer keeps its first 14 (`tier0_integer_depth_count`); clang takes rbx for its base pointer, which in Tier 0's
  table would split the depths. An allocation rounds its bytes up to the frame's alignment rather than 16 (`add r,
  alignment - 1; and r, -alignment`), so that rsp keeps the alignment at every call after it, and its address stays a
  multiple of 16 above the outgoing area. (clang 23's code realigns such a frame the same way, its base rbx, but rounds
  a dynamic allocation to 16 alone, so that a call after one may pass a struct aligned to 32 off its alignment, which
  its callee takes in place: c_check_program_test.c's program of such calls leaves allocations out for that.)
- **The incoming places** (stack parameters' homes and the slots the prologue reads, an aggregate's incoming bytes, the
  variadic overflow area's anchor) are rbp's, past rbp's push and the return address: `[rbp + 16 + stack_offset]`
  (`Tier0Frame.incoming_base` and `incoming`), since nothing knows rsp's distance from them.
- **The epilogue** is a frame pointer's: rsp set back from rbp past the pushes (`lea rsp, [rbp - 8 * pushes]`), the
  pops (r15's among them) and rbp's.
- **The published frame** (`Tier0Code.frame_alignment_log2` and `frame_base`) lets `unwind_tier0_frame` check the `and`
  and the base's `mov` after the reservation. Neither is a transition: from rbp's `mov` on, the CFA is rbp + 16, whatever
  rsp does, so the realignment is part of the reservation, which the CFA doesn't follow ([the unwind guide](unwind.md)).

A parameter aligned past 16 is copied into the realigned frame (`tier0_parameter_in_place`): an aligned typedef passes
as its base, so its slot is aligned only as far as its type's own alignment goes, and a struct's own alignment past 16,
which its caller does align its slot to, is copied the same way rather than trusted. A variadic definition's named
arguments may be aligned past 16 (`incoming_variadic`'s stack alignment, which raises its incoming area's); its
`va_arg` recipes align each argument they read as before.

**At no cost to the rest.** `tier0_frame_alignment` and its twin are out of line and read the objects' alignments (in a
function with objects) and every call's stack alignment, in loops kept rolled (the scalar twin's kept scalar too):
clang's unrolled copies spread the few iterations a function takes over many instruction cache lines, which the
per-entry passes evict between functions. Both selections take the frame's alignment as a constant
(`tier0_select_entries` and `tier0_select_vector_entries`, as the memory classes' `memory`): a function without a
realigned frame or the memory classes has main's loops, and one with either takes the out-of-line copy. The realigned
prologue's records come from a cold helper (`tier0_emit_realignment`). A function without such objects or calls has the
frame, the prologue, the epilogue and the code it had before: `sqlite3.c`'s objects and c-testsuite's 220 programs'
are the same bytes as main's, by both paths. [Measurements](#measurements) has `benchmark-backend`'s.

## Tail calls (#147)

A `TAIL_CALL` entry calls row `value` of the calls table in place of the function's return: clang's `musttail` on a
return statement, whose callee then returns to the function's caller. It pops its operands as `CALL` does, pushes
nothing (`class` VOID) and ends the code as `RETURN` does. The rows it calls are marked in an optional column,
`call_tails` (1 for a tail call's row, 0 for a `CALL`'s, null in a function without tail calls), and `tier0_validate`
checks the two against each other: a `TAIL_CALL` of a row that isn't marked, a `CALL` of one that is, and a `TAIL_CALL`
with a class or with values left under its operands are refused. A marked row passes what the function's own entry
takes, as the front end's check of the signatures makes it (`tier0_tail_call_valid`, whose breaks both compile paths
refuse too, as metadata):

- neither the row nor the function is variadic;
- its result (`AbiReturn`) is the function's: its callee returns where the function returns, in the same registers or
  through the function's own hidden pointer (parameter 0), which the call passes on in rdi, with no result object;
- its stack arguments end within the function's own incoming ones (`tier0_incoming_bytes`: the most any parameter's
  slot reaches), which the function's caller made room for.

The emission (`tier0_emit_tail_call`, out of line, as tail calls are rare) starts with a call's moves, with nothing
under its operands to save: the arguments into their registers and the stack arguments into the outgoing area, every
argument read from wherever it was, the incoming slots among them. Then `tier0_emit_tail` writes:

1. rdi, set to the function's own hidden pointer for a result through one;
2. the stack arguments, copied from the outgoing area to the incoming one (from rbp with a frame pointer, from rsp past
   the frame otherwise) through rax and xmm15, which no argument takes (neither side is variadic, so al passes
   nothing);
3. the frame released and the callee-saved registers popped, as the epilogue does (`tier0_emit_teardown`), rbp last
   with a frame pointer;
4. the jump: through r11 for an indirect callee, or a `jmp rel32` with a call's fixup.

A function whose tail calls pass a hidden pointer on keeps that pointer's home out of the registers the moves write
(`BUSTER_TIER0_TAIL_CLOBBERS`). Otherwise a tail call touches nothing a call doesn't, and it makes the function no
caller: the frame keeps no alignment for it. The epilogue still follows the code's last entry, as unreachable after a
tail call as after a `RETURN`.

`Tier0Code.tail_records` gives each tail call's jump, in the entries' order. The unwind tables describe each teardown
and its jump (`unwind_tier0_frame_tails`, [the unwind guide](unwind.md)'s "Tail calls"). The validator's vector path
leaves a function with tail calls to the scalar one. The shared IR's lowering refuses a `TAIL_CALL` by name, as it
refuses `FRAME_ADDRESS`, and the typed stream's interpreter has no model of one.

`test-tier0`'s "tail errors" break each of these rules once. Its "tail calls" (`tier0_tail_test.c`) compiles a
tail-calling function per aggregate fixture ("Structs and unions by value") in five frames: none, a frame pointer that
an `ALLOCA` moves rsp under, saved registers across a call, a realigned frame, and a realigned frame with a base. Each
is compiled by both paths, encoded by both encoders and run from a driver, and the probe it jumps to must find:

- each argument where its location says;
- the driver's slots past the arguments as the driver stored them;
- rsp where the driver's call left it, and the callee-saved registers as the driver had them, which only a jump that
  released the whole frame leaves.

What the probe returns must reach the driver, through the hidden pointer too.

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
   - with an object aligned to 16, the frame keeps rsp a multiple of 16, as a function that calls does; with one aligned
     past 16, the frame is realigned ("Realigned frames", below).

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
- `STACK_SAVE` and `STACK_RESTORE` are a `mov` from rsp and to it, and `FRAME_ADDRESS` a `mov` from rbp. A function
  whose only stack entry is `FRAME_ADDRESS` takes the same frame as one whose rsp moves: rare enough that a frame of
  its own (rbp's, its places rsp's) wouldn't pay for its code;
- the epilogue sets rsp back from rbp (`lea rsp, [rbp - 8 * pushes]`, or `mov rsp, rbp`), past whatever the body
  allocated, before its pops and `pop rbp`.

Structs and unions by value (below) need nothing more: the prologue reads their incoming slots from rsp before anything
is allocated, their homes and a call's result objects are addressed from rbp as the other homes are, a call copies them
into the outgoing area at rsp and its red-zone temporaries are below rsp, and the epilogue loads a result's registers
before it releases the frame. A function without them keeps the rsp-relative frame, record for record.

**Realigned frames.** A function whose objects or calls' outgoing areas need more than 16 bytes' alignment has a frame
pointer too, and rounds rsp down to that alignment after the reservation ("Realigned frames", below).

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
  and `dec`. The records the emitter writes with a constant it computes (the frame's reservation and release, its
  realignment, `ALLOCA`'s rounding, a vector constant's words) take their imm8 or imm32 by `x86_immediate_encodes`, the
  same rule (#221). Every form the emitter writes has `x86_semantics.c`'s effects, which `test-tier0` checks on every
  record it compiles but the X86 entries'.

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
   kinds in the four kinds' bitset), then a count per set bit, without a branch per entry. The greedy loop gives the k-th local of
   a kind that takes a register (not in one yet, its address not taken, referenced) the k-th free register of its
   kind: the caller-saved registers in depth order, then the callee-saved ones to a local referenced twice or more
   (those come first in the order), or the XMM registers. So for up to 64 locals the vector path needs no order:
   each such local's rank among its kind's, counted by vector compares 16 locals at a time, picks its register from
   the free registers' list (`vpermd`), and its home is a masked store. Past 64 locals, and for the scalar twin, only
   each kind's first 16 eligible locals can take a register. `tier0_register_locals_vector` filters 16 locals at a
   time by eligibility and the current smallest retained count, then inserts a candidate into 16 register lanes by
   comparisons and permutations. Equal counts keep the earlier local, as the old stable full sort did. Its scalar
   twin maintains the same bounded prefixes with at most 16 moves per insertion. Both take linear time in the locals,
   including increasing counts and builtin recipes' alternating counts (#664), and hand the unchanged greedy register
   assignment at most 32 identities in fixed scratch storage. No local-sized order array is allocated.
   `test-tier0` checks both prefixes and the complete homes/save masks against the full stable order and original
   greedy policy, with guarded columns through 100,000 locals, arbitrary unsigned counts, ties and unavailable
   registers. Small cases also compare that order with the former insertion sort. The frame's slots go to a mask of the locals
   that need one, from the homes, the references and the flags, 16 at a time, and the prologue's records are counted
   rather than emitted twice.
   [The frozen #664 qualification](https://github.com/buster14a/buster-performance/tree/4df43f7d7a685133b6beade655d9e9a306947081/docs/measurements/unclaimed-perf-2026-10-28/issue-664/target/README.md)
   compares `bc1b4861` with `be441ff8`: doubling the generated popcount input from 4k to 8k calls grows Tier 0 time
   3.74 times before and 1.99 times after the change, with byte-identical objects and independent runtime checks.
   Ordinary SQLite and vector-backend controls are near-neutral; the scalar twin's measured cost is retained in
   the record. These timings apply to those frozen revisions, rather than later rebased source.
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
looks each entry's slot up, homes select the bounded register candidates, measuring counts a deferred entry's records by running its emitter,
and emission builds each record field by field from the templates' specifications, not from their compiled bytes. It
chooses with `x86_choose_forms_scalar` and applies each choice with `x86_choice_instruction`.

## Validation (#569)

`tier0_validate` checks the stream's rules, the compile paths' precondition: the kinds, classes and values, the
operands and depths, the targets and the stacks there, the locals and the calls. The lowering runs it on every function
it lowers (`check_lower_part`), so that a producer's bug is an internal compiler error rather than wrong code, as
#440's was. It has two paths, which give the same verdict and the same rule on every stream.

**The scalar validator** (`tier0_validate_scalar`, the twin and the oracle) runs the stream as a stack machine over one
stack of classes, an entry at a time. At each target it records a hash of the stack when control first arrives and
compares it on every other arrival. It reports the first entry that breaks a rule, and the rule. `--scalar` runs it
alone.

The selected validator uses the scalar path for nonempty streams of at most 16 entries when the local, call and
argument tables each have at most 16 entries too. Locked 9700X size controls put the crossover between 16 and 24
entries, both alone and after 200 ms of scalar work. Empty streams keep the vector setup path; larger tables
keep the vector path so a small entry count does not hide substantial metadata work. The vector acceptor remains
tested independently at these tiny sizes.

**The vector path** (`tier0_validate_accepts`) takes a stream only where the scalar validator does. It leaves every
other stream to the scalar validator, which then gives the verdict and the rule. It reads 64 entries at a time in byte
lanes, as Tier 0's own passes do:

1. **Depths.** Each entry's pops and pushes come from tables by kind; a call's operands come from a per-function table
   of its calls, by `vpermi2b`, or by a gather past 128 calls. The depths are their prefix sums, and an entry's floor is
   its depth after its pops.
2. **Producers.** An operand's producer is the nearest entry before it whose floor is at or below the operand's place.
   The parser's doubling search over the floors finds it for the top and for the one below (`tier0_scan_previous`). An
   operand from an earlier block comes from the carried stack: the classes of the values earlier blocks left, a
   register of 64 places, and their producers.
3. **Rules.** Each entry's class, its top's and the one below's are checked against 16-bit masks of classes by kind,
   or against the entry's own class, a compare's top, or CONVERT's mask by class. The top's class must be the entry
   before's (`tier0_last_mismatch`'s rule), and the values must be within bounds by kind. A local's classes come from a
   per-function table of masks, and every RETURN must be of one class. A call's operands are its top and the one below,
   or for more than two, the stack's places: the carried stack below the lowest place the block reached before the
   call, and the values the block left above it, in order.
4. **Arrivals.** An arrival that brings only depth 0 (a statement's) sets its target's bit, which the target's block
   checks; a backward one checks the target's stored depth at once. Any other arrival brings its depth, the producer
   just below what it may differ in, and the class of a JUMP_VALUE's value. It is checked against the target's own
   where the same block holds the target. Otherwise it is checked against the first forward arrival's, and the target's
   block checks that one against its own. Code no path reaches, after a jump or a return and before a target, is at
   depth 0.
5. **Tables.** A plain function's locals are checked 64 at a time and each call's arguments eight at a time
   (`tier0_scan_tables`). A function with aggregates' metadata, an incoming variadic area, or vectors' constants and
   operations takes the scalar helpers instead.

The vector path leaves these to the scalar validator:

- a function with vectors;
- a block whose pops come to more than 100 (`BUSTER_TIER0_SCAN_POPS`), so that relative depths stay within a byte;
- a depth past 64 at a block's start (`BUSTER_TIER0_SCAN_DEPTH`), the carried stack's register;
- a call with more than 64 total operands: a 64-argument indirect call also has its callee, which needs a 65th lane;
- a backward arrival with values on the stack;
- a target whose arrivals bring values of two producers, as `x ?: y`'s do, which the scalar validator compares by
  class;
- every stream that breaks a rule.

**Why the verdicts agree.** The vector path's stack is the one the prefix sums give each entry. The scalar validator's
stack is the same at every entry, given what the arrivals' checks establish:

- every arrival at a target brings the target's stack, and code no path reaches starts at depth 0;
- a forward arrival matches the target in depth, in the producer just below what it may differ in (so every value
  below it is the same value), and in the class of the value a JUMP_VALUE brings;
- a backward arrival has nothing on the stack.

Every rule the vector path checks on its stack is then the scalar validator's rule on the same stack, and the hashes
the scalar validator compares are of equal stacks. The only difference left is resources: the vector path needs no
copies of the stacks, so it takes a valid stream where the scalar validator would run out of scratch memory.

**The memory contract.** It reads the columns with masked loads, never past `count`. It loads no 16-entry quarter of a
table wholly past the table's end: a masked load reaching untouched pages takes a slow path even though it reads none
of them. That cost sqlite3.c's validation 7 cycles per entry until the tables' builders stopped doing it. Its scratch
is about 10 bytes per entry, and 4 per local and per call past 128.
Before any scalar producer lookup or class-dependent shift, the block must pass its kind, class, bounds and depth
checks; a malformed byte depth must not reach a producer index. Arrival mismatches likewise stop dependent checks.
The regression suite ends kinds, classes and values independently at separate inaccessible pages and combines
underflow or invalid classes with later calls and arrivals. Direct/indirect 63/64-argument calls cross a block boundary,
with both matching and mismatched final arguments, and the selected wrapper's exact diagnostic matches the scalar oracle.

**Measurements.** On the 9700X, pinned to CPU 2, on the frozen `sqlite3.c` (sha256 `0a409f16…`) that `-c` lowers
into 2,602 functions of 396,893 entries, every one of which the vector path takes alone:

| | ns/entry | cycles/entry | instructions/entry | IPC |
| --- | ---: | ---: | ---: | ---: |
| `tier0_validate` (the vector path) | 1.96 to 1.98 | 10.86 to 10.96 | 29.51 | 2.69 to 2.72 |
| `tier0_validate_scalar` | 8.35 | 46.30 | 129.52 | 2.80 |

These are `benchmark-backend-source --only validate` and `--only validate-scalar`, the CI's options, three processes.
`-c` of the same `sqlite3.c` with #136's speedtest1 options, `measure-compile --runs 21` of each build against the
other: the `lower` stage went from 16.18 ms (main, 8ab9a208) to 13.03 ms (this change, e92fbf1d), and the whole compile
from 148.0 ms to 144.3 ms, with the object's bytes unchanged. On the laptop's Zen 4, a function of 16 entries or fewer
costs the vector path about 600 TSC ticks, nearly a whole block's code for its few entries, more than the scalar
validator takes; past 32 entries the vector path is faster, and 3 times as fast past 128. Of the vector path's samples
there, 28% are the function's tables and setup and 69% the blocks, of which the calls with more than two operands take
15%.

The completed one-binary vector/scalar/disabled campaign isolates validation from executable layout: the `lower`
medians are 13.020, 15.662 and 12.146 ms, respectively. Vector validation therefore retains about 0.874 ms on this
input, compared with the scalar check's 3.516 ms. Eight rotated `perf stat -r 3` rounds average 1,517.49 M user
instructions for vector, 1,558.32 M for scalar and 1,505.60 M with checking disabled. The vector/disabled cycle ranges
overlap; disabling validation is solely a measurement control, never a production option. Sharing a validated plan
with Tier 0's analysis would require preserving refusal before unchecked backend consumers and a separate measured
producer/consumer design. This PR claims the measured reduction while retaining that correctness gate.

Fresh target-host size controls use three normal processes and a scalar-conditioned process. At 16 entries,
table-free scalar/vector validation takes about 60.0/70.1 ns, and a fixture with eight locals, six calls and four
argument records takes 100.6/116.9 ns; at 24 entries vector wins in both fixtures. Empty table-free streams favor
vector, 11.9 ns against scalar's 14.6 ns. The 16-entry dispatch threshold follows these controls, with the metadata
cap as a conservative guard. These synthetic controls do not establish a universal threshold or whole-corpus gain.
The [retained campaign and review evidence](https://github.com/buster14a/buster-performance/pull/57) includes all
samples, source and binary identities, exact commands, native/sanitizer qualification and defect-detecting mutations.

## Tests

`test-tier0` (about 0.8 s, or 2.9 s with `--cross-check`, on the 9700X, before the records' shape) generates programs of
seven shapes: one function of integers, integers and calls, everything, larger, larger with structs and unions by value
(#137), larger past the depth tables, and that with objects aligned past 16 bytes, 300 each by default. Each program:

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
    parameter, aligned past 2^30, sizes without alignments, a `LOCAL` of it, past `BUSTER_TIER0_MAX_OBJECT_BYTES`, and
    an object aligned to 2^30 a byte past it (its size rounded up to 16 and its alignment less 16 count). It must take
    exactly the limit, both ways.
  - 2,000 random frames, each of up to 24 locals: objects of 1 to 300 bytes at every alignment to 16, 8-byte values,
    some address-taken, and `VA_LIST`s of 24 bytes; in every other round a quarter of the objects aligned past 16 instead
    (to 32, 64, 128 or 4096, from a sequence of their own, so that the other rounds' frames are the ones they were).
    Both paths compile them, and must agree. Each local's place, from its `LOCAL_ADDRESS`'s `lea`, must be a multiple of
    its alignment (8 but for objects), inside the frame and apart from every other local's bytes. Under an object
    aligned to 16, rsp must be a multiple of 16; with one aligned past it, the frame must be realigned: rbp's push and
    mov, the reservation and `and rsp` by the most any object needs, which the frame publishes.
- **frame alignment:** `tier0_frame_alignment` and its vector twin against the test's own most, for every count of
  locals to 40 and of calls to 12: random columns, each ending at a `PROT_NONE` page (a read past one faults), with
  sizes of 0 among the locals (whatever their alignment's byte), alignments past 16 now and then (any byte, to 255),
  and calls whose every other byte is random too; then the same without `local_sizes`, the calls' alone.
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
- **deep** (`tier0_deep_test.c`, #136's item 6): eleven functions of one U64 parameter whose stacks fill their tables,
  then cross the line with an entry of each kind the generated programs reach rarely or never: a value pushed past it
  and dropped back to it (a `DISCARD`, which uses no register), `?:` whose condition, branch and arms' values cross it,
  a compare fused into its branch that drops two values across it, 17 floating values (through U64 conversions, one of
  them dropped across the floating line), division by a local (whose dividend only rax reads) and by a value, `%`, and
  shifts by a register past it, a frame pointer's 14 values and two past them, both stacks crossing at one conversion,
  rsp moved past a frame pointer's line (#434's review): rsp saved, two allocations stored through and loaded back,
  rsp restored, every entry a deep one while the values below are in their slots, which rbp addresses; and a conversion
  to U64 deep on the floating stack alone at integer depth 13, whose register form's scratch would be rbp, which
  nothing else in it reaches; a realigned frame's 14 (an object aligned to 64) and two past them, among them the
  object's address modulo 64 and its first word; and a realigned frame with a base, r15, the table's last register but
  rbp (an object aligned to 4096 and rsp saved): 13 values, then an allocation past them, rounded up to the frame's
  4096, rsp modulo 4096 after it and the object's and the allocation's addresses' low bits (all 0), their words, and
  rsp restored. Each must compile by both paths into the same records with deep entries, a frame pointer exactly in the
  cases that have one, the realigned frames published as such, write no callee-saved register that its prologue
  doesn't push (by
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
- **aggregates** (#137), at locations written by hand: 14 fixtures of what `abi_assign` makes rarely or never on its
  own and of the parts' edges (each kind of general and XMM part, both kinds in one aggregate, the high eightbyte alone
  in each kind of register and on the stack, the low eightbyte alone of a larger aggregate on the stack (`LOW`) before
  a value in the next slot, SPLIT both ways in both kinds, an ignored aggregate, aggregates on the stack
  whole, in place and in a slot misaligned for them, and results in rax and rdx, xmm0 and xmm1, both mixed orders, the
  high eightbyte alone in each kind, a vector, 5 and 6 bytes, and through a hidden pointer), each run natively both
  ways:
  - **aggregate calls:** Tier 0's caller, by both paths, passes the aggregates' bytes from pages that end or start at a
    `PROT_NONE` page to a probe, records of the test's own, which records every argument register and its stack slots:
    each value and part must be where its location says, and a result the probe returns must land in its object;
  - **aggregate functions:** Tier 0's function, by both paths, whose parameters a driver passes from the same bytes,
    copies each parameter's bytes out, each aggregate's address after them (a multiple of its alignment), and returns
    the result in its registers or through the hidden pointer, which must come back in rax;
  - **aggregate errors:** 61 breaks of the fixtures' metadata, each refused by `tier0_validate` with its rule, or as
    malformed metadata by both compile paths too: among them a function with a result size that reaches its end by
    falling off it, by a jump and by a branch, a jump that takes a value to the end, a function with a hidden result
    that returns nothing or falls off its end, an aggregate's two parts in one register, a register two parameters
    take, a part, a value or a result in a register outside the convention's (rax, xmm8, rbx), as an argument and
    as a parameter, and a low eightbyte alone of 8 bytes, with `HIGH`, in registers and past the call's area.
- **tail calls** (#147, `tier0_tail_test.c`): a tail-calling function per aggregate fixture in five frames, run from a
  driver, whose probe must find the arguments, the stack and the callee-saved registers as "Tail calls" (above) says,
  the saved registers' frame first calling a function that clobbers every register a call may; and **tail errors**,
  each of the tail calls' rules broken once, the variadic function's metadata valid without its tail call.
- **parameter errors:** a function of values alone, which no optional part makes the compile paths check: two
  parameters in rdi, one in rbx and one in xmm8, which `tier0_validate` must refuse, and an F64 in rdx, which doesn't
  fit its class.
- **stack:**
  - `tier0_validate` must refuse, at the entry, an allocation of another class, with a value or of a U32, a save of
    another class or with a value, a restore of another class or with a value, and a frame's address of another class
    or with a value, and take a save restored.
  - 14 and 15 integer values at once must compile with a frame pointer and without, by both paths alike: with one,
    the 15th goes to its deep slot (rbp, the table's last depth, is the frame's).
  - a function of the test's own (`tier0_test_stack_entry`) allocates 64 KB and up 1,000 times in a loop, each in a
    scope of its own: the position saved, the allocation's first and last words written, its address modulo 16 added
    in, a call with stack arguments made (whose outgoing area mustn't overlap the allocation), the words read back and
    the position restored. Without its restores it would take 64 MB of stack. Both paths must compile it alike, with
    `push rbp` in its prologue, and it must run in a child on eight inputs with the interpreter's result and the
    expected one.
- **frame address** (`tier0_test_frame_address`): a function of `FRAME_ADDRESS` alone must compile by both paths
  alike, with a frame pointer and a `mov` from rbp; then a callee that returns the word its frame address points at
  (the rbp its push saved) and an entry that compares it with its own frame address, takes its frame address again
  after an allocation (which moves rsp, not rbp) and checks the allocation below it, both functions with frame
  pointers, run in a child on eight inputs against the expected results (the interpreter has no frame to model).
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
    2^63 too), calls of C functions through their addresses and frame objects (four, of 16 to 32 bytes aligned to 8 or
    16, given values first, read by `LOCAL`, written by `STORE_LOCAL` and `SET_LOCAL` and copied out at the end), against
    the check's own stack machine over the stream. A program past the depth tables compiles through the deep slots
    (#780); one that both paths refuse (`TIER0_ERROR_DEPTH`) would be counted, not compiled. The programs
    compiled, run and checked must execute every kind and conversion of the class and both arms of a `?:`. Separate
    bitmaps record kinds and conversions, and the interpreter marks only entries and arms it runs. Any refusal except
    a random program's excess depth fails the check with its error and entry.
- **int128** (`tier0_int128_test.c`), the 128-bit integers compiled by both paths into the same records, encoded by both
  encoders into the same bytes and run natively in a child, called from C through function pointers, so that the file's
  C (clang's code of `__int128`) is the oracle of their values and of the convention for them:
  - **rules:** each break of the classes' rules (above) refused at its entry by `tier0_validate`;
  - **slots:** for the 128-bit integers and for long double, stacks of a memory class's values to the integer table's
    last slot, one and two past it and 40, each of which both paths compile to the same code and runs (past the table
    through the deep slots, #780); of 238, which both compile; and of 239, whose stack with the output's address below
    it reaches `BUSTER_TIER0_MAX_DEPTH`, which both refuse;
  - **boundaries:** for each class, its six compares, `SHR` by 0, 1, 63, 64, 65 and 127, and `SHR` and `SHL` by variable
    counts, on every pair of ten boundary values (0, 1, -1, the extremes, 2^63, 2^64 and their neighbours), against the
    check's stack machine and against C's own operators;
  - **functions** of 128-bit parameters and results, in register pairs and on the stack (where no pair is left), called
    from C on boundary values, and functions that call C functions of them;
  - **programs:** 300 random functions (`--count`) of statements that each write a value into 16 bytes of an output (a
    128-bit integer of either class, an integer of a class I8 to U64 converted from one, a compare's result), through
    constants (low halves past a sign-extended imm32 among them), arithmetic, shifts by constant and variable counts,
    `?:`, `DUP`, `STORE`'s kept value, conversions both ways and frame objects (as the x87 check's), under live values at
    the depths where `MUL` saves rax and rdx and a shift rcx, against the check's own stack machine over the stream. A
    program counts toward the coverage only once both paths compiled it, it ran and every output matched, and then only
    the entries the stack machine ran: those must reach, for each class apart (their signed and unsigned code differs),
    every kind, every conversion to and from it, each of the saves and both arms of a `?:`, in bitmaps of their own.

  Every function must compile: an error of both paths alike fails the check, but a depth refusal in a random program
  (`TIER0_ERROR_DEPTH`, past the deep slots since #780), which is counted.
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
  - local arrays of 2 to 16 elements and structs of 2 to 5 members laid out as C lays them out, some `_Alignas(16)`,
    and with the `over_aligned` option (the seventh shape) half of those `_Alignas(32)`, `_Alignas(64)` or
    `_Alignas(4096)` instead, from a draw a program without the option doesn't make, whose functions' frames are
    realigned (a copy between them takes `BUSTER_TIER0_MAX_COPY_BYTES` at most);
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
- with the `deep` option (the sixth and seventh shapes), values past the depth tables: one function in four has 13 to
  32 parameters, whose callers evaluate each argument over the ones before it on the stacks, and expressions have room
  for 40 values at once. With the default count, at least one entry in a hundred must be deep (about 2% are). A program
  without the option is the one the same seed gave before it;
- with the `records` option (the fifth shape, #137), structs and unions passed and returned by value, from a catalog
  that `tier0_tables_open` builds in the type table and classifies: byte arrays of 1 to 17 bytes, whose parts load in
  every way and the largest of which goes on the stack; floats and doubles, mixed with integers both ways round; unions;
  a member aligned to 16 (one part of 16 bytes); the empty struct; records aligned to 32 (on a stack aligned to 32 at the
  call, which realigns the caller's frame, and copied into the callee's realigned frame); and records past 16 bytes, on
  the stack and returned
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
larger shape's are, and of the shape aligned past 16 bytes, whose objects clang's frames realign as Tier 0's do.

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
- **Realigned frames (#136)** at about one instruction per entry to code without them. On these programs, which have no
  object aligned past 16, the 9700X measured the branch with main merged in against main twice, six rounds each in
  alternating order: the vector path at 170.86 instructions per entry against 169.85 both times, its cycles 80.63
  against 79.83 against main's `51bf882` (1.0% more, and 1.2% alone) and 80.49 against 80.62 against `bbe59b7` (within
  main's spread), and the scalar path at 573.75 instructions against 570.95 (1.5% and 1.0% more cycles). The
  instruction is each function's: the frame alignment's scan before selection, the frame's added fields, and the
  prologue's and the layout's checks of the realignment. Against an earlier main (`b7b1f7a`), the branch's earlier head
  ran the vector path 1.15% and the scalar one 3.8% slower: the benchmark's unit (`tier0_benchmark.c`, linked after
  `main.c`'s) sat 14,928 bytes further on, and main moved by as much (`.skip` before the unit's code, no code changed)
  ran its vector path 0.8% slower. `-c sqlite3.c`'s objects are main's and its time within main's spread, at 0.09% more
  instructions, most of them clang's code for the lowering's `check_lower_part`, which moves with any edit of its body
  ([the record](https://github.com/buster14a/buster-performance/tree/12adf9e72419f5352e10088edef316bdfaa0624d/docs/measurements/aligned-objects-136-2026-10-27)).
- **Against #115's budget:** the budget is about 32 cycles per output token for selection, allocation and encoding.
  On the generated programs the vector path's cycles per entry are about 2.3 times that. On `sqlite3.c` (below) the
  three stages take 41 to 42 cycles per output token, 1.3 times the budget: it is not met.
- **Where the time goes** after the second round (perf on the 7940HS, each pass kept out of line), as shares of the
  vector path's time:
  - emission 21%, fusion and depths (P0 to P4) 16%, selection 13% (the slot gather the largest part), measuring 10%;
  - the call emitter and its count 14% together, for the 3.6% of entries that are deferred;
  - the immediates' choice (`x86_choose_forms`) and applying it 11%;
  - the homes, the frame, the prologue and the per-function setup 15%.

**On `sqlite3.c`** (`benchmark-backend-source`, [record](https://github.com/buster14a/buster-performance/tree/9a5bad45a0e39305ee4f426f98b171a105d0f659/docs/measurements/backend-source-136-2026-10-26)):
the functions `-c` lowers of the frozen SQLite 3.53.2 amalgamation, with the 9700X job's options. Until #1041 the
benchmark compiled every file for the baseline x86-64 profile, whatever `-march` said, so these numbers, and the
job's before #1041's merge, are of baseline code, not of the `-march=znver5` code the job's options ask for; from
that merge the job times znver5's, and its numbers are not comparable across it. That is 2,602
functions, 400,995 typed entries and 366,044 records (0.91 per entry, against the generated programs' 1.15), with
3.49% of the entries deferred. The 9700X, CPU 2, medians of 21 passes, the range over three processes:

| Variant | ns per entry | Cycles per entry | Instructions per entry | Cycles per record | IPC |
| --- | --- | --- | --- | --- | --- |
| Vector path | 11.4–11.7 | 63.1–64.7 | 152.7 | 69.1–70.9 | 2.36–2.42 |
| Scalar path | 30.8–30.9 | 170.9–171.3 | 526.7 | 187.2–187.7 | 3.07–3.08 |
| `size_branches` | 0.94–0.95 | 5.2 | 11.1 | 5.7 | 2.1 |
| `x86_encode` | 0.70–0.72 | 3.9–4.0 | 14.2 | 4.3–4.4 | 3.6 |

- The vector path takes 62.6 cycles per entry alone, and 63.5 after 200 ms of the scalar path.
- Real code costs less per entry than the generator's (79.6 cycles there): it makes fewer records per entry.
- The three stages take about 5.3 ms for the file, 41 to 42 cycles for each of its 706,237 output tokens.
- **The immediates' choice** (#221, `--only choose`, from the requests `Tier0Function.choice_requests` copies; the
  9700X at `cff7bc98`, three processes, [record](https://github.com/buster14a/buster-performance/tree/a74e2a6ee49e31ea9f948f597ecefc9c02f3e835/docs/measurements/x86-semantics-221-2026-10-28)): 71,198 requests, 0.179 per entry, at 1.74 to 1.88 cycles per entry (10 per
  request, a function's 27 at a time) and 3.32 instructions, 1.50 after 200 ms of its scalar twin, which takes 7.70.
  3.3% of the requests kept their template's form, 44.0% took another encoding of the instruction and 52.7% another
  instruction; their records' 450,190 bytes became 234,542, 15% of the code the functions would make with each
  template's widest form. The vector path's 152.82 instructions per entry against main's 152.43 are the frame's
  immediates through `x86_immediate_encodes` (the shared rule's calls in each prologue and epilogue).

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

**SQLite's speedtest1 against clang -O0 and tcc**
([record](https://github.com/buster14a/buster-performance/tree/c1bbfb2e56f58deb4972ff16d50221d7cd82f0fd/docs/measurements/speedtest1-tables-136-2026-10-27);
[before the jump tables](https://github.com/buster14a/buster-performance/tree/35da10607a8769545da88379cecc60891c153829/docs/measurements/speedtest1-136-2026-10-26)).
The inputs are SQLite 3.53.2's `test/speedtest1.c` and the frozen `sqlite3.c`. They are built with SQLite's own
`test/speedtest.tcl` options:

- by `-c`, linked by cc, on main at `ad408cf2`, where `switch` dispatches through jump tables (#536), and at its first
  parent `b7f444eb`, before them;
- by each tree's direct path, both files as one unit;
- by clang -O0, linked by cc;
- by tcc.

Each build runs with that script's options at `--size 5`, from a database on tmpfs. On the 9700X, CPU 2, every build
gives the same verification hash. The run and compile times are medians of 11 rounds in a rotating order, and the
instructions and instructions per cycle medians of 3 rounds under `perf stat`:

| | Tier 0, `-c` | Tier 0, `-c`, before the tables | Tier 0, direct path | clang -O0 | tcc |
| --- | --- | --- | --- | --- | --- |
| Run time | 256.7 ms | 295.0 ms | 255.4 ms | 295.9 ms | 271.6 ms |
| User-space instructions | 5,686 M | 6,866 M | 5,686 M | 3,922 M | 4,833 M |
| Instructions per cycle | 4.31 | 4.49 | 4.32 | 2.57 | 3.45 |
| `.text` of the executable | 1,319,528 B | 1,331,336 B | 1,319,408 B | 1,024,342 B | 1,295,452 B |
| `sqlite3.c` compiled | 191.4 ms | 191.9 ms | | 577.4 ms | 42.2 ms |

- **Run time:** Tier 0's code takes 13.3% less time than clang -O0's, in 1.45 times its instructions, and 5.5% less
  than tcc's. Before the jump tables it ran at clang's speed, in 1.75 times its instructions: the tables cut its time by
  13.0%, its instructions by 17.2% and its cycles by 13.8%.
- **Code size:**
  - `.text` is 1.29 times clang's, in `sqlite3.o` and in the executable; the tables took 11,808 bytes out of
    `sqlite3.o`'s and added 11,232 to its `.rodata`, 2,808 entries;
  - `sqlite3.o`'s `.eh_frame` is 2.46 times clang's: `unwind.c` advances with `DW_CFA_advance_loc4` throughout, for FDE
    sizes known before writing.
- **`switch`** dispatches through a jump table where clang -O0's does (`docs/c-checker.md`, "Switches"):
  - `sqlite3VdbeExec`'s dispatch on the opcode is a bounds check and a table, as clang's is, where before the tables it
    was 184 compare-and-branch pairs;
  - it and the parser's `yy_reduce` account for 938 M of the 1,180 M instructions the tables saved (the record's
    profiles): 767 M and 100 M now, against clang's 628 M and 47 M.

## Follow-ups

- **Deep values' cost:** deep code that hovers around the tables' edge stores and loads every register at each
  crossing: a stack of 15 integer values that takes and drops one a few times pays about 30 records each time, where
  storing and loading the one register the entry needs would do. A deep entry of the memory classes or vectors also
  loads the deep slots of its operands in their own slots, which it doesn't read.
- **Uncovered entries:** these are refused (`TIER0_ERROR_UNSUPPORTED`) or not yet in the stream:
  - bit-fields need no entry of their own: the checker's lowering reads and writes the bytes that hold a field's bits
    as pieces of 8, 4, 2 and 1 bytes, with shifts and masks (`docs/c-checker.md`, "Bit-fields"). It refuses a field
    whose bits are spread over more than 8 bytes (a packed one), with `CHECK_PROGRAM_UNSUPPORTED`;
  - complex values need no entry of their own: the checker's lowering takes them as aggregates of their two parts and
    computes each part, libgcc's functions their products and quotients (`docs/c-checker.md`, "Complex values");
  - inline assembly, `setjmp` and atomics;
  - `switch` needs no entry of its own: the checker's lowering dispatches it by compares or, where clang -O0 would,
    through a jump table and `JUMP_INDIRECT` to its cases' labels (`docs/c-checker.md`, "Switches").
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
- **Real corpora:** `sqlite3.c`'s functions are measured, both paths alike on each (`benchmark-backend-source`), and
  SQLite's shell and `speedtest1` compile and run against clang -O0's and tcc's builds ("Measurements"); c-testsuite
  (#145) runs. Still to do: the other corpora's functions, contract 6 measured on them, and #144's benchmark rows
  for real functions.
- **Unwind tables (#88):** done outside this file. #345's `unwind_tier0.c` derives each function's CFI from its sized
  records, the fixed prologue and epilogue (`docs/unwind.md`). It refuses a function with a frame pointer, whose body
  writes rsp (an allocation, or the epilogue's `mov rsp, rbp`): those functions' CFI would follow rbp. It refused every
  function with long double code too, whose x87 forms `x86_semantics.c` had no rows for until #221 covered them.

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
| Published frame + branch sizing's offsets → unwind tables | Implemented for the C driver (#395): `unwind_tier0_frame` takes each transition's end from the frame's records, and `check_program_emit` writes the functions' FDEs with one CIE. |

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
