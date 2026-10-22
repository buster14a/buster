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
  (`abi_capacity_bytes`: 42 bytes a type, and 448 for the columns' alignment),
  and classifies the builtin types; it fails, leaving the arena as it was, if
  the arena can't hold them. `abi_classify_all` then classifies every complete
  type the table has that isn't classified yet. Each type gets its row of
  classes, its pass bits, its stack slot's size, its canonical alignment, its
  result, and what passing needs of clang's LLVM type for it (see
  "Representation"). `abi_classes(abi, type, named)` gives a type's two
  classes as a named argument or a result sees them, or as an unnamed
  (variadic) argument does.
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
of 4 GB or more, whose stack size is `UINT32_MAX` (and, until the module
models them, the `_Atomic` types "Next steps" names). The table is the code
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
| 9 | `PAIR` | one 128-bit integer value (clang's `i128`), which LLVM never splits between a register and the stack; with a NO_CLASS high eightbyte too (an `i128` a union stores), which clang counts and passes as two INTEGER eightbytes |
| 10, 11 | `SIGN`, `ZERO` | clang's `Extend`: the caller extends the value to 32 bits (the callee, a result) |
| 12 | `AGGREGATE` | clang's `byval`: in memory as an argument, a copy on the stack (or an integer value: see "Two counts") |
| 13 | `END_MEMORY` | a type of no size that is MEMORY at offset 16 though not at 0 |
| 14 | `UNSUPPORTED` | an `UNCOUNTED` vector wider than 16 bytes and than the target's vector registers, which LLVM would split across several registers as no class says: the code generator refuses it |
| 15 | `CLASSIFIED` | the row and these bits are in the table |
| 16 to 18 | `VALUE` | the kind of value a scalar in memory passes as: a general register (`GENERAL`), two (`PAIR`), a vector register (`VECTOR`), x87's (`X87`), which always goes on the stack, so that such a type isn't `UNCOUNTED`, or two halves, each in a general register while one is left and then on the stack (`HALVES`: a `<1 x i128>`, which LLVM splits as it never does an `i128`) |
| 20 to 25 | `ALIGNMENT` | the log2 of the stack slot's alignment: 3, or the type's own if larger (an aggregate's own, a scalar's value's natural one) |
| 26 | `EMPTY` | a type of no size: in memory it takes an 8-byte slot, or none as an `i0` (see "Two counts") |
| 27 | `LOW_GENERAL` | the low eightbyte, SSE by its class and counted so by clang, goes in a general register: clang's type for it is narrow and the high part's isn't aligned to 8 (see "Where clang's LLVM types decide") |

Beside the pass bits, per type: `stack_bytes`, the slot's size (the type's
size rounded up to 8; 8 for a type of no size, as LLVM gives a `byval` of no
size 8 bytes; `UINT32_MAX` for 4 GB or more), `alignment_log2`, its canonical
alignment (an aligned typedef looked through, as clang's canonical types have
no typedefs, and an array's element's), and `results`, its `AbiReturn`. An
aligned typedef (`ALIGNED`) passes as its base: `abi_copy` gives it its base's
row, bits, slot, result and LLVM type.

### clang's LLVM types

Some of clang's and LLVM's choices follow from the LLVM IR type clang gives a
C type, not from its classes (see "Where clang's LLVM types decide"). Two more
columns keep what those choices need, which `abi_finish` works out from the
members' own:

- **`storage`**, a byte: the log2 of the type's alignment in LLVM's layout,
  `STORAGE_I128` when its storage at offset 0 is an `i128` (`abi_storage`),
  and `STORAGE_EMPTY` for a record clang's layout counts empty
  (`isEmptyRecordForLayout`: each member a zero-width bit-field or of such a
  record's type). An `__int128` is an `i128` aligned to 16, and so is a
  `_BitInt` of 65 to 128 bits as LLVM stores it (a wider one is aligned to 16
  too). A struct's storage at offset 0 is its first member's there, and the
  struct is aligned to its members' largest alignment unless `determinePacked`
  packs it (a member off its own alignment, or a size that isn't a multiple of
  it). A struct's bit-fields share access units (`accumulateBitFields`): a run
  of them, from one that starts a byte through those that start inside a byte
  after it, wider than an eightbyte is the integer its bytes round up to, an
  `i128` for 9 to 16, where that fits before the next member with storage (but
  a zero-width bit-field and an empty record) or the struct's end, and
  otherwise an array of its bytes (`abi_bit_field_run`). So `struct { unsigned
  __int128 x : 65; }` stores an `i128`, and packed, or followed by a `char` or
  an `int[0]`, a `[9 x i8]`; a run's narrower units are left as its
  bit-fields' types are aligned. A union is stored as the member LLVM aligns
  most, then the largest, the first of equals
  (`CGRecordLowering::lowerUnion`); a bit-field there is an `iN` of its width,
  or of its type's bits if fewer, rounded up to bytes (`getIntNType`: 121 to
  128 bits make an `i128`). An array, a complex type, an enum and an `_Atomic`
  type take their element's, underlying type's or value's, never
  `STORAGE_EMPTY`. A record larger than 16 bytes gets none (0): only a type of
  16 bytes or less passes by these columns, and such a type holds a larger
  record only as a zero-length array's element (a larger record is never
  empty, which takes a size of 0). clang keeps that array in its LLVM type, as
  `[0 x %struct.big]`, aligned as the record, so the array's storage is the
  record's alignment, which counts in the smaller record's alignment and in a
  union's choice of its storage member; the array works it out when it is
  finished (`abi_wide_alignment`: the record's members' storage alignments, a
  bit-field's run's unit's among them, a larger record walked in turn on an
  explicit stack, or 1 if packed), and waits in the levels until everything
  the walk reads is classified. `union {struct {long a, b; struct {__int128 x;
  long y;} z[0];} s; __int128 i;}` passes as two `i64`s (the struct is aligned
  to 16 and comes first) where `union {__int128 i; ...}` passes as one
  `i128`.
- **`layout`**, 8 bytes, which only a type of 16 bytes or less gets, since
  only such types read their own and their members' and elements': the scalars
  of the LLVM type, as `GetSSETypeAtOffset` and `GetINTEGERTypeAtOffset` find
  them (`abi_layout`): a 4-bit kind at each even offset (`i8` to `i64`,
  `half`, `bfloat`, `float`, `double`, or another), and which of the 16 bytes
  hold the C type's data (`BitsContainNoUserData`'s view, which takes a
  bit-field as its whole declared type, not its width). A struct's padding
  holds `i8`s, as the byte arrays clang pads with do; its bit-fields are an
  integer of their type at the start of their storage unit, an approximation
  of clang's access units. A union's scalars are its storage member's.

### Results, locations and calls

`AbiReturn`, 8 bytes:

- **kind:** `IGNORE` (void, or nothing to return), `REGISTERS` or `MEMORY`
  (the caller passes the result's address in rdi, and the callee returns it in
  rax);
- **registers[4]**, in order, each holding `part_bytes` of the value: an
  eightbyte, what a vector register holds of it, or 16 bytes for an x87
  register's 80-bit value;
- **flags:** `SIGN` or `ZERO` (the callee extends to 32 bits), `HIGH` (the
  value's low eightbyte is padding: the register holds its high one), and
  `DEMOTED`: a value clang returns as a value and LLVM through a hidden
  pointer, which clang doesn't count (see "Two counts").

`AbiLocation`, 8 bytes: `stack_offset` (from rsp at the call), `registers[2]`,
`kind` and `flags`. The kinds:

- **`IGNORE`:** nothing is passed.
- **`REGISTERS`:** `registers[0]` holds the first part and `registers[1]` the
  second, or none: an eightbyte each, one vector register for a `VECTOR`, two
  general registers for a `PAIR`.
- **`STACK`:** the whole value at `stack_offset`. With `HIGH`, the value's
  high eightbyte alone, in an 8-byte slot.
- **`SPLIT`:** one part in its register and the other, `NONE` in its place, in
  the 8-byte slot at `stack_offset`, where clang's count runs ahead of LLVM's
  registers (see "Two counts").

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
   by how many entries they fold (a record's members, or an array's
   elements), a counting sort over 64 buckets, whose last takes every count
   from 63 up and whose prefix sums stop at the last bucket in use, and
   grouped four at a time, so that a group's lanes end together. Each step of
   a group is a 64-byte block of four entries; a lane that ends early, or has
   no type, merges `NOTHING` (NO_CLASS, NO_CLASS), which changes nothing.
2. **Entries.** A scalar pass writes each member's entry into its group's
   blocks: its classes at the enclosing type's 16 offsets, by
   `abi_record_entry`'s and `abi_array_entry`'s rules, 16 offsets at once
   (`abi_member_entry`, `abi_write_entries`). That is the member's row shifted
   by its byte offset with one `vpshufb` (lane o reads offset (o + shift) mod
   16, `abi_shifted_row`), MEMORY where it would be misaligned, by a
   `vptestnmb` of o + shift against its canonical alignment's mask
   (`abi_aligned_lanes`); a bit-field's two classes from the offsets where it
   starts in the low eightbyte and ends in the high one, two byte compares
   (`NOTHING` for an unnamed one, which clang skips); or an element's row
   shifted by the element's offset. A member that can't be in a record larger
   than 16 bytes is MEMORY at every offset.
3. **Parts.** For a record or an array of 16 bytes or less, the only types
   whose `storage` and `layout` passing reads, the same pass reduces each
   member's or element's part as it writes the entry (`abi_member_part`,
   `abi_element_part`): two 64-bit words of what it adds to the type's LLVM
   type, by `abi_member_storage`'s, `abi_storage`'s and `abi_layout`'s rules
   for the member alone. The layout word holds, as `layout` has them, the
   scalars it puts at the even offsets (a member's type's where it is; a
   bit-field's integer at its storage unit's start, unless a member before it
   put a scalar there, which the pass keeps track of along a struct's
   members) and its data bytes, and in a struct the bytes its storage covers.
   The storage word holds, in a struct, a bit for its storage's alignment
   (bit 32 plus the log2), whether it is off that alignment
   (`determinePacked`) and the i128 of the first member at offset 0 with
   bytes; in a union, the member as a candidate for the union's storage
   member: its alignment, bytes and the steps left after it from the top bit
   down, then its i128 and its scalars, so that the largest word, unsigned, is
   `lowerUnion`'s member (most aligned, then largest, then first) and carries
   what the union takes from it. The words are ORed, but a union's storage
   words, of which the pass keeps the largest; then `abi_finish_parts` makes
   the two its type's `storage` and `layout`: a struct's alignment is its
   highest alignment bit's, a union's its storage member's, either 1 if
   packed (a member off its own, or a size the alignment doesn't divide); a
   union takes its scalars, covered bytes and `i128` from its storage
   member's key; and the padding's even offsets get `i8`s (a `pext` and a
   `pdep`). The pass works the parts out a member at a time anyway, and an OR
   (or a max) there is cheaper than handing them to the fold: reducing them
   four lanes at a time in the merge loop, from a second 64-byte block per
   step, measured slower on the 9700X (see "Measurements on the 9700X").
4. **Merging** (`abi_fold_groups`): each step is clang's merge in all 64
   bytes at once (`abi_merge_vector`). The table
   `merge[accumulated << 3 | field]`, 64 bytes in the `AbiTable`, is one ZMM
   register: a `vpternlogd` builds each byte's low-eightbyte index from the
   accumulated class's bits shifted left 3 and the field's, another its high
   one's from bits 4 to 6 of both, two `vpermb` look them up (they read an
   index's low 6 bits only), and an OR recombines them.
5. **Post-merging** (`abi_post_merge_vector`): clang's four rules by byte
   compares and masks, with a group's offsets that its types can't be at (and
   an array's misaligned ones) forced to MEMORY and, for types larger than 16
   bytes, offset 0 required to be SSE, SSEUP. Each lane's 16 bytes are then
   stored as its type's row. Then `abi_finish_passing` finishes each folded
   type with parts, and `abi_finish` each one without: the records and arrays
   larger than 16 bytes, whose `storage` takes no loop (a record has none, an
   array its element's), and a union of 16 bytes or less with more members
   than its keys' 22 bits of steps count, which `abi_finish` reduces with the
   twin's loops.

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
element (an array's), members (a record's, unless its row doesn't depend on
them: larger than 64 bytes, or with a flexible array member) or value (an
`_Atomic` type's, whose storage and layout it takes) are classified, and for
a zero-length array of a record larger than 16 bytes every type whose storage
`abi_wide_alignment` reads, classified together by `abi_classify`, then the
next level, until none is left. Each of these is complete when the type is,
so no type waits forever. (When an `_Atomic` type didn't wait for its value,
its storage and layout depended on whether the value happened to be finished
before it, and the two paths could differ.) Without scratch memory for the
lists it classifies a type at a time with the twin, in passes over the table,
until a pass classifies nothing.

### Finishing a type

`abi_finish` derives the rest from a type's row[0] and what it is, the twin's
way for every type (the vector path's for the types it doesn't reduce parts
for):

- **`storage` and `layout`** (see "clang's LLVM types"), from its members',
  elements' or base's, which the levels classify first: `abi_storage` and
  `abi_layout`, loops over a record's members and an array's elements (and a
  union's, twice more, for its storage member), which the vector path
  reduces as it writes the fold's entries instead (its step 3);

and then `abi_finish_passing`, which both paths share:

- **the pass bits** of its classes, and `AGGREGATE` for records, arrays,
  complex types, `_Atomic` ones of those and `_BitInt`s
  (`isAggregateTypeForABI`, and `getIndirectResult`'s and
  `getIndirectReturnResult`'s `isBitIntType`), and for clang's illegal
  vectors (`IsIllegalVectorType`: 8 bytes or less, wider than the target's
  vectors, or of `__int128`s), which go in memory as `byval` copies too,
  though they return as their values;
- **`PAIR`** when clang passes two INTEGER eightbytes as one `i128`
  (`abi_is_pair`): `__int128` and `_BitInt(128)` by themselves, and aggregates
  whose `storage` at offset 0 is an `i128` (`GetINTEGERTypeAtOffset` returns
  the `i128` it finds there): a struct whose first member is an `__int128`, a
  `_BitInt` of 65 to 128 bits or an aggregate stored so, or whose first run of
  bit-fields is one; a union that stores one; an array of them.
  `_BitInt(65)` to `_BitInt(127)` by themselves pass as two `i64`s, which LLVM
  can split. A union stored as an `i128` whose high eightbyte is NO_CLASS
  (`union { int a; unsigned _BitInt(128) b : 127; }`) is a `PAIR` too, with
  `HIGH_INTEGER`: clang counts and passes two INTEGER eightbytes;
- **`LOW_GENERAL`** when the low eightbyte is SSE but clang's type for it, by
  `GetSSETypeAtOffset` over `layout`, is narrow (a `bfloat`, `<2 x half>` or
  `<2 x bfloat>`) and the high part's isn't aligned to 8 (`abi_low_general`):
  `GetX86_64ByValArgumentPair` widens such a low part to an `i64`, which goes
  in a general register;
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
  (`abi_value_result`): rax, rax and rdx for an `i128` and for a `<1 x i128>`,
  a vector of one element where its element would go (LLVM scalarizes `<1 x
  T>`), st0, or as many of the target's widest vector registers as the value
  needs, which LLVM splits it into (`RetCC_X86Common` has four of each width),
  and `MEMORY` with `DEMOTED` for a vector that would need more than four, or
  of `__int128`s wider than 16 bytes, which LLVM returns through a hidden
  pointer; st0 for X87; st0 and st1 for COMPLEX_X87; one vector register for
  SSE, SSEUP; otherwise an eightbyte a register, INTEGER in rax then rdx and
  SSE in xmm0 then xmm1 (a `LOW_GENERAL` low eightbyte in rax, and a `PAIR`'s
  high one in rdx, NO_CLASS or not), with `HIGH` when the low one is padding;
- **`EMPTY`** and **`END_MEMORY`** for a type of no size, and the canonical
  alignment.

## Assignment

### Two counts: clang's and LLVM's

clang decides from its count of free registers whether an argument's
eightbytes go to registers (`computeInfo`: all of them if every register they
need is free, otherwise the whole argument to memory, its registers staying
free for the arguments after it); LLVM then gives each value clang passes it
the next register of its kind or, once there is none, a stack slot of its
own. The two counts differ wherever clang counts otherwise than LLVM assigns:

- **LLVM uses registers clang doesn't count.** `nullptr_t` and `_Atomic`
  scalars are MEMORY by their classes, and clang's `getIndirectResult` passes
  a scalar in memory as its value ("assume LLVM will pass it in the right
  place naturally", with the FIXME of PR12193), which `CC_X86_64_C` puts in
  the next register of its kind (`UNCOUNTED`). A `DEMOTED` result's hidden
  pointer takes rdi, which clang, returning a value, doesn't count. A
  `LOW_GENERAL` low eightbyte takes a general register where clang counted a
  vector one.
- **clang counts registers LLVM doesn't use.** A named `WIDE` vector of a
  variadic call goes on the stack, clang having counted a vector register for
  it, and a `PAIR` that LLVM can't give two general registers goes on the
  stack whole, clang having counted two. The values clang then sends to
  memory find LLVM's registers free: a scalar or a vector that clang passes
  as its value takes the next vector register, an integer the next general
  one, and an aggregate of 8 bytes or less aligned to at most 8, which
  `getIndirectResult` passes as an integer of its size once clang's count has
  no general register left (MEMORY or not), takes a general register too, or
  8 bytes of the stack; one of no size is an `i0`, which takes none
  (`EMPTY`).

So the twin, `abi_assign_call` (one call; `abi_assign_scalar` runs it over
every call), keeps both counts: clang's free registers, 6 and 8 (5 general
with a hidden pointer that isn't `DEMOTED`), and LLVM's used ones. For each
argument in order:

1. `IGNORE`: nothing.
2. `UNCOUNTED`: its value takes LLVM's next general register, its next two
   for a `PAIR` (consecutive, or none), the next for each half of `HALVES`
   (which may leave it `SPLIT`), or its next vector register (in a variadic
   call only up to 16 bytes, as `CCIfNotVarArg` keeps YMM and ZMM registers
   from variadic calls); otherwise a stack slot. clang's counts don't change.
3. A scalar or a vector that needs vector registers alone, with none left in
   clang's count, takes LLVM's next one if it has one (but a `WIDE` vector of
   a variadic call); an integer or a pointer, with clang's general count at 0,
   takes LLVM's next general register if it has one.
4. MEMORY (by its class, or `WIDE` and unnamed), or clang's counts short of
   its needs: a stack slot of its size, aligned to its slot's alignment; but
   with clang's general count at 0, an aggregate of 8 bytes or less aligned to
   at most 8 takes LLVM's next general register, or an 8-byte slot, or
   nothing if it is `EMPTY`.
5. A `VECTOR` or a `PAIR`, one value: clang's counts drop; LLVM's next vector
   register, or two consecutive general ones; otherwise the whole value on
   the stack, as `CC_X86_64_I128` never splits an `i128`, in a slot aligned to
   16 at least (`AllocateStack(16, Align(16))`). A `WIDE` vector in a variadic
   call always goes on the stack, in a slot aligned to its size
   (`CCAssignToStack<32, 32>` and `<64, 64>`), clang having counted a vector
   register for it all the same (the psABI, and GCC, pass a named one in a
   register: LLVM's FIXME on `CCIfNotVarArg`).
6. Otherwise an eightbyte at a time, each a value of its own (clang coerces
   such an argument to `{i64, double}` and the like): clang's counts drop;
   the low eightbyte (unless it is padding) and then the high one each take
   LLVM's next register of its class (a general one for a `LOW_GENERAL` low
   eightbyte), or an 8-byte slot. The location is `REGISTERS`, `STACK` (both
   on the stack) or `SPLIT`.

For `f(long, long, long, long, nullptr_t, struct {long, long}, long)` the
struct is `SPLIT` between r9 and the stack's first slot, and the last `long`,
which clang sends to memory, takes the second slot; for `f(double × 7,
_Atomic double, struct {long, double}, long)` the struct is `SPLIT` between
rdi and the stack (xmm7 went to the `_Atomic double`). The other way, for
`f(long × 4, nullptr_t, __int128, long)` LLVM has r9 alone for the `__int128`,
which goes on the stack at 0, and the last `long`, which clang sends to
memory as a value, takes r9. Each slot is placed by `abi_stack_place`: aligned
up to its alignment, the area growing past it and its alignment to the
slot's.

### The vector path: segmented prefix sums

`abi_assign` takes every call's arguments in order, 16 at a time whatever call
they belong to, after a scalar setup over the calls that has no loop over a
call's arguments: it writes each call's result, starting totals and first
argument, and its marks, a byte per argument (zeroed first): at the call's
first argument `START`, `HIDDEN` (its result goes in memory: the hidden pointer
takes rdi before it), `DEMOTED` and `VARIADIC`, and `UNNAMED` at its first
unnamed one. Past the last argument, a `START` ends it.

1. **Loads:** a masked load of 16 `TypeId`s, two `vpgatherdd` of their pass
   bits and stack bytes, and the marks of the 16 arguments and of the 16
   after each: a lane whose next argument starts a call ends its own.
2. **Needs:** per lane, the general registers it needs in the low 16 bits and
   the vector ones in the high 16 (0 for an argument in memory), plus 1
   general at a call's first lane after a hidden pointer.
3. **A segmented scan** (`abi_scan_add`): four steps of `valignd` by 1, 2, 4
   and 8 lanes and a masked add, each lane adding the partial sum 2^k lanes
   below it only when no call starts after that lane and up to it; then the
   lanes before the vector's first call start add the carry, the previous
   vector's last lane's sum (broadcast with `vpermd`). Inclusive sums less the
   needs give each argument its first register of each class. A segmented OR
   scan (`abi_scan_or`) of the marks gives each lane its call's `VARIADIC`
   and, from the call's first unnamed argument on, `UNNAMED`.
4. **Fit:** an argument whose registers fit (first plus need at most 6 for
   the general ones it needs and 8 for the vector ones) gets them, by a
   `vpermd` from a 16-entry table (rdi to r9, then none) for general ones and
   an OR of 0x20 for vector ones; one that needs registers and doesn't fit
   goes to the stack. Each class is tested only where the argument needs it:
   a `long` after nine `double`s fits, though the vector sum is past 8.
5. **The stack:** the slots of the arguments on the stack in another
   segmented scan, whose exclusive sums are their offsets, and an OR scan of
   their alignments, whose highest bit is the call's, with bit 0 (below every
   slot's alignment, 8 or more) set by a lane the twin must assign.
6. **Stores:** each lane's 8-byte location (its offset, and a word of its
   registers, kind and flags), interleaved with `vpermt2d` and stored with two
   masked 64-bit stores of 8; and at the lanes that end a call, the three
   scans' inclusive values, its totals, into columns of their own (three
   masked stores).

A last pass over the calls reads each call's totals at its last argument (a
call without arguments keeps its starting ones): the registers used, clamped
to 6 and 8, the stack bytes and the largest alignment. The calls with bit 0
set go to the twin. The setup's marks and the totals' columns replace a loop
over each call's arguments, whose random lengths mispredicted at their exits,
and a loop per vector over the calls that end in it.

Where nothing makes clang's and LLVM's counts differ, the twin's two counts
are complements, so one sum serves both; an argument that spills adds its
needs to the sums, but only past a class's limit, where no argument after it
finds a register of that class anyway; and with every slot a multiple of 8
and every offset its alignment divides, the twin's padding is zero, so the
slots' sums are its placements. That is exactly the twin's assignment unless
a lane:

- doesn't fit while a register it needs is still free (a two-eightbyte one
  with one free, or one of both classes with one class full), which the sums
  count as used though it stays free for the arguments after;
- is on the stack at an offset its alignment doesn't divide, where the twin
  would pad, or aligned to 2^32 or more, which a 32-bit lane can't hold;
- makes the counts differ: it is `UNCOUNTED` or `LOW_GENERAL`, a named
  `WIDE` vector in a variadic call, or the first of a call whose result is
  `DEMOTED`;
- is `EMPTY` on the stack, which may take no slot;
- or is the first of a call of more than 32,767 arguments
  (`BUSTER_ABI_COUNTED_ARGUMENTS`), whose sums 16 bits a class can't hold: an
  argument needs at most two registers of a class and the hidden pointer one
  more, 65,535 in all, and past that the general registers' sum would carry
  into the vector ones'.

A call with such a lane carries bit 0 in its totals, and after the loop the
twin assigns it again, over what the vector path wrote: 1.7% of the
benchmark's calls, which hold 5.2% of its arguments (see "Measurements on the
9700X"). Without scratch memory for the setup's columns, `abi_assign` runs the
twin on every call.

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
  2^`overflow_alignment_log2` (8, or the canonical type's alignment if larger:
  `EmitVAArg` classifies and aligns the canonical type, so an aligned
  typedef's attribute doesn't count), which then advances by `stack_bytes`,
  the type's size rounded up to 8. Types in memory, `WIDE` ones among them, go
  here.

clang's `va_arg` differs from clang's own calls in four places. The module
keeps both sides as clang has them in the first three, and takes GCC's memory
in the fourth:

- **`nullptr_t`**: the call passes it in a general register (`UNCOUNTED`),
  but its `va_arg`, which sees MEMORY, reads it from the overflow area.
- **A type of no size in memory** (a struct of a flexible array member alone):
  the call gives it an 8-byte slot (the `byval` minimum), its `va_arg` reads
  and skips none.
- **After the counts differ.** `va_start` starts at the registers LLVM gave
  the named arguments and the hidden pointer, and `va_arg` reads each unnamed
  argument from the save area if all the registers its classes need remain
  there, else from the overflow area: clang's count, by the classes. Past an
  argument the call placed by LLVM's registers otherwise (`SPLIT`,
  `UNCOUNTED`, `LOW_GENERAL`, a `PAIR` on the stack, or after a `DEMOTED`
  result), the two can disagree; a scalar, which both place by LLVM's
  registers, still agrees.
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
  `_Atomic long double`, x87's on the stack, which leaves it plain MEMORY; a
  vector of one element as its element, as LLVM scalarizes `<1 x T>`, and a
  `<1 x i128>` as two `i64`s); and the `SPLIT` and shifted arguments after
  them.
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
  the canonical type as an unnamed argument and read the overflow area aligned
  to its alignment past 8.
- **clang's LLVM types:** `getIndirectResult`'s integers, which LLVM may give
  a register clang didn't count; `GetX86_64ByValArgumentPair`'s widened low
  parts, by `GetSSETypeAtOffset` and `GetINTEGERTypeAtOffset` over the
  record's LLVM type (`CGRecordLowering`); `CC_X86_64_I128`'s unsplit `i128`
  and its slot aligned to 16; results LLVM demotes to a hidden pointer
  (`CanLowerReturn`, then `sret` added by LLVM); and LLVM's own placement of
  values clang didn't count (`CC_X86_64_C`): see "Where clang's LLVM types
  decide".

## Where clang's LLVM types decide

The cross-check found these rules, in which the LLVM IR type clang gives an
argument or a result, not its classes, decides where it goes. None is in the
psABI. Each was confirmed with `clang -S -emit-llvm` probes of clang 23.1.3
and has a fixture:

- **Integer values instead of copies.** Once clang's count has no general
  register left, `getIndirectResult` passes an aggregate of 8 bytes or less,
  aligned to at most 8, as an integer of its size, MEMORY or not. LLVM gives
  it a general register if it still has one (after the counts differ), and 8
  bytes of the stack otherwise. A struct of a flexible array member alone is
  an `i0`, which takes no slot (`EMPTY`); clang crashes now and then on `i0`
  arguments (in `LowerArguments` and in `ADCE`), so the cross-check passes a
  `long` in their place.
- **Widened low parts.** `GetX86_64ByValArgumentPair` widens a low part
  narrower than 8 bytes when the high part's type isn't aligned to 8: a
  `half` or a `float` to a `double`, which changes nothing, and anything else
  to an `i64`, which goes in a general register though clang counted a
  vector one (`LOW_GENERAL`). That is a `bfloat`, `<2 x half>` or `<2 x
  bfloat>` before an `i32`, a `float` or the like: `struct { __bf16 a, b;
  _Alignas(8) int c; }` passes in rdi and rsi.
- **`i128` storage.** An `i128` at a record's offset 0 makes the record one
  `i128` value (`PAIR`), which LLVM never splits, in a stack slot aligned to
  16 whatever the record's alignment. With a NO_CLASS high eightbyte too, as
  in a union stored as an `i128`, clang still counts and passes two INTEGER
  eightbytes: a probe after five `long`s tells, since the union then goes
  `byval` rather than in r9. A run of bit-fields is an `i128` only where its
  16 bytes fit (see "clang's LLVM types"): after an uncounted `_Atomic int`
  and four `long`s, `struct { unsigned __int128 x : 65; }` goes whole on the
  stack and a `long` after it takes r9, while packed (an `i64` and an `i8`) it
  is split between r9 and the stack, as is `{ unsigned __int128 x : 65; char
  c; }`, and `{ unsigned __int128 a : 60, b : 10; }` is one `i128`.
- **Zero-length arrays.** clang keeps one in its LLVM type of a record, as
  `[0 x %struct.big]`, aligned as its element however large: `struct {long
  a, b; struct {__int128 x; long y;} z[0];}` is aligned to 16 there, so in a
  union with an `__int128` it ties, and the first of the two is the union's
  storage: two `i64`s, or one `i128` (`PAIR`). A packed element, aligned to 1,
  leaves the struct at 8, and the `__int128` wins wherever it is; so does an
  element whose run of bit-fields is bytes, as in `{unsigned __int128 x : 65;
  char c; long d;}`, which LLVM's type aligns to 8.
- **Demoted results.** A vector result that would need more than four
  registers, or of `__int128`s wider than 16 bytes, is a value in clang's IR,
  and LLVM's demotion adds the hidden pointer, which clang doesn't count
  (`DEMOTED`): for a `struct {long, long}` after four `long`s, returning
  `float __attribute__((vector_size(128)))` without AVX, clang splits the
  struct between r9 and the stack. GCC 16 returns such a vector through the
  psABI's hidden pointer, which it counts.
- **Vectors of one element.** LLVM scalarizes `<1 x T>`: an `_Atomic` vector
  of one `short` goes in a general register, and a `<1 x i128>` splits into
  two `i64`s (`HALVES`), as a real `i128` never does.
- **LLVM's free registers.** Where clang counts registers that LLVM doesn't
  use (for a named `WIDE` vector of a variadic call, a `PAIR` on the stack),
  the values clang then sends to memory as values take the registers LLVM
  still has.

Two more such rules aren't modeled yet; "Next steps" has them.

## Where GCC passes otherwise

GCC 16.2 lays out or passes these otherwise than clang 23. The module follows
clang in every one, and the cross-check's GCC worlds leave them out, each
named in the test where it is. Five are GCC bugs, which crash GCC or make it
disagree with itself.

**Classification:**

- **`_Atomic` types and `nullptr_t`:** clang makes every one MEMORY, so
  `_Atomic` aggregates and records with an `_Atomic` or `nullptr_t` member go
  in memory and `_Atomic` scalars and `nullptr_t` pass uncounted; GCC
  classifies them as their values.
- **Flexible array members:** clang puts the record in memory; GCC classifies
  the rest.
- **Vectors of 4 bytes or less of floating elements:** clang INTEGER (GCC's
  rule once), GCC 16 SSE.
- **Bit-fields:** clang skips unnamed ones; GCC classifies those of a nonzero
  width, and a union's by their declared type, not their width (a zero-width
  one too).
- **A vector of one `__int128` in an aggregate:** GCC classifies it as one
  eightbyte.
- **Members of no size:** GCC ignores them, where clang makes a record larger
  than 16 bytes MEMORY, or a member that ends one at 16 (see `END_MEMORY`).
- **Misaligned members:** clang checks each member at an offset its type's
  alignment divides, GCC each scalar at one its size divides (and no
  `_BitInt`). They agree for scalars and arrays of them, but not for records,
  unions and aligned typedefs that packing, or a typedef that lowers an
  alignment, moves off their alignment.
- **Wide unions with a long array:** clang classifies an array wider than 16
  bytes in a union as nothing, so `union { __m128 v; char c[24]; }` is SSE,
  SSEUP to clang and MEMORY to GCC. They agree on a vector wrapped in records
  of one member its size.
- **Narrow low parts:** GCC passes a `LOW_GENERAL` type's low eightbyte in a
  vector register, as the psABI says.
- **`_Complex _Float16` in an aggregate (a GCC bug):** GCC 16 classifies one
  at any offset but an eightbyte's start as two classes, the second the next
  eightbyte's (`classify_argument`'s `HCmode`).

**Passing:**

- **Vector results MEMORY by their size:** clang returns them as values
  (xmm0 for `<1 x double>`, xmm0 and on for one wider than the target's), GCC
  through a hidden pointer. GCC also counts the hidden pointer of a result
  clang `DEMOTED`s.
- **Named `WIDE` arguments of variadic calls:** LLVM on the stack, GCC in a
  YMM or ZMM register, as the psABI says.
- **Unnamed `WIDE` aggregates (a GCC bug):** GCC gives a union of vectors as
  wide as a vector register an integer mode, so its caller passes it in a
  register and its `va_arg` reads 32 or 64 bytes from the register save
  area, past the 16 a register has there; GCC 16 crashes calling itself so.
- **`_Complex __bf16` (a GCC bug):** GCC passes it in two SSE registers but
  counts one, as neither its `va_arg` nor clang reads it.
- **`va_arg` of a raised alignment (a GCC bug):** both compilers place a
  typedef that raises a type's alignment by the type's own, but GCC 16's
  `va_arg` copies one raised to 16 or more from the overflow area with an
  aligned load (`movdqa`), which faults at an offset 16 doesn't divide; at
  `-O1` and `-O2` GCC crashes calling itself so.

**Layout and spelling:**

- **What GCC can't spell:** `__fp16` on x86-64, `_Complex __float128`,
  clang's `ext_vector_type` and complex `_BitInt`s.
- **Arrays:** GCC refuses an array of length 0 before a struct's last member,
  and an array whose element's size isn't a multiple of its alignment.
- **Enums:** GCC ignores `aligned` on an enum, and gives one whose values no
  64-bit type holds `__int128`, where clang gives it `long`.
- **Bit-fields:** GCC places a bit-field of an aligned typedef by the
  typedef's alignment, clang by its type's. And GCC 16 gives a `_BitInt(128)`
  bit-field of all 128 bits, at an offset 16 divides, TImode's alignment, 16,
  which raises its record's: `struct { _BitInt(128) x : 128; char c; }` is 32
  bytes aligned to 16 there, and 24 aligned to 8 to clang (and to GCC with a
  127-bit field, or with the field at 8).
- **Declarators' attributes:** GCC applies a declarator's attributes before
  the `vector_size` of its declaration's specifiers, dropping the `aligned` of
  `typedef long __attribute__((vector_size(8))) *T __attribute__((aligned(32)))`
  and the `packed` of a member `unsigned char __attribute__((vector_size(16)))
  m __attribute__((packed))`, which it sees on an `unsigned char`.
- **A crash:** GCC 16's `bitintlower` pass fails on a `_BitInt` as wide as a
  vector register copied into place with `memcpy` before a call whose result
  is an aggregate, so the test's C loads such an argument as its type.

## Tests

`./build/compiler test-calling-convention [--seed N] [--count N]
[--cross-check] [--signatures N] [--clang PATH] [--gcc PATH] [--keep]`.
`--seed N` changes every random input, `--count N` the random worlds' sizes
and how many random calls are assigned (2,000 by default), `--signatures N`
the cross-check's signatures per run (400), `--clang` and `--gcc` its
compilers, and `--keep` keeps its files. The counts below are the defaults'
(seed 1).

- **Merge** (192 checks): clang's `merge` and `postMerge` written again as
  clang writes them, against the module's on every pair of classes and both
  kinds of size; the module's merge must equal clang's but where clang stops
  at MEMORY.
- **Fixtures** (1,204 checks): about 115 types whose classes, pass bits and
  results clang's IR shows (`clang -O1 -S -emit-llvm` of functions that take
  and return them, read for `byval`, `sret`, the coerced types and the
  attributes), at each of the three vector widths, by the vector path and by
  the twin: every scalar, `_BitInt` and complex type, `__fp16`, `nullptr_t`
  and `_Atomic` ones, small and wide vectors, vectors of one element, and
  records with bit-fields, padding, packing, alignment, flexible and `_Atomic`
  members, x87 members in unions, wide vectors in records and unions, nesting,
  `i128` storage (`_BitInt`s and bit-fields in structs and unions, runs of
  bit-fields that are an `i128` or, packed, followed by a member or split at a
  byte, not, and unions whose struct holds a zero-length array of a larger
  record, plain, nested, packed or with a run of bit-fields that is an `i128`
  or bytes, which ties with an `__int128` or doesn't) and narrow low parts
  (`{__bf16, _Alignas(8) char}` and its kin). Then eleven calls whose
  locations clang and LLVM give, with their totals: an `UNCOUNTED` `nullptr_t`
  that splits a struct; a `PAIR` on the stack with r9 taken by the `long`
  after it; a high eightbyte alone on the stack; an `UNCOUNTED` `_Atomic
  double` in xmm7; a hidden pointer; a struct of a flexible array member
  alone, in an 8-byte slot while a general register is free and in none after
  (an `i0`); a `LOW_GENERAL` struct in two general registers; a union stored
  as an `i128`, counted as two registers, on the stack after five `long`s; and
  after an `UNCOUNTED` `_Atomic int` and four `long`s, a packed run of
  bit-fields split between r9 and the stack, and a run that is an `i128` on
  the stack with r9 taken by the `long` after it. And `va_arg`'s plan for a
  `long` aligned to 32, which reads the overflow area aligned to 8.
- **Worlds and the oracle** (81,607 offsets compared, 125,274 checks): random
  worlds of types (scalars, vectors, complex types, `_BitInt`s, `_Atomic` and
  aligned types, and records and unions of them, small ones mostly, with
  bit-fields, arrays, nesting, packing, alignment and flexible array members,
  and in clang's worlds now and then an array of no elements of any type, a
  large record's among them), one at each vector width and one shaped for GCC,
  classified by the vector path into one table and by the twin into another,
  which must be identical, column by column (rows, pass bits, stack bytes,
  alignments, `storage`, `layout` and results). Each classified type at each
  offset it can be at must then have the oracle's classes: clang's `classify`
  transcribed as clang has it, its recursion over members and elements a stack
  of frames, from the type table alone (no rows); and as an unnamed argument
  too. Then the twin classifies every type again, all at once with everything
  they read classified, and its table must not change: the paths share the
  levels, so only this shows a type classified before what it reads (without
  the wait for `abi_wide_alignment`'s types, seeds 4 to 10 fail it).
- **Assignment** (33,294 arguments of random calls, 236,049 checks): random
  calls over a world's types, at each vector width, named and variadic, short
  and past the registers, with results in memory and in registers, assigned by
  the vector path and by the twin, which must give the same locations and
  totals; calls around the vector path's most arguments, which both paths must
  assign alike: 65,542 `long`s (the last six of which would take rdi to r9
  again if the vector path kept them), 32,767 `__int128`s after a hidden
  pointer (65,535 general registers in its sum) and 32,768 without one, and
  the same of `_Complex double`; and the vector path on every prefix of up to
  40 arguments, with the argument and location columns ending at a `PROT_NONE`
  page.

### The cross-check

`--cross-check`, contract 4 on #135, needs no front end. For each compiler
(clang and GCC; `--clang`, `--gcc`) and each vector width it builds a world of
types (600 random ones beside the builtin, `_BitInt` and vector ones),
signatures over its passable ones (`--signatures`, 400 by default), and their
locations from `abi_assign`. clang's first five are directed
(`abi_test_world_directed`): `(nullptr_t, long × 4, R, long)` for five structs
R of runs of bit-fields wider than an eightbyte (an `i128`, packed, followed
by a `char`, two bit-fields in one run, and a packed `_BitInt(128)` one),
which the uncounted `nullptr_t` leaves one general register where clang counts
two; the rest are random, with up to 21 named arguments and 6 unnamed, a fifth
of the results `void`:

- **The C file** has the world's typedefs, written by `c_types_test.c`'s
  oracle, every signature's argument and result bytes (each value a valid
  representation of its type, floating values finite so that no copy through a
  register quiets a NaN's payload, with a mask of its meaningful bits: no
  padding, nor a `_BitInt`'s bits past its width, nor bytes clang doesn't
  pass), and per signature a C callee, which checks its parameters and its
  `va_arg`s, a C caller of our callee, which checks what our callee stored of
  its arguments, the al it got and the result it returned, and `main`, which
  runs them and checks what our caller stored of the C callee's result and the
  hidden pointer it got back.
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

The signatures leave out `_Atomic` types themselves (the fixtures check
them; records of `_Atomic` members are in), and the unnamed arguments leave
out the types the default argument promotions change, `nullptr_t` and
`_Atomic` ones, types wider than 16 bytes that are SSE, SSEUP as unnamed
arguments, types of no size, whose calls and `va_arg`s clang passes
differently (see "va_arg"), and `LOW_GENERAL` ones. clang's signatures leave
out every type that holds an array of records of no size
(`abi_test_empty_arrays`): clang 23 crashes on one after a `float`, a `half` or
a `bfloat` (`struct { long l; _Float16 h; struct {} e[1]; }` passed by value),
whose `GetSSETypeAtOffset` looks at the bytes after it, finds the array there
and divides by its element's LLVM size, 0. Two kinds of argument
become `long`s, since clang disagrees with itself on them: one that clang
would pass as an `i0`, which its passes crash on now and then, and an unnamed
one that clang's `va_arg` reads elsewhere than its call puts it
(`abi_test_va_disagreement`, from `abi_va_arg`'s plans and the named
arguments' totals; see "va_arg"), which is also where the cross-check runs
`abi_va_arg`. GCC's worlds leave out what "Where GCC passes otherwise" lists.
The cross-check doesn't see a missing extension (each side compares a value's
own bytes), which the fixtures' `SIGN` and `ZERO` check instead. The files go
in a directory under `$TMPDIR` (or `/tmp`), removed unless `--keep` or a
failure.

With the defaults (seed 1), each run has 400 signatures of 2,200 to 2,600
arguments, and the six runs make about 33,700 checks; on the 9700X the whole
`--cross-check` takes 18 s. Seeds 1 to 100 pass, with clang 23.1.3 and GCC
16.2.1.

## Measurements on the 9700X

`benchmark-calling-convention` (AGENTS.md has its options) on the Ryzen 7
9700X, pinned to CPU 2, with clang 23.1.1's `-O3 -march=native` build of this
module as it is now, each process run under the host's lock on its own. A
process gives the medians of 21 passes that take turns going first, per type
for the classification and per argument for the assignment. The table has the
medians of three processes in the default state, with their range, and the
times of two processes that time each path alone, back to back (`--path`),
and two that run the twin for 100 ms before each sample of the vector path
(`--after-scalar 100`):

| Case | Path | ns | cycles | instructions | IPC | alone | after 100 ms |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |
| classify-all, 16 B | vector | 56.55 [55.81–56.75] | 313.5 | 803 | 2.56 | 56.42 | 55.98 |
|  | scalar | 77.45 [76.76–78.37] | 429.2 | 1,519 | 3.54 | 76.75 | 76.84 |
| classify-all, 32 B | vector | 62.17 [61.36–62.54] | 344.5 | 841 | 2.44 | 61.52 | 61.45 |
|  | scalar | 78.28 [77.87–79.07] | 434.1 | 1,524 | 3.51 | 77.15 | 77.56 |
| classify-all, 64 B | vector | 66.37 [65.73–68.06] | 367.8 | 899 | 2.44 | 65.78 | 66.45 |
|  | scalar | 78.30 [78.06–79.68] | 434.0 | 1,527 | 3.52 | 77.25 | 77.86 |
| batches of 1 | vector | 50.07 [49.77–52.88] | 277.6 | 964 | 3.47 | 49.57 | 51.58 |
|  | scalar | 63.94 [63.83–64.18] | 354.2 | 1,459 | 4.12 | 63.13 | 63.66 |
| batches of 4 | vector | 50.16 [50.11–52.36] | 277.9 | 840 | 3.02 | 49.53 | 51.30 |
|  | scalar | 65.55 [65.54–65.97] | 363.3 | 1,437 | 3.96 | 64.17 | 65.40 |
| batches of 16 | vector | 46.36 [46.35–47.50] | 257.0 | 764 | 2.97 | 45.61 | 47.05 |
|  | scalar | 64.15 [63.95–64.19] | 355.6 | 1,429 | 4.02 | 62.52 | 63.97 |
| assign | vector | 4.81 [4.81–4.81] | 26.7 | 55 | 2.05 | 4.75 | 4.81 |
|  | scalar | 7.40 [7.40–7.40] | 41.1 | 146 | 3.55 | 7.35 | 7.41 |
| assign-simple | vector | 4.07 [4.07–4.08] | 22.6 | 48 | 2.11 | 4.05 | 4.08 |
|  | scalar | 6.87 [6.87–6.88] | 38.1 | 149 | 3.91 | 6.84 | 6.88 |

The twin's time over the vector path's, default state: 1.37, 1.26 and 1.18 for
`classify-all` at 16, 32 and 64 bytes; 1.28, 1.31 and 1.38 for batches of 1, 4
and 16 types (1.00, 2.76 and 8.17 types per call); 1.54 for `assign` and 1.69
for `assign-simple`.

- **The classification's table:** 32,756 types to classify, 26,779 of them
  records (4,383 unions) with 107,818 members, in 13 levels of nesting; 18,045
  of the records are MEMORY at 16 bytes. Up to about 4,096 types the branch
  predictor learns the table from one run to the next, and both paths look
  1.4 to 2.3 times as fast as they are; from 16,384 types on the costs stay
  within about 10%, hence the default of 32,768.
- **Where the classification's time goes:** in the finishing step both paths
  share (`abi_finish`, 60% of the vector path's instructions in a profile)
  and the level scans; the fold's merges are a few percent. clang's LLVM
  types (`storage` and `layout`) cost both paths about 400 instructions per
  type: before them, `classify-all` at 16 bytes took 396 instructions per
  type on the vector path and 1,097 on the twin (34.75 and 57.07 ns, 1.64
  apart). Working out layouts only for types of 16 bytes or less, the only
  ones read, took 280 of those instructions off both paths.
- **The assignment:** the vector path leaves 567 of the 32,768 calls (1.7%,
  with 5.2% of the arguments) to the twin: 219 for a register its sums count
  as used though it is free, 364 for padding on the stack and 18 for an
  `EMPTY` argument there; `assign-simple` leaves none. With its fit testing
  both classes for every argument, as it did first, it left 23% of the
  arguments, and was 1.12 times the twin.
- **The power cap (#199):** the throttle takes 0.02 to 0.06 of the vector
  paths' dispatch slots per cycle, less than the dataflow solver loses on its
  large CFGs alone and uncapped (0.11 a cycle, from `docs/dataflow.md`'s
  table), against its 1.9 after scalar code. These are short AVX-512 phases,
  too light to need a state of their own: the clock is 5.54 GHz in every
  state, and the states' medians agree within 3%. Of the two processes after
  the twin, one ran the batches as the default state does and the other 6%
  slower, at the same clock and with 0.05 of the slots a cycle withheld,
  too few to account for it.

## Next steps

- **#136's use:** call sequences, prologues and `va_arg` from these
  locations, and Tier 0's depth-to-register order (contract 6 on #135).
- **Types the module doesn't place yet**, which the cross-check's worlds
  leave out: an `_Atomic` of a type of no size, which clang's LLVM type makes
  `{T, [1 x i8]}` (16 bytes for `_Atomic(struct { __int128 x[0]; })`, whose C
  type is 1), and which a packed record holding one passes `byval` at LLVM's
  size; and an `_Atomic` of an `ext_vector_type` that LLVM splits into its
  elements (`_Atomic <5 x i8>` goes as five `i8`s, a general register each).
  Until they are modeled, the code generator should refuse them, as it does
  `UNSUPPORTED` types.
- **The finishing step:** `abi_finish` is scalar and shared by both paths,
  and with clang's LLVM types it is most of the vector path's time (see
  "Measurements on the 9700X"). Its work per member (the storage, the
  layout) is a reduction over the members, like the fold's merge, and could
  move into the fold.
- **The assignment's setup:** its loops over each call's arguments (the lane
  flags and call numbers), whose trip counts are random, take about 30% of
  the vector path's time in a profile of the benchmark, mostly at their
  mispredicted exits; the same columns could come from the counts' prefix
  sums without a loop per call.
- **Plain `char`:** `abi_extension` takes it as signed, as x86-64 has it;
  `-funsigned-char` would need a setting the type table doesn't have yet.
