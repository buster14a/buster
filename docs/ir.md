# Per-function IR

`ir.c` implements the representation, verifier, compact derived arrays and stable
printer of [#131](https://github.com/buster14a/buster/issues/131). `ir_lower.c`
adapts the existing `Tier0Function` typed stream to an all-in-memory form.
The implementation is original project code; no external implementation or
test corpus is imported.

## Status and consumers

The producers are `ir_lower_typed` / `ir_lower_typed_scalar` and their explicit
output-target variants `ir_lower_typed_target` / `ir_lower_typed_target_scalar`,
which take the C driver's typed streams (`check_program_emit`'s
`Tier0Function`s, #133), the
handwritten fixtures, `ir_parse.c` (#175) and the synthetic benchmarks. The
consumers are the two verifiers, the CFG and use-list builders, the printer,
`ssa_ir.c`'s promotion (#134), the optimizer passes (#139), the interpreters
(#176) and Tier 1's selection (#138).

The IR lowered from every function of the corpora verifies (#131): `test-ir-source`
lowers every function `-c` compiles of a set of C files by both paths, which must
agree column for column and verify, and promotes each by both of `ssa_ir.c`'s
paths, whose results verify with dominance checked ("Tests and measurements"
below). Every function of the frozen `sqlite3.c` (2,602 of them), of c-testsuite's
subset (430, in its 220 programs), of Lua 5.4.8's 32 files but `lua.c` and
`luac.c` (1,052) and of the 1,049 of musl 1.2.5's 1,355 units that `-c` compiles
(1,914; it refuses the others' features by name, `alias` and `weak` attributes
and explicit register variables as `asm` operands among them) passes.
`benchmark-ir-source` measures the IR's bytes per source token on `sqlite3.c`
("Bytes per source token" below).

The explicit-target adapter admits #732's bounded 128/256/512-bit `VECTOR`
transports and #717's exact i32 arithmetic/bitwise/comparison `X86` rows. The
[SIMD contract](ir-simd.md) specifies widths, mask expansion, required output
features and transactional refusal of other forms. Legacy entry points grant
no target features. #147's tail calls (`TAIL_CALL`) refuse with
`IR_ERROR_OPCODE`.
The stable printer's text is read back by `ir_parse.c` (#175).

`test-ir-source --simd -march=x86-64-v4 tests/ir/vector-i32.c tests/ir/mask-builtins.c tests/ir/bmi-builtins.c tests/ir/vector-rows.c` checks actual C
producers, both lowering paths and both text parsers. It explicitly requires
promotion of a function with vectors (#141) and of one whose only SIMD values
are masks (#179), as ordinary corpus mode requires promotion of every
function. The shared
interpreter executes vector/mask operations with sparse wide payloads while
retaining the 16-byte scalar value API. First-class vector entry values and
vector/mask calls remain unsupported. Native selection of vectors is
[#141](https://github.com/buster14a/buster/issues/141)'s; Tier 1 selects masks'
operations and allocates them in the opmask class
([#179](https://github.com/buster14a/buster/issues/179)); see the
[backend handoff](tier1.md#vector-and-opmask-handoff).

## Lifetime and stage contract

`ir_create(Arena *, const IrSizes *, IrFunction *)` allocates exact-sized columns
in one caller-owned function arena. `IrSizes` accepts 64-bit counts so limits are
diagnosed before narrowing. Each logical count must fit u32. An instruction ID is
its zero-based index; `UINT32_MAX` is reserved for a missing operand and is never
a valid instruction ID. Overflow returns `IR_ERROR_LIMIT`, allocation failure
`IR_ERROR_MEMORY`. Failure preserves both the result view and the arena length.

The producer writes disjoint columns directly and publishes a complete function.
The byte columns have stride 1 and the operand columns stride 4; allocation
aligns their bases to 64 bytes. There is **no readable or writable tail padding**.
Logical length, allocation capacity and the kernel's 16-lane width are distinct.
The vector verifier uses masked final loads and bounded indexed byte reads;
callers may also supply unaligned, exact-length views. Empty functions are valid
only with zero blocks and a void result. Empty side arrays may have null bases.
Raw pointers must actually cover their declared counts; no verifier can establish
the mapping ownership of an arbitrary caller-supplied pointer.

The producer and consumers preserve instruction order, block IDs and side-effect
order. The IR has no hidden allocation or automatic mutation of use lists. A
rewrite that changes instruction numbering or block membership builds fresh
columns and explicitly rebuilds the affected derived arrays. Allocations and
views belong to the caller; releasing the function arena releases its IR.

Verifiers restore scratch on success and failure. Derived-array builders and
lowering require disjoint input, output and scratch storage, and commit their
output only on success. A consumer can keep CFG or use arrays in a separate
shorter-lived arena and discard them immediately after its pass. No full-vector
buffer must be retained across a semantic dependency: complete functions and
contiguous block ranges are the logical batches, with every partial 16-row batch
processed immediately.

## Instruction columns

| Column | Width | Meaning |
| --- | ---: | --- |
| `op` | u8 | `IrOp`; below 64, for one-register byte tables |
| `type` | u8 | Result type; `void` instructions cannot be value operands |
| `flags` | u8 | Currently `BUSTER_IR_VOLATILE` on loads/stores |
| `alignment_log2` | u8 | Load/store alignment as a power of two, 0 through 31 |
| `a`, `b`, `c` | 3 × u32 | Value IDs or opcode-specific indices |

These columns use **16 bytes per instruction**, before block offsets and sparse
tables. `arena_bytes` reports the entire allocation charged to the arena, including
alignment gaps. It is not just the sum of payload widths.

Types are `void`, `i1`, `i8`, `i16`, `i32`, `i64`, `i128`, `f32`, `f64`, `f80`
and `ptr`, plus first-class `v4i32`, `v8i32`, `v16i32` vectors and `m8`, `m16`,
`m32`, `m64` masks. The [SIMD contract](ir-simd.md) records their exact lane,
predication, sparse-constant and unsupported-consumer rules. Result admission
uses bounded 32-bit type sets; all old scalar IDs remain unchanged.
Signedness is in operations such as `sdiv` / `udiv` and `sext` /
`zext`, rather than duplicated in integer type IDs. Aggregates remain in memory.
Integer arithmetic wraps at the result width. Division and shift validity are
the language/lowering pass's responsibility; the structural verifier does not
prove dynamic divisors or shift counts. Floating predicates follow C: ordered
comparisons, except `ne`, which also succeeds for unordered operands.

All unused operands are `BUSTER_IR_NONE`; unused flags/alignment are zero.
This canonicalization catches accidental metadata reuse and makes printing and
column comparisons deterministic.

| Operations | Operand contract |
| --- | --- |
| `const` | `a` indexes a two-u64 bit-pattern constant; `b,c` absent |
| `vconst` | `a` indexes a sparse eight-u64 vector constant; `b,c` absent |
| `vcmp` | `a,b` have the same vector shape; `c` is an integer predicate; result is m8 or m16 for that shape |
| `vselect` | `a` has the shape's mask type; `b,c` match the vector result; inactive lanes take `c` |
| `undef` | No operands; an unspecified value of its result type |
| `param` | `a` indexes `parameter_types` |
| Unary / conversion | `a` is the source value |
| Binary arithmetic | `a,b` are values; result/operand types agree |
| `intrinsic` | `c` the operation (`BUSTER_IR_INTRINSIC_*`, printed after the operands). Binary scalar kinds use `a,b` of the result's type, `i32` or `i64`: `mulhu` (an `i64`'s high unsigned product), `pdep`, `pext`, `bzhi` and `bextr` with BMI semantics. Unary `splat` uses scalar `a` (`i8`, `i16`, `i32`, `i64`, `f32` or `f64`), absent `b` and a vector result, repeating the scalar's raw bits. The unary bit operations (#1374), `tzcnt`, `bsf`, `lzcnt`, `bsr`, `popcnt` and `bswap`, use `a` of the result's type, `i32` or `i64`, and absent `b`. The signed overflows (#1385), `saddo`, `ssubo` and `smulo`, use `a,b` of the result's type, `i32` or `i64`, and give 1 or 0. All kinds have zero flags; see [Intrinsics](#intrinsics-141) and [scalar splats](ir-simd.md#scalar-splats-1450). |
| Shifts | `a` matches the result; `b` has an integer type |
| `icmp`, `fcmp` | `a,b` are equally typed; `c` is `IrPredicate`; result is `i1` |
| `select` | `a:i1`, `b,c` match the result type |
| `stack_addr` | `a` indexes `slots`; result is `ptr` |
| `symbol_addr` | `a` is a link `SymbolId`; result is `ptr` |
| `address` | `a:ptr`, optional `b:i64`, `c` indexes `addresses` |
| `load` | `a:ptr`; result is the loaded type |
| `store` | `a:ptr`, `b` is the stored value; result is `void` |
| `masked_load` | `a:ptr`, `b` the mask of the lanes in flags (integer lanes alone), `c` the passthrough of the vector result's type; alignment a load's. A lane whose bit is clear is the passthrough's and is never read ([masked loads and stores](ir-simd.md#masked-loads-and-stores-1840)) |
| `masked_store` | `a:ptr`, `b` the stored vector, `c` the mask of the lanes in flags; result is `void`. A lane whose bit is clear is never written |
| `call`, `call_indirect` | `a` is a symbol or a pointer value; `b` indexes `calls`; `c` an aggregate result's destination (a `ptr`), or absent |
| `phi` | `a` is an extra-word offset, `b` the incoming count; pairs are `(predecessor, value)` |
| `br` | `a` is a block ID |
| `cbr` | `a:i1`, `b` true block, `c` false block |
| `switch` | `a` integer value, `b` extra-word offset, `c` case count; words are `default_block, constant_id, target, ...` |
| `ret` | Optional value in `a`, matching the function result; the instruction itself is `void` |
| `unreachable` | No operands; a terminator |
| `br_indirect` | `a:ptr`, a label's address; a terminator whose successors are its listed labels (`b` an extra-word offset, `c` their count; label indices) or, with `b` and `c` none, the function's labels |

Constants preserve exact low/high bits, including NaNs, negative zero and x87
payloads. Unused bits above the type's width must be zero. The printer uses fixed
hexadecimal payloads, independent of locale or decimal rounding.

`IrSlot` records size, power-of-two alignment, optional scalar type, and flags for
address-taken, volatile or temporary storage, or for one of a variadic
definition's two anchors (below). A `void` slot is untyped memory; scalar slots
must be large enough for their type. `IrAddress` holds a signed
64-bit displacement and scale 1, 2, 4 or 8. Other scales lower into a multiply
and an address with scale 1. The IR does not promise that every displacement
fits an x86 immediate; selection must materialize larger ones.

`IrCall` records an argument-ID slice in `extra`, a fixed parameter-type slice in
`call_types`, result type and variadic flag. Calls retain C-level values, rather
than assigned ABI registers or stack offsets. Actual value types must match
fixed parameters; additional variadic arguments must be nonvoid. `symbol_count`
is an exclusive bound supplied by the translation unit; `UINT32_MAX` permits any
nonzero, nonsentinel link ID. Zero is reserved by `link.c` and is rejected for
both direct calls and symbol addresses. A call that passes or returns an
aggregate by value has ABI records (`IrCall.abi`, below), and so does one that
passes an aggregate the convention ignores, as an empty struct of no bytes is,
which a call without records would place in a register by its type (#141); the
typed stream loses
the named/variadic parameter boundary, as described below.

### Labels and `br_indirect`

GNU C's labels as values and the producer's jump tables (#133's
`JUMP_INDIRECT`) are the function's labels: `labels[k]` is label `k`'s block, at
whose code the producer defines its `k`-th label symbol, as it defines it at
`Tier0Code.label_records[k]` for Tier 0. `br_indirect` goes to the label whose
address its operand holds, one of the labels it lists (#785: `extra[b..+c)`, label
indices, printed `[#0, #2]`) or, without a list, any of the function's; its CFG
successors are those labels' blocks, one edge per distinct block, in their order.
Each label a `br_indirect` may go to must have a block; any other may have none
(`BUSTER_IR_NONE`): no jump reaches it, and a backend defines its symbol anywhere
in the function. A pass that removes a label's block (SSA promotion's unreachable
blocks, the optimizer's dead ones) leaves the label none; one that threads a
block's jump moves the label to its target; and every rewrite keeps each list's
label indices. A jump table's dispatch reaches only its own table's labels, and a
computed goto only labels whose addresses the program takes (GNU C's rule), so the
typed-stream adapter gives each jump table's jump a `br_indirect` listing its
table's labels, and all of a function's computed gotos one, which they branch to
(below), listing the labels of no table: its CFG has an edge per jump and per
label rather than every jump's to every label.

### By-value aggregates' ABI records

The IR has no aggregate types, so a struct or union passed or returned by value
(and #136's other objects that cross calls: a long double parameter, an
`__int128` argument, parameter or result) carries the producer's calling-convention
assignment, which no IR type can give a backend. These records are trusted, as
`IrDominance` is: the verifier checks the IR's own rules, not the convention's.

- `IrPassed { AbiLocation location; uint32_t size; }`: an argument's or a
  parameter's location, and its size, nonzero for an aggregate, whose IR value is
  a `ptr` to its bytes. The callee receives its own copy of them.
- `IrAbiCall { AbiCall call; uint32_t passed; uint32_t result_size; }`: a call's
  totals and result, its arguments' records (`passed[passed..+argument_count)`)
  and, nonzero, an aggregate result's bytes, which the call writes where its
  instruction's `c` operand (a `ptr`, the call's destination) points; the call's
  own result type is then `void`. `IrCall.abi` is 1 plus the record's index in
  `abi_calls`, or 0 for a call of C values alone. The destination is a use at
  position 2, so promotion and the optimizer treat its slot as escaping.
- The function's own, when `parameter_passed` isn't 0: its parameters' records,
  `passed[parameter_passed - 1..+parameter_count)` (an aggregate's `PARAM` is a
  `ptr` to the callee's copy), its result as the convention returns it
  (`result_abi`) and, nonzero, `result_size`: an aggregate returned in registers,
  whose bytes the function returns from the address its `ret` operand holds (its
  result type is then `ptr`). A result through a hidden pointer (`result_abi`'s
  kind `MEMORY`) keeps the stream's form: parameter 0 is the pointer, which the
  function writes and returns; a call of it passes its destination there.

The verifier checks each record's slice, the destination's presence and type
against the call's result size, and that each aggregate argument and parameter is
a `ptr`. A backend that compiles a call or a function with records takes their
locations as they are; one that doesn't (Tier 1 today) refuses them.

### A variadic definition's incoming frame

#148's `va_start` and `va_arg` are the front end's: they read and write a
`va_list`'s fields and the incoming frame through two anchors. With `variadic`,
the function has its frame: `incoming`, its named arguments' assignment
(`Tier0Function.incoming_variadic`: the overflow area starts `stack_bytes` past
the incoming stack arguments). The anchors are slots, address-taken and untyped,
at most one of each, aligned to at least 16:
`BUSTER_IR_SLOT_REGISTER_SAVE_AREA` (`BUSTER_ABI_SAVE_AREA_BYTES` bytes, which
hold at entry the argument registers as the psABI lays them out: rdi to r9, then
xmm0 to xmm7 from byte 48) and `BUSTER_IR_SLOT_OVERFLOW_AREA` (no bytes of its
own: its address is where the incoming stack arguments past the named ones
start). A `va_list` is an ordinary slot of its 24 bytes.

## Blocks, CFG and use lists

`block_offsets[block_count + 1]` partitions all instructions into nonempty,
contiguous ranges. Every range ends in exactly one terminator; phis are a prefix
of their block. Broad rewrites rebuild these ranges. There are no linked ranges.
Block zero is the function's entry and must contain no phis: initial entry has
no predecessor edge selecting an incoming value. A producer introducing phis
into an entry loop must create a real preheader with explicit initial values.
Edges back to block zero remain valid for phi-free input; no implicit `undef`
incoming edge or value is invented by the verifier.

`ir_build_cfg` validates shape, instruction rules and terminator placement, then
builds CSR successors/predecessors and `instruction_blocks`. Its topology build
does not substitute for the full verifier's phi or dominance checks. Multiple
targets from the same source to the same destination become **one CFG edge**.
Successors retain first occurrence order in the terminator (true before false;
default before switch cases); predecessors are in ascending source-block order.
The successor columns directly match `dataflow.c`'s existing CFG interface.

Each phi names every distinct predecessor exactly once, with no extra entries.
Its value is used at the **end of that predecessor**, so a loop backedge can
legitimately name a later instruction. Self-referencing phis outside the entry
block are structurally valid; removing trivial phis is an SSA construction/cleanup policy, not an
assumption imposed by this representation.

`ir_build_uses` fully verifies the function, then counts uses, prefix-sums the
counts and fills `IrUses`. A caller that has just verified the function, as the
optimizer's preparation has, calls `ir_cfg_validated` and `ir_uses_validated`,
which build the same arrays without verifying it again (#786). `offsets` has `instruction_count + 1` entries;
`uses` contains `(instruction, operand)` pairs. Positions 0/1/2 denote a/b/c,
and 3 plus the argument/incoming index denotes a call or phi side operand.
Each slice is stable in instruction/operand order, and repeated operands produce
repeated uses. Void-result instructions have empty slices. Block IDs, symbols,
slots, constants and side-table offsets never count as value uses. Derived counts
are accumulated in u64 and checked before becoming u32 offsets.

The builder has two twins (#1634). The scalar one, `ir_uses_validated_scalar`,
decodes each operand through `ir_operand`. The vector one, `ir_uses_validated`,
decodes 16 instructions at a time (`ir_uses_batch`):

- One two-register byte permute (`vpermi2b`) of the opcode tables, 128 entries
  each since the masked loads and stores took opcodes 64 and 65 (#1840), gives
  every instruction's `a`, `b` and `c` masks. One compare per column finds the optional operands that are present.
- `pdep` spreads the three column masks into one 48-bit mask. Bit `3 * lane +
  column` is set where that instruction has that operand, so ascending bits run
  in `ir_operand`'s order: instruction by instruction, then `a`, `b`, `c`.
- The count and fill loops walk that mask with `tzcnt` and read each operand
  straight from its column, with no per-operand decode and no staging buffer.
  A first version interleaved and compressed the operands through a stack
  buffer. On the 9700X its overlapping vector stores and scalar reloads made
  32-instruction functions 14% to 19% slower, so it was dropped.
- A phi's inputs or a call's arguments follow that instruction's own fixed
  operands, so the mask is walked in segments that end at each phi or call.

Both twins allocate the same arrays and build identical lists. The optimizer's
preparation uses the scalar twin on its scalar path. Placing a use is still
a dependent increment of its value's cursor, so a value with many uses
serializes its own increments in both twins.

On the 9700X, against `687f44c3` (before the twins), `BUSTER_DEBUG_INFO=0`,
CPU 2, three `ab` rounds:

| Measurement | Change | Controls |
| --- | ---: | --- |
| `benchmark-ir`'s `uses` (verification and building), 32 instructions, 3 placements | −14.9% to −17.6% | vector verifier, CFG builder and scalar verifier −4.4% to +4.3% |
| the same, 262,144 instructions | −5.5% to −8.1% | same |
| `benchmark-ir-source` on the frozen `sqlite3.c`, `gvn` per typed entry | 129.99 → 118.87 ns (−8.6%) | `gvn-scalar` −0.5% |
| the same, `optimize` per typed entry | 123.31 → 114.67 ns (−7.0%) | `optimize-scalar` +0.0% |
| `-c --tier1` of that file | 388.40 → 386.40 ms (−0.51%) | objects byte-identical |

The scalar paths build their lists with the unchanged scalar twin, so they
serve as controls. The large synthetic function gains less because three
quarters of its instructions use value 0, the serial chain above.
[The measurement record](https://github.com/buster14a/buster-performance/tree/main/docs/measurements/uses-1634-2026-11-07)
has the scripts, the logs and the dropped first version's round.

## Verification

`ir_verify` checks common instruction rules 16 rows at a time. Byte-permute tables
classify operand roles and semantic rules; type-set membership, operand/result
agreement and terminator masks are vector comparisons. An operand's type is a
byte permute of two registers holding the 128 types that end with the batch's
own (all of a function of at most 128 instructions), or of one holding the
function's first 64 (its parameters' and entry constants'); only a value in
neither is a byte load of its own, so no read is a hardware gather or an
accidental four-byte read from a byte column (`ir_operand_types`, #786).
Constants, calls, phis, switches and conversions use exact scalar handling; a
failing vector batch also uses the scalar rule to produce the same diagnostic.
`ir_verify_scalar` checks one instruction at a time. Both return the same
`IrCheck` fields, including the first error location.

The graph phase checks phi coverage/types, duplicate switch constants and use
ordering. Duplicate switch detection uses stable byte-radix sorting with at most
16 passes, so adversarial i128 keys cannot cause a quadratic hash-probe scan. The
vector path finds the phis and switches 64 operations at a time; a phi after a
non-phi of its block is the first misplaced one, so the check needs no state
carried through the block.

An optional `IrDominance` view supplies trusted half-open dominator-tree intervals.
Both endpoints are zero for an unreachable block. The verifier checks use order
within a block regardless of this view. With it, cross-block definitions must
enclose the use block's interval; cross-block uses involving unreachable blocks
are rejected. Phi uses use their predecessor's position and interval. The vector
path checks 16 instructions' operand columns at a time (`ir_dominance_batch`,
#786): a use of an earlier value of its own block, the common case, needs nothing
more, and its batch's blocks, at most 16 consecutive ones, give each lane's block
start and interval by a permute of one load. With the view, an earlier block's
definition has its block and interval gathered. A use at or after its own
instruction, a phi's and a call's operands, and a failing lane go to the exact
per-instruction check (`ir_check_operands`), which is the scalar twin, in order,
so both report the same first error. `dominance_checked` distinguishes complete interval checking from
structural verification. The verifier does not construct or prove the supplied
dominator tree: #174 owns that analysis.

## Typed-stream lowering

`ir_lower_typed(input, result_type, symbol_count, output, scratch, result)` and
its scalar twin accept the current `Tier0Function` contract after validating it.
The semantic result type is explicit because the stream's `AbiReturn` describes
locations, not its source type: it is the RETURN entries' class, which for an
aggregate is its address's (U64, `i64`). A source producer takes it from the
RETURNs, or for a function none leaves from its declared result
(`ir_source_result`).

An indirect call (a calls table's target 0) pops, under its arguments, the
address it calls: it becomes a `ptr` (`inttoptr`) for `call_indirect`.

A narrow argument (class `I8`, `U8`, `I16` or `U16`: a char's, a short's or a
`_Bool`'s) crosses its call as an `i32`, extended by its class's signedness
(`sext` or `zext`), and its call type is `i32`. clang's callers extend such an
argument to 32 bits, and its callees may rely on that (#141). A narrow parameter
arrives as an `i32` parameter, truncated to its type before its slot's store, so
the callee relies on no caller's extension. A call's types still match its
callee's parameter types.

#148's incoming variadic frame becomes the IR's (`variadic`, `incoming`), and its
roles' locals slots: a `va_list` (`BUSTER_TYPED_LOCAL_VA_LIST`) its 24 bytes
(aligned to 8), the register save area and the overflow area the two anchors.
tier0_validate checks the roles' rules first (a role on a parameter, two roles,
an anchor without the frame, a role without `ADDRESS_TAKEN` or on a class other
than U64 is a structural error).

#137's aggregates by value get their ABI records (above). An aggregate argument's
address becomes a `ptr`, with its size and location; a call with a result object
(`call_result_objects`) gets the object's slot address as its destination and the
object's size as its `result_size`; an object parameter is a `ptr` whose bytes the
preheader copies into its object, as `COPY` copies (below); a result in
registers (`result_size`) returns `inttoptr` of its RETURN's address, and the
IR's result type is `ptr`. A function with an object parameter or an aggregate
result, in registers or through a hidden pointer, has its own records; a call that
passes or returns one has its call's.

Long double (`F80`) and the 128-bit integers (`I128`, `U128`) are `f80` and
`i128` values, which Tier 0 keeps in frame slots. A constant of `F80` is its
`x87_constants` pair (the significand, then the sign and exponent); one of `I128`
sign-extends its 64 bits, of `U128` zero-extends them. Their locals are objects of
at least 16 bytes, which `LOCAL`, `STORE_LOCAL` and `SET_LOCAL` read and write as
their first 10 or 16 bytes. The expression slots that carry stack values across
blocks are 16 bytes, aligned to 16, in a function with values of these classes,
and 8 otherwise. The classification's signedness and floating tables (one byte
per class, which both paths read) take `I128` as signed and `F80` as floating,
which Tier 0's own predicates leave apart.

#136's frame objects (a local with a size in `local_sizes`: an array, a struct
or a union, of class VOID, address-taken) each get a slot of their own: the
object's size, `1 << local_alignments[l]` as its alignment, type `void` and
`BUSTER_IR_SLOT_ADDRESS_TAKEN`, so #134 never promotes it. `LOCAL_ADDRESS`
reaches it as it reaches any address-taken local, and its bytes go through
`PTR_ADD`, `LOAD` and `SET` as ordinary memory. A slot's bytes start as the
frame leaves them: the typed contract leaves an object's first contents to the
front end.

`COPY` (`value` bytes, 1 to `BUSTER_TIER0_MAX_COPY_BYTES`) needs no IR operation
of its own. Both addresses cross `inttoptr` once; then, from offset 0, each piece
is the widest integer that fits what remains (i64 while 8 bytes remain, then at
most one each of i32, i16 and i8), loaded from the source and stored to the
destination at the same offset, through an `ADDRESS` with no index (offset 0
uses the pointer itself). 256 bytes take 32 load/store pairs, 23 take five. The
typed contract makes the two ranges the same or apart, so loading each piece
before storing it reads the source's bytes either way; a partial overlap would
need every load first. The pieces' alignment is unknown (`alignment_log2` 0),
as a typed `LOAD`'s is.

#136's `ALLOCA`, `STACK_SAVE` and `STACK_RESTORE` lower to the dynamic-stack
operations below. #148's `FRAME_ADDRESS` (`__builtin_frame_address(0)`, and
`__builtin_return_address(0)`'s load 8 bytes above it) lowers to a
`stack_save` of the flag `BUSTER_IR_STACK_SAVE_FRAME` (#1847): the function's
frame pointer, whose IR opcodes are all taken. #147's `TAIL_CALL` is refused
(`IR_ERROR_UNSUPPORTED`, below). Typed validation still requires class U64 and `value == 0` for each, and a
U64 operand for allocation/restoration. The stack-shape plan treats allocation
as replacing its top value, save as pushing one value, and restore as popping
one. Allocation emits `alloca.ptr` followed by `ptrtoint.i64`; saves already
produce i64. The existing local and expression-edge slots carry these bits
through branches and loops without moving their effects. This adds no planning
arrays, instruction columns or input traversals for ordinary functions.

#133's labels (`label_entries`) each start a block, and the IR's labels are
those blocks. A jump table's `JUMP_INDIRECT` (`Tier0Function.table_jumps`, #785)
pops its label's address at statement level and is a `br_indirect` of its table's
labels (`label_tables`), which the plan arrives at with the stacks empty. Any
other `JUMP_INDIRECT`, a computed goto, stores the address in the dispatch slot
(an `i64` expression slot after the depth slots) and branches to the dispatch
block, the function's last, which loads it and is the `br_indirect` of the labels
of no table (every label, in a function without tables); the plan arrives at
those labels once, with the stacks empty.
This is clang's `indirectgoto` block. With a `br_indirect` per jump, each of a
function's jumps would be a predecessor of each of its labels, and promotion would
give a label a phi of a pair per jump for each value live there: Lua 5.4.8's
`luaV_execute`, whose 78 computed gotos (`vmdispatch`) can each go to any of its 83
labels, promoted into 213,368 phi pairs (1.7 MB, 4.2 times `lvm.c`'s lowered IR),
and `sqlite3VdbeExec`'s five jump tables into 20,000. Through the dispatch block,
whose phis promotion makes of the jumps' values, they are 3,555 and 6,089, and
`lvm.c`'s promoted IR is 457,577 bytes, less than its lowered 518,569. A backend
that wants a jump per site again, for the branch predictor, duplicates the
dispatch block into its predecessors, as LLVM's tail duplication does.
`TAIL_CALL` is refused with `IR_ERROR_UNSUPPORTED`, as is a
segment override the IR doesn't carry (#1447, `ir_typed_segment_valid`: an
`ATOMIC_RMW`'s or `ATOMIC_CAS`'s, or a long double's or a mask's access; "Segment
overrides" below), and the
vector values and `X86` rows that the [SIMD contract](ir-simd.md) doesn't admit
for the output target (any, through the old entry points) with
`IR_ERROR_UNSUPPORTED`, at their entry, after validation and before anything
is allocated: no function of the scalar corpora has one. A static
assertion pins the typed kinds' count, `FENCE` (#1059) the last, so that a new kind
fails to build until this adapter lowers or refuses it, since the plan and the
emitter would take an unnamed kind for a binary operation. A `FENCE` lowers to the
IR's `fence` of its value (below).

The ordered replay is written once in `ir_lower_emit.c` and instantiated for
counting and filling. Counting fixes its output to null; filling receives the
non-null local function after `ir_create` succeeds. Constructor helpers stay
inline so count mode can erase column writes and fill mode can erase null checks
before general inlining decisions. Both scalar/vector paths use the same two
instantiations, and all existing allocation, validation and rollback gates remain.

### Slot lifetimes (#177)

`lifetime` (void, `a` a slot, `b` 0 for `start` and 1 for `end`; printed
`lifetime.void $N start` or `end`) marks where a frame slot's object's lifetime
starts and ends. It reads and writes nothing: after a start the slot's bytes
are indeterminate, and after an end nothing may access the slot until the next
start. A slot without markers lives throughout its function. The lowering puts
markers before the typed entries that `Tier0Function.lifetime_entries` names
(the C driver's `--tier1` gives a block's objects its lifetime:
[the frame guide](tier1-frame.md)'s "Slot sharing between objects"), and both
verifiers check the slot and the edge by the slot rule. The optimizer keeps
markers as roots and moves no load or store past them; SSA promotion drops a
promoted slot's markers with its address and renumbers the others' slots. Tier
1's selection gives them a rule with no records, and the CFG consumer colors
the frame's objects by them (`tier1_emit_cfg_lifetimes`).

### Fences (#1059)

`fence` (void, `a` its order, C's `memory_order` 0 to 5 in bits 0 to 2, and bit 3
for a signal fence; printed `fence.void ORDER` or `fence.void ORDER signal`, the
orders `relaxed`, `consume`, `acquire`, `release`, `acq_rel` and `seq_cst`) is a
memory fence: the typed stream's `FENCE`, which the C producer gives a fence of a
constant order rather than a helper's call ([the builtin integration
guide](c-builtins-integration.md)'s "Helper contracts and direct forms"). It
reads and writes no value, but no pass moves a load, a store or a call across it,
eliminates one past it or drops it: it is an optimizer root
(`ir_optimize_roots`), neither value numbering's nor loop-invariant motion's
whitelists take it, and private-field replacement and promotion leave it where
it is, since a private slot no other thread or signal handler can reach. Both
verifiers check its operand by its own rule (`IR_RULE_FENCE`, the vector path's
deferred rows'); both interpreters run it as nothing, one thread's accesses
already in program order (they model no other thread). Tier 1's selection gives
it `TIER1_RULE_FENCE`: `mfence` for a thread fence of `seq_cst`, the one x86-64
needs an instruction for, and no records for the others. `tests/ir/fences.ir`
reads every order of both scopes back, `test-ir-optimize`'s `fence-dce` and
`fence-licm` pairs keep every fence in its place among the stores and loads (the
executor traces a fence among the stores, volatile loads and calls), and
`tests/ir-source/fences.c` runs through both lowerings, promotion, the text's
round trip and the IR's three forms against clang.

### Segment overrides (#1447)

A `load` or a `store` in `__seg_fs`, `__seg_gs` or address space 258 carries its
segment in `BUSTER_IR_SEGMENT` (flags bits 5 and 6: 1 FS, 2 GS, 3 SS), from the
typed stream's `BUSTER_TYPED_ACCESS_SEGMENT` (`ir_typed_access`), printed last as
`fs`, `gs` or `ss` (`load.i32 %0 align=4 fs`; `tests/ir/segments.ir`). SS's base
is 0 in 64-bit mode, but clang keeps the space apart and emits its `36`, so an
SS access is kept apart as the others are. Its address is an
offset from the segment's base, which nothing relates to a flat address, so the
segment is in `BUSTER_IR_OBSERVABLE`: promotion, private-field replacement and
Tier 1's folding keep the access as they keep a volatile one, and both
interpreters refuse it (`INTERPRETER_UNSUPPORTED`). A 128-bit integer's halves
(`ir_wide.c`) and a wide vector's pieces (`ir_vector_split.c`) keep their
access's flags. Tier 1's selection gives the access's memory record the
override (`Tier1SelectHoles.segment`, `tier1_select_segment`: `form + segment *
X86_FORM_COUNT`, as Tier 0's `tier0_apply_segments` does), a RIP-relative one
too. An atomic read/modify/write's flags hold its operation in those bits, so a
function with one in a segment is refused with `IR_ERROR_UNSUPPORTED` and Tier
0's, as is one with a long double's or a mask's access in one, which Tier 1
moves through rows of their own (`ir_typed_segment_valid`).
`check_test_driver_tier1_segments` runs such a unit, every function Tier 1's,
against clang's build, by both paths, for the native target and x86-64-v3.

### Atomic accesses (#1059)

A `load` or a `store` with `BUSTER_IR_ATOMIC` in its flags is an atomic access
of a C memory order (`BUSTER_IR_ORDER`'s bits: a load's `relaxed`, `consume`,
`acquire` or `seq_cst`; a store's `relaxed`, `release` or `seq_cst`), of an
integer of 1, 2, 4 or 8 bytes or a pointer, whose `align=N` is its size at
least; printed after `align=N` (and `volatile`) as `atomic ORDER`. It is one access of its type's bytes that no pass
splits, widens, merges, eliminates, forwards or moves past another access, a
call or a fence: the guarantees of a volatile access (`BUSTER_IR_OBSERVABLE`,
which promotion, private-field replacement, Tier 1's folding and both
interpreters read in place of `BUSTER_IR_VOLATILE`) and those of its order. Its
machine obligations are an ordinary access's for loads and relaxed/release
stores, since naturally aligned accesses of 1, 2, 4 or 8 bytes are atomic on
x86-64 and its total store order gives their orders. A seq_cst store remains
`store` in the IR and both native tiers emit memory `xchg`, discarding the old
value without adding a fence instruction (#1396). It still has write-only
source semantics: an initializing store never reads undefined bytes in either
interpreter. Tier 1 copies a live desired value into its reserved scratch before
the destructive exchange. An explicit `fence.void seq_cst` still emits `mfence`.
Both verifiers check the flags (`ir_access_flags_valid`; the vector path sends
any row with flags past `volatile` to the scalar rule), and the type and the
alignment (`ir_atomic_access`): an atomic access declared below its natural
alignment is refused, since the backend emits one access of its size whatever
the address. The typed stream's producer is the C lowering's direct
atomic load or store ([the builtin integration guide](c-builtins-integration.md)'s
"Helper contracts and direct forms"): its marked `LOAD`, `STORE` or `SET`
(`BUSTER_TYPED_ACCESS_ATOMIC`), which `ir_lower` translates with its class's
natural alignment, which the checker proved, and refuses in another shape. `tests/ir/atomics.ir` reads every order back, `test-ir`'s verifier
cases refuse an order a kind doesn't take, an order without the flag and an
order past `seq_cst` and an 8-byte access aligned to 1 or 4 (as `test-ir-parse`
refuses one written as text), and `tests/ir-source/atomics.c` runs through both
lowerings, promotion, the text's round trip and the IR's three forms against
clang, its `long`'s accesses printed `align=8`. `tests/ir-source/atomic-stores.c`
also runs through the typed interpreter and strict native Tier 1: initial stores,
nested assignments, operands evaluated once, desired values live past a store,
and narrow integer, bool, pointer and floating representation results. Both
interpreter unit suites check initializing seq_cst stores with memory and
undefined-byte checking enabled; read/modify/write checks remain strict.

### Atomic read/modify/writes (#1059)

`rmw` (an integer of 1, 2, 4 or 8 bytes, a pointer's as `i64`; `a` the object's
address, `b` the operand, of the result's type; its flags `BUSTER_IR_ATOMIC`, a C
memory order, any of the six, since the locked access is a full barrier, and its
operation (`BUSTER_IR_RMW_OPERATION`'s bits): `BUSTER_IR_RMW_EXCHANGE`,
`BUSTER_IR_RMW_ADD` or `BUSTER_IR_RMW_COMPARE`, whose `b` is the expected value
and `c` the desired one, of the result's type, an optional value the others
have none of (`ir_optional_table`), since the IR's 64 operations left no room
for another then; printed `rmw.iN %p, %v align=N xchg|add ORDER` and
`rmw.iN %p, %e, %d align=N cas ORDER`) returns the object's old value and leaves
the operand (an exchange), the sum (an addition), or the desired value where the
old one is the expected one and the old one where it isn't (a compare-exchange,
as `cmpxchg` writes it) in it, as one atomic access of its type's bytes: the
typed stream's `ATOMIC_RMW`, which the C producer gives an exchange, an addition
or a subtraction of a constant order, and its `ATOMIC_CAS`, whose address is a
local's (`ir_lower` reads it), which it gives a compare-exchange
([the builtin integration guide](c-builtins-integration.md)'s "Direct
read/modify/writes"). Its alignment is its size at least, as an atomic load's
is, and `ir_lower` gives it its class's natural one. No pass moves an access or a
call across it, merges it or drops it: it is an optimizer root, neither value
numbering nor loop-invariant motion takes it, and its address counts as taken,
so promotion and private-field replacement leave its object in memory; dataflow
reads its address and its operand, as a store's, and a compare-exchange's
desired value, which inlining, rebuilding and every pass's operand mask
(`ir_value_mask`) map as a call's optional destination is. Both verifiers check its flags
(`ir_rmw_flags_valid`) and its type and alignment (`ir_atomic_access`, its own
rule's, `IR_RULE_ATOMIC_RMW`); both interpreters run it as one thread does, a
byte at a time through volatile accesses, refusing an address misaligned for its
declared alignment and checking its object as a load's (undefined bytes are
reported) before a store's (a compare-exchange's desired value is a use, even
where it isn't stored). Tier 1 selects `TIER1_RULE_ATOMIC_RMW`:
the operand copied to the scratch register, `xchg` or `lock xadd` of the object's
width (never the narrow value's 32-bit register's), and the old value
zero-extended as a load's is; a compare-exchange saves rax in the scratch
register (reading an operand that was in rax from there), moves the expected
value into rax, `lock cmpxchg` of the object's width, and the old value
zero-extended from rax, rax restored unless it is the result's; a barrier no load
folds past. `tests/ir/atomics.ir` reads every width, operation and order back,
`test-ir`'s verifier cases refuse an operation past a compare-exchange, a
compare-exchange without a desired value of its type and another operation with
one, a non-atomic or a volatile one, an order past `seq_cst` and an 8-byte one
aligned to 4 (and `test-ir-parse` one written as text aligned below its size, and
the desired value's misuses), `test-tier1-select` runs each width's exchange,
addition and compare-exchange (that succeeds and fails, each operand in rax in
turn) on guard bytes, and `tests/ir-source/atomics.c`'s read/modify/writes run
through both lowerings, promotion, the text's round trip and the IR's three forms
against clang.

### Intrinsics (#141)

`intrinsic` keeps its operation in `c` (`BUSTER_IR_INTRINSIC_*`): `mulhu`,
`pdep`, `pext`, `bzhi`, `bextr` and the unary scalar `splat`, one opcode since the opcode tables'
byte-permute register held 64 and was full (two registers' 128 since #1840), the 128-bit integers' `mulhu`
before it the opcode's. The text names the operation after the operands, as a
comparison's predicate (`%2 = intrinsic.i64 %0, %1 pdep`). Both verifiers check
it by its own rule (`IR_RULE_INTRINSIC`, deferred by the batch verifier): the
five scalar kinds require both operands of the result's type, `i32` or `i64`,
and `mulhu` an `i64`'s alone. `splat` requires one scalar `a` (`i8`, `i16`, `i32`,
`i64`, `f32` or `f64`), absent `b`, a vector result and zero flags. Its scalar
type gives the lane width and its raw bits are repeated unchanged, including
NaN payloads and signed zero (`%7 = intrinsic.v8i32 %3 splat`). The opcode's
operand metadata makes `b` optional; the semantic rule makes it mandatory for
the five binary kinds. The shared interpreter evaluates the scalar kinds as
their instruction pseudocode specifies and the splat as a bit copy. SCCP,
GVN and LICM can fold, reuse or move splats under their ordinary rules; the
five scalar kinds retain their previous optimization boundary. Dead code
removes an unused intrinsic. The typed stream's BMI rows lower to it
([the SIMD IR guide](ir-simd.md)'s "Scalar x86 builtins"), and Tier 1 selects
`mulhu` as `mul` and the other scalar kinds as their VEX instructions ([the selection
guide](tier1-selection.md)). Exact single-record producer splats also lower to
it; their target checks and native bit-copy expansion are documented in
[SIMD IR](ir-simd.md#scalar-splats-1450). `tests/ir/splats.ir` reads every scalar
type and vector width back. `tests/ir/intrinsics.ir` reads each scalar operation of
both widths back, `test-ir-parse` refuses an unnamed or unknown operation, an
`i32`'s `mulhu`, an `i16`'s and mixed operand types, `test-ir-interpreter` checks
each against values a Zen 5's instructions give (an index, a start or a length
at and past the width, of zero, and of the control's bytes alone), and
`test-checker-driver`'s Tier 1 fixture of the builtins runs both units' code by
`-march=x86-64-v3` against clang's.

The bit operations (#1374) are six more unary kinds, of one `a` of the result's
type, `i32` or `i64`, and absent `b` (`%2 = intrinsic.i64 %0 tzcnt`), each its x86
instruction's value: `tzcnt` and `lzcnt` the counts of trailing and leading zero
bits, the width for 0; `bsf` and `bsr` the indices of the lowest and the highest
set bit, unspecified for 0, which only a producer whose 0 is undefined or masked
writes (the interpreters give the width); `popcnt` the count of set bits; and
`bswap` the bytes reversed. The C producer's bit builtins reach them through their
rows ([the integration guide](c-builtins-integration.md)'s "The bit builtins'
instructions"): `ir_simd_typed_bits` decodes one record of the count's form, an
operand of 32 or 64 bits and a result of 32 bits or of the operand's width (a
64-bit count is then the intrinsic of the operand's `i64`, truncated), or the
byte swap's move and `bswap` of one class, and needs the features its form does
(BMI, LZCNT and POPCNT; none for `bsf`, `bsr` and `bswap`). Tier 1 selects each
as its one instruction (`bswap` after a move into its destination where that
isn't its operand's register). They keep the BMI kinds' optimization boundary:
GVN and LICM don't take them, SCCP doesn't fold them. `tests/ir/bit-intrinsics.ir`
reads each of both widths back, `test-ir-interpreter` checks each at the width's
boundaries and at 0, and `test-ir`'s fixtures refuse a second operand, a result
of another type and a type other than `i32` and `i64`.

The signed overflows (#1385) are three binary kinds, of `a` and `b` of the
result's type, `i32` or `i64` (`%4 = intrinsic.i64 %0, %1 saddo`): 1 where the
exact sum, difference or product of the two as signed integers leaves the type's
range, else 0, as x86's `add`, `sub` and `imul` set OF; a signed overflow
builtin's flag, whose value the typed operation beside it computes. The C
producer's signed 64-bit overflow builtins of one class reach them through their
rows ([the integration guide](c-builtins-integration.md)'s "The overflow
builtins' instructions"): `ir_simd_typed_overflow` decodes `add`, `cmp` or
`imul` of `INTEGER(1)` into `INTEGER(0)`, `seto` of `SCRATCH(0)` and its `movzx`
into `INTEGER_RESULT`, two operands of an int's or a long's class into an int,
as the intrinsic of the operands' type, truncated to the result's; their forms are
the baseline's. Tier 1 selects each as its operation and `seto`
(`tier1_select_overflow`). They keep the other kinds' optimization boundary.
`tests/ir/overflow-intrinsics.ir` reads each of both widths back,
`test-ir-interpreter` checks each at the width's boundaries, and `test-ir`'s
fixtures refuse a missing second operand, operands of two types, a result of
another type and a type other than `i32` and `i64`.

### Machine rows (#141)

The typed stream compiles C's vector operators and every vector builtin as X86
rows (`Tier0Operation`, #717): records whose registers are Tier 0's roles. The
operators' arithmetic, shifts and comparisons at every element type are the IR's
own operations of their lanes ([the SIMD IR guide](ir-simd.md)'s "Lanes"); the IR
has no operation per builtin (its opcode tables hold 128 opcodes, and the builtins
number in the hundreds), nor for a splat of a scalar or a shuffle, so any other
such row is the IR's machine row, which
a row call runs: an `IR_CALL` whose descriptor names the row (`IrCall.row`, 1 plus
its index in `IrFunction.rows`) in place of a callee, its symbol `a` 0. Its
arguments are the row's operands in order and its result the row's, of the types
`ir_row_type` gives their classes: an integer's of its bytes, whatever its
signedness, a float's or a double's, a vector's container of its bytes (`v4i32`,
`v8i32` or `v16i32`, whatever its elements: the row's records give its bits their
meaning; an SSE carrier's `f32` or `f64`, below), a mask's of its bytes, and
`void` for a row of no result (a masked store's). A row call is as opaque to the passes as a call of an unknown function,
which reads and writes memory: SSA promotion and SCCP, dead code and CFG cleanup
keep it where it is, and GVN and LICM refuse a function with a row, as they refuse
every SIMD function (`ir_function_has_simd`), and so does SROA without #1282's
`simd` option; the inliner clones a callee's rows with it, rebasing its row calls'
`IrCall.row`, a row call staying a call ([the inlining guide](ir-inlining.md)'s
"SIMD callees and by-value vectors"); the interpreter
refuses one too, having no model of x86 records (Tier 1's native code is its
oracle, against Tier 0's). The verifier checks each row call's descriptor against
its row (`ir_row_call_valid`): no symbol, no ABI records and no variadic
arguments, one fixed argument per operand of its operand's type, the result the
row's, and the row within its columns; the records themselves are the producer's,
trusted as `IrPassed`'s locations are, and Tier 1 checks them
(`tier0_operation_valid`) before it places them ([the selection
guide](tier1-selection.md)'s "Machine rows"). The lowering copies a row's used
fields alone, its unused records and operands zero, so that equal rows are equal
bytes.

The text has a line per row before the slots, its operands' classes and its
result's (the typed stream's names, a vector's and a mask's with their bytes:
`vector64`, `mask8`) and each record as `[<form> reg vvvv rm index mode decorations
immediate displacement]`, the form by its `X86Form` name without `X86_FORM_`, so
that the text doesn't depend on the form table's order; a row call names its row
as `row #k`:

```
  row #0 (vector64, vector64, mask8) -> vector64 [VPADDB_Z_Z_ZM512 0x80 0x80 0x81 0x00 0x00 0x0a 0 0]
  %4 = call.v16i32 row #0(%0, %1, %2) fixed=(v16i32, v16i32, m64) variadic=0
```

#### SSE carriers (#1280)

The typed stream's `VECTOR` class has values of 4 and 8 bytes too, the SSE
carriers: the narrow vectors of the intrinsics' shuffles and conversions (the
operand of `_mm256_cvtepu8_epi32`'s `vpmovzxbd`, the shuffle of `__m128i` to 8
bytes that makes it) and MMX's `__m64`. Their IR type is `f64` for 8 bytes and
`f32` for 4 (`ir_typed_stack_type`, `ir_row_type`), containers of their bits as
`v4i32` is a 16-byte vector's. The lowering carries them as any vector:

- a constant's first word becomes a `const` of its bits;
- `LOCAL` and `STORE_LOCAL` objects, loads and stores move their bytes, at their
  own alignment;
- `DUP` and `DISCARD` keep or drop them;
- the edge slots hold them by depth and type, as every value.

These are bits, not floats, and the existing types keep them exact:

- the lowering gives them no floating operation: only a row reads one, and the
  IR's own vector decoders take 16, 32 and 64 bytes alone;
- SCCP and dead code elimination fold no scalar floating arithmetic, and these
  carriers are scalars: their constants are bit patterns, a signaling NaN's among
  them, and GVN numbers no floating operation but `fneg`, which they never have;
- Tier 1's moves of `f32` and `f64` are bit moves ([the selection
  guide](tier1-selection.md)'s "Machine rows"). Each row operand is zero-extended
  into its role, as Tier 0's reloads leave it, which the rows rely on. The
  verifier admits any `f32` or `f64` value as a carrier operand, a parameter's or
  a call's among them, whose bytes past it the convention leaves undefined.

Two new types would have needed their own moves, spills and constants in Tier 1,
a place in the type masks and in the SIMD model's contiguous shapes, and the
interpreter's and the parser's support, for no bit these two don't keep.
An SSE carrier's row takes AVX512F and VL, as any vector narrower than 64 bytes
does, for the moves that stage it.

`tests/ir/rows.ir` reads rows of a vector, a mask and no result back, a row of two
records among them, and `tests/ir/carrier-rows.ir` rows of SSE carriers' operands
and results, their constants (a signaling NaN's bits and -0's), loads, stores and a
phi across a loop. `test-ir`'s SIMD checks refuse a row call with a symbol, a row
past the column, an argument or a result of another type (an SSE carrier's of a
vector's type among them), an argument too few, a variadic or an indirect one, and
a row of a 12-byte vector, and accept an SSE carrier's of an `f64` and of an
`f32`. `test-ir-simd`'s machine row decoding takes carriers' operands and results,
with VL's moves, and refuses vectors of 2 and 12 bytes. `test-ir-source --simd`
runs `tests/ir/vector-rows.c`'s and `tests/ir/carrier-rows.c`'s functions through
both lowerings, both promoters and the text's round trip.

### Asm bodies (#1839)

An asm statement the C driver places in its caller as an asm body
([the inline assembly guide](c-inline-assembly.md)'s "Asm bodies") is a body call:
an `IR_CALL` whose descriptor names the body (`IrCall.body`, 1 plus its index in
`IrFunction.asm_bodies`) in place of a callee, its symbol `a` 0 and its row 0,
void, of no ABI records and not variadic. An `IrAsmBody` holds the statement's
records (borrowed from the producer, `Tier0AsmBody`'s), its own branches and their
targets relative to its first record (a target may be the body's end), and the
registers it writes (`touched`, rax to r15 in bits 0 to 15 and xmm0 to xmm15 in 16
to 31). Its records address the statement's operand block through
`BUSTER_TIER0_ASM_BLOCK`'s base; the block is the call's first argument, a
`ptr` that is the block local's `stack_addr` (a body that reads no block has
none). The statement's register operands follow it (#2144): one argument per
operand its records read in a register when they start (`IrAsmBody.passed`), a
narrow integer extended as a call's argument is, each with its register in
`IrAsmBody.locations` (Tier 0's call row's `AbiLocation`s: a general register's
number, or `BUSTER_ABI_REGISTER_VECTOR | n` for xmm n), as Tier 0's call row
passes them (`CheckProgramAsm.inline_passed`), so that they need no round trip
through the block. `ir_asm_body_block` names the block argument.

So every pass takes a body call as an unknown callee's of that pointer: the block
escapes into it (neither SSA promotion nor SROA's `flagged` mode makes its slot
values), its stores stay before the call, no load or store moves across it, and
the operands' objects, whose addresses the block holds, are memory the call may
read and write. The inliner copies a callee's bodies with it (appended to the
caller's, `IrCall.body` rebased), its block argument the rebased slot's address,
as a row call's row is copied. The verifier checks a body call's descriptor
against its body (`ir_body_call_valid`): within the column, of no row, void, one
`ptr` argument or none, then a register operand's per location, each in
registers, and the branch columns in order within the records.

The text form has no syntax for a body: the printer writes `asm #k(...)` and a
line per body (`asm #k records=<count> branches=<count> touched=<mask>`) for
diagnostics, which the parser doesn't read, so the source tools whose round trips
print and parse the text (`test-ir-source`, `test-tier1-cfg-source`) keep the
statements' helpers (`CheckSourceRequest.asm_helpers`), and the structural
rebuild under the reducer refuses a function with bodies. The interpreter
(`interpret-c`, `interpret-ir`) refuses one as it refuses a machine row: it has no
model of x86 records.

### Effects through the consumers (#1059)

An audit of every IR and Tier 1 consumer for the effects #1059 asks to survive:
ordering (fences, atomic accesses and read/modify/writes), volatility, the
floating environment and termination.

- **Constant propagation and dead code** (`ir_optimize.c`). Loads, stores,
  calls, fences, `rmw`s, branches, returns and `unreachable` are unconditional
  roots (`ir_optimize_roots`), never dropped or folded. The floating operations
  are roots too. SCCP folds no scalar floating arithmetic, and folds a vector's
  floating lanes only where every rounding, FTZ and DAZ gives the same bits
  (#1450), keeping the operation's row for its flags. So a `FENV_ACCESS` region's
  rounding and exception flags stay the code's.
- **GVN and LICM** take allow-lists of pure operations (`ir_gvn_classify`'s and
  `ir_licm_classify`'s tables). Neither numbers nor moves a load, a store, a call,
  a fence, an `rmw` or a floating operation other than `fneg`, so a new operation
  is impure until it is listed.
- **SROA and promotion** leave every address-taken or volatile slot, and every
  observable access, in memory. The C lowering marks a volatile local's slot
  address-taken.
- **The inliner and the rebuild** copy each node's flags and alignment, and map an
  `rmw`'s optional desired value as a call's optional destination. Dataflow reads
  every operand `ir_value_mask` gives.
- **Tier 1's scheduler** pins every record that touches memory (a fence's effect
  is one), control, calls or MXCSR, or may fault. So a locked access, `mfence` and
  every SSE floating operation keep their places (`test-tier1-schedule`).
- **Tier 1's load folding** moves a load into its single consumer past no store,
  call, fence, `rmw`, stack move or other load (`tier1_select_fold_barrier`):
  - The typed stream doesn't carry a C access's volatility, so the IR's loads of
    volatile objects aren't flagged `volatile`. Two volatile reads must keep their
    order, so the IR treats every unflagged load as possibly volatile.
  - A volatile or atomic load folds into nothing.
  - `test-checker-driver`'s volatile-order check compiles two volatile reads
    through pointers by `--tier1`, which once read the second first.
- **Tier 0** keeps every entry in the stream's order.
- **Termination:** `unreachable` is a root that ends its block, and the terminal
  builtins' calls are roots and barriers, as every call is.

Carrying a C access's volatility in the typed stream (a marker on `LOAD`, `STORE`
and `SET`, and on locals and globals) would let Tier 1 fold past ordinary loads
again. That change touches every lowering path's access emission, and it isn't
made here.

### Dynamic stack contract

| Operation | Result | Operand a | Meaning |
| --- | --- | --- | --- |
| `alloca` | ptr | i64 unsigned byte count | Reserve `(size + 15) & ~15` bytes, returning a 16-byte-aligned address. |
| `stack_save` | i64 | absent | Capture this invocation's current dynamic-stack position as opaque bits. |
| `stack_save frame` | i64 | absent | This invocation's frame address (#1847, flag `BUSTER_IR_STACK_SAVE_FRAME`): its frame pointer, the address of the caller's rbp its prologue pushed. |
| `stack_restore` | void | i64 saved position | Release allocations after that position in this invocation. |

All other operands are `BUSTER_IR_NONE`; flags and alignment columns are zero,
but a frame address's flag. The interpreters have no machine frame and refuse a
frame address (`INTERPRETER_UNSUPPORTED`); the inliner takes no function with a
`stack_save` of either kind.
Alignment is exactly the typed contract's fixed 16 bytes, not a configurable
alignment or the type's size. No sparse metadata is needed: the usual 16 bytes
per instruction suffice. The operand/rule tables drive both verifiers, use lists,
SSA rewrites, printer and parser. Counts other than i64, wrong result types,
extraneous fields and memory modifiers are rejected deterministically.

Allocation, save and restore are ordered effects on per-invocation state. They
cannot be eliminated merely because their results are unused, commoned, hoisted,
speculated or duplicated. Allocations start undefined, including padding. A
zero count reserves nothing and need not have a unique address. It creates no
accessible extent; an address aliasing an older live object retains that object's
ordinary access checks. Rounding must not wrap: counts above `UINT64_MAX - 15` fail with
an arithmetic diagnostic in the execution oracle; an unavailable representable
extent fails with a resource diagnostic. Such inputs are outside successful
native execution, not permission to silently allocate a small wrapped extent.

Saved bits may travel through copies, phis, selects and ordinary i64 local
storage, but must not be fabricated, changed or dereferenced. A restore must
name a position captured in this invocation that has not been discarded by a
restore to an earlier position. Repeated restores to the same retained position
are valid. Equal-position captures are interchangeable. Static `IrSlot` storage,
local homes and expression-edge slots survive restoration; storage created by a
callee is released on its return. The producer owns source scope exits, including
break/continue/goto and backedges: the adapter preserves the supplied CFG and
does not invent VLA scope information. Return releases the invocation's storage.

Structural verification checks types, operands, CFG and ordinary dominance; it
does not claim a path-sensitive lifetime/provenance proof for handles transported
through mutable memory. Checked execution rejects handle values that do not
match a retained capture in the current invocation. An independently fabricated
integer with identical bits passes that numeric membership check; handle
provenance remains a producer obligation. Checked memory accesses diagnose
released or undefined bytes. The reference interpreters lazily place dynamic
records/bytes/definedness in the existing arena and register live extents with
the existing memory registry. Restoration unregisters later dynamic extents and
rewinds their storage together. No dynamic
records are allocated by ordinary functions. A restored address reused by a new
allocation cannot distinguish an old pointer's provenance: as with native
address reuse, callers must respect lifetimes.

SCCP treats the values as unknown; DCE roots all three operations. GVN and LICM's
positive purity tables exclude them. SROA still splits only fixed slots. The
inliner refuses bodies with these operations because it has no callee-lifetime
boundary repair. Native Tier 1 selection returns
`TIER1_SELECT_ERROR_UNSUPPORTED` transactionally at the dynamic operation. Its
current fixed rsp-relative frame/spill/call implementation cannot safely move
rsp. Native support requires a stable frame base, separately placed outgoing
arguments, aligned checked dynamic subtraction, epilogue recovery, and effects
through scheduling before that refusal can be removed. Tier 0 is unchanged.

`test-ir-lowering` exercises the real interpreters, scalar/vector lowering,
SSA/optimizer consumers, canonical text and the native refusal independently.
Generated VLA programs also compare shared-IR execution with the separate Tier 0
typed reference and Tier 0 native execution. This proves neither native Tier 1
VLA execution nor full source-corpus acceptance. `benchmark-ir-lowering` reports
ordinary shapes separately from dynamic programs, retained IR bytes and minimum
temporary scratch capacity for each path. Its ns/stack-operation number charges
the entire mixed lowering; it is not marginal operation latency or a native
runtime measurement.

Three additional isolated streams each lower 512 target operations: allocation
with constant-size operands/discards, saves with discards, and restores using
one initial save plus repeated DUP/restore pairs. Their ns/target-operation
figures include validation, planning, classification, exact allocation/emission
and verification, along with the stated operand setup. They measure lowering
throughput per operation, not generated instruction or runtime latency. Both
paths must agree before timing; retained bytes and minimum scratch capacity are
reported separately for every shape. Scratch-capacity searches run after the
recorded samples. `--ordinary` selects only the three established ordinary
shapes, including when comparing an identical harness on an older producer.

The strict generated dynamic case uses integer expressions with VLA storage,
calls and control flow, with memory and undefined-byte checking enabled.
Handwritten fixtures separately cover floating memory and ordinary fixed
objects surviving restores. Existing generated object/COPY tests remain intact.
Mixed representation-copy execution remains an independent consumer gap in the
IR path: the typed stream's COPY expands to ordinary integer loads and stores
(`ir_lower.c`), and the IR interpreter diagnoses an undefined padding byte in
such a load. A byte-representation transfer can legally copy padding without
observing its value; the typed interpreter now transfers the representation and
its definedness directly (`docs/ir-tools.md`, "Execution contract"), while the
IR expansion still needs the same repair. Disabling undefined checks here would
conceal real lifetime errors. The existing generator FP-to-integer/CVTT oracle boundary is
also excluded from the strict dynamic case. Neither gap is claimed resolved.

The adapter finds block boundaries, compares incoming stack shapes exactly,
classifies 16 entries at once (one at a time in the twin), counts output, then
emits into final-sized columns. Stack replay and irregular calls/control flow
are shared ordered work. Locals begin in typed slots. Only expression-stack
depths live across block boundaries receive temporary slots, one for each depth
and IR type that some block's incoming shape has (#1189), so that each slot holds
one type and promotes. Every block is entered with one shape, so a block's
stores and its successors' loads agree on each depth's type. One slot per depth
held several types in a function whose `&&`, `||` and `?:` results differ in type
at one depth, as Lua's `luaV_execute`'s do, and stayed in memory: each truth value
a store, a load and its slot's address, which removing took 1.2% of speedtest1's
`--tier1` instructions and 1.3% of Lua's (the Tier 1 guide's "Where the gap to
clang -O2 is"). Values are spilled before outgoing edges and reloaded in the
receiving block; `JUMP_VALUE` therefore preserves conditional-expression arms
even with an outer expression still live.

A separate preheader materializes slot addresses and initializes parameters.
It also defines, once, each jump table's base that #1740's column names. Each
direct symbol address the function takes at least twice
(`ir_typed_plan_hoisted`, #2574) is one value too: a `GLOBAL_ADDRESS` of a U64
whose symbol the access column reaches directly, counted in an open-addressed
table of the symbols and numbered in entry order. Every entry that takes it
reads that one value, which Tier 1 keeps in a register around a loop that reads
it on every iteration (Lua's dispatch table) and otherwise recomputes where it
is read ([the allocation guide](tier1-allocation.md)'s "Recomputable values
read on every iteration"). A GOT slot's or a thread-local's address stays where
it is taken.

Where the value is defined is `ir_typed_plan_places`'s (#2593), over the typed
CFG the emission's branches make (the preheader, each source block and the
dispatch block), its dominators and natural loops (`dominators.c`'s; the loops
only where an edge goes back in block order). The definition goes to the start
of its reachable takes' nearest common dominator, P, and then:

- where P is inside a loop, to its outermost loop's header's immediate
  dominator, outside it, as loop-invariant code motion hoists it, if some take
  is inside that loop and the value can keep a register there (#2618,
  `ir_typed_loop_resident`): every iteration of the loop takes it, and so does
  every iteration of a loop of its nest that holds a take and has an iteration
  that calls nothing. `luaV_execute` defines `disptab` before its dispatch
  loop, whose handlers that call nothing each end in its take. Otherwise the
  takes stay their own: where no take is inside the loop
  (`yy_find_shift_action`'s `yy_action`, read on two of the loop's exits
  alone), where some iteration doesn't take it (`sqlite3StrICmp`'s
  `sqlite3UpperToLower`, read where two characters differ, mostly as the loop
  ends), and where every iteration calls (`sqlite3_str_vappendf`'s `fmtinfo`,
  read through a format loop that appends each piece), over which Tier 1
  would keep the value in a callee-saved register the loop's own values need,
  or recompute it after each call;
- otherwise at P, where some take is inside a loop or where every path from P
  takes it before leaving the function (`sqlite3GetToken`'s `aiClass`, read by
  its switch). A path that takes it nowhere (`powerOfTen`'s `aBase`,
  `sqlite3VdbeMemSetText`'s destructor compared on two branches) leaves the
  takes their own, as before #2574; #2601's sharing in Tier 1's selection still
  joins those of one block.

P must also precede every take in source order, which the emission's numbering
needs and a backward `goto` can break: P then rises to its dominators until it
does. A take in an unreachable block stays its own, and dominators past their
matrices' limit leave every definition in the preheader, as #2574 did. At the
block's start, a definition no register keeps until a read is left out by Tier
1 (`tier1_emit_cfg_dead_definition`); at the block's end, where an edge seems to
read its register, it stayed, and speedtest1 ran 0.8 M more instructions.

#2574 had put every definition in the preheader. A path that never read it
computed it anyway, as `yy_find_shift_action`'s two tables on its early
return, and one live across a call took a callee-saved register that the
prologue saved on every call. By callgrind with #1189's `gap.c` (the inputs of
#1217's record) against main `df85213e`, speedtest1 runs 3,010.68 M
instructions before and 3,002.54 M after (−8.13 M; without any hoist,
3,006.43 M), and Lua's `luaV_execute` the same 2,420.29 M (without any hoist,
2,456.45 M). The rules measured on the way, as speedtest1's instructions
against main's:

| Rule | speedtest1 | `luaV_execute` |
| --- | ---: | ---: |
| no hoist | −4.25 M | +36.2 M |
| nearest common dominator alone | +5.42 M | +101.5 M |
| it, lifted out of its outermost loop | −4.31 M | 0 |
| a definition per outermost loop, none outside loops | −4.48 M | 0 |
| this rule | −8.13 M | 0 |
| this rule, and a symbol taken once in a loop | −2.14 M | 0 |
| this rule without the every-path test | −6.25 M | 0 |
| this rule, a definition per loop where P's paths don't all take it | −8.55 M | 0 |

A symbol taken once in a loop, hoisted out of it, also cost Lua 22 M in
`match`, `classend` and `luaV_finishset`. The last rule gains
`jsonTranslateBlobToText` (−0.89 M) and loses `sqlite3GetToken` (+0.64 M) for
two more passes over the takes, and isn't taken.

On the 9700X ([the record](https://github.com/buster14a/buster-performance/tree/201fb8b8aae14d797d47f7a3ace0aa754f992a91/docs/measurements/tier1-hoist-places-2593-2026-11-13),
#2574's method), Lua's time doesn't move (0.9996, 95% interval 0.9966 to
1.0026 over six link orders), and speedtest1's user cycles fall 0.66% over
six layouts (0.9934, 0.9863 to 1.0006) with 0.27% fewer instructions at
every one. The placement costs `sqlite3.c`'s `--tier1 -c` 0.14% more
instructions, in the same time and cycles: by callgrind on a baseline build,
7.6 M of 15,078 M, of which the dominators' analysis is 4.0 M and the CFG's
construction and the walks 2.9 M.

#2618's test of the loops, two walks of each loop's body from its header (one
stopped by the takes, one by the calls, `ir_typed_loop_every` and
`ir_typed_loop_quiet`), from a budget of sixteen visits per edge past which a
definition stays lifted, leaves the takes their own in the losers that #2593
left. By callgrind at main `26f0bb0e`, with the allocator's change of #2618
before it ([the allocation guide](tier1-allocation.md)'s "Recipes give way to
hinted values"), speedtest1 runs 2,993.97 M instructions instead of 2,996.70 M
(main 2,997.70 M, no hoist 3,001.58 M), and `luaV_execute` the same:
`sqlite3_str_vappendf` −1.08 M against main, `sqlite3GenerateColumnNames`
−0.27 M, `patternCompare` −0.26 M, `sqlite3StrICmp` −0.23 M, `sqlite3Atoi64`
−0.09 M, each now as without the hoist, and `jsonTranslateTextToBlob`
−1.06 M. The rules measured on the way, as speedtest1's instructions against
this one's:

| Rule | speedtest1 | `luaV_execute` |
| --- | ---: | ---: |
| every iteration of the outermost loop alone (no calls' test) | +0.64 M | 0 |
| the calls' test on the outermost loop alone, not its nest | +0.03 M | +36.2 M |
| the definition at P, not the takes their own, where the test fails | +3.48 M | 0 |
| outside loops, one definition only where every path from P takes it | +0.17 M | 0 |
| outside loops, the loops' test where not every path from P takes it | +3.45 M | 0 |

Without the "some take is in a loop" rule outside loops, `jsonTranslateBlobToText`
gains 0.89 M and `sqlite3GetToken` loses 1.22 M: its identifier loops, which
call nothing, read `sqlite3CtypeMap` on every character, and the two can't be
told apart from their loops alone. What the hoist still costs is the
allocator's, at most 0.89 M a function: `jsonTranslateBlobToText`'s
`sqlite3CtypeMap`, defined at a switch every call runs and read in a few
cases' loops, takes a callee-saved register its prologue saves; `speedtest1_run`'s
hot `g` is recomputed after each call where its accesses would have folded
rip-relative (+0.27 M); `sqlite3FindFunction` +0.12 M, `hexFunc` +0.07 M.
`test-ir-lowering` checks both lowering paths, the verifiers, the interpreter's
results and promotion for a symbol taken three times, one taken once and one
through its GOT slot, and each of the rules above: a definition past an early
return, two takes of three paths left their own, a definition every path from
a branch takes, one before a loop and two takes on a loop's exits left their
own; and (#2618) takes left their own in a loop whose every iteration calls and
in one where a single iteration takes the symbol, and one definition before a
nest whose call-free inner loop takes it on every iteration.
Backedges to source entry zero skip that initialization. The typed stream's U64
pointer representation becomes explicit `inttoptr` / `ptrtoint` at memory and
address operations; ordinary U64 arithmetic remains integer arithmetic. Integer
comparisons produce `i1`, explicitly extended to the stream's `i32` result.

The typed endpoint becomes a void return for a void function and an `undef`
return for nonvoid fallthrough, whose value the existing stream leaves unspecified.
The caller must diagnose source-language constraints before lowering. The adapter
handles the scalar corpus and the explicit-target SIMD subset described above,
while refusing other `X86` rows and `TAIL_CALL`; it does not
invent missing volatile or prototype information. In particular,
the stream provides argument classes and a variadic bit but no
named-parameter count: this adapter checks all supplied classes as
fixed parameters. A source producer must preserve the effective call-site
prototype and conversions already retained by the checker when constructing
`IrCall` descriptors.

The returned IR is verified before publication. All count, allocation and invalid
input failures preserve the caller's result and reclaim temporary memory. Scalar
and vector classification produce identical columns, side arrays and block ranges.

## Tests and measurements

```sh
./build/compiler test-ir
./build/compiler test-ir --seed 81985529216486895 --count 1024
./build/compiler test-ir-lowering
./build/compiler benchmark-ir
./build/compiler benchmark-ir --only vector --after-scalar 200
./build/compiler benchmark-ir-lowering
./build/compiler benchmark-ir-lowering --only vector --after-scalar 200
./build/compiler test-ir-source [--print NAME] [preprocessor options] file...
./build/compiler benchmark-ir-source [--only VARIANT] [--after-scalar MS] [preprocessor options] file...
```

`test-ir` compares the two use-list twins on its printer fixtures, guarded
tails, every-operand-kind fixture, generated functions and allocation
failures. `test-ir-source` compares them on every lowered and promoted source
form (`uses_forms`, `uses_failed`). `test-ir` also has independently expected CFG/use lists and rejection fixtures,
conversion and constant coverage, loop/diamond/self phis, duplicate targets,
8,192 adversarial i128 switch cases, guarded columns of lengths 0 through 33,
dominance tails, overflow, arena rollback, exact printer text and random
integer DAG mutations. Agreement alone is insufficient: malformed fixtures must
be rejected by both paths. Generated functions of up to 40 blocks, with phis,
selects, stores, addresses with and without an index and returns, are valid but
for where each value is used; both paths must report the first use error, or
none, that an independent oracle gives under no dominator view, a chain of
blocks (some unreachable) and random intervals (`ir_test_dominance_generated`,
#786: four per `--count`). A labels fixture (`ir_test_labels`) checks
`br_indirect`'s successors (three labels, two distinct blocks: two edges), a
label's phi, and the refusals of a label without a block beside an indirect
branch, a label past the blocks, a branch through an integer and a missing label
column. An aggregates fixture (`ir_test_aggregates`) checks a variadic
definition's anchors and a call's ABI records and destination, which its use list
has at position 2, and refuses each break: records past their tables, a result
without its destination or into an integer, a destination without a result, an
aggregate argument or parameter that isn't a `ptr`, an aggregate result without
the function's records, anchors outside a variadic definition, of other sizes,
typed, not address-taken, two of one, both at once and aligned below 16.
`tests/ir/labels.ir` and `tests/ir/aggregates.ir` are their canonical text, which
`test-ir-parse` reads, prints back byte for byte and requires of every opcode.
`tests/ir/optimize/` holds [#139](https://github.com/buster14a/buster/issues/139)'s
before/after pairs in the same text form, which `test-ir-optimize` parses with
both parser paths, runs through both optimizer twins, reprints byte for byte
and executes on both sides: [#175](https://github.com/buster14a/buster/issues/175)'s
last acceptance item ([the tools guide](ir-tools.md#pass-fixtures-in-the-text-form)).

`test-ir-lowering` lowers generated typed programs through both paths, verifies
every function, compares their columns, and executes the resulting IR in a
test-only explicit-frame executor against the existing typed-stream interpreter.
Calls and global-memory effects are compared, with targeted fixtures for values
across conditional joins, backedges to entry zero, narrow shifts and wide pointer
scales. This is adapter testing, not the production interpreter of #176.

It generates sets of programs from the same seeds: values only, which give
the same streams as before objects existed; with the generator's `objects`
option (local arrays and structs, pointers into them, copies between twins,
some aligned to 16); with calls through pointers to functions (`indirect`); and
with the catalog's records passed and returned by value (`records`: structs and
unions in registers, on the stack and through hidden pointers), whose IR the
executor runs as the ABI records say: a callee's own copy of each aggregate
argument, a hidden pointer's destination as parameter 0, and a result's bytes
copied to the call's destination before the callee's frame goes. Each set must
reach what it is for (indirect calls; calls with records, aggregate parameters and
both kinds of result). Each program
is also compiled by Tier 0 and run natively in a child process
(`test-tier0`'s harness), and its result and global bytes must equal the IR
executor's, besides the typed interpreter's. The records' set runs again without
floating values (whose out-of-range conversions the shared interpreter refuses, as
C leaves them undefined) through the shared IR's interpreter (#788), its calls with
records as [the tools guide](ir-tools.md)'s "Aggregates by value and variadic
definitions" has them, with undefined-byte checks off: its `COPY` entries carry
structs' padding, which nothing reads. And `test-tier0-variadic-frame`'s callees
(`ir_lower_test_variadic_frames`: `va_start`, `va_copy` and `va_arg` over general,
vector and overflow arguments) are lowered, run natively from clang's caller and by
the shared interpreter from an IR caller of the same arguments, and every output that
isn't an address must be the native run's. The executor bounds every access
by its slot, so a piece past an object's end faults. Two fixtures target the
new inputs: two objects, of 23 bytes (aligned to 8) and 24 (aligned to 16),
the first written in pieces of every width and copied into the second, whose
24th byte the copy must not reach, then the second copied over itself, the
slots' sizes, alignments and flags checked; and `COPY` of every size from 1 to
256 between two globals and from one onto itself, whose bytes must be exactly
the copy's, with one load and one store per piece. A copy whose pieces were one
size too wide, and an object's slot rounded down to 8 bytes, each fail the
command (checked by mutation when this was added).

Every lowered function of both sets is then promoted by both of `ssa_ir.c`'s
paths, which must accept it with real dominance checking, pass both verifiers,
and keep each object's slot (a `void` slot that isn't a temporary) with its
size, alignment and flags, in order: promotion compacts the slots it removes
and must never take an object's. The two paths may order their output
differently, so their columns aren't compared here; `test-ssa-ir` checks their
semantics by execution. A promotion that shrank an object's slot by a byte
fails this check.

`ir_lower_test_wide` runs Tier 0's own random programs of long double and of the
128-bit integers (`tier0_x87_test.c`, `tier0_int128_test.c`, 128 of each by
default) through both lowerings and promotion, then through the shared IR's
interpreter (#176) on random inputs, every output slot compared with their stack
machines': the x87 programs' calls of C functions through their addresses (as the
interpreter's natives), NaNs alike, and the wide integers' arithmetic, shifts,
compares and conversions. Fixtures check a computed goto through a `?:` of two
labels' addresses (executed, and promoted with both labels kept), a variadic
definition whose result is its register save area's first unnamed argument (its
anchors' sizes, alignments and flags, its frame, five breaks of the roles' rules
refused), a function that takes, passes and returns a struct by value (its
own and its call's records, exactly the stream's), and two computed gotos, which
must branch to the one dispatch block, whose promoted address is a phi of their
two (the promoted function run by the shared IR's interpreter, which has phis). Each of these was checked by
mutation when added: an `__int128` constant's lost sign extension, `I128` taken
as unsigned, a long double constant's halves swapped, a parameter's copy a byte
short, a call's result room a byte short, the labels in the wrong order, an
indirect call through its first argument and a register save area of 168 bytes
each fail the command.
The current generator can still produce out-of-range floating-to-integer
conversions with its `defined` option. For those undefined results only, the
test executor chooses the existing Tier 0 interpreter's CVTT convention; its
host casts are range-checked. Representable conversions retain the IR operation's
signed or unsigned semantics, and F32 arithmetic/conversion rounds at F32 precision.

`benchmark-ir` measures the complete verifier and derived-array entry points on
32-instruction and larger synthetic integer/control-flow functions. Setup,
independent accounting and calibration happen outside retained samples. Rebuild
timings include allocation, clearing and filling. Each sample lasts at least
2 ms, variants rotate order, and the default is 21 passes. `--only` isolates a
path; `--after-scalar` conditions the vector path with lighter scalar verification.
It reports ns/function, ns/instruction, useful instructions/s, TSC ticks and
actual core/CFG/use arena charges. TSC ticks are not core cycles.

`benchmark-ir-lowering` measures the available producer, handoff and consumer
together: input validation, planning, classification, count/emission and output
verification. Its units are typed entries, not source tokens, and its inputs
are generated programs, not SQLite. Its small and larger cases are value-only
programs, as before #136's frame objects, so they stay comparable with earlier
runs; a third case, reported apart, is the larger shape with the generator's
objects and copies. Target conclusions require the optimized
repository build on the approved 9700X, pinned to one CPU and under the shared
lock; cloud correctness runs do not establish Zen 5 throughput.

### Bytes per source token

#15 asks for the IR's bytes per source token on `sqlite3.c`, whose largest
function, `sqlite3VdbeExec`, is 5.0% of its `-O0` code. `benchmark-ir-source`
on the frozen `sqlite3.c`, with the 9700X job's options (benchmark-backend-source's),
counts each function's IR as an arena of its own (`IrFunction.arena_bytes`: every
column and side table, each aligned to 64 bytes, with the gaps), against the
705,670 tokens that reach the parser:

| IR | Instructions | Bytes | Per token | Per typed entry |
| --- | ---: | ---: | ---: | ---: |
| Lowered (all in memory) | 598,568 | 13,986,379 | 19.82 | 35.24 |
| Promoted (`ssa_ir_promote`) | 493,934 | 12,227,119 | 17.33 | 30.81 |

The lowered IR's 19.82 bytes per token are 13.57 of instruction columns (16 bytes
per instruction), 4.64 of block offsets and side tables (constants, slots,
addresses, calls, their extra words and types, labels and the ABI records) and
1.60 of the columns' 64-byte alignment gaps, which small functions pay for most.
Each constant takes an instruction and a 16-byte `IrConstant` of its own: the
adapter shares none.

`sqlite3VdbeExec` is the largest function: 571,792 bytes, 4.09% of the IR's
(26,802 instructions, 4.48% of them), from 18,937 typed entries. The ten largest
are 14.00% (#15: 16.8% of the `-O0` code), the next being `yy_reduce` (2.68%) and
`sqlite3Pragma` (1.65%). One function's IR is thus at most about 0.6 MB of a
lane's arena at a time (#151). c-testsuite's subset comes to 2.63 bytes per token,
its tokens mostly its headers'.

### `sqlite3.c`'s functions on the 9700X

`benchmark-ir-source` on the frozen `sqlite3.c`, on CPU 2 of the 9700X under the
shared lock, with the 9700X job's build (`BUSTER_DEBUG_INFO=0`) of `dc8adc81`
([the record](https://github.com/buster14a/buster-performance/tree/5120896936665615bade0b20d72e3a1f4319819d/docs/measurements/ir-source-131-2026-10-28)):
21-pass medians per typed entry over every function.

| Variant | ns | Cycles | Instructions | IPC |
| --- | ---: | ---: | ---: | ---: |
| `lower` (`ir_lower_typed`) | 46.86 | 259.67 | 569.59 | 2.19 |
| `lower-scalar` | 49.64 | 274.93 | 687.01 | 2.50 |
| `verify` (`ir_verify`) | 23.45 | 129.77 | 330.79 | 2.55 |
| `verify-scalar` | 24.06 | 133.25 | 429.03 | 3.22 |
| `promote` (`ssa_ir_promote`) | 159.40 | 882.69 | 2,535.19 | 2.87 |
| `promote-scalar` | 150.85 | 835.80 | 2,285.41 | 2.73 |

Lowering the unit's 396,872 typed entries thus takes about 19 ms, 26 ns per source
token, and after 200 ms of scalar work before each sample `lower` takes 46.92 ns
per entry, the same. The vector path is 6% faster than its twin here: the
emission, which both share, is most of the work. Promotion is #134's: its vector
path is 6% slower than its scalar twin on these functions, and runs under Zen 5's
power cap (#199), 31.2 dispatch slots per instruction throttled against the twin's
13.9. Since #787 a function of more than 16,384 (block, slot) cells takes the
scalar constructor (`BUSTER_SSA_IR_VECTOR_CELLS`, docs/ssa.md): on the 9700X
`promote` then takes 148.6 ns per entry against 160.5 (−7.4%, and −7.7% after
scalar work), under its twin's 151.1, with 2,208 instructions per entry against
2,535 and 13.4 throttled dispatch slots per instruction against 31.5 ([the
record](https://github.com/buster14a/buster-performance/tree/11e845e033dc696b47969899e6837c1ed2e6218c/docs/measurements/ssa-vector-cutover-787-2026-10-29)).
#134's later measurement of both promotions, with each path's phis, is in [the SSA
guide](ssa.md)'s "Benchmark". Since #866 `benchmark-ir-source` also runs both
of GVN's paths once on every prepared function, requires both to accept it, and
times `gvn` and `gvn-scalar` on the same lowered IR; [the value-numbering
guide](ir-value-numbering.md)'s performance acceptance has those medians and the
work counters.

Against main (`9bb5060e`), in the same record: `-c` of `sqlite3.c` runs the same
instructions but 0.0004% and writes the same object. The checker's unit compiled
1.14 s longer (25.6 s against 24.4 s), since `test-ir-source` and
`benchmark-ir-source` ran there with the IR's modules: a command that runs the
checker's emission and the adapter, whose validation reads the x86 encoder's
templates as the emission's encoding does, had to run both in one unit's copy
(`./build.sh check-call-graph`'s split state). Since #784 they run in the
backend's unit, which compiles the IR's modules anyway: the checker's unit homes
their check and emission (`ir_source_prepare`, `BUSTER_IR_SOURCE_ENTRY`), and
`main.c`'s units share the encoder's templates (`BUSTER_X86_SHARED`, the driver's
unit's), which the emission's encoding fills and the adapter's validation reads.
On the 9700X ([the
record](https://github.com/buster14a/buster-performance/tree/9dccbab8b026945783ce996eb7c66f2c32a3ef0b/docs/measurements/ir-backend-unit-784-2026-10-29))
the checker's unit then compiles in 25.57 s against main's 26.71 s, and `-c` of
`sqlite3.c` runs about 6,500 more instructions (0.0005%) and writes the same
object, its time within the floors over six placements. The IR's synthetic
benchmarks read the verifier 1% to 7% slower on the 9700X and 2% to 7% faster on a
Zen 4 laptop: its vector path runs 1.1% fewer instructions than main's, with 13.6%
more of its dispatch slots left empty by the front end. The lowering of the
generated programs does 1.8% more work, the new kinds' and records' tests, and
takes 3% to 8% longer there (within 1% on the laptop's larger programs).

### The verifier, the CFG builder and the optimizer over placements (#786)

#786 asked that the verifier's, the CFG builder's and the optimizer's hot loops run
alike over the code's placements on the 9700X, and that `benchmark-ir` and
`benchmark-ir-optimize` be at or below #774's base, `9bb5060e`. Their data-parallel
graph check, the type windows and the optimizer's single verification per
preparation, with AUTO verifying by the vector twin, measured over six placements
of the backend's unit and three rounds against `9bb5060e` and main `ec5f0f6b`
([the record](https://github.com/buster14a/buster-performance/tree/b7f697e78b47b392a4108be388a22bc126c69374/docs/measurements/ir-graph-786-2026-11-06)):

| Row (median over placements) | `9bb5060e` | Main | #786 | Spread over placements, #786 |
| --- | ---: | ---: | ---: | ---: |
| Vector verifier, 32 instructions (ns/instruction) | 5.866 | 5.289 | 1.804 | 2.4% |
| Vector verifier, 262,144 instructions | 5.604 | 5.101 | 1.269 | 1.1% |
| Scalar verifier, 32 instructions | 9.445 | 9.332 | 9.225 | 2.4% |
| Scalar verifier, 262,144 instructions | 8.790 | 8.582 | 8.108 | 1.4% |
| CFG, 32 instructions | 1.683 | 1.500 | 1.488 | 2.2% |
| CFG, 262,144 instructions | 1.357 | 1.480 | 1.080 | 1.6% |
| Uses, 262,144 instructions | 12.529 | 11.680 | 7.726 | 1.0% |
| Optimizer, phi-diamond, scalar (ns/input instruction) | 150.916 | 168.841 | 133.337 | 1.8% |
| Optimizer, loop, scalar | 133.305 | 146.459 | 116.136 | 2.2% |
| Optimizer, jump-chain, scalar | 60.731 | 63.488 | 49.005 | 0.6% |
| Optimizer, small-dag, AUTO | – | 59.444 | 39.368 | 3.1% |

Every row of both benchmarks is below `9bb5060e`'s at its median, and in 106 of 108
pairs at the same placement (the 32-instruction scalar verifier at two, +0.1% and
+0.5%); the verifier's and the CFG builder's spreads over placements are 1.1% to 2.4%,
against `9bb5060e`'s 3.8% to 25.2%. The vector verifier's front end leaves 0.09% of its
dispatch slots empty, against main's 12.4%, with 76 taken branches per 1,000
instructions against 108. On the frozen `sqlite3.c`'s functions `benchmark-ir-source`'s
`verify` takes 11.59 ns per typed entry against main's 21.14, with 181 instructions
against 331, and `promote`, whose result it verifies, 127.22 against 142.16.

### Initial 9700X qualification

[Protected run 39934441798](https://github.com/buster14a/buster/actions/runs/39934441798)
passed the production build, complete tests and benchmarks for head
`7a6ed05745a6b522209005d2423103caedb6c99a` against main `635434f`, with
Clang 23.1.1 on the Ryzen 7 9700X. The workflow held the shared host lock and
pinned each benchmark to one allowed CPU. Each row below is a 21-pass median;
the after-scalar command runs 200 ms of untimed scalar work before every vector
sample in a separate process.

| Workload | Unit | Scalar | Vector | Vector after scalar |
| --- | --- | ---: | ---: | ---: |
| Core verifier, 32 instructions | ns/IR instruction | 9.029 | 5.718 | 5.744 |
| Core verifier, 262,144 instructions | ns/IR instruction | 8.726 | 5.539 | 5.550 |
| Lowering, 4 functions / 136 typed entries | ns/typed entry | 32.084 | 28.151 | 28.260 |
| Lowering, 64 functions / 20,617 typed entries | ns/typed entry | 84.115 | 75.861 | 76.660 |

The large core fixture used 4,227,137 bytes for its function (16.125 bytes per
instruction including side metadata and arena alignment), 1,179,772 bytes for
the CFG and 4,653,116 bytes for uses. The larger lowering corpus produced 47,783
IR instructions and charged 944,733 core bytes in total (45.823 per typed entry),
with a maximum function allocation of 43,779 bytes.

These are observations from one controlled CI run on generated inputs. They do
not establish source-corpus throughput, multi-process reproducibility or SQLite
bytes per source token. This recorded revision precedes the entry-phi rejection
regression; its timings are not relabeled as measurements of later revisions.
