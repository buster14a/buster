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
no target features. `FRAME_ADDRESS` still refuses with `IR_ERROR_OPCODE`.
The stable printer's text is read back by `ir_parse.c` (#175).

`test-ir-source --simd -march=x86-64-v4 tests/ir/vector-i32.c` checks actual C
producers, both lowering paths and both text parsers. It explicitly requires
transactional refusal by the still-scalar SSA promoter, not successful
promotion; ordinary corpus mode continues to require promotion. The shared
interpreter executes vector/mask operations with sparse wide payloads while
retaining the 16-byte scalar value API. First-class vector entry values and
vector/mask calls remain unsupported. Native selection and opmask allocation
are separate consumers: [#141](https://github.com/buster14a/buster/issues/141)
and [#179](https://github.com/buster14a/buster/issues/179), respectively; see the
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
| Shifts | `a` matches the result; `b` has an integer type |
| `icmp`, `fcmp` | `a,b` are equally typed; `c` is `IrPredicate`; result is `i1` |
| `select` | `a:i1`, `b,c` match the result type |
| `stack_addr` | `a` indexes `slots`; result is `ptr` |
| `symbol_addr` | `a` is a link `SymbolId`; result is `ptr` |
| `address` | `a:ptr`, optional `b:i64`, `c` indexes `addresses` |
| `load` | `a:ptr`; result is the loaded type |
| `store` | `a:ptr`, `b` is the stored value; result is `void` |
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
aggregate by value has ABI records (`IrCall.abi`, below); the typed stream loses
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
counts and fills `IrUses`. `offsets` has `instruction_count + 1` entries;
`uses` contains `(instruction, operand)` pairs. Positions 0/1/2 denote a/b/c,
and 3 plus the argument/incoming index denotes a call or phi side operand.
Each slice is stable in instruction/operand order, and repeated operands produce
repeated uses. Void-result instructions have empty slices. Block IDs, symbols,
slots, constants and side-table offsets never count as value uses. Derived counts
are accumulated in u64 and checked before becoming u32 offsets.

## Verification

`ir_verify` checks common instruction rules 16 rows at a time. Byte-permute tables
classify operand roles and semantic rules; type-set membership, operand/result
agreement and terminator masks are vector comparisons. Indexed type reads use
bounded scalar byte loads into the batch, avoiding hardware gathers and accidental
four-byte reads from byte columns. Constants, calls, phis, switches and conversions
use exact scalar handling; a failing vector batch also uses the scalar rule to
produce the same diagnostic. `ir_verify_scalar` checks one instruction at a time.
Both return the same `IrCheck` fields, including the first error location.

The graph phase checks phi coverage/types, duplicate switch constants and use
ordering. Duplicate switch detection uses stable byte-radix sorting with at most
16 passes, so adversarial i128 keys cannot cause a quadratic hash-probe scan.

An optional `IrDominance` view supplies trusted half-open dominator-tree intervals.
Both endpoints are zero for an unreachable block. The verifier checks use order
within a block regardless of this view. With it, cross-block definitions must
enclose the use block's interval; cross-block uses involving unreachable blocks
are rejected. Phi uses use their predecessor's position and interval. The vector
path compares batches of 16 uses; the scalar twin performs the same comparisons
one at a time. `dominance_checked` distinguishes complete interval checking from
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
operations below. #148's `FRAME_ADDRESS` (`__builtin_frame_address(0)`) is
refused, as `X86` is (`IR_ERROR_OPCODE`, below). Typed validation still requires class U64 and `value == 0` for each, and a
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
`VECTOR` values are refused with `IR_ERROR_TYPE`, and `X86` rows and
`FRAME_ADDRESS` with `IR_ERROR_OPCODE`, at their entry, after validation and
before anything is allocated: no function of the corpora has one. A static
assertion pins the typed kinds' count, `X86` the last, so that a new kind fails to
build until this adapter lowers or refuses it, since the plan and the emitter
would take an unnamed kind for a binary operation.

The ordered replay is written once in `ir_lower_emit.c` and instantiated for
counting and filling. Counting fixes its output to null; filling receives the
non-null local function after `ir_create` succeeds. Constructor helpers stay
inline so count mode can erase column writes and fill mode can erase null checks
before general inlining decisions. Both scalar/vector paths use the same two
instantiations, and all existing allocation, validation and rollback gates remain.

### Dynamic stack contract

| Operation | Result | Operand a | Meaning |
| --- | --- | --- | --- |
| `alloca` | ptr | i64 unsigned byte count | Reserve `(size + 15) & ~15` bytes, returning a 16-byte-aligned address. |
| `stack_save` | i64 | absent | Capture this invocation's current dynamic-stack position as opaque bits. |
| `stack_restore` | void | i64 saved position | Release allocations after that position in this invocation. |

All other operands are `BUSTER_IR_NONE`; flags and alignment columns are zero.
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
Mixed representation-copy execution has an independent consumer gap: the older
COPY expansion uses ordinary integer loads, while the production interpreter
diagnoses an undefined padding byte in such a load. A byte-representation
transfer can legally copy padding without observing its value. That contract
needs a separate repair; disabling undefined checks here would conceal real
lifetime errors. The existing generator FP-to-integer/CVTT oracle boundary is
also excluded from the strict dynamic case. Neither gap is claimed resolved.

The adapter finds block boundaries, compares incoming stack shapes exactly,
classifies 16 entries at once (one at a time in the twin), counts output, then
emits into final-sized columns. Stack replay and irregular calls/control flow
are shared ordered work. Locals begin in typed slots. Only expression-stack
depths live across block boundaries receive temporary slots. Values are spilled
before outgoing edges and reloaded in the receiving block; `JUMP_VALUE` therefore
preserves conditional-expression arms even with an outer expression still live.

A separate preheader materializes slot addresses and initializes parameters.
Backedges to source entry zero skip that initialization. The typed stream's U64
pointer representation becomes explicit `inttoptr` / `ptrtoint` at memory and
address operations; ordinary U64 arithmetic remains integer arithmetic. Integer
comparisons produce `i1`, explicitly extended to the stream's `i32` result.

The typed endpoint becomes a void return for a void function and an `undef`
return for nonvoid fallthrough, whose value the existing stream leaves unspecified.
The caller must diagnose source-language constraints before lowering. The adapter
handles the scalar corpus and the explicit-target SIMD subset described above,
while refusing other `X86` rows and `FRAME_ADDRESS`; it does not invent missing
volatile or prototype information. In particular,
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

`test-ir` has independently expected CFG/use lists and rejection fixtures,
conversion and constant coverage, loop/diamond/self phis, duplicate targets,
8,192 adversarial i128 switch cases, guarded columns of lengths 0 through 33,
dominance tails, overflow, arena rollback, exact printer text and random
integer DAG mutations. Agreement alone is insufficient: malformed fixtures must
be rejected by both paths. A labels fixture (`ir_test_labels`) checks
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
guide](ssa.md)'s "Benchmark".

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
