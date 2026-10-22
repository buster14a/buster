# The System V calling convention

`src/buster/compiler/calling_convention.c` is
[#137](https://github.com/buster14a/buster/issues/137)'s System V AMD64
calling convention: where each argument and result of a call, or of a
function's entry, is passed, as data for Tier 0's code generator
([#136](https://github.com/buster14a/buster/issues/136)), which emits every
instruction. It is contract 3 of
[the coordination comment on #135](https://github.com/buster14a/buster/issues/135#issuecomment-6293200394),
and it builds on contract 5, [#132](https://github.com/buster14a/buster/issues/132)'s
type table (`c_types.c`, `docs/c-types.md`): it classifies `TypeId`s and reads
their sizes, alignments and members from the table's columns.

The reference is clang 23 on x86-64 Linux, as
[#154](https://github.com/buster14a/buster/issues/154) decided. clang's
`X86_64ABIInfo` (`clang/lib/CodeGen/Targets/X86.cpp`: `classify`, `merge`,
`postMerge`, `classifyArgumentType`, `classifyReturnType`, `getIndirectResult`,
`getIndirectReturnResult`, `computeInfo` and `EmitVAArg`) decides how each
value is passed, and LLVM's `CC_X86_64_C` and `RetCC_X86_64_C`
(`llvm/lib/Target/X86/X86CallingConv.td`) place what clang leaves to them.
Their sources were read for these rules; none of them is copied. This
document's statements about them were checked against their sources at
`llvmorg-23.1.3` and against clang 23.1.3's output. Where clang and GCC pass a
value differently, the module follows clang: "Where GCC passes otherwise"
lists the cases found so far. `test-calling-convention` checks the module
against a transcription of clang's classifier and against fixtures read from
clang's IR, and links code generated from its locations with C that clang and
GCC compiled (see "Tests").

## What Tier 0 gets

Data, not instructions:

- **Classes, per `TypeId`, in a column.** `abi_initialize` pushes the table's
  columns from an arena, sized for the type table's capacity
  (`abi_capacity_bytes`: 33 bytes a type, and 320 for the columns'
  alignment), and classifies the builtin types; it fails, leaving the arena as
  it was, if the arena can't hold them. `abi_classify_all` then classifies
  every complete type the table has that isn't classified yet. Each type gets
  its row of classes, its pass bits, its stack slot's size, its canonical
  alignment and its result (see "Representation"). `abi_classes(abi, type,
  named)` gives a type's two classes as a named argument or a result sees
  them, or as an unnamed (variadic) argument does.
- **Locations, per call and per function entry.** `abi_assign` takes many
  calls' signatures as columns (`AbiSignatures`: each call's result type, its
  argument count, how many of them are named, whether the callee's type has
  `...`, and every call's argument types one after another) and writes each
  argument's `AbiLocation` and each call's `AbiCall`: the size and alignment
  of its argument area, the general and vector registers its arguments use
  (the hidden pointer's among them) and its result. A function's entry is the
  call of its own type; the callee finds each stack argument 8 bytes further
  from `rsp` than the caller put it, past the return address.
- **Results:** `AbiReturn`, per type in the table and in each `AbiCall`:
  nothing, registers (rax and rdx, xmm0 and xmm1, st0 and st1, or the vector
  registers LLVM splits a vector into), or memory, whose address the caller
  passes in rdi and the callee returns in rax.
- **al.** A variadic call sets al to an upper bound on the vector registers
  its arguments use; `AbiCall.vector_registers` is LLVM's exact count.
- **Variadic functions:** the register save area's layout, `va_list`
  (`AbiVaList`), where `va_start`'s offsets start (`AbiCall`'s register
  counts), and `abi_va_arg`, `va_arg`'s plan for a type (see "va_arg").
- **The constants:** the argument and result registers, the callee-saved set,
  the red zone, the stack's alignment at a call and the save area's offsets
  (see "Registers and constants").

#136 emits the call sequences, prologues, epilogues and `va_arg`'s code from
these. Before it assigns a call it classifies its types and refuses two kinds
of type the module can't place: `UNSUPPORTED` ones (see "Pass bits") and ones
of 4 GB or more, whose stack size is `UINT32_MAX`. The table is the code
generator's: it classifies after the front end has made the types, and a tag
once it is complete (the type table's columns never change once written, but
a tag's at its completion). Lanes may read the table while none writes it.

`vector_bytes`, which `abi_initialize` takes, is the widest vector register
the generated code may use: 16, 32 or 64 (no AVX, AVX, AVX-512F), clang's
`AVXLevel` (`getNativeVectorSizeForAVXABI`). It decides how vectors of 32 and
64 bytes pass, and it belongs to the generated code's ISA, not the host's.

`AbiSignatures.variadic` is a call's own: LLVM's assignment depends on whether
the callee's type has `...` even where the call passes nothing past the named
arguments. In C17, clang calls a function without a prototype as a variadic
one with every argument named, and sets al, unless an argument passes in a
register wider than 16 bytes (`isNoProtoCallVariadic`; clang 23.1.3 calls
`f(d, 1)` for `void f();` as `void (double, i32, ...)` with al 1, and
`f(v, 2.0)` with a 32-byte vector `v` as a call that isn't variadic).

## Representation

### Classes and rows

The classes are clang's (`X86_64ABIInfo::Class`), in its order: `INTEGER` 0,
`SSE`, `SSEUP`, `X87`, `X87UP`, `COMPLEX_X87`, `NO_CLASS` and `MEMORY` 7. A
type's two classes, its low eightbyte's and its high one's, are an
`AbiClasses` byte: the low one in bits 0 to 2 and the high one in bits 4 to 6.

clang classifies a member at its offset within the outermost aggregate: the
offset decides which eightbyte gets its class and whether it straddles both.
So each type is classified once into a **row**: its classes at each byte
offset 0 to 15 of an enclosing aggregate of at most 16 bytes (row[0] is the
type's own). Sixteen offsets are every place where clang's recursion can ask
for a type's classes and use them:

- an aggregate passed in registers is at most 16 bytes, or larger and SSE,
  SSEUP: a struct whose named members are each as large as it is, a union, or
  an array of one element, all with their members at offset 0;
- every other aggregate larger than 16 bytes is MEMORY whatever its members'
  classes, or, if it is an array, NO_CLASS without a look at its elements
  (see below), so a type's classes at an offset past 0 where it would end
  beyond 16 bytes change nothing: its row holds MEMORY there
  (`BUSTER_ABI_IN_MEMORY`);
- a member of no size can also be at 16, the end of a 16-byte aggregate,
  where it has its classes at 0 (the row is read modulo 16), unless it is
  `END_MEMORY` (see "Records and arrays: the twin").

A record's or an array's row is then a merge of its members' rows, each
shifted by its member's offset: n members cost n merges of a row whatever the
nesting, since a nested record's row is already folded.

MEMORY is canonical: a pair with MEMORY in either half is MEMORY in both
(0x77). clang passes an argument whose low class is MEMORY in memory whatever
its high one, and an aggregate with a member that has MEMORY in either half
stops merging there and ends with MEMORY in its low class after `postMerge`;
so one byte value means "in memory" everywhere. clang's `merge` asserts that
the accumulated class isn't MEMORY, since its loops stop at it; the module's
`abi_merge_class` makes MEMORY absorb on either side instead, as the vector
fold needs, since it doesn't stop. `abi_post_merge` is clang's `postMerge` as
Linux has it, with revision 0.98's rule that an `X87UP` not preceded by `X87`
is MEMORY (`honorsRevision0_98` holds everywhere but on Darwin).

### Pass bits

`AbiTable.pass` says how a classified type is passed, from its classes and
what it is (`abi_finish`):

| Bits | Name | Meaning |
| --- | --- | --- |
| 0 to 3 | `LOW_INTEGER`, `LOW_SSE`, `HIGH_INTEGER`, `HIGH_SSE` | the register class of each eightbyte passed in a register; a high eightbyte without a low one (padding) passes alone |
| 4 | `VECTOR` | SSE then SSEUP: one vector register holds the value |
| 5 | `WIDE` | a `VECTOR` of 32 or 64 bytes |
| 6 | `MEMORY` | on the stack as an argument: the classes MEMORY, X87 and COMPLEX_X87 |
| 7 | `UNCOUNTED` | MEMORY by its class, but a scalar clang passes as its value, in a register clang doesn't count, or on the stack once LLVM has none: `nullptr_t` and `_Atomic` scalars |
| 8 | `IGNORE` | nothing to pass: both classes NO_CLASS (an empty struct, a struct of unnamed bit-fields) |
| 9 | `PAIR` | one 128-bit integer value (clang's `i128`), which LLVM never splits between a register and the stack |
| 10, 11 | `SIGN`, `ZERO` | clang's `Extend`: the caller extends the value to 32 bits (the callee, a result) |
| 12 | `AGGREGATE` | clang's `byval`: in memory as an argument, a copy on the stack |
| 13 | `END_MEMORY` | a type of no size that is MEMORY at offset 16 though not at 0 |
| 14 | `UNSUPPORTED` | an `UNCOUNTED` vector wider than 16 bytes and than the target's vector registers, which LLVM would split across several registers as no class says: the code generator refuses it |
| 15 | `CLASSIFIED` | the row and these bits are in the table |
| 16 to 18 | `VALUE` | the kind of value a scalar in memory passes as: a general register (`GENERAL`), two (`PAIR`), a vector register (`VECTOR`), or x87's (`X87`), which always goes on the stack, so that such a type isn't `UNCOUNTED` |
| 20 to 25 | `ALIGNMENT` | the log2 of the stack slot's alignment: 3, or the type's own if larger (an aggregate's own, a scalar's value's natural one) |

Beside the pass bits, per type: `stack_bytes`, the slot's size (the type's
size rounded up to 8; 8 for a type of no size, as LLVM gives a `byval` of no
size 8 bytes; `UINT32_MAX` for 4 GB or more), `alignment_log2`, its canonical
alignment (an aligned typedef looked through, as clang's canonical types have
no typedefs, and an array's element's), and `results`, its `AbiReturn`. An
aligned typedef (`ALIGNED`) passes as its base: `abi_copy` gives it its base's
row, bits, slot and result.

### Results, locations and calls

`AbiReturn`, 8 bytes:

- **kind:** `IGNORE` (void, or nothing to return), `REGISTERS` or `MEMORY`
  (the caller passes the result's address in rdi, and the callee returns it in
  rax);
- **registers[4]**, in order, each holding `part_bytes` of the value: an
  eightbyte, what a vector register holds of it, or 16 bytes for an x87
  register's 80-bit value;
- **flags:** `SIGN` or `ZERO` (the callee extends to 32 bits), and `HIGH`
  (the value's low eightbyte is padding: the register holds its high one).

`AbiLocation`, 8 bytes: `stack_offset` (from rsp at the call), `registers[2]`,
`kind` and `flags`. The kinds:

- **`IGNORE`:** nothing is passed.
- **`REGISTERS`:** `registers[0]` holds the first part and `registers[1]` the
  second, or none: an eightbyte each, one vector register for a `VECTOR`, two
  general registers for a `PAIR`.
- **`STACK`:** the whole value at `stack_offset`. With `HIGH`, the value's
  high eightbyte alone, in an 8-byte slot.
- **`SPLIT`:** one part in its register and the other, `NONE` in its place, in
  the 8-byte slot at `stack_offset`. In the module only an `UNCOUNTED`
  argument before it makes one (see "Assignment").

The flags are `SIGN` and `ZERO` (the caller extends the value to 32 bits) and
`HIGH` (the parts are the value's high eightbyte alone).

`AbiCall`, 16 bytes: `stack_bytes` (the argument area's size, a multiple of
8, whose start is rsp at the call), `stack_alignment_log2` (4, or more when an
argument is aligned more), `integer_registers` and `vector_registers` (the
registers the arguments use, the hidden pointer among the first), and the
result's `AbiReturn`.

### Registers and constants

Registers are numbers as the encoder has them: a general register's
`X86Register` (RAX 0 to R15 15), a vector register's number plus
`BUSTER_ABI_REGISTER_VECTOR` (0x20; XMM, YMM or ZMM by the value's size), an
x87 register's plus `BUSTER_ABI_REGISTER_X87` (0x40, ST0 0), and
`BUSTER_ABI_REGISTER_NONE` (0xff). The constants:

| Constant | Value |
| --- | --- |
| `abi_integer_arguments` (`BUSTER_ABI_INTEGER_ARGUMENTS`) | rdi, rsi, rdx, rcx, r8, r9 |
| `BUSTER_ABI_VECTOR_ARGUMENTS` | 8: xmm0 to xmm7 |
| `abi_integer_results` (`BUSTER_ABI_INTEGER_RESULTS`) | rax, rdx (and xmm0, xmm1, st0 and st1 by the result's classes) |
| `BUSTER_ABI_CALLEE_SAVED` | a bit per general register: rbx, rbp, r12 to r15 (also preserved: the x87 control word and MXCSR's control bits) |
| `BUSTER_ABI_RED_ZONE_BYTES` | 128, below rsp, for a leaf function |
| `BUSTER_ABI_STACK_ALIGNMENT_LOG2` | 4: rsp at a call is 16-byte aligned, or more for an argument aligned more |
| `BUSTER_ABI_SLOT_BYTES` | 8: a stack argument's size rounds up to it, and its slot is aligned to it or to its own alignment if larger |
| `BUSTER_ABI_VARIADIC_COUNT_REGISTER` | rax: al |
| `BUSTER_ABI_REGISTER_BYTES`, `BUSTER_ABI_MAX_CLASSIFIED_BYTES` | 16 and 64: the largest aggregate passed in registers but as one vector, and the largest clang classifies (Rule 1) |

### The register save area and va_list

A variadic function's prologue fills a 176-byte register save area
(`BUSTER_ABI_SAVE_AREA_BYTES`): rdi to r9 at 0 to 40, then xmm0 to xmm7 in
16-byte slots from 48 (`BUSTER_ABI_SAVE_AREA_VECTOR_OFFSET` and
`_VECTOR_SLOT_BYTES`), the vector ones only when al isn't 0. `va_list` is a
one-element array of `AbiVaList`, the psABI's 24 bytes (`gp_offset`,
`fp_offset`, `overflow_arg_area` and `reg_save_area`; the type table's
`type_builtin_va_list` makes the same type for the front end). `va_start` sets
`gp_offset` to 8 times the entry's `integer_registers`, `fp_offset` to 48 plus
16 times its `vector_registers`, `overflow_arg_area` to the first stack
argument past the named ones and `reg_save_area` to the save area.

## Classification

### Simple types

Everything but records and arrays is a simple type (`abi_simple`), whose
classes at an offset follow from what it is, by one of two rules: `FIXED`
gives both classes wherever the type is (such types are 16 bytes or more, so
only offset 0 counts, or MEMORY), and `CURRENT` gives its class to the
eightbyte the offset falls in, and to the high one too when the type crosses
into it (offset + span > 8). The span is 1 for a scalar, which clang never
splits; the element's size plus 1 for a complex type, whose imaginary part's
start decides; and the size for a vector, whose last byte does.

| Type | Classes |
| --- | --- |
| `void` | NO_CLASS, NO_CLASS |
| the integer types, pointers, enums (as their integer type) and `_BitInt(N)` to 64 bits | INTEGER, current |
| `__int128` and `_BitInt(N)` of 65 to 128 bits | INTEGER, INTEGER |
| `_BitInt(N)` past 128 bits | MEMORY |
| `float`, `double`, `_Float16` and `__bf16` | SSE, current |
| `__float128` | SSE, SSEUP |
| `long double` | X87, X87UP |
| `__fp16`, `nullptr_t` and every `_Atomic` type | MEMORY |
| `_Complex` of an integer type | INTEGER, current, to 8 bytes; INTEGER, INTEGER to 16; then MEMORY |
| `_Complex float`, `_Float16` and `__bf16` | SSE, current |
| `_Complex double` | SSE, SSE |
| `_Complex long double` | COMPLEX_X87, COMPLEX_X87 |
| `_Complex __float128` | MEMORY |
| vectors of 1 to 4 bytes | INTEGER, current, whatever their elements |
| vectors of 8 bytes | SSE, current (`<1 x long long>` too); `<1 x double>` MEMORY |
| vectors of 16 bytes | SSE, SSEUP |
| vectors of 32 and 64 bytes | SSE, SSEUP up to `vector_bytes`, but MEMORY with `__int128` elements; MEMORY past it |

As an unnamed argument a vector of 32 or 64 bytes is MEMORY (clang's
`classify` with `isNamedArg` false), which `abi_classes` gives for a `WIDE`
type; the row has the named classes. `abi_simple_entry` gives a simple type's
classes at one offset, and `abi_simple_row` all 16 at once with byte compares
and blends.

### Records and arrays: the twin

The scalar classification, the vector fold's twin and the tests' reference,
takes a record or an array an offset at a time (`abi_record_entry` and
`abi_array_entry`, from the members' rows, which are classified first). A
record at byte offset o:

1. is MEMORY if it can't be there (o isn't 0 and it would end past 16 bytes),
   is larger than 64 bytes or has a flexible array member;
2. otherwise merges its members in order, skipping unnamed bit-fields:
   - in a record larger than 16 bytes, a member that isn't the record's size
     (in a struct; a union's may be smaller) or any member of a record wider
     than `vector_bytes` is MEMORY;
   - a bit-field is INTEGER in each eightbyte it touches: bit-fields straddle,
     and their alignment is never checked;
   - a member that isn't at a multiple of its canonical alignment is MEMORY;
   - any other member has the classes its row gives at o plus its offset;
3. and is post-merged.

An array at byte offset o is MEMORY if it can't be there, is larger than 64
bytes, or o isn't a multiple of its element's alignment as written (an aligned
typedef's included); NO_CLASS, NO_CLASS, unmerged and not post-merged, if it
is larger than 16 bytes but isn't one element the target's vectors hold
(clang's `classify` returns there); and otherwise its elements' rows at o plus
each element's offset, merged and post-merged.

The two alignment checks differ as clang's do: a record checks a member's
offset against its canonical type's alignment
(`getTypeAlign(i->getType().getCanonicalType())`), without an aligned typedef
and, for an array member, its element's, while an array checks its own offset
against its element type as written (`getTypeAlign(AT->getElementType())`).
That gives `END_MEMORY`. Take an array of no size of `T`, a typedef of
`struct {char c[32];}` aligned to 32, at offset 16 of a packed 16-byte struct:
the struct checks it against its canonical type's alignment, the `char`
struct's 1, and the array checks itself against `T`'s, 32, which 16 isn't a
multiple of, so the struct is MEMORY. At offset 0 the array is NO_CLASS, and
the row, read modulo 16, would give it those classes at 16 too.
`abi_member_row_at` reads such a member's classes at 16 as MEMORY instead,
and `abi_end_memory` marks the types it applies to: such arrays, arrays of no
size of elements that are such, and records of no size with such a member or
with a member whose canonical alignment passes 16 bytes.

### The vector fold

`abi_classify` (`abi_classify_vector`) takes a level of types, whose members,
elements and bases are classified, and folds its records and arrays four at a
time in ZMM registers, one type per 128-bit lane, the lane's 16 bytes the
type's 16 offsets:

1. **Groups.** A first pass gives simple types and the records and arrays
   that need no fold their rows (below), and lists the others. They are sorted
   by how many entries they fold (members but unnamed bit-fields, or
   elements), a counting sort over 64 buckets whose last takes every count
   from 63 up, and grouped four at a time, so that a group's lanes end
   together. Each step of a group is a 64-byte block of four entries; a lane
   that ends early, or has no type, merges `NOTHING` (NO_CLASS, NO_CLASS),
   which changes nothing.
2. **Entries.** A scalar pass writes each member's entry into its group's
   blocks: its classes at the enclosing type's 16 offsets, by
   `abi_record_entry`'s and `abi_array_entry`'s rules, 16 offsets at once
   (`abi_member_entry`, `abi_write_entries`). That is the member's row shifted
   by its byte offset with one `vpshufb` (lane o reads offset (o + shift) mod
   16, `abi_shifted_row`), MEMORY where it would be misaligned, by a
   `vptestnmb` of o + shift against its canonical alignment's mask
   (`abi_aligned_lanes`); a bit-field's two classes from the offsets where it
   starts in the low eightbyte and ends in the high one, two byte compares;
   or an element's row shifted by the element's offset. A member that can't
   be in a record larger than 16 bytes is MEMORY at every offset.
3. **Merging** (`abi_fold_groups`): each step is clang's merge in all 64 bytes
   at once (`abi_merge_vector`). The table `merge[accumulated << 3 | field]`,
   64 bytes in the `AbiTable`, is one ZMM register: a `vpternlogd` builds each
   byte's low-eightbyte index from the accumulated class's bits shifted left 3
   and the field's, another its high one's from bits 4 to 6 of both, two
   `vpermb` look them up (they read an index's low 6 bits only), and an OR
   recombines them.
4. **Post-merging** (`abi_post_merge_vector`): clang's four rules by byte
   compares and masks, with a group's offsets that its types can't be at (and
   an array's misaligned ones) forced to MEMORY and, for types larger than 16
   bytes, offset 0 required to be SSE, SSEUP. Each lane's 16 bytes are then
   stored as its type's row, and `abi_finish` finishes each folded type.

Records and arrays whose rows need no fold get them directly: a record larger
than 64 bytes, with a flexible array member, or larger than 16 bytes and than
`vector_bytes` is MEMORY everywhere; an array larger than 64 bytes MEMORY too;
one larger than 16 bytes that isn't a passable vector `NOTHING` at 0 and
MEMORY elsewhere; one of some elements of no size its element's row where it
is aligned (the merge of n copies of one row is that row). Simple types get
`abi_simple_row`. If the scratch arena can't hold the fold, `abi_classify`
classifies the whole level with the twin, `abi_classify_scalar`, which gives
the same table; a type classified already is classified again to the same
result.

### Levels

`abi_classify_all` (and its twin `abi_classify_all_scalar`) lists every type
that is classifiable (a complete object type, or `void`) and not classified
yet, then takes them in levels: the types whose base (an aligned typedef's),
element (an array's) or members (a record's, unless its row doesn't depend on
them: larger than 64 bytes, or with a flexible array member) are classified,
classified together by `abi_classify`, then the next level, until none is
left. Without scratch memory for the lists it classifies a type at a time with
the twin, in passes over the table, until a pass classifies nothing.

### Finishing a type

`abi_finish` derives the rest from a type's row[0] and what it is:

- **the pass bits** of its classes, and `AGGREGATE` for records, arrays,
  complex types, `_Atomic` ones of those and `_BitInt`s
  (`isAggregateTypeForABI`, and `getIndirectResult`'s and
  `getIndirectReturnResult`'s `isBitIntType`), and for clang's illegal
  vectors (`IsIllegalVectorType`: 8 bytes or less, wider than the target's
  vectors, or of `__int128`s), which go in memory as `byval` copies too,
  though they return as their values;
- **`PAIR`** when clang passes two INTEGER eightbytes as one `i128`
  (`abi_is_pair`): `__int128`, `_BitInt(128)` and `_Atomic` ones, and
  aggregates whose storage at offset 0 is one, as clang lays them out in
  LLVM's types (`GetINTEGERTypeAtOffset` returns an `i128` it finds there): a
  struct's first member there, a union's most aligned and then largest member
  (`CGRecordLowering::lowerUnion`), an array's element, and a bit-field wider
  than 64 bits of a 128-bit type. `_BitInt(65)` to `_BitInt(127)` pass as two
  `i64`s, which LLVM can split;
- **`SIGN` or `ZERO`** for `bool`, the character and short types, enums of
  them and `_BitInt`s narrower than `int`, by their signedness (plain `char`
  is signed on x86-64): clang's `isPromotableIntegerTypeForABI`;
- **`UNCOUNTED`** and the value's kind for `nullptr_t` and `_Atomic` scalars
  in memory, and `UNSUPPORTED` for such a vector that LLVM would split;
- **the slot's alignment**: an aggregate's or an illegal vector's own
  alignment, as clang's `byval` takes the larger of it and 8, and a scalar's
  value's natural one (an enum's integer type's, not its aligned attribute's),
  as LLVM's stack slots for its IR types have;
- **the result** (`abi_result`, clang's `classifyReturnType` with LLVM's
  registers): `IGNORE`; `MEMORY` for an aggregate in memory (`sret`); for a
  scalar in memory, the registers LLVM returns its value in
  (`abi_value_result`): rax, rax and rdx for an `i128`, st0, or as many of
  the target's widest vector registers as the value needs, which LLVM splits
  it into (`RetCC_X86Common` has four of each width), and `MEMORY` for a
  vector that would need more than four, or of `__int128`s wider than 16
  bytes, which LLVM returns through a hidden pointer; st0 for X87; st0 and
  st1 for COMPLEX_X87; one vector register for SSE, SSEUP; otherwise an
  eightbyte a register, INTEGER in rax then rdx and SSE in xmm0 then xmm1,
  with `HIGH` when the low one is padding;
- **`END_MEMORY`** for a type of no size, and the canonical alignment.

## Assignment

### Two counts: clang's and LLVM's

clang decides from its count of free registers whether an argument's
eightbytes go to registers (`computeInfo`: all of them if every register they
need is free, otherwise the whole argument to memory, its registers staying
free for the arguments after it); LLVM then gives each value clang passes it
the next register of its kind or, once there is none, a stack slot of its
own. The module has the two counts differ where clang passes a value it
didn't count: `nullptr_t` and `_Atomic` scalars are MEMORY by their classes,
and clang's `getIndirectResult` passes a scalar in memory as its value
("assume LLVM will pass it in the right place naturally", with the FIXME of
PR12193), which `CC_X86_64_C` puts in the next register of its kind. (LLVM
can also end up with a register clang counted: see "Where it doesn't match
clang yet".)

So the twin, `abi_assign_call` (one call; `abi_assign_scalar` runs it over
every call), keeps both counts: clang's free registers, 6 and 8 (5 general
with a hidden pointer), which an `UNCOUNTED` argument doesn't lower, and
LLVM's used ones. For each argument in order:

1. `IGNORE`: nothing.
2. `UNCOUNTED`: its value takes LLVM's next general register, its next two
   for a `PAIR` (consecutive, or none), or its next vector register (in a
   variadic call only up to 16 bytes, as `CCIfNotVarArg` keeps YMM and ZMM
   registers from variadic calls); otherwise a stack slot. clang's counts
   don't change.
3. MEMORY (by its class, or `WIDE` and unnamed), or clang's counts short of
   its needs: a stack slot of its size, aligned to its slot's alignment.
4. A `VECTOR` or a `PAIR`, one value: clang's counts drop; LLVM's next vector
   register, or two consecutive general ones; otherwise the whole value on
   the stack, as `CC_X86_64_I128` never splits an `i128`. A `WIDE` vector in a
   variadic call always goes on the stack, in a slot aligned to its size
   (`CCAssignToStack<32, 32>` and `<64, 64>`), clang having counted a vector
   register for it all the same (the psABI, and GCC, pass a named one in a
   register: LLVM's FIXME on `CCIfNotVarArg`).
5. Otherwise an eightbyte at a time, each a value of its own (clang coerces
   such an argument to `{i64, double}` and the like): clang's counts drop;
   the low eightbyte (unless it is padding) and then the high one each take
   LLVM's next register of its class, or an 8-byte slot. The location is
   `REGISTERS`, `STACK` (both on the stack) or `SPLIT`.

In this model the counts differ only after an `UNCOUNTED` argument: then an
eightbyte clang meant for a register can find none. For `f(long, long, long,
long, nullptr_t, struct {long, long}, long)` the struct is `SPLIT` between r9
and the stack's first slot, and the last `long`, which clang sends to memory,
takes the second slot; for `f(double × 7, _Atomic double, struct {long,
double}, long)` the struct is `SPLIT` between rdi and the stack (xmm7 went to
the `_Atomic double`). Each slot is placed by `abi_stack_place`: aligned up
to its alignment, the area growing past it and its alignment to the slot's.

### The vector path: segmented prefix sums

`abi_assign` takes every call's arguments in order, 16 at a time whatever call
they belong to, after a scalar setup that writes each call's result and
starting totals and each argument's lane flags (first of its call, last,
unnamed, after a hidden pointer, in a variadic call) and call number:

1. **Loads:** a masked load of 16 `TypeId`s, two `vpgatherdd` of their pass
   bits and stack bytes, and the flags.
2. **Needs:** per lane, the general registers it needs in the low 16 bits and
   the vector ones in the high 16 (0 for an argument in memory), plus 1
   general at a call's first lane after a hidden pointer.
3. **A segmented scan** (`abi_scan_add`): four steps of `valignd` by 1, 2, 4
   and 8 lanes and a masked add, each lane adding the partial sum 2^k lanes
   below it only when no call starts after that lane and up to it; then the
   lanes before the vector's first call start add the carry, the previous
   vector's last lane's sum (broadcast with `vpermd`). Inclusive sums less the
   needs give each argument its first register of each class.
4. **Fit:** an argument whose registers all fit (first plus need at most 6
   and 8) gets them, by a `vpermd` from a 16-entry table (rdi to r9, then
   none) for general ones and an OR of 0x20 for vector ones; one that needs
   registers and doesn't fit goes to the stack.
5. **The stack:** the slots of the arguments on the stack in another
   segmented scan, whose exclusive sums are their offsets, and an OR scan of
   their alignments, whose highest bit is the call's.
6. **Stores:** each lane's 8-byte location (its offset, and a word of its
   registers, kind and flags), interleaved with `vpermt2d` and stored with two
   masked 64-bit stores of 8; at the lanes that end a call, its totals: the
   registers used (clamped to 6 and 8), the stack bytes and the alignment.

Without `UNCOUNTED` arguments the twin's two counts are complements, so one
sum serves both; an argument that spills adds its needs to the sums, but only
past a class's limit, where no argument after it finds a register of that
class anyway; and with every slot a multiple of 8 and every offset its
alignment divides, the twin's padding is zero, so the slots' sums are its
placements. That is exactly the twin's assignment unless a lane:

- doesn't fit while a register it needs is still free (a two-eightbyte one
  with one free, or one of both classes with one class full), which the sums
  count as used though it stays free for the arguments after;
- is on the stack at an offset its alignment doesn't divide, where the twin
  would pad;
- is `UNCOUNTED`, whose registers clang doesn't count but LLVM does;
- or is a named `WIDE` vector in a variadic call, the reverse.

A call with such a lane (rare in compiled code) is listed, and after the loop
the twin assigns it again, over what the vector path wrote. Without scratch
memory for the setup's columns, `abi_assign` runs the twin on every call.

## va_arg

`abi_va_arg` is clang's `EmitVAArg` as a plan for the code generator, its
kind a location's, for a type after the default argument promotions:

- **`IGNORE`:** nothing to read, and `va_list` stays as it is (an empty
  struct, which the call didn't pass either).
- **`REGISTERS`:** from the register save area if `gp_offset <= 48 - 8 ×
  integer_registers` and `fp_offset <= 176 - 16 × vector_registers`, which
  then advance by 8 and 16 times those counts; otherwise from the overflow
  area, as for `STACK`. `sources[i]` says where eightbyte i comes from:
  `gp_offset`'s next 8-byte slot (`BUSTER_ABI_VA_GENERAL`), `fp_offset`'s next
  16-byte slot (`BUSTER_ABI_VA_VECTOR`; a `VECTOR` takes one slot for both
  eightbytes) or none; and `HIGH` says that the low eightbyte is padding and
  the register holds the high one.
- **`STACK`:** from `overflow_arg_area`, aligned up to
  2^`overflow_alignment_log2` (8, or the type's alignment if larger), which
  then advances by `stack_bytes`, the type's size rounded up to 8. Types in
  memory, `WIDE` ones among them, go here.

clang's `va_arg` differs from clang's own calls in three places. The module
keeps both sides as clang has them in the first two, and takes GCC's memory
in the third:

- **`nullptr_t`**: the call passes it in a general register (`UNCOUNTED`),
  but its `va_arg`, which sees MEMORY, reads it from the overflow area.
- **A type of no size in memory** (a struct of a flexible array member alone):
  the call gives it an 8-byte slot (the `byval` minimum), its `va_arg` reads
  and skips none.
- **A `WIDE` union of a 16-byte member and a long array** (SSE, SSEUP, since
  the array adds NO_CLASS): as an unnamed argument clang classifies it so,
  counts a vector register, and its call puts it on the stack while its
  `va_arg` reads it from the save area. There is no convention there to follow
  but GCC's, memory, which the module takes for every unnamed `WIDE` type.

## clang's and LLVM's rules, and where they come from

Each rule the module reproduces, with what in clang 23 or LLVM 23 gives it:

- **The merge and post-merge** (the psABI's rules 4 and 5): `merge` and
  `postMerge`, with revision 0.98's `X87UP` rule (`honorsRevision0_98`).
- **Rule 1:** records and arrays larger than 64 bytes are MEMORY (`classify`).
- **Records:** a flexible array member makes a record MEMORY
  (`hasFlexibleArrayMember`); unnamed bit-fields are skipped
  (`isUnnamedBitField`); a bit-field is INTEGER in each eightbyte it touches
  and is never misaligned; a member is checked against its canonical type's
  alignment; a record larger than 16 bytes is MEMORY unless each named member
  is its size (a struct) and it fits the target's vectors.
- **Arrays:** checked against their element's alignment as written; larger
  than 16 bytes and not one element the target's vectors hold, NO_CLASS
  without merging, so that `union {float x8; char c[20];}` is one YMM register
  with AVX; elements of no size all at the array's offset. Hence `END_MEMORY`.
- **MEMORY where `classify` sets no class**, which starts each type at MEMORY:
  `__fp16` (`BuiltinType::Half` isn't among its kinds), `nullptr_t`
  (`NullPtr` neither), every `_Atomic` type (it has no `AtomicType` case),
  `_BitInt`s past 128 bits, `_Complex __float128`, and `<1 x double>` ("gcc
  passes <1 x double> in memory").
- **`UNCOUNTED`:** a scalar in memory is passed as its value
  (`getIndirectResult`'s first case), which `computeInfo` doesn't count:
  `nullptr_t` in a general register, an `_Atomic` scalar as its value type
  passes (a general register, two for an `i128`, a vector register, or, for
  `_Atomic long double`, x87's on the stack, which leaves it plain MEMORY);
  and the `SPLIT` and shifted arguments after them.
- **Vectors:** of 4 bytes or less INTEGER whatever their elements ("gcc
  passes the following as integer"); `<1 x long long>` SSE on Linux
  (`classifyIntegerMMXAsSSE`); of `__int128` and wider than 16 bytes MEMORY
  on Linux (`passInt128VectorsInMem`); of 32 and 64 bytes SSE, SSEUP only when
  named and within `vector_bytes`; illegal vectors (`IsIllegalVectorType`)
  `byval` in memory and returned as values.
- **In memory:** an aggregate or an illegal vector as a `byval` copy aligned
  to the larger of 8 and its alignment (`getIndirectResult`), at least 8
  bytes (`CCIfByVal<CCPassByVal<8, 8>>`); `_BitInt`s as aggregates
  (`isBitIntType`, for results too); other scalars in LLVM's slots for their
  types, each aligned to its size (`CCAssignToStack`: 8 bytes for `i32`,
  `i64`, `f32` and `f64`, 16 for `f80`, `f128` and 16-byte vectors, 32 and 64
  for wide vectors; and 16 for an `i128`, `CC_X86_64_I128`'s
  `AllocateStack(16, Align(16))`).
- **Registers:** an `i128` in two consecutive registers or on the stack
  (`CC_X86_64_I128`); a `WIDE` vector of a variadic call on the stack
  (`CCIfNotVarArg`); a value whose low eightbyte is padding in its high one's
  register alone (`getDirect(HighPart, 8)`); narrow integers extended
  (`isPromotableIntegerTypeForABI`, `getExtend`).
- **Results:** rax and rdx, xmm0 and xmm1, st0 and st1 (`classifyReturnType`,
  `RetCC_X86_64_C`, `RetCC_X86Common`'s `f80` registers); scalars in memory as
  their values (`getIndirectReturnResult`), split across up to four vector
  registers, or through a hidden pointer where LLVM can't return them in
  registers; `long double` in st0 and `_Complex long double` in st0 and st1,
  though both go in memory as arguments.
- **`va_arg`:** `EmitVAArg` and `EmitX86_64VAArgFromMemory`, which classify
  the type as an unnamed argument and read the overflow area aligned to the
  type's alignment past 8 (the canonical type's: see "Where it doesn't match
  clang yet").

## Where it doesn't match clang yet

Found while writing this document, at b6f26b8, each checked against clang
23.1.3's output and against the module's own (a throwaway program calling
`abi_assign` on the same signatures; `abi_va_arg` by reading it):

- **A register LLVM still has where clang counted it.** LLVM gives a register
  to any value clang passes directly (not `byval`), whatever clang counted,
  so where LLVM has a register clang thinks is used, an argument clang sends
  to memory as a value takes it; the twin puts it on the stack. Two things
  leave LLVM such a register:
  - a named `WIDE` vector in a variadic call, for which clang counts a vector
    register and LLVM uses none. For `callee(v, 1.0, ..., 8.0)` of `void
    callee(m256, double, double, double, double, double, double, double,
    double, ...)` with `-mavx2`, clang passes the eighth `double` in xmm7 with
    al 8; the module puts it on the stack at 32 with al 7. The cross-check
    catches it: with the defaults its 64-byte clang run fails on signature
    304, whose three named 64-byte vectors leave xmm5 to an unnamed 16-byte
    vector (al 6 against 5);
  - a `PAIR` after an `UNCOUNTED` argument when LLVM has one general register
    left: clang counts two, and LLVM puts the `i128` on the stack and keeps
    the register. For `callee(long × 4, nullptr_t, __int128, long)` clang
    passes the last `long` in r9, and so an aggregate of up to 8 bytes
    aligned to at most 8 in its place (`struct {int a, b;}`, which
    `getIndirectResult` coerces to an `i64` once clang's count is 0); the
    module puts each on the stack.
- **`_BitInt(128)` on the stack after an `UNCOUNTED` argument.** LLVM aligns
  an `i128`'s slot to 16; the module takes the type's slot alignment, which
  is 8 for `_BitInt(128)` and for records whose storage is one. For
  `callee(long × 4, nullptr_t, packed {char, int}, _BitInt(128), long)` clang
  puts the `_BitInt(128)` at 16 and the module at 8.
- **A result LLVM returns through a hidden pointer.** A vector result that
  needs more than four registers, or of `__int128`s wider than 16 bytes, is
  returned directly in clang's IR, and LLVM's demotion adds the hidden
  pointer: clang counts six free general registers, and LLVM's pointer takes
  rdi. The module counts five, as for clang's own hidden pointer. For a
  `struct {long, long}` after four `long`s, returning `float
  __attribute__((vector_size(128)))` without AVX, or `__int128
  __attribute__((vector_size(32)))` at any width, clang splits the struct
  between r9 and the stack, and the module puts it whole on the stack, as GCC
  16 does (its hidden pointer is the psABI's).
- **`va_arg` of an aligned typedef.** `EmitVAArg` classifies and aligns the
  canonical type, so a typedef's `aligned` attribute doesn't align the
  overflow area: clang 23.1.3 reads `va_arg(ap, T)` of `typedef long T
  __attribute__((aligned(32)))` from the overflow area unaligned and advances
  it by 8. `abi_va_arg` aligns to the typedef's alignment, against
  `abi_assign`, which passes the type as its base. Nothing tests `abi_va_arg`
  yet: the cross-check's C side reads the unnamed arguments, and the module's
  callee reads its arguments from their locations.

## Where GCC passes otherwise

Provisional: #137's GCC cross-check is still being triaged, and at b6f26b8 it
doesn't pass with the defaults (it fails at 16- and 32-byte vectors, and GCC
refuses the 64-byte run's C: "size of array element is not a multiple of its
alignment"). The cases the test's comments document there, which the GCC
worlds leave out or work around:

- **`_Atomic` types:** clang makes every one MEMORY, so `_Atomic` aggregates
  and records with an `_Atomic` or `nullptr_t` member go in memory and
  `_Atomic` scalars and `nullptr_t` pass uncounted; GCC classifies them as
  their values.
- **Flexible array members:** clang puts the record in memory; GCC classifies
  the rest.
- **Vectors of 4 bytes or less of floating elements:** clang INTEGER (GCC's
  rule once), GCC 16 SSE.
- **Unnamed bit-fields of a nonzero width:** clang skips them, GCC classifies
  them.
- **A vector of one `__int128` in an aggregate:** GCC classifies it as one
  eightbyte.
- **Packed members:** clang checks each member at an offset its type's
  alignment divides, GCC each scalar at one its size divides. They agree for
  scalars and arrays of them, but not for records, unions and aligned
  typedefs that packing moves off their alignment.
- **Vector results MEMORY by their size:** clang returns them as values
  (xmm0 for `<1 x double>`, xmm0 and on for one wider than the target's), GCC
  through a hidden pointer.
- **Named `WIDE` arguments of variadic calls:** LLVM on the stack, GCC in a
  YMM or ZMM register, as the psABI says.
- **Unnamed `WIDE` aggregates** (a union of vectors as wide as a vector
  register): GCC gives them an integer mode, so its caller passes them in a
  register and its `va_arg` reads 32 or 64 bytes from the register save area,
  past the 16 a register has there; GCC 16 crashes calling itself so.
- **`_Complex __bf16`:** GCC passes it in two SSE registers but counts one, as
  neither its `va_arg` nor clang reads it.
- **What GCC can't spell or lays out otherwise:** `__fp16` on x86-64,
  `_Complex __float128`, clang's `ext_vector_type` and complex `_BitInt`s; an
  array of length 0 before a struct's last member, which GCC refuses; and a
  declarator's attributes, which GCC applies before the `vector_size` of its
  declaration's specifiers, dropping the `aligned` of `typedef long
  __attribute__((vector_size(8))) *T __attribute__((aligned(32)))` and the
  `packed` of a member `unsigned char __attribute__((vector_size(16))) m
  __attribute__((packed))`, which it sees on an `unsigned char`.
- **A GCC 16 crash:** its `bitintlower` pass fails on a `_BitInt` as wide as
  a vector register copied into place with `memcpy` before a call whose result
  is an aggregate, so the test's C loads such an argument as its type.

## Tests

`./build/compiler test-calling-convention [--seed N] [--count N]
[--cross-check] [--signatures N] [--clang PATH] [--gcc PATH] [--keep]`.
`--seed N` changes every random input, `--count N` the random worlds' sizes
and how many random calls are assigned (2,000 by default), `--signatures N`
the cross-check's signatures per run (400), `--clang` and `--gcc` its
compilers, and `--keep` keeps its files. The counts below are the defaults'
(seed 1) at b6f26b8.

- **Merge** (192 checks): clang's `merge` and `postMerge` written again as
  clang writes them, against the module's on every pair of classes and both
  kinds of size; the module's merge must equal clang's but where clang stops
  at MEMORY.
- **Fixtures** (772 checks): about 85 types whose classes, pass bits and
  results clang's IR shows (`clang -O1 -S -emit-llvm` of functions that take
  and return them, read for `byval`, `sret`, the coerced types and the
  attributes), at each of the three vector widths, by the vector path and by
  the twin: every scalar, `_BitInt` and complex type, `__fp16`, `nullptr_t`
  and `_Atomic` ones, small and wide vectors, and records with bit-fields,
  padding, packing, alignment, flexible and `_Atomic` members, x87 members in
  unions, wide vectors in records and unions, and nesting. Then five calls
  whose locations clang and LLVM give, with their totals: an `UNCOUNTED`
  `nullptr_t` that splits a struct, a `PAIR` on the stack with the register
  after it taken, a high eightbyte alone on the stack, an `UNCOUNTED`
  `_Atomic double` in xmm7, and a hidden pointer.
- **Worlds and the oracle** (76,982 offsets compared, 110,008 checks): random
  worlds of types (scalars, vectors, complex types, `_BitInt`s, `_Atomic` and
  aligned types, and records and unions of them, small ones mostly, with
  bit-fields, arrays, nesting, packing, alignment and flexible array members),
  one at each vector width and one shaped for GCC, classified by the vector
  path into one table and by the twin into another, which must be identical,
  column by column. Each classified type at each offset it can be at must
  then have the oracle's classes: clang's `classify` transcribed as clang has
  it, its recursion over members and elements a stack of frames, from the
  type table alone (no rows); and as an unnamed argument too.
- **Assignment** (33,339 arguments, 39,471 checks): random calls over a
  world's types, at each vector width, named and variadic, short and past the
  registers, with results in memory and in registers, assigned by the vector
  path and by the twin, which must give the same locations and totals; and
  the vector path on every prefix of up to 40 arguments, with the argument and
  location columns ending at a `PROT_NONE` page.

### The cross-check

`--cross-check`, contract 4 on #135, needs no front end. For each compiler
(clang and GCC; `--clang`, `--gcc`) and each vector width it builds a world of
types (600 random ones beside the builtin, `_BitInt` and vector ones), random
signatures over its passable ones (`--signatures`, 400 by default; up to 21
named arguments and 6 unnamed, a fifth of the results `void`), and their
locations from `abi_assign`:

- **The C file** has the world's typedefs, written by `c_types_test.c`'s
  oracle, every signature's argument and result bytes (each value a valid
  representation of its type, with a mask of its meaningful bits: no padding,
  nor a `_BitInt`'s bits past its width), and per signature a C callee, which
  checks its parameters and its `va_arg`s, a C caller of our callee, which
  checks what our callee stored of its arguments, the al it got and the
  result it returned, and `main`, which runs them and checks what our caller
  stored of the C callee's result and the hidden pointer it got back.
- **Our object** has, per signature, a caller of the C callee and a callee of
  the C caller, generated as encoder records from the locations: stack
  arguments copied with `rep movsb`, register ones loaded (extended as their
  flags say), the hidden pointer and al set, the result stored (x87 parts with
  `fstp`) or copied through the hidden pointer, which our callee returns in
  rax. The records go through `x86_encode`, `link_add_code_fixups` and the
  object writer. This emitter is test code; production emission is #136's.
- The compiler's driver compiles the C (`-std=gnu23 -O1 -fno-strict-aliasing`
  and `-march=x86-64`, `-march=x86-64-v3` or `-march=x86-64-v4` for 16, 32 or
  64 bytes) and links it with our object and `-latomic`; the program runs
  every signature both ways and fails if a value differs where its mask says
  it means something, or a hidden pointer comes back wrong.

The signatures leave out `_Atomic` types (the fixtures check them), and the
unnamed arguments leave out the types the default argument promotions change,
`nullptr_t` and `_Atomic` ones, types wider than 16 bytes that are SSE, SSEUP
as unnamed arguments, and types of no size, whose calls and `va_arg`s clang
passes differently (see "va_arg"). GCC's worlds leave out what "Where GCC
passes otherwise" lists. The cross-check doesn't see a missing extension
(each side compares a value's own bytes), which the fixtures' `SIGN` and
`ZERO` check instead, and it doesn't run `abi_va_arg`. The files go in a
directory under `$TMPDIR` (or `/tmp`), removed unless `--keep` or a failure.
With the defaults at b6f26b8 (clang 23.1.3), clang's runs pass at 16 and 32
bytes (400 signatures each, of 2,591 and 2,432 arguments) and fail at 64 (see
"Where it doesn't match clang yet").

## Measurements on the 9700X

To come: `benchmark-calling-convention` (`calling_convention_benchmark.c`)
will report the vector paths against their twins on the 9700X, classification
per type and assignment per argument, and this section will hold its numbers.

## Next steps

- **The benchmark** and its numbers, above, in both of Zen 5's states (#199):
  both vector paths are short AVX-512 phases between scalar work.
- **The mismatches above**, then the GCC cross-check passing, with the
  divergences it leaves out recorded here.
- **#136's use:** call sequences, prologues and `va_arg` from these
  locations, and Tier 0's depth-to-register order (contract 6 on #135).
