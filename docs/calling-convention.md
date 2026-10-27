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
  nothing, registers (rax and rdx, xmm0 and xmm1, st0 and st1, the vector
  registers LLVM splits a vector into, or those of the elements it scalarizes
  one into: rax, rdx and rcx, or xmm0, xmm1 and st0), or memory, whose address
  the caller passes in rdi and the callee returns in rax.
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
| 9 | `PAIR` | one 128-bit integer value (clang's `i128`), which LLVM never splits between a register and the stack; with a NO_CLASS or SSE high eightbyte too (an `i128` a union stores), which clang counts and passes as two INTEGER eightbytes |
| 10, 11 | `SIGN`, `ZERO` | clang's `Extend`: the caller extends the value to 32 bits; for a result, what clang's IR asks of the callee, which LLVM's x86-64 code doesn't do (see "Extension") |
| 12 | `AGGREGATE` | clang's `byval`: in memory as an argument, a copy on the stack (or an integer value: see "Two counts") |
| 13 | `END_MEMORY` | a type of no size that is MEMORY at offset 16 though not at 0 |
| 14 | `UNSUPPORTED` | what the module doesn't place as clang and LLVM do, which the code generator refuses: an `UNCOUNTED` value LLVM splits into more parts than a location holds, as no class says (an `_Atomic` vector wider than 16 bytes and than the target's vector registers, or one LLVM breaks into its elements, and an integer of more than 128 bits); a vector SSE, SSEUP by its classes that LLVM breaks up (of bools or of `_BitInt`s narrower than a byte, of long doubles, of two or more `__float128`s, of `_BitInt`s wider than 128 bits), and a record or an array of one such member, which clang passes as it; and one of `_BitInt(128)`s, whose `i64`s LLVM puts in general registers once clang has no vector register for it; a type holding a `LARGER` one that clang lays out otherwise than the C type, or whose LLVM size the module doesn't keep; and one whose INTEGER high eightbyte clang reads as an `i128` from offset 8, under an SSE or NO_CLASS low one (see "Where clang's LLVM types decide") |
| 15 | `CLASSIFIED` | the row and these bits are in the table |
| 16 to 18 | `VALUE` | the kind of value a scalar in memory passes as: a general register (`GENERAL`), two (`PAIR`), a vector register (`VECTOR`), x87's (`X87`), which always goes on the stack, so that such a type isn't `UNCOUNTED`, or two halves, each in a general register while one is left and then on the stack (`HALVES`: a `<1 x i128>`, which LLVM splits as it never does an `i128`) |
| 20 to 25 | `ALIGNMENT` | the log2 of the stack slot's alignment: 3, or the type's own if larger (an aggregate's own, an `_Atomic` one's canonical type's, a scalar's value's natural one) |
| 26 | `EMPTY` | a type of no size: in memory it takes an 8-byte slot, or none as an `i0` (see "Two counts") |
| 27 | `LOW_GENERAL` | the low eightbyte, SSE by its class and counted so by clang, goes in a general register: clang's type for it is narrow and the high part's isn't aligned to 8 (see "Where clang's LLVM types decide") |
| 28 | `GROWN` | a `LARGER` type of 8 bytes or less, aligned to 8 or less, whose copy takes more than 8 bytes, so that its slot depends on clang's count: a `byval` copy of `stack_bytes` while clang has a general register free, and once it has none an integer of its size (`getIndirectResult`), in a general register LLVM has left or 8 bytes of the stack |
| 29 | `LARGER` | clang's LLVM type for it is larger than its C type, and `stack_bytes` that type's size, which a `byval` copy takes, rounded up to 8: an `_Atomic` of a type of no size, and a struct that ends in one running past its end (see "Where clang's LLVM types decide") |
| 30 | `SMALLER` | clang's LLVM type for it is smaller than its C type, and `stack_bytes` that type's size, rounded up to 8: a vector whose elements LLVM packs in memory (of bools, of `_BitInt`s narrower than a byte, of long doubles), whose objects clang makes that small though `sizeof` says more. A copy of it takes those bytes, and so does what a callee stores through a hidden pointer; the rest of the C type is padding (see "Where clang's LLVM types decide") |

Beside the pass bits, per type: `stack_bytes`, the slot's size (the type's
size, or a `LARGER` or `SMALLER` type's LLVM size, rounded up to 8; 8 for a
type of no size, as LLVM gives a `byval` of no size 8 bytes; `UINT32_MAX` for
4 GB or more), `alignment_log2`, its canonical alignment (an aligned typedef
looked through, as clang's canonical types have no typedefs, an array's
element's, and an `_Atomic` type's as of its value without one), and
`results`, its `AbiReturn`. An aligned typedef (`ALIGNED`) passes as its base:
`abi_copy` gives it its base's row, bits, slot, result and LLVM type.

An uncounted atomic vector uses its legalized register type's overflow slot.
Genuine short vectors widen to a 128-bit type, including a single `_Float16`
or `__bf16`, so their slot is 16 bytes aligned to 16 even when the memory
object is smaller. A vector of a single ordinary scalar of at most 8 bytes keeps that scalar's
8-byte slot, and a builtin-bool vector keeps its integer representation.
Vectors of an enum backed by bool retain `<N x i1>` and therefore take the
vector slot. This is register spilling, without the `LARGER` copy flag.
`abi_test_atomic_vector_overflow` checks two spilled values after eight
vector arguments and six integer arguments, including the following integer:
16-byte slots put it at offset 32, while the scalar controls put it at 16.
The rule follows LLVM's `getRegisterTypeForCallingConv` and
`CC_X86_64_C` at [LLVM revision 0d261d1](https://github.com/llvm/llvm-project/blob/0d261d1ca552c95a8f007e061c787ac7132fbcbc/llvm/lib/Target/X86/X86CallingConv.td#L477-L554).


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
  `_BitInt` of 65 to 128 bits as LLVM stores it; a wider one is an integer of
  its size, aligned to 16, where that is a multiple of 16 bytes, and otherwise
  an array of its bytes, aligned to 1 (`_BitInt(129)` is `[24 x i8]`). A
  struct's storage at offset 0 is its first member's there, and the
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
  bit-fields' types are aligned, which LLVM's units may not be (`struct { int
  a : 3; }` is `{ i8, [3 x i8] }`, aligned to 1). A union is stored as the
  member LLVM aligns
  most, then the largest, the first of equals
  (`CGRecordLowering::lowerUnion`); a bit-field there is an `iN` of its width,
  or of its type's bits if fewer, rounded up to bytes (`getIntNType`: 121 to
  128 bits make an `i128`). An array, a complex type, an enum and an `_Atomic`
  type take their element's, underlying type's or value's, never
  `STORAGE_EMPTY`. A record larger than 16 bytes gets none, and is
  `STORAGE_INEXACT`: only a type of 16 bytes or less passes by these columns,
  and such a type holds a larger record only as an array of none of them, a
  zero-length array or a flexible array member (a larger record is never
  empty, which takes a size of 0). clang keeps that array in its LLVM type, as
  `[0 x %struct.big]`, aligned as the record, so the array's storage is the
  record's alignment, which counts in the smaller record's alignment and in a
  union's choice of its storage member; the array works it out when it is
  finished (`abi_wide_alignment`: the record's members' storage alignments, a
  bit-field's run's unit's among them, a larger record walked in turn on an
  explicit stack, or 1 if packed), and waits in the levels until everything
  the walk reads is classified. The walk keeps the records it has finished,
  a sparse set in scratch memory, and takes each distinct one once, however
  many paths lead to it: `union W1 {W0 a, b;}`, `union W2 {W1 a, b;}` and so
  on have 2^n of them. `union {struct {long a, b; struct {__int128 x;
  long y;} z[0];} s; __int128 i;}` passes as two `i64`s (the struct is aligned
  to 16 and comes first) where `union {__int128 i; ...}` passes as one `i128`.
  `STORAGE_INEXACT` also marks a record with a flexible array member and a
  size, which gets no storage either (its members may be classified after it:
  see "Levels"), and what holds one, a record the walk takes among them: its
  alignment may not be LLVM's. It marks a type aligned past the 2^15 the
  storage holds too, and what holds one: LLVM aligns a vector as large as
  itself, so one of 64 KB or more. An `_Atomic` of a type of no size takes its
  value's alignment as its size, and is refused for an inexact one. A type of
  16 bytes or less holds such a type only through an array of none of them,
  and passes by that alignment unchecked. A vector is aligned as in C, but a
  vector of bools is the integer of its bits clang stores it as (aligned as
  LLVM aligns integers, an `i128` at 128 bits), and one of long doubles or of
  `_BitInt`s narrower than a byte, whose elements LLVM packs, is aligned to
  its bytes rounded up to a power of two (`abi_vector_llvm`).
  Short bit-field runs of eight bytes or less also propagate `STORAGE_INEXACT`
  through both the scalar storage calculation and vector parts, and through the
  wide-record walk. Their access units remain a separate follow-up. Wide
  `_BitInt` storage uses LLVM's memory type: alignment 16 for sizes divisible
  by 16 bytes, and a byte array aligned to 1 otherwise. An enum backed by bool
  keeps vector storage; only a vector written with builtin bool elements uses
  the atomic integer-coercion rule.
- **`layout`**, 8 bytes, which only a type of 16 bytes or less gets, since
  only such types read their own and their members' and elements': the scalars
  of the LLVM type, as `GetSSETypeAtOffset` and `GetINTEGERTypeAtOffset` find
  them (`abi_layout`): a 4-bit kind at each even offset (`i8` to `i64`,
  `half`, `bfloat`, `float`, `double`, or another), and which of the 16 bytes
  hold the C type's data (`BitsContainNoUserData`'s view, which takes a
  bit-field as its whole declared type, not its width). A struct's padding
  holds `i8`s, as the byte arrays clang pads with do; its bit-fields are an
  integer of their type at the start of their storage unit, an approximation
  of clang's access units. A union's scalars are its storage member's. A
  vector is another kind of scalar, but a vector of bools is its integer: an
  `i8` for 8 bools or fewer, an `i16`, `i32` or `i64` for 16, 32 or 64, and
  another for an `i9` and the like, which `GetINTEGERTypeAtOffset` doesn't
  take as one of those.

### Results, locations and calls

`AbiReturn`, 8 bytes:

- **kind:** `IGNORE` (void, or nothing to return), `REGISTERS` or `MEMORY`
  (the caller passes the result's address in rdi, and the callee returns it in
  rax);
- **registers[4]**, in order, each holding `part_bytes` of the value, part `i`
  at byte `i × part_bytes` (past the low eightbyte with `HIGH`): an eightbyte,
  what a vector register holds of it, or for an x87 register 16 bytes for an
  80-bit value, 10 for one of a vector of long doubles (packed 10 bytes
  apart), and 8 for a double (the third of a `<3 x double>`, after xmm0 and
  xmm1);
- **flags:** `SIGN` or `ZERO` (clang's IR has the callee extend to 32 bits,
  which neither its x86-64 code nor GCC's does: a caller extends a narrow
  result itself, as "Extension" says), `HIGH` (the
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
   what the union takes from it. A struct's storage word and a union's layout
   word, in bits neither uses otherwise, also say whether the member keeps the
   record from being `STORAGE_EMPTY` and whether its storage is
   `STORAGE_INEXACT`. The words are ORed, but a union's storage
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
element (an array's), value (an `_Atomic` type's, whose storage and layout it
takes) or members (a record's, but a flexible array member, which is no
complete type: its element) are classified, and for an array of none of a
record larger than 16 bytes (a zero-length array, or a flexible array member
of a record of no size) every type whose storage `abi_wide_alignment` reads,
classified together by `abi_classify`, then the next level, until none is
left. Each of these is complete when the type is, so no type waits forever.
(When an `_Atomic` type didn't wait for its value, its storage and layout
depended on whether the value happened to be finished before it, and the two
paths could differ.) A record whose row doesn't depend on its members (larger
than 64 bytes, or with a flexible array member) waits for them only where a
member can make its copy `LARGER`: when the table has a `LARGER` type
already, or the call classifies an `_Atomic` of a type of no size, the root
every `LARGER` type holds. Waiting would add a level for every nesting of
such records: about 140 more instructions per type in the benchmark's
classification. A flexible record of no size always waits, since an `_Atomic`
of it in a later call takes its alignment as its size. `abi_classify` takes a
level whose types' bases, elements, values and members are classified, as a
caller's own levels would have them, and needs no such rule. Without scratch
memory for the lists `abi_classify_all` classifies a type at a time with the
twin, in passes over the table, until a pass classifies nothing.

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
  (`union { int a; unsigned _BitInt(128) b : 127; }`) or SSE (a `struct {
  long l; double d; }` beside the struct of an unnamed bit-field that holds
  the `i128`) is a `PAIR` too, with `HIGH_INTEGER` and without `HIGH_SSE`:
  `classifyArgumentType` returns the `i128` an INTEGER low eightbyte reads
  before it looks at the high one, and clang counts and passes two INTEGER
  eightbytes. Under an SSE or NO_CLASS low eightbyte, an INTEGER high one
  reads the `i128` from offset 8, which the module refuses (`UNSUPPORTED`);
- **`LOW_GENERAL`** when the low eightbyte is SSE but clang's type for it, by
  `GetSSETypeAtOffset` over `layout`, is narrow (a `bfloat`, `<2 x half>` or
  `<2 x bfloat>`) and the high part's isn't aligned to 8 (`abi_low_general`):
  `GetX86_64ByValArgumentPair` widens such a low part to an `i64`, which goes
  in a general register;
- **`SIGN` or `ZERO`** for `bool`, the character and short types, enums of
  them and `_BitInt`s narrower than `int`, by their signedness (plain `char`
  is signed on x86-64): clang's `isPromotableIntegerTypeForABI`;
- **`UNCOUNTED`** and the value's kind for `nullptr_t` and `_Atomic` scalars
  in memory (an `_Atomic` vector of bools is the integer of its bits clang
  stores it as), and `UNSUPPORTED` for one that LLVM splits into more parts
  than a location holds (`abi_value_splits`);
- **the slot's alignment**: an aggregate's or an illegal vector's own
  alignment, as clang's `byval` takes the larger of it and 8, and a scalar's
  value's natural one (an enum's integer type's, not its aligned attribute's),
  as LLVM's stack slots for its IR types have. An `_Atomic` aggregate's is
  its canonical type's, `getTypeInfo`'s for an `_Atomic` of its value without
  a typedef (`abi_atomic_alignment_log2`): the value's for one of no size or
  past 16 bytes, its size rounded up to a power of two otherwise. So for
  `_Atomic(T)` with `T` an aligned(64) typedef of a struct of 32 bytes, as for
  one of an empty struct, the typedef's attribute doesn't count, and the slot
  is aligned to 8;
- **the slot's size and `LARGER`** (`abi_copy_bytes`): the bytes of clang's
  LLVM type, which a `byval` copy takes, where they aren't the C size;
  `GROWN` where that copy passes 8 bytes for a type of 8 or less, aligned to
  8 or less; and `UNSUPPORTED` where the module can't give them (see "Where
  clang's LLVM types decide"). While the table has classified no `LARGER`
  type (`AbiTable.larger`), no struct holds one, and `abi_finish` doesn't look
  at a struct's members for one;
- **a vector's LLVM type** (`abi_vector_llvm`): the storage, layout, copy
  (`SMALLER`), result and `UNSUPPORTED` of a vector LLVM packs or breaks up
  (see "Where clang's LLVM types decide"), and `UNSUPPORTED` with its result
  for a record or an array SSE, SSEUP of one such member, which clang passes
  as that member (`abi_single_member`: `isSingleElementStruct`, which skips
  unnamed bit-fields and members of no class of 16 bytes or less, as
  `isEmptyField` does). Only these, `_Atomic` types and, once the table has a
  `LARGER` type or a refused vector (`AbiTable.rare`), records and arrays take
  this rare path, `abi_finish_rare`, out of line;
- **the result** (`abi_result`, clang's `classifyReturnType` with LLVM's
  registers): `IGNORE`; `MEMORY` for an aggregate in memory (`sret`); for a
  scalar in memory, the registers LLVM returns its value in
  (`abi_value_result`): rax, rax and rdx for an `i128` and for a `<1 x i128>`,
  a vector of one element where its element would go (LLVM scalarizes `<1 x
  T>`), st0, or as many of the target's widest vector registers as the value
  needs, which LLVM splits it into (`RetCC_X86Common` has four of each width),
  and `MEMORY` with `DEMOTED` for a vector that would need more than four, or
  of `__int128`s wider than 16 bytes, which LLVM returns through a hidden
  pointer (and for the vectors LLVM breaks up, `abi_vector_llvm`'s registers
  or hidden pointer); st0 for X87; st0 and st1 for COMPLEX_X87; one vector
  register for SSE, SSEUP; otherwise an eightbyte a register, INTEGER in rax
  then rdx and SSE in xmm0 then xmm1 (a `LOW_GENERAL` low eightbyte in rax,
  and a `PAIR`'s high one in rdx, NO_CLASS or not), with `HIGH` when the low
  one is padding;
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
  8 bytes of the stack (a `GROWN` one too, whose copy would take more); one
  of no size is an `i0`, which takes none (`EMPTY`).

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
   its needs: a stack slot of its size (a `LARGER` one's LLVM size), aligned
   to its slot's alignment; but with clang's general count at 0, an aggregate
   of 8 bytes or less aligned to at most 8, `GROWN` ones among them, takes
   LLVM's next general register, or an 8-byte slot, or nothing if it is
   `EMPTY`.
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
takes rdi before it), `DEMOTED`, `VARIADIC` and `LONG` (more arguments than
its 16-bit register sums count), and `UNNAMED` at its first unnamed one. Past
the last argument, a `START` ends it.

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
- is `EMPTY` or `GROWN` on the stack, whose slot depends on clang's count;
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

clang's `va_arg` differs from clang's own calls in five places. The module
keeps both sides as clang has them in the first four, and takes GCC's memory
in the fifth (the vectors LLVM breaks up, which an unnamed argument's call
passes as values too, are refused: see "Where clang's LLVM types decide"):

- **`nullptr_t`**: the call passes it in a general register (`UNCOUNTED`),
  but its `va_arg`, which sees MEMORY, reads it from the overflow area.
- **A type of no size in memory** (a struct of a flexible array member alone):
  the call gives it an 8-byte slot (the `byval` minimum), its `va_arg` reads
  and skips none.
- **A `LARGER` or `SMALLER` type in memory**: the call gives it its LLVM
  type's copy (24 bytes for a packed struct of `_Atomic(struct { __int128
  x[0]; })`, 48 for a vector of 257 bools, of 64), and its `va_arg` skips the
  C size rounded up to 8 (`EmitX86_64VAArgFromMemory` takes `getTypeSize`),
  as `abi_va_arg`'s `stack_bytes` does.
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
  `va_arg` reads it from the save area. And a `WIDE` vector of
  `_BitInt(128)`s, MEMORY as an unnamed argument but no illegal vector: its
  call passes it as its value, `<N x i128>`, whose `i64`s go in general
  registers clang doesn't count (`rsi`, `rdx`, `rcx` and `r8` after an `int`,
  for two of them at `-march=x86-64-v3`), while its `va_arg` reads it from
  the overflow area. There is no convention there to follow but GCC's, memory,
  which the module takes for every unnamed `WIDE` type.

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
  `<1 x i128>` as two `i64`s; a vector of bools as the integer clang stores
  it as, `ConvertTypeForMem`'s; and refused where `getVectorTypeBreakdown`
  splits it into more values than a location holds, or promotes the elements
  of `_BitInt`s narrower than a byte); and the `SPLIT` and shifted arguments
  after them.
- **Vectors:** of 4 bytes or less INTEGER whatever their elements ("gcc
  passes the following as integer"); `<1 x long long>` SSE on Linux
  (`classifyIntegerMMXAsSSE`); of `__int128` and wider than 16 bytes MEMORY
  on Linux (`passInt128VectorsInMem`); of 32 and 64 bytes SSE, SSEUP only when
  named and within `vector_bytes`; illegal vectors (`IsIllegalVectorType`)
  `byval` in memory and returned as values.
- **In memory:** an aggregate or an illegal vector as a `byval` copy aligned
  to the larger of 8 and its alignment (`getIndirectResult`), of its LLVM
  type's alloc size (`getTypeAllocSize`), at least 8 bytes
  (`CCIfByVal<CCPassByVal<8, 8>>`); `_BitInt`s as aggregates
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
  though both go in memory as arguments. A vector's elements, where LLVM
  scalarizes it, in rax, rdx and rcx for integers, xmm0 and xmm1 then st0 and
  st1 for doubles, xmm0 and xmm1 for `fp128`s, and st0 and st1 for `f80`s.
- **`va_arg`:** `EmitVAArg` and `EmitX86_64VAArgFromMemory`, which classify
  the canonical type as an unnamed argument and read the overflow area aligned
  to its alignment past 8.
- **clang's LLVM types:** `getIndirectResult`'s integers, which LLVM may give
  a register clang didn't count; `GetX86_64ByValArgumentPair`'s widened low
  parts, by `GetSSETypeAtOffset` and `GetINTEGERTypeAtOffset` over the
  record's LLVM type (`CGRecordLowering`); `CC_X86_64_I128`'s unsplit `i128`
  and its slot aligned to 16; results LLVM demotes to a hidden pointer
  (`CanLowerReturn`, then `sret` added by LLVM); `CodeGenTypes`'
  `{T, [1 x i8]}` for an `_Atomic` of a type of no size, and the capstone
  `CGRecordLowering`'s `insertPadding` leaves in a struct after an overrun;
  `getVectorTypeBreakdown`'s values for an `_Atomic` vector, and for a vector
  that `GetByteVectorType` passes as its own type (a record of one, through
  `isSingleElementStruct`) or `getIndirectReturnResult` returns as its value;
  `ConvertTypeForMem`'s integer for a vector of bools and LLVM's packed
  vectors of `x86_fp80`s and narrow integers, smaller than the C types; and
  LLVM's own placement of values clang didn't count (`CC_X86_64_C`): see
  "Where clang's LLVM types decide".

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
  `byval` rather than in r9. So it does with an SSE high eightbyte, since
  `classifyArgumentType` returns the `i128` an INTEGER low eightbyte reads
  before it looks at the high one: a union of `struct { unsigned __int128 :
  125; }`, which has no class and is an `i128` to LLVM, and `struct { long l;
  double d; }` passes in rdi and rsi, and a `double` after it takes xmm0. An
  INTEGER high eightbyte under another low one reads that `i128` too, from
  offset 8 (`GetINTEGERTypeAtOffset` returns an `i128` at any offset into
  it): after an SSE low eightbyte the value is `{i64, i128}`, three general
  registers that hold none of the high eightbyte's bytes, and alone it is an
  `i128` in two general registers that clang counts as one. The module
  refuses both (`UNSUPPORTED`). A run of bit-fields is an `i128` only where
  its 16 bytes fit (see "clang's LLVM types"): after an uncounted `_Atomic
  int` and four `long`s, `struct { unsigned __int128 x : 65; }` goes whole on
  the stack and a `long` after it takes r9, while packed (an `i64` and an
  `i8`) it is split between r9 and the stack, as is `{ unsigned __int128 x :
  65; char c; }`, and `{ unsigned __int128 a : 60, b : 10; }` is one `i128`.
- **Zero-length arrays.** clang keeps one in its LLVM type of a record, as
  `[0 x %struct.big]`, aligned as its element however large: `struct {long
  a, b; struct {__int128 x; long y;} z[0];}` is aligned to 16 there, so in a
  union with an `__int128` it ties, and the first of the two is the union's
  storage: two `i64`s, or one `i128` (`PAIR`). A packed element, aligned to 1,
  leaves the struct at 8, and the `__int128` wins wherever it is; so does an
  element whose run of bit-fields is bytes, as in `{unsigned __int128 x : 65;
  char c; long d;}`, which LLVM's type aligns to 8. An `_Atomic` of a struct
  of no size takes that alignment as its size: `_Atomic(struct {struct
  {__int128 x; char c[100];} z[0];})` is 16 bytes to LLVM, as
  `_Atomic(struct {__int128 x[0];})` is.
- **Demoted results.** A vector result that would need more than four
  registers, or of `__int128`s wider than 16 bytes, is a value in clang's IR,
  and LLVM's demotion adds the hidden pointer, which clang doesn't count
  (`DEMOTED`): for a `struct {long, long}` after four `long`s, returning
  `float __attribute__((vector_size(128)))` without AVX, clang splits the
  struct between r9 and the stack. GCC 16 returns such a vector through the
  psABI's hidden pointer, which it counts.
- **Vectors of one element.** LLVM scalarizes `<1 x T>`: an `_Atomic` vector
  of one `short` goes in a general register, and a `<1 x i128>` (of an
  `__int128` or a `_BitInt(128)`) splits into two `i64`s (`HALVES`), as a
  real `i128` never does. One of a wider `_BitInt` is more parts than a
  location holds (`UNSUPPORTED`).
- **LLVM's free registers.** Where clang counts registers that LLVM doesn't
  use (for a named `WIDE` vector of a variadic call, a `PAIR` on the stack),
  the values clang then sends to memory as values take the registers LLVM
  still has.
- **Copies larger than the C type.** clang's LLVM type for an `_Atomic` of a
  type of no size is `{T, [1 x i8]}` (`CodeGenTypes`; `getTypeInfo` gives
  such an `_Atomic` a byte), as large as LLVM aligns `T`: 16 bytes for
  `_Atomic(struct { __int128 x[0]; })`, of 1. A `byval` copy takes LLVM's
  size, a slot of 16 here (`LARGER`). Of a packed `T`, aligned to 1, it takes
  16 while clang has a general register free, and once it has none an `i8`'s
  8 bytes, since `getIndirectResult`'s integer has the C size (`GROWN`).
  `CGRecordLowering` lays out a struct's members at their C offsets with
  their LLVM types, and a capstone at its size. Where a member's type runs
  past the next member's offset, `insertPadding` records a negative padding
  at its end, which the sort puts after the capstone and `pop_back` drops in
  the capstone's place. So a struct that ends in such an `_Atomic`, which
  only a packed one can, is the `_Atomic`'s LLVM type and an `i8` past the
  `_Atomic`'s offset: `packed { char c; _Atomic(struct { __int128 x[0]; })
  a; }` is 18 bytes to LLVM, of 2, a slot of 24, and `GROWN`. Where a member
  follows the overrun, clang moves it and those after it to other offsets
  than `offsetof` gives. For `{ char c; _Atomic(...) a; char d; }` (64 bytes
  to LLVM, of 32), `p->d` reads byte 32 where `offsetof` says 17, a clang bug
  that only builds with assertions catch. The struct's LLVM size can also wrap
  past 2^64, as a negative padding left in it is
  `[18446744073709551601 x i8]`. There is no layout to follow, and the module
  refuses such a struct (`UNSUPPORTED`). It also refuses one holding a type it
  keeps no LLVM size of: an array of them, an `_Atomic` of a `LARGER` struct,
  or a `LARGER` struct, unless the next member leaves that struct's slot
  room; and an `_Atomic` of a type of no size whose alignment comes, through
  an array of none of them, from a record with a flexible array member and a
  size or from one that holds one (a larger record's comes from its members',
  `abi_wide_alignment`'s walk), or from a vector of 64 KB or more, aligned
  past the 2^15 the storage holds (`STORAGE_INEXACT`). A union is never
  larger: `lowerUnion` cuts its storage to its size.
- **Vectors LLVM breaks up.** clang passes a vector as its LLVM type, `<N x
  T>`: an `_Atomic` one always, as its value, and another where its classes
  are SSE, SSEUP (`GetByteVectorType`, `<2N x i64>` for `i128`s) and as a
  result in memory (`getIndirectReturnResult`). A record or an array of one
  such vector (`isSingleElementStruct`: but empty members, as large as it,
  through nested ones) passes and returns as the vector. Up to 8 bytes the
  classes coerce a vector that isn't `_Atomic` to an integer or a `double` of
  its size instead. `getVectorTypeBreakdown` gives `<N x T>` one vector
  register when LLVM has a vector type of N elements or of the next power of
  two: 16 bytes always, 32 and 64 within the target's registers, and v8f16 for
  16-bit floating elements, fewer than eight (X86's override). Otherwise LLVM
  passes each element as a value of its own:
  - `_Atomic` `<3 x i8>`, `<5 x i8>` to `<7 x i8>` and `<3 x i16>` go in a
    general register an element at every width; three of them return in al,
    dl and cl, more through a hidden pointer.
  - Two or more elements of 16 bytes or more go as `i64` halves, 16-byte stack
    slots or a vector register each. A vector of long doubles passes as
    `f80`s, each in a 16-byte stack slot, though clang counted a vector
    register (even for one of them, 16 bytes, which LLVM scalarizes), and
    returns in st0, in st0 and st1 (10 bytes apart, as clang packs them in
    memory: `part_bytes` 10), or through a hidden pointer. A vector of two or
    more `__float128`s passes an `fp128` in each of LLVM's next vector
    registers, where clang counted one for them all, and returns in xmm0 and
    xmm1 or through a hidden pointer. A vector of `_BitInt`s wider than 128
    bits passes as `i64`s in general registers (four for `<1 x _BitInt(256)>`
    at `-march=x86-64-v3`) and returns through a hidden pointer, as does one
    of `_BitInt(128)`s in memory, like one of `__int128`s.
  - A vector in memory whose length isn't a power of two returns an element at
    a time: three 8-byte integers in rax, rdx and rcx (`ext_vector_type(3)` of
    `long` at `-march=x86-64`), three doubles in xmm0, xmm1 and st0 (`fldl` of
    the third: `part_bytes` 8), and five floats or more through a hidden
    pointer, as no return registers are left.
  - A vector of `_BitInt(128)`s isn't an illegal vector (that takes
    `__int128`s), so once clang's count has no vector register for it, clang
    passes it as its value, `<N x i128>`, rather than `GetByteVectorType`'s
    `<2N x i64>`: after eight `double`s one goes in rdi and rsi (r9 and a
    stack slot with one general register left), two in four general registers
    at `-march=x86-64-v3`, four in six and two stack slots at `-v4`. A record
    of one passes as a copy then.

  No location holds more than two registers, nor gives clang's count of one
  vector register where LLVM uses others, so the module refuses these as
  arguments (`UNSUPPORTED`), as it does a vector wider than the target's
  registers, which LLVM splits across several, and keeps their results.
- **Vectors of bools and of narrow `_BitInt`s.** clang stores a vector of
  bools as an integer of its bits, at least 8 (`ConvertTypeForMem`): its
  storage and layout are that integer's. An `i8` (8 bools or fewer), `i16`,
  `i32` or `i64` is a leaf `GetINTEGERTypeAtOffset` takes, so that `struct {
  __bf16 a, b; _Alignas(8) bool v __attribute__((ext_vector_type(8))); }`
  widens its low part into rdi (`LOW_GENERAL`), which an `i9` for 9 bools
  doesn't; and 128 bools are an `i128`, so that a union of them and a `long`
  is one `PAIR`, in rdi and rsi. An `_Atomic` vector of bools passes as that
  integer. Up to 64 bits it is one general register: an `i10` goes in r9 after
  five `long`s, uncounted, and ax holds one as a result. From 65 to 127 bits
  it is two halves, which LLVM splits as it never does an `i128` (`HALVES`).
  128 bits are one `i128` (`PAIR`), and past them more parts (`UNSUPPORTED`).
  clang's atomic loads of most of them (every width that isn't 8, 16, 32, 64 or
  128 bits) crash its instruction selection, so a caller clang compiles can't
  pass one. Its callee compiles. A vector of bools that isn't `_Atomic` is
  `<N x i1>`: up to 8 bytes the classes' integer or `double`, and from 16
  bytes up N values, each an `i8` in a general register or an 8-byte slot (65
  bools take edi to r9d and 59 slots), returned through a hidden pointer that
  clang doesn't count (`DEMOTED`). A vector of `_BitInt`s narrower than a byte
  is `<N x iK>`, its elements packed in memory, and promoted to a byte each in
  a vector register (`<16 x i4>` in xmm0), or to 16 bits in an `_Atomic` one's
  (`<8 x i4>`): no copy of the C type's bytes gives those, and the module
  refuses them (`UNSUPPORTED`).
- **Copies smaller than the C type** (`SMALLER`). A vector of bools, of long
  doubles (`<N x x86_fp80>`, 10 bytes an element) or of narrow `_BitInt`s is
  smaller to LLVM than to `sizeof`: 48 bytes for 257 bools, of 64, 80 for 513,
  of 128, 32 for `ext_vector_type(3)` of long double, of 64, and 16 for 32
  `unsigned _BitInt(4)`s, of 32, each LLVM's bytes rounded up to a power of
  two (to 16 past 64 bits for an integer). A `byval` copy takes those bytes,
  and the arguments after it follow them (the `long` after 513 bools at 80).
  clang's own objects of these types are that small too: its locals take the
  LLVM type (`alloca i257` for 257 bools), so that a `memcpy` of `sizeof`
  bytes into one runs past it, and a callee must store no more than LLVM's
  bytes through a hidden pointer.

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
  typedef's alignment, clang by its type's. Clang checks that a bit-field fits
  its type's storage unit before it aligns the field as an `aligned` attribute
  on it says, and GCC after: a field aligned below its type's alignment, which
  crosses the unit only once aligned, is moved to the next unit by GCC and
  left across it by clang. So
  `struct { char c; unsigned long f : 45 __attribute__((aligned(4))); }` has
  `f` at byte 8 in GCC and at byte 4 in clang, and GCC's worlds give a
  bit-field no alignment below its type's. The plain test checks the rule
  without compilers (`abi_test_gcc_bit_fields`), whatever `--seed` says: on
  fixed fields, then on worlds from the fixed seeds 1 to 16, where GCC's must
  have no such field and clang's some. And
  GCC 16 gives a `_BitInt(128)` bit-field of all 128 bits, at an offset 16
  divides, TImode's alignment, 16, which raises its record's:
  `struct { _BitInt(128) x : 128; char c; }` is 32 bytes aligned to 16 there,
  and 24 aligned to 8 to clang (and to GCC with a 127-bit field, or with the
  field at 8).
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
[--cross-check] [--signatures N] [--clang PATH] [--gcc PATH] [--buster PATH]
[--keep]`. `--seed N` changes every random input, `--count N` the random
worlds' sizes and how many random calls are assigned (2,000 by default),
`--signatures N` the cross-check's signatures per run (400), `--clang` and
`--gcc` its compilers, `--buster` the compiler of our side's C in the compiled
runs (this executable, `/proc/self/exe`, by default), and `--keep` keeps its
files. The counts below are the defaults'
(seed 1).

- **Merge** (192 checks): clang's `merge` and `postMerge` written again as
  clang writes them, against the module's on every pair of classes and both
  kinds of size; the module's merge must equal clang's but where clang stops
  at MEMORY.
- **Fixtures** (3,158 checks): types whose classes, pass bits and
  results clang's IR shows (`clang -O1 -S -emit-llvm` of functions that take
  and return them, read for `byval`, `sret`, the coerced types and the
  attributes), at each of the three vector widths, by the vector path and by
  the twin: every scalar, `_BitInt` and complex type, `__fp16`, `nullptr_t`
  and `_Atomic` ones, small and wide vectors, vectors of one element, and
  records with bit-fields, padding, packing, alignment, flexible and `_Atomic`
  members, x87 members in unions, wide vectors in records and unions, nesting,
  `i128` storage (`_BitInt`s and bit-fields in structs and unions, runs of
  bit-fields that are an `i128` or, packed, followed by a member or split at a
  byte, not, unions of a struct of an unnamed bit-field, one `PAIR` under an
  SSE high eightbyte too and refused with an INTEGER high eightbyte under
  another low one, and unions whose struct holds a zero-length array of a
  larger record, plain (of 32 bytes, and of 80, whose row doesn't wait for its
  members), nested, packed or with a run of bit-fields that is an `i128` or
  bytes, which ties with an `__int128` or doesn't, and four whose larger
  record one of `abi_wide_alignment`'s rules alone aligns to 1: a member off
  its own alignment, a record off its own, a size its alignment doesn't
  divide, and a union's members) and narrow low parts (`{__bf16, _Alignas(8)
  char}` and its kin). Then the `_Atomic` types clang's LLVM types decide,
  with the slots that `-mllvm -stop-after=finalize-isel`'s fixed stack objects
  show: `_Atomic`s of structs of no size, `LARGER` and `GROWN` ones among them
  (of a packed one, of a flexible array member's `unsigned _BitInt(127)`, of
  one aligned by an array of none of a record larger than 64 bytes, and of an
  array of none of a 32 KB vector or of a record holding one, 32 KB to LLVM),
  and of aligned typedefs of an empty struct and of a 32-byte one, aligned to
  8; packed structs ending in one (17 to 117 bytes to LLVM, and 32 KB and a
  byte), structs and a union with room for one, the C layout, and the structs
  refused (a member after the overrun, a member of no size or a flexible one,
  a struct ending in one, an `_Atomic` of one; `_Atomic`s of a struct and a
  union of an array of none of a struct with a flexible array member and a
  size, and of a struct of an array of none of a struct of 32 bytes holding
  one: `STORAGE_INEXACT`; and `_Atomic`s of structs of no size aligned by a
  vector of 64 KB, past what the storage holds, or by a record holding one,
  and a packed struct ending in the first); a union at offset 1 of an empty
  struct and an array of none of an `_Atomic` of an `aligned(16)` typedef of
  one (through an `aligned(1)` typedef), which passes as an `i8`, since clang
  checks a member's offset against its canonical type's alignment, and the
  canonical `_Atomic` drops its value's typedefs; and `_Atomic` vectors in one
  register (`<8 x i8>`, `<9 x i8>`, `<3 x half>`, `<17 x i8>` with AVX),
  refused (`<3 x i8>`, `<5 x i8>`, `<3 x i16>`, two `__int128`s, `long
  double`s or `__float128`s, a `_BitInt(256)`, `<17 x i8>` without AVX, eight
  `_BitInt(4)`s), in two halves (`<1 x _BitInt(128)>`), of one `_BitInt(4)` as
  its integer, and of bools (10 and 64 in a general register, 65 in halves,
  128 a `PAIR`, 129 refused). Then the vectors that aren't `_Atomic` which
  LLVM breaks up or packs (`abi_test_build_split_vectors`): of bools (8 in
  rax, 64 in xmm0, 65 refused, 129, 257 and 513 in memory where the target's
  registers don't hold them, copies of 32, 48 and 80 bytes, `SMALLER` but the
  first, and a hidden pointer as a result); records of 128 bools refused with
  them (a struct, a union, beside an `int[0]`, a struct of a struct, a struct
  of an array of one of 65), but not a union of them and a `float` vector or
  of 256 bools and a `char[20]`; a union of 128 bools and a `long` one `PAIR`,
  of 100 not; a struct of two `__bf16`s and 8 bools aligned to 8
  `LOW_GENERAL`, of 9 not; long doubles (one refused, st0; two in memory or
  refused, st0 and st1 10 bytes apart; three and four in memory, copies of 32
  and 64 bytes, or refused, through a hidden pointer; a struct of one refused,
  a union of one and a `double` not); `__float128`s (one in xmm0, two in
  memory or refused, xmm0 and xmm1, four through a hidden pointer); `_BitInt`s
  (one of 256 bits refused or in memory; one, two and four of 128 refused in a
  vector register, a struct of one not, and in memory through a hidden
  pointer; `unsigned _BitInt(4)`s: eight in xmm0, `SMALLER`, sixteen refused,
  32 a copy of 16 bytes); and lengths that aren't a power of two in memory
  (five floats and five doubles through a hidden pointer, three `long`s in
  rax, rdx and rcx, three doubles in xmm0, xmm1 and st0). Then thirty calls
  whose locations clang and LLVM give, with their totals: an `UNCOUNTED`
  `nullptr_t` that splits a struct; a `PAIR` on the stack with r9 taken by the
  `long` after it; a high eightbyte alone on the stack; an `UNCOUNTED`
  `_Atomic double` in xmm7; a hidden pointer; a struct of a flexible array
  member alone, in an 8-byte slot while a general register is free and in none
  after (an `i0`); a `LOW_GENERAL` struct in two general registers; a union
  stored as an `i128`, counted as two registers, on the stack after five
  `long`s; after an `UNCOUNTED` `_Atomic int` and four `long`s, a packed run
  of bit-fields split between r9 and the stack, and a run that is an `i128` on
  the stack with r9 taken by the `long` after it; the `LARGER` copies' slots
  after five and six `long`s (16 bytes for `_Atomic(struct { __int128 x[0];
  })`, 24 and 32 for packed structs ending in one, and a `GROWN` one's 16 or
  its integer's 8 by clang's count); and `_Atomic` bool vectors after five
  `long`s (an `i10` in r9, which leaves the `long` after it on the stack; an
  `i65` `SPLIT`; an `i128` whole on the stack, the `long` after it in r9); and
  a union stored as an `i128` with an SSE high eightbyte, counted as two
  general registers too, on the stack after five `long`s and in rdi and rsi
  before a `double` in xmm0; a vector of 4,096 bools, which isn't `_Atomic`,
  returned through the hidden pointer LLVM demotes it to, in rdi, with the
  `long` after it in rsi (and one of 1,024, at every width); the 32 KB copy of
  an `_Atomic` aligned to 32 KB after six `long`s, the `long` after it at 32
  KB, and a packed struct ending in it, 32 KB and a byte in a slot of 32,776
  after five `long`s, with r9 left for the `long` after it, and an `i8` after
  six; and after a result of two `__int128`s in a vector, which LLVM demotes
  to a hidden pointer clang doesn't count (`DEMOTED`), and five `long`s, a
  struct aligned to 16 that clang counts in a register, an `i64` at stack 0,
  and the `long` after it at 8; and 513 bools after six `long`s, a copy of 80
  bytes aligned to 128, the `long` after it at 80, and as a result, whose
  hidden pointer clang doesn't count, the sixth `long` on the stack. And
  `va_arg`'s plans for a `long` aligned to 32, which reads the overflow area
  aligned to 8, and for that union, from two general registers' slots. Last
  the levels across four calls by each path: a struct of 101 bytes ending in
  `packed {packed {_Atomic {__int128[0]}}}`, refused once it waits for that
  member, which it does because an earlier call classified a `LARGER` type;
  and `_Atomic`s of structs of no size classified two calls before, aligned to
  16 by the struct two levels deep in them: a flexible array's element, and in
  a struct of 128 bytes, which doesn't wait for it, a flexible array's and a
  zero-length array's element, whose structs of no size wait for what
  `abi_wide_alignment` reads. And the walk without scratch memory for its
  stack: with the thread's first scratch arena of 64 bytes and up, every type
  must be classified all the same, and the `_Atomic` that an array of none of
  a record of 116 bytes aligns must have its 16 bytes or be refused. And
  records that many paths lead to: over a struct of 32 bytes aligned to 16, 64
  unions of two members of the one before, 2^64 paths, under a 60-second alarm
  (a walk per path wouldn't end), where by each path every type must be
  classified, and an `_Atomic` of a struct of no size that an array of none of
  the last aligns be a `LARGER` copy of 16 bytes. - **Worlds and the oracle**
  (104,651 offsets compared, 159,632 checks): random - **Worlds and the
  oracle** (102,270 offsets compared, 157,816 checks): random
  worlds of types (scalars, vectors, complex types, `_BitInt`s, `_Atomic` and
  aligned types, structs of arrays of no size and `_Atomic`s of them, the
  vectors LLVM breaks up or packs in clang's worlds: of bools, of `_BitInt`s
  of 1 to 4 bits and of 128 to 512, of long doubles, of `__float128`s, and of
  lengths that aren't a power of two past 16 bytes; any of the worlds' types
  made `_Atomic`, and records and unions of them, small ones mostly, with
  bit-fields, arrays, nesting, packing, alignment and flexible array members,
  and in clang's worlds now and then an array of no elements of any type, a
  large record's among them), one at each vector width, one shaped for GCC and
  a rootless one at 32 bytes, without `_Atomic`s of types of no size,
  classified by the vector path into one table and by the twin into another,
  which must be identical, column by column (rows, pass bits, stack bytes,
  alignments, `storage`, `layout` and results). Each classified type at each
  offset it can be at must then have the oracle's classes: clang's `classify`
  transcribed as clang has it, its recursion over members and elements a stack
  of frames, from the type table alone (no rows); and as an unnamed argument
  too. Then the twin classifies every type again, all at once with everything
  they read classified, and its table must not change: the paths share the
  levels, so only this shows a type classified before what it reads, and only
  in the rootless world: in the others an `_Atomic` of a type of no size makes
  every record wait for its members (see "Levels"). Without the zero-length
  array's wait, the rootless world fails at 9 of seeds 1 to 20, and the
  fixtures' levels at every seed; only the levels catch the flexible array
  member's.
- **Assignment** (33,329 arguments of random calls, 236,084 checks): random
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

`--cross-check`, contract 4 on #135, needs no front end for its record runs;
its compiled runs put this compiler's own C on our side (below). For each
compiler (clang and GCC; `--clang`, `--gcc`) and each vector width it builds a
world of
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
  pass: a NO_CLASS eightbyte's, and an SSE one's past its LLVM type's part, as
  `union { struct { char __attribute__((vector_size(16))) z[0]; _Complex float
  c; float f; } s; long __attribute__((vector_size(16))) w; }`, stored as its
  struct (the array aligns it to 16 in LLVM), passes as `(<2 x float>,
  float)`, without the vector's bytes 12 to 15), and per signature a C callee,
  which checks its parameters and its `va_arg`s, a C caller of our callee,
  which checks what our callee stored of its arguments, the al it got and the
  result it returned, and `main`, which runs them and checks what our caller
  stored of the C callee's result and the hidden pointer it got back.
- **Our object** has, per signature, a caller of the C callee and a callee of
  the C caller, generated as encoder records from the locations: stack
  arguments copied with `rep movsb`, register ones loaded (extended as their
  flags say), the hidden pointer and al set, the result stored (x87 parts with
  `fstp`, 80 bits each `part_bytes` apart, or a double's 64 for a part of 8)
  or copied through the hidden pointer, which our callee returns in rax.
  The records go through `x86_encode`, `link_add_code_fixups` and the object
  writer. This emitter is test code; production emission is #136's.
- The compiler's driver compiles the C (`-std=gnu23 -O1 -fno-strict-aliasing`
  and `-march=x86-64`, `-march=x86-64-v3` or `-march=x86-64-v4` for 16, 32 or
  64 bytes) and links it with our object and `-latomic`; the program runs
  every signature both ways and fails if a value differs where its mask says
  it means something, or a hidden pointer comes back wrong.

The signatures leave out `_Atomic` types themselves (the fixtures check them;
records of `_Atomic` members and aligned typedefs of `_Atomic` types are in).
The clang worlds' `_Atomic`s of structs of no size reach them that way: at
seeds 1 to 20, 330 `LARGER` arguments, 167 of them `GROWN`, for which our side
copies a stack argument's own bytes, fewer than its slot's. clang's worlds
leave out a type passed in registers that holds an array of elements of no
size (`abi_test_empty_arrays`), on which clang 23 crashes now and then:
`getFPTypeAtOffset` and `GetINTEGERTypeAtOffset` divide an offset by such an
element's LLVM size, 0, where they look into it, as `GetSSETypeAtOffset` does
after the `_Float16` in `struct { long l; _Float16 h; struct {} e[1]; }`
passed by value. Their signatures pass no type with an SSE low eightbyte and
bit-fields in an INTEGER high one (`abi_test_bit_field_bytes`), whose access
units the module doesn't work out yet, an aligned typedef of an `_Atomic`
vector of bools, whose atomic loads crash its instruction selection, a record
SSE, SSEUP of a size other than 16, 32 or 64 bytes or one SSE, SSE that holds
a `__float128` (see "Next steps" for these three), or a union that holds a
vector LLVM packs into fewer bits than its bytes (of bools, but for a multiple
of 8 that fills them, of narrow `_BitInt`s or of long doubles): clang's LLVM
type for a union is its most aligned member's, the first of them, and its
copies through such a vector lose the other members' bits past it, as `union {
bool b __attribute__((ext_vector_type(9))); __bf16 h; }`'s `i9` loses `h`'s
top 7 bits. They make the vectors LLVM breaks up or packs, whose refused ones
no signature passes. For a `SMALLER` type the C side copies and checks LLVM's
bytes, as clang's own objects of it are no larger, and our callee stores those
through a hidden pointer. The unnamed arguments leave out the types the
default argument promotions change, `nullptr_t` and `_Atomic` ones, types
wider than 16 bytes that are SSE, SSEUP as unnamed arguments, types of no
size, `SMALLER` ones and `WIDE` vectors of `_BitInt(128)`s, whose calls and
`va_arg`s clang passes differently (see "va_arg"), and `LOW_GENERAL` ones. Two
kinds of argument become `long`s, since clang disagrees with itself on them:
one that clang would pass as an `i0`, which its passes crash on now and then,
and an unnamed one that clang's `va_arg` reads elsewhere than its call puts it
(`abi_test_va_disagreement`, from `abi_va_arg`'s plans and the named
arguments' totals; see "va_arg"), which is also where the cross-check runs
`abi_va_arg`. GCC's worlds leave out what "Where GCC passes otherwise" lists.
The cross-check doesn't see a missing extension (each side compares a value's
own bytes), which the fixtures' `SIGN` and `ZERO` check instead. The files go
in a directory under `$TMPDIR` (or `/tmp`), removed unless `--keep` or a
failure.

**The compiled runs** (#137): two more runs, against clang and against GCC at
16 bytes, put C that this compiler compiles on our side. Their worlds
(`AbiTestWorld.compiled`) hold what its C passes: integers of up to 64 bits,
`__int128` and `unsigned __int128` (#133's lowering to Tier 0's I128 and U128),
enums, pointers, `float`, `double` and `long double` (#136's item 8),
`_Complex float`, `double` and `long double` (#133's lowering, as aggregates of
their two parts), and the structs, unions, arrays and aligned typedefs made of
them, aligned as the clang worlds' are (to 64: Tier 0 realigns a frame for an
object or a call's outgoing area aligned past 16, and a value aligned past 8 is
an object), with flexible array members, bit-fields, packing and member
alignments;
no `nullptr_t`, `_BitInt`, vector, complex integer or `_Atomic` type, and none of
clang's directed records. C23's enums of a fixed underlying type, structs
whose only named member is a flexible array member (a GNU extension) and
bit-fields with attributes after their widths are among their types, as
clang and GCC write them. Tier 0 holds a call's arguments on its stacks at once (15
integer and 16 floating values, 14 integer ones in a realigned frame, whose
rbp is the table's last) and copies its aggregates through each stack's first
free register, and our caller's `r = c_callee(...)` holds `r`'s address under
the arguments when the result is a struct, a union, a `long double` or a
128-bit integer, so a compiled world's signature passes at most 12 integer
values (an aggregate is its address, and a `long double` or a 128-bit integer
takes an integer depth) and 15 floating ones,
an argument past them becoming one of the other stack's. Our side is C (`abi_test_write_ours`): per signature, a callee
that stores each parameter's bytes, its unnamed ones read with `va_arg`, and
returns the result's from the signature's data, and a caller that copies each
argument's bytes into a local of its type, calls the C callee with them and
stores the result's bytes. This executable (or `--buster PATH`) compiles it
with `--verify -std=gnu23 -w -c`, the vector paths' records checked by
`x86_validate`, and again with `--scalar`, whose object must be the same
bytes; the compiler's driver links the object with the C side, which checks
every argument and result both ways as in the record runs. C names neither the
al our caller sets nor the hidden pointer our callee gets back, so those
checks are the record runs' alone.

On the cloud VM (not the 9700X: Intel, clang 23.1.5, this compiler under
Intel SDE for its AVX-512), the compiled runs pass at seeds 1 to 11, with both
paths' objects the same bytes every time: against clang, 4,400 signatures of
26,801 arguments, 60,660 checks; against GCC's worlds, compiled there by GCC 13
(`-std=gnu2x` for `-std=gnu23`), 4,400 signatures of 25,907 arguments, 58,842
checks. On the 7940HS (2026-10-26, clang 23.1.4 and GCC 16.2.1), once the
compiled worlds took `__int128` and `_Complex`, every run passes at seeds 1 to
100. The eight runs of random signatures (the six record runs and the two
compiled ones) make 320,000 signatures and 4,465,433 checks, of which the
compiled runs' are 80,000 signatures and 1,080,346 checks; the two extension
runs ("Extension", below) pass as well.

With the defaults (seed 1), each run of random signatures has 400 of 2,200 to
2,600 arguments, and the eight make about 45,000 checks, besides the two
extension runs' 1,004 each; on the 9700X the six record runs took 4.8 s, about
a clang run's time. The ten runs are independent and nearly all of each one's
time is its compiler's, so they go at once, a child process each (the six
record runs one after another took 18.1 s); the test prints their lines and
adds their counts in the runs' order (clang's, then GCC's, each at 16, 32 and
64 bytes, then the compiled runs against clang and against GCC, then the
extension runs), and a run that fails keeps its files. The record runs passed
seeds 1 to 100 with clang 23.1.3 and GCC 16.2.1, before the compiled runs.

Capture descriptors stay above stderr, even when the command starts with
standard streams closed. Before the ordinary cross-check, bounded child
processes exercise all eight combinations of open/closed standard descriptors
with `/bin/false` as every compiler. Each must exit 1, preserve the standard
descriptors' state and retain all ten runs' failure files; the regression
then removes its own files. A 10-second alarm and an 8 MiB file-size limit
bound a replay that regresses into writing back into its capture. Two more
children exhaust the descriptor limit at duplication and at the second
capture, checking cleanup and failure accounting. These checks have their
own tally and were added after the timings above.

### Extension

clang extends a `_Bool`, a character type, a `short` or an `unsigned short`
argument, an enum of one and a `_BitInt` narrower than `int` (its `Extend`) to
32 bits, by its signedness, plain `char`'s with `-funsigned-char` too; its
callee takes the 32 bits as they come (`int f(char c) { return c; }` is
`movl %edi, %eax`), so a caller must extend them as the flags say. A result is
the other way round: clang's IR has the callee extend it (`signext`,
`zeroext`), but LLVM's x86-64 code doesn't (`char g(long v) { return v; }` is
`movq %rdi, %rax`, and a load of `v` back from its slot at `-O0`), nor does
GCC's (`movl %edi, %eax`), and clang's caller extends
what it gets back (`movsbl %al, %eax`). A caller reads a narrow result's bits
alone and extends them itself, as Tier 0 does, its narrow values' upper bits
unspecified; its own `RETURN` extends, which no caller needs.

The cross-check's two extension runs (`abi_test_cross_extension`), with plain
`char` signed and unsigned (`TypeTable.char_unsigned` and the C compilers'
`-fsigned-char` or `-funsigned-char`), check these on `char`, `signed char`,
`unsigned char`, enums of a fixed underlying `char` and `signed char`, `short`,
`unsigned short` and `bool`, at 17 values with bits set past the type's: a
record callee returns the 32 bits its argument arrived in from clang's caller,
which must be the flags' extension; a record caller extends as the flags say
before calling clang's callee, which must then return the value, and the other
way (the control), which must show; a record caller returns the 32 bits of
clang's result, whose low bits must be the value's; and this compiler's C,
compiled with each signedness by both paths into the same bytes, passes,
takes, returns and gets back each type with clang's code. Each run makes
1,004 checks, and 71 of clang's 136 results come back unextended. A mutant of
the module that ignores `char_unsigned`, one of the type table that does, and
one of Tier 0 that doesn't extend a narrow argument each fail them.

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
| classify-all, 16 B | vector | 39.38 [39.04–39.56] | 218.4 | 607 | 2.78 | 39.12 | 39.44 |
|  | scalar | 67.72 [67.31–67.81] | 375.6 | 1,379 | 3.67 | 67.33 | 67.48 |
| classify-all, 32 B | vector | 43.68 [43.24–43.78] | 242.1 | 648 | 2.68 | 43.36 | 43.65 |
|  | scalar | 68.57 [67.98–68.96] | 380.0 | 1,384 | 3.64 | 68.07 | 68.28 |
| classify-all, 64 B | vector | 46.86 [46.39–47.02] | 259.9 | 710 | 2.73 | 46.55 | 46.82 |
|  | scalar | 68.92 [68.53–69.18] | 381.9 | 1,387 | 3.63 | 68.47 | 68.72 |
| batches of 1 | vector | 31.40 [31.31–31.53] | 173.7 | 724 | 4.17 | 30.89 | 31.65 |
|  | scalar | 53.54 [53.36–53.87] | 296.6 | 1,309 | 4.41 | 53.34 | 53.20 |
| batches of 4 | vector | 30.55 [30.17–30.56] | 169.2 | 612 | 3.61 | 29.41 | 30.58 |
|  | scalar | 55.17 [54.55–55.28] | 305.9 | 1,287 | 4.21 | 54.48 | 54.28 |
| batches of 16 | vector | 28.38 [28.32–28.52] | 157.3 | 548 | 3.48 | 27.66 | 28.50 |
|  | scalar | 53.92 [53.18–53.98] | 298.9 | 1,279 | 4.28 | 53.01 | 53.50 |
| assign | vector | 3.28 [3.27–3.28] | 18.2 | 46 | 2.54 | 3.15 | 3.29 |
|  | scalar | 7.47 [7.46–7.50] | 41.4 | 149 | 3.59 | 7.46 | 7.49 |
| assign-simple | vector | 2.84 [2.83–2.84] | 15.7 | 39 | 2.48 | 2.81 | 2.84 |
|  | scalar | 6.94 [6.90–6.94] | 38.4 | 152 | 3.96 | 6.94 | 6.92 |

The twin's time over the vector path's, default state: 1.72, 1.57 and 1.47 for
`classify-all` at 16, 32 and 64 bytes; 1.71, 1.81 and 1.90 for batches of 1, 4
and 16 types (1.00, 2.76 and 8.17 types per call); 2.28 for `assign` and 2.44
for `assign-simple`.

Against main with #267's `_Atomic` rules, before the members' parts and the
assignment's marks, three processes of each by turns, the vector paths in ns
per type or argument, with their instructions and mispredictions:

| Case | before | after | | instructions | mispredictions |
| --- | ---: | ---: | ---: | ---: | ---: |
| classify-all, 16 B | 59.58 | 39.38 | 1.51× | 932 → 607 | 4.72 → 2.86 |
| classify-all, 32 B | 65.01 | 43.68 | 1.49× | 969 → 648 | 5.23 → 3.34 |
| classify-all, 64 B | 69.66 | 46.86 | 1.49× | 1,027 → 710 | 5.47 → 3.45 |
| batches of 1 | 54.45 | 31.40 | 1.73× | 1,084 → 724 | 3.27 → 1.48 |
| batches of 4 | 53.05 | 30.55 | 1.74× | 960 → 612 | 4.17 → 1.90 |
| batches of 16 | 49.36 | 28.38 | 1.74× | 884 → 548 | 4.15 → 2.08 |
| assign | 4.99 | 3.28 | 1.52× | 56 → 46 | 0.37 → 0.03 |
| assign-simple | 4.26 | 2.84 | 1.50× | 48 → 39 | 0.31 → 0.01 |

No row's ranges overlap. Where the code lands moves these rows by a few
percent, so the same three processes of each ran again with every function
aligned to 64 bytes in both builds (`-falign-functions=64`): 1.55 and 1.56 for
`classify-all`, 1.69 to 1.73 for the batches, 1.48 for `assign` and 1.45 for
`assign-simple`. The twin is 16% faster for `classify-all` and 20 to 21% for
the batches (19% and 22 to 23% aligned), as it no longer works out the storage
of records larger than 16 bytes, and as fast as before for the assignment.

- **The classification's table:** 32,756 types to classify, 26,779 of them
  records (4,383 unions) with 107,818 members, in 13 levels of nesting; 18,045
  of the records are MEMORY at 16 bytes. Up to about 4,096 types the branch
  predictor learns the table from one run to the next, and both paths look
  1.4 to 2.3 times as fast as they are; from 16,384 types on the costs stay
  within about 10%, hence the default of 32,768.
- **Where the classification's time goes:** in a profile of the vector
  classification (`perf record` of `--only classification --path vector`),
  the finishing step both paths share, `abi_finish_passing`, takes 43% of the
  cycles, 94% of them at the instruction where `abi_result`'s branches on the
  classes join; the levels' scans 18%; the pre-pass's entries and parts 17%;
  the merges and the post-merge 5%; and the rest of `abi_classify_vector`,
  its sort and dispatch, 5%.
- **What lost, measured and dropped:**
  - The members' parts reduced in the fold, from a second 64-byte block per
    step, ORed (with a masked `vpmaxuq` for a union's storage words) beside
    the merges, rather than as the pre-pass writes the entries: 1.7% to 7.4%
    slower, three processes of each by turns before #267's rules
    (`classify-all` 38.97 against 38.25 ns at 16 bytes, batches of 16 29.72
    against 27.66), with 559 instructions and 3.03 mispredictions per type
    against 551 and 2.91.
  - Every record's exact storage, those larger than 64 bytes waiting in the
    levels for their members: 110 more instructions and 2 more mispredictions
    per type on the laptop's counts, the levels taking 13 rounds where they
    take 7. `abi_wide_alignment` works out only what an array of no elements
    needs.
  - At the work both paths share, on the laptop's counts: a table of the
    folded aggregates' pass bits and results by their classes (554 → 547
    instructions per type, as many mispredictions); each pending record's
    scan of its members resumed where it stopped (no change); levels from
    lists of dependents, Kahn's way (835 instructions and 8.7 mispredictions
    per type, from a loop of random length per type); and `abi_result`'s
    `AbiReturn` kept out of memory, two ways (no change: the cost is the
    mispredicted branches, not a store waiting to forward).
  - Merging #267's rules first made `abi_dependencies_classified` too large
    for clang to inline into the levels' loop, and its walk's readiness came
    back through a local's address, which gave it a stack protector's
    canary: 656 instructions per type in `classify-all` at 16 bytes and 41.03
    ns. With the rare waits out of line (`abi_wide_classified`,
    `abi_members_classified`) and the readiness a flag in the walk's result,
    it inlines again: 607 and 39.38.
- **The assignment:** the vector path leaves 567 of the 32,768 calls (1.7%,
  with 5.2% of the arguments) to the twin: 219 for a register its sums count
  as used though it is free, 364 for padding on the stack and 18 for an
  `EMPTY` or `GROWN` argument there; `assign-simple` leaves none. In a
  profile of the vector assignment, its loop over the arguments takes 58% of
  the cycles (the segmented prefix sums 20 of them), the setup's pass over the
  calls 15%, the last pass over the calls 8% and the twin, for the deferred
  calls, 6%; before the marks, the setup's loops over each call's arguments
  took about 30% of the vector path's time, mostly at their mispredicted
  exits.
- **The power cap (#199):** the throttle takes 0.056 to 0.062 of the vector
  classification's dispatch slots per cycle in `classify-all`, 0.010 to 0.024
  in the batches and 0.009 to 0.010 in the assignment, in every state. These
  are short AVX-512 phases, too light to need a state of their own: after 100
  ms of the twin each vector row is within 1% of its default state, as are the
  base's; alone, the vector paths are up to 4% faster (the batches of 4 and
  `assign`), and the twin as fast. The clock is 5.55 GHz throughout.

## Next steps

- **#136's use:** call sequences, prologues and `va_arg` from these
  locations, and Tier 0's depth-to-register order (contract 6 on #135).
- **A union stored as an `fp128` with an SSE high eightbyte**, which the
  cross-check leaves out (any record SSE, SSE that holds a `__float128`).
  clang's `GetSSETypeAtOffset` returns the `fp128` at the union's offset 0 for
  its low eightbyte, 16 bytes, so that `GetX86_64ByValArgumentPair` can't
  start the high part at 8 and widens the low one to an `i64` (its assertion
  that the low part is an integer or a pointer is off in release builds):
  `union { __float128 q; char v __attribute__((vector_size(8))) [2]; }` passes
  in rdi and xmm0 and returns in rax and xmm0, where the module gives xmm0 and
  xmm1. It is `LOW_GENERAL`'s rule for a 16-byte low part, which `abi_layout`
  keeps no `fp128` leaf to find.
- **Records SSE, SSEUP of a size that isn't a vector register's**, which the
  cross-check leaves out. A union of a 16-byte vector and a longer array
  (NO_CLASS), packed or of an alignment below its size, can be SSE, SSEUP at
  22 or 24 bytes, and the module passes it in one vector register of its size
  (`WIDE`, on the stack in a variadic call). clang's `GetByteVectorType` gives
  such a record `<size / 8 x double>`: 22 bytes are `<2 x double>`, in xmm0
  even in a variadic call (al 1) and losing their last 6 bytes; 24 bytes are
  `<3 x double>`, which LLVM widens to a ymm register, and in a variadic call
  to a 32-byte slot aligned to 32 where the module gives one of 24 aligned to
  8. Each size needs its rule (`<size / 8 x double>`'s register and slot), or
  `UNSUPPORTED`.
- **The finishing step's branches.** `abi_finish_passing`, which both paths
  share, is now the largest part of the vector classification's time: 43% of
  its cycles in a profile of the benchmark, 94% of them at the instruction
  where `abi_result`'s branches on the classes join (keeping the result out
  of memory changed nothing on the laptop, so it isn't a store waiting to be
  forwarded). Pass bits and results without those branches, from a table
  indexed by the classes and the type's kind or from the fold's vectors four
  types at a time, are the next try.
- **The levels.** Their scans (`abi_classify_levels` asking
  `abi_dependencies_classified` about every pending type, a pass per level)
  take 18% of the vector classification's cycles and branch on what each type
  waits for. Each type's level is the longest path below it in the graph of
  what it waits for, which pointer jumping or a pass in a topological order
  could give for every type at once.
- **Bit-fields' access units.** `abi_layout` and the parts take a run of
  bit-fields within an eightbyte as its first bit-field's type, at its type's
  offset, where clang's `accumulateBitFields` makes access units of the run's
  bytes (merged up to a register while the next one's integer fits before the
  next member with storage, else an array of bytes), and the struct's LLVM
  alignment from them. Units decide what the module gives in three places: in
  an INTEGER high eightbyte, by their alignment, whether a narrow SSE low part
  widens (`LOW_GENERAL`); through a struct's LLVM alignment, a union's storage
  member and so `PAIR` (`union { struct { __int128 a : 8; } s; __int128 i; }`
  is one `i128` to clang, and the module splits it); and the LLVM sizes of
  `LARGER` copies. `struct { _Complex _Float16 h; _Complex long z[0];
  _BitInt(8) a : 3; unsigned char b : 8; }` is `(i64, i16)` to clang, a unit
  of two bytes after the `<2 x half>`, and the module passes its low part in
  xmm0, as it takes an `i8` with data after it. clang's cross-check found it
  at seeds 50, 82 and 98 of 100, and leaves such types out until the module
  works the units out (in `abi_bit_field_run`, which does it for a run wider
  than an eightbyte), or refuses them.
- **Flexible records with a size** keep no storage (`STORAGE_INEXACT`): an
  `_Atomic` of a record of no size that holds an array of none of them is
  refused, but a record of 16 bytes or less that holds one passes by alignment
  1, unchecked: `union {{long, long, {int, __int128[]}[0]}, __int128}` is two
  `i64`s to clang 23, and the module makes it a `PAIR` at every width.
  `abi_wide_alignment` could walk them as it walks large records, with the
  levels waiting for what it reads.
- **Plain `char`:** `abi_extension` takes it as signed, as x86-64 has it, or
  as unsigned where the type table says so (`TypeTable.char_unsigned`, from
  `-funsigned-char`). The cross-check's worlds keep signed char; its extension
  runs check plain char with each signedness (see "Extension").

## Boundary audit #298

Audit pin: `42630114aeb0553c3e2fabfd7fe9bc84b0ce6126`;
[scope and evidence](simd-boundary-audit.md).
[Classification, assignment and ownership](https://github.com/buster14a/buster/blob/42630114aeb0553c3e2fabfd7fe9bc84b0ce6126/src/buster/compiler/calling_convention.c#L4)
are implemented; source-to-function construction is still an integration.

| Producer → consumer | Data and batch conditions |
| --- | --- |
| Completed TypeTable rows → `abi_classify` / `abi_classify_all` | Type-indexed 16-byte class rows plus pass/storage/layout/result metadata. Member/element dependencies must be classified first; level discovery is part of the cost. |
| Classified member rows → aggregate fold → `abi_finish` | Reuses shifted rows instead of recursive reclassification. LLVM-shape/storage exceptional rules still inspect member/type data; psABI classes alone are insufficient. |
| Signature ranges/types → `abi_assign` → argument locations and `AbiCall` | Contiguous arguments and per-call boundaries, named/variadic counts and result metadata. Register demand is a segmented scan; a call that exceeds vector limits goes whole to the exact twin. |
| ABI metadata → Tier 0 generated-function inputs / frame planner / `abi_va_arg` | Consumers use locations, hidden result, outgoing bytes/alignment, variadic state and copy/storage rules; they must not reclassify from size alone. Cross-link tests are real machine-code consumers. |

The ABI table is caller-owned arena storage tied to the same live TypeTable;
IDs from another table are invalid. `abi_initialize` sizes columns from type
capacity, classifies builtins and accepts vector widths 16, 32 or 64 bytes;
failure restores its arena. Rows and metadata include u8 class/alignment/storage,
u32 pass/stack bytes, u64 layout and `AbiReturn` records. One writer classifies;
readers cannot race classification or tag completion. `abi_assign` is a trusted
consumer: all referenced types must already be classified, pass must not have
`BUSTER_ABI_PASS_UNSUPPORTED` set, and stack bytes must not be `UINT32_MAX`. Its producer
must reject unsupported types before assignment.

Natural typed-column strides and explicit counts/capacities apply;
assignment's masked tails do not require caller padding. Its padded internal
scratch is private; scratch allocation failure uses the exact scalar path.
Empty signatures and partial batches are tested. A call is not
splittable into independent batches because earlier arguments consume registers;
only full independent call ranges may be repartitioned.

A future frontend can collect signatures across a unit, avoiding a one-call API
that underfills vector work. It must expose real completion/ready frontiers,
not sort types through dependencies or give later tag completion to earlier
source contexts. The current classification scans for readiness; a producer
completion list is a candidate, not a demonstrated saving. `abi_finish`'s
storage/LLVM-shape work is retained for correctness and must be charged, even
when the two-byte class result looks cheap.

Disposition: retain the shared ABI result rather than force each emitter to
reconstruct it. `test-calling-convention --cross-check` validates generated
callers/callees against Clang/GCC, including variadics, guards and exceptional
counts; `test-types` covers its producer. Existing bulk and incremental type
benchmarks and call batches are synthetic. [#133](https://github.com/buster14a/buster/issues/133)/[#137](https://github.com/buster14a/buster/issues/137)/[#136](https://github.com/buster14a/buster/issues/136)/[#177](https://github.com/buster14a/buster/issues/177) retain real
source-signature production, whole type + ABI + emission costs, small/large
occupancy, scratch/allocations and generated-program qualification.
