// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// The System V AMD64 calling convention (#137): where each argument and result of a call or of a function's entry is
// passed, as data for the code generator (Tier 0, #136), which emits every instruction. docs/calling-convention.md has
// the design and its 9700X measurements. The reference is clang 23 on x86-64 Linux, as #154 decided: its X86_64ABIInfo
// (classify, merge, postMerge, classifyArgumentType, classifyReturnType, getIndirectResult and computeInfo) decides,
// and LLVM's x86-64 calling convention places what clang leaves to it. Their sources were read for these rules; none of
// them is copied. test-calling-convention checks them against clang and GCC by linking callers and callees made of
// encoder records with C that they compiled.
//
// Classification (the psABI's 3.2.3): each eightbyte of a type gets a class, BUSTER_ABI_INTEGER to BUSTER_ABI_MEMORY in
// clang's order, and a type's two classes are an AbiClasses byte (the low eightbyte's in bits 0 to 2, the high one's in
// bits 4 to 6). clang classifies an aggregate's members at their offsets within the outermost aggregate, so each type is
// classified once into a row: its classes at each byte offset 0 to 15 of an enclosing aggregate of at most 16 bytes,
// which is every place clang's recursion can ask for them (row[0] is the type's own, and a place the type can't be at
// holds MEMORY). A record's or an array's row is then a fold of its members' rows, each shifted by its member's offset,
// merged lane by lane with clang's merge (a 64-entry table) and post-merged, its 16 offsets at once: n members cost n
// merges, whatever the nesting. MEMORY is canonical: a class pair with MEMORY in either half is MEMORY in both
// (BUSTER_ABI_IN_MEMORY), since MEMORY in either half passes the whole value in memory.
//
// clang's own rules, mimicked where GCC passes otherwise (the tests skip them with GCC; the document lists them):
// _Atomic types are MEMORY, so _Atomic aggregates and records with an _Atomic or nullptr_t member go in memory, and
// _Atomic scalars and nullptr_t are passed as their values, in registers LLVM gives them without clang counting them
// (BUSTER_ABI_PASS_UNCOUNTED), which shifts the arguments after them; records with a flexible array member go in memory;
// __fp16 is MEMORY; vectors of 4 bytes or less are INTEGER whatever their elements. And where clang's LLVM types decide
// the registers (abi_storage and abi_layout model the types it needs): an i128 at a record's offset 0 makes it one value
// LLVM never splits (BUSTER_ABI_PASS_PAIR), as a run of bit-fields wider than an eightbyte is where 16 bytes fit for it
// (abi_bit_field_run); a narrow low part with a high part not aligned to 8 goes in a general
// register (LOW_GENERAL); a vector result LLVM demotes to a hidden pointer isn't in clang's count (DEMOTED); a vector of
// one element is its element's value (HALVES for a <1 x i128>).
//
// Assignment (computeInfo): an argument whose classes need general and vector registers takes them in order (rdi,
// rsi, rdx, rcx, r8 and r9; xmm0 to xmm7) if all of them are free, and otherwise goes whole to the stack, the registers
// staying free for the arguments after it. A result in MEMORY makes the caller pass its address in rdi. The stack
// arguments are at increasing offsets from rsp at the call, each aligned to 8 or to its alignment if larger. Where
// clang's count runs ahead of the registers LLVM gives (after an UNCOUNTED one, a PAIR LLVM put on the stack, a
// LOW_GENERAL one or a DEMOTED result), what clang leaves to LLVM as a value, a scalar or a small aggregate passed as an
// integer, takes the register LLVM still has (abi_assign_call); one that needs two may be split (SPLIT).
//
// Entry points: abi_initialize (the table, with the builtin types classified), abi_classify_all (every complete type
// not yet classified, in levels whose types depend only on earlier ones) and abi_classify (one such level: the vector
// path), with their twins abi_classify_all_scalar and abi_classify_scalar; abi_assign (many calls' arguments and
// results: the vector path, 16 arguments at a time across calls) and its twin abi_assign_scalar; abi_classes and
// abi_va_arg (va_arg's plan for a type). The twins give identical results, and the tests compare them.
//
// Who writes what: the table is the code generator's, which classifies after the front end has made the types (the
// type table's columns never change once written, but a tag's at its completion, after which abi_classify_all
// classifies it). Lanes may read it while none writes.
//
// main.c and calling_convention_benchmark.c include this file after lane.c (Arena, Scratch) and c_types.c (TypeTable).
// Layout, in this order:
//   - API: the classes (BUSTER_ABI_INTEGER ...) and AbiClasses, the registers (BUSTER_ABI_RAX ...), the convention's
//     constants, the pass bits (BUSTER_ABI_PASS_*), AbiReturn, AbiLocation, AbiCall, AbiSignatures, AbiVaArg, AbiVaList,
//     AbiTable and the prototypes.
//   - Classes: abi_merge_class, abi_merge, abi_canonical and abi_post_merge.
//   - Simple types (everything but records and arrays): AbiSimple, abi_simple_fixed, abi_simple_current,
//     abi_resolved_index, abi_simple_floating, abi_simple, abi_simple_entry and abi_simple_row.
//   - Entries, the scalar classification: abi_row_at, abi_alignment_bytes_log2, abi_array_entry, abi_bit_field_classes,
//     abi_member_row_at, abi_record_entry and abi_row_entry.
//   - Finishing a type: abi_canonical_alignment_log2; clang's LLVM types (abi_member_storage, abi_union_storage_member,
//     abi_storage, abi_layout and its helpers, abi_sse_part, abi_integer_part, abi_low_general, abi_is_pair); values
//     and results (abi_value_class, abi_value_alignment_log2, abi_is_aggregate, abi_is_illegal_vector, abi_extension,
//     abi_value_result, abi_result); abi_end_memory, abi_finish and abi_copy.
//   - The vector classification: AbiFold, AbiFoldGroup, abi_lane_offsets, abi_aligned_lanes, abi_possible_lanes,
//     abi_shifted_row, abi_member_entry, abi_write_entries, abi_merge_vector, abi_post_merge_vector, abi_fold_groups,
//     abi_store_row and abi_classify_vector.
//   - The drivers: abi_capacity_bytes, abi_initialize, abi_classify_scalar, abi_classify, abi_classifiable,
//     abi_is_classified, abi_dependencies_classified, abi_classify_levels, abi_classify_all, abi_classify_all_scalar and
//     abi_classes.
//   - Assignment: abi_stack_place, abi_assign_call (the twin, one call), abi_assign_scalar, the vector path's AbiScan,
//     abi_scan, abi_scan_add, abi_scan_or and abi_assign.
//   - va_arg: abi_va_arg.

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

// CPU architecture dependent includes
#include <immintrin.h>

// ---------------------------------------------------------------------------------------------------------------------
// API

// The classes, in clang's order (X86_64ABIInfo::Class), which the merge table's index follows.
#define BUSTER_ABI_INTEGER 0
#define BUSTER_ABI_SSE 1
#define BUSTER_ABI_SSEUP 2
#define BUSTER_ABI_X87 3
#define BUSTER_ABI_X87UP 4
#define BUSTER_ABI_COMPLEX_X87 5
#define BUSTER_ABI_NO_CLASS 6
#define BUSTER_ABI_MEMORY 7
#define BUSTER_ABI_CLASS_COUNT 8

// A type's classes: the low eightbyte's in bits 0 to 2 and the high one's in bits 4 to 6.
typedef uint8_t AbiClasses;
#define BUSTER_ABI_CLASSES(low, high) ((AbiClasses)((low) | (high) << 4))
#define BUSTER_ABI_LOW(classes) ((uint8_t)((classes) & 7))
#define BUSTER_ABI_HIGH(classes) ((uint8_t)((classes) >> 4 & 7))
#define BUSTER_ABI_IN_MEMORY BUSTER_ABI_CLASSES(BUSTER_ABI_MEMORY, BUSTER_ABI_MEMORY)
#define BUSTER_ABI_NOTHING BUSTER_ABI_CLASSES(BUSTER_ABI_NO_CLASS, BUSTER_ABI_NO_CLASS)
// A row: a type's classes at each byte offset of an enclosing aggregate of at most this many bytes.
#define BUSTER_ABI_ROW_BYTES 16
// The largest aggregate the psABI passes in registers (two eightbytes), and the largest clang classifies at all (eight
// eightbytes, Rule 1; beyond that, MEMORY).
#define BUSTER_ABI_REGISTER_BYTES 16
#define BUSTER_ABI_MAX_CLASSIFIED_BYTES 64

// Registers, as the encoder numbers them (X86Register: RAX 0 ... R15 15), a vector register's number (XMM0 0, or YMM0
// or ZMM0 by the value's size) plus BUSTER_ABI_REGISTER_VECTOR, and an x87 register's (ST0 0) plus
// BUSTER_ABI_REGISTER_X87.
#define BUSTER_ABI_RAX 0
#define BUSTER_ABI_RCX 1
#define BUSTER_ABI_RDX 2
#define BUSTER_ABI_RBX 3
#define BUSTER_ABI_RSP 4
#define BUSTER_ABI_RBP 5
#define BUSTER_ABI_RSI 6
#define BUSTER_ABI_RDI 7
#define BUSTER_ABI_R8 8
#define BUSTER_ABI_R9 9
#define BUSTER_ABI_R10 10
#define BUSTER_ABI_R11 11
#define BUSTER_ABI_R12 12
#define BUSTER_ABI_R13 13
#define BUSTER_ABI_R14 14
#define BUSTER_ABI_R15 15
#define BUSTER_ABI_REGISTER_VECTOR 0x20
#define BUSTER_ABI_REGISTER_X87 0x40
#define BUSTER_ABI_REGISTER_NONE 0xff

// The convention's constants: the argument registers, in order, the result registers, the registers a function must
// preserve (a bit per general register; also the x87 control word and MXCSR's control bits), the red zone below rsp
// that a leaf function may use, and rsp's alignment at a call (more when an argument on the stack is aligned more).
#define BUSTER_ABI_INTEGER_ARGUMENTS 6
#define BUSTER_ABI_VECTOR_ARGUMENTS 8
static const uint8_t abi_integer_arguments[BUSTER_ABI_INTEGER_ARGUMENTS] = {
    BUSTER_ABI_RDI,
    BUSTER_ABI_RSI,
    BUSTER_ABI_RDX,
    BUSTER_ABI_RCX,
    BUSTER_ABI_R8,
    BUSTER_ABI_R9,
};
#define BUSTER_ABI_INTEGER_RESULTS 2
static const uint8_t abi_integer_results[BUSTER_ABI_INTEGER_RESULTS] = {
    BUSTER_ABI_RAX,
    BUSTER_ABI_RDX,
};
#define BUSTER_ABI_CALLEE_SAVED                                                                                        \
    (1u << BUSTER_ABI_RBX | 1u << BUSTER_ABI_RBP | 1u << BUSTER_ABI_R12 | 1u << BUSTER_ABI_R13 | 1u << BUSTER_ABI_R14 | \
     1u << BUSTER_ABI_R15)
#define BUSTER_ABI_RED_ZONE_BYTES 128
#define BUSTER_ABI_STACK_ALIGNMENT_LOG2 4
// A stack argument's slot: its size rounded up to this, aligned to this or to its alignment if larger.
#define BUSTER_ABI_SLOT_BYTES 8
#define BUSTER_ABI_SLOT_LOG2 3
// A variadic call sets al to the number of vector registers its arguments use (an upper bound, 0 to 8).
#define BUSTER_ABI_VARIADIC_COUNT_REGISTER BUSTER_ABI_RAX

// A variadic function's register save area, which its prologue fills: rdi to r9 at 0, 8 ... 40, then xmm0 to xmm7 in
// 16-byte slots from 48 (the vector ones only when al isn't 0). va_list is a one-element array of AbiVaList, whose
// offsets point into it: gp_offset from 0 to 48, fp_offset from 48 to 176.
#define BUSTER_ABI_SAVE_AREA_BYTES 176
#define BUSTER_ABI_SAVE_AREA_VECTOR_OFFSET 48
#define BUSTER_ABI_SAVE_AREA_VECTOR_SLOT_BYTES 16

typedef struct AbiVaList AbiVaList;
struct AbiVaList
{
    uint32_t gp_offset;
    uint32_t fp_offset;
    uint64_t overflow_arg_area;
    uint64_t reg_save_area;
};
_Static_assert(sizeof(AbiVaList) == 24, "the psABI's va_list element is 24 bytes");

// How a classified type is passed (AbiTable.pass), from its classes and what it is:
//   - LOW_INTEGER ... HIGH_SSE: the register class of each eightbyte passed in a register (none for a MEMORY or
//     IGNORE type). A high eightbyte without a low one (the low one is padding) passes the high one alone.
//   - VECTOR: SSE then SSEUP: one vector register holds the whole value (XMM, or a YMM or ZMM register when WIDE).
//   - WIDE: a VECTOR of 32 or 64 bytes. LLVM passes it in a YMM or ZMM register only in a call that isn't variadic, and
//     on the stack otherwise: named, after clang has counted a vector register for it; unnamed, MEMORY, uncounted.
//     (clang classifies a WIDE union of a 16-byte member and a long array SSE, SSEUP as an unnamed argument too, counts
//     a vector register for it and puts it on the stack all the same, while its va_arg reads it from the save area:
//     there is no convention there to follow but GCC's, memory.)
//   - MEMORY: on the stack as an argument (MEMORY, X87 and COMPLEX_X87). As a result: through a hidden pointer if
//     AGGREGATE, otherwise in st0 (X87), st0 and st1 (COMPLEX_X87) or as UNCOUNTED says.
//   - UNCOUNTED: MEMORY by its class, but a scalar clang passes as its value, in a register LLVM gives it (VALUE says
//     which kind) and clang doesn't count, or on the stack once there is none: nullptr_t and _Atomic scalars.
//   - IGNORE: nothing to pass (both classes NO_CLASS: an empty struct, a struct of unnamed bit-fields).
//   - PAIR: one 128-bit integer value (clang's i128) that LLVM never splits between a register and the stack; with a
//     high eightbyte NO_CLASS too (an i128 a union stores, as union { int a; unsigned _BitInt(128) : 127; }), which
//     clang counts and passes as two INTEGER eightbytes (HIGH_INTEGER is set, though the row says NO_CLASS).
//   - SIGN, ZERO: clang's Extend: the caller sign- or zero-extends the value to 32 bits (the callee, for a result).
//   - AGGREGATE: clang's byval: a copy of it on the stack (and, 8 bytes or less with no general register left in
//     clang's count, an integer value instead). Results don't read it: a vector LLVM has no register type for is
//     AGGREGATE as an argument but returned as its value (abi_result reads abi_is_aggregate).
//   - END_MEMORY: a type of no size that is MEMORY at offset 16 though not at 0 (see abi_member_row_at), which only the
//     classification reads.
//   - UNSUPPORTED: an UNCOUNTED vector wider than the target's vector registers (an _Atomic one), which LLVM would split
//     across several registers as no class says: the code generator refuses it.
//   - CLASSIFIED: the type's row and these bits are in the table.
//   - VALUE (bits 16 to 18): an UNCOUNTED value's kind: a general register (GENERAL), two (PAIR), a vector register
//     (VECTOR), x87's, always on the stack (X87), or two halves, each in a general register while one is left and
//     then on the stack (HALVES: a <1 x i128>, which LLVM splits as it never does an i128).
//   - ALIGNMENT (bits 20 to 25): the log2 of its stack slot's alignment: 3, or its own if larger (a scalar's natural
//     one, an aggregate's own).
//   - LOW_GENERAL (bit 27): the low eightbyte, SSE by its class (LOW_SSE, which clang counts), goes in a general
//     register: clang's type for it is narrow (a bfloat, <2 x half> or <2 x bfloat>) and the high part's isn't aligned
//     to 8, so GetX86_64ByValArgumentPair widens it to an i64 (it widens a half or a float to a double).
//   - EMPTY (bit 26): a type of no size. One in memory (a record of a flexible array member alone) takes an 8-byte slot,
//     as LLVM gives a byval argument, unless clang has no general register free and its alignment is 8 or less: clang
//     then passes an aggregate of 8 bytes or less as an integer of its size (getIndirectResult), here an i0, which
//     takes no slot.
#define BUSTER_ABI_PASS_LOW_INTEGER 0x0001u
#define BUSTER_ABI_PASS_LOW_SSE 0x0002u
#define BUSTER_ABI_PASS_HIGH_INTEGER 0x0004u
#define BUSTER_ABI_PASS_HIGH_SSE 0x0008u
#define BUSTER_ABI_PASS_REGISTER_CLASSES 0x000fu
#define BUSTER_ABI_PASS_VECTOR 0x0010u
#define BUSTER_ABI_PASS_WIDE 0x0020u
#define BUSTER_ABI_PASS_MEMORY 0x0040u
#define BUSTER_ABI_PASS_UNCOUNTED 0x0080u
#define BUSTER_ABI_PASS_IGNORE 0x0100u
#define BUSTER_ABI_PASS_PAIR 0x0200u
#define BUSTER_ABI_PASS_SIGN 0x0400u
#define BUSTER_ABI_PASS_ZERO 0x0800u
#define BUSTER_ABI_PASS_AGGREGATE 0x1000u
#define BUSTER_ABI_PASS_END_MEMORY 0x2000u
#define BUSTER_ABI_PASS_UNSUPPORTED 0x4000u
#define BUSTER_ABI_PASS_CLASSIFIED 0x8000u
#define BUSTER_ABI_PASS_VALUE_SHIFT 16
#define BUSTER_ABI_PASS_VALUE (7u << BUSTER_ABI_PASS_VALUE_SHIFT)
#define BUSTER_ABI_PASS_ALIGNMENT_SHIFT 20
#define BUSTER_ABI_PASS_ALIGNMENT (63u << BUSTER_ABI_PASS_ALIGNMENT_SHIFT)
#define BUSTER_ABI_PASS_EMPTY 0x04000000u
#define BUSTER_ABI_PASS_LOW_GENERAL 0x08000000u
#define BUSTER_ABI_VALUE_NONE 0
#define BUSTER_ABI_VALUE_GENERAL 1
#define BUSTER_ABI_VALUE_PAIR 2
#define BUSTER_ABI_VALUE_VECTOR 3
#define BUSTER_ABI_VALUE_X87 4
#define BUSTER_ABI_VALUE_HALVES 5

// A result: IGNORE (void, or nothing to return), REGISTERS (registers[] in order, each holding part_bytes of the value:
// eightbytes, a vector register's, or 16 for an x87 register's 80-bit value), or MEMORY (the caller passes the address
// of the result in rdi, and the callee returns it in rax). flags: SIGN or ZERO (the callee extends to 32 bits), HIGH,
// and DEMOTED: a value clang returns that LLVM returns through a hidden pointer, rdi, which clang doesn't count.
#define BUSTER_ABI_RESULT_IGNORE 0
#define BUSTER_ABI_RESULT_REGISTERS 1
#define BUSTER_ABI_RESULT_MEMORY 2
#define BUSTER_ABI_RESULT_PARTS 4

typedef struct AbiReturn AbiReturn;
struct AbiReturn
{
    uint8_t kind;
    uint8_t flags;
    uint8_t part_bytes;
    uint8_t registers[BUSTER_ABI_RESULT_PARTS];
    uint8_t padding;
};
_Static_assert(sizeof(AbiReturn) == 8, "a result is 8 bytes");

// An argument: IGNORE (nothing passed), REGISTERS (registers[0] holds its first part and registers[1] its second, or
// NONE: eightbytes, or a vector register for a VECTOR, two general ones for a PAIR), STACK (the whole value at
// stack_offset from rsp at the call) or SPLIT (a part LLVM couldn't give a register, where clang's count runs ahead of
// LLVM's registers: the register of the part that has one, NONE for the one in the 8-byte slot at stack_offset).
// flags: SIGN or ZERO (the caller extends the value to 32 bits) and HIGH (the parts are the value's high eightbyte
// alone: its low one is padding).
#define BUSTER_ABI_LOCATION_IGNORE 0
#define BUSTER_ABI_LOCATION_REGISTERS 1
#define BUSTER_ABI_LOCATION_STACK 2
#define BUSTER_ABI_LOCATION_SPLIT 3
#define BUSTER_ABI_FLAG_SIGN 0x01
#define BUSTER_ABI_FLAG_ZERO 0x02
#define BUSTER_ABI_FLAG_HIGH 0x04
#define BUSTER_ABI_FLAG_DEMOTED 0x08

typedef struct AbiLocation AbiLocation;
struct AbiLocation
{
    uint32_t stack_offset;
    uint8_t registers[2];
    uint8_t kind;
    uint8_t flags;
};
_Static_assert(sizeof(AbiLocation) == 8, "a location is 8 bytes");

// A call's (or a function entry's) totals and result: the size of its argument area (a multiple of 8), whose start
// is rsp at the call, aligned to 2^stack_alignment_log2 (16, or more for an argument aligned more); the general and the
// vector registers its arguments use (the hidden pointer's among the former; al is vector_registers in a variadic call,
// and va_start's gp_offset and fp_offset start at 8 times the first and 48 plus 16 times the second).
typedef struct AbiCall AbiCall;
struct AbiCall
{
    uint32_t stack_bytes;
    uint8_t stack_alignment_log2;
    uint8_t integer_registers;
    uint8_t vector_registers;
    uint8_t padding;
    AbiReturn result;
};
_Static_assert(sizeof(AbiCall) == 16, "a call is 16 bytes");

// Calls, as columns: call c returns results[c] (BUSTER_TYPE_VOID for none) and takes counts[c] arguments, the first
// named[c] of them named (the rest are a variadic call's, after the default argument promotions); variadic[c] says
// whether the callee's type has "...", which LLVM's assignment depends on even where the call passes nothing past the
// named arguments (a call with unnamed ones must say variadic). Its arguments' types follow those of call c - 1 in
// `arguments`, which holds the sum of the counts. A function's entry is the call of its own type.
typedef struct AbiSignatures AbiSignatures;
struct AbiSignatures
{
    const TypeId* results;
    const uint32_t* counts;
    const uint32_t* named;
    const uint8_t* variadic;
    uint32_t count;
    const TypeId* arguments;
};

// va_arg's plan for a type (the psABI's 3.5.7 and clang's EmitVAArg), its kind a location's: IGNORE (nothing to read:
// va_list stays as it is), STACK (from overflow_arg_area alone) or REGISTERS (from the save area if gp_offset <= 48 - 8
// * integer_registers and fp_offset <= 176 - 16 * vector_registers, else from overflow_arg_area). sources[i] says where
// eightbyte i comes from in the save area: BUSTER_ABI_VA_GENERAL (gp_offset's slots, 8 bytes each),
// BUSTER_ABI_VA_VECTOR (fp_offset's, 16 bytes each, of which a VECTOR takes one for both eightbytes) or none; flags'
// HIGH: the value's low eightbyte is padding, and the register holds its high one. From overflow_arg_area: aligned to
// 2^overflow_alignment_log2 (8, or the type's if larger), stack_bytes read, and advanced past them.
#define BUSTER_ABI_VA_NONE 0
#define BUSTER_ABI_VA_GENERAL 1
#define BUSTER_ABI_VA_VECTOR 2

typedef struct AbiVaArg AbiVaArg;
struct AbiVaArg
{
    uint8_t kind;
    uint8_t integer_registers;
    uint8_t vector_registers;
    uint8_t sources[2];
    uint8_t flags;
    uint8_t overflow_alignment_log2;
    uint32_t stack_bytes;
};

// The table: per type index (the type table's), its row, how it passes, its stack bytes (its size rounded up to 8;
// UINT32_MAX past 4 GB, which no argument may be), its canonical alignment (ALIGNED looked through, as clang's canonical
// types have no typedefs, and arrays' elements too) and its result. merge is clang's merge, indexed by
// accumulated << 3 | field. vector_bytes is the widest vector register the generated code may use (16, 32 or 64: no AVX,
// AVX, AVX-512F), which decides how 32- and 64-byte vectors pass.
typedef struct AbiTable AbiTable;
// AbiTable.storage: clang's LLVM type of each type, as far as passing needs it (abi_storage): the log2 of its alignment
// in LLVM's layout, whether its storage at offset 0 is an i128 (which makes a record's two INTEGER eightbytes one value
// that LLVM never splits), and EMPTY: a record clang's layout counts empty (isEmptyRecordForLayout: each member a
// zero-width bit-field or of such a record's type), which doesn't end a run of bit-fields' room (abi_bit_field_run).
#define BUSTER_ABI_STORAGE_ALIGNMENT 0x0fu
#define BUSTER_ABI_STORAGE_I128 0x10u
#define BUSTER_ABI_STORAGE_EMPTY 0x20u

// AbiTable.layout: the scalars of clang's LLVM type of each type in its first 16 bytes, as GetSSETypeAtOffset and
// GetINTEGERTypeAtOffset find them (abi_layout): 4 bits for each even offset (bits 0 to 31: the kind of the scalar that
// starts there), and which of the 16 bytes hold data of the C type (bits 32 to 47, BitsContainNoUserData's).
#define BUSTER_ABI_LEAF_NONE 0u
#define BUSTER_ABI_LEAF_I8 1u
#define BUSTER_ABI_LEAF_I16 2u
#define BUSTER_ABI_LEAF_I32 3u
#define BUSTER_ABI_LEAF_I64 4u
#define BUSTER_ABI_LEAF_HALF 5u
#define BUSTER_ABI_LEAF_BFLOAT 6u
#define BUSTER_ABI_LEAF_FLOAT 7u
#define BUSTER_ABI_LEAF_DOUBLE 8u
#define BUSTER_ABI_LEAF_OTHER 9u
#define BUSTER_ABI_LAYOUT_DATA_SHIFT 32

struct AbiTable
{
    uint8_t* rows;
    uint32_t* pass;
    uint32_t* stack_bytes;
    uint8_t* alignment_log2;
    uint8_t* storage;
    uint64_t* layout;
    AbiReturn* results;
    uint32_t capacity;
    uint32_t vector_bytes;
    _Alignas(64) uint8_t merge[64];
};

// How many bytes abi_initialize pushes for a table of `types` types.
static __attribute__((unused)) uint64_t abi_capacity_bytes(uint32_t types);
// Pushes the columns from `arena` for every type `table` has room for and classifies the builtin types. Returns false,
// with the arena as it was, if it can't hold them or vector_bytes isn't 16, 32 or 64.
static __attribute__((unused)) bool abi_initialize(AbiTable* abi, Arena* arena, const TypeTable* table, uint32_t vector_bytes);
// Classifies every complete type the table has that isn't yet, a level at a time (each level's types depend only on
// types classified before it): with the vector path, or its twin. Returns how many types it classified.
static __attribute__((unused)) uint32_t abi_classify_all(AbiTable* abi, const TypeTable* table);
static __attribute__((unused)) uint32_t abi_classify_all_scalar(AbiTable* abi, const TypeTable* table);
// Classifies `count` types, every one of whose members, elements or bases is classified: the vector path (records and
// arrays folded four at a time, one per 128-bit lane) and its twin (an offset of a type at a time). A type classified
// already is classified again, to the same result.
static __attribute__((unused)) void abi_classify(AbiTable* abi, const TypeTable* table, const TypeId* types, uint32_t count);
static __attribute__((unused)) void abi_classify_scalar(AbiTable* abi, const TypeTable* table, const TypeId* types, uint32_t count);
// A classified type's classes, as a named argument, a result and a member see them, or as an unnamed (variadic) one.
static __attribute__((unused)) AbiClasses abi_classes(const AbiTable* abi, TypeId type, bool named);
// Each call's arguments' locations (`locations`, as `arguments` is indexed) and its totals and result (`calls`). Every
// argument's and result's type must be classified, and its pass bits not UNSUPPORTED, nor its stack bytes UINT32_MAX:
// the code generator refuses those first. The vector path takes 16 arguments at a time across calls, and gives the
// calls it can't decide exactly to its twin, which takes one call at a time.
static __attribute__((unused)) void abi_assign(const AbiTable* abi, AbiSignatures signatures, AbiLocation* locations, AbiCall* calls);
static __attribute__((unused)) void abi_assign_scalar(const AbiTable* abi, AbiSignatures signatures, AbiLocation* locations, AbiCall* calls);
// va_arg's plan for a classified type, after the default argument promotions.
static __attribute__((unused)) AbiVaArg abi_va_arg(const AbiTable* abi, const TypeTable* table, TypeId type);

// ---------------------------------------------------------------------------------------------------------------------
// Classes

// clang's merge (the psABI's 3.2.3 rule 4), with MEMORY absorbing on either side, as clang's loops stop at it.
static uint8_t abi_merge_class(uint8_t accumulated, uint8_t field)
{
    uint8_t result = BUSTER_ABI_SSE;
    if (accumulated == BUSTER_ABI_MEMORY || field == BUSTER_ABI_MEMORY)
    {
        result = BUSTER_ABI_MEMORY;
    }
    else if (accumulated == field || field == BUSTER_ABI_NO_CLASS)
    {
        result = accumulated;
    }
    else if (accumulated == BUSTER_ABI_NO_CLASS)
    {
        result = field;
    }
    else if (accumulated == BUSTER_ABI_INTEGER || field == BUSTER_ABI_INTEGER)
    {
        result = BUSTER_ABI_INTEGER;
    }
    else if (field == BUSTER_ABI_X87 || field == BUSTER_ABI_X87UP || field == BUSTER_ABI_COMPLEX_X87 || accumulated == BUSTER_ABI_X87 ||
             accumulated == BUSTER_ABI_X87UP)
    {
        result = BUSTER_ABI_MEMORY;
    }
    return result;
}

static AbiClasses abi_merge(AbiClasses accumulated, AbiClasses field)
{
    const uint8_t low = abi_merge_class(BUSTER_ABI_LOW(accumulated), BUSTER_ABI_LOW(field));
    const uint8_t high = abi_merge_class(BUSTER_ABI_HIGH(accumulated), BUSTER_ABI_HIGH(field));
    return BUSTER_ABI_CLASSES(low, high);
}

// MEMORY in either half is MEMORY in both.
static AbiClasses abi_canonical(uint8_t low, uint8_t high)
{
    return low == BUSTER_ABI_MEMORY || high == BUSTER_ABI_MEMORY ? BUSTER_ABI_IN_MEMORY : BUSTER_ABI_CLASSES(low, high);
}

// clang's postMerge (the psABI's 3.2.3 rule 5) for an aggregate of `size` bytes, as Linux has it (revision 0.98's X87UP
// rule).
static AbiClasses abi_post_merge(uint64_t size, AbiClasses classes)
{
    uint8_t low = BUSTER_ABI_LOW(classes);
    uint8_t high = BUSTER_ABI_HIGH(classes);
    if (high == BUSTER_ABI_MEMORY)
    {
        low = BUSTER_ABI_MEMORY;
    }
    if (high == BUSTER_ABI_X87UP && low != BUSTER_ABI_X87)
    {
        low = BUSTER_ABI_MEMORY;
    }
    if (size > BUSTER_ABI_REGISTER_BYTES && (low != BUSTER_ABI_SSE || high != BUSTER_ABI_SSEUP))
    {
        low = BUSTER_ABI_MEMORY;
    }
    if (high == BUSTER_ABI_SSEUP && low != BUSTER_ABI_SSE)
    {
        high = BUSTER_ABI_SSE;
    }
    return abi_canonical(low, high);
}

// ---------------------------------------------------------------------------------------------------------------------
// Simple types: everything but records and arrays, whose classes at an offset follow from what they are.

// FIXED: (low, high) at every offset the type can be at, as clang assigns both regardless of the offset (only offset 0
// is possible for these, which are 16 bytes or more, or MEMORY anywhere). CURRENT: `low` goes to the eightbyte the
// offset falls in, and to the high one too when the type crosses into it: when offset + span > 8 (span 1 for a scalar,
// which clang never splits; an element's size plus one for a complex type, whose imaginary part's start decides; the
// size for a vector, whose last byte does).
#define BUSTER_ABI_RULE_FIXED 0
#define BUSTER_ABI_RULE_CURRENT 1

typedef struct AbiSimple AbiSimple;
struct AbiSimple
{
    uint8_t rule;
    uint8_t low;
    uint8_t high;
    uint8_t span;
    uint64_t size;
};

static AbiSimple abi_simple_fixed(uint8_t low, uint8_t high, uint64_t size)
{
    const AbiSimple result = {
        .rule = BUSTER_ABI_RULE_FIXED,
        .low = low,
        .high = high,
        .span = 1,
        .size = size,
    };
    return result;
}

static AbiSimple abi_simple_current(uint8_t class, uint8_t span, uint64_t size)
{
    const AbiSimple result = {
        .rule = BUSTER_ABI_RULE_CURRENT,
        .low = class,
        .high = class,
        .span = span,
        .size = size,
    };
    return result;
}

// The index whose classes a type has: ALIGNED looked through (a typedef's alignment changes no class), qualifiers
// dropped.
static uint32_t abi_resolved_index(const TypeTable* table, TypeId type)
{
    uint32_t index = type >> BUSTER_TYPE_INDEX_SHIFT;
    if (table->types.kind[index] == BUSTER_TYPE_KIND_ALIGNED)
    {
        index = table->types.base[index] >> BUSTER_TYPE_INDEX_SHIFT;
    }
    return index;
}

// A floating type's code: what decides its class.
static AbiSimple abi_simple_floating(uint8_t code, uint64_t size)
{
    AbiSimple result = abi_simple_fixed(BUSTER_ABI_MEMORY, BUSTER_ABI_MEMORY, size);
    if (code == BUSTER_TYPE_CODE_FLOAT || code == BUSTER_TYPE_CODE_DOUBLE || code == BUSTER_TYPE_CODE_FLOAT16 || code == BUSTER_TYPE_CODE_BF16)
    {
        result = abi_simple_current(BUSTER_ABI_SSE, 1, size);
    }
    else if (code == BUSTER_TYPE_CODE_FLOAT128)
    {
        result = abi_simple_fixed(BUSTER_ABI_SSE, BUSTER_ABI_SSEUP, size);
    }
    else if (code == BUSTER_TYPE_CODE_LONG_DOUBLE)
    {
        result = abi_simple_fixed(BUSTER_ABI_X87, BUSTER_ABI_X87UP, size);
    }
    return result;
}

// What a simple type is, by its kind; `type_index` is resolved (not ALIGNED), and an enum is its underlying integer
// type (complete enums only reach here). Records and arrays aren't simple.
static AbiSimple abi_simple(const AbiTable* abi, const TypeTable* table, uint32_t type_index)
{
    const bool enumeration = table->types.kind[type_index] == BUSTER_TYPE_KIND_ENUM;
    const uint32_t index = enumeration ? table->types.base[type_index] >> BUSTER_TYPE_INDEX_SHIFT : type_index;
    const uint8_t kind = table->types.kind[index];
    const uint8_t code = table->types.code[index];
    const uint64_t size = table->types.size[index];
    AbiSimple result = abi_simple_fixed(BUSTER_ABI_MEMORY, BUSTER_ABI_MEMORY, size);
    if (kind == BUSTER_TYPE_KIND_VOID)
    {
        result = abi_simple_fixed(BUSTER_ABI_NO_CLASS, BUSTER_ABI_NO_CLASS, size);
    }
    else if (kind == BUSTER_TYPE_KIND_INTEGER && (code == BUSTER_TYPE_CODE_INT128 || code == BUSTER_TYPE_CODE_UINT128))
    {
        result = abi_simple_fixed(BUSTER_ABI_INTEGER, BUSTER_ABI_INTEGER, size);
    }
    else if (kind == BUSTER_TYPE_KIND_INTEGER || kind == BUSTER_TYPE_KIND_POINTER)
    {
        result = abi_simple_current(BUSTER_ABI_INTEGER, 1, size);
    }
    else if (kind == BUSTER_TYPE_KIND_FLOATING)
    {
        result = abi_simple_floating(code, size);
    }
    else if (kind == BUSTER_TYPE_KIND_BIT_INT)
    {
        const uint64_t width = table->types.extent[index];
        if (width <= 64)
        {
            result = abi_simple_current(BUSTER_ABI_INTEGER, 1, size);
        }
        else if (width <= 128)
        {
            result = abi_simple_fixed(BUSTER_ABI_INTEGER, BUSTER_ABI_INTEGER, size);
        }
    }
    else if (kind == BUSTER_TYPE_KIND_COMPLEX)
    {
        const uint32_t element = table->types.base[index] >> BUSTER_TYPE_INDEX_SHIFT;
        const uint8_t element_kind = table->types.kind[element];
        const uint8_t element_code = table->types.code[element];
        const uint64_t element_size = table->types.size[element];
        const uint8_t span = (uint8_t)(element_size + 1);
        if (element_kind == BUSTER_TYPE_KIND_INTEGER || element_kind == BUSTER_TYPE_KIND_BIT_INT)
        {
            if (size <= 8)
            {
                result = abi_simple_current(BUSTER_ABI_INTEGER, span, size);
            }
            else if (size <= BUSTER_ABI_REGISTER_BYTES)
            {
                result = abi_simple_fixed(BUSTER_ABI_INTEGER, BUSTER_ABI_INTEGER, size);
            }
        }
        else if (element_code == BUSTER_TYPE_CODE_FLOAT || element_code == BUSTER_TYPE_CODE_FLOAT16 || element_code == BUSTER_TYPE_CODE_BF16)
        {
            result = abi_simple_current(BUSTER_ABI_SSE, span, size);
        }
        else if (element_code == BUSTER_TYPE_CODE_DOUBLE)
        {
            result = abi_simple_fixed(BUSTER_ABI_SSE, BUSTER_ABI_SSE, size);
        }
        else if (element_code == BUSTER_TYPE_CODE_LONG_DOUBLE)
        {
            result = abi_simple_fixed(BUSTER_ABI_COMPLEX_X87, BUSTER_ABI_COMPLEX_X87, size);
        }
    }
    else if (kind == BUSTER_TYPE_KIND_VECTOR || kind == BUSTER_TYPE_KIND_EXT_VECTOR)
    {
        const uint32_t element = table->types.base[index] >> BUSTER_TYPE_INDEX_SHIFT;
        const uint8_t element_code = table->types.code[element];
        const bool double_element = table->types.kind[element] == BUSTER_TYPE_KIND_FLOATING && element_code == BUSTER_TYPE_CODE_DOUBLE;
        const bool int128_element = table->types.kind[element] == BUSTER_TYPE_KIND_INTEGER &&
                                    (element_code == BUSTER_TYPE_CODE_INT128 || element_code == BUSTER_TYPE_CODE_UINT128);
        if (size <= 4)
        {
            // gcc passed these as integers once (clang keeps it; GCC 16 passes floating ones as SSE).
            result = abi_simple_current(BUSTER_ABI_INTEGER, (uint8_t)size, size);
        }
        else if (size == 8)
        {
            // <1 x double> in memory; everything else, <1 x long long> included (Linux), SSE.
            result = double_element ? abi_simple_fixed(BUSTER_ABI_MEMORY, BUSTER_ABI_MEMORY, size) : abi_simple_current(BUSTER_ABI_SSE, 8, size);
        }
        else if (size == BUSTER_ABI_REGISTER_BYTES || size <= abi->vector_bytes)
        {
            // Wider vectors of __int128 in memory, as GCC passes them (Linux).
            result = int128_element && size != BUSTER_ABI_REGISTER_BYTES ? abi_simple_fixed(BUSTER_ABI_MEMORY, BUSTER_ABI_MEMORY, size)
                                                                          : abi_simple_fixed(BUSTER_ABI_SSE, BUSTER_ABI_SSEUP, size);
        }
    }
    // ATOMIC, NULLPTR, __fp16 (a FLOATING code above), _BitInt past 128 bits: MEMORY.
    return result;
}

// A simple type's classes at byte `offset` of an enclosing aggregate (MEMORY where it can't be: past offset 0, the
// aggregate would be larger than 16 bytes).
static AbiClasses abi_simple_entry(AbiSimple simple, uint32_t offset)
{
    AbiClasses result = BUSTER_ABI_IN_MEMORY;
    if (offset == 0 || offset + simple.size <= BUSTER_ABI_ROW_BYTES)
    {
        if (simple.rule == BUSTER_ABI_RULE_FIXED)
        {
            result = abi_canonical(simple.low, simple.high);
        }
        else if (offset < 8)
        {
            result = abi_canonical(simple.low, offset + simple.span > 8 ? simple.low : BUSTER_ABI_NO_CLASS);
        }
        else
        {
            result = abi_canonical(BUSTER_ABI_NO_CLASS, simple.low);
        }
    }
    return result;
}

// The same for the 16 offsets at once.
static __m128i abi_simple_row(AbiSimple simple)
{
    const __m128i lanes = _mm_setr_epi8(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15);
    const uint64_t room = simple.size < BUSTER_ABI_ROW_BYTES ? BUSTER_ABI_ROW_BYTES - simple.size : 0;
    // The offsets the type can be at: 0, and those that leave the aggregate at most 16 bytes.
    const __mmask16 possible = _mm_cmple_epu8_mask(lanes, _mm_set1_epi8((char)room)) | 1;
    __m128i classes = _mm_set1_epi8((char)abi_canonical(simple.low, simple.high));
    if (simple.rule == BUSTER_ABI_RULE_CURRENT)
    {
        const __mmask16 low_half = _mm_cmplt_epu8_mask(lanes, _mm_set1_epi8(8));
        const __mmask16 crossing = _mm_cmpgt_epu8_mask(_mm_add_epi8(lanes, _mm_set1_epi8((char)simple.span)), _mm_set1_epi8(8));
        const __m128i low_only = _mm_set1_epi8((char)BUSTER_ABI_CLASSES(simple.low, BUSTER_ABI_NO_CLASS));
        const __m128i both = _mm_set1_epi8((char)BUSTER_ABI_CLASSES(simple.low, simple.low));
        const __m128i high_only = _mm_set1_epi8((char)BUSTER_ABI_CLASSES(BUSTER_ABI_NO_CLASS, simple.low));
        classes = _mm_mask_blend_epi8(low_half, high_only, _mm_mask_blend_epi8(crossing, low_only, both));
    }
    return _mm_mask_blend_epi8(possible, _mm_set1_epi8((char)BUSTER_ABI_IN_MEMORY), classes);
}

// ---------------------------------------------------------------------------------------------------------------------
// Entries: a record's or an array's classes at one offset, from its members' rows, as clang's classify gives them.
// This is the scalar classification, the vector fold's twin.

static AbiClasses abi_row_at(const AbiTable* abi, uint32_t index, uint32_t offset)
{
    return abi->rows[(uint64_t)index * BUSTER_ABI_ROW_BYTES + offset];
}

static uint32_t abi_alignment_bytes_log2(const TypeTable* table, TypeId type)
{
    return table->types.alignment_log2[type >> BUSTER_TYPE_INDEX_SHIFT];
}

// An array at byte `offset`: MEMORY beyond 64 bytes or where its elements would be misaligned (its element's alignment
// as written, ALIGNED included); NO_CLASS for one beyond 16 bytes that isn't a single vector the target passes (clang
// returns before merging, and before post-merging); otherwise its elements' classes merged, then post-merged.
static AbiClasses abi_array_entry(const AbiTable* abi, const TypeTable* table, uint32_t index, uint32_t offset)
{
    const uint64_t size = table->types.size[index];
    const uint64_t count = table->types.extent[index];
    const TypeId element = table->types.base[index];
    const uint32_t element_index = element >> BUSTER_TYPE_INDEX_SHIFT;
    const uint64_t element_size = table->types.size[abi_resolved_index(table, element)];
    const uint64_t alignment = 1ull << abi_alignment_bytes_log2(table, element);
    AbiClasses result = BUSTER_ABI_IN_MEMORY;
    if (offset != 0 && offset + size > BUSTER_ABI_ROW_BYTES)
    {
    }
    else if (size > BUSTER_ABI_MAX_CLASSIFIED_BYTES || offset % alignment != 0)
    {
    }
    else if (size > BUSTER_ABI_REGISTER_BYTES && (size != element_size || size > abi->vector_bytes))
    {
        result = BUSTER_ABI_NOTHING;
    }
    else
    {
        AbiClasses accumulated = BUSTER_ABI_NOTHING;
        for (uint64_t i = 0; i < count && accumulated != BUSTER_ABI_IN_MEMORY; i += 1)
        {
            accumulated = abi_merge(accumulated, abi_row_at(abi, element_index, (uint32_t)((offset + i * element_size) % BUSTER_ABI_ROW_BYTES)));
        }
        result = abi_post_merge(size, accumulated);
    }
    return result;
}

// A bit-field of `width` bits at bit `start` of the enclosing aggregate: INTEGER in each eightbyte it touches, which
// may be both (bit-fields straddle, and never force memory).
static AbiClasses abi_bit_field_classes(uint64_t start, uint64_t width)
{
    const uint64_t last = start + width - 1;
    AbiClasses result = BUSTER_ABI_CLASSES(BUSTER_ABI_INTEGER, last >= 64 ? BUSTER_ABI_INTEGER : BUSTER_ABI_NO_CLASS);
    if (start >= 64)
    {
        result = BUSTER_ABI_CLASSES(BUSTER_ABI_NO_CLASS, BUSTER_ABI_INTEGER);
    }
    return result;
}

// A member's classes at byte `position` of the enclosing aggregate, for the positions it can be at: below 16, or 16 for
// a member of no size ending it, where it has its classes at 0 unless it is END_MEMORY: an array of no size whose
// elements' aligned typedef asks for more than 16 bytes (clang checks an array's offset against its elements'
// alignment as written, while the enclosing record checks the canonical type's), or a record with such a member.
static AbiClasses abi_member_row_at(const AbiTable* abi, uint32_t index, uint64_t position)
{
    AbiClasses result = abi_row_at(abi, index, (uint32_t)(position % BUSTER_ABI_ROW_BYTES));
    if (position == BUSTER_ABI_ROW_BYTES && (abi->pass[index] & BUSTER_ABI_PASS_END_MEMORY) != 0)
    {
        result = BUSTER_ABI_IN_MEMORY;
    }
    return result;
}

// A record at byte `offset`: MEMORY beyond 64 bytes or with a flexible array member; otherwise its members merged in
// order (unnamed bit-fields skipped), each at its offset: MEMORY for a member that isn't at a multiple of its canonical
// alignment (bit-fields excepted), and beyond 16 bytes for a member that isn't the size of a struct, or a record wider
// than the target's vectors; then post-merged.
static AbiClasses abi_record_entry(const AbiTable* abi, const TypeTable* table, uint32_t index, uint32_t offset)
{
    const uint64_t size = table->types.size[index];
    const bool is_union = table->types.kind[index] == BUSTER_TYPE_KIND_UNION;
    const uint32_t tag = table->types.index[index];
    AbiClasses result = BUSTER_ABI_IN_MEMORY;
    if (offset != 0 && offset + size > BUSTER_ABI_ROW_BYTES)
    {
    }
    else if (size > BUSTER_ABI_MAX_CLASSIFIED_BYTES || (table->types.flags[index] & BUSTER_TYPE_FLAG_FLEXIBLE) != 0)
    {
    }
    else
    {
        AbiClasses accumulated = BUSTER_ABI_NOTHING;
        const uint32_t begin = table->tags.member_begin[tag];
        const uint32_t end = begin + table->tags.member_count[tag];
        for (uint32_t member = begin; member < end && accumulated != BUSTER_ABI_IN_MEMORY; member += 1)
        {
            const TypeId type = table->members.type[member];
            const bool bit_field = (table->members.flags[member] & BUSTER_TYPE_MEMBER_BIT_FIELD) != 0;
            const uint64_t member_size = table->types.size[abi_resolved_index(table, type)];
            const uint64_t start = (uint64_t)offset * 8 + table->members.offset[member];
            AbiClasses field = BUSTER_ABI_NOTHING;
            if (bit_field && table->members.name[member] == 0)
            {
            }
            else if (size > BUSTER_ABI_REGISTER_BYTES && ((!is_union && size != member_size) || size > abi->vector_bytes))
            {
                field = BUSTER_ABI_IN_MEMORY;
            }
            else if (bit_field)
            {
                field = abi_bit_field_classes(start, table->members.bit_width[member]);
            }
            else if (start % (8ull << abi->alignment_log2[type >> BUSTER_TYPE_INDEX_SHIFT]) != 0)
            {
                field = BUSTER_ABI_IN_MEMORY;
            }
            else
            {
                field = abi_member_row_at(abi, type >> BUSTER_TYPE_INDEX_SHIFT, start / 8);
            }
            accumulated = abi_merge(accumulated, field);
        }
        result = abi_post_merge(size, accumulated);
    }
    return result;
}

// Any classifiable type's classes at an offset (an ALIGNED type's are its base's, copied when it is classified).
static AbiClasses abi_row_entry(const AbiTable* abi, const TypeTable* table, uint32_t index, uint32_t offset)
{
    const uint8_t kind = table->types.kind[index];
    AbiClasses result = BUSTER_ABI_IN_MEMORY;
    if (kind == BUSTER_TYPE_KIND_STRUCT || kind == BUSTER_TYPE_KIND_UNION)
    {
        result = abi_record_entry(abi, table, index, offset);
    }
    else if (kind == BUSTER_TYPE_KIND_ARRAY)
    {
        result = abi_array_entry(abi, table, index, offset);
    }
    else
    {
        result = abi_simple_entry(abi_simple(abi, table, index), offset);
    }
    return result;
}

// ---------------------------------------------------------------------------------------------------------------------
// Finishing a type: what follows from its row and what it is (both paths share it).

// The alignment clang's record classification checks a member's offset against: its canonical type's, with no
// ALIGNED, in arrays' elements either.
static uint8_t abi_canonical_alignment_log2(const AbiTable* abi, const TypeTable* table, uint32_t index)
{
    const uint8_t kind = table->types.kind[index];
    uint8_t result = table->types.alignment_log2[index];
    if (kind == BUSTER_TYPE_KIND_ALIGNED || kind == BUSTER_TYPE_KIND_ARRAY)
    {
        result = abi->alignment_log2[table->types.base[index] >> BUSTER_TYPE_INDEX_SHIFT];
    }
    return result;
}

// A member's storage in clang's LLVM type of its record (see abi_storage): its type's, or for a bit-field an integer: in
// a union iN for N the smaller of its width and its type's bits rounded up to bytes (lowerUnion's getStorageType, an
// i128 for 121 to 128 bits); in a struct, as aligned as its type, unless its run of bit-fields is wider than an
// eightbyte (abi_storage takes the run's unit from abi_bit_field_run; clang's smaller units, which this doesn't work
// out, are integers no wider than an eightbyte). `bytes` is its size there.
static uint8_t abi_member_storage(const AbiTable* abi, const TypeTable* table, uint32_t member, bool in_union, uint64_t* bytes)
{
    const uint32_t resolved = abi_resolved_index(table, table->members.type[member]);
    const bool bit_field = (table->members.flags[member] & BUSTER_TYPE_MEMBER_BIT_FIELD) != 0;
    const uint32_t width = table->members.bit_width[member];
    const uint64_t type_bits = table->types.size[resolved] * 8;
    // getIntNType rounds the bits up to bytes: an i128 for 121 to 128.
    const uint64_t bits = ((width < type_bits ? width : type_bits) + 7) & ~(uint64_t)7;
    // iN's alignment and size: those of the smallest of i8, i16, i32, i64 and i128 that holds it.
    const uint8_t integer_log2 = bits <= 8 ? 0 : bits <= 16 ? 1 : bits <= 32 ? 2 : bits <= 64 ? 3 : 4;
    uint8_t result = abi->storage[resolved];
    *bytes = table->types.size[resolved];
    if (bit_field && in_union)
    {
        result = (uint8_t)(integer_log2 | (bits == 128 ? BUSTER_ABI_STORAGE_I128 : 0));
        *bytes = 1ull << integer_log2;
    }
    else if (bit_field)
    {
        result = (uint8_t)(abi->storage[resolved] & BUSTER_ABI_STORAGE_ALIGNMENT);
    }
    return result;
}

// The access unit clang gives a run of a struct's bit-fields (CGRecordLowering::accumulateBitFields, with x86-64's cheap
// unaligned accesses): the run starts with `member`, a bit-field at a byte's start, and takes the bit-fields after it
// that start inside a byte. Past an eightbyte, clang adds no other run to its unit, which is the integer its bytes
// round up to (16 for 9 to 16: an i128, aligned to 16) when that fits before the next member with storage (but a
// zero-width bit-field and an empty record) or else the struct's end, and otherwise an array of its bytes, aligned to 1:
// `struct { unsigned __int128 x : 65; }` stores an i128, and packed or followed by a char, a [9 x i8]. Returns the
// unit's storage, or UINT8_MAX for a run of an eightbyte or less (its bit-fields keep their types' alignments, see
// abi_member_storage), and in `run_end` the member after the run. `members` end at `end`, in a struct of `size` bytes.
static uint8_t abi_bit_field_run(const AbiTable* abi, const TypeTable* table, uint32_t member, uint32_t end, uint64_t size, uint32_t* run_end)
{
    const uint64_t start = table->members.offset[member];
    uint64_t stop = start + table->members.bit_width[member];
    uint32_t next = member + 1;
    while (next < end && (table->members.flags[next] & BUSTER_TYPE_MEMBER_BIT_FIELD) != 0 && (table->members.offset[next] & 7) != 0)
    {
        const uint64_t last = table->members.offset[next] + table->members.bit_width[next];
        stop = last > stop ? last : stop;
        next += 1;
    }
    *run_end = next;
    const uint64_t bytes = (stop - start + 7) >> 3;
    uint8_t result = UINT8_MAX;
    if (bytes > 8)
    {
        uint64_t limit = size;
        bool found = false;
        for (uint32_t probe = next; probe < end && !found; probe += 1)
        {
            const bool bit_field = (table->members.flags[probe] & BUSTER_TYPE_MEMBER_BIT_FIELD) != 0;
            const bool zero_width = bit_field && table->members.bit_width[probe] == 0;
            const uint32_t type = abi_resolved_index(table, table->members.type[probe]);
            const bool empty = !bit_field && (abi->storage[type] & BUSTER_ABI_STORAGE_EMPTY) != 0;
            found = !zero_width && !empty;
            limit = found ? table->members.offset[probe] >> 3 : limit;
        }
        // LLVM's size of an integer of more than 8 bytes: a multiple of 16.
        const uint64_t natural = (bytes + 15) & ~(uint64_t)15;
        const bool fits = (start >> 3) + natural <= limit;
        result = !fits ? 0 : natural == 16 ? (uint8_t)(4 | BUSTER_ABI_STORAGE_I128) : 4;
    }
    return result;
}

// The member clang stores a union as (lowerUnion): the one whose storage (abi_member_storage) LLVM aligns most, then
// the largest, the first of equals, and no zero-width bit-field. UINT32_MAX if it has none.
static uint32_t abi_union_storage_member(const AbiTable* abi, const TypeTable* table, uint32_t index)
{
    const uint32_t tag = table->types.index[index];
    const uint32_t begin = table->tags.member_begin[tag];
    const uint32_t end = begin + table->tags.member_count[tag];
    uint32_t result = UINT32_MAX;
    uint8_t largest = 0;
    uint64_t largest_bytes = 0;
    for (uint32_t member = begin; member < end; member += 1)
    {
        const bool zero_width = (table->members.flags[member] & BUSTER_TYPE_MEMBER_BIT_FIELD) != 0 && table->members.bit_width[member] == 0;
        uint64_t bytes = 0;
        const uint8_t alignment = abi_member_storage(abi, table, member, true, &bytes) & BUSTER_ABI_STORAGE_ALIGNMENT;
        const bool better = !zero_width && (result == UINT32_MAX || alignment > largest || (alignment == largest && bytes > largest_bytes));
        result = better ? member : result;
        largest = better ? alignment : largest;
        largest_bytes = better ? bytes : largest_bytes;
    }
    return result;
}

// clang's LLVM type for a type whose members' are in place (AbiTable.storage), from CGRecordLowering and LLVM's
// x86-64 data layout: a 128-bit integer is an i128, aligned to 16, and so is a _BitInt of 65 to 128 bits in memory (a
// wider one is an integer aligned to 16 too); a struct's storage at offset 0 is its first member's there with a size
// (for a bit-field, its run's unit: abi_bit_field_run), and it is aligned to its members' largest alignment unless a
// member is off its own or the size isn't a multiple of it (determinePacked: a packed struct, aligned to 1); a union's
// storage is its most aligned and then largest member's, the first of equals (lowerUnion), packed if the size isn't a
// multiple of its alignment; an array's, a complex type's, an enum's and an _Atomic one's are their element's,
// underlying type's or value's (never EMPTY); any other type is aligned as in C. A record with a flexible array member
// is MEMORY, its members maybe not classified yet: nothing.
static uint8_t abi_storage(const AbiTable* abi, const TypeTable* table, uint32_t index)
{
    const uint8_t kind = table->types.kind[index];
    const uint8_t code = table->types.code[index];
    const uint64_t size = table->types.size[index];
    const bool record = kind == BUSTER_TYPE_KIND_STRUCT || kind == BUSTER_TYPE_KIND_UNION;
    uint8_t result = (uint8_t)(table->types.alignment_log2[index] & BUSTER_ABI_STORAGE_ALIGNMENT);
    if (kind == BUSTER_TYPE_KIND_INTEGER && (code == BUSTER_TYPE_CODE_INT128 || code == BUSTER_TYPE_CODE_UINT128))
    {
        result = 4 | BUSTER_ABI_STORAGE_I128;
    }
    else if (kind == BUSTER_TYPE_KIND_BIT_INT)
    {
        result = size <= 8 ? (uint8_t)__builtin_ctzll(size) : (uint8_t)(4 | (size == 16 ? BUSTER_ABI_STORAGE_I128 : 0));
    }
    else if (kind == BUSTER_TYPE_KIND_ARRAY || kind == BUSTER_TYPE_KIND_INCOMPLETE_ARRAY || kind == BUSTER_TYPE_KIND_COMPLEX || kind == BUSTER_TYPE_KIND_ENUM ||
             kind == BUSTER_TYPE_KIND_ATOMIC)
    {
        result = (uint8_t)(abi->storage[table->types.base[index] >> BUSTER_TYPE_INDEX_SHIFT] & ~BUSTER_ABI_STORAGE_EMPTY);
    }
    else if (record && (table->types.flags[index] & BUSTER_TYPE_FLAG_FLEXIBLE) != 0)
    {
        result = 0;
    }
    else if (record)
    {
        const bool in_union = kind == BUSTER_TYPE_KIND_UNION;
        const uint32_t tag = table->types.index[index];
        const uint32_t begin = table->tags.member_begin[tag];
        const uint32_t end = begin + table->tags.member_count[tag];
        uint8_t largest = 0;
        uint8_t chosen = 0;
        bool found = false;
        bool packed = false;
        bool empty = true;
        uint32_t run_end = begin;
        uint8_t run_storage = UINT8_MAX;
        const uint32_t stored = in_union ? abi_union_storage_member(abi, table, index) : UINT32_MAX;
        for (uint32_t member = begin; member < end; member += 1)
        {
            const bool bit_field = (table->members.flags[member] & BUSTER_TYPE_MEMBER_BIT_FIELD) != 0;
            const bool zero_width = bit_field && table->members.bit_width[member] == 0;
            uint64_t bytes = 0;
            uint8_t storage = abi_member_storage(abi, table, member, in_union, &bytes);
            // A struct's bit-fields share their run's unit when it is wider than an eightbyte.
            const bool in_run = !in_union && bit_field && !zero_width;
            run_storage = in_run && member >= run_end ? abi_bit_field_run(abi, table, member, end, size, &run_end) : run_storage;
            storage = in_run && run_storage != UINT8_MAX ? run_storage : storage;
            empty = empty && (zero_width || (!bit_field && (storage & BUSTER_ABI_STORAGE_EMPTY) != 0));
            const uint8_t alignment = storage & BUSTER_ABI_STORAGE_ALIGNMENT;
            const uint64_t offset = table->members.offset[member];
            if (member == stored)
            {
                // A storage member of no size (a zero-length array LLVM aligns most) stores nothing at offset 0: the
                // padding after it is bytes.
                chosen = bytes != 0 ? storage : alignment;
                largest = alignment;
            }
            else if (!zero_width && !in_union)
            {
                packed = packed || (!bit_field && ((offset >> 3) & ((1ull << alignment) - 1)) != 0);
                largest = alignment > largest ? alignment : largest;
                const bool first = !found && offset == 0 && bytes != 0;
                chosen = first ? storage : chosen;
                found = found || first;
            }
        }
        packed = packed || (size & ((1ull << largest) - 1)) != 0;
        result = (uint8_t)((packed ? 0 : largest) | (chosen & BUSTER_ABI_STORAGE_I128) | (empty ? BUSTER_ABI_STORAGE_EMPTY : 0));
    }
    return result;
}

// The data bytes [offset, offset + size) of a layout's 16 (AbiTable.layout).
static uint64_t abi_layout_bytes(uint64_t offset, uint64_t size)
{
    const uint64_t first = offset < 16 ? offset : 16;
    const uint64_t last = size < 16 - first ? first + size : 16;
    return (((1ull << (last - first)) - 1) << first) << BUSTER_ABI_LAYOUT_DATA_SHIFT;
}

// A member's or an element's layout `offset` bytes into its enclosing type's: its data, and its scalars when the offset
// is even (at an odd one, they are at offsets GetSSETypeAtOffset and GetINTEGERTypeAtOffset don't ask about).
static uint64_t abi_layout_at(uint64_t layout, uint64_t offset)
{
    uint64_t result = 0;
    if (offset < 16)
    {
        const uint64_t data = ((layout >> BUSTER_ABI_LAYOUT_DATA_SHIFT) << offset) & 0xffffu;
        const uint64_t leaves = (offset & 1) == 0 ? ((layout & 0xffffffffu) << (offset * 2)) & 0xffffffffu : 0;
        result = leaves | data << BUSTER_ABI_LAYOUT_DATA_SHIFT;
    }
    return result;
}

// The kind of an integer of `bytes` bytes as a scalar of a layout.
static uint64_t abi_integer_leaf(uint64_t bytes)
{
    return bytes == 1 ? BUSTER_ABI_LEAF_I8 : bytes == 2 ? BUSTER_ABI_LEAF_I16 : bytes == 4 ? BUSTER_ABI_LEAF_I32 : bytes == 8 ? BUSTER_ABI_LEAF_I64 : BUSTER_ABI_LEAF_OTHER;
}

// A type's layout (AbiTable.layout) once its members' are in place: a scalar's own kind at 0 (an integer or a _BitInt
// by its size: i8 to i64, its memory type; a vector, an i128 and the like OTHER); an enum's, an _Atomic one's, a complex
// type's and an array's from their underlying type's, value's or elements'; a struct's from its members' at their
// offsets, a bit-field as an integer of its type at the start of its storage unit (an approximation of clang's access
// units) and data over its type's size from its offset; a union's scalars its storage member's
// (abi_union_storage_member), as in its LLVM type, and its data every member's. A record with a flexible array member
// is MEMORY: nothing.
static uint64_t abi_layout(const AbiTable* abi, const TypeTable* table, uint32_t index)
{
    const uint8_t kind = table->types.kind[index];
    const uint8_t code = table->types.code[index];
    const uint64_t size = table->types.size[index];
    const uint32_t base = table->types.base[index] >> BUSTER_TYPE_INDEX_SHIFT;
    const bool record = kind == BUSTER_TYPE_KIND_STRUCT || kind == BUSTER_TYPE_KIND_UNION;
    uint64_t leaf = BUSTER_ABI_LEAF_OTHER;
    if (kind == BUSTER_TYPE_KIND_INTEGER || kind == BUSTER_TYPE_KIND_BIT_INT)
    {
        leaf = abi_integer_leaf(size);
    }
    else if (kind == BUSTER_TYPE_KIND_FLOATING)
    {
        leaf = code == BUSTER_TYPE_CODE_FLOAT16 || code == BUSTER_TYPE_CODE_HALF ? BUSTER_ABI_LEAF_HALF
               : code == BUSTER_TYPE_CODE_BF16                                   ? BUSTER_ABI_LEAF_BFLOAT
               : code == BUSTER_TYPE_CODE_FLOAT                                  ? BUSTER_ABI_LEAF_FLOAT
               : code == BUSTER_TYPE_CODE_DOUBLE                                 ? BUSTER_ABI_LEAF_DOUBLE
                                                                                 : BUSTER_ABI_LEAF_OTHER;
    }
    else if (kind == BUSTER_TYPE_KIND_POINTER || kind == BUSTER_TYPE_KIND_NULLPTR)
    {
        leaf = BUSTER_ABI_LEAF_I64;
    }
    uint64_t result = leaf | abi_layout_bytes(0, size);
    if (kind == BUSTER_TYPE_KIND_ENUM || kind == BUSTER_TYPE_KIND_ATOMIC)
    {
        result = abi->layout[base];
    }
    else if (kind == BUSTER_TYPE_KIND_COMPLEX)
    {
        result = abi->layout[base] | abi_layout_at(abi->layout[base], table->types.size[base]);
    }
    else if (kind == BUSTER_TYPE_KIND_ARRAY)
    {
        const uint64_t element = table->types.size[base];
        const uint64_t count = table->types.extent[index];
        result = 0;
        for (uint64_t i = 0; element != 0 && i < count && i * element < 16; i += 1)
        {
            result |= abi_layout_at(abi->layout[base], i * element);
        }
    }
    else if (kind == BUSTER_TYPE_KIND_INCOMPLETE_ARRAY || kind == BUSTER_TYPE_KIND_VOID || kind == BUSTER_TYPE_KIND_FUNCTION ||
             (record && (table->types.flags[index] & BUSTER_TYPE_FLAG_FLEXIBLE) != 0))
    {
        result = 0;
    }
    else if (record)
    {
        const bool in_union = kind == BUSTER_TYPE_KIND_UNION;
        const uint32_t stored = in_union ? abi_union_storage_member(abi, table, index) : UINT32_MAX;
        const uint32_t tag = table->types.index[index];
        const uint32_t begin = table->tags.member_begin[tag];
        const uint32_t end = begin + table->tags.member_count[tag];
        uint64_t leaves = 0;
        uint64_t data = 0;
        // The bytes the LLVM type's fields cover: the rest is padding, arrays of i8 GetINTEGERTypeAtOffset finds.
        uint64_t covered = 0;
        for (uint32_t member = begin; member < end; member += 1)
        {
            const uint32_t type = abi_resolved_index(table, table->members.type[member]);
            const bool bit_field = (table->members.flags[member] & BUSTER_TYPE_MEMBER_BIT_FIELD) != 0;
            const uint64_t width = table->members.bit_width[member];
            const uint64_t offset = table->members.offset[member];
            const uint64_t unit = table->types.size[type];
            const bool stores = !in_union || member == stored;
            // A bit-field's storage: in a union, an integer of its width rounded to bytes at 0 (lowerUnion), in a struct
            // its type's storage unit.
            const uint64_t bits = ((width < unit * 8 ? width : unit * 8) + 7) & ~(uint64_t)7;
            const uint64_t storage_bytes = in_union ? (bits <= 8 ? 1 : bits <= 16 ? 2 : bits <= 32 ? 4 : bits <= 64 ? 8 : 16) : unit;
            const uint64_t storage_offset = in_union ? 0 : unit != 0 ? (offset / (unit * 8)) * unit : 0;
            if (bit_field && width != 0)
            {
                // Its storage's integer, unless a bit-field before it in the unit put one there; and data from its
                // offset over its type's size (BitsContainNoUserData looks at a bit-field's type, not its width).
                const uint64_t first = offset >> 3;
                const uint64_t unit_layout = abi_layout_at(abi_integer_leaf(storage_bytes), storage_offset);
                const uint64_t slot_mask = storage_offset < 16 && (storage_offset & 1) == 0 ? 15ull << (storage_offset * 2) : 0;
                const bool keep = in_union ? stores : (leaves & slot_mask) == 0;
                leaves |= keep ? unit_layout & 0xffffffffu : 0;
                covered |= stores ? abi_layout_bytes(storage_offset, storage_bytes) : 0;
                data |= abi_layout_bytes(first, ((offset + unit * 8 - 1) >> 3) - first + 1);
            }
            else if (!bit_field)
            {
                const uint64_t placed = abi_layout_at(abi->layout[type], offset >> 3);
                leaves |= stores ? placed & 0xffffffffu : 0;
                covered |= stores ? abi_layout_bytes(offset >> 3, table->types.size[type]) : 0;
                data |= placed & ~(uint64_t)0xffffffffu;
            }
        }
        // An i8 at each even offset of the padding within the type's first 16 bytes.
        const uint64_t holes = ~(covered >> BUSTER_ABI_LAYOUT_DATA_SHIFT) & ((1ull << (size < 16 ? size : 16)) - 1);
        for (uint64_t offset = 0; offset < 16; offset += 2)
        {
            leaves |= (holes >> offset & 1) != 0 ? BUSTER_ABI_LEAF_I8 << (offset * 2) : 0;
        }
        result = leaves | data;
    }
    return result;
}

// The scalar a layout has at byte `offset` (even, or none).
static uint32_t abi_leaf(uint64_t layout, uint64_t offset)
{
    return offset < 16 && (offset & 1) == 0 ? (uint32_t)(layout >> (offset * 2)) & 15u : BUSTER_ABI_LEAF_NONE;
}

static bool abi_leaf_is_floating(uint32_t leaf)
{
    return leaf >= BUSTER_ABI_LEAF_HALF && leaf <= BUSTER_ABI_LEAF_DOUBLE;
}

// clang's type for an SSE eightbyte at `base` of a type of `size` bytes (GetSSETypeAtOffset): a double unless a float
// or a 16-bit one starts it; a float or a 16-bit one alone (a 16-bit one looks for a second at 2, then at 4); two
// floats, <2 x float>; two 16-bit ones, <2 x T> or, with another at 4, <4 x T>; a 16-bit one and a float, <4 x half>.
// Returns the log2 of its alignment, and whether it is narrow: a bfloat, <2 x half> or <2 x bfloat>, which
// GetX86_64ByValArgumentPair widens to an i64 (a half or a float, to a double).
static uint32_t abi_sse_part(uint64_t layout, uint64_t base, uint64_t size, bool* narrow)
{
    const uint64_t source = size - base;
    const uint32_t first = abi_leaf(layout, base);
    const bool first_16 = first == BUSTER_ABI_LEAF_HALF || first == BUSTER_ABI_LEAF_BFLOAT;
    const uint64_t first_bytes = first_16 ? 2 : 4;
    const uint32_t next = source > first_bytes ? abi_leaf(layout, base + first_bytes) : BUSTER_ABI_LEAF_NONE;
    const uint32_t later = first_16 && source > 4 ? abi_leaf(layout, base + 4) : BUSTER_ABI_LEAF_NONE;
    const uint32_t second = abi_leaf_is_floating(next) ? next : abi_leaf_is_floating(later) ? later : BUSTER_ABI_LEAF_NONE;
    const bool second_16 = second == BUSTER_ABI_LEAF_HALF || second == BUSTER_ABI_LEAF_BFLOAT;
    const bool four = abi_leaf_is_floating(later);
    uint32_t result = 3;
    *narrow = false;
    if (!abi_leaf_is_floating(first) || first == BUSTER_ABI_LEAF_DOUBLE)
    {
    }
    else if (second == BUSTER_ABI_LEAF_NONE)
    {
        result = first_16 ? 1 : 2;
        *narrow = first == BUSTER_ABI_LEAF_BFLOAT;
    }
    else if (first_16 && second_16)
    {
        result = four ? 3 : 2;
        *narrow = !four;
    }
    return result;
}

// The log2 of the alignment of clang's type for the INTEGER eightbyte at 8 of a type of `size` bytes
// (GetINTEGERTypeAtOffset): the i64 or pointer that starts it; the i8, i16 or i32 that does when no data follows it in
// the eightbyte; otherwise an integer of the bytes left, at most 8 (LLVM aligns an i24 as an i32, an i40 to i56 as an
// i64).
static uint32_t abi_integer_part(uint64_t layout, uint64_t size)
{
    const uint32_t leaf = abi_leaf(layout, 8);
    const uint64_t data = layout >> BUSTER_ABI_LAYOUT_DATA_SHIFT;
    const uint32_t bytes_log2 = leaf == BUSTER_ABI_LEAF_I8 ? 0 : leaf == BUSTER_ABI_LEAF_I16 ? 1 : 2;
    const uint64_t after = 0xffffu & ~((1ull << (8 + (1u << bytes_log2))) - 1);
    const bool alone = leaf >= BUSTER_ABI_LEAF_I8 && leaf <= BUSTER_ABI_LEAF_I32 && (data & after) == 0;
    const uint64_t left = size - 8 < 8 ? size - 8 : 8;
    const uint32_t fallback = left <= 1 ? 0 : left == 2 ? 1 : left <= 4 ? 2 : 3;
    return leaf == BUSTER_ABI_LEAF_I64 ? 3 : alone ? bytes_log2 : fallback;
}

// Whether a type's low eightbyte, SSE by its class, goes in a general register (BUSTER_ABI_PASS_LOW_GENERAL): clang's
// type for it is narrow and its high part's isn't aligned to 8, so the pair would put the high part before 8.
static bool abi_low_general(const AbiTable* abi, const TypeTable* table, uint32_t index, uint8_t low, uint8_t high)
{
    const uint64_t layout = abi->layout[index];
    const uint64_t size = table->types.size[index];
    bool narrow = false;
    bool high_narrow = false;
    bool result = false;
    if (low == BUSTER_ABI_SSE && (high == BUSTER_ABI_INTEGER || high == BUSTER_ABI_SSE) && size > 8)
    {
        abi_sse_part(layout, 0, size, &narrow);
        const uint32_t high_alignment_log2 = high == BUSTER_ABI_INTEGER ? abi_integer_part(layout, size) : abi_sse_part(layout, 8, size, &high_narrow);
        result = narrow && high_alignment_log2 < 3;
    }
    return result;
}

// Whether clang passes the type's two INTEGER eightbytes as one i128, which LLVM never splits: __int128 and
// _BitInt(128) by themselves (a _BitInt of 65 to 127 bits alone passes as two i64), and aggregates whose LLVM storage
// at offset 0 is an i128 (abi_storage).
static bool abi_is_pair(const AbiTable* abi, const TypeTable* table, uint32_t index)
{
    const uint8_t kind = table->types.kind[index];
    const uint8_t code = table->types.code[index];
    bool result = (abi->storage[index] & BUSTER_ABI_STORAGE_I128) != 0;
    if (kind == BUSTER_TYPE_KIND_INTEGER)
    {
        result = code == BUSTER_TYPE_CODE_INT128 || code == BUSTER_TYPE_CODE_UINT128;
    }
    else if (kind == BUSTER_TYPE_KIND_BIT_INT)
    {
        result = table->types.extent[index] == 128;
    }
    return result;
}

// The kind of value LLVM gets for a scalar passed as its value: the value type of an _Atomic one.
static uint32_t abi_value_class(const TypeTable* table, uint32_t index)
{
    uint32_t value = index;
    if (table->types.kind[value] == BUSTER_TYPE_KIND_ATOMIC)
    {
        value = abi_resolved_index(table, table->types.base[value]);
    }
    if (table->types.kind[value] == BUSTER_TYPE_KIND_ENUM)
    {
        value = table->types.base[value] >> BUSTER_TYPE_INDEX_SHIFT;
    }
    // LLVM scalarizes a vector of one element: its value goes where the element's would (<1 x i16> in a general
    // register), but for a <1 x i128>, whose halves it splits.
    const bool single = (table->types.kind[value] == BUSTER_TYPE_KIND_VECTOR || table->types.kind[value] == BUSTER_TYPE_KIND_EXT_VECTOR) &&
                        table->types.extent[value] == 1;
    value = single ? abi_resolved_index(table, table->types.base[value]) : value;
    const uint8_t kind = table->types.kind[value];
    const uint8_t code = table->types.code[value];
    uint32_t result = single && kind == BUSTER_TYPE_KIND_INTEGER && table->types.size[value] == 16 ? BUSTER_ABI_VALUE_HALVES : BUSTER_ABI_VALUE_GENERAL;
    if (kind == BUSTER_TYPE_KIND_FLOATING)
    {
        result = code == BUSTER_TYPE_CODE_LONG_DOUBLE ? BUSTER_ABI_VALUE_X87 : BUSTER_ABI_VALUE_VECTOR;
    }
    else if (kind == BUSTER_TYPE_KIND_VECTOR || kind == BUSTER_TYPE_KIND_EXT_VECTOR)
    {
        result = BUSTER_ABI_VALUE_VECTOR;
    }
    else if (kind == BUSTER_TYPE_KIND_INTEGER && table->types.size[value] == 16 && !single)
    {
        result = BUSTER_ABI_VALUE_PAIR;
    }
    return result;
}

// The natural alignment (log2) of a scalar's value as LLVM lays it on the stack: an enum's integer type's (not its
// aligned attribute's), an _Atomic scalar's own.
static uint32_t abi_value_alignment_log2(const TypeTable* table, uint32_t index)
{
    uint32_t value = index;
    if (table->types.kind[value] == BUSTER_TYPE_KIND_ENUM)
    {
        value = table->types.base[value] >> BUSTER_TYPE_INDEX_SHIFT;
    }
    return table->types.alignment_log2[value];
}

// Whether clang treats the type as an aggregate: by value as a copy (byval) when it goes in memory as an argument, and
// through a hidden pointer as a result: records, arrays, complex types, _Atomic ones of those, and _BitInts (which
// clang passes in memory as aggregates).
static bool abi_is_aggregate(const TypeTable* table, uint32_t index)
{
    uint8_t kind = table->types.kind[index];
    if (kind == BUSTER_TYPE_KIND_ATOMIC)
    {
        kind = table->types.kind[abi_resolved_index(table, table->types.base[index])];
    }
    return kind == BUSTER_TYPE_KIND_STRUCT || kind == BUSTER_TYPE_KIND_UNION || kind == BUSTER_TYPE_KIND_ARRAY || kind == BUSTER_TYPE_KIND_COMPLEX ||
           kind == BUSTER_TYPE_KIND_BIT_INT;
}

// A vector LLVM has no register type for (clang's IsIllegalVectorType): 8 bytes or less, wider than the target's
// vector registers, or a wide one of __int128s. As an argument in memory it is passed as an aggregate (byval); as a
// result, it is returned as its value.
static bool abi_is_illegal_vector(const AbiTable* abi, const TypeTable* table, uint32_t index)
{
    const uint8_t kind = table->types.kind[index];
    const uint64_t size = table->types.size[index];
    bool result = false;
    if (kind == BUSTER_TYPE_KIND_VECTOR || kind == BUSTER_TYPE_KIND_EXT_VECTOR)
    {
        const uint32_t element = table->types.base[index] >> BUSTER_TYPE_INDEX_SHIFT;
        const bool int128_element = table->types.kind[element] == BUSTER_TYPE_KIND_INTEGER && table->types.size[element] == 16;
        result = size <= 8 || size > abi->vector_bytes || int128_element;
    }
    return result;
}

// Whether clang extends the value (Extend): bool, the character and short types, enums of them, and _BitInts narrower
// than int; how, by their signedness (char is signed on x86-64).
static uint32_t abi_extension(const TypeTable* table, uint32_t index)
{
    uint32_t value = index;
    if (table->types.kind[value] == BUSTER_TYPE_KIND_ENUM)
    {
        value = table->types.base[value] >> BUSTER_TYPE_INDEX_SHIFT;
    }
    const uint8_t kind = table->types.kind[value];
    const uint8_t code = table->types.code[value];
    uint32_t result = 0;
    if (kind == BUSTER_TYPE_KIND_INTEGER && table->types.size[value] < 4)
    {
        const bool is_unsigned = code == BUSTER_TYPE_CODE_BOOL || code == BUSTER_TYPE_CODE_UCHAR || code == BUSTER_TYPE_CODE_USHORT;
        result = is_unsigned ? BUSTER_ABI_PASS_ZERO : BUSTER_ABI_PASS_SIGN;
    }
    else if (kind == BUSTER_TYPE_KIND_BIT_INT && table->types.extent[value] < 32)
    {
        result = (table->types.flags[value] & BUSTER_TYPE_FLAG_UNSIGNED) != 0 ? BUSTER_ABI_PASS_ZERO : BUSTER_ABI_PASS_SIGN;
    }
    return result;
}

// A MEMORY scalar result, as LLVM returns its value: in a general register (rax), two (rax, rdx), x87's st0, or vector
// registers, as many of the target's widest as the value needs (LLVM splits a wider one), each holding part_bytes of
// it. A vector that would need more than four, or of __int128s, LLVM returns through a hidden pointer, as clang's
// MEMORY results.
static void abi_value_result(const AbiTable* abi, const TypeTable* table, uint32_t index, uint32_t value, AbiReturn* result)
{
    const uint64_t size = table->types.size[index];
    result->kind = BUSTER_ABI_RESULT_REGISTERS;
    if (value == BUSTER_ABI_VALUE_GENERAL)
    {
        result->registers[0] = BUSTER_ABI_RAX;
        result->part_bytes = (uint8_t)size;
    }
    else if (value == BUSTER_ABI_VALUE_PAIR || value == BUSTER_ABI_VALUE_HALVES)
    {
        result->registers[0] = BUSTER_ABI_RAX;
        result->registers[1] = BUSTER_ABI_RDX;
        result->part_bytes = 8;
    }
    else if (value == BUSTER_ABI_VALUE_X87)
    {
        result->registers[0] = BUSTER_ABI_REGISTER_X87;
        result->part_bytes = 16;
    }
    else
    {
        const uint32_t vector = table->types.kind[index] == BUSTER_TYPE_KIND_ATOMIC ? abi_resolved_index(table, table->types.base[index]) : index;
        const uint32_t element = table->types.base[vector] >> BUSTER_TYPE_INDEX_SHIFT;
        const bool is_vector = table->types.kind[vector] == BUSTER_TYPE_KIND_VECTOR || table->types.kind[vector] == BUSTER_TYPE_KIND_EXT_VECTOR;
        const bool int128_element = is_vector && table->types.kind[element] == BUSTER_TYPE_KIND_INTEGER && table->types.size[element] == 16;
        const uint64_t part = size <= abi->vector_bytes ? size : abi->vector_bytes;
        const uint64_t parts = (size + part - 1) / part;
        if (parts > BUSTER_ABI_RESULT_PARTS || (int128_element && size > BUSTER_ABI_REGISTER_BYTES))
        {
            // LLVM's hidden pointer, which clang, returning a value, doesn't count among the general registers.
            result->kind = BUSTER_ABI_RESULT_MEMORY;
            result->flags |= BUSTER_ABI_FLAG_DEMOTED;
        }
        else
        {
            for (uint64_t i = 0; i < parts; i += 1)
            {
                result->registers[i] = (uint8_t)(BUSTER_ABI_REGISTER_VECTOR | i);
            }
            result->part_bytes = (uint8_t)part;
        }
    }
}

// A type's result: clang's classifyReturnType, with LLVM's registers for what it returns as a value.
static AbiReturn abi_result(const AbiTable* abi, const TypeTable* table, uint32_t index, uint32_t pass)
{
    const AbiClasses classes = abi->rows[(uint64_t)index * BUSTER_ABI_ROW_BYTES];
    const uint8_t low = BUSTER_ABI_LOW(classes);
    const uint8_t high = BUSTER_ABI_HIGH(classes);
    const uint64_t size = table->types.size[index];
    AbiReturn result = {
        .kind = BUSTER_ABI_RESULT_IGNORE,
        .flags = (uint8_t)((pass & BUSTER_ABI_PASS_SIGN) != 0 ? BUSTER_ABI_FLAG_SIGN : (pass & BUSTER_ABI_PASS_ZERO) != 0 ? BUSTER_ABI_FLAG_ZERO : 0),
        .part_bytes = 0,
        .registers = {
            BUSTER_ABI_REGISTER_NONE,
            BUSTER_ABI_REGISTER_NONE,
            BUSTER_ABI_REGISTER_NONE,
            BUSTER_ABI_REGISTER_NONE,
        },
        .padding = 0,
    };
    if ((pass & BUSTER_ABI_PASS_IGNORE) != 0)
    {
    }
    else if (low == BUSTER_ABI_MEMORY && abi_is_aggregate(table, index))
    {
        result.kind = BUSTER_ABI_RESULT_MEMORY;
    }
    else if (low == BUSTER_ABI_MEMORY)
    {
        // A scalar clang returns as its value: nullptr_t, _Atomic scalars, vectors MEMORY by their size.
        abi_value_result(abi, table, index, abi_value_class(table, index), &result);
    }
    else if (low == BUSTER_ABI_X87)
    {
        result.kind = BUSTER_ABI_RESULT_REGISTERS;
        result.registers[0] = BUSTER_ABI_REGISTER_X87;
        result.part_bytes = 16;
    }
    else if (low == BUSTER_ABI_COMPLEX_X87)
    {
        result.kind = BUSTER_ABI_RESULT_REGISTERS;
        result.registers[0] = BUSTER_ABI_REGISTER_X87;
        result.registers[1] = BUSTER_ABI_REGISTER_X87 | 1;
        result.part_bytes = 16;
    }
    else if (high == BUSTER_ABI_SSEUP)
    {
        result.kind = BUSTER_ABI_RESULT_REGISTERS;
        result.registers[0] = BUSTER_ABI_REGISTER_VECTOR;
        result.part_bytes = (uint8_t)size;
    }
    else
    {
        uint32_t integers = 0;
        uint32_t vectors = 0;
        uint32_t part = 0;
        result.kind = BUSTER_ABI_RESULT_REGISTERS;
        result.part_bytes = 8;
        if (low == BUSTER_ABI_NO_CLASS)
        {
            result.flags |= BUSTER_ABI_FLAG_HIGH;
        }
        const uint8_t classes_in_order[2] = {
            (pass & BUSTER_ABI_PASS_LOW_GENERAL) != 0 ? BUSTER_ABI_INTEGER : low,
            (pass & BUSTER_ABI_PASS_PAIR) != 0 ? BUSTER_ABI_INTEGER : high,
        };
        for (uint32_t i = 0; i < 2; i += 1)
        {
            if (classes_in_order[i] == BUSTER_ABI_INTEGER)
            {
                result.registers[part] = abi_integer_results[integers];
                integers += 1;
                part += 1;
            }
            else if (classes_in_order[i] == BUSTER_ABI_SSE || classes_in_order[i] == BUSTER_ABI_X87UP)
            {
                result.registers[part] = (uint8_t)(BUSTER_ABI_REGISTER_VECTOR | vectors);
                vectors += 1;
                part += 1;
            }
        }
    }
    return result;
}

// Whether a type of no size is MEMORY at offset 16 of an enclosing aggregate (see abi_member_row_at): an array whose
// elements, as written, are aligned past 16 bytes, or which has elements that are such, or a record with a member whose
// canonical type is aligned past 16 bytes (its offset, 16, then isn't a multiple of it) or is such.
static bool abi_end_memory(const AbiTable* abi, const TypeTable* table, uint32_t index)
{
    const uint8_t kind = table->types.kind[index];
    bool result = false;
    if (kind == BUSTER_TYPE_KIND_ARRAY)
    {
        const uint32_t element = table->types.base[index] >> BUSTER_TYPE_INDEX_SHIFT;
        // The alignment is checked whatever the length; the elements' classes only if there are any.
        result = table->types.alignment_log2[element] > 4 || (table->types.extent[index] != 0 && (abi->pass[element] & BUSTER_ABI_PASS_END_MEMORY) != 0);
    }
    else if (kind == BUSTER_TYPE_KIND_STRUCT || kind == BUSTER_TYPE_KIND_UNION)
    {
        const uint32_t tag = table->types.index[index];
        const uint32_t begin = table->tags.member_begin[tag];
        const uint32_t end = begin + table->tags.member_count[tag];
        for (uint32_t member = begin; member < end; member += 1)
        {
            const bool unnamed_bit_field = (table->members.flags[member] & BUSTER_TYPE_MEMBER_BIT_FIELD) != 0 && table->members.name[member] == 0;
            const uint32_t type = table->members.type[member] >> BUSTER_TYPE_INDEX_SHIFT;
            result = result || (!unnamed_bit_field && (abi->alignment_log2[type] > 4 || (abi->pass[type] & BUSTER_ABI_PASS_END_MEMORY) != 0));
        }
    }
    return result;
}

// The pass bits and the rest of a type whose row is in place.
static void abi_finish(AbiTable* abi, const TypeTable* table, uint32_t index)
{
    const AbiClasses classes = abi->rows[(uint64_t)index * BUSTER_ABI_ROW_BYTES];
    const uint8_t low = BUSTER_ABI_LOW(classes);
    const uint8_t high = BUSTER_ABI_HIGH(classes);
    const uint8_t kind = table->types.kind[index];
    const uint64_t size = table->types.size[index];
    const bool aggregate = abi_is_aggregate(table, index);
    const bool illegal_vector = abi_is_illegal_vector(abi, table, index);
    uint32_t pass = BUSTER_ABI_PASS_CLASSIFIED;
    abi->alignment_log2[index] = abi_canonical_alignment_log2(abi, table, index);
    abi->storage[index] = abi_storage(abi, table, index);
    // Only a type of 16 bytes or less reads its layout, and its members' and elements' (a larger type is MEMORY or one
    // vector, and so is any type it is in; an array of no elements reads none).
    abi->layout[index] = size <= BUSTER_ABI_REGISTER_BYTES ? abi_layout(abi, table, index) : 0;
    if (low == BUSTER_ABI_NO_CLASS && high == BUSTER_ABI_NO_CLASS)
    {
        pass |= BUSTER_ABI_PASS_IGNORE;
    }
    else if (low == BUSTER_ABI_MEMORY || low == BUSTER_ABI_X87 || low == BUSTER_ABI_COMPLEX_X87)
    {
        pass |= BUSTER_ABI_PASS_MEMORY;
        // A scalar clang passes as its value, uncounted (long double's x87 value always goes to the stack).
        const bool scalar_value = (kind == BUSTER_TYPE_KIND_NULLPTR || kind == BUSTER_TYPE_KIND_ATOMIC) && !aggregate;
        if (scalar_value)
        {
            const uint32_t value = abi_value_class(table, index);
            pass |= value == BUSTER_ABI_VALUE_X87 ? 0 : BUSTER_ABI_PASS_UNCOUNTED;
            pass |= value << BUSTER_ABI_PASS_VALUE_SHIFT;
            // LLVM splits a vector wider than the target's registers across several: no class says how.
            pass |= value == BUSTER_ABI_VALUE_VECTOR && size > abi->vector_bytes && size > BUSTER_ABI_REGISTER_BYTES ? BUSTER_ABI_PASS_UNSUPPORTED : 0;
        }
    }
    else
    {
        pass |= low == BUSTER_ABI_INTEGER ? BUSTER_ABI_PASS_LOW_INTEGER : 0;
        pass |= low == BUSTER_ABI_SSE ? BUSTER_ABI_PASS_LOW_SSE : 0;
        pass |= high == BUSTER_ABI_INTEGER ? BUSTER_ABI_PASS_HIGH_INTEGER : 0;
        pass |= high == BUSTER_ABI_SSE || high == BUSTER_ABI_X87UP ? BUSTER_ABI_PASS_HIGH_SSE : 0;
        pass |= high == BUSTER_ABI_SSEUP ? BUSTER_ABI_PASS_VECTOR : 0;
        pass |= high == BUSTER_ABI_SSEUP && size > BUSTER_ABI_REGISTER_BYTES ? BUSTER_ABI_PASS_WIDE : 0;
        // An i128 at offset 0 makes clang's type for the low eightbyte the whole i128, a high one NO_CLASS too, which
        // clang then counts and passes as two INTEGER eightbytes.
        const bool pair_classes = low == BUSTER_ABI_INTEGER && (high == BUSTER_ABI_INTEGER || (high == BUSTER_ABI_NO_CLASS && size == 16));
        pass |= pair_classes && abi_is_pair(abi, table, index) ? BUSTER_ABI_PASS_PAIR | BUSTER_ABI_PASS_HIGH_INTEGER : 0;
        pass |= low == BUSTER_ABI_INTEGER && high == BUSTER_ABI_NO_CLASS ? abi_extension(table, index) : 0;
        pass |= abi_low_general(abi, table, index, low, high) ? BUSTER_ABI_PASS_LOW_GENERAL : 0;
    }
    pass |= aggregate || illegal_vector ? BUSTER_ABI_PASS_AGGREGATE : 0;
    // A type MEMORY at 0 is at 16 too (and a record with a flexible array member, whose members may not be classified
    // yet, is).
    pass |= size == 0 && classes != BUSTER_ABI_IN_MEMORY && abi_end_memory(abi, table, index) ? BUSTER_ABI_PASS_END_MEMORY : 0;
    pass |= size == 0 ? BUSTER_ABI_PASS_EMPTY : 0;
    // An aggregate's slot takes its own alignment; a scalar's, its value's natural one.
    const uint32_t alignment_log2 = aggregate || illegal_vector ? table->types.alignment_log2[index] : abi_value_alignment_log2(table, index);
    pass |= (alignment_log2 > BUSTER_ABI_SLOT_LOG2 ? alignment_log2 : BUSTER_ABI_SLOT_LOG2) << BUSTER_ABI_PASS_ALIGNMENT_SHIFT;
    abi->results[index] = abi_result(abi, table, index, pass);
    // A stack slot is a multiple of 8 bytes, and never empty: LLVM gives a byval argument of no size (a struct of a
    // flexible array member alone) 8 bytes (but see EMPTY).
    const uint64_t slot = size == 0 ? BUSTER_ABI_SLOT_BYTES : (size + BUSTER_ABI_SLOT_BYTES - 1) & ~(uint64_t)(BUSTER_ABI_SLOT_BYTES - 1);
    abi->stack_bytes[index] = slot > UINT32_MAX ? UINT32_MAX : (uint32_t)slot;
    abi->pass[index] = pass;
}

// An ALIGNED type passes as its base: clang's canonical types have no typedefs. Its row, pass bits and the rest are its
// base's.
static void abi_copy(AbiTable* abi, const TypeTable* table, uint32_t index)
{
    const uint32_t base = table->types.base[index] >> BUSTER_TYPE_INDEX_SHIFT;
    memcpy(abi->rows + (uint64_t)index * BUSTER_ABI_ROW_BYTES, abi->rows + (uint64_t)base * BUSTER_ABI_ROW_BYTES, BUSTER_ABI_ROW_BYTES);
    abi->pass[index] = abi->pass[base];
    abi->stack_bytes[index] = abi->stack_bytes[base];
    abi->alignment_log2[index] = abi->alignment_log2[base];
    abi->storage[index] = abi->storage[base];
    abi->layout[index] = abi->layout[base];
    abi->results[index] = abi->results[base];
}

// ---------------------------------------------------------------------------------------------------------------------
// The vector classification: records and arrays folded four at a time, one per 128-bit lane of a ZMM register (the
// lane's 16 bytes are the type's 16 offsets), a member of each per step.
//
// A pre-pass writes each member's entry (its classes at each offset of the type: its own type's row shifted by its
// offset, MEMORY where it would be misaligned; a bit-field's from where it starts and ends; abi_record_entry's and
// abi_array_entry's rules, 16 offsets at once) into blocks of 64 bytes, one per step of a group of four types, sorted
// by how many entries they fold so that a group's lanes end together; a lane that ends earlier merges NOTHING, which
// changes nothing. The kernel then merges each group's steps (two vpermb of the merge table each), post-merges every
// lane, and stores the four rows. Records and arrays whose rows need no fold (larger than 64 bytes, with a flexible
// array member, wider than the target's vectors) get theirs directly.

// A type the kernel folds: its index, how many entries it folds (members but unnamed bit-fields, or elements), the
// offsets it can't be at (a bit per offset: past offset 0 the enclosing aggregate would pass 16 bytes, and an array's
// misaligned ones), and whether it is larger than 16 bytes (post-merge then wants SSE, SSEUP at offset 0).
typedef struct AbiFold AbiFold;
struct AbiFold
{
    uint32_t index;
    uint32_t steps;
    uint16_t memory;
    bool big;
};

#define BUSTER_ABI_FOLD_LANES 4
#define BUSTER_ABI_FOLD_EMPTY UINT32_MAX
#define BUSTER_ABI_FOLD_BLOCK_BYTES 64
// The counting sort's buckets: types folding more entries than this share the last one.
#define BUSTER_ABI_FOLD_BUCKETS 64

// Four types folded together: their indices (EMPTY in an unused lane), their first block and how many steps they take,
// and their lanes' offsets forced to MEMORY and big types' offset 0, a bit per byte of the ZMM register.
typedef struct AbiFoldGroup AbiFoldGroup;
struct AbiFoldGroup
{
    uint32_t items[BUSTER_ABI_FOLD_LANES];
    uint64_t block;
    uint32_t steps;
    uint64_t memory;
    uint64_t big;
};

static __m128i abi_lane_offsets(void)
{
    return _mm_setr_epi8(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15);
}

// The offsets o (0 to 15) at which o + shift is a multiple of 2^alignment_log2, for shifts below 16: alignments of 32
// bytes and more leave only o + shift == 0.
static __mmask16 abi_aligned_lanes(uint64_t shift, uint32_t alignment_log2)
{
    const uint32_t mask = alignment_log2 >= 7 ? 127u : (1u << alignment_log2) - 1;
    return _mm_testn_epi8_mask(_mm_add_epi8(abi_lane_offsets(), _mm_set1_epi8((char)shift)), _mm_set1_epi8((char)mask));
}

// The offsets a type of `size` bytes can be at: 0, and those that leave the enclosing aggregate at most 16 bytes.
static __mmask16 abi_possible_lanes(uint64_t size)
{
    const uint64_t room = size < BUSTER_ABI_ROW_BYTES ? BUSTER_ABI_ROW_BYTES - size : 0;
    return (__mmask16)(_mm_cmple_epu8_mask(abi_lane_offsets(), _mm_set1_epi8((char)room)) | 1);
}

// A type's row as seen from `shift` bytes into an enclosing type: lane o holds the type's classes at offset o + shift,
// taken modulo 16. Only the lanes the enclosing type can be at count, where o + shift is below 16 for a member with a
// size; a member of no size can end the enclosing type, at 16, where it has its classes at 0 (its members' alignments,
// which decide them, divide 16 once the member's own does, which the enclosing type checks).
static __m128i abi_shifted_row(const AbiTable* abi, uint32_t index, uint64_t shift)
{
    const __m128i row = _mm_load_si128((const __m128i*)(abi->rows + (uint64_t)index * BUSTER_ABI_ROW_BYTES));
    const __m128i positions = _mm_and_si128(_mm_add_epi8(abi_lane_offsets(), _mm_set1_epi8((char)shift)), _mm_set1_epi8(BUSTER_ABI_ROW_BYTES - 1));
    return _mm_shuffle_epi8(row, positions);
}

// A record member's entry: abi_record_entry's rules for one member, at the 16 offsets.
static __m128i abi_member_entry(const AbiTable* abi, const TypeTable* table, uint64_t size, bool is_union, uint32_t member)
{
    const TypeId type = table->members.type[member];
    const uint32_t index = type >> BUSTER_TYPE_INDEX_SHIFT;
    const bool bit_field = (table->members.flags[member] & BUSTER_TYPE_MEMBER_BIT_FIELD) != 0;
    const uint64_t member_size = table->types.size[abi_resolved_index(table, type)];
    const uint64_t start = table->members.offset[member];
    __m128i result = _mm_set1_epi8((char)BUSTER_ABI_IN_MEMORY);
    if (size > BUSTER_ABI_REGISTER_BYTES && ((!is_union && size != member_size) || size > abi->vector_bytes))
    {
    }
    else if (bit_field)
    {
        // Offsets below low_end start the bit-field in the low eightbyte; from high_start on, it ends in the high one.
        const uint64_t last = start + table->members.bit_width[member] - 1;
        const uint64_t low_end = start >= 64 ? 0 : (64 - start + 7) / 8;
        const uint64_t high_start = last >= 64 ? 0 : (64 - last + 7) / 8;
        const __mmask16 low = _mm_cmplt_epu8_mask(abi_lane_offsets(), _mm_set1_epi8((char)low_end));
        const __mmask16 high = _mm_cmpge_epu8_mask(abi_lane_offsets(), _mm_set1_epi8((char)high_start));
        const __m128i low_classes = _mm_mask_blend_epi8(low, _mm_set1_epi8(BUSTER_ABI_NO_CLASS), _mm_set1_epi8(BUSTER_ABI_INTEGER));
        const __m128i high_classes = _mm_mask_blend_epi8(high, _mm_set1_epi8(BUSTER_ABI_NO_CLASS << 4), _mm_set1_epi8(BUSTER_ABI_INTEGER << 4));
        result = _mm_or_si128(low_classes, high_classes);
    }
    else
    {
        // At offset 16 - shift, a member of no size ends the enclosing aggregate: see abi_member_row_at.
        const uint64_t shift = start / 8;
        const bool end_misaligned = member_size == 0 && shift != 0 && (abi->pass[index] & BUSTER_ABI_PASS_END_MEMORY) != 0;
        const __mmask16 misaligned_end = end_misaligned ? (__mmask16)(1u << (BUSTER_ABI_ROW_BYTES - shift)) : 0;
        result = _mm_mask_blend_epi8(abi_aligned_lanes(shift, abi->alignment_log2[index]) & (__mmask16)~misaligned_end, result, abi_shifted_row(abi, index, shift));
    }
    return result;
}

// Writes a type's entries into its lane (16 bytes of each step's block, `lane` the first), and NOTHING after them up to
// `steps`.
static void abi_write_entries(const AbiTable* abi, const TypeTable* table, uint32_t index, uint8_t* lane, uint32_t steps)
{
    const uint8_t kind = table->types.kind[index];
    uint32_t step = 0;
    if (kind == BUSTER_TYPE_KIND_ARRAY)
    {
        const uint32_t element = table->types.base[index] >> BUSTER_TYPE_INDEX_SHIFT;
        const uint64_t element_size = table->types.size[abi_resolved_index(table, table->types.base[index])];
        const uint64_t count = table->types.extent[index];
        for (uint64_t i = 0; i < count; i += 1)
        {
            _mm_store_si128((__m128i*)(lane + (uint64_t)step * BUSTER_ABI_FOLD_BLOCK_BYTES), abi_shifted_row(abi, element, i * element_size));
            step += 1;
        }
    }
    else
    {
        const uint64_t size = table->types.size[index];
        const bool is_union = kind == BUSTER_TYPE_KIND_UNION;
        const uint32_t tag = table->types.index[index];
        const uint32_t begin = table->tags.member_begin[tag];
        const uint32_t end = begin + table->tags.member_count[tag];
        for (uint32_t member = begin; member < end; member += 1)
        {
            const bool unnamed_bit_field = (table->members.flags[member] & BUSTER_TYPE_MEMBER_BIT_FIELD) != 0 && table->members.name[member] == 0;
            if (!unnamed_bit_field)
            {
                _mm_store_si128((__m128i*)(lane + (uint64_t)step * BUSTER_ABI_FOLD_BLOCK_BYTES), abi_member_entry(abi, table, size, is_union, member));
                step += 1;
            }
        }
    }
    for (; step < steps; step += 1)
    {
        _mm_store_si128((__m128i*)(lane + (uint64_t)step * BUSTER_ABI_FOLD_BLOCK_BYTES), _mm_set1_epi8((char)BUSTER_ABI_NOTHING));
    }
}

// clang's merge in every byte: the merge table indexed by the accumulated class in bits 3 to 5 and the field's in bits 0
// to 2 (vpermb reads an index's low 6 bits only, so what lies above them doesn't matter), for each half.
static __m512i abi_merge_vector(__m512i merge, __m512i accumulated, __m512i field)
{
    const __m512i accumulated_bits = _mm512_set1_epi8(0x38);
    // 0xe4: the third operand's bits choose the first's, the rest the second's.
    const __m512i low_index = _mm512_ternarylogic_epi32(_mm512_slli_epi16(accumulated, 3), field, accumulated_bits, 0xe4);
    const __m512i high_index = _mm512_ternarylogic_epi32(_mm512_srli_epi16(accumulated, 1), _mm512_srli_epi16(field, 4), accumulated_bits, 0xe4);
    const __m512i low = _mm512_permutexvar_epi8(low_index, merge);
    const __m512i high = _mm512_permutexvar_epi8(high_index, merge);
    return _mm512_or_si512(low, _mm512_slli_epi16(high, 4));
}

// abi_post_merge in every byte, `big` (bytes of offset 0 of types larger than 16 bytes) wanting SSE, SSEUP, and
// `memory` forcing MEMORY.
static __m512i abi_post_merge_vector(__m512i classes, __mmask64 big, __mmask64 memory)
{
    const __m512i low = _mm512_and_si512(classes, _mm512_set1_epi8(7));
    const __m512i high = _mm512_and_si512(_mm512_srli_epi16(classes, 4), _mm512_set1_epi8(7));
    const __mmask64 low_sse = _mm512_cmpeq_epi8_mask(low, _mm512_set1_epi8(BUSTER_ABI_SSE));
    const __mmask64 high_sseup = _mm512_cmpeq_epi8_mask(high, _mm512_set1_epi8(BUSTER_ABI_SSEUP));
    const __mmask64 lone_x87up = _mm512_cmpeq_epi8_mask(high, _mm512_set1_epi8(BUSTER_ABI_X87UP)) & ~_mm512_cmpeq_epi8_mask(low, _mm512_set1_epi8(BUSTER_ABI_X87));
    const __mmask64 in_memory = _mm512_cmpeq_epi8_mask(low, _mm512_set1_epi8(BUSTER_ABI_MEMORY)) | _mm512_cmpeq_epi8_mask(high, _mm512_set1_epi8(BUSTER_ABI_MEMORY)) |
                                lone_x87up | (big & ~(low_sse & high_sseup)) | memory;
    const __m512i lone_sseup = _mm512_mask_mov_epi8(classes, high_sseup & ~low_sse, _mm512_or_si512(low, _mm512_set1_epi8(BUSTER_ABI_SSE << 4)));
    return _mm512_mask_mov_epi8(lone_sseup, in_memory, _mm512_set1_epi8((char)BUSTER_ABI_IN_MEMORY));
}

// Folds each group's blocks and stores its rows.
static void abi_fold_groups(AbiTable* abi, const uint8_t* blocks, const AbiFoldGroup* groups, uint32_t group_count)
{
    const __m512i merge = _mm512_load_si512((const void*)abi->merge);
    for (uint32_t g = 0; g < group_count; g += 1)
    {
        const AbiFoldGroup group = groups[g];
        const uint8_t* block = blocks + group.block * BUSTER_ABI_FOLD_BLOCK_BYTES;
        __m512i accumulated = _mm512_set1_epi8((char)BUSTER_ABI_NOTHING);
        for (uint32_t step = 0; step < group.steps; step += 1)
        {
            accumulated = abi_merge_vector(merge, accumulated, _mm512_load_si512((const void*)(block + (uint64_t)step * BUSTER_ABI_FOLD_BLOCK_BYTES)));
        }
        _Alignas(64) uint8_t rows[BUSTER_ABI_FOLD_LANES * BUSTER_ABI_ROW_BYTES];
        _mm512_store_si512((void*)rows, abi_post_merge_vector(accumulated, group.big, group.memory));
        for (uint32_t lane = 0; lane < BUSTER_ABI_FOLD_LANES; lane += 1)
        {
            if (group.items[lane] != BUSTER_ABI_FOLD_EMPTY)
            {
                memcpy(abi->rows + (uint64_t)group.items[lane] * BUSTER_ABI_ROW_BYTES, rows + lane * BUSTER_ABI_ROW_BYTES, BUSTER_ABI_ROW_BYTES);
            }
        }
    }
}

static void abi_store_row(AbiTable* abi, uint32_t index, __m128i row)
{
    _mm_store_si128((__m128i*)(abi->rows + (uint64_t)index * BUSTER_ABI_ROW_BYTES), row);
}

// The vector classification of a level (see abi_classify). Returns false, having classified only some of the types, if
// the scratch memory can't hold the fold; the caller then classifies them all with the twin.
static bool abi_classify_vector(AbiTable* abi, const TypeTable* table, const TypeId* types, uint32_t count)
{
    Scratch scratch = scratch_begin(0);
    AbiFold* folds = BUSTER_ARENA_PUSH_ARRAY(scratch.arena, AbiFold, count);
    uint32_t fold_count = 0;
    bool result = folds != 0;
    for (uint32_t i = 0; result && i < count; i += 1)
    {
        const uint32_t index = types[i] >> BUSTER_TYPE_INDEX_SHIFT;
        const uint8_t kind = table->types.kind[index];
        const uint64_t size = table->types.size[index];
        if (kind == BUSTER_TYPE_KIND_ALIGNED)
        {
            abi_copy(abi, table, index);
        }
        else if (kind == BUSTER_TYPE_KIND_STRUCT || kind == BUSTER_TYPE_KIND_UNION)
        {
            const bool flexible = (table->types.flags[index] & BUSTER_TYPE_FLAG_FLEXIBLE) != 0;
            if (size > BUSTER_ABI_MAX_CLASSIFIED_BYTES || flexible || (size > BUSTER_ABI_REGISTER_BYTES && size > abi->vector_bytes))
            {
                abi_store_row(abi, index, _mm_set1_epi8((char)BUSTER_ABI_IN_MEMORY));
                abi_finish(abi, table, index);
            }
            else
            {
                const uint32_t tag = table->types.index[index];
                const uint32_t begin = table->tags.member_begin[tag];
                const uint32_t end = begin + table->tags.member_count[tag];
                uint32_t steps = 0;
                for (uint32_t member = begin; member < end; member += 1)
                {
                    steps += (table->members.flags[member] & BUSTER_TYPE_MEMBER_BIT_FIELD) != 0 && table->members.name[member] == 0 ? 0 : 1;
                }
                folds[fold_count] = (AbiFold){
                    .index = index,
                    .steps = steps,
                    .memory = (uint16_t)~abi_possible_lanes(size),
                    .big = size > BUSTER_ABI_REGISTER_BYTES,
                };
                fold_count += 1;
            }
        }
        else if (kind == BUSTER_TYPE_KIND_ARRAY)
        {
            const TypeId element = table->types.base[index];
            const uint64_t element_size = table->types.size[abi_resolved_index(table, element)];
            const __mmask16 aligned = abi_aligned_lanes(0, abi_alignment_bytes_log2(table, element));
            if (size > BUSTER_ABI_MAX_CLASSIFIED_BYTES)
            {
                abi_store_row(abi, index, _mm_set1_epi8((char)BUSTER_ABI_IN_MEMORY));
                abi_finish(abi, table, index);
            }
            else if (size > BUSTER_ABI_REGISTER_BYTES && (size != element_size || size > abi->vector_bytes))
            {
                abi_store_row(abi, index, _mm_mask_mov_epi8(_mm_set1_epi8((char)BUSTER_ABI_IN_MEMORY), 1, _mm_set1_epi8((char)BUSTER_ABI_NOTHING)));
                abi_finish(abi, table, index);
            }
            else if (element_size == 0 && table->types.extent[index] != 0)
            {
                // Elements of no size, all at the array's offset: merging them gives one's classes (an empty record's
                // NOTHING, or MEMORY for one with a flexible array member), already post-merged.
                const __m128i element_row = _mm_load_si128((const __m128i*)(abi->rows + (uint64_t)(element >> BUSTER_TYPE_INDEX_SHIFT) * BUSTER_ABI_ROW_BYTES));
                abi_store_row(abi, index, _mm_mask_mov_epi8(_mm_set1_epi8((char)BUSTER_ABI_IN_MEMORY), aligned, element_row));
                abi_finish(abi, table, index);
            }
            else
            {
                folds[fold_count] = (AbiFold){
                    .index = index,
                    .steps = (uint32_t)table->types.extent[index],
                    .memory = (uint16_t)~(abi_possible_lanes(size) & aligned),
                    .big = size > BUSTER_ABI_REGISTER_BYTES,
                };
                fold_count += 1;
            }
        }
        else
        {
            abi_store_row(abi, index, abi_simple_row(abi_simple(abi, table, index)));
            abi_finish(abi, table, index);
        }
    }
    // The folds sorted by their steps (a counting sort), grouped four at a time.
    const uint32_t group_count = (fold_count + BUSTER_ABI_FOLD_LANES - 1) / BUSTER_ABI_FOLD_LANES;
    AbiFold* sorted = result ? BUSTER_ARENA_PUSH_ARRAY(scratch.arena, AbiFold, fold_count) : 0;
    AbiFoldGroup* groups = result ? BUSTER_ARENA_PUSH_ARRAY(scratch.arena, AbiFoldGroup, group_count) : 0;
    result = result && (fold_count == 0 || (sorted != 0 && groups != 0));
    if (result && fold_count != 0)
    {
        uint32_t starts[BUSTER_ABI_FOLD_BUCKETS + 1] = { 0 };
        for (uint32_t f = 0; f < fold_count; f += 1)
        {
            const uint32_t bucket = folds[f].steps < BUSTER_ABI_FOLD_BUCKETS ? folds[f].steps : BUSTER_ABI_FOLD_BUCKETS - 1;
            starts[bucket + 1] += 1;
        }
        for (uint32_t b = 0; b < BUSTER_ABI_FOLD_BUCKETS; b += 1)
        {
            starts[b + 1] += starts[b];
        }
        for (uint32_t f = 0; f < fold_count; f += 1)
        {
            const uint32_t bucket = folds[f].steps < BUSTER_ABI_FOLD_BUCKETS ? folds[f].steps : BUSTER_ABI_FOLD_BUCKETS - 1;
            sorted[starts[bucket]] = folds[f];
            starts[bucket] += 1;
        }
        uint64_t blocks_total = 0;
        for (uint32_t g = 0; g < group_count; g += 1)
        {
            AbiFoldGroup group = {
                .items = {
                    BUSTER_ABI_FOLD_EMPTY,
                    BUSTER_ABI_FOLD_EMPTY,
                    BUSTER_ABI_FOLD_EMPTY,
                    BUSTER_ABI_FOLD_EMPTY,
                },
                .block = blocks_total,
                .steps = 0,
                .memory = 0,
                .big = 0,
            };
            for (uint32_t lane = 0; lane < BUSTER_ABI_FOLD_LANES && g * BUSTER_ABI_FOLD_LANES + lane < fold_count; lane += 1)
            {
                const AbiFold fold = sorted[g * BUSTER_ABI_FOLD_LANES + lane];
                group.items[lane] = fold.index;
                group.steps = fold.steps > group.steps ? fold.steps : group.steps;
                group.memory |= (uint64_t)fold.memory << (lane * BUSTER_ABI_ROW_BYTES);
                group.big |= (uint64_t)fold.big << (lane * BUSTER_ABI_ROW_BYTES);
            }
            groups[g] = group;
            blocks_total += group.steps;
        }
        uint8_t* blocks = arena_push(scratch.arena, blocks_total * BUSTER_ABI_FOLD_BLOCK_BYTES + BUSTER_ABI_FOLD_BLOCK_BYTES, BUSTER_ABI_FOLD_BLOCK_BYTES);
        result = blocks != 0;
        for (uint32_t g = 0; result && g < group_count; g += 1)
        {
            for (uint32_t lane = 0; lane < BUSTER_ABI_FOLD_LANES; lane += 1)
            {
                if (groups[g].items[lane] != BUSTER_ABI_FOLD_EMPTY)
                {
                    uint8_t* destination = blocks + groups[g].block * BUSTER_ABI_FOLD_BLOCK_BYTES + lane * BUSTER_ABI_ROW_BYTES;
                    abi_write_entries(abi, table, groups[g].items[lane], destination, groups[g].steps);
                }
                else
                {
                    for (uint32_t step = 0; step < groups[g].steps; step += 1)
                    {
                        uint8_t* destination = blocks + (groups[g].block + step) * BUSTER_ABI_FOLD_BLOCK_BYTES + lane * BUSTER_ABI_ROW_BYTES;
                        _mm_store_si128((__m128i*)destination, _mm_set1_epi8((char)BUSTER_ABI_NOTHING));
                    }
                }
            }
        }
        if (result)
        {
            abi_fold_groups(abi, blocks, groups, group_count);
            for (uint32_t f = 0; f < fold_count; f += 1)
            {
                abi_finish(abi, table, sorted[f].index);
            }
        }
    }
    scratch_end(scratch);
    return result;
}

// ---------------------------------------------------------------------------------------------------------------------
// The drivers

static uint64_t abi_capacity_bytes(uint32_t types)
{
    const uint64_t count = types;
    return count * BUSTER_ABI_ROW_BYTES + count * sizeof(uint32_t) * 2 + count * 2 + count * sizeof(uint64_t) + count * sizeof(AbiReturn) + 7 * 64;
}

static bool abi_initialize(AbiTable* abi, Arena* arena, const TypeTable* table, uint32_t vector_bytes)
{
    const uint64_t length = arena->length;
    const uint32_t capacity = table->types.capacity;
    abi->rows = arena_push(arena, (uint64_t)capacity * BUSTER_ABI_ROW_BYTES, 64);
    abi->pass = abi->rows != 0 ? arena_push(arena, (uint64_t)capacity * sizeof(uint32_t), 64) : 0;
    abi->stack_bytes = abi->pass != 0 ? arena_push(arena, (uint64_t)capacity * sizeof(uint32_t), 64) : 0;
    abi->alignment_log2 = abi->stack_bytes != 0 ? arena_push(arena, capacity, 64) : 0;
    abi->storage = abi->alignment_log2 != 0 ? arena_push(arena, capacity, 64) : 0;
    abi->layout = abi->storage != 0 ? arena_push(arena, (uint64_t)capacity * sizeof(uint64_t), 64) : 0;
    abi->results = abi->layout != 0 ? arena_push(arena, (uint64_t)capacity * sizeof(AbiReturn), 64) : 0;
    abi->capacity = capacity;
    abi->vector_bytes = vector_bytes;
    const bool result = abi->results != 0 && (vector_bytes == 16 || vector_bytes == 32 || vector_bytes == 64);
    if (result)
    {
        memset(abi->pass, 0, (uint64_t)capacity * sizeof(uint32_t));
        memset(abi->storage, 0, capacity);
        memset(abi->layout, 0, (uint64_t)capacity * sizeof(uint64_t));
        for (uint32_t accumulated = 0; accumulated < BUSTER_ABI_CLASS_COUNT; accumulated += 1)
        {
            for (uint32_t field = 0; field < BUSTER_ABI_CLASS_COUNT; field += 1)
            {
                abi->merge[accumulated << 3 | field] = abi_merge_class((uint8_t)accumulated, (uint8_t)field);
            }
        }
        for (uint32_t index = 1; index < BUSTER_TYPE_BUILTIN_COUNT && index < table->types.count; index += 1)
        {
            if (table->types.kind[index] != BUSTER_TYPE_KIND_NONE)
            {
                abi_store_row(abi, index, abi_simple_row(abi_simple(abi, table, index)));
                abi_finish(abi, table, index);
            }
        }
    }
    else
    {
        arena_pop_to(arena, length);
    }
    return result;
}

static void abi_classify_scalar(AbiTable* abi, const TypeTable* table, const TypeId* types, uint32_t count)
{
    for (uint32_t i = 0; i < count; i += 1)
    {
        const uint32_t index = types[i] >> BUSTER_TYPE_INDEX_SHIFT;
        if (table->types.kind[index] == BUSTER_TYPE_KIND_ALIGNED)
        {
            abi_copy(abi, table, index);
        }
        else
        {
            for (uint32_t offset = 0; offset < BUSTER_ABI_ROW_BYTES; offset += 1)
            {
                abi->rows[(uint64_t)index * BUSTER_ABI_ROW_BYTES + offset] = abi_row_entry(abi, table, index, offset);
            }
            abi_finish(abi, table, index);
        }
    }
}

static void abi_classify(AbiTable* abi, const TypeTable* table, const TypeId* types, uint32_t count)
{
    if (!abi_classify_vector(abi, table, types, count))
    {
        abi_classify_scalar(abi, table, types, count);
    }
}

// Whether a type can be classified: a complete object type, or void.
static bool abi_classifiable(const TypeTable* table, uint32_t index)
{
    const uint32_t resolved = abi_resolved_index(table, (TypeId)index << BUSTER_TYPE_INDEX_SHIFT);
    const uint8_t kind = table->types.kind[resolved];
    const bool object = kind != BUSTER_TYPE_KIND_NONE && kind != BUSTER_TYPE_KIND_FUNCTION && kind != BUSTER_TYPE_KIND_INCOMPLETE_ARRAY &&
                        kind != BUSTER_TYPE_KIND_VARIABLE_ARRAY;
    return object && (kind == BUSTER_TYPE_KIND_VOID || (table->types.flags[resolved] & BUSTER_TYPE_FLAG_COMPLETE) != 0);
}

static bool abi_is_classified(const AbiTable* abi, TypeId type)
{
    return (abi->pass[type >> BUSTER_TYPE_INDEX_SHIFT] & BUSTER_ABI_PASS_CLASSIFIED) != 0;
}

// Whether what a type's row is made from is classified: an ALIGNED type's base, an array's element, a record's members
// (but for the records whose rows don't depend on them).
static bool abi_dependencies_classified(const AbiTable* abi, const TypeTable* table, uint32_t index)
{
    const uint8_t kind = table->types.kind[index];
    bool result = true;
    if (kind == BUSTER_TYPE_KIND_ALIGNED || kind == BUSTER_TYPE_KIND_ARRAY)
    {
        result = abi_is_classified(abi, table->types.base[index]);
    }
    else if ((kind == BUSTER_TYPE_KIND_STRUCT || kind == BUSTER_TYPE_KIND_UNION) && table->types.size[index] <= BUSTER_ABI_MAX_CLASSIFIED_BYTES &&
             (table->types.flags[index] & BUSTER_TYPE_FLAG_FLEXIBLE) == 0)
    {
        const uint32_t tag = table->types.index[index];
        const uint32_t begin = table->tags.member_begin[tag];
        const uint32_t end = begin + table->tags.member_count[tag];
        for (uint32_t member = begin; result && member < end; member += 1)
        {
            result = abi_is_classified(abi, table->members.type[member]);
        }
    }
    return result;
}

// Classifies, a level at a time, every type that is classifiable and not yet classified. Without scratch memory for the
// levels, a type at a time in passes over the table, by the twin, until a pass classifies nothing.
static uint32_t abi_classify_levels(AbiTable* abi, const TypeTable* table, bool scalar)
{
    Scratch scratch = scratch_begin(0);
    const uint32_t count = table->types.count;
    uint32_t* pending = BUSTER_ARENA_PUSH_ARRAY(scratch.arena, uint32_t, count);
    TypeId* ready = pending != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch.arena, TypeId, count) : 0;
    uint32_t classified = 0;
    if (ready != 0)
    {
        uint32_t pending_count = 0;
        for (uint32_t index = 1; index < count; index += 1)
        {
            if ((abi->pass[index] & BUSTER_ABI_PASS_CLASSIFIED) == 0 && abi_classifiable(table, index))
            {
                pending[pending_count] = index;
                pending_count += 1;
            }
        }
        bool progress = true;
        while (pending_count != 0 && progress)
        {
            uint32_t ready_count = 0;
            uint32_t kept = 0;
            for (uint32_t i = 0; i < pending_count; i += 1)
            {
                if (abi_dependencies_classified(abi, table, pending[i]))
                {
                    ready[ready_count] = (TypeId)pending[i] << BUSTER_TYPE_INDEX_SHIFT;
                    ready_count += 1;
                }
                else
                {
                    pending[kept] = pending[i];
                    kept += 1;
                }
            }
            pending_count = kept;
            progress = ready_count != 0;
            if (scalar)
            {
                abi_classify_scalar(abi, table, ready, ready_count);
            }
            else
            {
                abi_classify(abi, table, ready, ready_count);
            }
            classified += ready_count;
        }
    }
    else
    {
        bool progress = true;
        while (progress)
        {
            progress = false;
            for (uint32_t index = 1; index < count; index += 1)
            {
                const TypeId type = (TypeId)index << BUSTER_TYPE_INDEX_SHIFT;
                if ((abi->pass[index] & BUSTER_ABI_PASS_CLASSIFIED) == 0 && abi_classifiable(table, index) && abi_dependencies_classified(abi, table, index))
                {
                    abi_classify_scalar(abi, table, &type, 1);
                    classified += 1;
                    progress = true;
                }
            }
        }
    }
    scratch_end(scratch);
    return classified;
}

static uint32_t abi_classify_all(AbiTable* abi, const TypeTable* table)
{
    return abi_classify_levels(abi, table, false);
}

static uint32_t abi_classify_all_scalar(AbiTable* abi, const TypeTable* table)
{
    return abi_classify_levels(abi, table, true);
}

static AbiClasses abi_classes(const AbiTable* abi, TypeId type, bool named)
{
    const uint32_t index = type >> BUSTER_TYPE_INDEX_SHIFT;
    AbiClasses result = abi->rows[(uint64_t)index * BUSTER_ABI_ROW_BYTES];
    if (!named && (abi->pass[index] & BUSTER_ABI_PASS_WIDE) != 0)
    {
        result = BUSTER_ABI_IN_MEMORY;
    }
    return result;
}

// ---------------------------------------------------------------------------------------------------------------------
// Assignment

// The argument area's next slot of `bytes` bytes, aligned to 2^alignment_log2: its offset; the area grows past it, and
// its alignment to the slot's.
static uint32_t abi_stack_place(uint64_t* stack, uint8_t* area_alignment_log2, uint64_t bytes, uint32_t alignment_log2)
{
    const uint64_t alignment = 1ull << alignment_log2;
    const uint64_t offset = (*stack + alignment - 1) & ~(alignment - 1);
    *stack = offset + bytes;
    *area_alignment_log2 = alignment_log2 > *area_alignment_log2 ? (uint8_t)alignment_log2 : *area_alignment_log2;
    return (uint32_t)offset;
}

// One call, as clang and LLVM assign it (the vector path's twin): clang decides from its count of free registers
// whether an argument's eightbytes go to registers; LLVM then gives each value it gets the next register of its kind
// or, once there is none, a stack slot of its own. The two counts differ where clang counts otherwise than LLVM
// assigns: LLVM uses registers clang doesn't count for an UNCOUNTED argument, a DEMOTED result's hidden pointer and a
// LOW_GENERAL low eightbyte (counted as SSE), so that an eightbyte clang meant for a register can land on the stack
// (SPLIT); and clang counts registers LLVM doesn't use for a named WIDE vector of a variadic call and a PAIR on the
// stack, which LLVM then gives to the values clang sends to memory after them.
static AbiCall abi_assign_call(const AbiTable* abi, const TypeId* arguments, uint32_t count, uint32_t named, bool variadic, TypeId result, AbiLocation* locations)
{
    AbiCall call = {
        .stack_bytes = 0,
        .stack_alignment_log2 = BUSTER_ABI_STACK_ALIGNMENT_LOG2,
        .integer_registers = 0,
        .vector_registers = 0,
        .padding = 0,
        .result = abi->results[result >> BUSTER_TYPE_INDEX_SHIFT],
    };
    // The free registers as clang counts them, and the used ones as LLVM assigns them.
    uint32_t clang_integers = BUSTER_ABI_INTEGER_ARGUMENTS;
    uint32_t clang_vectors = BUSTER_ABI_VECTOR_ARGUMENTS;
    uint32_t integers = 0;
    uint32_t vectors = 0;
    uint64_t stack = 0;
    if (call.result.kind == BUSTER_ABI_RESULT_MEMORY)
    {
        clang_integers -= (call.result.flags & BUSTER_ABI_FLAG_DEMOTED) != 0 ? 0u : 1u;
        integers = 1;
    }
    for (uint32_t i = 0; i < count; i += 1)
    {
        const uint32_t index = arguments[i] >> BUSTER_TYPE_INDEX_SHIFT;
        const uint32_t pass = abi->pass[index];
        const uint32_t bytes = abi->stack_bytes[index];
        const uint32_t alignment_log2 = (pass & BUSTER_ABI_PASS_ALIGNMENT) >> BUSTER_ABI_PASS_ALIGNMENT_SHIFT;
        const bool unnamed = i >= named;
        const bool in_memory = (pass & BUSTER_ABI_PASS_MEMORY) != 0 || ((pass & BUSTER_ABI_PASS_WIDE) != 0 && unnamed);
        const uint32_t need_integers = ((pass & BUSTER_ABI_PASS_LOW_INTEGER) != 0 ? 1u : 0u) + ((pass & BUSTER_ABI_PASS_HIGH_INTEGER) != 0 ? 1u : 0u);
        const uint32_t need_vectors = ((pass & BUSTER_ABI_PASS_LOW_SSE) != 0 ? 1u : 0u) + ((pass & BUSTER_ABI_PASS_HIGH_SSE) != 0 ? 1u : 0u);
        AbiLocation location = {
            .stack_offset = 0,
            .registers = {
                BUSTER_ABI_REGISTER_NONE,
                BUSTER_ABI_REGISTER_NONE,
            },
            .kind = BUSTER_ABI_LOCATION_IGNORE,
            .flags = (uint8_t)((pass >> 10) & (BUSTER_ABI_FLAG_SIGN | BUSTER_ABI_FLAG_ZERO)),
        };
        if ((pass & BUSTER_ABI_PASS_IGNORE) != 0)
        {
        }
        else if (in_memory && (pass & BUSTER_ABI_PASS_UNCOUNTED) != 0)
        {
            const uint32_t value = (pass & BUSTER_ABI_PASS_VALUE) >> BUSTER_ABI_PASS_VALUE_SHIFT;
            location.kind = BUSTER_ABI_LOCATION_REGISTERS;
            if (value == BUSTER_ABI_VALUE_GENERAL && integers < BUSTER_ABI_INTEGER_ARGUMENTS)
            {
                location.registers[0] = abi_integer_arguments[integers];
                integers += 1;
            }
            else if (value == BUSTER_ABI_VALUE_PAIR && integers + 2 <= BUSTER_ABI_INTEGER_ARGUMENTS)
            {
                location.registers[0] = abi_integer_arguments[integers];
                location.registers[1] = abi_integer_arguments[integers + 1];
                integers += 2;
            }
            else if (value == BUSTER_ABI_VALUE_VECTOR && vectors < BUSTER_ABI_VECTOR_ARGUMENTS && (!variadic || bytes <= BUSTER_ABI_REGISTER_BYTES))
            {
                location.registers[0] = (uint8_t)(BUSTER_ABI_REGISTER_VECTOR | vectors);
                vectors += 1;
            }
            else if (value == BUSTER_ABI_VALUE_HALVES)
            {
                uint32_t stacked = 0;
                for (uint32_t half = 0; half < 2; half += 1)
                {
                    const bool in_register = integers < BUSTER_ABI_INTEGER_ARGUMENTS;
                    const uint32_t slot = in_register ? 0 : abi_stack_place(&stack, &call.stack_alignment_log2, BUSTER_ABI_SLOT_BYTES, BUSTER_ABI_SLOT_LOG2);
                    location.registers[half] = in_register ? abi_integer_arguments[integers] : location.registers[half];
                    location.stack_offset = !in_register && stacked == 0 ? slot : location.stack_offset;
                    integers += in_register ? 1 : 0;
                    stacked += in_register ? 0 : 1;
                }
                location.kind = stacked == 0 ? BUSTER_ABI_LOCATION_REGISTERS : stacked == 2 ? BUSTER_ABI_LOCATION_STACK : BUSTER_ABI_LOCATION_SPLIT;
            }
            else
            {
                location.kind = BUSTER_ABI_LOCATION_STACK;
                location.stack_offset = abi_stack_place(&stack, &call.stack_alignment_log2, bytes, alignment_log2);
            }
        }
        else if (!in_memory && (pass & BUSTER_ABI_PASS_AGGREGATE) == 0 && need_integers == 0 && clang_vectors < need_vectors && vectors < BUSTER_ABI_VECTOR_ARGUMENTS &&
                 !((pass & BUSTER_ABI_PASS_WIDE) != 0 && variadic))
        {
            // A scalar or a vector clang finds no vector register for, after named WIDE vectors of a variadic call that
            // clang counted and LLVM put on the stack: clang leaves it to LLVM as a value (getIndirectResult), which
            // gives it the register it has.
            location.kind = BUSTER_ABI_LOCATION_REGISTERS;
            location.registers[0] = (uint8_t)(BUSTER_ABI_REGISTER_VECTOR | vectors);
            vectors += 1;
        }
        else if (!in_memory && (pass & (BUSTER_ABI_PASS_AGGREGATE | BUSTER_ABI_PASS_PAIR)) == 0 && need_vectors == 0 && need_integers == 1 &&
                 clang_integers == 0 && integers < BUSTER_ABI_INTEGER_ARGUMENTS)
        {
            // The same for a general register, after a PAIR that clang counted two for and LLVM put on the stack.
            location.kind = BUSTER_ABI_LOCATION_REGISTERS;
            location.registers[0] = abi_integer_arguments[integers];
            integers += 1;
        }
        else if (in_memory || clang_integers < need_integers || clang_vectors < need_vectors)
        {
            // With no general register free, clang passes an aggregate of 8 bytes or less, aligned to 8 or less, as an
            // integer of its size (getIndirectResult), MEMORY or not: LLVM gives it a general register if it has one
            // left (after a count of clang's that LLVM's runs behind), else 8 bytes of the stack, as a copy would take;
            // one of no size is an i0, which takes none (EMPTY).
            const bool integer = (pass & BUSTER_ABI_PASS_AGGREGATE) != 0 && bytes == BUSTER_ABI_SLOT_BYTES && alignment_log2 == BUSTER_ABI_SLOT_LOG2 && clang_integers == 0;
            const bool vanishes = integer && (pass & BUSTER_ABI_PASS_EMPTY) != 0;
            const bool in_register = integer && !vanishes && integers < BUSTER_ABI_INTEGER_ARGUMENTS;
            location.kind = vanishes ? BUSTER_ABI_LOCATION_IGNORE : in_register ? BUSTER_ABI_LOCATION_REGISTERS : BUSTER_ABI_LOCATION_STACK;
            location.registers[0] = in_register ? abi_integer_arguments[integers] : BUSTER_ABI_REGISTER_NONE;
            integers += in_register ? 1 : 0;
            location.stack_offset = vanishes || in_register ? 0 : abi_stack_place(&stack, &call.stack_alignment_log2, bytes, alignment_log2);
        }
        else if ((pass & (BUSTER_ABI_PASS_VECTOR | BUSTER_ABI_PASS_PAIR)) != 0)
        {
            // One value: a vector register, or two general ones; LLVM puts it on the stack whole if it hasn't them, and a
            // vector wider than 16 bytes in a variadic call always (its slot aligned to its size), clang having counted a
            // register for it all the same (LLVM's FIXME on CCIfNotVarArg: the psABI, and GCC, pass a named one in a
            // register).
            const bool wide_on_stack = (pass & BUSTER_ABI_PASS_WIDE) != 0 && variadic;
            clang_integers -= need_integers;
            clang_vectors -= need_vectors;
            location.kind = BUSTER_ABI_LOCATION_REGISTERS;
            if ((pass & BUSTER_ABI_PASS_VECTOR) != 0 && !wide_on_stack && vectors < BUSTER_ABI_VECTOR_ARGUMENTS)
            {
                location.registers[0] = (uint8_t)(BUSTER_ABI_REGISTER_VECTOR | vectors);
                vectors += 1;
            }
            else if ((pass & BUSTER_ABI_PASS_PAIR) != 0 && integers + 2 <= BUSTER_ABI_INTEGER_ARGUMENTS)
            {
                location.registers[0] = abi_integer_arguments[integers];
                location.registers[1] = abi_integer_arguments[integers + 1];
                integers += 2;
            }
            else
            {
                // A PAIR LLVM puts on the stack as an i128, aligned to 16 whatever the record's alignment.
                const uint32_t pair_log2 = (pass & BUSTER_ABI_PASS_PAIR) != 0 && alignment_log2 < 4 ? 4 : alignment_log2;
                const uint32_t slot_log2 = wide_on_stack ? (uint32_t)__builtin_ctz(bytes) : pair_log2;
                location.kind = BUSTER_ABI_LOCATION_STACK;
                location.stack_offset = abi_stack_place(&stack, &call.stack_alignment_log2, bytes, slot_log2);
            }
        }
        else
        {
            // An eightbyte at a time, the low one (unless it is padding) then the high one, each a value of its own.
            clang_integers -= need_integers;
            clang_vectors -= need_vectors;
            const uint32_t classes[2] = {
                (pass & (BUSTER_ABI_PASS_LOW_INTEGER | BUSTER_ABI_PASS_LOW_GENERAL)) != 0 ? 1u : (pass & BUSTER_ABI_PASS_LOW_SSE) != 0 ? 2u : 0u,
                (pass & BUSTER_ABI_PASS_HIGH_INTEGER) != 0 ? 1u : (pass & BUSTER_ABI_PASS_HIGH_SSE) != 0 ? 2u : 0u,
            };
            uint32_t parts = 0;
            uint32_t stacked = 0;
            for (uint32_t half = 0; half < 2; half += 1)
            {
                if (classes[half] == 1 && integers < BUSTER_ABI_INTEGER_ARGUMENTS)
                {
                    location.registers[parts] = abi_integer_arguments[integers];
                    integers += 1;
                }
                else if (classes[half] == 2 && vectors < BUSTER_ABI_VECTOR_ARGUMENTS)
                {
                    location.registers[parts] = (uint8_t)(BUSTER_ABI_REGISTER_VECTOR | vectors);
                    vectors += 1;
                }
                else if (classes[half] != 0)
                {
                    const uint32_t slot = abi_stack_place(&stack, &call.stack_alignment_log2, BUSTER_ABI_SLOT_BYTES, BUSTER_ABI_SLOT_LOG2);
                    location.stack_offset = stacked == 0 ? slot : location.stack_offset;
                    stacked += 1;
                }
                parts += classes[half] != 0 ? 1 : 0;
            }
            location.flags |= classes[0] == 0 ? BUSTER_ABI_FLAG_HIGH : 0;
            location.kind = stacked == 0 ? BUSTER_ABI_LOCATION_REGISTERS : stacked == parts ? BUSTER_ABI_LOCATION_STACK : BUSTER_ABI_LOCATION_SPLIT;
        }
        locations[i] = location;
    }
    call.stack_bytes = (uint32_t)stack;
    call.integer_registers = (uint8_t)integers;
    call.vector_registers = (uint8_t)vectors;
    return call;
}

static void abi_assign_scalar(const AbiTable* abi, AbiSignatures signatures, AbiLocation* locations, AbiCall* calls)
{
    uint64_t first = 0;
    for (uint32_t c = 0; c < signatures.count; c += 1)
    {
        const uint32_t count = signatures.counts[c];
        const uint32_t named = signatures.named[c] < count ? signatures.named[c] : count;
        calls[c] = abi_assign_call(abi, signatures.arguments + first, count, named, signatures.variadic[c] != 0, signatures.results[c], locations + first);
        first += count;
    }
}

// The vector path's lane flags, which its setup writes per argument: the first and last arguments of a call, an unnamed
// one, the first of a call whose result goes in memory (the hidden pointer takes rdi before it) and whether that result
// is DEMOTED, an argument of a variadic call, and the first of a call with more arguments than the sums of its
// registers can count (LONG: see BUSTER_ABI_COUNTED_ARGUMENTS).
#define BUSTER_ABI_LANE_START 0x01
#define BUSTER_ABI_LANE_END 0x02
#define BUSTER_ABI_LANE_UNNAMED 0x04
#define BUSTER_ABI_LANE_HIDDEN 0x08
#define BUSTER_ABI_LANE_VARIADIC 0x10
#define BUSTER_ABI_LANE_DEMOTED 0x20
#define BUSTER_ABI_LANE_LONG 0x40

// The most arguments a call may have for the vector path to assign it: each class's prefix sum of registers has 16 bits
// of a lane, and an argument needs at most two registers of a class, the hidden pointer one more (2 * 32,767 + 1 =
// 65,535). A longer call goes to the twin: past it, the general registers' sum would carry into the vector ones'.
#define BUSTER_ABI_COUNTED_ARGUMENTS 32767u
#define BUSTER_ABI_LANES 16

// A segmented scan's windows: steps[k] holds the lanes that add the lane 2^k below them (no segment starts between),
// and continuing the lanes before the vector's first start, which continue the vector before's last segment.
typedef struct AbiScan AbiScan;
struct AbiScan
{
    __mmask16 steps[4];
    __mmask16 continuing;
};

static AbiScan abi_scan(__mmask16 starts)
{
    AbiScan result;
    __mmask16 closed = starts;
    result.steps[0] = (__mmask16)~closed;
    closed = closed | _kshiftli_mask16(closed, 1);
    result.steps[1] = (__mmask16)~closed;
    closed = closed | _kshiftli_mask16(closed, 2);
    result.steps[2] = (__mmask16)~closed;
    closed = closed | _kshiftli_mask16(closed, 4);
    result.steps[3] = (__mmask16)~closed;
    closed = closed | _kshiftli_mask16(closed, 8);
    result.continuing = (__mmask16)~closed;
    return result;
}

// Inclusive sums of each segment's lanes, the continuing ones from `carry` (the vector before's last lane's).
static __m512i abi_scan_add(__m512i values, AbiScan scan, __m512i carry)
{
    const __m512i zero = _mm512_setzero_si512();
    __m512i result = _mm512_mask_add_epi32(values, scan.steps[0], values, _mm512_alignr_epi32(values, zero, 15));
    result = _mm512_mask_add_epi32(result, scan.steps[1], result, _mm512_alignr_epi32(result, zero, 14));
    result = _mm512_mask_add_epi32(result, scan.steps[2], result, _mm512_alignr_epi32(result, zero, 12));
    result = _mm512_mask_add_epi32(result, scan.steps[3], result, _mm512_alignr_epi32(result, zero, 8));
    return _mm512_mask_add_epi32(result, scan.continuing, result, carry);
}

static __m512i abi_scan_or(__m512i values, AbiScan scan, __m512i carry)
{
    const __m512i zero = _mm512_setzero_si512();
    __m512i result = _mm512_mask_or_epi32(values, scan.steps[0], values, _mm512_alignr_epi32(values, zero, 15));
    result = _mm512_mask_or_epi32(result, scan.steps[1], result, _mm512_alignr_epi32(result, zero, 14));
    result = _mm512_mask_or_epi32(result, scan.steps[2], result, _mm512_alignr_epi32(result, zero, 12));
    result = _mm512_mask_or_epi32(result, scan.steps[3], result, _mm512_alignr_epi32(result, zero, 8));
    return _mm512_mask_or_epi32(result, scan.continuing, result, carry);
}

// The vector path: every call's arguments in order, 16 at a time whatever call they belong to. Within each call's
// segment, prefix sums of the registers each argument needs (general ones in a lane's low half, vector ones in its
// high half, the hidden pointer's at the call's first lane) give each argument its first register of each class, and
// prefix sums of the slot sizes of those on the stack their offsets. That is exactly the assignment unless:
//   - an argument doesn't fit while a register it needs is still free (a two-eightbyte one with one free, or one of
//     both classes with one class full), which the sums count as used though it stays free for the arguments after;
//   - a stack argument's alignment doesn't divide the offset the sums give it, so the twin would pad before it;
//   - an argument is UNCOUNTED, whose registers clang doesn't count but LLVM does, or a named WIDE one of a variadic
//     call, the reverse;
//   - a stack argument is EMPTY, whose slot depends on clang's count, or one is LOW_GENERAL, whose low eightbyte clang
//     counts as SSE and LLVM puts in a general register, or the result is one LLVM demotes to a hidden pointer, which
//     clang doesn't count (DEMOTED);
//   - the call has more than BUSTER_ABI_COUNTED_ARGUMENTS arguments, whose sums 16 bits a class can't hold;
// and the calls with one of those (rare in compiled code) go to the twin, which assigns them one argument at a time.
static void abi_assign(const AbiTable* abi, AbiSignatures signatures, AbiLocation* locations, AbiCall* calls)
{
    Scratch scratch = scratch_begin(0);
    uint64_t total = 0;
    for (uint32_t c = 0; c < signatures.count; c += 1)
    {
        total += signatures.counts[c];
    }
    uint8_t* flags = arena_push(scratch.arena, total + BUSTER_ABI_LANES, 64);
    uint32_t* call_ids = flags != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch.arena, uint32_t, total + BUSTER_ABI_LANES) : 0;
    uint64_t* firsts = call_ids != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch.arena, uint64_t, signatures.count) : 0;
    uint32_t* deferred = firsts != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch.arena, uint32_t, signatures.count) : 0;
    if (deferred == 0)
    {
        abi_assign_scalar(abi, signatures, locations, calls);
    }
    else
    {
        // The setup: each call's result and starting totals, and its arguments' lane flags and call.
        memset(flags, 0, total);
        uint64_t first = 0;
        for (uint32_t c = 0; c < signatures.count; c += 1)
        {
            const uint32_t count = signatures.counts[c];
            const uint32_t named = signatures.named[c] < count ? signatures.named[c] : count;
            const AbiReturn result = abi->results[signatures.results[c] >> BUSTER_TYPE_INDEX_SHIFT];
            const bool hidden = result.kind == BUSTER_ABI_RESULT_MEMORY;
            calls[c] = (AbiCall){
                .stack_bytes = 0,
                .stack_alignment_log2 = BUSTER_ABI_STACK_ALIGNMENT_LOG2,
                .integer_registers = hidden ? 1 : 0,
                .vector_registers = 0,
                .padding = 0,
                .result = result,
            };
            firsts[c] = first;
            if (count != 0)
            {
                const bool demoted = hidden && (result.flags & BUSTER_ABI_FLAG_DEMOTED) != 0;
                flags[first] |= BUSTER_ABI_LANE_START | (hidden ? BUSTER_ABI_LANE_HIDDEN : 0) | (demoted ? BUSTER_ABI_LANE_DEMOTED : 0) |
                                (count > BUSTER_ABI_COUNTED_ARGUMENTS ? BUSTER_ABI_LANE_LONG : 0);
                flags[first + count - 1] |= BUSTER_ABI_LANE_END;
                for (uint32_t i = 0; i < count; i += 1)
                {
                    flags[first + i] |= (i >= named ? BUSTER_ABI_LANE_UNNAMED : 0) | (signatures.variadic[c] != 0 ? BUSTER_ABI_LANE_VARIADIC : 0);
                }
                for (uint32_t i = 0; i < count; i += 1)
                {
                    call_ids[first + i] = c;
                }
            }
            first += count;
        }
        const __m512i zero = _mm512_setzero_si512();
        const __m512i general = _mm512_setr_epi32(BUSTER_ABI_RDI, BUSTER_ABI_RSI, BUSTER_ABI_RDX, BUSTER_ABI_RCX, BUSTER_ABI_R8, BUSTER_ABI_R9, BUSTER_ABI_REGISTER_NONE,
                                                  BUSTER_ABI_REGISTER_NONE, BUSTER_ABI_REGISTER_NONE, BUSTER_ABI_REGISTER_NONE, BUSTER_ABI_REGISTER_NONE,
                                                  BUSTER_ABI_REGISTER_NONE, BUSTER_ABI_REGISTER_NONE, BUSTER_ABI_REGISTER_NONE, BUSTER_ABI_REGISTER_NONE,
                                                  BUSTER_ABI_REGISTER_NONE);
        const __m512i low_halves = _mm512_set1_epi32(0xffff);
        const __m512i last_lane = _mm512_set1_epi32(BUSTER_ABI_LANES - 1);
        const __m512i interleave_low = _mm512_setr_epi32(0, 16, 1, 17, 2, 18, 3, 19, 4, 20, 5, 21, 6, 22, 7, 23);
        const __m512i interleave_high = _mm512_setr_epi32(8, 24, 9, 25, 10, 26, 11, 27, 12, 28, 13, 29, 14, 30, 15, 31);
        const __m512i none = _mm512_set1_epi32(BUSTER_ABI_REGISTER_NONE);
        __m512i carry_registers = zero;
        __m512i carry_stack = zero;
        __m512i carry_alignment = zero;
        uint32_t deferred_count = 0;
        for (uint64_t base = 0; base < total; base += BUSTER_ABI_LANES)
        {
            const uint64_t remaining = total - base;
            const __mmask16 valid = remaining >= BUSTER_ABI_LANES ? (__mmask16)0xffff : (__mmask16)((1u << remaining) - 1);
            const __m512i indices = _mm512_srli_epi32(_mm512_maskz_loadu_epi32(valid, signatures.arguments + base), BUSTER_TYPE_INDEX_SHIFT);
            const __m512i pass = _mm512_mask_i32gather_epi32(zero, valid, indices, abi->pass, 4);
            const __m512i bytes = _mm512_mask_i32gather_epi32(zero, valid, indices, abi->stack_bytes, 4);
            const __m512i lane_flags = _mm512_cvtepu8_epi32(_mm_maskz_loadu_epi8(valid, flags + base));
            const __mmask16 starts = _mm512_test_epi32_mask(lane_flags, _mm512_set1_epi32(BUSTER_ABI_LANE_START));
            const __mmask16 ends = _mm512_test_epi32_mask(lane_flags, _mm512_set1_epi32(BUSTER_ABI_LANE_END));
            const __mmask16 unnamed = _mm512_test_epi32_mask(lane_flags, _mm512_set1_epi32(BUSTER_ABI_LANE_UNNAMED));
            const __mmask16 hidden = _mm512_test_epi32_mask(lane_flags, _mm512_set1_epi32(BUSTER_ABI_LANE_HIDDEN));
            const __mmask16 variadic = _mm512_test_epi32_mask(lane_flags, _mm512_set1_epi32(BUSTER_ABI_LANE_VARIADIC));
            const __mmask16 ignore = _mm512_test_epi32_mask(pass, _mm512_set1_epi32(BUSTER_ABI_PASS_IGNORE));
            const __mmask16 uncounted = _mm512_test_epi32_mask(pass, _mm512_set1_epi32(BUSTER_ABI_PASS_UNCOUNTED));
            const __mmask16 in_memory = _mm512_test_epi32_mask(pass, _mm512_set1_epi32(BUSTER_ABI_PASS_MEMORY)) |
                                        (_mm512_test_epi32_mask(pass, _mm512_set1_epi32(BUSTER_ABI_PASS_WIDE)) & unnamed);
            const __mmask16 counted = valid & (__mmask16)~in_memory;
            // The registers each argument needs, as clang counts them.
            const __m512i low_integer = _mm512_and_si512(pass, _mm512_set1_epi32(1));
            const __m512i low_sse = _mm512_and_si512(_mm512_srli_epi32(pass, 1), _mm512_set1_epi32(1));
            const __m512i high_integer = _mm512_and_si512(_mm512_srli_epi32(pass, 2), _mm512_set1_epi32(1));
            const __m512i high_sse = _mm512_and_si512(_mm512_srli_epi32(pass, 3), _mm512_set1_epi32(1));
            const __m512i need_integers = _mm512_maskz_add_epi32(counted, low_integer, high_integer);
            const __m512i need_vectors = _mm512_maskz_add_epi32(counted, low_sse, high_sse);
            const __m512i need = _mm512_add_epi32(need_integers, _mm512_slli_epi32(need_vectors, 16));
            const AbiScan scan = abi_scan(starts);
            const __m512i inclusive = abi_scan_add(_mm512_mask_add_epi32(need, hidden, need, _mm512_set1_epi32(1)), scan, carry_registers);
            carry_registers = _mm512_permutexvar_epi32(last_lane, inclusive);
            const __m512i exclusive = _mm512_sub_epi32(inclusive, need);
            const __m512i exclusive_integers = _mm512_and_si512(exclusive, low_halves);
            const __m512i exclusive_vectors = _mm512_srli_epi32(exclusive, 16);
            // Each class only where the argument needs it: a long fits after nine doubles, whose sum is past 8 (the
            // benchmark found the vector path left 23% of its arguments to the twin before, and 5% so).
            const __mmask16 integers_fit = (__mmask16)~_mm512_test_epi32_mask(need_integers, need_integers) |
                                           _mm512_cmple_epu32_mask(_mm512_add_epi32(exclusive_integers, need_integers), _mm512_set1_epi32(BUSTER_ABI_INTEGER_ARGUMENTS));
            const __mmask16 vectors_fit = (__mmask16)~_mm512_test_epi32_mask(need_vectors, need_vectors) |
                                          _mm512_cmple_epu32_mask(_mm512_add_epi32(exclusive_vectors, need_vectors), _mm512_set1_epi32(BUSTER_ABI_VECTOR_ARGUMENTS));
            const __mmask16 fits = integers_fit & vectors_fit;
            const __mmask16 spills = _mm512_test_epi32_mask(need, need) & (__mmask16)~fits;
            const __mmask16 inexact = spills & ((_mm512_test_epi32_mask(need_integers, need_integers) &
                                                 _mm512_cmplt_epu32_mask(exclusive_integers, _mm512_set1_epi32(BUSTER_ABI_INTEGER_ARGUMENTS))) |
                                                (_mm512_test_epi32_mask(need_vectors, need_vectors) &
                                                 _mm512_cmplt_epu32_mask(exclusive_vectors, _mm512_set1_epi32(BUSTER_ABI_VECTOR_ARGUMENTS))));
            const __mmask16 on_stack = (in_memory | spills) & valid & (__mmask16)~ignore;
            // The stack slots, and their alignments ORed (each a power of two, so the OR's highest bit is the largest).
            const __m512i slot_alignment = _mm512_sllv_epi32(_mm512_set1_epi32(1), _mm512_srli_epi32(_mm512_and_si512(pass, _mm512_set1_epi32((int)BUSTER_ABI_PASS_ALIGNMENT)),
                                                                                                     BUSTER_ABI_PASS_ALIGNMENT_SHIFT));
            const __m512i slot_bytes = _mm512_maskz_mov_epi32(on_stack, bytes);
            const __m512i stack_inclusive = abi_scan_add(slot_bytes, scan, carry_stack);
            const __m512i alignment_inclusive = abi_scan_or(_mm512_maskz_mov_epi32(on_stack, slot_alignment), scan, carry_alignment);
            carry_stack = _mm512_permutexvar_epi32(last_lane, stack_inclusive);
            carry_alignment = _mm512_permutexvar_epi32(last_lane, alignment_inclusive);
            const __m512i stack_exclusive = _mm512_sub_epi32(stack_inclusive, slot_bytes);
            const __mmask16 padded = on_stack & _mm512_test_epi32_mask(stack_exclusive, _mm512_sub_epi32(slot_alignment, _mm512_set1_epi32(1)));
            // A named wide vector in a variadic call: clang counts a register that LLVM doesn't give it. And one of no size
            // on the stack, which may take no slot (EMPTY).
            const __mmask16 wide_named = _mm512_test_epi32_mask(pass, _mm512_set1_epi32(BUSTER_ABI_PASS_WIDE)) & variadic & (__mmask16)~unnamed;
            const __mmask16 empty = on_stack & _mm512_test_epi32_mask(pass, _mm512_set1_epi32((int)BUSTER_ABI_PASS_EMPTY));
            const __mmask16 general_low = _mm512_test_epi32_mask(pass, _mm512_set1_epi32((int)BUSTER_ABI_PASS_LOW_GENERAL));
            const __mmask16 demoted = _mm512_test_epi32_mask(lane_flags, _mm512_set1_epi32(BUSTER_ABI_LANE_DEMOTED));
            const __mmask16 long_call = _mm512_test_epi32_mask(lane_flags, _mm512_set1_epi32(BUSTER_ABI_LANE_LONG));
            const __mmask16 deferred_lanes = valid & (uncounted | inexact | padded | wide_named | empty | general_low | demoted | long_call);
            // The registers: the low eightbyte's, then the high one's (first for a value whose low one is padding).
            const __mmask16 low_integer_lanes = _mm512_test_epi32_mask(low_integer, low_integer);
            const __mmask16 low_sse_lanes = _mm512_test_epi32_mask(low_sse, low_sse);
            const __mmask16 has_low = low_integer_lanes | low_sse_lanes;
            const __m512i first_general = _mm512_permutexvar_epi32(_mm512_min_epu32(exclusive_integers, _mm512_set1_epi32(15)), general);
            const __m512i second_general = _mm512_permutexvar_epi32(_mm512_min_epu32(_mm512_add_epi32(exclusive_integers, low_integer), _mm512_set1_epi32(15)), general);
            const __m512i first_vector = _mm512_or_si512(exclusive_vectors, _mm512_set1_epi32(BUSTER_ABI_REGISTER_VECTOR));
            const __m512i second_vector = _mm512_or_si512(_mm512_add_epi32(exclusive_vectors, low_sse), _mm512_set1_epi32(BUSTER_ABI_REGISTER_VECTOR));
            const __m512i low_register = _mm512_mask_mov_epi32(_mm512_mask_mov_epi32(none, low_sse_lanes, first_vector), low_integer_lanes, first_general);
            const __m512i high_register = _mm512_mask_mov_epi32(_mm512_mask_mov_epi32(none, _mm512_test_epi32_mask(high_sse, high_sse), second_vector),
                                                                _mm512_test_epi32_mask(high_integer, high_integer), second_general);
            const __mmask16 in_registers = valid & (__mmask16)~on_stack & (__mmask16)~ignore;
            const __m512i first_register = _mm512_mask_mov_epi32(none, in_registers, _mm512_mask_mov_epi32(high_register, has_low, low_register));
            const __m512i second_register = _mm512_mask_mov_epi32(none, in_registers & has_low, high_register);
            const __m512i kind = _mm512_mask_mov_epi32(_mm512_maskz_mov_epi32(in_registers, _mm512_set1_epi32(BUSTER_ABI_LOCATION_REGISTERS)), on_stack,
                                                       _mm512_set1_epi32(BUSTER_ABI_LOCATION_STACK));
            const __m512i extension = _mm512_and_si512(_mm512_srli_epi32(pass, 10), _mm512_set1_epi32(BUSTER_ABI_FLAG_SIGN | BUSTER_ABI_FLAG_ZERO));
            const __m512i location_flags = _mm512_mask_or_epi32(extension, in_registers & (__mmask16)~has_low, extension, _mm512_set1_epi32(BUSTER_ABI_FLAG_HIGH));
            const __m512i word = _mm512_or_si512(_mm512_or_si512(first_register, _mm512_slli_epi32(second_register, 8)),
                                                 _mm512_or_si512(_mm512_slli_epi32(kind, 16), _mm512_slli_epi32(location_flags, 24)));
            const __m512i offset = _mm512_maskz_mov_epi32(on_stack, stack_exclusive);
            _mm512_mask_storeu_epi64(locations + base, (__mmask8)valid, _mm512_permutex2var_epi32(offset, interleave_low, word));
            _mm512_mask_storeu_epi64(locations + base + 8, (__mmask8)(valid >> 8), _mm512_permutex2var_epi32(offset, interleave_high, word));
            const __mmask16 call_ends = ends & valid;
            if ((call_ends | deferred_lanes) != 0)
            {
                _Alignas(64) uint32_t ids[BUSTER_ABI_LANES];
                _Alignas(64) uint32_t registers[BUSTER_ABI_LANES];
                _Alignas(64) uint32_t stack[BUSTER_ABI_LANES];
                _Alignas(64) uint32_t alignment[BUSTER_ABI_LANES];
                _mm512_store_si512((void*)ids, _mm512_maskz_loadu_epi32(valid, call_ids + base));
                _mm512_store_si512((void*)registers, inclusive);
                _mm512_store_si512((void*)stack, stack_inclusive);
                _mm512_store_si512((void*)alignment, alignment_inclusive);
                for (uint32_t bits = call_ends; bits != 0; bits &= bits - 1)
                {
                    const uint32_t lane = (uint32_t)__builtin_ctz(bits);
                    AbiCall* call = calls + ids[lane];
                    const uint32_t integers = registers[lane] & 0xffff;
                    const uint32_t vectors = registers[lane] >> 16;
                    const uint32_t largest = alignment[lane] == 0 ? 0 : 31 - (uint32_t)__builtin_clz(alignment[lane]);
                    call->stack_bytes = stack[lane];
                    call->stack_alignment_log2 = (uint8_t)(largest > BUSTER_ABI_STACK_ALIGNMENT_LOG2 ? largest : BUSTER_ABI_STACK_ALIGNMENT_LOG2);
                    call->integer_registers = (uint8_t)(integers < BUSTER_ABI_INTEGER_ARGUMENTS ? integers : BUSTER_ABI_INTEGER_ARGUMENTS);
                    call->vector_registers = (uint8_t)(vectors < BUSTER_ABI_VECTOR_ARGUMENTS ? vectors : BUSTER_ABI_VECTOR_ARGUMENTS);
                }
                for (uint32_t bits = deferred_lanes; bits != 0; bits &= bits - 1)
                {
                    const uint32_t id = ids[__builtin_ctz(bits)];
                    if (deferred_count == 0 || deferred[deferred_count - 1] != id)
                    {
                        deferred[deferred_count] = id;
                        deferred_count += 1;
                    }
                }
            }
        }
        for (uint32_t i = 0; i < deferred_count; i += 1)
        {
            const uint32_t c = deferred[i];
            const uint32_t count = signatures.counts[c];
            const uint32_t named = signatures.named[c] < count ? signatures.named[c] : count;
            calls[c] = abi_assign_call(abi, signatures.arguments + firsts[c], count, named, signatures.variadic[c] != 0, signatures.results[c], locations + firsts[c]);
        }
    }
    scratch_end(scratch);
}

// ---------------------------------------------------------------------------------------------------------------------
// va_arg

// clang's EmitVAArg's plan: the type classified as an unnamed argument; from the save area what is passed in registers,
// from the overflow area the rest, which it aligns to the type's alignment as va_arg names it (an aligned typedef's
// included) when that is more than 8.
static AbiVaArg abi_va_arg(const AbiTable* abi, const TypeTable* table, TypeId type)
{
    const uint32_t index = type >> BUSTER_TYPE_INDEX_SHIFT;
    const uint32_t pass = abi->pass[index];
    // EmitVAArg aligns the overflow area to the canonical type's alignment: an aligned typedef's attribute doesn't count.
    const uint32_t alignment_log2 = table->types.alignment_log2[abi_resolved_index(table, type)];
    AbiVaArg result = {
        .kind = BUSTER_ABI_LOCATION_STACK,
        .integer_registers = 0,
        .vector_registers = 0,
        .sources = {
            BUSTER_ABI_VA_NONE,
            BUSTER_ABI_VA_NONE,
        },
        .flags = 0,
        .overflow_alignment_log2 = (uint8_t)(alignment_log2 > BUSTER_ABI_SLOT_LOG2 ? alignment_log2 : BUSTER_ABI_SLOT_LOG2),
        .stack_bytes = (uint32_t)((table->types.size[abi_resolved_index(table, type)] + BUSTER_ABI_SLOT_BYTES - 1) & ~(uint64_t)(BUSTER_ABI_SLOT_BYTES - 1)),
    };
    if ((pass & BUSTER_ABI_PASS_IGNORE) != 0)
    {
        result.kind = BUSTER_ABI_LOCATION_IGNORE;
    }
    else if ((pass & (BUSTER_ABI_PASS_MEMORY | BUSTER_ABI_PASS_WIDE)) == 0)
    {
        result.kind = BUSTER_ABI_LOCATION_REGISTERS;
        result.integer_registers = (uint8_t)(((pass & BUSTER_ABI_PASS_LOW_INTEGER) != 0 ? 1 : 0) + ((pass & BUSTER_ABI_PASS_HIGH_INTEGER) != 0 ? 1 : 0));
        result.vector_registers = (uint8_t)(((pass & BUSTER_ABI_PASS_LOW_SSE) != 0 ? 1 : 0) + ((pass & BUSTER_ABI_PASS_HIGH_SSE) != 0 ? 1 : 0));
        result.sources[0] = (pass & BUSTER_ABI_PASS_LOW_INTEGER) != 0 ? BUSTER_ABI_VA_GENERAL : (pass & BUSTER_ABI_PASS_LOW_SSE) != 0 ? BUSTER_ABI_VA_VECTOR : BUSTER_ABI_VA_NONE;
        result.sources[1] = (pass & BUSTER_ABI_PASS_HIGH_INTEGER) != 0 ? BUSTER_ABI_VA_GENERAL : (pass & BUSTER_ABI_PASS_HIGH_SSE) != 0 ? BUSTER_ABI_VA_VECTOR : BUSTER_ABI_VA_NONE;
        result.flags = (pass & (BUSTER_ABI_PASS_LOW_INTEGER | BUSTER_ABI_PASS_LOW_SSE)) == 0 ? BUSTER_ABI_FLAG_HIGH : 0;
    }
    return result;
}
