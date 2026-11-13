// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// The C type table (#132): every type of a translation unit as a TypeId, the u32 that the checker (#133) compares and the
// calling convention (#137) classifies. docs/c-types.md has the design and its 9700X measurements. The reference is
// clang 23 on x86-64 Linux (LP64, the System V psABI), as #154 decided: the builtin types are clang's, sizes and
// alignments its getTypeInfo's, record layout its ItaniumRecordLayoutBuilder's, the conversions its
// UsualArithmeticConversions, compatibility and composite types its mergeTypes, and printing its TypePrinter. clang's
// source was read for these rules; none of it is copied. test-types checks each part against clang itself.
//
// Types:
//   - A TypeId is a type's index shifted left by 3, with const, volatile and restrict in its low bits. _Atomic is a
//     type of its own, since it changes size and alignment. Index 0 is none.
//   - The builtin types sit at fixed indices below 64, each equal to its arithmetic code (BUSTER_TYPE_CODE_*): every
//     builtin type clang has on x86-64 (void, bool, the character and standard integer types, __int128, __bf16,
//     _Float16, __fp16, float, double, long double, __float128, nullptr_t) and the complex types of their elements.
//   - Derived types are hash-consed: pointers, arrays of constant, unknown or variable size, functions, GNU and clang
//     vectors, _Atomic, _BitInt(N), complex _BitInts, and ALIGNED, a typedef's aligned attribute. Equal structures get
//     equal TypeIds, so that compatibility is most often a compare. Requests are normalized first as clang's canonical
//     types are (TypeColumns says how), so that every spelling of a type gives one TypeId.
//   - Tags (struct, union, enum) are one per declaration, with their members, layout and completion order.
//
// Hash-consing: the index (TypeIndex) maps a derived type's key (kind, key flags, base, extent and parameters) to its
// index, by open addressing over buckets of four 16-byte slots, one cache line, each slot holding as much of its key as
// fits: a lookup reads one line, and only a function's parameters and a wide extent need the columns. type_derive_batch
// hashes eight keys per vector and compares each bucket's four slots in one instruction; its misses go through the
// one-at-a-time path in request order, so that ids never depend on the batch.
//
// Layout is type_layout_scalar's, clang's algorithm one record at a time, or type_layout_records', 16 records at once,
// one per lane, with the scalar layout as its twin. The conversions are tables indexed by arithmetic codes, looked up
// with vpermb 64 pairs at a time, with a scalar twin. Compatibility, composite types and printing walk types with
// explicit stacks, the first two with a memo of the pairs they have met, since types share their insides: nothing here
// recurses.
//
// Who writes what: the front end (declarators, #127; the checker, #133; #pragma pack's events, #124; attributes, #147)
// asks for types and completes tags; layout writes the members' offsets and the records' sizes; the readers (#133,
// #137) read the columns, which never change once written but for a tag's at its completion. One lane writes at a time,
// and lanes may read while none writes.
//
// Memory: type_initialize pushes every column from one arena with room for the capacities it is given (records 0 to 63
// are the builtin types), and the index grows in an arena of its own. A request past a capacity fails and returns 0
// with last_error saying why; nothing is ever truncated.
//
// main.c and c_types_benchmark.c include this file after lane.c (Arena, Scratch) and String8. Layout, in this order:
//   - API: TypeId, the qualifiers, the codes (BUSTER_TYPE_CODE_*) and builtin TypeIds, the kinds (BUSTER_TYPE_KIND_*),
//     the flags, the limits, TypeError, the columns (TypeColumns, TypeParameters, TypeTags, TypeMembers), the index
//     (TypeSlot, TypeIndex), TypeTable, TypeCapacity, TypeMember, TypeRecordAttributes, TypeKeys, TypeNames and the
//     prototypes.
//   - The builtin types: TypeBuiltin, type_builtins, BUSTER_TYPE_COMPLEX_ELEMENTS.
//   - The table: type_capacity_bytes, type_index_capacity_bytes, type_initialize.
//   - What a type is: type_is_valid, type_kind, type_qualify, type_size, type_alignment, type_is_complete,
//     type_arithmetic_code, type_strip_aligned, type_element, type_parameters, type_return.
//   - Hash-consing: the hash (type_key_hash), the index (type_index_find, type_index_find_vector, type_index_grow), the
//     checks (type_check_derivation), type_derive, the constructors (type_pointer to type_function) and
//     type_derive_batch.
//   - Tags: type_tag, type_record_define, type_enum_value_bits, type_enum_underlying, type_enum_complete,
//     type_find_member, type_builtin_va_list.
//   - Layout: type_layout_scalar, type_record_layout, type_transpose_16, type_layout_read_types, type_layout_group,
//     type_layout_records.
//   - Conversions: type_integer_conversion, type_promote, type_promote_bit_field, type_default_argument_promote,
//     type_usual_arithmetic, the kernels' tables, type_promote_codes, type_usual_arithmetic_vector and its scalar twin
//     type_usual_arithmetic_code, type_usual_arithmetic_types.
//   - Compatibility and composite types: the pair memo (TypePairMemo), type_compatible, type_canonical_compare and
//     type_same_canonical, type_merge_finish, type_composite.
//   - Printing: TypePrintFrame, type_print_step, type_print.

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

// CPU architecture dependent includes
#include "host_isa.h"

// ---------------------------------------------------------------------------------------------------------------------
// API

// A type: its index in the table, shifted left by BUSTER_TYPE_INDEX_SHIFT, with its qualifiers in the low bits. 0 is
// none. Qualifying is an OR and dropping qualifiers a mask, so a qualified type has no record of its own.
typedef uint32_t TypeId;

#define BUSTER_TYPE_CONST 1u
#define BUSTER_TYPE_VOLATILE 2u
#define BUSTER_TYPE_RESTRICT 4u
#define BUSTER_TYPE_QUALIFIERS 7u
#define BUSTER_TYPE_INDEX_SHIFT 3
// Indices are 29 bits.
#define BUSTER_TYPE_MAX_INDEX ((1u << (32 - BUSTER_TYPE_INDEX_SHIFT)) - 1)

// Arithmetic codes: what the conversions see of a type, below 64 so that a code indexes a 64-byte table with one
// vpermb. The builtin types sit at the indices equal to their codes, so a builtin's TypeId is its code shifted left by
// BUSTER_TYPE_INDEX_SHIFT and a conversion kernel's result is a TypeId after a shift. The floating codes are in
// clang's FloatingRank order (__bf16, _Float16, __fp16, float, double, long double, __float128), so the usual
// arithmetic conversions on two of them are a maximum. An enumeration's code is its underlying type's; a _BitInt's is
// BIT_INT or UNSIGNED_BIT_INT, which are codes but not indices, since a _BitInt's rank depends on its width; a complex
// type's is COMPLEX plus its element's.
#define BUSTER_TYPE_CODE_NONE 0
#define BUSTER_TYPE_CODE_VOID 1
#define BUSTER_TYPE_CODE_BOOL 2
#define BUSTER_TYPE_CODE_CHAR 3
#define BUSTER_TYPE_CODE_SCHAR 4
#define BUSTER_TYPE_CODE_UCHAR 5
#define BUSTER_TYPE_CODE_SHORT 6
#define BUSTER_TYPE_CODE_USHORT 7
#define BUSTER_TYPE_CODE_INT 8
#define BUSTER_TYPE_CODE_UINT 9
#define BUSTER_TYPE_CODE_LONG 10
#define BUSTER_TYPE_CODE_ULONG 11
#define BUSTER_TYPE_CODE_LLONG 12
#define BUSTER_TYPE_CODE_ULLONG 13
#define BUSTER_TYPE_CODE_INT128 14
#define BUSTER_TYPE_CODE_UINT128 15
#define BUSTER_TYPE_CODE_BF16 16
#define BUSTER_TYPE_CODE_FLOAT16 17
#define BUSTER_TYPE_CODE_HALF 18
#define BUSTER_TYPE_CODE_FLOAT 19
#define BUSTER_TYPE_CODE_DOUBLE 20
#define BUSTER_TYPE_CODE_LONG_DOUBLE 21
#define BUSTER_TYPE_CODE_FLOAT128 22
#define BUSTER_TYPE_CODE_NULLPTR 23
#define BUSTER_TYPE_CODE_BIT_INT 24
#define BUSTER_TYPE_CODE_UNSIGNED_BIT_INT 25
// What a conversion kernel gives a pair it leaves to the scalar path: one with a _BitInt or a complex integer type.
#define BUSTER_TYPE_CODE_DEFERRED 31
#define BUSTER_TYPE_CODE_COMPLEX 32
#define BUSTER_TYPE_CODE_COUNT 64
// Indices below this are the builtin types' (some unused); the types the table makes start here.
#define BUSTER_TYPE_BUILTIN_COUNT 64

#define BUSTER_TYPE_BUILTIN(code) ((TypeId)(code) << BUSTER_TYPE_INDEX_SHIFT)
#define BUSTER_TYPE_VOID BUSTER_TYPE_BUILTIN(BUSTER_TYPE_CODE_VOID)
#define BUSTER_TYPE_BOOL BUSTER_TYPE_BUILTIN(BUSTER_TYPE_CODE_BOOL)
#define BUSTER_TYPE_CHAR BUSTER_TYPE_BUILTIN(BUSTER_TYPE_CODE_CHAR)
#define BUSTER_TYPE_SCHAR BUSTER_TYPE_BUILTIN(BUSTER_TYPE_CODE_SCHAR)
#define BUSTER_TYPE_UCHAR BUSTER_TYPE_BUILTIN(BUSTER_TYPE_CODE_UCHAR)
#define BUSTER_TYPE_SHORT BUSTER_TYPE_BUILTIN(BUSTER_TYPE_CODE_SHORT)
#define BUSTER_TYPE_USHORT BUSTER_TYPE_BUILTIN(BUSTER_TYPE_CODE_USHORT)
#define BUSTER_TYPE_INT BUSTER_TYPE_BUILTIN(BUSTER_TYPE_CODE_INT)
#define BUSTER_TYPE_UINT BUSTER_TYPE_BUILTIN(BUSTER_TYPE_CODE_UINT)
#define BUSTER_TYPE_LONG BUSTER_TYPE_BUILTIN(BUSTER_TYPE_CODE_LONG)
#define BUSTER_TYPE_ULONG BUSTER_TYPE_BUILTIN(BUSTER_TYPE_CODE_ULONG)
#define BUSTER_TYPE_LLONG BUSTER_TYPE_BUILTIN(BUSTER_TYPE_CODE_LLONG)
#define BUSTER_TYPE_ULLONG BUSTER_TYPE_BUILTIN(BUSTER_TYPE_CODE_ULLONG)
#define BUSTER_TYPE_INT128 BUSTER_TYPE_BUILTIN(BUSTER_TYPE_CODE_INT128)
#define BUSTER_TYPE_UINT128 BUSTER_TYPE_BUILTIN(BUSTER_TYPE_CODE_UINT128)
#define BUSTER_TYPE_BF16 BUSTER_TYPE_BUILTIN(BUSTER_TYPE_CODE_BF16)
#define BUSTER_TYPE_FLOAT16 BUSTER_TYPE_BUILTIN(BUSTER_TYPE_CODE_FLOAT16)
#define BUSTER_TYPE_HALF BUSTER_TYPE_BUILTIN(BUSTER_TYPE_CODE_HALF)
#define BUSTER_TYPE_FLOAT BUSTER_TYPE_BUILTIN(BUSTER_TYPE_CODE_FLOAT)
#define BUSTER_TYPE_DOUBLE BUSTER_TYPE_BUILTIN(BUSTER_TYPE_CODE_DOUBLE)
#define BUSTER_TYPE_LONG_DOUBLE BUSTER_TYPE_BUILTIN(BUSTER_TYPE_CODE_LONG_DOUBLE)
#define BUSTER_TYPE_FLOAT128 BUSTER_TYPE_BUILTIN(BUSTER_TYPE_CODE_FLOAT128)
#define BUSTER_TYPE_NULLPTR BUSTER_TYPE_BUILTIN(BUSTER_TYPE_CODE_NULLPTR)
// The builtin complex types: _Complex of each integer type but bool, and of each floating type but __fp16, at COMPLEX
// plus their element's code (BUSTER_TYPE_COMPLEX_ELEMENTS; a declaration may spell fewer of them).
#define BUSTER_TYPE_COMPLEX(element_code) BUSTER_TYPE_BUILTIN(BUSTER_TYPE_CODE_COMPLEX + (element_code))
#define BUSTER_TYPE_COMPLEX_FLOAT BUSTER_TYPE_COMPLEX(BUSTER_TYPE_CODE_FLOAT)
#define BUSTER_TYPE_COMPLEX_DOUBLE BUSTER_TYPE_COMPLEX(BUSTER_TYPE_CODE_DOUBLE)
#define BUSTER_TYPE_COMPLEX_LONG_DOUBLE BUSTER_TYPE_COMPLEX(BUSTER_TYPE_CODE_LONG_DOUBLE)

// Kinds: what a type is, and so what its columns hold (TypeColumns).
#define BUSTER_TYPE_KIND_NONE 0
#define BUSTER_TYPE_KIND_VOID 1
// bool, the character types, the standard integer types and __int128.
#define BUSTER_TYPE_KIND_INTEGER 2
// The real floating types.
#define BUSTER_TYPE_KIND_FLOATING 3
#define BUSTER_TYPE_KIND_NULLPTR 4
#define BUSTER_TYPE_KIND_BIT_INT 5
#define BUSTER_TYPE_KIND_COMPLEX 6
#define BUSTER_TYPE_KIND_POINTER 7
#define BUSTER_TYPE_KIND_ARRAY 8
#define BUSTER_TYPE_KIND_INCOMPLETE_ARRAY 9
// A variable length array, or one of unspecified size ([*], in a prototype).
#define BUSTER_TYPE_KIND_VARIABLE_ARRAY 10
#define BUSTER_TYPE_KIND_FUNCTION 11
// GNU's vector_size vectors and clang's ext_vector_type vectors.
#define BUSTER_TYPE_KIND_VECTOR 12
#define BUSTER_TYPE_KIND_EXT_VECTOR 13
#define BUSTER_TYPE_KIND_ATOMIC 14
#define BUSTER_TYPE_KIND_STRUCT 15
#define BUSTER_TYPE_KIND_UNION 16
#define BUSTER_TYPE_KIND_ENUM 17
// The wrappers, the last kinds, so that one compare finds either (BUSTER_TYPE_KIND_WRAPPERS, #1447): the readers look
// through them, their base being the type with their qualifiers.
// A typedef's aligned attribute: its base type with another alignment, higher or lower (clang's __m128i_u).
#define BUSTER_TYPE_KIND_ALIGNED 18
// clang's extended qualifiers (#1447): its base type, unqualified, with an address space and a garbage collection
// qualifier (extent, a word of BUSTER_TYPE_SPACE_* and BUSTER_TYPE_GC_*), part of the type's identity as const is. Its size,
// alignment, code and completeness are its base's, and the readers look through it as through ALIGNED
// (type_strip_aligned). Its base is never ALIGNED, QUALIFIED, an array (an array's element takes the qualifiers, as
// clang's canonical types have them) or a function; an ALIGNED type's base may be QUALIFIED.
#define BUSTER_TYPE_KIND_QUALIFIED 19
#define BUSTER_TYPE_KIND_COUNT 20
// The first of the kinds that the readers look through, every kind from it a wrapper.
#define BUSTER_TYPE_KIND_WRAPPERS BUSTER_TYPE_KIND_ALIGNED
_Static_assert(BUSTER_TYPE_KIND_QUALIFIED == BUSTER_TYPE_KIND_ALIGNED + 1 && BUSTER_TYPE_KIND_COUNT == BUSTER_TYPE_KIND_QUALIFIED + 1, "the wrappers are the last kinds");

// A QUALIFIED type's word. The address space is clang's LangAS as C on x86-64 has it: 0 the default, then OpenCL's
// (opencl_global to opencl_global_host, which C takes as attributes), then a target's `address_space(N)` as
// BUSTER_TYPE_SPACE_TARGET + N, N at most BUSTER_TYPE_SPACE_MAX_TARGET. The garbage collection qualifier is objc_gc's
// weak or strong.
#define BUSTER_TYPE_SPACE_DEFAULT 0u
#define BUSTER_TYPE_SPACE_GLOBAL 1u
#define BUSTER_TYPE_SPACE_LOCAL 2u
#define BUSTER_TYPE_SPACE_CONSTANT 3u
#define BUSTER_TYPE_SPACE_PRIVATE 4u
#define BUSTER_TYPE_SPACE_GENERIC 5u
#define BUSTER_TYPE_SPACE_GLOBAL_DEVICE 6u
#define BUSTER_TYPE_SPACE_GLOBAL_HOST 7u
#define BUSTER_TYPE_SPACE_TARGET 16u
// clang's limit, "address space is larger than the maximum supported": 2^23 less its LangAS enumerators.
#define BUSTER_TYPE_SPACE_MAX_TARGET 8388580u
#define BUSTER_TYPE_SPACE_MASK 0xffffffu
#define BUSTER_TYPE_GC_SHIFT 24
#define BUSTER_TYPE_GC_MASK (3u << BUSTER_TYPE_GC_SHIFT)
#define BUSTER_TYPE_GC_WEAK (1u << BUSTER_TYPE_GC_SHIFT)
#define BUSTER_TYPE_GC_STRONG (2u << BUSTER_TYPE_GC_SHIFT)

// Flags. The first two are part of a derived type's key. Functions additionally use the record-only DEFINED bit for
// BUSTER_TYPE_FLAG_FUNCTION_INFO, which is part of their key; pointers retain a function's convention through their base
// TypeId, never through inherited flags.
#define BUSTER_TYPE_FLAG_UNSIGNED 0x01
#define BUSTER_TYPE_FLAG_VARIADIC 0x01
#define BUSTER_TYPE_FLAG_PROTOTYPE 0x02
#define BUSTER_TYPE_FLAG_KEY 0x03
// A VECTOR's key flags (#1447): clang's NEON vectors (neon_vector_type, neon_polyvector_type), a vector kind of their
// own, which spells and mangles otherwise but is compatible with GNU's of the same element and count, as clang's
// areCompatVectorTypes has it on x86-64.
#define BUSTER_TYPE_FLAG_NEON 0x01
#define BUSTER_TYPE_FLAG_NEON_POLY 0x02
// A complete object type: its size and alignment are known (a variable length array's size only at run time).
#define BUSTER_TYPE_FLAG_COMPLETE 0x04
#define BUSTER_TYPE_FLAG_VARIABLY_MODIFIED 0x08
// The type is or contains an ALIGNED type, so its canonical form (clang's, which has no typedefs) is another type.
#define BUSTER_TYPE_FLAG_ALIGNED_INSIDE 0x10
// A struct or union whose members are set, whether or not it has been laid out yet.
#define BUSTER_TYPE_FLAG_DEFINED 0x20
// A struct or union whose last member is a flexible array member.
#define BUSTER_TYPE_FLAG_FLEXIBLE 0x40
// A validated GNU transparent union. This nominal-record property does not make the union itself compatible with
// its members. The source owner retains activation history when an attribute is added after earlier uses.
#define BUSTER_TYPE_FLAG_TRANSPARENT_UNION 0x80
// A function whose calling convention isn't System V's, or whose parameters have ABIs (TypeParameterABI): its parameter
// range holds an info word after its parameters, the convention in its low byte and BUSTER_TYPE_FUNCTION_PARAMETER_ABIS,
// and with that bit a word of eight 4-bit parameter ABIs per eight parameters after it. A System V function without them,
// almost every function, has neither the bit nor the words, and is hashed, stored and compared as it was when the
// convention was three flag bits, all of whose values were taken (#1447).
#define BUSTER_TYPE_FLAG_FUNCTION_INFO 0x20
#define BUSTER_TYPE_FLAG_FUNCTION_KEY (BUSTER_TYPE_FLAG_KEY | BUSTER_TYPE_FLAG_FUNCTION_INFO)
#define BUSTER_TYPE_FUNCTION_ABI_MASK 0xffu
#define BUSTER_TYPE_FUNCTION_PARAMETER_ABIS 0x100u
// A prototype's cfi_salt (#1447, clang's FunctionProtoType CFI salt, part of its type's identity): an id of the salt's
// text (the checker's interned spelling) in the info word's bits from BUSTER_TYPE_FUNCTION_SALT_SHIFT, 0 for none. A
// TypeFunctionABI argument of type_function_full may carry it in the same bits.
#define BUSTER_TYPE_FUNCTION_SALT_SHIFT 9u
#define BUSTER_TYPE_FUNCTION_SALT_LIMIT (1u << (32u - BUSTER_TYPE_FUNCTION_SALT_SHIFT))
#define BUSTER_TYPE_PARAMETER_ABI_BITS 4
#define BUSTER_TYPE_PARAMETER_ABIS_PER_WORD 8

// Semantic function identity on the supported x86-64 target. This is not a claim that code generation implements
// every convention. The ordinary constructor and explicit sysv_abi/cdecl have the same default identity.
// no_caller_saved_registers is clang's function type bit (FunctionType::ExtInfo's NoCallerSavedRegs), beside the
// convention there; here it is a convention of its own with System V's, a System V function's whose callee keeps every
// register (#147), and TYPE_FUNCTION_ABI_KEEPS_REGISTERS beside another convention (#1447): `TYPE_FUNCTION_ABI_X |
// TYPE_FUNCTION_ABI_KEEPS_REGISTERS` is X's with the bit, a value type_function_abi_kind gives as it is
// (type_function_convention gives X, type_function_keeps_registers the bit).
// intel_ocl_bicc, swiftcall and swiftasynccall are clang's CC_IntelOclBicc, CC_Swift and CC_SwiftAsync (#1447).
typedef enum TypeFunctionABI
{
    TYPE_FUNCTION_ABI_SYSV,
    TYPE_FUNCTION_ABI_MICROSOFT,
    TYPE_FUNCTION_ABI_VECTOR,
    TYPE_FUNCTION_ABI_REG,
    TYPE_FUNCTION_ABI_PRESERVE_MOST,
    TYPE_FUNCTION_ABI_PRESERVE_ALL,
    TYPE_FUNCTION_ABI_PRESERVE_NONE,
    TYPE_FUNCTION_ABI_NO_CALLER_SAVED,
    TYPE_FUNCTION_ABI_INTEL_OCL,
    TYPE_FUNCTION_ABI_SWIFT,
    TYPE_FUNCTION_ABI_SWIFT_ASYNC,
    TYPE_FUNCTION_ABI_COUNT,
    TYPE_FUNCTION_ABI_INVALID = 255,
} TypeFunctionABI;
_Static_assert(TYPE_FUNCTION_ABI_COUNT <= BUSTER_TYPE_FUNCTION_ABI_MASK, "a function's ABI fits its info word's low byte");
#define TYPE_FUNCTION_ABI_KEEPS_REGISTERS 0x40u
_Static_assert(TYPE_FUNCTION_ABI_COUNT < TYPE_FUNCTION_ABI_KEEPS_REGISTERS, "no_caller_saved_registers' bit is past every convention");

// Whether `abi` (a TypeFunctionABI, with TYPE_FUNCTION_ABI_KEEPS_REGISTERS or not) is one a function type takes: a
// convention, or another convention than System V's (and no_caller_saved_registers' own) with the bit.
static inline bool type_function_abi_valid(uint32_t abi)
{
    const uint32_t base = abi & ~TYPE_FUNCTION_ABI_KEEPS_REGISTERS;
    return base < TYPE_FUNCTION_ABI_COUNT &&
           ((abi & TYPE_FUNCTION_ABI_KEEPS_REGISTERS) == 0 || (base != TYPE_FUNCTION_ABI_SYSV && base != TYPE_FUNCTION_ABI_NO_CALLER_SAVED));
}

// A parameter's ABI, clang's ParameterABI (FunctionProtoType::ExtParameterInfo), part of its function's type: the Swift
// parameter attributes swift_indirect_result, swift_error_result, swift_context and swift_async_context.
typedef enum TypeParameterABI
{
    TYPE_PARAMETER_ABI_ORDINARY,
    TYPE_PARAMETER_ABI_SWIFT_INDIRECT_RESULT,
    TYPE_PARAMETER_ABI_SWIFT_ERROR_RESULT,
    TYPE_PARAMETER_ABI_SWIFT_CONTEXT,
    TYPE_PARAMETER_ABI_SWIFT_ASYNC_CONTEXT,
    TYPE_PARAMETER_ABI_COUNT,
} TypeParameterABI;
_Static_assert(TYPE_PARAMETER_ABI_COUNT <= 1u << BUSTER_TYPE_PARAMETER_ABI_BITS, "a parameter's ABI fits its bits");

// A member's flags.
#define BUSTER_TYPE_MEMBER_PACKED 0x01
#define BUSTER_TYPE_MEMBER_BIT_FIELD 0x02
// clang's counted_by family on a member (#148), its kind in the flags' bits 2 to 4 as TypeMember.count_kind has it, and
// the count's member's name in TypeMembers.count.
#define BUSTER_TYPE_MEMBER_COUNT_SHIFT 2
#define BUSTER_TYPE_MEMBER_COUNT_MASK 0x1C
// TypeMember.count_kind: none, or clang's CountAttributedType kinds plus one.
#define BUSTER_TYPE_COUNT_NONE 0
#define BUSTER_TYPE_COUNTED_BY 1
#define BUSTER_TYPE_SIZED_BY 2
#define BUSTER_TYPE_COUNTED_BY_OR_NULL 3
#define BUSTER_TYPE_SIZED_BY_OR_NULL 4
// TypeMember.bit_width of a member that isn't a bit-field.
#define BUSTER_TYPE_NOT_BIT_FIELD UINT32_MAX

// The member lookup (TypeLookup): a record of at most BUSTER_TYPE_LOOKUP_SCAN members, none of them an anonymous struct or
// union, is scanned with one compare; any other has an index of buckets of BUSTER_TYPE_LOOKUP_SLOTS names, or, where its
// index doesn't fit, TypeTags.lookup BUSTER_TYPE_LOOKUP_WALK. type_lookup_member's answer BUSTER_TYPE_MEMBER_UNKNOWN, which
// no member's index is, says that a walk ran out of scratch memory.
#define BUSTER_TYPE_LOOKUP_SCAN 16
#define BUSTER_TYPE_LOOKUP_SLOTS 8
#define BUSTER_TYPE_LOOKUP_WALK UINT32_MAX
#define BUSTER_TYPE_MEMBER_UNKNOWN UINT32_MAX

// The limits: clang's array limit (2^61 bytes), _BitInt's widths, and alignments up to 2^32 bytes.
#define BUSTER_TYPE_MAX_OBJECT_BYTES ((1ull << 61) - 1)
#define BUSTER_TYPE_MAX_BIT_INT_WIDTH 8388608u
#define BUSTER_TYPE_MAX_ALIGNMENT_LOG2 32
// A vector's largest width after rounding, 2^31 bits: past it clang's 32-bit alignment arithmetic overflows (a vector
// of 2^32 - 1 chars gets alignment 0), so the table refuses such vectors where clang gives them nonsense.
#define BUSTER_TYPE_MAX_VECTOR_BITS (1ull << 31)
// The aligned attribute without an argument, on x86-64 (DefaultAlignForAttributeAligned): 16 bytes.
#define BUSTER_TYPE_DEFAULT_ALIGNED_LOG2 4

typedef enum TypeError
{
    TYPE_ERROR_NONE,
    // A column is at its capacity, the index can't grow, or (for type_composite) the scratch memory can't hold a walk.
    TYPE_ERROR_FULL,
    // An operand isn't a type the table has, or a request's kind or flags aren't ones it takes.
    TYPE_ERROR_OPERAND,
    // An array's element isn't a complete object type.
    TYPE_ERROR_ELEMENT,
    // An array's element's size isn't a multiple of its alignment (an aligned typedef raised it).
    TYPE_ERROR_ELEMENT_ALIGNMENT,
    // An object larger than BUSTER_TYPE_MAX_OBJECT_BYTES, or a vector wider than BUSTER_TYPE_MAX_VECTOR_BITS.
    TYPE_ERROR_TOO_LARGE,
    // A function returns an array, a function or __fp16.
    TYPE_ERROR_RETURN,
    // A parameter is void, an array or a function (callers adjust them), __fp16, or without a prototype.
    TYPE_ERROR_PARAMETER,
    // _Atomic of a qualified, array, function, atomic, incomplete or _BitInt type.
    TYPE_ERROR_ATOMIC,
    // A vector's element isn't one clang takes, or its count is 0 or doesn't divide its size.
    TYPE_ERROR_VECTOR,
    // A _BitInt's width is out of range: 1 (unsigned) or 2 (signed) to BUSTER_TYPE_MAX_BIT_INT_WIDTH.
    TYPE_ERROR_BIT_INT,
    // _Complex of a type that can't be its element.
    TYPE_ERROR_COMPLEX,
    // An alignment past BUSTER_TYPE_MAX_ALIGNMENT_LOG2, or an aligned function type.
    TYPE_ERROR_ALIGNMENT,
    // Not a tag of the right kind, or one already defined or completed.
    TYPE_ERROR_TAG,
    // A member's type is incomplete, a function, void or variably modified, or a flexible array member isn't last.
    TYPE_ERROR_MEMBER,
    // A bit-field's type isn't an integer type, its width is past its type's, or a named one has width 0.
    TYPE_ERROR_BIT_FIELD,
    // An enumeration's underlying type isn't a (non-_BitInt) integer type.
    TYPE_ERROR_UNDERLYING,
    // An unknown GNU machine mode, including a malformed vector-mode count.
    TYPE_ERROR_MODE,
    // A machine mode and the declaration's base type have incompatible categories.
    TYPE_ERROR_MODE_BASE,
    // A recognized mode has no target type, or requires a type this table cannot represent.
    TYPE_ERROR_MODE_UNSUPPORTED,
    // Microsoft bit-field storage whose byte size is not a power of two needs an unrepresentable alignment.
    TYPE_ERROR_MS_STRUCT_ALIGNMENT,
} TypeError;

// Per type, by index. What base, index and extent hold depends on the kind:
//   - POINTER: base is the pointee, with its qualifiers.
//   - ARRAY, INCOMPLETE_ARRAY, VARIABLE_ARRAY: base is the element without qualifiers, which go in the array's TypeId
//     instead, as clang's canonical types hoist them (a const int[3] and an array of const int are one TypeId).
//     extent is an ARRAY's length, and a VARIABLE_ARRAY's size expression (the caller's id for it; 0 is [*]).
//   - FUNCTION: base is the return type, with its qualifiers (clang keeps them: int (void) and const int (void) are
//     incompatible); index is the first parameter in TypeParameters and extent their count. Parameters are adjusted
//     and unqualified, as C 6.7.6.3p15 and clang's canonical function types have them.
//   - VECTOR, EXT_VECTOR: base is the element, with its qualifiers (clang keeps them too); extent is the count.
//   - ATOMIC: base is the value type. ALIGNED: base is the type it aligns, unqualified and never ALIGNED, and extent the
//     alignment's log2. COMPLEX: base is the element. BIT_INT: extent is the width.
//   - STRUCT, UNION, ENUM: index is the tag (TypeTags); an ENUM's base is its underlying type once it is complete.
//   - ALIGNED, QUALIFIED: index is the index under every wrapper (type_unwrapped_index).
// size is in bytes: 1 for void and functions, as GNU's sizeof has it, and 0 for incomplete types and variable length
// arrays. alignment_log2 is in bytes too. An ALIGNED type's size, code and completeness are its base's, which
// type_size, type_arithmetic_code and type_is_complete read, since its base may be a tag completed later. hash is a
// derived type's key's, for growing the index.
typedef struct TypeColumns TypeColumns;
struct TypeColumns
{
    uint8_t* kind;
    uint8_t* code;
    uint8_t* flags;
    uint8_t* alignment_log2;
    TypeId* base;
    uint32_t* index;
    uint64_t* extent;
    uint64_t* size;
    uint64_t* hash;
    uint32_t count;
    uint32_t capacity;
};

// Functions' parameters, each function's a range.
typedef struct TypeParameters TypeParameters;
struct TypeParameters
{
    TypeId* type;
    uint32_t count;
    uint32_t capacity;
};

// Per tag (struct, union or enum), by tag index. A tag is one per declaration: never hash-consed. completion is the
// order in which tags were completed, from 1 (0 while incomplete), which #113's deferred bodies compare. packed,
// ms_struct, pack_log2 and alignment_log2 are layout inputs (an enum's alignment too); the last two hold log2 + 1, 0 for
// none. lookup is how a struct's or union's members are found by name (TypeLookup): 0 for a scan of its members,
// BUSTER_TYPE_LOOKUP_WALK for a walk, and otherwise its index's first bucket, of 2^lookup_log2. Record 0 is unused.
typedef struct TypeTags TypeTags;
struct TypeTags
{
    TypeId* type;
    uint32_t* name;
    uint32_t* member_begin;
    uint32_t* member_count;
    uint32_t* completion;
    uint8_t* packed;
    uint8_t* ms_struct;
    uint8_t* pack_log2;
    uint8_t* alignment_log2;
    uint32_t* lookup;
    uint8_t* lookup_log2;
    uint32_t count;
    uint32_t capacity;
    // How many tags have been completed.
    uint32_t completions;
};

// Per member of a struct or union, each record's a range. name is an identifier id (#116's IdentId once #192 lands; 0
// for an unnamed bit-field and an anonymous struct or union), alignment_log2 its explicit alignment (the largest of its
// _Alignas and aligned attributes) as log2 + 1 or 0, and offset its offset in bits, which layout writes. Record 0 is
// unused, so that 0 is no member (type_find_member's answer when there is none).
typedef struct TypeMembers TypeMembers;
struct TypeMembers
{
    uint32_t* name;
    TypeId* type;
    uint32_t* bit_width;
    uint8_t* alignment_log2;
    uint8_t* flags;
    uint64_t* offset;
    // The name of the member a counted_by family attribute names (#148), or 0.
    uint32_t* count_name;
    uint32_t count;
    uint32_t capacity;
};

// The member lookup's index (#591): what a name finds in a struct or union, without a pass over its members. Each record
// that isn't scanned (BUSTER_TYPE_LOOKUP_SCAN) has a hash table of every name its lookup finds, through its anonymous
// members too, each with the first of its own members in declaration order that finds it: the named member, or the
// anonymous member that holds the name, whose record's lookup goes on from there. type_record_define builds it, half
// full at most, its anonymous members' names from their records' lookups, so a lookup takes a probe per anonymous member
// it goes through, and never visits a record twice, however often records hold one another. A bucket is a cache line:
// BUSTER_TYPE_LOOKUP_SLOTS names (0 in an empty slot), compared at once, and their members; a probe goes from the name's
// bucket on until it finds the name or an empty slot. Bucket 0 is unused, so that TypeTags.lookup 0 is a scan.
typedef struct TypeLookupBucket TypeLookupBucket;
struct TypeLookupBucket
{
    uint32_t names[BUSTER_TYPE_LOOKUP_SLOTS];
    uint32_t members[BUSTER_TYPE_LOOKUP_SLOTS];
};

typedef struct TypeLookup TypeLookup;
struct TypeLookup
{
    TypeLookupBucket* buckets;
    uint32_t count;
    uint32_t capacity;
};

// A slot of the hash-consing index: a derived type's key as far as 12 bytes hold it, and its index (0 in an empty
// slot). key is the kind, the key flags, a WIDE bit for an extent of 2^32 or more, and 16 bits of the key's hash.
typedef struct TypeSlot TypeSlot;
struct TypeSlot
{
    TypeId base;
    uint32_t extent;
    uint32_t key;
    uint32_t index;
};

// Open addressing over buckets of four slots, one cache line each, probed bucket after bucket from the hash's. Its
// memory is index_arena's alone: growing pops it and pushes one twice as large, rebuilt from the columns.
typedef struct TypeIndex TypeIndex;
struct TypeIndex
{
    Arena* arena;
    TypeSlot* slots;
    uint32_t bucket_count;
    // How many types the index holds.
    uint32_t count;
};

typedef struct TypeChanges TypeChanges;
typedef struct TypeTable TypeTable;
struct TypeTable
{
    TypeColumns types;
    TypeParameters parameters;
    TypeTags tags;
    TypeMembers members;
    TypeLookup lookup;
    TypeIndex index;
    // Why the last request that failed did, for its diagnostic.
    TypeError last_error;
    // #113: where the table records its changes, or null; and the types a view hides, the indices from hidden_begin
    // to hidden_end (excluded), which lookups never find, so that a request for one makes the type again.
    TypeChanges* changes;
    uint32_t hidden_begin;
    uint32_t hidden_end;
    // Whether plain char is unsigned (-funsigned-char): the source reader sets it from the unit's options
    // (check_input_run), and type_integer_properties answers it. Signed, x86-64's, when false.
    bool char_unsigned;
    // Whether the target inlines atomics of 16 bytes (CMPXCHG16B, cx16: clang's MaxAtomicInlineWidth of 128 bits rather
    // than 64), which the lock-free queries answer by (#148): the source reader sets it from the unit's target, as it sets
    // char_unsigned. 8 bytes, x86-64's baseline, when false.
    bool atomic_inline_16;
    // Whether the table has a QUALIFIED record (#1447), set when it makes its first and never cleared: a unit without one
    // reads no type's extended qualifiers (type_extended_qualifiers, type_object_qualifiers).
    bool extended;
    // Whether the table has an extended vector (ext_vector_type), set when it makes its first and never cleared: only
    // such a unit's source reader decodes a `->` member's name as a swizzle (check_input_vector_member), since `p->y`
    // can name one only through a pointer to an extended vector.
    bool ext_vectors;
    // Whether an atomic struct/union has been made, monotonic like ext_vectors: only such a unit needs the
    // -Watomic-access member candidates in the common runtime-warning walk.
    bool atomic_records;
};

// How many records of each table there is room for: types besides the builtin ones, tags and members besides record 0,
// and the member lookup's buckets besides bucket 0 (TypeLookup), where 0 asks for one a member: more than a unit's records
// take unless their anonymous members nest deeply, past which their lookups walk.
typedef struct TypeCapacity TypeCapacity;
struct TypeCapacity
{
    uint32_t types;
    uint32_t parameters;
    uint32_t tags;
    uint32_t members;
    uint32_t lookups;
};

// A member as type_record_define takes it.
typedef struct TypeMember TypeMember;
struct TypeMember
{
    uint32_t name;
    TypeId type;
    // BUSTER_TYPE_NOT_BIT_FIELD if it isn't one.
    uint32_t bit_width;
    // The largest of its _Alignas and aligned attributes, as log2 + 1; 0 for none.
    uint8_t alignment_log2;
    // __attribute__((packed)) on the member.
    bool packed;
    // A counted_by family attribute's kind (BUSTER_TYPE_COUNTED_BY and the others, #148) and the name of its count's
    // member; BUSTER_TYPE_COUNT_NONE and 0 for none.
    uint8_t count_kind;
    uint32_t count_name;
};

// A struct's or union's layout attributes: GNU's packed, #pragma pack's value (#124 gives it), the aligned attribute
// (the last two as log2 + 1 or 0 for none), and Microsoft's bit-field storage-unit rules.
typedef struct TypeRecordAttributes TypeRecordAttributes;
struct TypeRecordAttributes
{
    bool packed;
    uint8_t pack_log2;
    uint8_t alignment_log2;
    bool ms_struct;
};

// #113's deferred bodies check each function body apart from the top level, and the top level without its bodies,
// each with a table of its own: the top level's, and for a body a copy of it (type_copy) that hides the types the top
// level made after the body (TypeTable.hidden_begin to hidden_end). Each table records its changes (TypeChanges), with
// the source position its caller is at (type_changes_at) and their order there, and type_replay merges the records by
// position into one table: the one that checking in source order makes, with the same types in the same order.
//
// A change is what a mutating call did: DERIVE a derived type (type_find_or_add's request, normalized already, so that
// the replay normalizes nothing), TAG a tag (type_tag), DEFINE a record's members (type_record_define), LAYOUT a
// record's layout and completion (type_finish_record), ENUM an enum's completion (type_enum_complete) and TRANSPARENT a
// union made transparent. A request that fails changes nothing but last_error, of which a record keeps the last write.
#define BUSTER_TYPE_CHANGE_DERIVE 1
#define BUSTER_TYPE_CHANGE_TAG 2
#define BUSTER_TYPE_CHANGE_DEFINE 3
#define BUSTER_TYPE_CHANGE_LAYOUT 4
#define BUSTER_TYPE_CHANGE_ENUM 5
#define BUSTER_TYPE_CHANGE_TRANSPARENT 6
// The largest mark of a predefined type (TypeChanges.predefined) a replay keeps apart.
#define BUSTER_TYPE_PREDEFINED_MARKS 32

typedef struct TypeChange TypeChange;
struct TypeChange
{
    // The key: the caller's source position, and the change's order among the record's changes there.
    uint32_t position;
    uint32_t sequence;
    uint8_t operation;
    // DERIVE: the request's kind and flags; TAG: the tag's kind; ENUM: the aligned attribute (log2 + 1, or 0).
    uint8_t kind;
    uint8_t flags;
    uint8_t alignment_log2;
    // DERIVE: the request's base; ENUM: the underlying type.
    TypeId base;
    // DERIVE: the request's extent (a FUNCTION's parameter count).
    uint64_t extent;
    // A FUNCTION's DERIVE: its first parameter in TypeChanges.parameters. DEFINE: its first member in
    // TypeChanges.members, and how many.
    uint32_t first;
    uint32_t count;
    // The type made (DERIVE, TAG) or changed (DEFINE, LAYOUT, ENUM, TRANSPARENT), in the recording table's ids.
    TypeId type;
    // TAG: the tag's name, and its mark (TypeChanges.predefined), 0 for none.
    uint32_t name;
    uint32_t predefined;
    // TAG, DEFINE, LAYOUT, ENUM and TRANSPARENT: the tag's index in the recording table.
    uint32_t tag;
    // DEFINE: the record's layout attributes.
    TypeRecordAttributes attributes;
};

// A table's record of its changes, in arrays of the caller's with their capacities (type_changes_initialize).
struct TypeChanges
{
    TypeChange* items;
    uint32_t count;
    uint32_t capacity;
    TypeId* parameters;
    uint32_t parameter_count;
    uint32_t parameter_capacity;
    TypeMember* members;
    uint32_t member_count;
    uint32_t member_capacity;
    // The key of the next change: the caller's position (type_changes_at), and the next order there.
    uint32_t position;
    uint32_t sequence;
    // The mark type_tag gives the tags it makes, which the caller sets around its predefined types' construction (0
    // for none): a replay keeps the first tag of each mark, by key, and takes every later one's changes for its.
    uint32_t predefined;
    // The last write of last_error, with its key; error_written is false while there has been none.
    bool error_written;
    uint32_t error_position;
    uint32_t error_sequence;
    TypeError error;
    // A change didn't fit: the record is incomplete, and a replay refuses it.
    bool full;
};

typedef enum TypeTransparentUnionStatus
{
    TYPE_TRANSPARENT_UNION_APPLIED,
    TYPE_TRANSPARENT_UNION_NOT_UNION,
    TYPE_TRANSPARENT_UNION_INCOMPLETE,
    TYPE_TRANSPARENT_UNION_EMPTY,
    TYPE_TRANSPARENT_UNION_FIRST_FLOAT,
    TYPE_TRANSPARENT_UNION_FIRST_VECTOR,
    TYPE_TRANSPARENT_UNION_SIZE,
    TYPE_TRANSPARENT_UNION_ALIGNMENT,
    // A field of incomplete type (a GNU flexible array member), which clang's handler leaves without a word.
    TYPE_TRANSPARENT_UNION_INCOMPLETE_FIELD,
} TypeTransparentUnionStatus;

typedef struct TypeTransparentUnionResult TypeTransparentUnionResult;
struct TypeTransparentUnionResult
{
    TypeTransparentUnionStatus status;
    // Zero-based field index for a first-field/size/alignment failure; UINT32_MAX for a subject failure.
    uint32_t member;
};

// type_derive_batch's requests, as columns: request i asks for kinds[i] of bases[i] with extents[i] and flags[i], as
// the one-at-a-time constructors take them (type_pointer's kind POINTER, type_array's ARRAY with the length, and so
// on). Every kind but FUNCTION and the tags.
typedef struct TypeKeys TypeKeys;
struct TypeKeys
{
    const uint8_t* kinds;
    const uint8_t* flags;
    const TypeId* bases;
    const uint64_t* extents;
};

// Parentheses belonging to one declarator occurrence, outside the type at this level of its base chain. Canonical
// TypeIds keep no syntax. A chain has no repeated type, so its type identifies a level without affecting parameters
// or another occurrence of that type. A caller expands typedef sugar by omitting this view too.
typedef struct TypePrintParen TypePrintParen;
struct TypePrintParen
{
    TypeId type;
    uint32_t count;
};

// The spellings of identifier ids, for the printer: id i's bytes are bytes[i], lengths[i] of them, for i below count.
typedef struct TypeNames TypeNames;
struct TypeNames
{
    const uint8_t* const* bytes;
    const uint32_t* lengths;
    uint32_t count;
    // Diagnostic spelling sugar (#992): an alias prints `alias_names[i]` where a specifier level's unqualified
    // type is `alias_types[i]` (a typedef's own type in a declaration's sugar, or a builtin one like
    // `__size_t`), and `anonymous` gives an unnamed tag's printed name by tag index — the first typedef naming
    // it or `(unnamed at file:line:col)` — else `(unnamed)`.
    const TypeId* alias_types;
    const String8* alias_names;
    uint32_t alias_count;
    // Optional, per alias: the qualifiers its name covers where it matches a level unqualified, which then print only
    // the level's others (__builtin_dump_struct's `volatile CI` of a const int's CI, #148); null prints them all.
    const uint32_t* alias_covered;
    // Optional, per alias: the qualifiers its use writes again beside it (`const CI` of a const int's CI, #1447), which
    // print before its name, as clang's qualifiers over a typedef's sugar do, where the level has them.
    const uint32_t* alias_written;
    // Optional: the aliases by place, as a declaration spells its types (__builtin_dump_struct's, #148): entry k the alias
    // (UINT32_MAX for none) of the k-th specifier the printer prints, a typedef's or a type's own, in the order a C
    // declaration writes them (the outer specifiers, then each nested parameter's). A level then takes no alias but its
    // place's, so a typedef names only the occurrence it spells (`int (*)(I)`). The printer writes the specifiers it
    // printed to `alias_order_printed`, which its caller compares with its count.
    const uint32_t* alias_order;
    uint32_t alias_order_count;
    uint32_t* alias_order_printed;
    const String8* anonymous;
    uint32_t anonymous_count;
    // Whether a typedef's name of an anonymous tag (an `anonymous` row not in parentheses) prints after its keyword, as
    // clang's a.k.a. of a typedef whose declaration spells the tag's definition does (`'SP' (aka 'struct S *')`).
    bool anonymous_keyword;
    const TypePrintParen* parentheses;
    uint32_t parenthesis_count;
    // Optional (#1447): the macro that writes a target address space, as clang's MacroQualifiedType names it
    // (`'AS1 int *'`): a level whose space is `space_macro_spaces[i]` (its BUSTER_TYPE_SPACE_TARGET-based word) prints
    // `space_macro_texts[i]` before the level, with its other qualifiers after the level's specifier.
    const uint32_t* space_macro_spaces;
    const String8* space_macro_texts;
    uint32_t space_macro_count;
    // Optional (#1447): a variable array's size expression as written, which the table doesn't keep: a VARIABLE_ARRAY
    // level whose extent (the caller's id for its expression) is `bound_ids[i]` prints `[bound_texts[i]]`, as clang's
    // `int[n]`; any other prints `[]`.
    const uint64_t* bound_ids;
    const String8* bound_texts;
    uint32_t bound_count;
    // Optional (#1447): the macros of type sugar's MACRO nodes (c_types_sugar.c), by the attribute list's first token,
    // which only the caller's expansion history resolves (`'MS void (*)(void)'`).
    const uint32_t* sugar_macro_tokens;
    const String8* sugar_macro_texts;
    uint32_t sugar_macro_count;
};

// The printer's options: BUSTER_TYPE_PRINT_C23 spells bool as C23 and clang in C23 mode do (bool, not _Bool), and
// BUSTER_TYPE_PRINT_NO_TAG_KEYWORD prints a tag's name without its keyword, as clang's SuppressTagKeyword does (an
// unnamed tag's row, `(unnamed struct at file:line:col)`, then carries its kind itself).
#define BUSTER_TYPE_PRINT_C23 0x01
#define BUSTER_TYPE_PRINT_NO_TAG_KEYWORD 0x02
// BUSTER_TYPE_PRINT_CANONICAL_CC spells a calling convention as clang's canonical function type does (printFunctionAfter,
// #1447): an a.k.a.'s or a macro's (`intel_ocl_bicc`, and swiftasynccall without its space), where the attribute a
// declaration wrote prints by its AttributedType (`inteloclbicc`).
#define BUSTER_TYPE_PRINT_CANONICAL_CC 0x04
// BUSTER_TYPE_PRINT_INSIDE_CC prints no function's calling convention, as clang's TypePrinter doesn't inside a convention's
// AttributedType (InsideCCAttribute), which spells it itself (type_sugar_print). no_caller_saved_registers' bit still prints.
#define BUSTER_TYPE_PRINT_INSIDE_CC 0x08

// At most how many bytes type_initialize pushes from its arena for `capacity`, and how many the index arena needs to
// grow to the largest index those types can use.
static __attribute__((unused)) uint64_t type_capacity_bytes(TypeCapacity capacity);
static __attribute__((unused)) uint64_t type_index_capacity_bytes(TypeCapacity capacity);
// Pushes every column from `arena`, with room for `capacity`, fills in the builtin types and starts the index in
// `index_arena`, which only the table may use from then on. Returns false, with every table without room and
// last_error TYPE_ERROR_FULL, if the arenas can't hold them or a capacity is too large for 32-bit ids; the arenas are
// then as they were.
static __attribute__((unused)) bool type_initialize(TypeTable* table, Arena* arena, Arena* index_arena, TypeCapacity capacity);

// What a type is. A TypeId the table hasn't given is invalid input to everything but type_is_valid.
static __attribute__((unused)) bool type_is_valid(const TypeTable* table, TypeId type);
static __attribute__((unused)) uint32_t type_index(TypeId type);
static __attribute__((unused)) uint32_t type_qualifiers(TypeId type);
static __attribute__((unused)) TypeId type_unqualified(TypeId type);
static __attribute__((unused)) uint8_t type_kind(const TypeTable* table, TypeId type);
// `type` with `qualifiers` added, but none on a function type, whose qualifiers clang drops (C gives them no meaning).
static __attribute__((unused)) TypeId type_qualify(const TypeTable* table, TypeId type, uint32_t qualifiers);
// Size in bytes and alignment in bytes and as log2 (see TypeColumns for void, functions and incomplete types).
static __attribute__((unused)) uint64_t type_size(const TypeTable* table, TypeId type);
static __attribute__((unused)) uint64_t type_alignment(const TypeTable* table, TypeId type);
static __attribute__((unused)) uint32_t type_alignment_log2(const TypeTable* table, TypeId type);
static __attribute__((unused)) bool type_is_complete(const TypeTable* table, TypeId type);
static __attribute__((unused)) uint8_t type_arithmetic_code(const TypeTable* table, TypeId type);
// An array's or a vector's element, with the array's qualifiers; a complex type's element.
static __attribute__((unused)) TypeId type_element(const TypeTable* table, TypeId type);
// A function's parameters (count of them) and return type.
static __attribute__((unused)) const TypeId* type_parameters(const TypeTable* table, TypeId function, uint32_t* count);
static __attribute__((unused)) TypeId type_return(const TypeTable* table, TypeId function);
// A function's semantic calling convention; INVALID for a non-function or invalid TypeId.
static __attribute__((unused)) TypeFunctionABI type_function_abi_kind(const TypeTable* table, TypeId function);
static __attribute__((unused)) TypeFunctionABI type_function_convention(const TypeTable* table, TypeId function);
static __attribute__((unused)) bool type_function_keeps_registers(const TypeTable* table, TypeId function);
// A function's parameter's ABI (ORDINARY past its parameters); whether any of them isn't ORDINARY.
static __attribute__((unused)) TypeParameterABI type_parameter_abi(const TypeTable* table, TypeId function, uint32_t parameter);
static __attribute__((unused)) bool type_function_has_parameter_abis(const TypeTable* table, TypeId function);
static __attribute__((unused)) uint32_t type_function_salt(const TypeTable* table, TypeId function);
static __attribute__((unused)) uint32_t type_function_extended_abi(const TypeTable* table, TypeId function);
// The type without ALIGNED and QUALIFIED at its top: its base, with the type's qualifiers. The extended qualifiers it
// drops are type_extended_qualifiers'.
static __attribute__((unused)) TypeId type_strip_aligned(const TypeTable* table, TypeId type);
// The type without ALIGNED at its top, but with its extended qualifiers: what a typedef's name may name.
static __attribute__((unused)) TypeId type_strip_alignment(const TypeTable* table, TypeId type);
// A type's extended qualifiers (#1447): its QUALIFIED word, under an ALIGNED; 0 for none.
static __attribute__((unused)) uint32_t type_extended_qualifiers(const TypeTable* table, TypeId type);
// The same of an array's innermost element, which takes an array's (clang's getAddressSpace of an array type).
static __attribute__((unused)) uint32_t type_object_qualifiers(const TypeTable* table, TypeId type);
// The address space of a word, and whether a pointer to `to` may point to `from`'s (clang's isAddressSpaceSupersetOf,
// as C on x86-64 has it).
static __attribute__((unused)) uint32_t type_address_space(uint32_t word);
static __attribute__((unused)) bool type_space_includes(uint32_t to, uint32_t from);

// The derived types, hash-consed: the same request gets the same TypeId, and so does an equal structure built in
// another order. Each returns 0, with last_error saying why, if the request breaks one of clang's rules (TypeError)
// or the table is full. The batch gives each request what the one-at-a-time constructors would, in request order.
static __attribute__((unused)) TypeId type_pointer(TypeTable* table, TypeId pointee);
// The pointer to `pointee` where the table has it already (a diagnostic's reader names a decayed array's), else 0.
static __attribute__((unused)) TypeId type_find_pointer(const TypeTable* table, TypeId pointee);
static __attribute__((unused)) TypeId type_array(TypeTable* table, TypeId element, uint64_t length);
static __attribute__((unused)) TypeId type_incomplete_array(TypeTable* table, TypeId element);
// expression is the caller's id for the size expression; 0 is [*].
static __attribute__((unused)) TypeId type_variable_array(TypeTable* table, TypeId element, uint32_t expression);
// flags: BUSTER_TYPE_FLAG_PROTOTYPE and BUSTER_TYPE_FLAG_VARIADIC. Parameters must be adjusted (type_adjust_parameter);
// their qualifiers are dropped.
static __attribute__((unused)) TypeId type_function(TypeTable* table, TypeId result, const TypeId* parameters, uint32_t count, uint8_t flags);
// As type_function, with an explicit calling convention. Vectorcall and regcall cannot be variadic.
static __attribute__((unused)) TypeId type_function_abi(TypeTable* table, TypeId result, const TypeId* parameters, uint32_t count, uint8_t flags, TypeFunctionABI abi);
// As type_function_abi, with each parameter's ABI (TypeParameterABI, count of them), or none for null.
static __attribute__((unused)) TypeId type_function_full(TypeTable* table, TypeId result, const TypeId* parameters, uint32_t count, uint8_t flags, TypeFunctionABI abi,
                                                         const uint8_t* parameter_abis);
// As type_function_abi, with `model`'s parameter ABIs: for a function type made again from one, with other parameters
// of the same count, another return type or another convention.
static __attribute__((unused)) TypeId type_function_like(TypeTable* table, TypeId result, const TypeId* parameters, uint32_t count, uint8_t flags, TypeFunctionABI abi,
                                                         TypeId model);
// A vector of `count` elements; ext chooses clang's ext_vector_type over GNU's vector_size (type_vector_size converts
// the latter's size in bytes).
static __attribute__((unused)) TypeId type_vector(TypeTable* table, TypeId element, uint32_t count, bool ext);
// A GNU vector of `count` elements with key flags `kind`: 0, BUSTER_TYPE_FLAG_NEON or BUSTER_TYPE_FLAG_NEON_POLY (#1447).
static __attribute__((unused)) TypeId type_vector_kind(TypeTable* table, TypeId element, uint32_t count, uint8_t kind);
static __attribute__((unused)) TypeId type_vector_size(TypeTable* table, TypeId element, uint64_t bytes);
// GNU mode replaces a declaration's type, including its existing qualifiers and aligned typedef wrappers, as
// clang does. This is NOT a qualifier-preserving derivation; the checker owns any later declaration qualifiers.
// An enum typedef becomes an integer without changing its tag. For a mode on an enum tag itself, the caller passes
// its underlying type (int before completion) and supplies the result to type_enum_complete. mode is a bounded,
// non-NUL-terminated identifier, with either plain or paired-underscore spelling. No source diagnostics are printed.
static __attribute__((unused)) TypeId type_machine_mode(TypeTable* table, TypeId base, String8 mode);
static __attribute__((unused)) TypeId type_atomic(TypeTable* table, TypeId value);
static __attribute__((unused)) TypeId type_aligned(TypeTable* table, TypeId base, uint32_t alignment_log2);
// `type` with extended qualifiers `word` (BUSTER_TYPE_SPACE_*, BUSTER_TYPE_GC_*): the fields word sets replace the
// type's, an array's element takes them, and an ALIGNED type keeps its alignment over them. A word of 0 is the type.
// A function type takes none (TYPE_ERROR_OPERAND): clang's "function type may not be qualified" is its callers'.
static __attribute__((unused)) TypeId type_extend(TypeTable* table, TypeId type, uint32_t word);
// `type` without its extended qualifiers (the fields `mask` names), an array's element's too.
static __attribute__((unused)) TypeId type_unextend(TypeTable* table, TypeId type, uint32_t mask);
static __attribute__((unused)) TypeId type_bit_int(TypeTable* table, uint32_t width, bool is_unsigned);
// _Complex of a builtin element is a builtin type; of a _BitInt, a derived one. It takes every element the usual
// arithmetic conversions make: the front end refuses the declarations clang refuses (BUSTER_TYPE_COMPLEX_SPELLABLE).
static __attribute__((unused)) TypeId type_complex(TypeTable* table, TypeId element);
static __attribute__((unused)) void type_derive_batch(TypeTable* table, TypeKeys keys, uint32_t count, TypeId* results);
// A parameter's type as C adjusts it: an array to a pointer to its element, a function to a pointer to it, and without
// qualifiers.
static __attribute__((unused)) TypeId type_adjust_parameter(TypeTable* table, TypeId type);

// Tags: a new incomplete struct, union or enum each call. Its members come with type_record_define and its layout with
// type_record_layout (one record, the scalar layout) or type_layout_records (many, 16 at a time in vectors);
// type_complete_record does both. Every member's type must be complete (callers complete records innermost first).
static __attribute__((unused)) TypeId type_tag(TypeTable* table, uint8_t kind, uint32_t name);
static __attribute__((unused)) bool type_record_define(TypeTable* table, TypeId record, const TypeMember* members, uint32_t count, TypeRecordAttributes attributes);
static __attribute__((unused)) bool type_record_layout(TypeTable* table, TypeId record);
// Lays out every record (each defined and not yet laid out) and completes them in array order. Returns false if one
// fails; the others are still laid out.
static __attribute__((unused)) bool type_layout_records(TypeTable* table, const TypeId* records, uint32_t count);
static __attribute__((unused)) bool type_complete_record(TypeTable* table, TypeId record, const TypeMember* members, uint32_t count, TypeRecordAttributes attributes);
// A complete union with at least one member can be transparent when its first member is neither a floating or
// complex-floating type nor a vector, and every member type has that first type's size and alignment. Member-level
// packed/aligned/bit-field placement does not change this check. Invalid requests leave the record and last_error
// unchanged: the source attribute consumer retains Clang's warning/ignored disposition and exact failing field.
static __attribute__((unused)) TypeTransparentUnionResult type_mark_transparent_union(TypeTable* table, TypeId record);
static __attribute__((unused)) bool type_is_transparent_union(const TypeTable* table, TypeId type);
// Completes an enum with its underlying type (fixed, or from type_enum_underlying) and its aligned attribute (log2 + 1,
// or 0), which may lower its alignment as clang's does.
static __attribute__((unused)) bool type_enum_complete(TypeTable* table, TypeId enumeration, TypeId underlying, uint8_t alignment_log2);
// The underlying type clang chooses for an enum without a fixed one, from its enumerators' values, which
// type_enum_value_bits folds into the bits counts one value at a time (both start at 0).
static __attribute__((unused)) void type_enum_value_bits(int64_t value, bool is_unsigned, uint32_t* negative_bits, uint32_t* positive_bits);
static __attribute__((unused)) TypeId type_enum_underlying(bool packed, uint32_t negative_bits, uint32_t positive_bits);
// A member of a struct or union by name, looking into anonymous members as C does, and its offset in bits from the
// record's start. Returns the member's index in TypeMembers, or 0 if there is none.
static __attribute__((unused)) uint32_t type_find_member(const TypeTable* table, TypeId record, uint32_t name, uint64_t* offset);
// The same with the qualifiers the member's access inherits: the record's and those of the anonymous members' types on
// the way. The member is the first with the name in declaration order, depth first through anonymous members, as clang's
// lookup finds it; BUSTER_TYPE_MEMBER_UNKNOWN if the scratch memory couldn't hold a walk. type_lookup_member takes each
// record's lookup (TypeLookup: its index or its scan, compared in vectors, and the walk only where its index didn't
// fit); type_lookup_member_scalar is its exact twin, which compares one name at a time; type_lookup_walk walks the
// members, without the index, depth first.
static __attribute__((unused)) uint32_t type_lookup_member(const TypeTable* table, TypeId record, uint32_t name, uint64_t* offset, uint32_t* qualifiers);
static __attribute__((unused)) uint32_t type_lookup_member_scalar(const TypeTable* table, TypeId record, uint32_t name, uint64_t* offset, uint32_t* qualifiers);
static __attribute__((unused)) uint32_t type_lookup_walk(const TypeTable* table, TypeId record, uint32_t name, uint64_t* offset, uint32_t* qualifiers);
// x86-64's __builtin_va_list: struct __va_list_tag { unsigned gp_offset, fp_offset; void* overflow_arg_area,
// *reg_save_area; }[1], with the names' ids given in that order (the tag's first).
static __attribute__((unused)) TypeId type_builtin_va_list(TypeTable* table, const uint32_t* names);

// The conversions (C 6.3.1), as clang has them: type_promote is the integer promotions and __fp16's promotion to float
// (clang's UsualUnaryConversions on a type), type_promote_bit_field the same for a bit-field of `width` bits,
// type_default_argument_promote adds float's (and __fp16's) to double, and type_usual_arithmetic gives the common type
// of two arithmetic operands (0 if one isn't), which may make a complex _BitInt type, hence the table it takes.
static __attribute__((unused)) TypeId type_promote(const TypeTable* table, TypeId type);
static __attribute__((unused)) TypeId type_promote_bit_field(const TypeTable* table, TypeId type, uint32_t width);
static __attribute__((unused)) TypeId type_default_argument_promote(const TypeTable* table, TypeId type);
static __attribute__((unused)) TypeId type_usual_arithmetic(TypeTable* table, TypeId left, TypeId right);
// The same on arithmetic codes, 64 at a time in vectors, and their scalar twins. A pair the codes alone don't decide
// (a _BitInt or a complex integer type with no floating type) gets BUSTER_TYPE_CODE_DEFERRED.
static __attribute__((unused)) void type_promote_codes(const uint8_t* codes, uint64_t count, uint8_t* results);
static __attribute__((unused)) void type_promote_codes_scalar(const uint8_t* codes, uint64_t count, uint8_t* results);
static __attribute__((unused)) void type_usual_arithmetic_codes(const uint8_t* left, const uint8_t* right, uint64_t count, uint8_t* results);
static __attribute__((unused)) void type_usual_arithmetic_codes_scalar(const uint8_t* left, const uint8_t* right, uint64_t count, uint8_t* results);
// The common types of `count` pairs of TypeIds: the codes' kernel, with the deferred pairs given to type_usual_arithmetic.
static __attribute__((unused)) void type_usual_arithmetic_types(TypeTable* table, const TypeId* left, const TypeId* right, uint64_t count, TypeId* results);

// The answer of a comparison that walks types in scratch memory. FULL: the scratch memory couldn't hold the walk, so
// there is no answer, which a caller must take for neither of the others.
typedef enum TypeAnswer
{
    TYPE_ANSWER_NO,
    TYPE_ANSWER_YES,
    TYPE_ANSWER_FULL,
} TypeAnswer;

// Compatibility (C 6.2.7) and the composite type, as clang's mergeTypes decides them (in C17: distinct tags are never
// compatible; C23's tag compatibility is #133's). Both walk the types with explicit stacks in scratch memory. The
// composite may make types: it returns 0 if they're incompatible, and 0 with last_error TYPE_ERROR_FULL if the table or
// the scratch memory is full.
static __attribute__((unused)) TypeAnswer type_compatible(const TypeTable* table, TypeId left, TypeId right);
static __attribute__((unused)) TypeId type_composite(TypeTable* table, TypeId left, TypeId right);
// History-aware variants of the same relation. Optional tag-indexed activation positions are exclusive source
// ends; zero and UINT32_MAX are inactive. A non-NULL history hides later transparent-union attributes. NULL asks
// for final TypeTable semantics. The other nominal/type properties keep the same contracts as the ordinary APIs.
static __attribute__((unused)) TypeAnswer type_compatible_at(const TypeTable* table, TypeId left, TypeId right, const uint32_t* activations, uint32_t activation_count, uint32_t source_position);
static __attribute__((unused)) TypeId type_composite_at(TypeTable* table, TypeId left, TypeId right, const uint32_t* activations, uint32_t activation_count, uint32_t source_position);
// Whether two types have one canonical type (clang's, which has no typedefs): the same TypeId, or the same structure
// once every ALIGNED is looked through.
static __attribute__((unused)) TypeAnswer type_same_canonical(const TypeTable* table, TypeId left, TypeId right);

// Prints a type as clang's TypePrinter does, after `name` if it has one (a declarator's), into `arena`, whose pushes
// the result's bytes take in order; `names` spell the tags. Returns the text, empty if the arena is full.
static __attribute__((unused)) String8 type_print(const TypeTable* table, Arena* arena, TypeId type, String8 name, const TypeNames* names, uint32_t options);

// #113's deferred bodies (see TypeChange). type_changes_initialize pushes a record's arrays from `arena`, with room for
// `changes` changes, `parameters` parameters and `members` members; false if the arena can't hold them. type_changes_at
// gives the position of the changes that follow; their order restarts at each new position.
static __attribute__((unused)) bool type_changes_initialize(TypeChanges* changes, Arena* arena, uint32_t count, uint32_t parameters, uint32_t members);
static __attribute__((unused)) void type_changes_at(TypeTable* table, uint32_t position);
// Copies `source` into `table`, its columns from `arena` with room for `extra` more of each kind of record, and its index
// from `index_arena`, with room for the source's types and a slack more (BUSTER_TYPE_COPY_INDEX_SLACK), which grows past
// them as any index does (type_view_reset takes it back to a mark). False, with the arenas as they were, without room.
static __attribute__((unused)) bool type_copy(TypeTable* table, const TypeTable* source, Arena* arena, Arena* index_arena, TypeCapacity extra);
// Takes a copy back to the counts it had (`mark`, from type_view_mark), the types made since out of its index, and
// hides nothing. An index that grew since is rebuilt at the mark's size.
typedef struct TypeViewMark TypeViewMark;
struct TypeViewMark
{
    uint32_t types;
    uint32_t parameters;
    uint32_t tags;
    uint32_t members;
    uint32_t lookups;
    uint32_t completions;
    uint32_t buckets;
};
static __attribute__((unused)) TypeViewMark type_view_mark(const TypeTable* table);
static __attribute__((unused)) bool type_view_reset(TypeTable* table, TypeViewMark mark);
// A tag's definition, completion and transparency withdrawn from a copy, for a body that comes before them
// (transparency alone with `transparency_only`), and given back from the table it was copied from.
static __attribute__((unused)) void type_tag_withdraw(TypeTable* table, uint32_t tag, bool transparency_only);
static __attribute__((unused)) void type_tag_restore(TypeTable* table, const TypeTable* source, uint32_t tag);
// A record's part in type_replay: its changes, and the maps from its table's type and tag indices to the replay's,
// which the replay fills, from `base` and `tag_base` (room for all its table made). Smaller indices are `parent`'s,
// the table its table was copied from, or with no parent the builtin types and tag 0, which every table shares.
typedef struct TypeReplayPart TypeReplayPart;
struct TypeReplayPart
{
    const TypeChanges* changes;
    const TypeReplayPart* parent;
    uint32_t base;
    uint32_t tag_base;
    uint32_t* types;
    uint32_t* tags;
    // Per tag from tag_base: 1 for one whose predefined type an earlier tag of the replay's is, whose changes are skipped.
    uint8_t* merged;
};
// Replays the parts' changes into `table`, a table without changes of its own, merged by key: parts[0]'s (the top
// level's) and then the others' in their order (bodies', in source order, whose positions don't overlap), parts[0]'s
// first at a position both have. Each change is made again with its types mapped, and last_error is the last write's
// by key. False if a change can't be made again as it was, or a record is incomplete: the parts aren't one unit's.
static __attribute__((unused)) bool type_replay(TypeTable* table, TypeReplayPart* parts, uint32_t count, Arena* scratch);
// The replay's index for a part's TypeId, with its qualifiers, and for a part's tag index.
static __attribute__((unused)) TypeId type_replay_type(const TypeReplayPart* part, TypeId type);
static __attribute__((unused)) uint32_t type_replay_tag(const TypeReplayPart* part, uint32_t tag);

// ---------------------------------------------------------------------------------------------------------------------
// The builtin types

// clang's builtin types on x86-64 (ASTContext::getTypeInfo with X86_64TargetInfo): their spellings (TypePrinter's,
// bool's depending on the language mode), kinds, sizes and alignments. long double is x87's 80-bit format in 16 bytes.
typedef struct TypeBuiltin TypeBuiltin;
struct TypeBuiltin
{
    const char* name;
    uint8_t kind;
    uint8_t size;
    uint8_t alignment_log2;
};

static const TypeBuiltin type_builtins[BUSTER_TYPE_CODE_COMPLEX] = {
    [BUSTER_TYPE_CODE_VOID] = {
        .name = "void",
        .kind = BUSTER_TYPE_KIND_VOID,
        .size = 1,
        .alignment_log2 = 0,
    },
    [BUSTER_TYPE_CODE_BOOL] = {
        .name = "_Bool",
        .kind = BUSTER_TYPE_KIND_INTEGER,
        .size = 1,
        .alignment_log2 = 0,
    },
    [BUSTER_TYPE_CODE_CHAR] = {
        .name = "char",
        .kind = BUSTER_TYPE_KIND_INTEGER,
        .size = 1,
        .alignment_log2 = 0,
    },
    [BUSTER_TYPE_CODE_SCHAR] = {
        .name = "signed char",
        .kind = BUSTER_TYPE_KIND_INTEGER,
        .size = 1,
        .alignment_log2 = 0,
    },
    [BUSTER_TYPE_CODE_UCHAR] = {
        .name = "unsigned char",
        .kind = BUSTER_TYPE_KIND_INTEGER,
        .size = 1,
        .alignment_log2 = 0,
    },
    [BUSTER_TYPE_CODE_SHORT] = {
        .name = "short",
        .kind = BUSTER_TYPE_KIND_INTEGER,
        .size = 2,
        .alignment_log2 = 1,
    },
    [BUSTER_TYPE_CODE_USHORT] = {
        .name = "unsigned short",
        .kind = BUSTER_TYPE_KIND_INTEGER,
        .size = 2,
        .alignment_log2 = 1,
    },
    [BUSTER_TYPE_CODE_INT] = {
        .name = "int",
        .kind = BUSTER_TYPE_KIND_INTEGER,
        .size = 4,
        .alignment_log2 = 2,
    },
    [BUSTER_TYPE_CODE_UINT] = {
        .name = "unsigned int",
        .kind = BUSTER_TYPE_KIND_INTEGER,
        .size = 4,
        .alignment_log2 = 2,
    },
    [BUSTER_TYPE_CODE_LONG] = {
        .name = "long",
        .kind = BUSTER_TYPE_KIND_INTEGER,
        .size = 8,
        .alignment_log2 = 3,
    },
    [BUSTER_TYPE_CODE_ULONG] = {
        .name = "unsigned long",
        .kind = BUSTER_TYPE_KIND_INTEGER,
        .size = 8,
        .alignment_log2 = 3,
    },
    [BUSTER_TYPE_CODE_LLONG] = {
        .name = "long long",
        .kind = BUSTER_TYPE_KIND_INTEGER,
        .size = 8,
        .alignment_log2 = 3,
    },
    [BUSTER_TYPE_CODE_ULLONG] = {
        .name = "unsigned long long",
        .kind = BUSTER_TYPE_KIND_INTEGER,
        .size = 8,
        .alignment_log2 = 3,
    },
    [BUSTER_TYPE_CODE_INT128] = {
        .name = "__int128",
        .kind = BUSTER_TYPE_KIND_INTEGER,
        .size = 16,
        .alignment_log2 = 4,
    },
    [BUSTER_TYPE_CODE_UINT128] = {
        .name = "unsigned __int128",
        .kind = BUSTER_TYPE_KIND_INTEGER,
        .size = 16,
        .alignment_log2 = 4,
    },
    [BUSTER_TYPE_CODE_BF16] = {
        .name = "__bf16",
        .kind = BUSTER_TYPE_KIND_FLOATING,
        .size = 2,
        .alignment_log2 = 1,
    },
    [BUSTER_TYPE_CODE_FLOAT16] = {
        .name = "_Float16",
        .kind = BUSTER_TYPE_KIND_FLOATING,
        .size = 2,
        .alignment_log2 = 1,
    },
    [BUSTER_TYPE_CODE_HALF] = {
        .name = "__fp16",
        .kind = BUSTER_TYPE_KIND_FLOATING,
        .size = 2,
        .alignment_log2 = 1,
    },
    [BUSTER_TYPE_CODE_FLOAT] = {
        .name = "float",
        .kind = BUSTER_TYPE_KIND_FLOATING,
        .size = 4,
        .alignment_log2 = 2,
    },
    [BUSTER_TYPE_CODE_DOUBLE] = {
        .name = "double",
        .kind = BUSTER_TYPE_KIND_FLOATING,
        .size = 8,
        .alignment_log2 = 3,
    },
    [BUSTER_TYPE_CODE_LONG_DOUBLE] = {
        .name = "long double",
        .kind = BUSTER_TYPE_KIND_FLOATING,
        .size = 16,
        .alignment_log2 = 4,
    },
    [BUSTER_TYPE_CODE_FLOAT128] = {
        .name = "__float128",
        .kind = BUSTER_TYPE_KIND_FLOATING,
        .size = 16,
        .alignment_log2 = 4,
    },
    [BUSTER_TYPE_CODE_NULLPTR] = {
        .name = "nullptr_t",
        .kind = BUSTER_TYPE_KIND_NULLPTR,
        .size = 8,
        .alignment_log2 = 3,
    },
};

// The codes of the builtin complex types' elements: every integer type but bool, and every floating type but __fp16.
// A declaration may spell fewer (BUSTER_TYPE_COMPLEX_SPELLABLE: clang refuses _Complex __int128, __bf16 and _BitInt),
// but the usual arithmetic conversions make the rest (_Complex int and __int128 give _Complex __int128).
#define BUSTER_TYPE_COMPLEX_ELEMENTS                                                                                   \
    (((1ull << (BUSTER_TYPE_CODE_FLOAT16 + 1)) - (1ull << BUSTER_TYPE_CODE_CHAR)) |                                    \
     ((1ull << (BUSTER_TYPE_CODE_FLOAT128 + 1)) - (1ull << BUSTER_TYPE_CODE_FLOAT)))
#define BUSTER_TYPE_COMPLEX_SPELLABLE                                                                                  \
    (((1ull << (BUSTER_TYPE_CODE_ULLONG + 1)) - (1ull << BUSTER_TYPE_CODE_CHAR)) | (1ull << BUSTER_TYPE_CODE_FLOAT16) | \
     ((1ull << (BUSTER_TYPE_CODE_FLOAT128 + 1)) - (1ull << BUSTER_TYPE_CODE_FLOAT)))

// ---------------------------------------------------------------------------------------------------------------------
// The table

// Each column starts on a cache line of its own. Each table's bytes per record, over its columns, and its number of
// columns.
#define BUSTER_TYPE_COLUMN_ALIGNMENT 64
#define BUSTER_TYPE_RECORD_BYTES (4 * sizeof(uint8_t) + sizeof(TypeId) + sizeof(uint32_t) + 3 * sizeof(uint64_t))
#define BUSTER_TYPE_RECORD_COLUMNS 9
#define BUSTER_TYPE_TAG_BYTES (sizeof(TypeId) + 5 * sizeof(uint32_t) + 5 * sizeof(uint8_t))
#define BUSTER_TYPE_TAG_COLUMNS 11
#define BUSTER_TYPE_MEMBER_BYTES (sizeof(uint32_t) + sizeof(TypeId) + sizeof(uint32_t) + 2 * sizeof(uint8_t) + sizeof(uint64_t) + sizeof(uint32_t))
#define BUSTER_TYPE_MEMBER_COLUMNS 7
// The index: four slots per 64-byte bucket, at most three quarters full, from 64 buckets (4 KB).
#define BUSTER_TYPE_BUCKET_SLOTS 4
#define BUSTER_TYPE_BUCKET_BYTES 64
#define BUSTER_TYPE_INITIAL_BUCKETS 64
// The index's bytes that the C driver keeps in base pages (arena_reserve_small): 65,536 buckets, room for 196,608 types,
// far more than a unit makes (sqlite3.c's 3,434 take 128 KB). A larger unit's index grows past them into huge pages.
#define BUSTER_TYPE_INDEX_SMALL_BYTES (4ull << 20)
// The types past a copy's own that its index has room for before it grows (type_copy): at most this many, and at most
// as many as the source's types, but no fewer than the minimum.
#define BUSTER_TYPE_COPY_INDEX_SLACK 4096
#define BUSTER_TYPE_COPY_INDEX_MINIMUM 512
_Static_assert(sizeof(TypeSlot) * BUSTER_TYPE_BUCKET_SLOTS == BUSTER_TYPE_BUCKET_BYTES, "a bucket is a cache line");
_Static_assert(sizeof(TypeLookupBucket) == BUSTER_TYPE_COLUMN_ALIGNMENT, "a lookup bucket is a cache line");

// The member lookup's buckets a capacity has room for besides bucket 0 (TypeCapacity.lookups, one a member for 0).
static uint64_t type_lookup_capacity(TypeCapacity capacity)
{
    return capacity.lookups != 0 ? capacity.lookups : capacity.members;
}

static uint64_t type_capacity_bytes(TypeCapacity capacity)
{
    const uint64_t padding = BUSTER_TYPE_COLUMN_ALIGNMENT - 1;
    uint64_t result = ((uint64_t)capacity.types + BUSTER_TYPE_BUILTIN_COUNT) * BUSTER_TYPE_RECORD_BYTES + BUSTER_TYPE_RECORD_COLUMNS * padding;
    result += ((uint64_t)capacity.parameters + 1) * sizeof(TypeId) + padding;
    result += ((uint64_t)capacity.tags + 1) * BUSTER_TYPE_TAG_BYTES + BUSTER_TYPE_TAG_COLUMNS * padding;
    result += ((uint64_t)capacity.members + 1) * BUSTER_TYPE_MEMBER_BYTES + BUSTER_TYPE_MEMBER_COLUMNS * padding;
    result += (type_lookup_capacity(capacity) + 1) * sizeof(TypeLookupBucket) + padding;
    return result;
}

// The index holds at most 3 types per bucket and doubles, so its buckets stay below twice the smallest power of two
// that holds every type.
static uint64_t type_index_capacity_bytes(TypeCapacity capacity)
{
    const uint64_t types = (uint64_t)capacity.types + BUSTER_TYPE_BUILTIN_COUNT;
    uint64_t buckets = BUSTER_TYPE_INITIAL_BUCKETS;
    while (buckets * 3 < types + 1)
    {
        buckets *= 2;
    }
    return buckets * 2 * BUSTER_TYPE_BUCKET_BYTES + BUSTER_TYPE_BUCKET_BYTES;
}

// A column of `records` elements of `element_bytes`, the first `cleared` of them zeroed, while every column before it
// has fit: 0, and *fits false, once one hasn't.
static void* type_push_column(Arena* arena, uint64_t records, uint64_t element_bytes, uint64_t cleared, bool* fits)
{
    void* result = 0;
    if (*fits)
    {
        result = arena_push_array(arena, records, element_bytes, BUSTER_TYPE_COLUMN_ALIGNMENT);
        *fits = result != 0;
    }
    if (result != 0)
    {
        memset(result, 0, cleared * element_bytes);
    }
    return result;
}

// Fills in the builtin types at their indices, and the builtin complex types from their elements.
static void type_set_builtins(TypeColumns* types)
{
    for (uint32_t code = 1; code < BUSTER_TYPE_CODE_COMPLEX; code += 1)
    {
        const TypeBuiltin* builtin = &type_builtins[code];
        if (builtin->kind != BUSTER_TYPE_KIND_NONE)
        {
            types->kind[code] = builtin->kind;
            types->code[code] = (uint8_t)code;
            types->flags[code] = builtin->kind == BUSTER_TYPE_KIND_VOID ? 0 : BUSTER_TYPE_FLAG_COMPLETE;
            types->alignment_log2[code] = builtin->alignment_log2;
            types->size[code] = builtin->size;
        }
    }
    for (uint32_t element = 0; element < BUSTER_TYPE_CODE_COMPLEX; element += 1)
    {
        if ((BUSTER_TYPE_COMPLEX_ELEMENTS >> element & 1) != 0)
        {
            const uint32_t index = BUSTER_TYPE_CODE_COMPLEX + element;
            types->kind[index] = BUSTER_TYPE_KIND_COMPLEX;
            types->code[index] = (uint8_t)index;
            types->flags[index] = BUSTER_TYPE_FLAG_COMPLETE;
            types->alignment_log2[index] = type_builtins[element].alignment_log2;
            types->base[index] = BUSTER_TYPE_BUILTIN(element);
            types->size[index] = 2ull * type_builtins[element].size;
        }
    }
}

static bool type_initialize(TypeTable* table, Arena* arena, Arena* index_arena, TypeCapacity capacity)
{
    const uint64_t length = arena->length;
    const uint64_t index_length = index_arena->length;
    TypeTable result = {
        .last_error = TYPE_ERROR_NONE,
    };
    // A lookup's first bucket is below BUSTER_TYPE_LOOKUP_WALK.
    const uint64_t lookups = type_lookup_capacity(capacity) + 1;
    bool fits = capacity.types <= BUSTER_TYPE_MAX_INDEX + 1 - BUSTER_TYPE_BUILTIN_COUNT && capacity.parameters < UINT32_MAX && capacity.tags < UINT32_MAX &&
                capacity.members < UINT32_MAX && lookups < BUSTER_TYPE_LOOKUP_WALK;
    if (fits)
    {
        const uint64_t types = (uint64_t)capacity.types + BUSTER_TYPE_BUILTIN_COUNT;
        const uint64_t tags = (uint64_t)capacity.tags + 1;
        result.types.kind = type_push_column(arena, types, sizeof(*result.types.kind), BUSTER_TYPE_BUILTIN_COUNT, &fits);
        result.types.code = type_push_column(arena, types, sizeof(*result.types.code), BUSTER_TYPE_BUILTIN_COUNT, &fits);
        result.types.flags = type_push_column(arena, types, sizeof(*result.types.flags), BUSTER_TYPE_BUILTIN_COUNT, &fits);
        result.types.alignment_log2 = type_push_column(arena, types, sizeof(*result.types.alignment_log2), BUSTER_TYPE_BUILTIN_COUNT, &fits);
        result.types.base = type_push_column(arena, types, sizeof(*result.types.base), BUSTER_TYPE_BUILTIN_COUNT, &fits);
        result.types.index = type_push_column(arena, types, sizeof(*result.types.index), BUSTER_TYPE_BUILTIN_COUNT, &fits);
        result.types.extent = type_push_column(arena, types, sizeof(*result.types.extent), BUSTER_TYPE_BUILTIN_COUNT, &fits);
        result.types.size = type_push_column(arena, types, sizeof(*result.types.size), BUSTER_TYPE_BUILTIN_COUNT, &fits);
        result.types.hash = type_push_column(arena, types, sizeof(*result.types.hash), BUSTER_TYPE_BUILTIN_COUNT, &fits);
        result.parameters.type = type_push_column(arena, (uint64_t)capacity.parameters + 1, sizeof(*result.parameters.type), 0, &fits);
        result.tags.type = type_push_column(arena, tags, sizeof(*result.tags.type), 1, &fits);
        result.tags.name = type_push_column(arena, tags, sizeof(*result.tags.name), 1, &fits);
        result.tags.member_begin = type_push_column(arena, tags, sizeof(*result.tags.member_begin), 1, &fits);
        result.tags.member_count = type_push_column(arena, tags, sizeof(*result.tags.member_count), 1, &fits);
        result.tags.completion = type_push_column(arena, tags, sizeof(*result.tags.completion), 1, &fits);
        result.tags.packed = type_push_column(arena, tags, sizeof(*result.tags.packed), 1, &fits);
        result.tags.ms_struct = type_push_column(arena, tags, sizeof(*result.tags.ms_struct), 1, &fits);
        result.tags.pack_log2 = type_push_column(arena, tags, sizeof(*result.tags.pack_log2), 1, &fits);
        result.tags.alignment_log2 = type_push_column(arena, tags, sizeof(*result.tags.alignment_log2), 1, &fits);
        result.tags.lookup = type_push_column(arena, tags, sizeof(*result.tags.lookup), 1, &fits);
        result.tags.lookup_log2 = type_push_column(arena, tags, sizeof(*result.tags.lookup_log2), 1, &fits);
        result.members.name = type_push_column(arena, (uint64_t)capacity.members + 1, sizeof(*result.members.name), 1, &fits);
        result.members.type = type_push_column(arena, (uint64_t)capacity.members + 1, sizeof(*result.members.type), 1, &fits);
        result.members.bit_width = type_push_column(arena, (uint64_t)capacity.members + 1, sizeof(*result.members.bit_width), 1, &fits);
        result.members.alignment_log2 = type_push_column(arena, (uint64_t)capacity.members + 1, sizeof(*result.members.alignment_log2), 1, &fits);
        result.members.flags = type_push_column(arena, (uint64_t)capacity.members + 1, sizeof(*result.members.flags), 1, &fits);
        result.members.offset = type_push_column(arena, (uint64_t)capacity.members + 1, sizeof(*result.members.offset), 1, &fits);
        result.members.count_name = type_push_column(arena, (uint64_t)capacity.members + 1, sizeof(*result.members.count_name), 1, &fits);
        result.lookup.buckets = type_push_column(arena, lookups, sizeof(*result.lookup.buckets), 1, &fits);
        result.index.slots = type_push_column(index_arena, BUSTER_TYPE_INITIAL_BUCKETS * BUSTER_TYPE_BUCKET_SLOTS, sizeof(TypeSlot),
                                              BUSTER_TYPE_INITIAL_BUCKETS * BUSTER_TYPE_BUCKET_SLOTS, &fits);
        if (fits)
        {
            result.types.count = BUSTER_TYPE_BUILTIN_COUNT;
            result.types.capacity = (uint32_t)types;
            result.parameters.capacity = capacity.parameters;
            result.tags.count = 1;
            result.tags.capacity = (uint32_t)tags;
            result.members.count = 1;
            result.members.capacity = capacity.members + 1;
            result.lookup.count = 1;
            result.lookup.capacity = (uint32_t)lookups;
            result.index.arena = index_arena;
            result.index.bucket_count = BUSTER_TYPE_INITIAL_BUCKETS;
            type_set_builtins(&result.types);
        }
    }
    if (!fits)
    {
        arena_pop_to(arena, length);
        arena_pop_to(index_arena, index_length);
        result = (TypeTable){
            .last_error = TYPE_ERROR_FULL,
        };
    }
    *table = result;
    return fits;
}

// last_error's every write, and its key in the table's record of changes if it has one (TypeChanges.error_written).
static void type_set_error(TypeTable* table, TypeError error)
{
    TypeChanges* changes = table->changes;
    table->last_error = error;
    if (changes != 0)
    {
        changes->error_written = true;
        changes->error_position = changes->position;
        changes->error_sequence = changes->sequence;
        changes->error = error;
        changes->sequence += 1;
    }
}

// Records a change, with the next key, its parameters (a FUNCTION's) or its members (DEFINE's), in the table's record
// if it has one; a change that doesn't fit marks the record full.
static void type_change_record(TypeTable* table, TypeChange change, const TypeId* parameters, uint64_t parameter_count, const TypeMember* members,
                               uint32_t member_count)
{
    TypeChanges* changes = table->changes;
    if (changes != 0 && !changes->full)
    {
        changes->full = changes->count == changes->capacity || parameter_count > changes->parameter_capacity - changes->parameter_count ||
                        member_count > changes->member_capacity - changes->member_count;
        if (!changes->full)
        {
            change.position = changes->position;
            change.sequence = changes->sequence;
            changes->sequence += 1;
            if (parameter_count != 0)
            {
                change.first = changes->parameter_count;
                memcpy(changes->parameters + changes->parameter_count, parameters, parameter_count * sizeof(TypeId));
                changes->parameter_count += (uint32_t)parameter_count;
            }
            if (member_count != 0)
            {
                change.first = changes->member_count;
                change.count = member_count;
                memcpy(changes->members + changes->member_count, members, (uint64_t)member_count * sizeof(TypeMember));
                changes->member_count += member_count;
            }
            changes->items[changes->count++] = change;
        }
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// What a type is

static bool type_is_valid(const TypeTable* table, TypeId type)
{
    const uint32_t index = type >> BUSTER_TYPE_INDEX_SHIFT;
    return index != 0 && index < table->types.count && table->types.kind[index] != BUSTER_TYPE_KIND_NONE;
}

static uint32_t type_index(TypeId type)
{
    return type >> BUSTER_TYPE_INDEX_SHIFT;
}

static uint32_t type_qualifiers(TypeId type)
{
    return type & BUSTER_TYPE_QUALIFIERS;
}

static TypeId type_unqualified(TypeId type)
{
    return type & ~BUSTER_TYPE_QUALIFIERS;
}

static uint8_t type_kind(const TypeTable* table, TypeId type)
{
    return table->types.kind[type >> BUSTER_TYPE_INDEX_SHIFT];
}

static TypeId type_qualify(const TypeTable* table, TypeId type, uint32_t qualifiers)
{
    TypeId result = type | (qualifiers & BUSTER_TYPE_QUALIFIERS);
    if (type_kind(table, type) == BUSTER_TYPE_KIND_FUNCTION)
    {
        result = type_unqualified(type);
    }
    return result;
}

// The index under a wrapper: an ALIGNED type's base's, then a QUALIFIED one's, which the wrapper's index column holds
// (#1447): one load past its callers' one compare, as the single ALIGNED was. A call, a loop over the wrappers or a
// second test made their callers too large to inline where they were (check_action_integer_type).
static inline __attribute__((always_inline)) uint32_t type_unwrapped_index(const TypeTable* table, uint32_t index)
{
    return table->types.index[index];
}

// The index whose size, code and completeness a type has: an ALIGNED or QUALIFIED type's base's. As in
// type_strip_aligned, a wrapper is rare, which the hint says (#1175; never, among sqlite3.c's 244 thousand lookups).
static uint32_t type_resolved_index(const TypeTable* table, TypeId type)
{
    uint32_t index = type >> BUSTER_TYPE_INDEX_SHIFT;
    if (__builtin_expect(table->types.kind[index] >= BUSTER_TYPE_KIND_WRAPPERS, 0))
    {
        index = type_unwrapped_index(table, index);
    }
    return index;
}

static uint64_t type_size(const TypeTable* table, TypeId type)
{
    return table->types.size[type_resolved_index(table, type)];
}

// An ALIGNED type's own alignment; a QUALIFIED one's base's, which may be a tag completed later.
static uint32_t type_alignment_log2(const TypeTable* table, TypeId type)
{
    uint32_t index = type >> BUSTER_TYPE_INDEX_SHIFT;
    if (__builtin_expect(table->types.kind[index] == BUSTER_TYPE_KIND_QUALIFIED, 0))
    {
        index = table->types.base[index] >> BUSTER_TYPE_INDEX_SHIFT;
    }
    return table->types.alignment_log2[index];
}

static uint64_t type_alignment(const TypeTable* table, TypeId type)
{
    return 1ull << type_alignment_log2(table, type);
}

static bool type_is_complete(const TypeTable* table, TypeId type)
{
    return (table->types.flags[type_resolved_index(table, type)] & BUSTER_TYPE_FLAG_COMPLETE) != 0;
}

static uint8_t type_arithmetic_code(const TypeTable* table, TypeId type)
{
    return table->types.code[type_resolved_index(table, type)];
}

// Inlined everywhere: without the hint, clang put the rare aligned case on the path and jumped around it, 6% of the
// taken branches of sqlite3.c's -fsyntax-only (#880). One test of the kind covers both wrappers (#1447).
static TypeId type_strip_aligned(const TypeTable* table, TypeId type)
{
    TypeId result = type;
    if (__builtin_expect(type_kind(table, type) >= BUSTER_TYPE_KIND_WRAPPERS, 0))
    {
        result = (TypeId)type_unwrapped_index(table, type >> BUSTER_TYPE_INDEX_SHIFT) << BUSTER_TYPE_INDEX_SHIFT | type_qualifiers(type);
    }
    return result;
}

static TypeId type_strip_alignment(const TypeTable* table, TypeId type)
{
    TypeId result = type;
    if (__builtin_expect(type_kind(table, type) == BUSTER_TYPE_KIND_ALIGNED, 0))
    {
        result = table->types.base[type >> BUSTER_TYPE_INDEX_SHIFT] | type_qualifiers(type);
    }
    return result;
}

// A wrapper's word, past the compare that is the readers' whole cost (#1447).
static inline __attribute__((always_inline)) uint32_t type_wrapper_qualifiers(const TypeTable* table, uint32_t index)
{
    uint32_t at = index;
    at = table->types.kind[at] == BUSTER_TYPE_KIND_ALIGNED ? table->types.base[at] >> BUSTER_TYPE_INDEX_SHIFT : at;
    return table->types.kind[at] == BUSTER_TYPE_KIND_QUALIFIED ? (uint32_t)table->types.extent[at] : 0;
}

// Inlined everywhere (#1447): a table without a QUALIFIED record (TypeTable.extended) reads no type.
static inline __attribute__((always_inline)) uint32_t type_extended_qualifiers(const TypeTable* table, TypeId type)
{
    uint32_t result = 0;
    if (__builtin_expect(table->extended, 0) && type_kind(table, type) >= BUSTER_TYPE_KIND_WRAPPERS)
    {
        result = type_wrapper_qualifiers(table, type >> BUSTER_TYPE_INDEX_SHIFT);
    }
    return result;
}

static uint32_t type_address_space(uint32_t word)
{
    return word & BUSTER_TYPE_SPACE_MASK;
}

// Equal spaces; OpenCL's generic includes every space but constant, and global its device and host parts. (The
// default space and a target's, 0 among them, include no other: clang keeps them apart in C.)
static bool type_space_includes(uint32_t to, uint32_t from)
{
    return to == from || (to == BUSTER_TYPE_SPACE_GENERIC && from != BUSTER_TYPE_SPACE_CONSTANT) ||
           (to == BUSTER_TYPE_SPACE_GLOBAL && (from == BUSTER_TYPE_SPACE_GLOBAL_DEVICE || from == BUSTER_TYPE_SPACE_GLOBAL_HOST));
}

static bool type_kind_is_array(uint8_t kind)
{
    return kind == BUSTER_TYPE_KIND_ARRAY || kind == BUSTER_TYPE_KIND_INCOMPLETE_ARRAY || kind == BUSTER_TYPE_KIND_VARIABLE_ARRAY;
}

static __attribute__((noinline)) uint32_t type_object_qualifiers_slow(const TypeTable* table, TypeId type)
{
    TypeId current = type;
    TypeId stripped = type_strip_aligned(table, current);
    while (type_kind_is_array(type_kind(table, stripped)))
    {
        current = table->types.base[type_index(stripped)];
        stripped = type_strip_aligned(table, current);
    }
    return type_extended_qualifiers(table, current);
}

// Inlined everywhere (#1447): a table without a QUALIFIED record (TypeTable.extended) reads no type.
static inline __attribute__((always_inline)) uint32_t type_object_qualifiers(const TypeTable* table, TypeId type)
{
    return __builtin_expect(table->extended, 0) ? type_object_qualifiers_slow(table, type) : 0;
}

// Whether the x86-64 psABI aligns a variable of `type` to at least 16 bytes (§3.1.2, clang's large arrays in
// ASTContext::getDeclAlign): an array of 16 bytes or more. A declaration with an alignment attribute or specifier takes
// that instead, and a compound literal and a string literal their types' alignment (#978).
static __attribute__((unused)) bool type_large_array(const TypeTable* table, TypeId type)
{
    const bool result = type_kind_is_array(type_kind(table, type_strip_aligned(table, type))) && type_size(table, type) >= 16;
    return result;
}

static TypeId type_element(const TypeTable* table, TypeId type)
{
    const uint8_t kind = type_kind(table, type);
    TypeId result = table->types.base[type >> BUSTER_TYPE_INDEX_SHIFT];
    if (type_kind_is_array(kind))
    {
        result |= type_qualifiers(type);
    }
    return result;
}

static const TypeId* type_parameters(const TypeTable* table, TypeId function, uint32_t* count)
{
    const uint32_t index = function >> BUSTER_TYPE_INDEX_SHIFT;
    *count = (uint32_t)table->types.extent[index];
    return table->parameters.type + table->types.index[index];
}

static TypeId type_return(const TypeTable* table, TypeId function)
{
    return table->types.base[function >> BUSTER_TYPE_INDEX_SHIFT];
}

// The flags a derived type inherits from what it is derived from.
static uint8_t type_inherited_flags(const TypeTable* table, TypeId type)
{
    return table->types.flags[type >> BUSTER_TYPE_INDEX_SHIFT] & (BUSTER_TYPE_FLAG_VARIABLY_MODIFIED | BUSTER_TYPE_FLAG_ALIGNED_INSIDE);
}

// ---------------------------------------------------------------------------------------------------------------------
// Hash-consing

// The key hash: the key's three words (kind, flags and base; the extent; the parameters' hash, 0 for a type without
// them) mixed in as link_hash mixes its words, with an XOR, a 64-bit multiplication and a fold of the product's high half,
// then SplitMix64's finalizer (THIRD_PARTY_NOTICES.md has its reference). Each step is one AVX-512 instruction on eight
// 64-bit lanes, which is what lets type_derive_batch hash eight keys as one.
#define BUSTER_TYPE_HASH_SEED 0x243F6A8885A308D3ull
#define BUSTER_TYPE_HASH_MULTIPLIER 0x9E3779B97F4A7C15ull
#define BUSTER_TYPE_HASH_FINISH0 0xbf58476d1ce4e5b9ull
#define BUSTER_TYPE_HASH_FINISH1 0x94d049bb133111ebull
// type_derive_batch's group of keys, one per 64-bit lane.
#define BUSTER_TYPE_BATCH 8
// The slot key's bit for an extent of 2^32 or more, which the slot can't hold whole.
#define BUSTER_TYPE_SLOT_WIDE 0x8000u
// The kinds whose base's qualifiers go in the derived type's TypeId: arrays (C's arrays of qualified elements are
// qualified arrays, as clang's canonical types have them), ALIGNED and QUALIFIED.
#define BUSTER_TYPE_HOISTING_KINDS                                                                                     \
    ((1ull << BUSTER_TYPE_KIND_ARRAY) | (1ull << BUSTER_TYPE_KIND_INCOMPLETE_ARRAY) | (1ull << BUSTER_TYPE_KIND_VARIABLE_ARRAY) | \
     (1ull << BUSTER_TYPE_KIND_ALIGNED) | (1ull << BUSTER_TYPE_KIND_QUALIFIED))
// The kinds that are hash-consed: every derived kind, and COMPLEX (only a complex _BitInt isn't a builtin type).
#define BUSTER_TYPE_HASHED_KINDS                                                                                       \
    (BUSTER_TYPE_HOISTING_KINDS | (1ull << BUSTER_TYPE_KIND_POINTER) | (1ull << BUSTER_TYPE_KIND_FUNCTION) |              \
     (1ull << BUSTER_TYPE_KIND_VECTOR) | (1ull << BUSTER_TYPE_KIND_EXT_VECTOR) | (1ull << BUSTER_TYPE_KIND_ATOMIC) |      \
     (1ull << BUSTER_TYPE_KIND_BIT_INT) | (1ull << BUSTER_TYPE_KIND_COMPLEX))

static uint64_t type_hash_mix(uint64_t hash, uint64_t word)
{
    const uint64_t result = (hash ^ word) * BUSTER_TYPE_HASH_MULTIPLIER;
    return result ^ (result >> 32);
}

static uint64_t type_hash_finish(uint64_t hash)
{
    uint64_t result = (hash ^ (hash >> 30)) * BUSTER_TYPE_HASH_FINISH0;
    result = (result ^ (result >> 27)) * BUSTER_TYPE_HASH_FINISH1;
    return result ^ (result >> 31);
}

// Two parameters per word, the count in the seed.
static uint64_t type_parameters_hash(const TypeId* parameters, uint32_t count)
{
    uint64_t hash = BUSTER_TYPE_HASH_SEED ^ count;
    for (uint32_t i = 0; i < count; i += 2)
    {
        const uint64_t second = i + 1 < count ? parameters[i + 1] : 0;
        hash = type_hash_mix(hash, parameters[i] | second << 32);
    }
    return type_hash_finish(hash);
}

// How many words a function's parameter range has (BUSTER_TYPE_FLAG_FUNCTION_INFO): its `count` parameters, and with the
// flag its info word and its parameter ABIs' words.
static uint64_t type_function_words(const TypeId* words, uint8_t flags, uint64_t count)
{
    uint64_t result = count;
    if ((flags & BUSTER_TYPE_FLAG_FUNCTION_INFO) != 0)
    {
        const bool abis = (words[count] & BUSTER_TYPE_FUNCTION_PARAMETER_ABIS) != 0;
        result += 1 + (abis ? (count + BUSTER_TYPE_PARAMETER_ABIS_PER_WORD - 1) / BUSTER_TYPE_PARAMETER_ABIS_PER_WORD : 0);
    }
    return result;
}

static uint64_t type_key_hash(uint8_t kind, uint8_t flags, TypeId base, uint64_t extent, uint64_t parameters_hash)
{
    uint64_t hash = type_hash_mix(BUSTER_TYPE_HASH_SEED, (uint64_t)kind | (uint64_t)flags << 8 | (uint64_t)base << 32);
    hash = type_hash_mix(hash, extent);
    hash = type_hash_mix(hash, parameters_hash);
    return type_hash_finish(hash);
}

static uint8_t type_key_flags(uint8_t kind, uint8_t flags)
{
    return flags & (kind == BUSTER_TYPE_KIND_FUNCTION ? BUSTER_TYPE_FLAG_FUNCTION_KEY : BUSTER_TYPE_FLAG_KEY);
}

static uint32_t type_slot_key(uint8_t kind, uint8_t flags, uint64_t extent, uint64_t hash)
{
    // Functions have a 32-bit parameter count; non-functions never carry the function-only INFO flag bit.
    const uint32_t wide = extent > UINT32_MAX ? BUSTER_TYPE_SLOT_WIDE : 0;
    return (uint32_t)kind | (uint32_t)flags << 8 | wide | (uint32_t)(hash >> 48) << 16;
}

// Whether the type at `index`, whose slot matched, has the whole key: the slot holds all of it but a function's
// parameters and the high half of a wide extent.
static bool type_key_matches(const TypeTable* table, uint32_t index, uint32_t key, uint64_t extent, const TypeId* parameters)
{
    bool result = true;
    if ((key & 0xff) == BUSTER_TYPE_KIND_FUNCTION)
    {
        // The flags are the slot's, so the two have the info word or neither: the parameters and it first, and only where
        // the info words are equal, which gives both the same count of words, the parameter ABIs' words after them, so that
        // no comparison reads past the stored type's words.
        const uint8_t flags = (uint8_t)(key >> 8);
        const TypeId* stored = table->parameters.type + table->types.index[index];
        const uint64_t head = extent + ((flags & BUSTER_TYPE_FLAG_FUNCTION_INFO) != 0 ? 1 : 0);
        result = head == 0 || memcmp(stored, parameters, head * sizeof(TypeId)) == 0;
        const uint64_t words = result ? type_function_words(parameters, flags, extent) : 0;
        result = result && (words == head || memcmp(stored + head, parameters + head, (words - head) * sizeof(TypeId)) == 0);
    }
    else if ((key & BUSTER_TYPE_SLOT_WIDE) != 0)
    {
        result = table->types.extent[index] == extent;
    }
    return result;
}

// The one-at-a-time lookup, slot by slot: the index of the type with this key, or 0, with *slot the empty slot where it
// would go. Types are never removed, so a key is absent once its probe meets an empty slot. A hidden type (a view's,
// TypeTable.hidden_begin) is passed over, and a type made again for the view comes after it in the probe.
static uint32_t type_index_find(const TypeTable* table, uint32_t key, TypeId base, uint64_t extent, const TypeId* parameters, uint64_t hash, uint32_t* slot)
{
    const TypeIndex* index = &table->index;
    const uint32_t mask = index->bucket_count - 1;
    uint32_t bucket = (uint32_t)hash & mask;
    uint32_t result = 0;
    bool searching = true;
    while (searching)
    {
        for (uint32_t i = 0; searching && i < BUSTER_TYPE_BUCKET_SLOTS; i += 1)
        {
            const uint32_t position = bucket * BUSTER_TYPE_BUCKET_SLOTS + i;
            const TypeSlot entry = index->slots[position];
            if (entry.index == 0)
            {
                *slot = position;
                searching = false;
            }
            else if (entry.key == key && entry.base == base && entry.extent == (uint32_t)extent && (entry.index < table->hidden_begin || entry.index >= table->hidden_end) &&
                     type_key_matches(table, entry.index, key, extent, parameters))
            {
                result = entry.index;
                searching = false;
            }
        }
        bucket = (bucket + 1) & mask;
    }
    return result;
}

// The batch's lookup of a key the slot holds whole: a bucket at a time, its four slots compared with the key in one
// vpcmpeqd. A slot matches where its first three dwords do; an empty slot's index dword is 0, as the pattern's is.
static uint32_t type_index_find_vector(const TypeIndex* index, uint32_t key, TypeId base, uint32_t extent, uint64_t hash)
{
    const __m512i pattern = _mm512_broadcast_i32x4(_mm_set_epi32(0, (int)key, (int)extent, (int)base));
    const uint32_t mask = index->bucket_count - 1;
    uint32_t bucket = (uint32_t)hash & mask;
    uint32_t result = 0;
    bool searching = true;
    while (searching)
    {
        const TypeSlot* slots = index->slots + (uint64_t)bucket * BUSTER_TYPE_BUCKET_SLOTS;
        const uint32_t equal = _mm512_cmpeq_epi32_mask(_mm512_load_si512(slots), pattern);
        const uint32_t matches = equal & (equal >> 1) & (equal >> 2) & 0x1111u;
        if (matches != 0)
        {
            result = slots[__builtin_ctz(matches) / BUSTER_TYPE_BUCKET_SLOTS].index;
            searching = false;
        }
        else if ((equal & 0x8888u) != 0)
        {
            searching = false;
        }
        bucket = (bucket + 1) & mask;
    }
    return result;
}

// The slot of a type the index doesn't hold, at the first empty slot from its hash's bucket.
static void type_index_place(TypeIndex* index, uint32_t type, TypeId base, uint64_t extent, uint32_t key, uint64_t hash)
{
    const uint32_t mask = index->bucket_count - 1;
    uint32_t position = ((uint32_t)hash & mask) * BUSTER_TYPE_BUCKET_SLOTS;
    while (index->slots[position].index != 0)
    {
        position = (position + 1) & (mask * BUSTER_TYPE_BUCKET_SLOTS + BUSTER_TYPE_BUCKET_SLOTS - 1);
    }
    index->slots[position] = (TypeSlot){
        .base = base,
        .extent = (uint32_t)extent,
        .key = key,
        .index = type,
    };
    index->count += 1;
}

// Doubles the index, in the same memory and past it, and puts every hash-consed type back from the columns. If the
// arena can't hold it, the index stays as it was: popping leaves the bytes, and pushing the old size again gets them.
static bool type_index_grow(TypeTable* table)
{
    TypeIndex* index = &table->index;
    const uint64_t start = (uint64_t)((uint8_t*)index->slots - index->arena->base);
    const uint64_t buckets = (uint64_t)index->bucket_count * 2;
    arena_pop_to(index->arena, start);
    TypeSlot* slots = arena_push_array(index->arena, buckets * BUSTER_TYPE_BUCKET_SLOTS, sizeof(TypeSlot), BUSTER_TYPE_COLUMN_ALIGNMENT);
    const bool result = slots != 0 && buckets <= UINT32_MAX;
    if (result)
    {
        memset(slots, 0, buckets * BUSTER_TYPE_BUCKET_BYTES);
        index->slots = slots;
        index->bucket_count = (uint32_t)buckets;
        index->count = 0;
        const TypeColumns* types = &table->types;
        for (uint32_t i = BUSTER_TYPE_BUILTIN_COUNT; i < types->count; i += 1)
        {
            if ((BUSTER_TYPE_HASHED_KINDS >> types->kind[i] & 1) != 0)
            {
                const uint8_t flags = type_key_flags(types->kind[i], types->flags[i]);
                const uint64_t hash = types->hash[i];
                type_index_place(index, i, types->base[i], types->extent[i], type_slot_key(types->kind[i], flags, types->extent[i], hash), hash);
            }
        }
    }
    else
    {
        arena_pop_to(index->arena, start);
        arena_push_array(index->arena, (uint64_t)index->bucket_count * BUSTER_TYPE_BUCKET_SLOTS, sizeof(TypeSlot), BUSTER_TYPE_COLUMN_ALIGNMENT);
    }
    return result;
}

// What a new derived type is: its size, alignment, code and the flags the table computes.
typedef struct TypeProperties TypeProperties;
struct TypeProperties
{
    uint64_t size;
    uint8_t alignment_log2;
    uint8_t code;
    uint8_t flags;
};

static uint64_t type_bit_ceil(uint64_t value)
{
    return value <= 1 ? 1 : 1ull << (64 - __builtin_clzll(value - 1));
}

static uint32_t type_log2(uint64_t power_of_two)
{
    return (uint32_t)__builtin_ctzll(power_of_two);
}

// clang's vector width and alignment in bits (getTypeInfo): the elements' bits (a bool element's 1 in a packed
// ext_vector_type), at least a byte, aligned to the width rounded up to a power of two and padded to it.
static void type_vector_layout(uint64_t element_bits, uint64_t count, uint64_t* width, uint64_t* alignment)
{
    uint64_t bits = element_bits * count;
    bits = bits < 8 ? 8 : bits;
    uint64_t align = bits;
    if ((align & (align - 1)) != 0)
    {
        align = type_bit_ceil(align);
        bits = (bits + align - 1) & ~(align - 1);
    }
    *width = bits;
    *alignment = align;
}

static bool type_is_power_of_two(uint64_t value)
{
    return value != 0 && (value & (value - 1)) == 0;
}

// Whether a vector may have `element` (unqualified, without ALIGNED) as its element: clang's BuildVectorType takes a
// builtin integer but bool or a real floating type, and BuildExtVectorType any integer type (bool and complete enums
// too) or real floating type; both take a _BitInt whose width is a power of two.
static bool type_vector_element_is_valid(const TypeTable* table, TypeId element, bool ext)
{
    const uint8_t kind = type_kind(table, element);
    const uint8_t code = table->types.code[type_index(element)];
    bool result = kind == BUSTER_TYPE_KIND_FLOATING || (kind == BUSTER_TYPE_KIND_INTEGER && (ext || code != BUSTER_TYPE_CODE_BOOL));
    result = result || (kind == BUSTER_TYPE_KIND_BIT_INT && type_is_power_of_two(table->types.extent[type_index(element)]));
    result = result || (ext && kind == BUSTER_TYPE_KIND_ENUM && type_is_complete(table, element));
    return result;
}

// Checks a normalized request and computes what its type would be. Validity only grows as tags are completed, so a
// type the index holds needs no check again.
static TypeError type_check_derivation(const TypeTable* table, uint8_t kind, uint8_t flags, TypeId base, uint64_t extent, const TypeId* parameters,
                                       TypeProperties* properties)
{
    TypeError result = TYPE_ERROR_NONE;
    *properties = (TypeProperties){ 0 };
    const bool needs_base = kind != BUSTER_TYPE_KIND_BIT_INT;
    if ((needs_base && !type_is_valid(table, base)) || (!needs_base && base != 0))
    {
        result = TYPE_ERROR_OPERAND;
    }
    else if (kind >= BUSTER_TYPE_KIND_COUNT || (BUSTER_TYPE_HASHED_KINDS >> kind & 1) == 0)
    {
        result = TYPE_ERROR_OPERAND;
    }
    else if (kind == BUSTER_TYPE_KIND_POINTER)
    {
        result = flags == 0 && extent == 0 ? TYPE_ERROR_NONE : TYPE_ERROR_OPERAND;
        properties->size = 8;
        properties->alignment_log2 = 3;
        properties->flags = BUSTER_TYPE_FLAG_COMPLETE | type_inherited_flags(table, base);
    }
    else if (type_kind_is_array(kind))
    {
        const uint8_t element_kind = type_kind(table, type_strip_aligned(table, base));
        const uint64_t element_size = type_size(table, base);
        const uint32_t element_alignment_log2 = type_alignment_log2(table, base);
        uint64_t size = 0;
        if (flags != 0 || (kind == BUSTER_TYPE_KIND_INCOMPLETE_ARRAY && extent != 0) || (kind == BUSTER_TYPE_KIND_VARIABLE_ARRAY && extent > UINT32_MAX))
        {
            result = TYPE_ERROR_OPERAND;
        }
        else if (element_kind == BUSTER_TYPE_KIND_VOID || element_kind == BUSTER_TYPE_KIND_FUNCTION || !type_is_complete(table, base))
        {
            result = TYPE_ERROR_ELEMENT;
        }
        else if ((element_size & ((1ull << element_alignment_log2) - 1)) != 0)
        {
            result = TYPE_ERROR_ELEMENT_ALIGNMENT;
        }
        else if (kind == BUSTER_TYPE_KIND_ARRAY && (__builtin_mul_overflow(element_size, extent, &size) || size > BUSTER_TYPE_MAX_OBJECT_BYTES))
        {
            result = TYPE_ERROR_TOO_LARGE;
        }
        properties->size = size;
        properties->alignment_log2 = (uint8_t)element_alignment_log2;
        properties->flags = type_inherited_flags(table, base);
        if (kind != BUSTER_TYPE_KIND_INCOMPLETE_ARRAY)
        {
            properties->flags |= BUSTER_TYPE_FLAG_COMPLETE;
        }
        if (kind == BUSTER_TYPE_KIND_VARIABLE_ARRAY)
        {
            properties->flags |= BUSTER_TYPE_FLAG_VARIABLY_MODIFIED;
        }
    }
    else if (kind == BUSTER_TYPE_KIND_FUNCTION)
    {
        const TypeId result_type = type_strip_aligned(table, base);
        const uint8_t result_kind = type_kind(table, result_type);
        const bool prototype = (flags & BUSTER_TYPE_FLAG_PROTOTYPE) != 0;
        const bool info = (flags & BUSTER_TYPE_FLAG_FUNCTION_INFO) != 0 && extent <= UINT32_MAX;
        const uint32_t word = info ? parameters[extent] : TYPE_FUNCTION_ABI_SYSV;
        const uint32_t abi = word & BUSTER_TYPE_FUNCTION_ABI_MASK & ~TYPE_FUNCTION_ABI_KEEPS_REGISTERS;
        const bool abi_valid = type_function_abi_valid(word & BUSTER_TYPE_FUNCTION_ABI_MASK);
        const bool variadic = (flags & BUSTER_TYPE_FLAG_VARIADIC) != 0;
        // The info word is there only for what a System V function without parameter ABIs hasn't, so that each type has
        // one key; and a parameter ABI's value is one of TypeParameterABI's, none past the parameters.
        // (A prototype's cfi_salt in the bits above them, #1447.)
        const uint32_t salt = word >> BUSTER_TYPE_FUNCTION_SALT_SHIFT;
        bool words_valid = !info || (word != TYPE_FUNCTION_ABI_SYSV && (salt == 0 || prototype));
        const uint64_t abi_words = info ? type_function_words(parameters, flags, extent) - extent - 1 : 0;
        bool any = false;
        for (uint64_t i = 0; words_valid && i < abi_words; i += 1)
        {
            const uint32_t abis = parameters[extent + 1 + i];
            for (uint32_t j = 0; j < BUSTER_TYPE_PARAMETER_ABIS_PER_WORD; j += 1)
            {
                const uint32_t value = abis >> (j * BUSTER_TYPE_PARAMETER_ABI_BITS) & ((1u << BUSTER_TYPE_PARAMETER_ABI_BITS) - 1);
                words_valid = words_valid && value < TYPE_PARAMETER_ABI_COUNT && (value == 0 || i * BUSTER_TYPE_PARAMETER_ABIS_PER_WORD + j < extent);
                any = any || value != 0;
            }
        }
        words_valid = words_valid && (abi_words == 0 || any);
        // clang's supportsVariadicCall: vectorcall, regcall and the Swift conventions take no variadic function.
        const bool unvariadic = abi == TYPE_FUNCTION_ABI_VECTOR || abi == TYPE_FUNCTION_ABI_REG || abi == TYPE_FUNCTION_ABI_SWIFT || abi == TYPE_FUNCTION_ABI_SWIFT_ASYNC;
        if ((flags & ~BUSTER_TYPE_FLAG_FUNCTION_KEY) != 0 || !abi_valid || !words_valid ||
            (!prototype && ((flags & BUSTER_TYPE_FLAG_KEY) != 0 || extent != 0)) || extent > UINT32_MAX || (variadic && unvariadic))
        {
            result = TYPE_ERROR_OPERAND;
        }
        else if (type_kind_is_array(result_kind) || result_kind == BUSTER_TYPE_KIND_FUNCTION || type_unqualified(result_type) == BUSTER_TYPE_HALF)
        {
            result = TYPE_ERROR_RETURN;
        }
        properties->size = 1;
        properties->alignment_log2 = 2;
        properties->flags = type_inherited_flags(table, base);
        for (uint64_t i = 0; result == TYPE_ERROR_NONE && i < extent; i += 1)
        {
            const TypeId parameter = parameters[i];
            if (!type_is_valid(table, parameter) || type_qualifiers(parameter) != 0)
            {
                result = TYPE_ERROR_OPERAND;
            }
            else
            {
                const TypeId stripped = type_strip_aligned(table, parameter);
                const uint8_t parameter_kind = type_kind(table, stripped);
                if (parameter_kind == BUSTER_TYPE_KIND_VOID || type_kind_is_array(parameter_kind) || parameter_kind == BUSTER_TYPE_KIND_FUNCTION ||
                    type_unqualified(stripped) == BUSTER_TYPE_HALF)
                {
                    result = TYPE_ERROR_PARAMETER;
                }
                properties->flags |= type_inherited_flags(table, parameter) & BUSTER_TYPE_FLAG_ALIGNED_INSIDE;
            }
        }
    }
    else if (kind == BUSTER_TYPE_KIND_VECTOR || kind == BUSTER_TYPE_KIND_EXT_VECTOR)
    {
        const TypeId element = type_unqualified(base);
        const bool ext = kind == BUSTER_TYPE_KIND_EXT_VECTOR;
        // A NEON vector's kind (#1447) is a GNU vector's flag alone, of one kind at a time.
        const bool neon = !ext && (flags == BUSTER_TYPE_FLAG_NEON || flags == BUSTER_TYPE_FLAG_NEON_POLY);
        if ((flags != 0 && !neon) || extent == 0 || extent > UINT32_MAX || !type_vector_element_is_valid(table, element, ext))
        {
            result = TYPE_ERROR_VECTOR;
        }
        else
        {
            const bool bits = ext && element == BUSTER_TYPE_BOOL;
            uint64_t width = 0;
            uint64_t alignment = 0;
            type_vector_layout(bits ? 1 : type_size(table, element) * 8, extent, &width, &alignment);
            if (width > BUSTER_TYPE_MAX_VECTOR_BITS)
            {
                result = TYPE_ERROR_TOO_LARGE;
            }
            properties->size = width / 8;
            properties->alignment_log2 = (uint8_t)type_log2(alignment / 8);
            properties->flags = BUSTER_TYPE_FLAG_COMPLETE;
        }
    }
    else if (kind == BUSTER_TYPE_KIND_ATOMIC)
    {
        const uint8_t value_kind = type_kind(table, type_strip_aligned(table, base));
        if (flags != 0 || extent != 0)
        {
            result = TYPE_ERROR_OPERAND;
        }
        else if (type_qualifiers(base) != 0 || type_extended_qualifiers(table, base) != 0 || type_kind_is_array(value_kind) || value_kind == BUSTER_TYPE_KIND_FUNCTION ||
                 value_kind == BUSTER_TYPE_KIND_ATOMIC ||
                 value_kind == BUSTER_TYPE_KIND_BIT_INT || !type_is_complete(table, base))
        {
            result = TYPE_ERROR_ATOMIC;
        }
        else
        {
            // clang's getTypeInfo for an AtomicType: up to MaxAtomicPromoteWidth (16 bytes on x86-64) the size rounds up
            // to a power of two and becomes the alignment, which may lower it; a type of size 0 gets a byte.
            uint64_t width = type_size(table, base) * 8;
            uint64_t alignment = type_alignment(table, base) * 8;
            if (width == 0)
            {
                width = 8;
            }
            else if (width <= 128)
            {
                width = type_bit_ceil(width);
                alignment = width;
            }
            properties->size = width / 8;
            properties->alignment_log2 = (uint8_t)type_log2(alignment / 8);
            properties->code = type_arithmetic_code(table, base);
            properties->flags = BUSTER_TYPE_FLAG_COMPLETE | type_inherited_flags(table, base);
        }
    }
    else if (kind == BUSTER_TYPE_KIND_QUALIFIED)
    {
        // A normalized request (type_extend): an unqualified base that is no wrapper, array or function, and a word
        // of a space clang has and weak or strong.
        const uint8_t base_kind = type_kind(table, base);
        const uint32_t space = (uint32_t)extent & BUSTER_TYPE_SPACE_MASK;
        const uint32_t gc = (uint32_t)extent & BUSTER_TYPE_GC_MASK;
        if (flags != 0 || extent == 0 || extent > UINT32_MAX || (extent & ~(uint64_t)(BUSTER_TYPE_SPACE_MASK | BUSTER_TYPE_GC_MASK)) != 0 ||
            gc == BUSTER_TYPE_GC_MASK || (space > BUSTER_TYPE_SPACE_GLOBAL_HOST && space < BUSTER_TYPE_SPACE_TARGET) ||
            space > BUSTER_TYPE_SPACE_TARGET + BUSTER_TYPE_SPACE_MAX_TARGET || type_qualifiers(base) != 0 || base_kind == BUSTER_TYPE_KIND_ALIGNED ||
            base_kind == BUSTER_TYPE_KIND_QUALIFIED || type_kind_is_array(base_kind) || base_kind == BUSTER_TYPE_KIND_FUNCTION)
        {
            result = TYPE_ERROR_OPERAND;
        }
        // Its base's properties where they are known, for the readers of the columns (layout reads a member's, which
        // only a garbage collection qualifier's pointer can be); the accessors read an incomplete tag's later.
        const bool complete = result == TYPE_ERROR_NONE && type_is_complete(table, base);
        properties->size = complete ? type_size(table, base) : 0;
        properties->alignment_log2 = complete ? (uint8_t)type_alignment_log2(table, base) : 0;
        properties->code = result == TYPE_ERROR_NONE ? type_arithmetic_code(table, base) : 0;
        properties->flags = type_inherited_flags(table, base) | (complete ? BUSTER_TYPE_FLAG_COMPLETE : 0);
    }
    else if (kind == BUSTER_TYPE_KIND_ALIGNED)
    {
        if (flags != 0)
        {
            result = TYPE_ERROR_OPERAND;
        }
        else if (extent > BUSTER_TYPE_MAX_ALIGNMENT_LOG2 || type_kind(table, base) == BUSTER_TYPE_KIND_FUNCTION)
        {
            result = TYPE_ERROR_ALIGNMENT;
        }
        properties->alignment_log2 = (uint8_t)extent;
        properties->flags = BUSTER_TYPE_FLAG_ALIGNED_INSIDE | (type_inherited_flags(table, base) & BUSTER_TYPE_FLAG_VARIABLY_MODIFIED);
    }
    else if (kind == BUSTER_TYPE_KIND_BIT_INT)
    {
        const bool is_unsigned = (flags & BUSTER_TYPE_FLAG_UNSIGNED) != 0;
        if ((flags & ~BUSTER_TYPE_FLAG_UNSIGNED) != 0)
        {
            result = TYPE_ERROR_OPERAND;
        }
        else if (extent < (is_unsigned ? 1u : 2u) || extent > BUSTER_TYPE_MAX_BIT_INT_WIDTH)
        {
            result = TYPE_ERROR_BIT_INT;
        }
        else
        {
            // TargetInfo::getBitIntAlign and getBitIntWidth: the width rounded up to a power of two, from a byte to
            // BitIntMaxAlign (long long's 64 bits on x86-64), and the width padded to that.
            uint64_t alignment = type_bit_ceil(extent);
            alignment = alignment < 8 ? 8 : alignment > 64 ? 64 : alignment;
            properties->size = ((extent + alignment - 1) & ~(alignment - 1)) / 8;
            properties->alignment_log2 = (uint8_t)type_log2(alignment / 8);
            properties->code = is_unsigned ? BUSTER_TYPE_CODE_UNSIGNED_BIT_INT : BUSTER_TYPE_CODE_BIT_INT;
            properties->flags = BUSTER_TYPE_FLAG_COMPLETE;
        }
    }
    else
    {
        if (flags != 0 || extent != 0)
        {
            result = TYPE_ERROR_OPERAND;
        }
        else if (type_qualifiers(base) != 0 || type_kind(table, base) != BUSTER_TYPE_KIND_BIT_INT)
        {
            result = TYPE_ERROR_COMPLEX;
        }
        else
        {
            properties->size = type_size(table, base) * 2;
            properties->alignment_log2 = (uint8_t)type_alignment_log2(table, base);
            properties->code = (uint8_t)(BUSTER_TYPE_CODE_COMPLEX + type_arithmetic_code(table, base));
            properties->flags = BUSTER_TYPE_FLAG_COMPLETE;
        }
    }
    return result;
}

// Appends a type's columns, and its parameters: its index, or 0 if a column is full.
static uint32_t type_append(TypeTable* table, uint8_t kind, uint8_t flags, TypeId base, uint64_t extent, const TypeId* parameters, TypeProperties properties,
                            uint64_t hash)
{
    TypeColumns* types = &table->types;
    TypeParameters* parameter_column = &table->parameters;
    const uint64_t parameter_count = kind == BUSTER_TYPE_KIND_FUNCTION ? type_function_words(parameters, flags, extent) : 0;
    uint32_t result = 0;
    if (types->count < types->capacity && parameter_count <= parameter_column->capacity - parameter_column->count)
    {
        result = types->count;
        types->kind[result] = kind;
        table->extended |= kind == BUSTER_TYPE_KIND_QUALIFIED;
        table->ext_vectors |= kind == BUSTER_TYPE_KIND_EXT_VECTOR;
        if (__builtin_expect(kind == BUSTER_TYPE_KIND_ATOMIC, 0))
        {
            const uint8_t base_kind = type_kind(table, type_strip_aligned(table, base));
            table->atomic_records |= base_kind == BUSTER_TYPE_KIND_STRUCT || base_kind == BUSTER_TYPE_KIND_UNION;
        }
        types->code[result] = properties.code;
        types->flags[result] = flags | properties.flags;
        types->alignment_log2[result] = properties.alignment_log2;
        types->base[result] = base;
        // A wrapper's index is the type under every wrapper (type_unwrapped_index): its base's, or what its base's says.
        const uint32_t under = base >> BUSTER_TYPE_INDEX_SHIFT;
        const uint32_t unwrapped = types->kind[under] >= BUSTER_TYPE_KIND_WRAPPERS ? types->index[under] : under;
        types->index[result] = kind == BUSTER_TYPE_KIND_FUNCTION ? parameter_column->count : kind >= BUSTER_TYPE_KIND_WRAPPERS ? unwrapped : 0;
        types->extent[result] = extent;
        types->size[result] = properties.size;
        types->hash[result] = hash;
        types->count += 1;
        if (parameter_count != 0)
        {
            memcpy(parameter_column->type + parameter_column->count, parameters, parameter_count * sizeof(TypeId));
            parameter_column->count += (uint32_t)parameter_count;
        }
    }
    return result;
}

// Finds or adds a normalized request's type: its index, or 0 with last_error set.
static uint32_t type_find_or_add(TypeTable* table, uint8_t kind, uint8_t flags, TypeId base, uint64_t extent, const TypeId* parameters)
{
    const uint64_t words = kind == BUSTER_TYPE_KIND_FUNCTION ? type_function_words(parameters, flags, extent) : 0;
    const uint64_t parameters_hash = kind == BUSTER_TYPE_KIND_FUNCTION ? type_parameters_hash(parameters, (uint32_t)words) : 0;
    const uint64_t hash = type_key_hash(kind, flags, base, extent, parameters_hash);
    const uint32_t key = type_slot_key(kind, flags, extent, hash);
    uint32_t slot = 0;
    uint32_t result = type_index_find(table, key, base, extent, parameters, hash, &slot);
    if (result == 0)
    {
        TypeProperties properties;
        TypeError error = type_check_derivation(table, kind, flags, base, extent, parameters, &properties);
        // A bounded, immutable view may find existing types, but must not grow the shared index before discovering
        // that its type column is full. Deferred expression lanes use such views; their error word is lane-local.
        if (error == TYPE_ERROR_NONE && table->types.count == table->types.capacity)
        {
            error = TYPE_ERROR_FULL;
        }
        if (error == TYPE_ERROR_NONE && (uint64_t)table->index.count + 1 > 3ull * table->index.bucket_count)
        {
            if (type_index_grow(table))
            {
                type_index_find(table, key, base, extent, parameters, hash, &slot);
            }
            else
            {
                error = TYPE_ERROR_FULL;
            }
        }
        if (error == TYPE_ERROR_NONE)
        {
            result = type_append(table, kind, flags, base, extent, parameters, properties, hash);
            error = result == 0 ? TYPE_ERROR_FULL : TYPE_ERROR_NONE;
        }
        if (error == TYPE_ERROR_NONE)
        {
            table->index.slots[slot] = (TypeSlot){
                .base = base,
                .extent = (uint32_t)extent,
                .key = key,
                .index = result,
            };
            table->index.count += 1;
            const TypeChange change = {
                .operation = BUSTER_TYPE_CHANGE_DERIVE,
                .kind = kind,
                .flags = flags,
                .base = base,
                .extent = extent,
                .type = (TypeId)result << BUSTER_TYPE_INDEX_SHIFT,
            };
            type_change_record(table, change, parameters, words, 0, 0);
        }
        else
        {
            type_set_error(table, error);
        }
    }
    return result;
}

// The one-at-a-time path of every derived kind but FUNCTION: normalizes the request as TypeColumns describes (a
// qualified array's qualifiers go in its TypeId; a pointer to a function points to it unqualified; an ALIGNED of an
// ALIGNED is one of its base, and one that doesn't change a complete type's alignment is that type; a vector's element
// loses its ALIGNED, which clang's canonical vector type doesn't have), then finds or adds its type.
static TypeId type_derive(TypeTable* table, uint8_t kind, uint8_t flags, TypeId base, uint64_t extent)
{
    TypeId result = 0;
    TypeId normal = base;
    uint32_t qualifiers = 0;
    bool valid = kind == BUSTER_TYPE_KIND_BIT_INT ? base == 0 : type_is_valid(table, base);
    if (valid)
    {
        if (kind < BUSTER_TYPE_KIND_COUNT && (BUSTER_TYPE_HOISTING_KINDS >> kind & 1) != 0)
        {
            qualifiers = type_qualifiers(base);
            normal = type_unqualified(base);
        }
        if (kind == BUSTER_TYPE_KIND_POINTER && type_kind(table, base) == BUSTER_TYPE_KIND_FUNCTION)
        {
            normal = type_unqualified(base);
        }
        else if (kind == BUSTER_TYPE_KIND_ALIGNED && type_kind(table, normal) == BUSTER_TYPE_KIND_ALIGNED)
        {
            normal = table->types.base[type_index(normal)];
        }
        else if (kind == BUSTER_TYPE_KIND_VECTOR || kind == BUSTER_TYPE_KIND_EXT_VECTOR)
        {
            normal = type_strip_aligned(table, base);
        }
    }
    if (!valid || kind == BUSTER_TYPE_KIND_FUNCTION)
    {
        type_set_error(table, TYPE_ERROR_OPERAND);
    }
    else if (kind == BUSTER_TYPE_KIND_ALIGNED && extent == type_alignment_log2(table, normal) && type_is_complete(table, normal))
    {
        result = normal | qualifiers;
    }
    else if (kind == BUSTER_TYPE_KIND_COMPLEX && type_index(normal) < BUSTER_TYPE_BUILTIN_COUNT && flags == 0 && extent == 0)
    {
        const uint32_t code = type_index(normal);
        if (type_qualifiers(normal) == 0 && (BUSTER_TYPE_COMPLEX_ELEMENTS >> code & 1) != 0)
        {
            result = BUSTER_TYPE_COMPLEX(code);
        }
        else
        {
            type_set_error(table, TYPE_ERROR_COMPLEX);
        }
    }
    else
    {
        const uint32_t index = type_find_or_add(table, kind, flags, normal, extent, 0);
        result = index == 0 ? 0 : (TypeId)index << BUSTER_TYPE_INDEX_SHIFT | qualifiers;
    }
    return result;
}

static TypeId type_find_pointer(const TypeTable* table, TypeId pointee)
{
    // A pointer to a function points to it unqualified, as type_derive normalizes it.
    const TypeId base = type_kind(table, pointee) == BUSTER_TYPE_KIND_FUNCTION ? type_unqualified(pointee) : pointee;
    const uint64_t hash = type_key_hash(BUSTER_TYPE_KIND_POINTER, 0, base, 0, 0);
    uint32_t slot = 0;
    const uint32_t index = type_is_valid(table, pointee) ? type_index_find(table, type_slot_key(BUSTER_TYPE_KIND_POINTER, 0, 0, hash), base, 0, 0, hash, &slot) : 0;
    return (TypeId)index << BUSTER_TYPE_INDEX_SHIFT;
}

static TypeId type_pointer(TypeTable* table, TypeId pointee)
{
    return type_derive(table, BUSTER_TYPE_KIND_POINTER, 0, pointee, 0);
}

static TypeId type_array(TypeTable* table, TypeId element, uint64_t length)
{
    return type_derive(table, BUSTER_TYPE_KIND_ARRAY, 0, element, length);
}

static TypeId type_incomplete_array(TypeTable* table, TypeId element)
{
    return type_derive(table, BUSTER_TYPE_KIND_INCOMPLETE_ARRAY, 0, element, 0);
}

static TypeId type_variable_array(TypeTable* table, TypeId element, uint32_t expression)
{
    return type_derive(table, BUSTER_TYPE_KIND_VARIABLE_ARRAY, 0, element, expression);
}

static TypeId type_vector(TypeTable* table, TypeId element, uint32_t count, bool ext)
{
    return type_derive(table, ext ? BUSTER_TYPE_KIND_EXT_VECTOR : BUSTER_TYPE_KIND_VECTOR, 0, element, count);
}

static TypeId type_vector_kind(TypeTable* table, TypeId element, uint32_t count, uint8_t kind)
{
    return type_derive(table, BUSTER_TYPE_KIND_VECTOR, kind, element, count);
}

// GNU's vector_size(bytes), as clang's BuildVectorType counts its elements: bytes must be a multiple of the element's
// size, and the count fit in 32 bits.
static TypeId type_vector_size(TypeTable* table, TypeId element, uint64_t bytes)
{
    TypeId result = 0;
    if (!type_is_valid(table, element))
    {
        type_set_error(table, TYPE_ERROR_OPERAND);
    }
    else
    {
        const uint64_t element_size = type_size(table, element);
        if (bytes == 0 || element_size == 0 || bytes % element_size != 0 || bytes / element_size > UINT32_MAX)
        {
            type_set_error(table, TYPE_ERROR_VECTOR);
        }
        else
        {
            result = type_vector(table, element, (uint32_t)(bytes / element_size), false);
        }
    }
    return result;
}

static TypeId type_atomic(TypeTable* table, TypeId value)
{
    return type_derive(table, BUSTER_TYPE_KIND_ATOMIC, 0, value, 0);
}

static TypeId type_aligned(TypeTable* table, TypeId base, uint32_t alignment_log2)
{
    return type_derive(table, BUSTER_TYPE_KIND_ALIGNED, 0, base, alignment_log2);
}

// The array at `depth` levels of arrays below `type` (unqualified, an array at each level above).
static TypeId type_array_level(const TypeTable* table, TypeId type, uint32_t depth)
{
    TypeId result = type;
    for (uint32_t d = 0; d < depth; d += 1)
    {
        result = table->types.base[type_index(result)];
    }
    return result;
}

// type_extend's and type_unextend's: the type's extended qualifiers less `mask`, with `word`'s. The array levels are
// made again outward from the element, each found again from the top, so that no level needs memory (arrays of
// arrays are few levels deep).
static TypeId type_requalify(TypeTable* table, TypeId type, uint32_t word, uint32_t mask)
{
    TypeId result = 0;
    if (!type_is_valid(table, type))
    {
        type_set_error(table, TYPE_ERROR_OPERAND);
    }
    else
    {
        const uint32_t qualifiers = type_qualifiers(type);
        TypeId top = type_unqualified(type);
        const bool aligned = type_kind(table, top) == BUSTER_TYPE_KIND_ALIGNED;
        const uint32_t alignment_log2 = aligned ? (uint32_t)table->types.extent[type_index(top)] : 0;
        top = aligned ? table->types.base[type_index(top)] : top;
        uint32_t depth = 0;
        TypeId element = top;
        while (type_kind_is_array(type_kind(table, element)))
        {
            element = table->types.base[type_index(element)];
            depth += 1;
        }
        const bool qualified = type_kind(table, element) == BUSTER_TYPE_KIND_QUALIFIED;
        const uint32_t old = qualified ? (uint32_t)table->types.extent[type_index(element)] : 0;
        const uint32_t merged = (old & ~mask) | word;
        element = qualified ? table->types.base[type_index(element)] : element;
        if (merged == old)
        {
            result = type;
        }
        else if (type_kind(table, element) == BUSTER_TYPE_KIND_FUNCTION)
        {
            type_set_error(table, TYPE_ERROR_OPERAND);
        }
        else
        {
            result = merged == 0 ? element : type_derive(table, BUSTER_TYPE_KIND_QUALIFIED, 0, element, merged);
            for (uint32_t level = depth; result != 0 && level-- > 0;)
            {
                const TypeId array = type_array_level(table, top, level);
                const uint32_t index = type_index(array);
                result = type_derive(table, table->types.kind[index], table->types.flags[index] & BUSTER_TYPE_FLAG_KEY, result, table->types.extent[index]);
            }
            result = result != 0 && aligned ? type_aligned(table, result, alignment_log2) : result;
            result = result != 0 ? result | qualifiers : 0;
        }
    }
    return result;
}

static TypeId type_extend(TypeTable* table, TypeId type, uint32_t word)
{
    const uint32_t mask = ((word & BUSTER_TYPE_SPACE_MASK) != 0 ? BUSTER_TYPE_SPACE_MASK : 0) | ((word & BUSTER_TYPE_GC_MASK) != 0 ? BUSTER_TYPE_GC_MASK : 0);
    return type_requalify(table, type, word, mask);
}

static TypeId type_unextend(TypeTable* table, TypeId type, uint32_t mask)
{
    return type_requalify(table, type, 0, mask);
}

static TypeId type_bit_int(TypeTable* table, uint32_t width, bool is_unsigned)
{
    return type_derive(table, BUSTER_TYPE_KIND_BIT_INT, is_unsigned ? BUSTER_TYPE_FLAG_UNSIGNED : 0, 0, width);
}

static TypeId type_complex(TypeTable* table, TypeId element)
{
    return type_derive(table, BUSTER_TYPE_KIND_COMPLEX, 0, element, 0);
}

// mode's scalar names are a small, cold declaration table. The rules were checked against clang 23.1.1 and its
// SemaDeclAttr.cpp; the implementation and fixtures here are original. Integer rows name the signed builtin, whose
// following code is its unsigned twin. Floating rows name a real type; C suffixes make its complex type. Zero is
// a recognized width with no x86-64 type. HC's complex __fp16 and complex/zero-element vectors are deliberately
// refused: clang can construct them through mode, but the existing type table cannot represent them.
typedef struct TypeMachineMode TypeMachineMode;
struct TypeMachineMode
{
    String8 name;
    uint8_t code;
    bool integer;
    bool complex;
};

static const TypeMachineMode type_machine_modes[] =
{
    {
        .name = {
            .pointer = "QI",
            .length = 2,
        },
        .code = BUSTER_TYPE_CODE_SCHAR,
        .integer = true,
        .complex = false,
    },
    {
        .name = {
            .pointer = "HI",
            .length = 2,
        },
        .code = BUSTER_TYPE_CODE_SHORT,
        .integer = true,
        .complex = false,
    },
    {
        .name = {
            .pointer = "SI",
            .length = 2,
        },
        .code = BUSTER_TYPE_CODE_INT,
        .integer = true,
        .complex = false,
    },
    {
        .name = {
            .pointer = "DI",
            .length = 2,
        },
        .code = BUSTER_TYPE_CODE_LONG,
        .integer = true,
        .complex = false,
    },
    {
        .name = {
            .pointer = "TI",
            .length = 2,
        },
        .code = BUSTER_TYPE_CODE_INT128,
        .integer = true,
        .complex = false,
    },
    {
        .name = {
            .pointer = "XI",
            .length = 2,
        },
        .code = 0,
        .integer = true,
        .complex = false,
    },
    {
        .name = {
            .pointer = "byte",
            .length = 4,
        },
        .code = BUSTER_TYPE_CODE_SCHAR,
        .integer = true,
        .complex = false,
    },
    {
        .name = {
            .pointer = "word",
            .length = 4,
        },
        .code = BUSTER_TYPE_CODE_LONG,
        .integer = true,
        .complex = false,
    },
    {
        .name = {
            .pointer = "pointer",
            .length = 7,
        },
        .code = BUSTER_TYPE_CODE_LONG,
        .integer = true,
        .complex = false,
    },
    {
        .name = {
            .pointer = "unwind_word",
            .length = 11,
        },
        .code = BUSTER_TYPE_CODE_LONG,
        .integer = true,
        .complex = false,
    },
    {
        .name = {
            .pointer = "QF",
            .length = 2,
        },
        .code = 0,
        .integer = false,
        .complex = false,
    },
    {
        .name = {
            .pointer = "HF",
            .length = 2,
        },
        .code = BUSTER_TYPE_CODE_HALF,
        .integer = false,
        .complex = false,
    },
    {
        .name = {
            .pointer = "SF",
            .length = 2,
        },
        .code = BUSTER_TYPE_CODE_FLOAT,
        .integer = false,
        .complex = false,
    },
    {
        .name = {
            .pointer = "DF",
            .length = 2,
        },
        .code = BUSTER_TYPE_CODE_DOUBLE,
        .integer = false,
        .complex = false,
    },
    {
        .name = {
            .pointer = "XF",
            .length = 2,
        },
        .code = BUSTER_TYPE_CODE_LONG_DOUBLE,
        .integer = false,
        .complex = false,
    },
    {
        .name = {
            .pointer = "TF",
            .length = 2,
        },
        .code = BUSTER_TYPE_CODE_FLOAT128,
        .integer = false,
        .complex = false,
    },
    {
        .name = {
            .pointer = "KF",
            .length = 2,
        },
        .code = BUSTER_TYPE_CODE_FLOAT128,
        .integer = false,
        .complex = false,
    },
    {
        .name = {
            .pointer = "IF",
            .length = 2,
        },
        .code = 0,
        .integer = false,
        .complex = false,
    },
    {
        .name = {
            .pointer = "QC",
            .length = 2,
        },
        .code = 0,
        .integer = false,
        .complex = true,
    },
    {
        .name = {
            .pointer = "HC",
            .length = 2,
        },
        .code = BUSTER_TYPE_CODE_HALF,
        .integer = false,
        .complex = true,
    },
    {
        .name = {
            .pointer = "SC",
            .length = 2,
        },
        .code = BUSTER_TYPE_CODE_FLOAT,
        .integer = false,
        .complex = true,
    },
    {
        .name = {
            .pointer = "DC",
            .length = 2,
        },
        .code = BUSTER_TYPE_CODE_DOUBLE,
        .integer = false,
        .complex = true,
    },
    {
        .name = {
            .pointer = "XC",
            .length = 2,
        },
        .code = BUSTER_TYPE_CODE_LONG_DOUBLE,
        .integer = false,
        .complex = true,
    },
    {
        .name = {
            .pointer = "TC",
            .length = 2,
        },
        .code = BUSTER_TYPE_CODE_FLOAT128,
        .integer = false,
        .complex = true,
    },
    {
        .name = {
            .pointer = "KC",
            .length = 2,
        },
        .code = BUSTER_TYPE_CODE_FLOAT128,
        .integer = false,
        .complex = true,
    },
    {
        .name = {
            .pointer = "IC",
            .length = 2,
        },
        .code = 0,
        .integer = false,
        .complex = true,
    },
};

static TypeId type_machine_mode(TypeTable* table, TypeId base, String8 mode)
{
    TypeId result = 0;
    const bool operand = type_is_valid(table, base);
    bool valid = mode.pointer != 0;
    if (valid && mode.length >= 4 && mode.pointer[0] == '_' && mode.pointer[1] == '_' &&
        mode.pointer[mode.length - 2] == '_' && mode.pointer[mode.length - 1] == '_')
    {
        mode.pointer += 2;
        mode.length -= 4;
    }
    uint64_t vector_count = 0;
    bool count_overflow = false;
    const bool vector_mode = valid && mode.length >= 4 && mode.pointer[0] == 'V';
    if (vector_mode)
    {
        uint64_t first = 1;
        while (first < mode.length && mode.pointer[first] >= '0' && mode.pointer[first] <= '9')
        {
            const uint32_t digit = (uint32_t)(mode.pointer[first] - '0');
            count_overflow |= vector_count > (UINT64_MAX - digit) / 10;
            if (!count_overflow)
            {
                vector_count = vector_count * 10 + digit;
            }
            first += 1;
        }
        valid = !count_overflow && vector_count != 0 && (vector_count & (vector_count - 1)) == 0;
        mode.pointer += first;
        mode.length -= first;
    }
    const TypeMachineMode* selected = 0;
    for (uint32_t i = 0; valid && selected == 0 && i < sizeof(type_machine_modes) / sizeof(type_machine_modes[0]); i += 1)
    {
        const TypeMachineMode* candidate = &type_machine_modes[i];
        if (candidate->name.length == mode.length && memcmp(candidate->name.pointer, mode.pointer, mode.length) == 0)
        {
            selected = candidate;
        }
    }
    if (!operand)
    {
        type_set_error(table, TYPE_ERROR_OPERAND);
    }
    else if (count_overflow)
    {
        type_set_error(table, TYPE_ERROR_TOO_LARGE);
    }
    else if (selected == 0)
    {
        type_set_error(table, TYPE_ERROR_MODE);
    }
    else
    {
        const TypeId stripped = type_strip_aligned(table, base);
        const uint8_t kind = type_kind(table, stripped);
        const bool vector_base = kind == BUSTER_TYPE_KIND_VECTOR || kind == BUSTER_TYPE_KIND_EXT_VECTOR;
        const TypeId element = vector_base ? type_strip_aligned(table, type_element(table, stripped)) : stripped;
        const uint8_t element_kind = type_kind(table, element);
        const bool enumeration = element_kind == BUSTER_TYPE_KIND_ENUM;
        const bool integer = element_kind == BUSTER_TYPE_KIND_INTEGER || enumeration;
        const bool complex = element_kind == BUSTER_TYPE_KIND_COMPLEX && type_kind(table, type_element(table, element)) == BUSTER_TYPE_KIND_FLOATING;
        const bool floating = element_kind == BUSTER_TYPE_KIND_FLOATING || complex;
        if ((vector_mode && enumeration) || (selected->integer ? !integer : selected->complex ? !complex : !floating))
        {
            type_set_error(table, TYPE_ERROR_MODE_BASE);
        }
        else if (selected->code == 0 || (selected->complex && (selected->code == BUSTER_TYPE_CODE_HALF || vector_base || vector_mode)))
        {
            type_set_error(table, TYPE_ERROR_MODE_UNSUPPORTED);
        }
        else
        {
            const uint8_t old_code = type_arithmetic_code(table, element);
            const bool is_signed = old_code == BUSTER_TYPE_CODE_CHAR ? !table->char_unsigned :
                                   old_code >= BUSTER_TYPE_CODE_SCHAR && old_code <= BUSTER_TYPE_CODE_INT128 && (old_code & 1) == 0;
            const uint8_t code = selected->code + (selected->integer ? !is_signed : selected->complex ? BUSTER_TYPE_CODE_COMPLEX : 0);
            result = BUSTER_TYPE_BUILTIN(code);
            if (vector_base && !vector_mode)
            {
                // Count the original elements' actual bytes, not the vector's rounded storage size.
                vector_count = type_size(table, element) * table->types.extent[type_index(stripped)] / type_size(table, result);
            }
            if (vector_mode || vector_base)
            {
                if (vector_count == 0)
                {
                    result = 0;
                    type_set_error(table, TYPE_ERROR_MODE_UNSUPPORTED);
                }
                else if (vector_count > UINT32_MAX)
                {
                    result = 0;
                    type_set_error(table, TYPE_ERROR_TOO_LARGE);
                }
                else
                {
                    // clang's mode produces a GNU vector even when the old type was an ext_vector_type; of an old vector's
                    // elements (no vector mode), its kind, which a NEON vector's flags are (#1447, handleModeAttr).
                    const uint8_t neon = vector_base && !vector_mode && kind == BUSTER_TYPE_KIND_VECTOR ?
                                         table->types.flags[type_index(stripped)] & (BUSTER_TYPE_FLAG_NEON | BUSTER_TYPE_FLAG_NEON_POLY) : 0;
                    result = type_vector_kind(table, result, (uint32_t)vector_count, neon);
                }
            }
            if (result != 0)
            {
                type_set_error(table, TYPE_ERROR_NONE);
            }
        }
    }
    return result;
}

// A function's info word (BUSTER_TYPE_FLAG_FUNCTION_INFO): System V's convention, without parameter ABIs, if it has none.
static uint32_t type_function_info(const TypeTable* table, uint32_t index)
{
    const bool info = (table->types.flags[index] & BUSTER_TYPE_FLAG_FUNCTION_INFO) != 0;
    return info ? table->parameters.type[table->types.index[index] + (uint32_t)table->types.extent[index]] : TYPE_FUNCTION_ABI_SYSV;
}

static TypeFunctionABI type_function_abi_kind(const TypeTable* table, TypeId function)
{
    TypeFunctionABI result = TYPE_FUNCTION_ABI_INVALID;
    if (type_is_valid(table, function))
    {
        const TypeId stripped = type_strip_aligned(table, function);
        if (type_kind(table, stripped) == BUSTER_TYPE_KIND_FUNCTION)
        {
            result = (TypeFunctionABI)(type_function_info(table, type_index(stripped)) & BUSTER_TYPE_FUNCTION_ABI_MASK);
        }
    }
    return result;
}

// A function type's convention without no_caller_saved_registers' bit (#1447, TYPE_FUNCTION_ABI_KEEPS_REGISTERS):
// TYPE_FUNCTION_ABI_INVALID for no function.
static TypeFunctionABI type_function_convention(const TypeTable* table, TypeId function)
{
    const TypeFunctionABI abi = type_function_abi_kind(table, function);
    return abi == TYPE_FUNCTION_ABI_INVALID ? abi : (TypeFunctionABI)(abi & ~TYPE_FUNCTION_ABI_KEEPS_REGISTERS);
}

// Whether a function type's callee keeps every register (#147, #1447): no_caller_saved_registers', System V's or beside
// another convention.
static bool type_function_keeps_registers(const TypeTable* table, TypeId function)
{
    const TypeFunctionABI abi = type_function_abi_kind(table, function);
    return abi != TYPE_FUNCTION_ABI_INVALID && (abi == TYPE_FUNCTION_ABI_NO_CALLER_SAVED || (abi & TYPE_FUNCTION_ABI_KEEPS_REGISTERS) != 0);
}

// A function type's cfi_salt id (#1447, BUSTER_TYPE_FUNCTION_SALT_SHIFT), 0 for none or for no function.
static uint32_t type_function_salt(const TypeTable* table, TypeId function)
{
    uint32_t result = 0;
    if (type_is_valid(table, function))
    {
        const TypeId stripped = type_strip_aligned(table, function);
        result = type_kind(table, stripped) == BUSTER_TYPE_KIND_FUNCTION ? type_function_info(table, type_index(stripped)) >> BUSTER_TYPE_FUNCTION_SALT_SHIFT : 0;
    }
    return result;
}

// A function type's convention with its cfi_salt (#1447), as type_function_full takes them back: what a type made again
// from it keeps. TYPE_FUNCTION_ABI_INVALID's for no function.
static uint32_t type_function_extended_abi(const TypeTable* table, TypeId function)
{
    const TypeFunctionABI abi = type_function_abi_kind(table, function);
    return abi == TYPE_FUNCTION_ABI_INVALID ? (uint32_t)abi : (uint32_t)abi | type_function_salt(table, function) << BUSTER_TYPE_FUNCTION_SALT_SHIFT;
}

static bool type_function_has_parameter_abis(const TypeTable* table, TypeId function)
{
    bool result = false;
    if (type_is_valid(table, function))
    {
        const TypeId stripped = type_strip_aligned(table, function);
        result = type_kind(table, stripped) == BUSTER_TYPE_KIND_FUNCTION && (type_function_info(table, type_index(stripped)) & BUSTER_TYPE_FUNCTION_PARAMETER_ABIS) != 0;
    }
    return result;
}

static TypeParameterABI type_parameter_abi(const TypeTable* table, TypeId function, uint32_t parameter)
{
    TypeParameterABI result = TYPE_PARAMETER_ABI_ORDINARY;
    if (type_function_has_parameter_abis(table, function))
    {
        const uint32_t index = type_index(type_strip_aligned(table, function));
        const uint32_t count = (uint32_t)table->types.extent[index];
        if (parameter < count)
        {
            const uint32_t word = table->parameters.type[table->types.index[index] + count + 1 + parameter / BUSTER_TYPE_PARAMETER_ABIS_PER_WORD];
            const uint32_t shift = parameter % BUSTER_TYPE_PARAMETER_ABIS_PER_WORD * BUSTER_TYPE_PARAMETER_ABI_BITS;
            result = (TypeParameterABI)(word >> shift & ((1u << BUSTER_TYPE_PARAMETER_ABI_BITS) - 1));
        }
    }
    return result;
}

// Parameters are stored unqualified, and are copied without their qualifiers into scratch memory, with the info word and
// the parameter ABIs' words where the function has them (BUSTER_TYPE_FLAG_FUNCTION_INFO).
static TypeId type_function_full(TypeTable* table, TypeId result_type, const TypeId* parameters, uint32_t count, uint8_t flags, TypeFunctionABI abi,
                                 const uint8_t* parameter_abis)
{
    TypeId result = 0;
    Scratch scratch = scratch_begin(0);
    bool any = false;
    bool abis_valid = true;
    for (uint32_t i = 0; parameter_abis != 0 && i < count; i += 1)
    {
        any = any || parameter_abis[i] != TYPE_PARAMETER_ABI_ORDINARY;
        abis_valid = abis_valid && parameter_abis[i] < TYPE_PARAMETER_ABI_COUNT;
    }
    // (The convention in the low byte, a prototype's cfi_salt above BUSTER_TYPE_FUNCTION_SALT_SHIFT, #1447.)
    const uint32_t salt = (uint32_t)abi >> BUSTER_TYPE_FUNCTION_SALT_SHIFT;
    const bool valid = (flags & ~BUSTER_TYPE_FLAG_KEY) == 0 && type_function_abi_valid((uint32_t)abi & BUSTER_TYPE_FUNCTION_ABI_MASK) &&
                       ((uint32_t)abi & BUSTER_TYPE_FUNCTION_PARAMETER_ABIS) == 0 && (salt == 0 || (flags & BUSTER_TYPE_FLAG_PROTOTYPE) != 0) &&
                       (count == 0 || parameters != 0) && abis_valid;
    const bool info = abi != TYPE_FUNCTION_ABI_SYSV || any;
    const uint32_t abi_words = any ? (count + BUSTER_TYPE_PARAMETER_ABIS_PER_WORD - 1) / BUSTER_TYPE_PARAMETER_ABIS_PER_WORD : 0;
    const uint64_t words = (uint64_t)count + (info ? 1 + abi_words : 0);
    // A parameter's qualifiers aren't its function type's, objc_gc's garbage collection qualifier among them (#1447, as clang's
    // canonical parameter types are unqualified).
    bool qualified = false;
    bool collected = false;
    for (uint32_t i = 0; valid && i < count; i += 1)
    {
        qualified = qualified || type_qualifiers(parameters[i]) != 0;
        collected = collected || (table->extended && (type_extended_qualifiers(table, parameters[i]) & BUSTER_TYPE_GC_MASK) != 0);
    }
    qualified |= collected;
    const TypeId* normal = parameters;
    if (valid && (qualified || info))
    {
        TypeId* copy = BUSTER_ARENA_PUSH_ARRAY(scratch.arena, TypeId, words);
        if (copy != 0)
        {
            for (uint32_t i = 0; i < count; i += 1)
            {
                copy[i] = type_unqualified(parameters[i]);
                copy[i] = collected && (type_extended_qualifiers(table, copy[i]) & BUSTER_TYPE_GC_MASK) != 0 ? type_unextend(table, copy[i], BUSTER_TYPE_GC_MASK) :
                                                                                                         copy[i];
            }
            if (info)
            {
                copy[count] = (uint32_t)abi | (any ? BUSTER_TYPE_FUNCTION_PARAMETER_ABIS : 0);
            }
            for (uint32_t i = 0; i < abi_words; i += 1)
            {
                uint32_t word = 0;
                for (uint32_t j = 0; j < BUSTER_TYPE_PARAMETER_ABIS_PER_WORD && i * BUSTER_TYPE_PARAMETER_ABIS_PER_WORD + j < count; j += 1)
                {
                    word |= (uint32_t)parameter_abis[i * BUSTER_TYPE_PARAMETER_ABIS_PER_WORD + j] << (j * BUSTER_TYPE_PARAMETER_ABI_BITS);
                }
                copy[count + 1 + i] = word;
            }
        }
        normal = copy;
    }
    if (!valid)
    {
        type_set_error(table, TYPE_ERROR_OPERAND);
    }
    else if (normal == 0 && words != 0)
    {
        type_set_error(table, TYPE_ERROR_FULL);
    }
    else if (!type_is_valid(table, result_type))
    {
        type_set_error(table, TYPE_ERROR_OPERAND);
    }
    else
    {
        const uint8_t key_flags = flags | (info ? BUSTER_TYPE_FLAG_FUNCTION_INFO : 0);
        const uint32_t index = type_find_or_add(table, BUSTER_TYPE_KIND_FUNCTION, key_flags, result_type, count, normal);
        result = (TypeId)index << BUSTER_TYPE_INDEX_SHIFT;
    }
    scratch_end(scratch);
    return result;
}

static TypeId type_function_abi(TypeTable* table, TypeId result_type, const TypeId* parameters, uint32_t count, uint8_t flags, TypeFunctionABI abi)
{
    return type_function_full(table, result_type, parameters, count, flags, abi, 0);
}

static TypeId type_function_like(TypeTable* table, TypeId result_type, const TypeId* parameters, uint32_t count, uint8_t flags, TypeFunctionABI abi, TypeId model)
{
    TypeId result = 0;
    // The model's cfi_salt too, where `abi` carries none and the type is a prototype (#1447).
    const uint32_t salt = (flags & BUSTER_TYPE_FLAG_PROTOTYPE) != 0 && ((uint32_t)abi >> BUSTER_TYPE_FUNCTION_SALT_SHIFT) == 0 ? type_function_salt(table, model) : 0;
    abi = abi == TYPE_FUNCTION_ABI_INVALID ? abi : (TypeFunctionABI)((uint32_t)abi | salt << BUSTER_TYPE_FUNCTION_SALT_SHIFT);
    if (type_function_has_parameter_abis(table, model))
    {
        Scratch scratch = scratch_begin(0);
        uint8_t* abis = BUSTER_ARENA_PUSH_ARRAY(scratch.arena, uint8_t, (uint64_t)count + 1);
        if (abis != 0)
        {
            for (uint32_t i = 0; i < count; i += 1)
            {
                abis[i] = (uint8_t)type_parameter_abi(table, model, i);
            }
            result = type_function_full(table, result_type, parameters, count, flags, abi, abis);
        }
        else
        {
            type_set_error(table, TYPE_ERROR_FULL);
        }
        scratch_end(scratch);
    }
    else
    {
        result = type_function_full(table, result_type, parameters, count, flags, abi, 0);
    }
    return result;
}

static TypeId type_function(TypeTable* table, TypeId result_type, const TypeId* parameters, uint32_t count, uint8_t flags)
{
    return type_function_abi(table, result_type, parameters, count, flags, TYPE_FUNCTION_ABI_SYSV);
}

// Always inlined (#880), as profile-guided builds do; hot caller: check_input_declarator_step, once per parameter.
static inline __attribute__((always_inline)) TypeId type_adjust_parameter(TypeTable* table, TypeId type)
{
    TypeId result = type_strip_aligned(table, type);
    const uint8_t kind = type_kind(table, result);
    if (type_kind_is_array(kind))
    {
        result = type_pointer(table, type_element(table, result));
    }
    else if (kind == BUSTER_TYPE_KIND_FUNCTION)
    {
        result = type_pointer(table, result);
    }
    else
    {
        result = type;
    }
    return type_unqualified(result);
}

// The batch: eight requests at a time, their bases' qualifiers hoisted and their keys hashed in vectors, their
// then each looked up with type_index_find_vector. The requests it can't settle that way (a miss,
// a wide extent, a builtin complex type, a kind it doesn't hash, and an ALIGNED one that is its base) go to type_derive
// in request order before the next eight are hashed, so every request gets what the one-at-a-time path would give it
// at that point: lookups don't change the table, and each miss is added (or found, if an earlier one added it) in
// order. An ALIGNED request is its base when the base is complete with that alignment; the index can still hold an
// entry for it, made before its tag was completed, which is stale.
static void type_derive_batch(TypeTable* table, TypeKeys keys, uint32_t count, TypeId* results)
{
#if !BUSTER_HOST_VECTOR
    for (uint32_t i = 0; i < count; i += 1)
    {
        results[i] = type_derive(table, keys.kinds[i], keys.flags[i], keys.bases[i], keys.extents[i]);
    }
#else
    // A view's lookups pass its hidden types over, which the vector lookup doesn't: its requests go one at a time.
    const uint32_t vector_count = table->hidden_end != 0 ? 0 : count;
    for (uint32_t i = vector_count; i < count; i += 1)
    {
        results[i] = type_derive(table, keys.kinds[i], keys.flags[i], keys.bases[i], keys.extents[i]);
    }
    const __m512i one = _mm512_set1_epi64(1);
    const __m512i multiplier = _mm512_set1_epi64((long long)BUSTER_TYPE_HASH_MULTIPLIER);
    for (uint32_t first = 0; first < vector_count; first += BUSTER_TYPE_BATCH)
    {
        const uint32_t batch = vector_count - first < BUSTER_TYPE_BATCH ? vector_count - first : BUSTER_TYPE_BATCH;
        const __mmask8 lanes = (__mmask8)_bzhi_u32(0xff, batch);
        const __m512i kinds = _mm512_cvtepu8_epi64(_mm_maskz_loadu_epi8(lanes, keys.kinds + first));
        const __m512i flags = _mm512_and_si512(_mm512_cvtepu8_epi64(_mm_maskz_loadu_epi8(lanes, keys.flags + first)), _mm512_set1_epi64(BUSTER_TYPE_FLAG_KEY));
        const __m512i extents = _mm512_maskz_loadu_epi64(lanes, keys.extents + first);
        __m512i bases = _mm512_cvtepu32_epi64(_mm256_maskz_loadu_epi32(lanes, keys.bases + first));
        const __mmask8 hoisting = _mm512_test_epi64_mask(_mm512_srlv_epi64(_mm512_set1_epi64((long long)BUSTER_TYPE_HOISTING_KINDS), kinds), one);
        const __m512i qualifiers = _mm512_maskz_and_epi64(hoisting, bases, _mm512_set1_epi64(BUSTER_TYPE_QUALIFIERS));
        bases = _mm512_xor_si512(bases, qualifiers);
        const __m512i word = _mm512_or_si512(_mm512_or_si512(kinds, _mm512_slli_epi64(flags, 8)), _mm512_slli_epi64(bases, 32));
        __m512i hash = _mm512_mullo_epi64(_mm512_xor_si512(_mm512_set1_epi64((long long)BUSTER_TYPE_HASH_SEED), word), multiplier);
        hash = _mm512_xor_si512(hash, _mm512_srli_epi64(hash, 32));
        hash = _mm512_mullo_epi64(_mm512_xor_si512(hash, extents), multiplier);
        hash = _mm512_xor_si512(hash, _mm512_srli_epi64(hash, 32));
        hash = _mm512_mullo_epi64(hash, multiplier);
        hash = _mm512_xor_si512(hash, _mm512_srli_epi64(hash, 32));
        hash = _mm512_mullo_epi64(_mm512_xor_si512(hash, _mm512_srli_epi64(hash, 30)), _mm512_set1_epi64((long long)BUSTER_TYPE_HASH_FINISH0));
        hash = _mm512_mullo_epi64(_mm512_xor_si512(hash, _mm512_srli_epi64(hash, 27)), _mm512_set1_epi64((long long)BUSTER_TYPE_HASH_FINISH1));
        hash = _mm512_xor_si512(hash, _mm512_srli_epi64(hash, 31));
        uint64_t hashes[BUSTER_TYPE_BATCH];
        uint64_t normal[BUSTER_TYPE_BATCH];
        uint64_t hoisted[BUSTER_TYPE_BATCH];
        _mm512_storeu_si512(hashes, hash);
        _mm512_storeu_si512(normal, bases);
        _mm512_storeu_si512(hoisted, qualifiers);
        for (uint32_t lane = 0; lane < batch; lane += 1)
        {
            const uint32_t i = first + lane;
            const uint8_t kind = keys.kinds[i];
            const uint8_t key_flags = keys.flags[i] & BUSTER_TYPE_FLAG_KEY;
            const uint64_t extent = keys.extents[i];
            // A builtin complex type isn't in the index; type_derive returns it without a lookup.
            const bool builtin = kind == BUSTER_TYPE_KIND_COMPLEX && type_index((TypeId)normal[lane]) < BUSTER_TYPE_BUILTIN_COUNT;
            uint32_t found = 0;
            if (kind < BUSTER_TYPE_KIND_COUNT && (BUSTER_TYPE_HASHED_KINDS >> kind & 1) != 0 && kind != BUSTER_TYPE_KIND_FUNCTION && extent <= UINT32_MAX && !builtin &&
                key_flags == keys.flags[i])
            {
                const uint32_t key = type_slot_key(kind, key_flags, extent, hashes[lane]);
                found = type_index_find_vector(&table->index, key, (TypeId)normal[lane], (uint32_t)extent, hashes[lane]);
            }
            // An ALIGNED hit whose base is now complete with that alignment is stale: its tag was completed since the
            // entry was made, and type_derive gives the base. (A hit's base is valid: the entry was made of it.)
            if (found != 0 && kind == BUSTER_TYPE_KIND_ALIGNED && extent == type_alignment_log2(table, (TypeId)normal[lane]) &&
                type_is_complete(table, (TypeId)normal[lane]))
            {
                found = 0;
            }
            if (found != 0)
            {
                results[i] = (TypeId)found << BUSTER_TYPE_INDEX_SHIFT | (TypeId)hoisted[lane];
            }
            else
            {
                results[i] = type_derive(table, kind, keys.flags[i], keys.bases[i], extent);
            }
        }
    }
#endif
}

// ---------------------------------------------------------------------------------------------------------------------
// Tags

static TypeId type_tag(TypeTable* table, uint8_t kind, uint32_t name)
{
    TypeColumns* types = &table->types;
    TypeTags* tags = &table->tags;
    TypeId result = 0;
    if (kind != BUSTER_TYPE_KIND_STRUCT && kind != BUSTER_TYPE_KIND_UNION && kind != BUSTER_TYPE_KIND_ENUM)
    {
        type_set_error(table, TYPE_ERROR_OPERAND);
    }
    else if (types->count == types->capacity || tags->count == tags->capacity)
    {
        type_set_error(table, TYPE_ERROR_FULL);
    }
    else
    {
        const uint32_t index = types->count;
        const uint32_t tag = tags->count;
        result = (TypeId)index << BUSTER_TYPE_INDEX_SHIFT;
        types->kind[index] = kind;
        types->code[index] = 0;
        types->flags[index] = 0;
        types->alignment_log2[index] = 0;
        types->base[index] = 0;
        types->index[index] = tag;
        types->extent[index] = 0;
        types->size[index] = 0;
        types->hash[index] = 0;
        types->count += 1;
        tags->type[tag] = result;
        tags->name[tag] = name;
        tags->member_begin[tag] = 0;
        tags->member_count[tag] = 0;
        tags->completion[tag] = 0;
        tags->packed[tag] = 0;
        tags->ms_struct[tag] = 0;
        tags->pack_log2[tag] = 0;
        tags->alignment_log2[tag] = 0;
        tags->lookup[tag] = 0;
        tags->lookup_log2[tag] = 0;
        tags->count += 1;
        const TypeChange change = {
            .operation = BUSTER_TYPE_CHANGE_TAG,
            .kind = kind,
            .type = result,
            .name = name,
            .predefined = table->changes != 0 ? table->changes->predefined : 0,
            .tag = tag,
        };
        type_change_record(table, change, 0, 0, 0, 0);
    }
    return result;
}

// The width of a bit-field's type, as clang's getIntWidth gives it: bool's is 1, a _BitInt's its N and an enum's its
// underlying type's; 0 for a type that can't be a bit-field's (not an integer type, or an incomplete enum).
static uint64_t type_integer_width(const TypeTable* table, TypeId type)
{
    TypeId stripped = type_strip_aligned(table, type);
    uint8_t kind = type_kind(table, stripped);
    if (kind == BUSTER_TYPE_KIND_ENUM && type_is_complete(table, stripped))
    {
        stripped = table->types.base[type_index(stripped)];
        kind = type_kind(table, stripped);
    }
    uint64_t result = 0;
    if (kind == BUSTER_TYPE_KIND_INTEGER)
    {
        result = type_unqualified(stripped) == BUSTER_TYPE_BOOL ? 1 : table->types.size[type_index(stripped)] * 8;
    }
    else if (kind == BUSTER_TYPE_KIND_BIT_INT)
    {
        result = table->types.extent[type_index(stripped)];
    }
    return result;
}

// Checks a member: its type complete (a flexible array member, the last member or any of a union's, and not a
// bit-field, excepted), not a function, void or variably modified; a bit-field's type an integer type, its width at most its type's, and 0 only
// unnamed; its alignment in range.
static TypeError type_check_member(const TypeTable* table, const TypeMember* member, bool last, bool* flexible)
{
    TypeError result = TYPE_ERROR_NONE;
    *flexible = false;
    if (!type_is_valid(table, member->type))
    {
        result = TYPE_ERROR_OPERAND;
    }
    else
    {
        const uint8_t kind = type_kind(table, type_strip_aligned(table, member->type));
        const bool bit_field = member->bit_width != BUSTER_TYPE_NOT_BIT_FIELD;
        // A member with extended qualifiers is clang's "field may not be qualified with an address space" (#1447).
        if (kind == BUSTER_TYPE_KIND_FUNCTION || kind == BUSTER_TYPE_KIND_VOID || (table->types.flags[type_index(member->type)] & BUSTER_TYPE_FLAG_VARIABLY_MODIFIED) != 0 ||
            type_address_space(type_object_qualifiers(table, member->type)) != 0)
        {
            result = TYPE_ERROR_MEMBER;
        }
        else if (!type_is_complete(table, member->type))
        {
            *flexible = kind == BUSTER_TYPE_KIND_INCOMPLETE_ARRAY && last && !bit_field;
            result = *flexible ? TYPE_ERROR_NONE : TYPE_ERROR_MEMBER;
        }
        if (result == TYPE_ERROR_NONE && bit_field)
        {
            const uint64_t width = type_integer_width(table, member->type);
            if (width == 0 || member->bit_width > width || (member->bit_width == 0 && member->name != 0))
            {
                result = TYPE_ERROR_BIT_FIELD;
            }
        }
        if (result == TYPE_ERROR_NONE && member->alignment_log2 > BUSTER_TYPE_MAX_ALIGNMENT_LOG2 + 1)
        {
            result = TYPE_ERROR_ALIGNMENT;
        }
    }
    return result;
}

// The member lookup (TypeLookup, #591): each record's index, built when its members are written; the kernels that scan
// a record and probe an index, compared in vectors, and their twins, one name at a time; and the lookup that takes them,
// with the walk over the members for a record whose index didn't fit.

// The tag of the struct or union that member `member` is an anonymous member of, or 0 if it isn't one.
static uint32_t type_lookup_anonymous(const TypeTable* table, uint32_t member)
{
    const TypeId inner = type_strip_aligned(table, table->members.type[member]);
    const uint8_t kind = type_kind(table, inner);
    const bool anonymous = table->members.name[member] == 0 && (table->members.flags[member] & BUSTER_TYPE_MEMBER_BIT_FIELD) == 0 &&
                           (kind == BUSTER_TYPE_KIND_STRUCT || kind == BUSTER_TYPE_KIND_UNION);
    return anonymous ? table->types.index[type_index(inner)] : 0;
}

// A name's first bucket among an index's 2^log2: the top bits of its Fibonacci hash, which spread an identifier table's
// consecutive ids; 0 for one bucket.
static uint32_t type_lookup_bucket(uint32_t name, uint32_t log2)
{
    const uint64_t hash = (uint64_t)name * UINT64_C(11400714819323198485);
    return (uint32_t)((hash >> 32) >> (32 - log2));
}

// The member of a scanned record (at most BUSTER_TYPE_LOOKUP_SCAN members, none an anonymous struct or union) that `name`,
// not 0, names, or 0: all its names compared at once, the lanes past its members masked off, so that the load reads
// nothing past them. The first in declaration order, should two share the name.
static uint32_t type_lookup_scan(const uint32_t* names, uint32_t begin, uint32_t count, uint32_t name)
{
    const __mmask16 lanes = (__mmask16)((1u << count) - 1);
    const __m512i members = _mm512_maskz_loadu_epi32(lanes, names + begin);
    const __mmask16 found = _mm512_mask_cmpeq_epi32_mask(lanes, members, _mm512_set1_epi32((int)name));
    return found != 0 ? begin + (uint32_t)__builtin_ctz(found) : 0;
}

// type_lookup_scan's twin: one name at a time.
static uint32_t type_lookup_scan_scalar(const uint32_t* names, uint32_t begin, uint32_t count, uint32_t name)
{
    uint32_t result = 0;
    for (uint32_t member = begin; result == 0 && member < begin + count; member += 1)
    {
        result = names[member] == name ? member : 0;
    }
    return result;
}

// type_lookup_probe's twin: one slot at a time, until the name or an empty slot. A bucket's names fill its slots in
// order (type_lookup_insert), so the first empty slot follows every name it holds, as a whole bucket's compare finds.
static uint32_t type_lookup_probe_scalar(const TypeLookupBucket* buckets, uint32_t first, uint32_t log2, uint32_t name)
{
    const uint32_t mask = (1u << log2) - 1;
    uint32_t bucket = type_lookup_bucket(name, log2);
    uint32_t result = 0;
    bool searching = true;
    for (uint32_t probes = 0; searching && probes <= mask; probes += 1)
    {
        const TypeLookupBucket* at = buckets + first + bucket;
        for (uint32_t slot = 0; searching && slot < BUSTER_TYPE_LOOKUP_SLOTS; slot += 1)
        {
            result = at->names[slot] == name ? at->members[slot] : 0;
            searching = at->names[slot] != name && at->names[slot] != 0;
        }
        bucket = (bucket + 1) & mask;
    }
    return result;
}

// The member that an index (its first bucket, and 2^log2 of them) gives `name`, not 0, or 0: from the name's bucket on,
// each bucket's names compared at once, until one has the name or an empty slot. An index is at most half full, so a
// probe takes one bucket, or a few: never more than the index has.
static uint32_t type_lookup_probe(const TypeLookupBucket* buckets, uint32_t first, uint32_t log2, uint32_t name)
{
#if !BUSTER_HOST_VECTOR
    return type_lookup_probe_scalar(buckets, first, log2, name);
#else
    const uint32_t mask = (1u << log2) - 1;
    const __m256i target = _mm256_set1_epi32((int)name);
    uint32_t bucket = type_lookup_bucket(name, log2);
    uint32_t result = 0;
    bool searching = true;
    for (uint32_t probes = 0; searching && probes <= mask; probes += 1)
    {
        const TypeLookupBucket* at = buckets + first + bucket;
        const __m256i names = _mm256_load_si256((const __m256i*)at->names);
        const __mmask8 found = _mm256_cmpeq_epi32_mask(names, target);
        result = found != 0 ? at->members[__builtin_ctz(found)] : 0;
        searching = found == 0 && _mm256_testn_epi32_mask(names, names) == 0;
        bucket = (bucket + 1) & mask;
    }
    return result;
#endif
}

// Gives `name` the member `member` in an index, unless an earlier member has it already: the index keeps the first
// member in declaration order that finds each name.
static void type_lookup_insert(TypeLookupBucket* buckets, uint32_t first, uint32_t log2, uint32_t name, uint32_t member)
{
    const uint32_t mask = (1u << log2) - 1;
    uint32_t bucket = type_lookup_bucket(name, log2);
    bool searching = true;
    for (uint32_t probes = 0; searching && probes <= mask; probes += 1)
    {
        TypeLookupBucket* at = buckets + first + bucket;
        uint32_t slot = 0;
        while (slot < BUSTER_TYPE_LOOKUP_SLOTS && at->names[slot] != 0 && at->names[slot] != name)
        {
            slot += 1;
        }
        if (slot < BUSTER_TYPE_LOOKUP_SLOTS && at->names[slot] == 0)
        {
            at->names[slot] = name;
            at->members[slot] = member;
        }
        searching = slot == BUSTER_TYPE_LOOKUP_SLOTS;
        bucket = (bucket + 1) & mask;
    }
}

// The names a record's lookup finds, an upper bound for its index's: its named members' if it is scanned, and its index's
// otherwise (a walking record's aren't counted).
static uint64_t type_lookup_names(const TypeTable* table, uint32_t tag)
{
    const uint32_t lookup = table->tags.lookup[tag];
    uint64_t result = 0;
    if (lookup == 0)
    {
        const uint32_t begin = table->tags.member_begin[tag];
        for (uint32_t member = begin; member < begin + table->tags.member_count[tag]; member += 1)
        {
            result += table->members.name[member] != 0;
        }
    }
    else if (lookup != BUSTER_TYPE_LOOKUP_WALK)
    {
        const TypeLookupBucket* buckets = table->lookup.buckets + lookup;
        const uint64_t slots = (uint64_t)BUSTER_TYPE_LOOKUP_SLOTS << table->tags.lookup_log2[tag];
        for (uint64_t slot = 0; slot < slots; slot += 1)
        {
            result += buckets[slot / BUSTER_TYPE_LOOKUP_SLOTS].names[slot % BUSTER_TYPE_LOOKUP_SLOTS] != 0;
        }
    }
    return result;
}

// A record's lookup, once its members are written. One of at most BUSTER_TYPE_LOOKUP_SCAN members, none of them an
// anonymous struct or union whose record finds a name, is scanned. Any other gets an index of at least twice as many
// slots as the names its members find, filled in declaration order, each anonymous member's record's names from that
// record's lookup; or, where the buckets left can't hold the index or an anonymous member's record walks, a walk. A record
// is built once, when it is defined, so this stays scalar: the lookups that take its index are what every access runs.
static void type_lookup_build(TypeTable* table, uint32_t tag)
{
    const TypeMembers* members = &table->members;
    const uint32_t begin = table->tags.member_begin[tag];
    const uint32_t count = table->tags.member_count[tag];
    uint64_t names = 0;
    bool anonymous = false;
    bool walk = false;
    for (uint32_t member = begin; member < begin + count; member += 1)
    {
        const uint32_t inner = type_lookup_anonymous(table, member);
        names += members->name[member] != 0 ? 1 : inner != 0 ? type_lookup_names(table, inner) : 0;
        anonymous |= inner != 0;
        walk |= inner != 0 && table->tags.lookup[inner] == BUSTER_TYPE_LOOKUP_WALK;
    }
    uint32_t lookup = 0;
    uint32_t log2 = 0;
    if (walk)
    {
        lookup = BUSTER_TYPE_LOOKUP_WALK;
    }
    else if (count > BUSTER_TYPE_LOOKUP_SCAN || (anonymous && names != 0))
    {
        while (log2 < 31 && ((uint64_t)BUSTER_TYPE_LOOKUP_SLOTS << log2) < names * 2)
        {
            log2 += 1;
        }
        const uint64_t buckets = UINT64_C(1) << log2;
        TypeLookup* index = &table->lookup;
        if (((uint64_t)BUSTER_TYPE_LOOKUP_SLOTS << log2) >= names * 2 && buckets <= index->capacity - index->count)
        {
            lookup = index->count;
            index->count += (uint32_t)buckets;
            memset(index->buckets + lookup, 0, buckets * sizeof(TypeLookupBucket));
            for (uint32_t member = begin; member < begin + count; member += 1)
            {
                const uint32_t inner = type_lookup_anonymous(table, member);
                const uint32_t inner_lookup = inner != 0 ? table->tags.lookup[inner] : 0;
                if (members->name[member] != 0)
                {
                    type_lookup_insert(index->buckets, lookup, log2, members->name[member], member);
                }
                else if (inner != 0 && inner_lookup == 0)
                {
                    const uint32_t inner_begin = table->tags.member_begin[inner];
                    for (uint32_t at = inner_begin; at < inner_begin + table->tags.member_count[inner]; at += 1)
                    {
                        if (members->name[at] != 0)
                        {
                            type_lookup_insert(index->buckets, lookup, log2, members->name[at], member);
                        }
                    }
                }
                else if (inner != 0)
                {
                    const uint64_t slots = (uint64_t)BUSTER_TYPE_LOOKUP_SLOTS << table->tags.lookup_log2[inner];
                    for (uint64_t slot = 0; slot < slots; slot += 1)
                    {
                        const uint32_t name = index->buckets[inner_lookup + slot / BUSTER_TYPE_LOOKUP_SLOTS].names[slot % BUSTER_TYPE_LOOKUP_SLOTS];
                        if (name != 0)
                        {
                            type_lookup_insert(index->buckets, lookup, log2, name, member);
                        }
                    }
                }
            }
        }
        else
        {
            lookup = BUSTER_TYPE_LOOKUP_WALK;
            log2 = 0;
        }
    }
    table->tags.lookup[tag] = lookup;
    table->tags.lookup_log2[tag] = (uint8_t)log2;
}

static bool type_record_define(TypeTable* table, TypeId record, const TypeMember* members, uint32_t count, TypeRecordAttributes attributes)
{
    TypeError error = TYPE_ERROR_NONE;
    uint8_t flags = BUSTER_TYPE_FLAG_DEFINED;
    if (!type_is_valid(table, record) || (type_kind(table, record) != BUSTER_TYPE_KIND_STRUCT && type_kind(table, record) != BUSTER_TYPE_KIND_UNION))
    {
        error = TYPE_ERROR_TAG;
    }
    else if ((table->types.flags[type_index(record)] & (BUSTER_TYPE_FLAG_DEFINED | BUSTER_TYPE_FLAG_COMPLETE)) != 0)
    {
        error = TYPE_ERROR_TAG;
    }
    else if (attributes.pack_log2 > BUSTER_TYPE_MAX_ALIGNMENT_LOG2 + 1 || attributes.alignment_log2 > BUSTER_TYPE_MAX_ALIGNMENT_LOG2 + 1)
    {
        error = TYPE_ERROR_ALIGNMENT;
    }
    else if (count > table->members.capacity - table->members.count)
    {
        error = TYPE_ERROR_FULL;
    }
    // A union's flexible array members may be any of its members, as GNU's extension, which clang takes, has them: each
    // is at offset 0, of size 0 and its element's alignment.
    const bool any_flexible = error == TYPE_ERROR_NONE && type_kind(table, record) == BUSTER_TYPE_KIND_UNION;
    for (uint32_t i = 0; error == TYPE_ERROR_NONE && i < count; i += 1)
    {
        bool flexible = false;
        error = type_check_member(table, &members[i], any_flexible || i + 1 == count, &flexible);
        if (error == TYPE_ERROR_NONE && attributes.ms_struct && members[i].bit_width != BUSTER_TYPE_NOT_BIT_FIELD)
        {
            const uint64_t size = type_size(table, members[i].type);
            error = size != 0 && (size & (size - 1)) == 0 ? TYPE_ERROR_NONE : TYPE_ERROR_MS_STRUCT_ALIGNMENT;
        }
        flags |= flexible ? BUSTER_TYPE_FLAG_FLEXIBLE : 0;
    }
    if (error == TYPE_ERROR_NONE)
    {
        TypeMembers* columns = &table->members;
        const uint32_t tag = table->types.index[type_index(record)];
        const uint32_t begin = columns->count;
        for (uint32_t i = 0; i < count; i += 1)
        {
            const TypeMember* member = &members[i];
            const bool bit_field = member->bit_width != BUSTER_TYPE_NOT_BIT_FIELD;
            columns->name[begin + i] = member->name;
            columns->type[begin + i] = member->type;
            columns->bit_width[begin + i] = bit_field ? member->bit_width : 0;
            columns->alignment_log2[begin + i] = member->alignment_log2;
            columns->flags[begin + i] = (uint8_t)((member->packed ? BUSTER_TYPE_MEMBER_PACKED : 0) | (bit_field ? BUSTER_TYPE_MEMBER_BIT_FIELD : 0) |
                                                  ((member->count_kind << BUSTER_TYPE_MEMBER_COUNT_SHIFT) & BUSTER_TYPE_MEMBER_COUNT_MASK));
            columns->offset[begin + i] = 0;
            columns->count_name[begin + i] = member->count_kind != BUSTER_TYPE_COUNT_NONE ? member->count_name : 0;
        }
        columns->count += count;
        table->tags.member_begin[tag] = begin;
        table->tags.member_count[tag] = count;
        table->tags.packed[tag] = attributes.packed ? 1 : 0;
        table->tags.ms_struct[tag] = attributes.ms_struct ? 1 : 0;
        table->tags.pack_log2[tag] = attributes.pack_log2;
        table->tags.alignment_log2[tag] = attributes.alignment_log2;
        type_lookup_build(table, tag);
        table->types.flags[type_index(record)] |= flags;
        const TypeChange change = {
            .operation = BUSTER_TYPE_CHANGE_DEFINE,
            .type = record,
            .attributes = attributes,
            .tag = tag,
        };
        type_change_record(table, change, 0, 0, members, count);
    }
    else
    {
        type_set_error(table, error);
    }
    return error == TYPE_ERROR_NONE;
}

static void type_enum_value_bits(int64_t value, bool is_unsigned, uint32_t* negative_bits, uint32_t* positive_bits)
{
    // clang's computeEnumBits: a value's active bits (at least 1, so that 0 counts), or a negative value's significant
    // bits (its active bits as a signed number).
    if (is_unsigned || value >= 0)
    {
        const uint64_t magnitude = (uint64_t)value;
        const uint32_t active = magnitude == 0 ? 1 : 64 - (uint32_t)__builtin_clzll(magnitude);
        *positive_bits = active > *positive_bits ? active : *positive_bits;
    }
    else
    {
        const uint64_t inverted = ~(uint64_t)value;
        const uint32_t significant = 65 - (inverted == 0 ? 64 : (uint32_t)__builtin_clzll(inverted));
        *negative_bits = significant > *negative_bits ? significant : *negative_bits;
    }
}

// clang's computeBestEnumTypes on x86-64, for C: the smallest of int, long and long long (signed char and short too
// when packed) that holds every value if one is negative, else of unsigned int, unsigned long and unsigned long long
// (unsigned char and unsigned short when packed). An enum without enumerators counts as one with the value 0.
static TypeId type_enum_underlying(bool packed, uint32_t negative_bits, uint32_t positive_bits)
{
    TypeId result = 0;
    if (negative_bits == 0 && positive_bits == 0)
    {
        positive_bits = 1;
    }
    if (negative_bits != 0)
    {
        if (packed && negative_bits <= 8 && positive_bits < 8)
        {
            result = BUSTER_TYPE_SCHAR;
        }
        else if (packed && negative_bits <= 16 && positive_bits < 16)
        {
            result = BUSTER_TYPE_SHORT;
        }
        else if (negative_bits <= 32 && positive_bits < 32)
        {
            result = BUSTER_TYPE_INT;
        }
        else if (negative_bits <= 64 && positive_bits < 64)
        {
            result = BUSTER_TYPE_LONG;
        }
        else
        {
            result = BUSTER_TYPE_LLONG;
        }
    }
    else if (packed && positive_bits <= 8)
    {
        result = BUSTER_TYPE_UCHAR;
    }
    else if (packed && positive_bits <= 16)
    {
        result = BUSTER_TYPE_USHORT;
    }
    else if (positive_bits <= 32)
    {
        result = BUSTER_TYPE_UINT;
    }
    else if (positive_bits <= 64)
    {
        result = BUSTER_TYPE_ULONG;
    }
    else
    {
        result = BUSTER_TYPE_ULLONG;
    }
    return result;
}

// The underlying type loses its qualifiers and ALIGNED, as clang's (with a warning, in C23) and its getTypeInfo for an
// enum do; it may be bool but not a _BitInt or another enum. The enum's size is its underlying type's, and its alignment
// the aligned attribute's if it has one, which may lower it.
static bool type_enum_complete(TypeTable* table, TypeId enumeration, TypeId underlying, uint8_t alignment_log2)
{
    TypeError error = TYPE_ERROR_NONE;
    if (!type_is_valid(table, enumeration) || type_kind(table, enumeration) != BUSTER_TYPE_KIND_ENUM || type_is_complete(table, enumeration))
    {
        error = TYPE_ERROR_TAG;
    }
    else if (!type_is_valid(table, underlying) || type_kind(table, type_strip_aligned(table, underlying)) != BUSTER_TYPE_KIND_INTEGER)
    {
        error = TYPE_ERROR_UNDERLYING;
    }
    else if (alignment_log2 > BUSTER_TYPE_MAX_ALIGNMENT_LOG2 + 1)
    {
        error = TYPE_ERROR_ALIGNMENT;
    }
    if (error == TYPE_ERROR_NONE)
    {
        TypeColumns* types = &table->types;
        const uint32_t index = type_index(enumeration);
        const TypeId integer = type_unqualified(type_strip_aligned(table, underlying));
        types->base[index] = integer;
        types->code[index] = types->code[type_index(integer)];
        types->size[index] = types->size[type_index(integer)];
        types->alignment_log2[index] = alignment_log2 != 0 ? (uint8_t)(alignment_log2 - 1) : types->alignment_log2[type_index(integer)];
        types->flags[index] |= BUSTER_TYPE_FLAG_COMPLETE;
        table->tags.alignment_log2[types->index[index]] = alignment_log2;
        table->tags.completions += 1;
        table->tags.completion[types->index[index]] = table->tags.completions;
        const TypeChange change = {
            .operation = BUSTER_TYPE_CHANGE_ENUM,
            .alignment_log2 = alignment_log2,
            .base = underlying,
            .type = enumeration,
            .tag = types->index[index],
        };
        type_change_record(table, change, 0, 0, 0, 0);
    }
    else
    {
        type_set_error(table, error);
    }
    return error == TYPE_ERROR_NONE;
}

// A fixed enum's aligned attribute after its body (#1447), where nothing has asked its layout since its completion at the
// body's `{`: clang applies it then (ASTContext::getTypeInfo of an enum takes its declaration's largest alignment, and
// memoizes it at the first query, which the caller rules out). The completion's record, the table's last ENUM of the
// tag, takes the alignment too, so that a replay completes it so from the start, which nothing could tell apart.
static __attribute__((unused)) bool type_enum_realign(TypeTable* table, TypeId enumeration, uint8_t alignment_log2)
{
    const bool valid = type_is_valid(table, enumeration) && type_kind(table, enumeration) == BUSTER_TYPE_KIND_ENUM && type_is_complete(table, enumeration) &&
                       alignment_log2 != 0 && alignment_log2 <= BUSTER_TYPE_MAX_ALIGNMENT_LOG2 + 1;
    if (valid)
    {
        const uint32_t index = type_index(enumeration);
        const uint32_t tag = table->types.index[index];
        table->types.alignment_log2[index] = (uint8_t)(alignment_log2 - 1);
        table->tags.alignment_log2[tag] = alignment_log2;
        TypeChanges* changes = table->changes;
        bool amended = changes == 0 || changes->full;
        for (uint32_t i = amended ? 0 : changes->count; !amended && i-- > 0;)
        {
            TypeChange* change = changes->items + i;
            amended = change->operation == BUSTER_TYPE_CHANGE_ENUM && change->tag == tag;
            change->alignment_log2 = amended ? alignment_log2 : change->alignment_log2;
        }
        // A record without the completion can't be replayed.
        if (!amended)
        {
            changes->full = true;
        }
    }
    else
    {
        type_set_error(table, TYPE_ERROR_ALIGNMENT);
    }
    return valid;
}

// The lookup: through each record's own lookup, a scan or a probe of its index, which gives the member that finds the
// name, and into that member's record when it is an anonymous member, until a named member has the name. Each step goes
// into a record that was complete before the record that holds it was defined, so there are never more steps than
// tags. A record whose index didn't fit is walked, from there on. `scalar`, a constant in each of its two callers, picks
// the kernels' twins.
static inline __attribute__((always_inline)) uint32_t type_lookup_member_with(const TypeTable* table, TypeId record, uint32_t name, uint64_t* offset,
                                                                               uint32_t* qualifiers, bool scalar)
{
    TypeId current = type_strip_aligned(table, record);
    const uint8_t kind = type_kind(table, current);
    uint32_t inherited = type_qualifiers(current);
    uint64_t at = 0;
    uint32_t result = 0;
    bool searching = name != 0 && (kind == BUSTER_TYPE_KIND_STRUCT || kind == BUSTER_TYPE_KIND_UNION);
    for (uint32_t steps = 0; searching && steps < table->tags.count; steps += 1)
    {
        const uint32_t tag = table->types.index[type_index(current)];
        const uint32_t lookup = table->tags.lookup[tag];
        if (lookup == BUSTER_TYPE_LOOKUP_WALK)
        {
            uint64_t rest = 0;
            uint32_t more = 0;
            result = type_lookup_walk(table, current, name, &rest, &more);
            at += rest;
            inherited |= more;
            searching = false;
        }
        else
        {
            const uint32_t begin = table->tags.member_begin[tag];
            const uint32_t count = table->tags.member_count[tag];
            const uint32_t log2 = table->tags.lookup_log2[tag];
            const uint32_t member = lookup == 0 ? (scalar ? type_lookup_scan_scalar(table->members.name, begin, count, name) : type_lookup_scan(table->members.name, begin, count, name)) :
                                    scalar ? type_lookup_probe_scalar(table->lookup.buckets, lookup, log2, name) : type_lookup_probe(table->lookup.buckets, lookup, log2, name);
            // Member 0, which no lookup finds, is zero in every column, so that its name is never the one sought.
            at += table->members.offset[member];
            result = table->members.name[member] == name ? member : 0;
            searching = member != 0 && result == 0;
            current = type_strip_aligned(table, table->members.type[member]);
            inherited |= searching ? type_qualifiers(current) : 0;
        }
    }
    const bool found = result != 0 && result != BUSTER_TYPE_MEMBER_UNKNOWN;
    *offset = found ? at : 0;
    *qualifiers = found ? inherited : 0;
    return result;
}

// Always inlined (#880), as profile-guided builds do; hot caller: check_action_member.
static inline __attribute__((always_inline)) uint32_t type_lookup_member(const TypeTable* table, TypeId record, uint32_t name, uint64_t* offset, uint32_t* qualifiers)
{
    return type_lookup_member_with(table, record, name, offset, qualifiers, BUSTER_SCALAR_PATH(false));
}

static uint32_t type_lookup_member_scalar(const TypeTable* table, TypeId record, uint32_t name, uint64_t* offset, uint32_t* qualifiers)
{
    return type_lookup_member_with(table, record, name, offset, qualifiers, true);
}

// The walk: depth first, in declaration order, with an explicit stack of the anonymous members still to search, as clang's
// lookup through anonymous structs and unions finds the first member with the name: for a record whose index didn't fit,
// and as the tests' oracle. It enters each record once, however many anonymous members hold it: a record entered before
// and searched to its end has no member with the name. The records entered are a sparse set over type indices, which
// nothing clears: `entered` lists them, and `places` gives each its place in the list, where an index that isn't entered
// has whatever value, never pointing at it.
static uint32_t type_lookup_walk(const TypeTable* table, TypeId record, uint32_t name, uint64_t* offset, uint32_t* qualifiers)
{
    typedef struct TypeMemberFrame TypeMemberFrame;
    struct TypeMemberFrame
    {
        uint32_t next;
        uint32_t end;
        uint32_t qualifiers;
        uint64_t offset;
    };
    Scratch scratch = scratch_begin(0);
    const TypeId root = type_strip_aligned(table, record);
    const uint8_t kind = type_kind(table, root);
    const bool searching = name != 0 && (kind == BUSTER_TYPE_KIND_STRUCT || kind == BUSTER_TYPE_KIND_UNION);
    // A frame for each record entered, at most one a tag.
    const uint32_t room = table->tags.count;
    TypeMemberFrame* stack = searching ? BUSTER_ARENA_PUSH_ARRAY(scratch.arena, TypeMemberFrame, room) : 0;
    uint32_t* entered = stack != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch.arena, uint32_t, room) : 0;
    uint32_t* places = entered != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch.arena, uint32_t, table->types.count) : 0;
    uint32_t result = searching && places == 0 ? BUSTER_TYPE_MEMBER_UNKNOWN : 0;
    uint32_t depth = 0;
    uint32_t count = 0;
    uint64_t found_offset = 0;
    uint32_t found_qualifiers = 0;
    if (searching && result == 0)
    {
        const uint32_t index = type_index(root);
        const uint32_t tag = table->types.index[index];
        places[index] = count;
        entered[count++] = index;
        stack[depth++] = (TypeMemberFrame){
            .next = table->tags.member_begin[tag],
            .end = table->tags.member_begin[tag] + table->tags.member_count[tag],
            .qualifiers = type_qualifiers(root),
            .offset = 0,
        };
    }
    while (result == 0 && depth != 0)
    {
        TypeMemberFrame* top = &stack[depth - 1];
        if (top->next == top->end)
        {
            depth -= 1;
        }
        else
        {
            const uint32_t member = top->next++;
            const uint64_t member_offset = top->offset + table->members.offset[member];
            if (table->members.name[member] == name)
            {
                result = member;
                found_offset = member_offset;
                found_qualifiers = top->qualifiers;
            }
            else if (type_lookup_anonymous(table, member) != 0)
            {
                const TypeId inner = type_strip_aligned(table, table->members.type[member]);
                const uint32_t index = type_index(inner);
                const bool seen = places[index] < count && entered[places[index]] == index;
                if (!seen)
                {
                    const uint32_t tag = table->types.index[index];
                    places[index] = count;
                    entered[count++] = index;
                    stack[depth++] = (TypeMemberFrame){
                        .next = table->tags.member_begin[tag],
                        .end = table->tags.member_begin[tag] + table->tags.member_count[tag],
                        .qualifiers = top->qualifiers | type_qualifiers(inner),
                        .offset = member_offset,
                    };
                }
            }
        }
    }
    scratch_end(scratch);
    *offset = found_offset;
    *qualifiers = found_qualifiers;
    return result;
}

static uint32_t type_find_member(const TypeTable* table, TypeId record, uint32_t name, uint64_t* offset)
{
    uint32_t qualifiers = 0;
    const uint32_t result = type_lookup_member(table, record, name, offset, &qualifiers);
    return result != BUSTER_TYPE_MEMBER_UNKNOWN ? result : 0;
}

static TypeId type_builtin_va_list(TypeTable* table, const uint32_t* names)
{
    TypeId result = 0;
    const TypeId record = type_tag(table, BUSTER_TYPE_KIND_STRUCT, names[0]);
    const TypeId pointer = type_pointer(table, BUSTER_TYPE_VOID);
    if (record != 0 && pointer != 0)
    {
        const TypeId member_types[4] = {
            BUSTER_TYPE_UINT,
            BUSTER_TYPE_UINT,
            pointer,
            pointer,
        };
        TypeMember members[4];
        for (uint32_t i = 0; i < 4; i += 1)
        {
            members[i] = (TypeMember){
                .name = names[i + 1],
                .type = member_types[i],
                .bit_width = BUSTER_TYPE_NOT_BIT_FIELD,
                .alignment_log2 = 0,
                .packed = false,
            };
        }
        const TypeRecordAttributes attributes = { 0 };
        if (type_complete_record(table, record, members, 4, attributes))
        {
            result = type_array(table, record, 1);
        }
    }
    return result;
}

// ---------------------------------------------------------------------------------------------------------------------
// Layout

// The record layout of C on x86-64 Linux, as clang's ItaniumRecordLayoutBuilder computes it (its LayoutField,
// LayoutBitField and FinishLayout, including ms_struct, without AIX and C++ records). In bits, from a data size
// (where the next member may start; a multiple of 8), the bits of its last byte that bit-fields left unfilled, and a
// size; the record's alignment starts at its aligned attribute's, or 1 byte. For each member:
//   - Any member: packed if the record or the member is. #pragma pack's value P caps alignments (aligned's too),
//     where it applies. A union's members all start at 0.
//   - Not a bit-field: its alignment is its type's raised by its explicit alignment, or 1 byte raised by its explicit
//     alignment if packed, each capped by P; it starts at the data size aligned up to that, and a flexible array
//     member takes no room. The unfilled bits are lost.
//   - A bit-field of width W, whose type has size S and alignment A: it would start at the data size less the unfilled
//     bits. Its alignment is A (1 bit if packed and W isn't 0) raised by its explicit alignment E; for W other than 0,
//     P caps it (the unpacked alignment, raised by E, if packed). It is aligned up to that alignment if W is 0, or
//     if it would cross a boundary of S bits from there and P isn't set; else, with E (and E at most P), it is aligned
//     to E. An unnamed bit-field doesn't raise the record's alignment. The data size becomes its end rounded up to a
//     byte (a union's: its width rounded up, if larger), and the unfilled bits the difference.
//   - The record's alignment rises to each member's alignment in whole bytes (so a packed bit-field's 1 bit never
//     raises it), and its size to the data size; at the end the size is rounded up to the alignment.
//   - ms_struct instead allocates whole bit-field storage units, shared only by adjacent fields of the same size
//     that fit. Zero-width fields after non-bit-fields are ignored; other zero-width fields align the next unit.
//     Unnamed fields raise alignment too. A union reserves the entire unit but its bit-fields never raise alignment.

// A member's layout inputs in bits, as the scalar and the vector layout both read them.
typedef struct TypeLayoutInput TypeLayoutInput;
struct TypeLayoutInput
{
    uint64_t size;
    uint64_t alignment;
    uint64_t explicit_alignment;
    uint32_t width;
    bool bit_field;
    bool packed;
    bool named;
};

// The columns the layouts read and write. Each layout copies them into a local of its own, so that the compiler keeps
// them in registers across the offsets' stores, which under -fno-strict-aliasing could otherwise change the table's
// pointers. (A copy passed by value is built on the stack and read back across its stores, which costs more.)
typedef struct TypeLayoutColumns TypeLayoutColumns;
struct TypeLayoutColumns
{
    const uint8_t* kind;
    const TypeId* base;
    const uint64_t* size;
    const uint8_t* alignment_log2;
    const TypeId* member_type;
    const uint8_t* member_alignment_log2;
    const uint32_t* bit_width;
    const uint8_t* member_flags;
    const uint32_t* name;
    uint64_t* offset;
};

static TypeLayoutColumns type_layout_columns(const TypeTable* table)
{
    TypeLayoutColumns result = {
        .kind = table->types.kind,
        .base = table->types.base,
        .size = table->types.size,
        .alignment_log2 = table->types.alignment_log2,
        .member_type = table->members.type,
        .member_alignment_log2 = table->members.alignment_log2,
        .bit_width = table->members.bit_width,
        .member_flags = table->members.flags,
        .name = table->members.name,
        .offset = table->members.offset,
    };
    return result;
}

#define BUSTER_INLINE static inline __attribute__((always_inline))

// Microsoft record layout raises a complete builtin field's alignment to its element's size, including through
// aligned typedefs and fixed arrays. Other types keep their ABI alignment, and flexible arrays keep theirs too.
static uint32_t type_layout_ms_alignment(const TypeLayoutColumns* columns, uint32_t index)
{
    const uint32_t alignment = columns->alignment_log2[index];
    uint32_t element = index;
    while (columns->kind[element] == BUSTER_TYPE_KIND_ALIGNED || columns->kind[element] == BUSTER_TYPE_KIND_QUALIFIED || columns->kind[element] == BUSTER_TYPE_KIND_ARRAY)
    {
        element = type_index(columns->base[element]);
    }
    const uint8_t kind = columns->kind[element];
    const uint64_t size = columns->size[element];
    const bool builtin = kind == BUSTER_TYPE_KIND_INTEGER || kind == BUSTER_TYPE_KIND_FLOATING || kind == BUSTER_TYPE_KIND_NULLPTR;
    const uint32_t width = builtin && size != 0 && (size & (size - 1)) == 0 ? type_log2(size) : 0;
    const uint32_t result = width > alignment ? width : alignment;
    return result;
}

// A member's inputs: its type's size (an aligned typedef's base's) and alignment, in bits.
BUSTER_INLINE TypeLayoutInput type_layout_input(const TypeLayoutColumns* columns, uint32_t member, bool ms_struct)
{
    const uint32_t index = columns->member_type[member] >> BUSTER_TYPE_INDEX_SHIFT;
    const uint32_t resolved = columns->kind[index] == BUSTER_TYPE_KIND_ALIGNED ? columns->base[index] >> BUSTER_TYPE_INDEX_SHIFT : index;
    const uint8_t alignment_log2 = columns->member_alignment_log2[member];
    const uint8_t flags = columns->member_flags[member];
    TypeLayoutInput result = {
        .size = columns->size[resolved] * 8,
        .alignment = 8ull << (ms_struct ? type_layout_ms_alignment(columns, index) : columns->alignment_log2[index]),
        .explicit_alignment = alignment_log2 != 0 ? 8ull << (alignment_log2 - 1) : 0,
        .width = columns->bit_width[member],
        .bit_field = (flags & BUSTER_TYPE_MEMBER_BIT_FIELD) != 0,
        .packed = (flags & BUSTER_TYPE_MEMBER_PACKED) != 0,
        .named = columns->name[member] != 0,
    };
    return result;
}

static uint64_t type_align_up(uint64_t value, uint64_t alignment)
{
    return (value + alignment - 1) & ~(alignment - 1);
}

static uint64_t type_maximum(uint64_t a, uint64_t b)
{
    return a > b ? a : b;
}

static uint64_t type_minimum(uint64_t a, uint64_t b)
{
    return a < b ? a : b;
}

// The scalar layout keeps its sizes in bits below 2^63, so that no addition or alignment in it overflows.
#define BUSTER_TYPE_MAX_RECORD_BITS ((1ull << 63) - 1)

// The scalar layout of one record, the vector layout's twin and fallback: the member offsets, and the record's size
// in bytes and alignment as log2 in *size and *alignment_log2. Fails with TYPE_ERROR_TOO_LARGE past
// BUSTER_TYPE_MAX_OBJECT_BYTES (where clang's own arithmetic in bits would overflow too).
static TypeError type_layout_scalar(TypeTable* table, uint32_t tag, uint64_t* size_bytes, uint32_t* alignment_log2)
{
    const TypeLayoutColumns columns = type_layout_columns(table);
    const TypeTags* tags = &table->tags;
    const bool is_union = type_kind(table, tags->type[tag]) == BUSTER_TYPE_KIND_UNION;
    const bool record_packed = tags->packed[tag] != 0;
    const bool ms_struct = tags->ms_struct[tag] != 0;
    const uint64_t pack = tags->pack_log2[tag] != 0 ? 8ull << (tags->pack_log2[tag] - 1) : 0;
    const uint32_t begin = tags->member_begin[tag];
    const uint32_t count = tags->member_count[tag];
    uint64_t alignment = tags->alignment_log2[tag] != 0 ? 1ull << (tags->alignment_log2[tag] - 1) : 1;
    uint64_t data_size = 0;
    uint64_t unfilled = 0;
    uint64_t previous_unit = 0;
    uint64_t size = 0;
    bool fits = true;
    for (uint32_t i = 0; fits && i < count; i += 1)
    {
        const uint32_t member = begin + i;
        const TypeLayoutInput input = type_layout_input(&columns, member, ms_struct);
        const bool packed = record_packed || input.packed;
        uint64_t offset = 0;
        uint64_t raise = 0;
        if (input.bit_field)
        {
            const uint64_t width = input.width;
            const bool ignored_zero = ms_struct && previous_unit == 0 && width == 0;
            if (ms_struct && (previous_unit != input.size || unfilled < width))
            {
                unfilled = 0;
                previous_unit = 0;
            }
            offset = is_union ? 0 : data_size - unfilled;
            const uint64_t natural_alignment = ms_struct ? (ignored_zero ? 1 : input.size) : input.alignment;
            uint64_t field_alignment = packed && width != 0 ? 1 : natural_alignment;
            uint64_t unpacked_alignment = natural_alignment;
            if (input.explicit_alignment != 0)
            {
                field_alignment = type_maximum(field_alignment, input.explicit_alignment);
                unpacked_alignment = type_maximum(unpacked_alignment, input.explicit_alignment);
            }
            if (pack != 0 && width != 0)
            {
                unpacked_alignment = type_minimum(unpacked_alignment, pack);
                field_alignment = packed ? unpacked_alignment : type_minimum(field_alignment, pack);
            }
            field_alignment = ms_struct && is_union ? 1 : field_alignment;
            if (width == 0 || (ms_struct ? width > unfilled : (pack == 0 && (offset & (field_alignment - 1)) + width > input.size)))
            {
                offset = type_align_up(offset, field_alignment);
            }
            else if (!ms_struct && input.explicit_alignment != 0 && (pack == 0 || input.explicit_alignment <= pack))
            {
                offset = type_align_up(offset, input.explicit_alignment);
            }
            raise = ms_struct || input.named ? field_alignment : 1;
            if (is_union)
            {
                data_size = type_maximum(data_size, ms_struct ? (width != 0 ? input.size : 8) : type_align_up(width, 8));
            }
            else if (ms_struct && width != 0)
            {
                if (unfilled == 0)
                {
                    data_size = offset + input.size;
                    unfilled = input.size;
                }
                unfilled -= width;
                previous_unit = input.size;
            }
            else
            {
                const uint64_t end = offset + width;
                data_size = type_align_up(end, 8);
                unfilled = data_size - end;
                previous_unit = 0;
            }
        }
        else
        {
            uint64_t unpacked_alignment = type_maximum(input.alignment, input.explicit_alignment);
            uint64_t packed_alignment = type_maximum(8, input.explicit_alignment);
            if (pack != 0)
            {
                unpacked_alignment = type_minimum(unpacked_alignment, pack);
                packed_alignment = type_minimum(packed_alignment, pack);
            }
            raise = packed ? packed_alignment : unpacked_alignment;
            offset = type_align_up(is_union ? 0 : data_size, raise);
            unfilled = 0;
            previous_unit = 0;
            uint64_t end = 0;
            fits = !__builtin_add_overflow(offset, input.size, &end) && end <= BUSTER_TYPE_MAX_RECORD_BITS;
            data_size = is_union ? type_maximum(data_size, input.size) : end;
        }
        columns.offset[member] = offset;
        size = type_maximum(size, data_size);
        alignment = type_maximum(alignment, raise / 8);
        fits = fits && data_size <= BUSTER_TYPE_MAX_RECORD_BITS;
    }
    size = type_align_up(size, alignment * 8);
    fits = fits && size / 8 <= BUSTER_TYPE_MAX_OBJECT_BYTES;
    *size_bytes = size / 8;
    *alignment_log2 = type_log2(alignment);
    return fits ? TYPE_ERROR_NONE : TYPE_ERROR_TOO_LARGE;
}

// Completes a laid-out record: its size and alignment, and the next completion number.
static void type_finish_record(TypeTable* table, uint32_t tag, uint64_t size, uint32_t alignment_log2)
{
    TypeColumns* types = &table->types;
    const uint32_t index = type_index(table->tags.type[tag]);
    types->size[index] = size;
    types->alignment_log2[index] = (uint8_t)alignment_log2;
    types->flags[index] |= BUSTER_TYPE_FLAG_COMPLETE;
    table->tags.completions += 1;
    table->tags.completion[tag] = table->tags.completions;
    const TypeChange change = {
        .operation = BUSTER_TYPE_CHANGE_LAYOUT,
        .type = table->tags.type[tag],
        .tag = tag,
    };
    type_change_record(table, change, 0, 0, 0, 0);
}

// Whether a record can be laid out now: a struct or union, defined, and not complete.
static bool type_record_is_pending(const TypeTable* table, TypeId record)
{
    const bool valid = type_is_valid(table, record);
    const uint8_t kind = valid ? type_kind(table, record) : 0;
    const uint8_t flags = valid ? table->types.flags[type_index(record)] : 0;
    return (kind == BUSTER_TYPE_KIND_STRUCT || kind == BUSTER_TYPE_KIND_UNION) && (flags & (BUSTER_TYPE_FLAG_DEFINED | BUSTER_TYPE_FLAG_COMPLETE)) == BUSTER_TYPE_FLAG_DEFINED;
}

static bool type_record_layout(TypeTable* table, TypeId record)
{
    TypeError error = type_record_is_pending(table, record) ? TYPE_ERROR_NONE : TYPE_ERROR_TAG;
    if (error == TYPE_ERROR_NONE)
    {
        const uint32_t tag = table->types.index[type_index(record)];
        uint64_t size = 0;
        uint32_t alignment_log2 = 0;
        error = type_layout_scalar(table, tag, &size, &alignment_log2);
        if (error == TYPE_ERROR_NONE)
        {
            type_finish_record(table, tag, size, alignment_log2);
        }
    }
    if (error != TYPE_ERROR_NONE)
    {
        type_set_error(table, error);
    }
    return error == TYPE_ERROR_NONE;
}

static bool type_complete_record(TypeTable* table, TypeId record, const TypeMember* members, uint32_t count, TypeRecordAttributes attributes)
{
    return type_record_define(table, record, members, count, attributes) && type_record_layout(table, record);
}

static bool type_is_transparent_union(const TypeTable* table, TypeId type)
{
    bool transparent = false;
    if (type_is_valid(table, type))
    {
        const TypeId stripped = type_strip_aligned(table, type);
        transparent = type_kind(table, stripped) == BUSTER_TYPE_KIND_UNION &&
                      (table->types.flags[type_index(stripped)] & BUSTER_TYPE_FLAG_TRANSPARENT_UNION) != 0;
    }
    return transparent;
}

static TypeTransparentUnionResult type_mark_transparent_union(TypeTable* table, TypeId record)
{
    TypeTransparentUnionResult result = { .status = TYPE_TRANSPARENT_UNION_NOT_UNION, .member = UINT32_MAX };
    if (type_is_valid(table, record))
    {
        record = type_strip_aligned(table, record);
        if (type_kind(table, record) == BUSTER_TYPE_KIND_UNION)
        {
            const uint32_t tag = table->types.index[type_index(record)];
            const uint32_t begin = table->tags.member_begin[tag];
            const uint32_t count = table->tags.member_count[tag];
            result.status = !type_is_complete(table, record) ? TYPE_TRANSPARENT_UNION_INCOMPLETE :
                            count == 0 ? TYPE_TRANSPARENT_UNION_EMPTY : TYPE_TRANSPARENT_UNION_APPLIED;
            if (result.status == TYPE_TRANSPARENT_UNION_APPLIED)
            {
                const TypeId first = table->members.type[begin];
                const TypeId stripped = type_strip_aligned(table, first);
                const uint8_t kind = type_kind(table, stripped);
                const bool floating = kind == BUSTER_TYPE_KIND_FLOATING ||
                                      (kind == BUSTER_TYPE_KIND_COMPLEX && type_kind(table, type_element(table, stripped)) == BUSTER_TYPE_KIND_FLOATING);
                if (floating || kind == BUSTER_TYPE_KIND_VECTOR || kind == BUSTER_TYPE_KIND_EXT_VECTOR)
                {
                    result.status = floating ? TYPE_TRANSPARENT_UNION_FIRST_FLOAT : TYPE_TRANSPARENT_UNION_FIRST_VECTOR;
                    result.member = 0;
                }
                const uint64_t size = type_size(table, first);
                const uint32_t alignment = type_alignment_log2(table, first);
                // clang's handleTransparentUnionAttr: every field has the first's size and no greater alignment (a smaller
                // one is fine), and an incomplete field's type (the first's included) ends the check without the attribute.
                for (uint32_t member = 0; result.status == TYPE_TRANSPARENT_UNION_APPLIED && member < count; member += 1)
                {
                    const TypeId type = table->members.type[begin + member];
                    const bool incomplete = !type_is_complete(table, type);
                    if (incomplete || type_size(table, type) != size || type_alignment_log2(table, type) > alignment)
                    {
                        result.status = incomplete ? TYPE_TRANSPARENT_UNION_INCOMPLETE_FIELD :
                                        type_size(table, type) != size ? TYPE_TRANSPARENT_UNION_SIZE : TYPE_TRANSPARENT_UNION_ALIGNMENT;
                        result.member = member;
                    }
                }
            }
            if (result.status == TYPE_TRANSPARENT_UNION_APPLIED)
            {
                // A change only where the flag changes: a union made transparent again records nothing.
                const bool already = (table->types.flags[type_index(record)] & BUSTER_TYPE_FLAG_TRANSPARENT_UNION) != 0;
                table->types.flags[type_index(record)] |= BUSTER_TYPE_FLAG_TRANSPARENT_UNION;
                const TypeChange change = {
                    .operation = BUSTER_TYPE_CHANGE_TRANSPARENT,
                    .type = record,
                    .tag = tag,
                };
                if (!already)
                {
                    type_change_record(table, change, 0, 0, 0, 0);
                }
            }
        }
    }
    return result;
}

// The vector layout: 16 records at once, one per 32-bit lane, a member of each per step, with masks for the lanes whose
// records have no more members. First a scalar pass reads each member's type's size and alignment (pointer chasing:
// a gather is 64 µops on Zen 5) into scratch columns, in the batch's order. Then each group of 16 records, grouped by
// member count so that its lanes end together, takes its members in blocks of 16 steps: each lane's next 16 members
// are contiguous in the member and scratch columns, so masked loads read them, and a transposition in registers turns
// the 16 lanes' rows into 16 steps. Each step computes both a bit-field's and another member's placement and blends
// them by mask, as the scalar layout's branches choose; then the block's offsets are transposed back and stored, each
// lane's 16 with a masked store. Arithmetic is in 32-bit bits, so a record whose inputs or sizes could pass 2^31 bits
// is marked as the steps go and laid out by the scalar layout instead. docs/c-types.md ("Layout") has the 9700X
// measurements, and the two designs this one beat: rows transposed by scalar stores, and gathers.
#define BUSTER_TYPE_LAYOUT_LANES 16
// The member counts by which the batch groups records: from 0 to 63 members, and 64 or more.
#define BUSTER_TYPE_LAYOUT_COUNT_BUCKETS 65
// A member's size in bits in the scratch column when it is 2^27 bytes or more, which sends its record to the scalar
// layout.
#define BUSTER_TYPE_LAYOUT_HUGE (1u << 30)

static __m512i type_align_up_vector(__m512i value, __m512i alignment)
{
    const __m512i minus_one = _mm512_sub_epi32(alignment, _mm512_set1_epi32(1));
    return _mm512_andnot_si512(minus_one, _mm512_add_epi32(value, minus_one));
}

// Transposes 16 rows of 16 dwords in place: element j of row i becomes element i of row j (64 shuffles).
BUSTER_INLINE void type_transpose_16(__m512i* rows)
{
    __m512i pairs[16];
    __m512i quads[16];
    for (uint32_t i = 0; i < 16; i += 2)
    {
        pairs[i] = _mm512_unpacklo_epi32(rows[i], rows[i + 1]);
        pairs[i + 1] = _mm512_unpackhi_epi32(rows[i], rows[i + 1]);
    }
    for (uint32_t i = 0; i < 16; i += 4)
    {
        quads[i + 0] = _mm512_unpacklo_epi64(pairs[i + 0], pairs[i + 2]);
        quads[i + 1] = _mm512_unpackhi_epi64(pairs[i + 0], pairs[i + 2]);
        quads[i + 2] = _mm512_unpacklo_epi64(pairs[i + 1], pairs[i + 3]);
        quads[i + 3] = _mm512_unpackhi_epi64(pairs[i + 1], pairs[i + 3]);
    }
    // quads[4g + c] holds, in its 128-bit lane q, element 4q + c of rows 4g to 4g + 3.
    for (uint32_t c = 0; c < 4; c += 1)
    {
        const __m512i low_even = _mm512_shuffle_i32x4(quads[c], quads[4 + c], 0x88);
        const __m512i low_odd = _mm512_shuffle_i32x4(quads[c], quads[4 + c], 0xdd);
        const __m512i high_even = _mm512_shuffle_i32x4(quads[8 + c], quads[12 + c], 0x88);
        const __m512i high_odd = _mm512_shuffle_i32x4(quads[8 + c], quads[12 + c], 0xdd);
        rows[c] = _mm512_shuffle_i32x4(low_even, high_even, 0x88);
        rows[4 + c] = _mm512_shuffle_i32x4(low_odd, high_odd, 0x88);
        rows[8 + c] = _mm512_shuffle_i32x4(low_even, high_even, 0xdd);
        rows[12 + c] = _mm512_shuffle_i32x4(low_odd, high_odd, 0xdd);
    }
}

// The scalar pass, in the batch's order: each pending record's members' types' sizes in bits (an aligned typedef's
// base's, or BUSTER_TYPE_LAYOUT_HUGE) and alignments as log2, from firsts[i] for records[i]. (A function of its own, so
// that its columns stay in registers: inlined into type_layout_records, they were reloaded from the stack for every
// member.)
static __attribute__((noinline)) void type_layout_read_types(const TypeTable* table, const TypeId* records, uint32_t count, const uint8_t* errors, uint32_t* firsts, uint32_t* layout_sizes, uint8_t* layout_log2)
{
    const TypeLayoutColumns columns = type_layout_columns(table);
    uint32_t first = 0;
    for (uint32_t i = 0; i < count; i += 1)
    {
        const uint32_t tag = errors[i] == TYPE_ERROR_NONE ? table->types.index[type_index(records[i])] : 0;
        const uint32_t begin = errors[i] == TYPE_ERROR_NONE ? table->tags.member_begin[tag] : 0;
        const uint32_t member_count = errors[i] == TYPE_ERROR_NONE ? table->tags.member_count[tag] : 0;
        firsts[i] = first;
        for (uint32_t m = 0; m < member_count; m += 1)
        {
            const uint32_t index = columns.member_type[begin + m] >> BUSTER_TYPE_INDEX_SHIFT;
            const uint32_t resolved = columns.kind[index] == BUSTER_TYPE_KIND_ALIGNED ? columns.base[index] >> BUSTER_TYPE_INDEX_SHIFT : index;
            const uint64_t size = columns.size[resolved];
            layout_sizes[first + m] = size < (BUSTER_TYPE_LAYOUT_HUGE >> 3) ? (uint32_t)size * 8 : BUSTER_TYPE_LAYOUT_HUGE;
            layout_log2[first + m] = (uint8_t)(table->tags.ms_struct[tag] != 0 ? type_layout_ms_alignment(&columns, index) : columns.alignment_log2[index]);
        }
        first += member_count;
    }
}

// Lays out up to 16 records, group[lane] for lane < lanes, whose members' type sizes in bits and alignments as log2 are
// layout_sizes[firsts[lane] + j] and layout_log2[firsts[lane] + j]: their member offsets, and their sizes in bytes and
// alignments as log2 into sizes and alignments. A lane whose inputs or sizes might pass 2^31 bits gets UINT64_MAX as its
// size, for the scalar layout to take. (A function of its own, as type_layout_read_types is.)
static __attribute__((noinline)) void type_layout_group(const TypeTable* table, const uint32_t* group, const uint32_t* firsts, uint32_t lanes, const uint32_t* layout_sizes, const uint8_t* layout_log2, uint64_t* sizes, uint32_t* alignments)
{
    const TypeTags* tags = &table->tags;
    const TypeLayoutColumns columns = type_layout_columns(table);
    uint32_t begins[BUSTER_TYPE_LAYOUT_LANES] = { 0 };
    uint32_t starts[BUSTER_TYPE_LAYOUT_LANES] = { 0 };
    uint32_t counts[BUSTER_TYPE_LAYOUT_LANES] = { 0 };
    uint32_t packs[BUSTER_TYPE_LAYOUT_LANES] = { 0 };
    uint32_t record_alignments[BUSTER_TYPE_LAYOUT_LANES];
    uint32_t steps = 0;
    uint32_t unions = 0;
    uint32_t packed = 0;
    uint32_t microsoft = 0;
    uint32_t too_aligned = 0;
    for (uint32_t lane = 0; lane < BUSTER_TYPE_LAYOUT_LANES; lane += 1)
    {
        record_alignments[lane] = 1;
        if (lane < lanes)
        {
            const uint32_t tag = group[lane];
            begins[lane] = tags->member_begin[tag];
            starts[lane] = firsts[lane];
            counts[lane] = tags->member_count[tag];
            packs[lane] = tags->pack_log2[tag] != 0 && tags->pack_log2[tag] <= 28 ? 8u << (tags->pack_log2[tag] - 1) : 0;
            record_alignments[lane] = tags->alignment_log2[tag] != 0 && tags->alignment_log2[tag] <= 28 ? 1u << (tags->alignment_log2[tag] - 1) : 1;
            too_aligned |= (tags->alignment_log2[tag] > 28 || tags->pack_log2[tag] > 28 ? 1u : 0u) << lane;
            unions |= (type_kind(table, tags->type[tag]) == BUSTER_TYPE_KIND_UNION ? 1u : 0u) << lane;
            packed |= (tags->packed[tag] != 0 ? 1u : 0u) << lane;
            microsoft |= (tags->ms_struct[tag] != 0 ? 1u : 0u) << lane;
            steps = counts[lane] > steps ? counts[lane] : steps;
        }
    }
    const __m512i zero = _mm512_setzero_si512();
    const __m512i one = _mm512_set1_epi32(1);
    const __m512i byte = _mm512_set1_epi32(8);
    const __m512i count_vector = _mm512_loadu_si512(counts);
    const __m512i pack = _mm512_loadu_si512(packs);
    const __mmask16 union_lanes = (__mmask16)unions;
    const __mmask16 packed_lanes = (__mmask16)packed;
    const __mmask16 ms_lanes = (__mmask16)microsoft;
    const __mmask16 pack_lanes = _mm512_test_epi32_mask(pack, pack);
    __mmask16 overflow = (__mmask16)too_aligned;
    __m512i data_size = zero;
    __m512i unfilled = zero;
    __m512i previous_unit = zero;
    __m512i size = zero;
    __m512i alignment = _mm512_loadu_si512(record_alignments);
    for (uint32_t block = 0; block < steps; block += BUSTER_TYPE_LAYOUT_LANES)
    {
        // Row `lane` of each: that lane's members block to block + 15. The words: the size in bits; the bit-field
        // width with bit 24 for a named member; and the type's alignment as log2, the explicit one's (log2 + 1) at bit
        // 8 and the member's flags at bit 16. Lanes past their members load nothing (zeros).
        __m512i size_rows[BUSTER_TYPE_LAYOUT_LANES];
        __m512i width_rows[BUSTER_TYPE_LAYOUT_LANES];
        __m512i log2_rows[BUSTER_TYPE_LAYOUT_LANES];
        for (uint32_t lane = 0; lane < BUSTER_TYPE_LAYOUT_LANES; lane += 1)
        {
            const uint32_t left = counts[lane] > block ? counts[lane] - block : 0;
            const __mmask16 valid = (__mmask16)_bzhi_u32(0xffff, left);
            const uint32_t member = left != 0 ? begins[lane] + block : 0;
            const uint32_t at = left != 0 ? starts[lane] + block : 0;
            const __m512i name = _mm512_maskz_loadu_epi32(valid, columns.name + member);
            const __m512i width = _mm512_and_si512(_mm512_maskz_loadu_epi32(valid, columns.bit_width + member), _mm512_set1_epi32(0xffffff));
            const __m512i type_log2 = _mm512_cvtepu8_epi32(_mm_maskz_loadu_epi8(valid, layout_log2 + at));
            const __m512i explicit_log2 = _mm512_cvtepu8_epi32(_mm_maskz_loadu_epi8(valid, columns.member_alignment_log2 + member));
            const __m512i flags = _mm512_cvtepu8_epi32(_mm_maskz_loadu_epi8(valid, columns.member_flags + member));
            size_rows[lane] = _mm512_maskz_loadu_epi32(valid, layout_sizes + at);
            width_rows[lane] = _mm512_mask_or_epi32(width, _mm512_test_epi32_mask(name, name), width, _mm512_set1_epi32(1 << 24));
            log2_rows[lane] = _mm512_or_si512(_mm512_or_si512(type_log2, _mm512_slli_epi32(explicit_log2, 8)), _mm512_slli_epi32(flags, 16));
        }
        type_transpose_16(size_rows);
        type_transpose_16(width_rows);
        type_transpose_16(log2_rows);
        __m512i offset_rows[BUSTER_TYPE_LAYOUT_LANES];
        const uint32_t block_steps = steps - block < BUSTER_TYPE_LAYOUT_LANES ? steps - block : BUSTER_TYPE_LAYOUT_LANES;
        for (uint32_t j = block_steps; j < BUSTER_TYPE_LAYOUT_LANES; j += 1)
        {
            offset_rows[j] = zero;
        }
        for (uint32_t j = 0; j < block_steps; j += 1)
        {
            const __mmask16 active = _mm512_cmpgt_epu32_mask(count_vector, _mm512_set1_epi32((int)(block + j)));
            const __m512i member_size = size_rows[j];
            const __m512i width = _mm512_and_si512(width_rows[j], _mm512_set1_epi32(0xffffff));
            const __mmask16 named = _mm512_test_epi32_mask(width_rows[j], _mm512_set1_epi32(1 << 24));
            const __m512i type_log2 = _mm512_and_si512(log2_rows[j], _mm512_set1_epi32(0xff));
            const __m512i explicit_log2 = _mm512_and_si512(_mm512_srli_epi32(log2_rows[j], 8), _mm512_set1_epi32(0xff));
            const __mmask16 bit_field = _mm512_test_epi32_mask(log2_rows[j], _mm512_set1_epi32(BUSTER_TYPE_MEMBER_BIT_FIELD << 16));
            const __mmask16 member_packed = packed_lanes | _mm512_test_epi32_mask(log2_rows[j], _mm512_set1_epi32(BUSTER_TYPE_MEMBER_PACKED << 16));
            // Every input below 2^30 bits, so that no step's sum can wrap; past it, the lane goes to the scalar layout.
            overflow |= active & (_mm512_cmpge_epu32_mask(member_size, _mm512_set1_epi32((int)BUSTER_TYPE_LAYOUT_HUGE)) | _mm512_cmpge_epu32_mask(type_log2, _mm512_set1_epi32(27)) |
                                  _mm512_cmpge_epu32_mask(explicit_log2, _mm512_set1_epi32(28)));
            const __m512i member_alignment = _mm512_sllv_epi32(byte, type_log2);
            const __mmask16 has_explicit = _mm512_test_epi32_mask(explicit_log2, explicit_log2);
            const __m512i explicit_alignment = _mm512_maskz_sllv_epi32(has_explicit, byte, _mm512_sub_epi32(explicit_log2, one));
            const __mmask16 zero_width = _mm512_cmpeq_epi32_mask(width, zero);
            const __m512i raised = _mm512_max_epu32(member_alignment, explicit_alignment);
            const __m512i unpacked = _mm512_mask_min_epu32(raised, pack_lanes, raised, pack);
            const __m512i packed_alignment = _mm512_mask_min_epu32(_mm512_max_epu32(byte, explicit_alignment), pack_lanes, _mm512_max_epu32(byte, explicit_alignment), pack);
            const __m512i plain_alignment = _mm512_mask_blend_epi32(member_packed, unpacked, packed_alignment);
            const __m512i plain_offset = type_align_up_vector(_mm512_maskz_mov_epi32((__mmask16)~union_lanes, data_size), plain_alignment);
            const __m512i plain_data = _mm512_mask_max_epu32(_mm512_add_epi32(plain_offset, member_size), union_lanes, data_size, member_size);
            const __mmask16 pack_applies = pack_lanes & (__mmask16)~zero_width;
            const __m512i bit_start = _mm512_maskz_sub_epi32((__mmask16)~union_lanes, data_size, unfilled);
            __m512i field_alignment = _mm512_mask_mov_epi32(member_alignment, member_packed & (__mmask16)~zero_width, one);
            field_alignment = _mm512_max_epu32(field_alignment, explicit_alignment);
            const __m512i unpacked_field = _mm512_mask_min_epu32(raised, pack_applies, raised, pack);
            field_alignment = _mm512_mask_min_epu32(field_alignment, pack_applies, field_alignment, pack);
            field_alignment = _mm512_mask_mov_epi32(field_alignment, pack_applies & member_packed, unpacked_field);
            const __m512i within = _mm512_and_si512(bit_start, _mm512_sub_epi32(field_alignment, one));
            const __mmask16 crosses = _mm512_cmpgt_epu32_mask(_mm512_add_epi32(within, width), member_size);
            const __mmask16 to_field = zero_width | (crosses & (__mmask16)~pack_lanes);
            const __mmask16 explicit_fits = (__mmask16)~pack_lanes | _mm512_cmple_epu32_mask(explicit_alignment, pack);
            const __mmask16 to_explicit = (__mmask16)~to_field & has_explicit & explicit_fits;
            __m512i bit_offset = _mm512_mask_mov_epi32(bit_start, to_field, type_align_up_vector(bit_start, field_alignment));
            bit_offset = _mm512_mask_mov_epi32(bit_offset, to_explicit, type_align_up_vector(bit_start, explicit_alignment));
            const __m512i bit_end = _mm512_add_epi32(bit_offset, width);
            const __m512i bit_end_bytes = type_align_up_vector(bit_end, byte);
            __m512i bit_data = _mm512_mask_max_epu32(bit_end_bytes, union_lanes, data_size, type_align_up_vector(width, byte));
            __m512i bit_unfilled = _mm512_mask_mov_epi32(_mm512_sub_epi32(bit_end_bytes, bit_end), union_lanes, unfilled);
            __m512i bit_raise = _mm512_mask_mov_epi32(one, named, field_alignment);
            if (ms_lanes != 0)
            {
                // An active unit belongs only to adjacent bit-fields of the same size. Full-unit allocation means
                // a following ordinary field starts after its unused bits, while a same-size field can share them.
                const __mmask16 continuing = _mm512_cmpeq_epi32_mask(previous_unit, member_size) & _mm512_cmpge_epu32_mask(unfilled, width);
                const __m512i remaining = _mm512_maskz_mov_epi32(continuing, unfilled);
                const __mmask16 ignored_zero = zero_width & _mm512_cmpeq_epi32_mask(previous_unit, zero);
                const __m512i natural = _mm512_mask_mov_epi32(member_size, ignored_zero, one);
                const __m512i unpacked_ms = _mm512_max_epu32(natural, explicit_alignment);
                __m512i aligned_ms = _mm512_max_epu32(_mm512_mask_mov_epi32(natural, member_packed & (__mmask16)~zero_width, one), explicit_alignment);
                aligned_ms = _mm512_mask_min_epu32(aligned_ms, pack_applies, aligned_ms, pack);
                aligned_ms = _mm512_mask_min_epu32(aligned_ms, pack_applies & member_packed, unpacked_ms, pack);
                aligned_ms = _mm512_mask_mov_epi32(aligned_ms, union_lanes, one);
                const __m512i start_ms = _mm512_maskz_sub_epi32((__mmask16)~union_lanes, data_size, remaining);
                const __mmask16 fresh = _mm512_cmpeq_epi32_mask(remaining, zero);
                const __mmask16 align_ms = zero_width | _mm512_cmpgt_epu32_mask(width, remaining);
                const __m512i offset_ms = _mm512_mask_mov_epi32(start_ms, align_ms, type_align_up_vector(start_ms, aligned_ms));
                const __m512i unit_end = _mm512_mask_add_epi32(data_size, fresh, offset_ms, member_size);
                __m512i data_ms = _mm512_mask_mov_epi32(unit_end, zero_width, type_align_up_vector(offset_ms, byte));
                data_ms = _mm512_mask_max_epu32(data_ms, union_lanes, data_size, _mm512_mask_mov_epi32(member_size, zero_width, byte));
                __m512i left_ms = _mm512_sub_epi32(_mm512_mask_mov_epi32(remaining, fresh, member_size), width);
                left_ms = _mm512_mask_sub_epi32(left_ms, zero_width, data_ms, offset_ms);
                left_ms = _mm512_mask_mov_epi32(left_ms, union_lanes, unfilled);
                bit_offset = _mm512_mask_mov_epi32(bit_offset, ms_lanes, offset_ms);
                bit_data = _mm512_mask_mov_epi32(bit_data, ms_lanes, data_ms);
                bit_unfilled = _mm512_mask_mov_epi32(bit_unfilled, ms_lanes, left_ms);
                bit_raise = _mm512_mask_mov_epi32(bit_raise, ms_lanes, aligned_ms);
                previous_unit = _mm512_mask_mov_epi32(previous_unit, active, _mm512_maskz_mov_epi32(bit_field & (__mmask16)~zero_width & (__mmask16)~union_lanes, member_size));
            }
            const __m512i new_data = _mm512_mask_blend_epi32(bit_field, plain_data, bit_data);
            const __m512i raise = _mm512_mask_blend_epi32(bit_field, plain_alignment, bit_raise);
            offset_rows[j] = _mm512_mask_blend_epi32(bit_field, plain_offset, bit_offset);
            data_size = _mm512_mask_mov_epi32(data_size, active, new_data);
            overflow |= active & _mm512_cmpge_epu32_mask(data_size, _mm512_set1_epi32((int)(1u << 31)));
            unfilled = _mm512_mask_mov_epi32(unfilled, active, _mm512_maskz_mov_epi32(bit_field, bit_unfilled));
            size = _mm512_mask_max_epu32(size, active, size, new_data);
            alignment = _mm512_mask_max_epu32(alignment, active, alignment, _mm512_srli_epi32(raise, 3));
        }
        type_transpose_16(offset_rows);
        for (uint32_t lane = 0; lane < lanes; lane += 1)
        {
            const uint32_t left = counts[lane] > block ? counts[lane] - block : 0;
            const uint32_t valid = _bzhi_u32(0xffff, left);
            uint64_t* destination = columns.offset + (left != 0 ? begins[lane] + block : 0);
            _mm512_mask_storeu_epi64(destination, (__mmask8)valid, _mm512_cvtepu32_epi64(_mm512_castsi512_si256(offset_rows[lane])));
            _mm512_mask_storeu_epi64(destination + 8, (__mmask8)(valid >> 8), _mm512_cvtepu32_epi64(_mm512_extracti64x4_epi64(offset_rows[lane], 1)));
        }
    }
    size = type_align_up_vector(size, _mm512_slli_epi32(alignment, 3));
    uint32_t final_sizes[BUSTER_TYPE_LAYOUT_LANES];
    uint32_t final_alignments[BUSTER_TYPE_LAYOUT_LANES];
    _mm512_storeu_si512(final_sizes, size);
    _mm512_storeu_si512(final_alignments, alignment);
    for (uint32_t lane = 0; lane < lanes; lane += 1)
    {
        const bool overflows = (overflow >> lane & 1) != 0;
        sizes[lane] = overflows ? UINT64_MAX : final_sizes[lane] / 8;
        alignments[lane] = overflows ? 0 : type_log2(final_alignments[lane]);
    }
}

// Groups the records by member count (a counting sort, stable), lays them out 16 at a time (the scalar layout taking
// those that might pass the vector layout's limit), then completes them all in array order.
static bool type_layout_records(TypeTable* table, const TypeId* records, uint32_t count)
{
    Scratch scratch = scratch_begin(0);
    uint32_t* order = BUSTER_ARENA_PUSH_ARRAY(scratch.arena, uint32_t, (uint64_t)count + 1);
    uint32_t* firsts = BUSTER_ARENA_PUSH_ARRAY(scratch.arena, uint32_t, (uint64_t)count + 1);
    uint64_t* sizes = BUSTER_ARENA_PUSH_ARRAY(scratch.arena, uint64_t, (uint64_t)count + 1);
    uint32_t* alignments = BUSTER_ARENA_PUSH_ARRAY(scratch.arena, uint32_t, (uint64_t)count + 1);
    uint8_t* errors = BUSTER_ARENA_PUSH_ARRAY(scratch.arena, uint8_t, (uint64_t)count + 1);
    uint32_t starts[BUSTER_TYPE_LAYOUT_COUNT_BUCKETS + 1] = { 0 };
    uint32_t pending = 0;
    uint64_t total = 0;
    bool memory = order != 0 && firsts != 0 && sizes != 0 && alignments != 0 && errors != 0;
    for (uint32_t i = 0; memory && i < count; i += 1)
    {
        errors[i] = TYPE_ERROR_TAG;
        if (type_record_is_pending(table, records[i]))
        {
            const uint32_t members = table->tags.member_count[table->types.index[type_index(records[i])]];
            starts[(members < BUSTER_TYPE_LAYOUT_COUNT_BUCKETS - 1 ? members : BUSTER_TYPE_LAYOUT_COUNT_BUCKETS - 1) + 1] += 1;
            errors[i] = TYPE_ERROR_NONE;
            total += members;
            pending += 1;
        }
    }
    // The scratch columns of the scalar pass.
    uint32_t* layout_sizes = memory ? arena_push_array(scratch.arena, total + 1, sizeof(uint32_t), 64) : 0;
    uint8_t* layout_log2 = memory ? arena_push_array(scratch.arena, total + 1, sizeof(uint8_t), 64) : 0;
    memory = memory && layout_sizes != 0 && layout_log2 != 0;
    bool result = memory;
    if (memory)
    {
        for (uint32_t bucket = 0; bucket < BUSTER_TYPE_LAYOUT_COUNT_BUCKETS; bucket += 1)
        {
            starts[bucket + 1] += starts[bucket];
        }
        for (uint32_t i = 0; i < count; i += 1)
        {
            if (errors[i] == TYPE_ERROR_NONE)
            {
                const uint32_t members = table->tags.member_count[table->types.index[type_index(records[i])]];
                const uint32_t bucket = members < BUSTER_TYPE_LAYOUT_COUNT_BUCKETS - 1 ? members : BUSTER_TYPE_LAYOUT_COUNT_BUCKETS - 1;
                order[starts[bucket]] = i;
                starts[bucket] += 1;
            }
        }
        type_layout_read_types(table, records, count, errors, firsts, layout_sizes, layout_log2);
        for (uint32_t first = 0; first < pending; first += BUSTER_TYPE_LAYOUT_LANES)
        {
            const uint32_t lanes = pending - first < BUSTER_TYPE_LAYOUT_LANES ? pending - first : BUSTER_TYPE_LAYOUT_LANES;
            uint32_t group[BUSTER_TYPE_LAYOUT_LANES];
            uint32_t group_firsts[BUSTER_TYPE_LAYOUT_LANES];
            uint64_t group_sizes[BUSTER_TYPE_LAYOUT_LANES];
            uint32_t group_alignments[BUSTER_TYPE_LAYOUT_LANES];
            for (uint32_t lane = 0; lane < lanes; lane += 1)
            {
                group[lane] = table->types.index[type_index(records[order[first + lane]])];
                group_firsts[lane] = firsts[order[first + lane]];
            }
            type_layout_group(table, group, group_firsts, lanes, layout_sizes, layout_log2, group_sizes, group_alignments);
            for (uint32_t lane = 0; lane < lanes; lane += 1)
            {
                const uint32_t i = order[first + lane];
                sizes[i] = group_sizes[lane];
                alignments[i] = group_alignments[lane];
                if (group_sizes[lane] == UINT64_MAX)
                {
                    uint32_t alignment_log2 = 0;
                    errors[i] = (uint8_t)type_layout_scalar(table, group[lane], &sizes[i], &alignment_log2);
                    alignments[i] = alignment_log2;
                }
            }
        }
        for (uint32_t i = 0; i < count; i += 1)
        {
            if (errors[i] == TYPE_ERROR_NONE)
            {
                type_finish_record(table, table->types.index[type_index(records[i])], sizes[i], alignments[i]);
            }
            else
            {
                type_set_error(table, (TypeError)errors[i]);
                result = false;
            }
        }
    }
    else
    {
        type_set_error(table, TYPE_ERROR_FULL);
    }
    scratch_end(scratch);
    return result;
}

// ---------------------------------------------------------------------------------------------------------------------
// Conversions

// The builtin integer types' properties, by code: clang's getIntegerRank ((width << 3) plus a number per rank, with
// bool's width 1), width (getIntWidth), whether the type is signed (char is, on x86-64, unless TypeTable.char_unsigned
// says otherwise, which type_integer_properties reads), and the unsigned type of the same rank
// (getCorrespondingUnsignedType).
typedef struct TypeIntegerInfo TypeIntegerInfo;
struct TypeIntegerInfo
{
    uint16_t rank;
    uint8_t width;
    bool is_signed;
    uint8_t unsigned_code;
};

static const TypeIntegerInfo type_integer_info[BUSTER_TYPE_CODE_UINT128 + 1] = {
    [BUSTER_TYPE_CODE_BOOL] = {
        .rank = 1 + (1 << 3),
        .width = 1,
        .is_signed = false,
        .unsigned_code = BUSTER_TYPE_CODE_BOOL,
    },
    [BUSTER_TYPE_CODE_CHAR] = {
        .rank = 2 + (8 << 3),
        .width = 8,
        .is_signed = true,
        .unsigned_code = BUSTER_TYPE_CODE_UCHAR,
    },
    [BUSTER_TYPE_CODE_SCHAR] = {
        .rank = 2 + (8 << 3),
        .width = 8,
        .is_signed = true,
        .unsigned_code = BUSTER_TYPE_CODE_UCHAR,
    },
    [BUSTER_TYPE_CODE_UCHAR] = {
        .rank = 2 + (8 << 3),
        .width = 8,
        .is_signed = false,
        .unsigned_code = BUSTER_TYPE_CODE_UCHAR,
    },
    [BUSTER_TYPE_CODE_SHORT] = {
        .rank = 3 + (16 << 3),
        .width = 16,
        .is_signed = true,
        .unsigned_code = BUSTER_TYPE_CODE_USHORT,
    },
    [BUSTER_TYPE_CODE_USHORT] = {
        .rank = 3 + (16 << 3),
        .width = 16,
        .is_signed = false,
        .unsigned_code = BUSTER_TYPE_CODE_USHORT,
    },
    [BUSTER_TYPE_CODE_INT] = {
        .rank = 4 + (32 << 3),
        .width = 32,
        .is_signed = true,
        .unsigned_code = BUSTER_TYPE_CODE_UINT,
    },
    [BUSTER_TYPE_CODE_UINT] = {
        .rank = 4 + (32 << 3),
        .width = 32,
        .is_signed = false,
        .unsigned_code = BUSTER_TYPE_CODE_UINT,
    },
    [BUSTER_TYPE_CODE_LONG] = {
        .rank = 5 + (64 << 3),
        .width = 64,
        .is_signed = true,
        .unsigned_code = BUSTER_TYPE_CODE_ULONG,
    },
    [BUSTER_TYPE_CODE_ULONG] = {
        .rank = 5 + (64 << 3),
        .width = 64,
        .is_signed = false,
        .unsigned_code = BUSTER_TYPE_CODE_ULONG,
    },
    [BUSTER_TYPE_CODE_LLONG] = {
        .rank = 6 + (64 << 3),
        .width = 64,
        .is_signed = true,
        .unsigned_code = BUSTER_TYPE_CODE_ULLONG,
    },
    [BUSTER_TYPE_CODE_ULLONG] = {
        .rank = 6 + (64 << 3),
        .width = 64,
        .is_signed = false,
        .unsigned_code = BUSTER_TYPE_CODE_ULLONG,
    },
    [BUSTER_TYPE_CODE_INT128] = {
        .rank = 7 + (128 << 3),
        .width = 128,
        .is_signed = true,
        .unsigned_code = BUSTER_TYPE_CODE_UINT128,
    },
    [BUSTER_TYPE_CODE_UINT128] = {
        .rank = 7 + (128 << 3),
        .width = 128,
        .is_signed = false,
        .unsigned_code = BUSTER_TYPE_CODE_UINT128,
    },
};

// An integer type's rank, width and signedness, for a builtin integer type or a _BitInt (whose rank is its width << 3,
// so that it loses to a builtin type of its width and beats any narrower one).
static void type_integer_properties(const TypeTable* table, TypeId type, uint64_t* rank, uint64_t* width, bool* is_signed)
{
    const uint32_t index = type_index(type);
    if (table->types.kind[index] == BUSTER_TYPE_KIND_BIT_INT)
    {
        *width = table->types.extent[index];
        *rank = *width << 3;
        *is_signed = (table->types.flags[index] & BUSTER_TYPE_FLAG_UNSIGNED) == 0;
    }
    else
    {
        *rank = type_integer_info[index].rank;
        *width = type_integer_info[index].width;
        *is_signed = index == BUSTER_TYPE_CODE_CHAR ? !table->char_unsigned : type_integer_info[index].is_signed;
    }
}

// clang's handleIntegerConversion, with getIntegerTypeOrder, on two promoted integer types (builtin or _BitInt,
// unqualified): the higher-ranked if their signedness agrees (the left on a tie), else the unsigned one if its rank is
// at least the signed one's, else the signed one if it is wider, else the signed one's unsigned type.
static TypeId type_integer_conversion(TypeTable* table, TypeId left, TypeId right)
{
    uint64_t left_rank = 0;
    uint64_t left_width = 0;
    bool left_signed = false;
    uint64_t right_rank = 0;
    uint64_t right_width = 0;
    bool right_signed = false;
    type_integer_properties(table, left, &left_rank, &left_width, &left_signed);
    type_integer_properties(table, right, &right_rank, &right_width, &right_signed);
    TypeId result = 0;
    if (left == right)
    {
        result = left;
    }
    else if (left_signed == right_signed)
    {
        result = left_rank >= right_rank ? left : right;
    }
    else
    {
        const TypeId unsigned_type = left_signed ? right : left;
        const TypeId signed_type = left_signed ? left : right;
        const uint64_t unsigned_rank = left_signed ? right_rank : left_rank;
        const uint64_t signed_rank = left_signed ? left_rank : right_rank;
        if (unsigned_rank >= signed_rank)
        {
            result = unsigned_type;
        }
        else if (left_width != right_width)
        {
            result = signed_type;
        }
        else if (type_kind(table, signed_type) == BUSTER_TYPE_KIND_BIT_INT)
        {
            result = type_bit_int(table, (uint32_t)table->types.extent[type_index(signed_type)], true);
        }
        else
        {
            result = BUSTER_TYPE_BUILTIN(type_integer_info[type_index(signed_type)].unsigned_code);
        }
    }
    return result;
}

// The type an operand's conversions start from: unqualified, without ALIGNED, and an atomic type's value type (an
// rvalue's, after lvalue conversion).
static TypeId type_conversion_operand(const TypeTable* table, TypeId type)
{
    TypeId result = type_unqualified(type_strip_aligned(table, type));
    if (type_kind(table, result) == BUSTER_TYPE_KIND_ATOMIC)
    {
        result = type_unqualified(type_strip_aligned(table, table->types.base[type_index(result)]));
    }
    return result;
}

// clang's UsualUnaryConversions on a type: bool, the character types, short and unsigned short to int (all fit in
// int), an enum to its underlying type's promotion (its promotion type, in C), and __fp16 to float. _BitInt and complex
// integer types aren't promoted.
static TypeId type_promote(const TypeTable* table, TypeId type)
{
    TypeId result = type_conversion_operand(table, type);
    if (type_kind(table, result) == BUSTER_TYPE_KIND_ENUM && type_is_complete(table, result))
    {
        result = table->types.base[type_index(result)];
    }
    const uint32_t index = type_index(result);
    if (index >= BUSTER_TYPE_CODE_BOOL && index <= BUSTER_TYPE_CODE_USHORT)
    {
        result = BUSTER_TYPE_INT;
    }
    else if (index == BUSTER_TYPE_CODE_HALF)
    {
        result = BUSTER_TYPE_FLOAT;
    }
    return result;
}

// clang's isPromotableBitField: a _BitInt bit-field keeps its type, one narrower than int becomes int, one as wide as
// int int or unsigned int by its type's signedness, and a wider one is promoted as its type is (GCC's rules, which clang
// follows over C's).
static TypeId type_promote_bit_field(const TypeTable* table, TypeId type, uint32_t width)
{
    const TypeId operand = type_conversion_operand(table, type);
    const uint8_t kind = type_kind(table, operand);
    TypeId result = type_promote(table, operand);
    if (kind == BUSTER_TYPE_KIND_BIT_INT)
    {
        result = operand;
    }
    else if ((kind == BUSTER_TYPE_KIND_INTEGER || kind == BUSTER_TYPE_KIND_ENUM) && width < 32)
    {
        result = BUSTER_TYPE_INT;
    }
    else if ((kind == BUSTER_TYPE_KIND_INTEGER || kind == BUSTER_TYPE_KIND_ENUM) && width == 32)
    {
        const TypeId integer = kind == BUSTER_TYPE_KIND_ENUM ? table->types.base[type_index(operand)] : operand;
        result = type_integer_info[type_index(integer)].is_signed ? BUSTER_TYPE_INT : BUSTER_TYPE_UINT;
    }
    return result;
}

// clang's DefaultArgumentPromotion: the unary conversions, and float and __fp16 to double (not _Float16 or __bf16).
// Always inlined, as it was before #1447 grew the type readers it inlines past clang's threshold.
static inline __attribute__((always_inline)) TypeId type_default_argument_promote(const TypeTable* table, TypeId type)
{
    const TypeId operand = type_conversion_operand(table, type);
    TypeId result = type_promote(table, operand);
    if (operand == BUSTER_TYPE_FLOAT || operand == BUSTER_TYPE_HALF)
    {
        result = BUSTER_TYPE_DOUBLE;
    }
    return result;
}

static bool type_is_arithmetic(const TypeTable* table, TypeId type)
{
    const uint8_t kind = type_kind(table, type);
    return kind == BUSTER_TYPE_KIND_INTEGER || kind == BUSTER_TYPE_KIND_FLOATING || kind == BUSTER_TYPE_KIND_BIT_INT || kind == BUSTER_TYPE_KIND_COMPLEX;
}

// A floating type's element code (its own for a real one), or 0 for a type with integer elements.
static uint32_t type_floating_element(const TypeTable* table, TypeId type)
{
    const uint32_t code = table->types.code[type_index(type)];
    const uint32_t element = code >= BUSTER_TYPE_CODE_COMPLEX ? code - BUSTER_TYPE_CODE_COMPLEX : code;
    return element >= BUSTER_TYPE_CODE_BF16 && element <= BUSTER_TYPE_CODE_FLOAT128 ? element : 0;
}

// clang's UsualArithmeticConversions on two operands' types: both promoted, then the same type if they agree; with a
// complex floating type (handleComplexConversion), the complex type of the higher-ranked floating element, or the
// complex floating operand against an integer one; with a real floating type (handleFloatConversion), the
// higher-ranked one, complex if the other operand is a complex integer; with a complex integer type
// (handleComplexIntConversion), the complex type of the integer conversion of the elements; else the integer
// conversion. 0 if an operand isn't arithmetic, or a type can't be made.
static TypeId type_usual_arithmetic(TypeTable* table, TypeId left, TypeId right)
{
    const TypeId l = type_promote(table, left);
    const TypeId r = type_promote(table, right);
    TypeId result = 0;
    if (!type_is_arithmetic(table, l) || !type_is_arithmetic(table, r))
    {
        result = 0;
    }
    else if (l == r)
    {
        result = l;
    }
    else
    {
        const uint32_t left_float = type_floating_element(table, l);
        const uint32_t right_float = type_floating_element(table, r);
        const bool left_complex = type_kind(table, l) == BUSTER_TYPE_KIND_COMPLEX;
        const bool right_complex = type_kind(table, r) == BUSTER_TYPE_KIND_COMPLEX;
        if (left_float != 0 || right_float != 0)
        {
            const uint32_t element = left_float > right_float ? left_float : right_float;
            result = left_complex || right_complex ? BUSTER_TYPE_COMPLEX(element) : BUSTER_TYPE_BUILTIN(element);
        }
        else if (left_complex || right_complex)
        {
            const TypeId left_element = left_complex ? table->types.base[type_index(l)] : l;
            const TypeId right_element = right_complex ? table->types.base[type_index(r)] : r;
            const TypeId element = type_integer_conversion(table, left_element, right_element);
            result = element == 0 ? 0 : type_complex(table, element);
        }
        else
        {
            result = type_integer_conversion(table, l, r);
        }
    }
    return result;
}

// The kernels' tables, a code each. type_promotion_codes is type_promote on codes. type_code_classes says what a code
// is: arithmetic, floating (real or complex), deferred (a _BitInt, a complex integer or a complex _BitInt: the codes
// alone don't decide the conversion) and _BitInt (a _BitInt or a complex _BitInt: equal codes don't mean one type).
// type_integer_codes is the integer conversion of two promoted integer codes (int to unsigned __int128, 8 to 15),
// indexed by their low 3 bits as (left << 3) | right. type_float_ranks is a floating code's rank from 1 (the variant
// with vpermi2b's), and type_pair_codes that variant's 128-byte table: type_integer_codes, then the floating result of
// two ranks (0 for a non-floating operand) at 64 + (left << 3) + right.
#define BUSTER_TYPE_CLASS_ARITHMETIC 0x01
#define BUSTER_TYPE_CLASS_FLOATING 0x02
#define BUSTER_TYPE_CLASS_DEFERRED 0x04
#define BUSTER_TYPE_CLASS_BIT_INT 0x08

static const uint8_t type_promotion_codes[BUSTER_TYPE_CODE_COUNT] __attribute__((aligned(64))) = {
    0x00, 0x01, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
    0x10, 0x11, 0x13, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F,
    0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2A, 0x2B, 0x2C, 0x2D, 0x2E, 0x2F,
    0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3A, 0x3B, 0x3C, 0x3D, 0x3E, 0x3F,
};

static const uint8_t type_code_classes[BUSTER_TYPE_CODE_COUNT] __attribute__((aligned(64))) = {
    0x00, 0x00, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
    0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x00, 0x0D, 0x0D, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05,
    0x03, 0x03, 0x00, 0x03, 0x03, 0x03, 0x03, 0x00, 0x0D, 0x0D, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

static const uint8_t type_integer_codes[BUSTER_TYPE_CODE_COUNT] __attribute__((aligned(64))) = {
    0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
    0x09, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
    0x0A, 0x0A, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
    0x0B, 0x0B, 0x0B, 0x0B, 0x0D, 0x0D, 0x0E, 0x0F,
    0x0C, 0x0C, 0x0C, 0x0D, 0x0C, 0x0D, 0x0E, 0x0F,
    0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0E, 0x0F,
    0x0E, 0x0E, 0x0E, 0x0E, 0x0E, 0x0E, 0x0E, 0x0F,
    0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F,
};

static const uint8_t type_float_ranks[BUSTER_TYPE_CODE_COUNT] __attribute__((aligned(64))) = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x01, 0x02, 0x00, 0x04, 0x05, 0x06, 0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

static const uint8_t type_pair_codes[2 * BUSTER_TYPE_CODE_COUNT] __attribute__((aligned(64))) = {
    0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
    0x09, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
    0x0A, 0x0A, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
    0x0B, 0x0B, 0x0B, 0x0B, 0x0D, 0x0D, 0x0E, 0x0F,
    0x0C, 0x0C, 0x0C, 0x0D, 0x0C, 0x0D, 0x0E, 0x0F,
    0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0E, 0x0F,
    0x0E, 0x0E, 0x0E, 0x0E, 0x0E, 0x0E, 0x0E, 0x0F,
    0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F,
    0x00, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16,
    0x10, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16,
    0x11, 0x11, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16,
    0x12, 0x12, 0x12, 0x12, 0x13, 0x14, 0x15, 0x16,
    0x13, 0x13, 0x13, 0x13, 0x13, 0x14, 0x15, 0x16,
    0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x15, 0x16,
    0x15, 0x15, 0x15, 0x15, 0x15, 0x15, 0x15, 0x16,
    0x16, 0x16, 0x16, 0x16, 0x16, 0x16, 0x16, 0x16,
};

// The scalar twin of type_promote_codes: the same promotion, from ranges of codes rather than the table.
static uint8_t type_promote_code(uint8_t code)
{
    uint8_t result = code;
    if (code >= BUSTER_TYPE_CODE_BOOL && code <= BUSTER_TYPE_CODE_USHORT)
    {
        result = BUSTER_TYPE_CODE_INT;
    }
    else if (code == BUSTER_TYPE_CODE_HALF)
    {
        result = BUSTER_TYPE_CODE_FLOAT;
    }
    return result;
}

static void type_promote_codes_scalar(const uint8_t* codes, uint64_t count, uint8_t* results)
{
    // Kept scalar (clang would vectorize the loop itself), as the twin the kernel is measured against.
#pragma clang loop vectorize(disable) interleave(disable)
    for (uint64_t i = 0; i < count; i += 1)
    {
        results[i] = type_promote_code(codes[i]);
    }
}

// 64 codes at a time, with a masked load and store for the last ones: one vpermb each.
static void type_promote_codes(const uint8_t* codes, uint64_t count, uint8_t* results)
{
#if !BUSTER_HOST_VECTOR
    type_promote_codes_scalar(codes, count, results);
#else
    const __m512i table = _mm512_load_si512(type_promotion_codes);
    for (uint64_t i = 0; i < count; i += BUSTER_TYPE_CODE_COUNT)
    {
        const __mmask64 lanes = count - i < BUSTER_TYPE_CODE_COUNT ? _bzhi_u64(~0ull, (uint32_t)(count - i)) : ~0ull;
        const __m512i input = _mm512_maskz_loadu_epi8(lanes, codes + i);
        _mm512_mask_storeu_epi8(results + i, lanes, _mm512_permutexvar_epi8(input, table));
    }
#endif
}

// What a code is, from its ranges (the scalar twin's, independent of type_code_classes).
static bool type_code_is_integer(uint32_t code)
{
    return code >= BUSTER_TYPE_CODE_BOOL && code <= BUSTER_TYPE_CODE_UINT128;
}

static bool type_code_is_bit_int(uint32_t code)
{
    const uint32_t element = code >= BUSTER_TYPE_CODE_COMPLEX ? code - BUSTER_TYPE_CODE_COMPLEX : code;
    return element == BUSTER_TYPE_CODE_BIT_INT || element == BUSTER_TYPE_CODE_UNSIGNED_BIT_INT;
}

static uint32_t type_code_floating_element(uint32_t code)
{
    const uint32_t element = code >= BUSTER_TYPE_CODE_COMPLEX ? code - BUSTER_TYPE_CODE_COMPLEX : code;
    const bool real = code < BUSTER_TYPE_CODE_COMPLEX && element >= BUSTER_TYPE_CODE_BF16 && element <= BUSTER_TYPE_CODE_FLOAT128;
    const bool complex = code >= BUSTER_TYPE_CODE_COMPLEX && (BUSTER_TYPE_COMPLEX_ELEMENTS >> element & 1) != 0 && element >= BUSTER_TYPE_CODE_BF16;
    return real || complex ? element : 0;
}

static bool type_code_is_arithmetic(uint32_t code)
{
    const uint32_t element = code >= BUSTER_TYPE_CODE_COMPLEX ? code - BUSTER_TYPE_CODE_COMPLEX : code;
    const bool real = code < BUSTER_TYPE_CODE_COMPLEX && (type_code_is_integer(code) || type_code_floating_element(code) != 0 || type_code_is_bit_int(code));
    const bool complex = code >= BUSTER_TYPE_CODE_COMPLEX && ((BUSTER_TYPE_COMPLEX_ELEMENTS >> element & 1) != 0 || type_code_is_bit_int(code));
    return real || complex;
}

// The integer conversion of two promoted builtin integer codes, as type_integer_conversion decides it.
static uint8_t type_integer_conversion_code(uint32_t left, uint32_t right)
{
    const TypeIntegerInfo l = type_integer_info[left];
    const TypeIntegerInfo r = type_integer_info[right];
    uint32_t result = 0;
    if (left == right)
    {
        result = left;
    }
    else if (l.is_signed == r.is_signed)
    {
        result = l.rank >= r.rank ? left : right;
    }
    else
    {
        const uint32_t unsigned_rank = l.is_signed ? r.rank : l.rank;
        const uint32_t signed_rank = l.is_signed ? l.rank : r.rank;
        const uint32_t signed_code = l.is_signed ? left : right;
        if (unsigned_rank >= signed_rank)
        {
            result = l.is_signed ? right : left;
        }
        else if (l.width != r.width)
        {
            result = signed_code;
        }
        else
        {
            result = type_integer_info[signed_code].unsigned_code;
        }
    }
    return (uint8_t)result;
}

// The scalar twin of the codes' kernel: type_usual_arithmetic's cases on codes, branch by branch.
static uint8_t type_usual_arithmetic_code(uint8_t left, uint8_t right)
{
    const uint8_t l = type_promote_code(left);
    const uint8_t r = type_promote_code(right);
    uint8_t result = 0;
    if (!type_code_is_arithmetic(l) || !type_code_is_arithmetic(r))
    {
        result = 0;
    }
    else if (l == r)
    {
        result = type_code_is_bit_int(l) ? BUSTER_TYPE_CODE_DEFERRED : l;
    }
    else if (type_code_floating_element(l) != 0 || type_code_floating_element(r) != 0)
    {
        const uint32_t left_float = type_code_floating_element(l);
        const uint32_t right_float = type_code_floating_element(r);
        const uint32_t element = left_float > right_float ? left_float : right_float;
        result = (uint8_t)(element | ((l | r) & BUSTER_TYPE_CODE_COMPLEX));
    }
    else if (l >= BUSTER_TYPE_CODE_COMPLEX || r >= BUSTER_TYPE_CODE_COMPLEX || type_code_is_bit_int(l) || type_code_is_bit_int(r))
    {
        result = BUSTER_TYPE_CODE_DEFERRED;
    }
    else
    {
        result = type_integer_conversion_code(l, r);
    }
    return result;
}

static void type_usual_arithmetic_codes_scalar(const uint8_t* left, const uint8_t* right, uint64_t count, uint8_t* results)
{
#pragma clang loop vectorize(disable) interleave(disable)
    for (uint64_t i = 0; i < count; i += 1)
    {
        results[i] = type_usual_arithmetic_code(left[i], right[i]);
    }
}

// Pairs with an equal code (and no _BitInt) keep it. Of the rest, those with a floating operand get the larger floating
// element, complex if either operand is complex; those with a deferred operand get BUSTER_TYPE_CODE_DEFERRED; two
// promoted integer codes get type_integer_codes' entry. (left & 7) << 3 is a 16-bit shift, whose bits from a byte's
// neighbour the mask clears.
static __m512i type_usual_arithmetic_vector(__m512i left, __m512i right, bool pair_table)
{
    const __m512i promotion = _mm512_load_si512(type_promotion_codes);
    const __m512i classes = _mm512_load_si512(type_code_classes);
    const __m512i l = _mm512_permutexvar_epi8(left, promotion);
    const __m512i r = _mm512_permutexvar_epi8(right, promotion);
    const __m512i left_class = _mm512_permutexvar_epi8(l, classes);
    const __m512i right_class = _mm512_permutexvar_epi8(r, classes);
    const __mmask64 left_arithmetic = _mm512_test_epi8_mask(left_class, _mm512_set1_epi8(BUSTER_TYPE_CLASS_ARITHMETIC));
    const __mmask64 right_arithmetic = _mm512_test_epi8_mask(right_class, _mm512_set1_epi8(BUSTER_TYPE_CLASS_ARITHMETIC));
    const __mmask64 arithmetic = left_arithmetic & right_arithmetic;
    const __mmask64 left_float = _mm512_test_epi8_mask(left_class, _mm512_set1_epi8(BUSTER_TYPE_CLASS_FLOATING));
    const __mmask64 right_float = _mm512_test_epi8_mask(right_class, _mm512_set1_epi8(BUSTER_TYPE_CLASS_FLOATING));
    const __mmask64 floating = arithmetic & (left_float | right_float);
    const __mmask64 deferred = arithmetic & ~floating & _mm512_test_epi8_mask(_mm512_or_si512(left_class, right_class), _mm512_set1_epi8(BUSTER_TYPE_CLASS_DEFERRED));
    const __mmask64 same = arithmetic & _mm512_cmpeq_epi8_mask(l, r) & ~_mm512_test_epi8_mask(left_class, _mm512_set1_epi8(BUSTER_TYPE_CLASS_BIT_INT));
    const __m512i complex = _mm512_and_si512(_mm512_or_si512(l, r), _mm512_set1_epi8(BUSTER_TYPE_CODE_COMPLEX));
    const __m512i integer_index = _mm512_or_si512(_mm512_and_si512(_mm512_slli_epi16(l, 3), _mm512_set1_epi8(0x38)), _mm512_and_si512(r, _mm512_set1_epi8(7)));
    __m512i result;
    if (pair_table)
    {
        const __m512i ranks = _mm512_load_si512(type_float_ranks);
        const __m512i left_rank = _mm512_permutexvar_epi8(l, ranks);
        const __m512i right_rank = _mm512_permutexvar_epi8(r, ranks);
        const __m512i float_index = _mm512_or_si512(_mm512_or_si512(_mm512_and_si512(_mm512_slli_epi16(left_rank, 3), _mm512_set1_epi8(0x38)), right_rank),
                                                    _mm512_set1_epi8(0x40));
        const __m512i index = _mm512_mask_mov_epi8(integer_index, floating, float_index);
        const __m512i pair = _mm512_permutex2var_epi8(_mm512_load_si512(type_pair_codes), index, _mm512_load_si512(type_pair_codes + BUSTER_TYPE_CODE_COUNT));
        result = _mm512_maskz_mov_epi8(arithmetic, _mm512_mask_mov_epi8(pair, floating, _mm512_or_si512(pair, complex)));
    }
    else
    {
        const __m512i integer = _mm512_permutexvar_epi8(integer_index, _mm512_load_si512(type_integer_codes));
        const __m512i element_mask = _mm512_set1_epi8(BUSTER_TYPE_CODE_COMPLEX - 1);
        const __m512i element = _mm512_max_epu8(_mm512_and_si512(_mm512_maskz_mov_epi8(left_float, l), element_mask),
                                                _mm512_and_si512(_mm512_maskz_mov_epi8(right_float, r), element_mask));
        result = _mm512_maskz_mov_epi8(arithmetic, integer);
        result = _mm512_mask_mov_epi8(result, floating, _mm512_or_si512(element, complex));
    }
    result = _mm512_mask_mov_epi8(result, deferred, _mm512_set1_epi8(BUSTER_TYPE_CODE_DEFERRED));
    return _mm512_mask_mov_epi8(result, same, l);
}

static void type_usual_arithmetic_codes_variant(const uint8_t* left, const uint8_t* right, uint64_t count, uint8_t* results, bool pair_table)
{
    for (uint64_t i = 0; i < count; i += BUSTER_TYPE_CODE_COUNT)
    {
        const __mmask64 lanes = count - i < BUSTER_TYPE_CODE_COUNT ? _bzhi_u64(~0ull, (uint32_t)(count - i)) : ~0ull;
        const __m512i l = _mm512_maskz_loadu_epi8(lanes, left + i);
        const __m512i r = _mm512_maskz_loadu_epi8(lanes, right + i);
        _mm512_mask_storeu_epi8(results + i, lanes, type_usual_arithmetic_vector(l, r, pair_table));
    }
}

// The kernel the benchmark chose (docs/c-types.md, "Conversions"); type_usual_arithmetic_codes_pairs is the other.
static void type_usual_arithmetic_codes(const uint8_t* left, const uint8_t* right, uint64_t count, uint8_t* results)
{
#if !BUSTER_HOST_VECTOR
    type_usual_arithmetic_codes_scalar(left, right, count, results);
#else
    type_usual_arithmetic_codes_variant(left, right, count, results, false);
#endif
}

static __attribute__((unused)) void type_usual_arithmetic_codes_pairs(const uint8_t* left, const uint8_t* right, uint64_t count, uint8_t* results)
{
    type_usual_arithmetic_codes_variant(left, right, count, results, true);
}

// 16 TypeIds' codes: a builtin's is its index, shifted out of the TypeId in a vector; another type's comes from the
// code column, by scalar loads.
static __m128i type_codes_of(const TypeTable* table, const TypeId* types, __mmask16 lanes)
{
    const __m512i indices = _mm512_srli_epi32(_mm512_maskz_loadu_epi32(lanes, types), BUSTER_TYPE_INDEX_SHIFT);
    const __mmask16 derived = lanes & _mm512_cmpge_epu32_mask(indices, _mm512_set1_epi32(BUSTER_TYPE_BUILTIN_COUNT));
    uint8_t codes[16];
    _mm_storeu_si128((__m128i*)codes, _mm512_cvtepi32_epi8(indices));
    for (uint32_t bits = derived; bits != 0; bits &= bits - 1)
    {
        const uint32_t lane = (uint32_t)__builtin_ctz(bits);
        codes[lane] = type_arithmetic_code(table, types[lane]);
    }
    return _mm_maskz_loadu_epi8(lanes, codes);
}

// 64 pairs at a time: the operands' codes, the kernel, and the results as TypeIds (a builtin code shifted left; 0 for
// a pair with an operand that isn't arithmetic), but each deferred pair's, which type_usual_arithmetic gives.
static void type_usual_arithmetic_types(TypeTable* table, const TypeId* left, const TypeId* right, uint64_t count, TypeId* results)
{
    for (uint64_t i = 0; i < count; i += BUSTER_TYPE_CODE_COUNT)
    {
        const uint64_t lanes = count - i < BUSTER_TYPE_CODE_COUNT ? count - i : BUSTER_TYPE_CODE_COUNT;
        uint8_t left_codes[BUSTER_TYPE_CODE_COUNT];
        uint8_t right_codes[BUSTER_TYPE_CODE_COUNT];
        uint8_t codes[BUSTER_TYPE_CODE_COUNT];
        for (uint64_t part = 0; part < lanes; part += 16)
        {
            const __mmask16 mask = lanes - part < 16 ? (__mmask16)_bzhi_u32(0xffff, (uint32_t)(lanes - part)) : (__mmask16)0xffff;
            _mm_storeu_si128((__m128i*)(left_codes + part), type_codes_of(table, left + i + part, mask));
            _mm_storeu_si128((__m128i*)(right_codes + part), type_codes_of(table, right + i + part, mask));
        }
        type_usual_arithmetic_codes(left_codes, right_codes, lanes, codes);
        for (uint64_t part = 0; part < lanes; part += 16)
        {
            const __mmask16 mask = lanes - part < 16 ? (__mmask16)_bzhi_u32(0xffff, (uint32_t)(lanes - part)) : (__mmask16)0xffff;
            const __m512i wide = _mm512_cvtepu8_epi32(_mm_maskz_loadu_epi8(mask, codes + part));
            _mm512_mask_storeu_epi32(results + i + part, mask, _mm512_slli_epi32(wide, BUSTER_TYPE_INDEX_SHIFT));
            const __mmask16 deferred = mask & _mm512_cmpeq_epi32_mask(wide, _mm512_set1_epi32(BUSTER_TYPE_CODE_DEFERRED));
            for (uint32_t bits = deferred; bits != 0; bits &= bits - 1)
            {
                const uint64_t pair = i + part + (uint32_t)__builtin_ctz(bits);
                results[pair] = type_usual_arithmetic(table, left[pair], right[pair]);
            }
        }
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// Compatibility and composite types

typedef struct TypePair TypePair;
struct TypePair
{
    TypeId left;
    TypeId right;
};

// Grows an array to room for at least `needed` elements, doubling: into a new array, with the old one's elements copied,
// so that the arrays and memos of one walk can share an arena. False if the arena can't hold it.
static bool type_grow_stack(Arena* arena, void** stack, uint64_t* capacity, uint64_t needed, uint64_t element_bytes)
{
    bool result = needed <= *capacity;
    if (!result)
    {
        uint64_t grown = *capacity == 0 ? 64 : *capacity;
        while (grown < needed)
        {
            grown *= 2;
        }
        void* larger = arena_push_array(arena, grown, element_bytes, 16);
        result = larger != 0;
        if (result)
        {
            if (*capacity != 0)
            {
                memcpy(larger, *stack, *capacity * element_bytes);
            }
            *stack = larger;
            *capacity = grown;
        }
    }
    return result;
}

// The pairs a walk has met, and what each merged to (the composite's): types are a DAG, so a pair can be inside many
// others (int (*)(T, T), nested), and a walk that expanded every occurrence of it would take time exponential in the
// types' depth. Open addressing over keys of left << 32 | right (0 is an empty slot: no valid TypeId is 0), at most half
// full, growing into a new table twice as large in its arena.
typedef struct TypePairMemo TypePairMemo;
struct TypePairMemo
{
    Arena* arena;
    uint64_t* keys;
    TypeId* values;
    uint64_t capacity;
    uint64_t count;
};

static uint64_t type_pair_key(TypeId left, TypeId right)
{
    return (uint64_t)left << 32 | right;
}

// The slot that holds key, or the empty one where it goes.
static uint64_t type_pair_slot(const TypePairMemo* memo, uint64_t key)
{
    const uint64_t mask = memo->capacity - 1;
    uint64_t slot = type_hash_finish(key) & mask;
    while (memo->keys[slot] != 0 && memo->keys[slot] != key)
    {
        slot = (slot + 1) & mask;
    }
    return slot;
}

// Whether the walk has met key, and if so its value.
static bool type_pair_find(const TypePairMemo* memo, uint64_t key, TypeId* value)
{
    bool result = memo->capacity != 0;
    if (result)
    {
        const uint64_t slot = type_pair_slot(memo, key);
        result = memo->keys[slot] == key;
        *value = result ? memo->values[slot] : 0;
    }
    return result;
}

// Records key with value; false if the arena can't hold the larger table that needs.
static bool type_pair_add(TypePairMemo* memo, uint64_t key, TypeId value)
{
    bool result = true;
    if ((memo->count + 1) * 2 > memo->capacity)
    {
        const uint64_t capacity = memo->capacity == 0 ? 64 : memo->capacity * 2;
        uint64_t* keys = arena_push_array(memo->arena, capacity, sizeof(uint64_t), 64);
        TypeId* values = keys != 0 ? arena_push_array(memo->arena, capacity, sizeof(TypeId), 64) : 0;
        result = values != 0;
        if (result)
        {
            memset(keys, 0, capacity * sizeof(uint64_t));
            TypePairMemo grown = {
                .arena = memo->arena,
                .keys = keys,
                .values = values,
                .capacity = capacity,
                .count = memo->count,
            };
            for (uint64_t i = 0; i < memo->capacity; i += 1)
            {
                if (memo->keys[i] != 0)
                {
                    const uint64_t slot = type_pair_slot(&grown, memo->keys[i]);
                    grown.keys[slot] = memo->keys[i];
                    grown.values[slot] = memo->values[i];
                }
            }
            *memo = grown;
        }
    }
    if (result)
    {
        const uint64_t slot = type_pair_slot(memo, key);
        memo->count += memo->keys[slot] == 0 ? 1 : 0;
        memo->keys[slot] = key;
        memo->values[slot] = value;
    }
    return result;
}

// Keeps, of `count` pairs, those that are two types and that the walk hasn't met (adding them to `met`), in order;
// returns how many it kept, or UINT32_MAX if `met` can't grow. A pair met before is compared already, or will be.
static uint32_t type_pairs_unmet(TypePairMemo* met, TypePair* pairs, uint32_t count)
{
    uint32_t kept = 0;
    bool fits = true;
    for (uint32_t i = 0; fits && i < count; i += 1)
    {
        const uint64_t key = type_pair_key(pairs[i].left, pairs[i].right);
        TypeId unused = 0;
        if (pairs[i].left != pairs[i].right && !type_pair_find(met, key, &unused))
        {
            fits = type_pair_add(met, key, 0);
            pairs[kept] = pairs[i];
            kept += 1;
        }
    }
    return fits ? kept : UINT32_MAX;
}

// mergeTypes's classes of types: a function with a prototype and one without are one class, the three arrays are one,
// and the two vectors are one.
static uint8_t type_merge_class(uint8_t kind)
{
    uint8_t result = kind;
    if (type_kind_is_array(kind))
    {
        result = BUSTER_TYPE_KIND_ARRAY;
    }
    else if (kind == BUSTER_TYPE_KIND_EXT_VECTOR)
    {
        result = BUSTER_TYPE_KIND_VECTOR;
    }
    return result;
}

// clang's mergeEnumWithInteger: an enum is compatible with its underlying type, compared with the other type as clang's
// hasSameType does, qualifiers included, so that (as clang has it) const enum E and const unsigned int aren't.
static bool type_enum_matches(const TypeTable* table, TypeId enumeration, TypeId other)
{
    const TypeId stripped = type_strip_aligned(table, other);
    return type_is_complete(table, enumeration) && table->types.base[type_index(enumeration)] == stripped;
}

// What mergeFunctionTypes requires of a prototype against a function without one: no ..., and no parameter the default
// argument promotions would change (a promotable integer type, an enum's underlying one counted, or float).
static bool type_prototype_matches_promotions(const TypeTable* table, TypeId function)
{
    uint32_t count = 0;
    const TypeId* parameters = type_parameters(table, function, &count);
    bool result = (table->types.flags[type_index(function)] & BUSTER_TYPE_FLAG_VARIADIC) == 0;
    for (uint32_t i = 0; result && i < count; i += 1)
    {
        TypeId parameter = type_unqualified(type_strip_aligned(table, parameters[i]));
        if (type_kind(table, parameter) == BUSTER_TYPE_KIND_ENUM)
        {
            result = type_is_complete(table, parameter);
            parameter = table->types.base[type_index(parameter)];
        }
        const uint32_t index = type_index(parameter);
        result = result && !(index >= BUSTER_TYPE_CODE_BOOL && index <= BUSTER_TYPE_CODE_USHORT) && parameter != BUSTER_TYPE_FLOAT;
    }
    return result;
}

// The checks of one pair of mergeTypes that don't need the pairs inside it: false if they rule the pair incompatible.
// *leaf says the pair is settled (compatible, or an enum against an integer type); otherwise the pairs inside it must
// be compatible too.
static bool type_parameter_abis_equal(const TypeTable* table, TypeId left, TypeId right);

static bool type_merge_shallow(const TypeTable* table, TypeId left, TypeId right, bool* leaf)
{
    const TypeId l = type_strip_aligned(table, left);
    const TypeId r = type_strip_aligned(table, right);
    const uint8_t left_kind = type_kind(table, l);
    const uint8_t right_kind = type_kind(table, r);
    bool result = true;
    *leaf = true;
    if (__builtin_expect(type_extended_qualifiers(table, left) != type_extended_qualifiers(table, right), 0))
    {
        // clang's mergeTypes: the address spaces must be one, and in C the garbage collection qualifiers too.
        result = false;
    }
    else if (l == r)
    {
        result = true;
    }
    else if (type_qualifiers(l) != type_qualifiers(r))
    {
        result = false;
    }
    else if (type_merge_class(left_kind) != type_merge_class(right_kind))
    {
        result = type_qualifiers(l) == 0 && ((left_kind == BUSTER_TYPE_KIND_ENUM && type_enum_matches(table, l, r)) ||
                                             (right_kind == BUSTER_TYPE_KIND_ENUM && type_enum_matches(table, r, l)));
    }
    else if (left_kind == BUSTER_TYPE_KIND_POINTER || left_kind == BUSTER_TYPE_KIND_ATOMIC)
    {
        *leaf = false;
    }
    else if (type_kind_is_array(left_kind))
    {
        result = !(left_kind == BUSTER_TYPE_KIND_ARRAY && right_kind == BUSTER_TYPE_KIND_ARRAY && table->types.extent[type_index(l)] != table->types.extent[type_index(r)]);
        *leaf = false;
    }
    else if (left_kind == BUSTER_TYPE_KIND_FUNCTION)
    {
        const uint8_t left_flags = table->types.flags[type_index(l)];
        const uint8_t right_flags = table->types.flags[type_index(r)];
        const bool left_prototype = (left_flags & BUSTER_TYPE_FLAG_PROTOTYPE) != 0;
        const bool right_prototype = (right_flags & BUSTER_TYPE_FLAG_PROTOTYPE) != 0;
        if (left_prototype && right_prototype)
        {
            result = table->types.extent[type_index(l)] == table->types.extent[type_index(r)] &&
                     (left_flags & BUSTER_TYPE_FLAG_VARIADIC) == (right_flags & BUSTER_TYPE_FLAG_VARIADIC);
        }
        else if (left_prototype || right_prototype)
        {
            result = type_prototype_matches_promotions(table, left_prototype ? l : r);
        }
        // The conventions are the same; and two prototypes' parameter ABIs too, which an old-style declaration's type hasn't
        // (clang's mergeFunctionTypes takes it with any).
        // Two prototypes' cfi_salts are the same too (#1447).
        result = result && type_function_abi_kind(table, l) == type_function_abi_kind(table, r) &&
                 (!(left_prototype && right_prototype) || (type_parameter_abis_equal(table, l, r) && type_function_salt(table, l) == type_function_salt(table, r)));
        *leaf = false;
    }
    else if (left_kind == BUSTER_TYPE_KIND_VECTOR || left_kind == BUSTER_TYPE_KIND_EXT_VECTOR)
    {
        result = table->types.base[type_index(l)] == table->types.base[type_index(r)] && table->types.extent[type_index(l)] == table->types.extent[type_index(r)];
    }
    else
    {
        // Builtin, complex, _BitInt and tag types are compatible only with themselves, which hash-consing makes the
        // same TypeId.
        result = false;
    }
    return result;
}

// Whether two functions' parameters have the same ABIs (clang's ExtParameterInfos, part of a function's type).
static bool type_parameter_abis_equal(const TypeTable* table, TypeId left, TypeId right)
{
    const bool has = type_function_has_parameter_abis(table, left);
    bool result = has == type_function_has_parameter_abis(table, right);
    const uint32_t count = (uint32_t)table->types.extent[type_index(left)];
    for (uint32_t i = 0; has && result && i < count; i += 1)
    {
        result = type_parameter_abi(table, left, i) == type_parameter_abi(table, right, i);
    }
    return result;
}

// The pairs inside a pair that type_merge_shallow didn't settle, in order: the pointees, the values, the elements, or
// a function's return types and, if both have prototypes, its parameters. Returns how many it wrote to pairs, which
// has room for every one.
static uint32_t type_merge_inner(const TypeTable* table, TypeId left, TypeId right, TypePair* pairs)
{
    const TypeId l = type_strip_aligned(table, left);
    const TypeId r = type_strip_aligned(table, right);
    const uint8_t kind = type_kind(table, l);
    uint32_t result = 1;
    if (type_kind_is_array(kind))
    {
        pairs[0] = (TypePair){
            .left = type_element(table, l),
            .right = type_element(table, r),
        };
    }
    else
    {
        pairs[0] = (TypePair){
            .left = table->types.base[type_index(l)],
            .right = table->types.base[type_index(r)],
        };
    }
    if (kind == BUSTER_TYPE_KIND_FUNCTION && (table->types.flags[type_index(l)] & table->types.flags[type_index(r)] & BUSTER_TYPE_FLAG_PROTOTYPE) != 0)
    {
        uint32_t count = 0;
        const TypeId* left_parameters = type_parameters(table, l, &count);
        const TypeId* right_parameters = type_parameters(table, r, &count);
        for (uint32_t i = 0; i < count; i += 1)
        {
            pairs[1 + i] = (TypePair){
                .left = left_parameters[i],
                .right = right_parameters[i],
            };
        }
        result += count;
    }
    return result;
}

// How many pairs type_merge_inner can write for a pair.
static uint64_t type_merge_inner_count(const TypeTable* table, TypeId left)
{
    const TypeId l = type_strip_aligned(table, left);
    const uint8_t kind = type_kind(table, l);
    return kind == BUSTER_TYPE_KIND_FUNCTION ? 1 + table->types.extent[type_index(l)] : 1;
}

#include "c_types_transparent.c"

// A worklist of the pairs still to compare, each of which must be compatible, and each pushed once: no composite is
// made. Invalid operands are incompatible.
static TypeAnswer type_compatible_at(const TypeTable* table, TypeId left, TypeId right, const uint32_t* activations, uint32_t activation_count, uint32_t source_position)
{
    Scratch scratch = scratch_begin(0);
    void* stack = 0;
    uint64_t capacity = 0;
    uint64_t depth = 0;
    TypePairMemo met = {
        .arena = scratch.arena,
    };
    bool compatible = type_is_valid(table, left) && type_is_valid(table, right);
    bool fits = !compatible || type_grow_stack(scratch.arena, &stack, &capacity, 1, sizeof(TypePair));
    TypeTransparentSearch transparent =
    {
        .arena = scratch.arena,
        .activations = activations,
        .activation_count = activation_count,
        .source_position = source_position,
    };
    if (compatible && fits)
    {
        ((TypePair*)stack)[0] = (TypePair){
            .left = left,
            .right = right,
        };
        depth = 1;
    }
    while (compatible && fits && depth != 0)
    {
        depth -= 1;
        const TypePair pair = ((TypePair*)stack)[depth];
        bool leaf = true;
        compatible = type_merge_shallow(table, pair.left, pair.right, &leaf);
        if (compatible && !leaf && type_has_transparent_parameters(table, pair.left, pair.right))
        {
            // The ordinary worklist contains only conjunctions. Once a transparent parameter introduces choices,
            // solve the original relation once with the memoized AND/OR graph, retaining the same source history.
            const TypeAnswer answer = type_transparent_query(&transparent, table, left, right, false, 0);
            compatible = answer == TYPE_ANSWER_YES;
            fits = answer != TYPE_ANSWER_FULL;
            depth = 0;
        }
        else if (compatible && !leaf)
        {
            fits = type_grow_stack(scratch.arena, &stack, &capacity, depth + type_merge_inner_count(table, pair.left), sizeof(TypePair));
            if (fits)
            {
                TypePair* pairs = (TypePair*)stack + depth;
                const uint32_t kept = type_pairs_unmet(&met, pairs, type_merge_inner(table, pair.left, pair.right, pairs));
                fits = kept != UINT32_MAX;
                depth += fits ? kept : 0;
            }
        }
    }
    const TypeAnswer result = !fits ? TYPE_ANSWER_FULL : compatible ? TYPE_ANSWER_YES : TYPE_ANSWER_NO;
    scratch_end(scratch);
    return result;
}

static TypeAnswer type_compatible(const TypeTable* table, TypeId left, TypeId right)
{
    return type_compatible_at(table, left, right, 0, 0, 0);
}

// The frames of the canonical comparison's and the composite's walks: a pair, and where its inner pairs' results start
// once it has pushed them.
typedef struct TypeMergeFrame TypeMergeFrame;
struct TypeMergeFrame
{
    TypeId left;
    TypeId right;
    uint64_t results;
    bool entered;
};

// The checks of one pair of the canonical comparison (ALIGNED stripped from both) that don't need the pairs inside it:
// whether the two could have one canonical type, which the pairs inside then decide.
static bool type_canonical_shallow(const TypeTable* table, TypeId l, TypeId r)
{
    const TypeColumns* types = &table->types;
    const uint32_t li = type_index(l);
    const uint32_t ri = type_index(r);
    const uint8_t kind = types->kind[li];
    const bool inside = ((types->flags[li] | types->flags[ri]) & BUSTER_TYPE_FLAG_ALIGNED_INSIDE) != 0;
    return inside && type_qualifiers(l) == type_qualifiers(r) && kind == types->kind[ri] && type_key_flags(kind, types->flags[li]) == type_key_flags(kind, types->flags[ri]) &&
           types->extent[li] == types->extent[ri] && (kind == BUSTER_TYPE_KIND_POINTER || type_kind_is_array(kind) || kind == BUSTER_TYPE_KIND_FUNCTION || kind == BUSTER_TYPE_KIND_ATOMIC) &&
           (kind != BUSTER_TYPE_KIND_FUNCTION || (type_function_info(table, li) == type_function_info(table, ri) && type_parameter_abis_equal(table, l, r)));
}

// Whether two types have one canonical type (clang's, without typedefs): the same TypeId, or the same structure once
// every ALIGNED is looked through. Types without ALIGNED inside need only the first compare. The rest is a post-order
// walk with explicit stacks in `arena`, popped when it ends: a pair is one canonical type when its own checks pass and
// every pair inside it is. `known` keeps every pair's answer, so that each pair is compared once however many types
// it is inside, and once in all the comparisons that share `known` (all of a composite's); it lives in another arena,
// or in `arena` when nothing shares it. TYPE_ANSWER_FULL if the stacks or `known` couldn't grow.
static TypeAnswer type_canonical_compare(const TypeTable* table, TypeId left, TypeId right, TypePairMemo* known, Arena* arena)
{
    const uint8_t aligned = (table->types.flags[type_index(left)] | table->types.flags[type_index(right)]) & BUSTER_TYPE_FLAG_ALIGNED_INSIDE;
    TypeAnswer result = left == right ? TYPE_ANSWER_YES : TYPE_ANSWER_NO;
    if (left != right && aligned != 0)
    {
        const Scratch stacks = {
            .arena = arena,
            .length = arena->length,
        };
        void* frames = 0;
        void* answers = 0;
        uint64_t frame_capacity = 0;
        uint64_t answer_capacity = 0;
        uint64_t depth = 0;
        uint64_t answer_count = 0;
        bool fits = type_grow_stack(arena, &frames, &frame_capacity, 1, sizeof(TypeMergeFrame));
        if (fits)
        {
            ((TypeMergeFrame*)frames)[0] = (TypeMergeFrame){
                .left = left,
                .right = right,
                .results = 0,
                .entered = false,
            };
            depth = 1;
        }
        while (fits && depth != 0)
        {
            TypeMergeFrame* frame = (TypeMergeFrame*)frames + depth - 1;
            const uint64_t key = type_pair_key(frame->left, frame->right);
            bool same = true;
            bool done = true;
            if (!frame->entered)
            {
                const TypeId l = type_strip_aligned(table, frame->left);
                const TypeId r = type_strip_aligned(table, frame->right);
                TypeId answer = 0;
                if (type_extended_qualifiers(table, frame->left) != type_extended_qualifiers(table, frame->right))
                {
                    same = false;
                }
                else if (l == r)
                {
                    same = true;
                }
                else if (type_pair_find(known, key, &answer))
                {
                    same = answer != 0;
                }
                else if (!type_canonical_shallow(table, l, r))
                {
                    same = false;
                    fits = type_pair_add(known, key, 0);
                }
                else
                {
                    const TypeColumns* types = &table->types;
                    const uint64_t inner = type_merge_inner_count(table, l);
                    fits = type_grow_stack(arena, &frames, &frame_capacity, depth + inner, sizeof(TypeMergeFrame));
                    if (fits)
                    {
                        // The pairs inside: the bases (pointees, elements without the array's qualifiers, which this
                        // pair compared, values or return types), and a function's parameters.
                        frame = (TypeMergeFrame*)frames + depth - 1;
                        frame->entered = true;
                        frame->results = answer_count;
                        TypeMergeFrame* pushed = (TypeMergeFrame*)frames + depth;
                        pushed[0] = (TypeMergeFrame){
                            .left = types->base[type_index(l)],
                            .right = types->base[type_index(r)],
                            .results = 0,
                            .entered = false,
                        };
                        uint32_t count = 0;
                        if (types->kind[type_index(l)] == BUSTER_TYPE_KIND_FUNCTION)
                        {
                            const TypeId* left_parameters = type_parameters(table, l, &count);
                            const TypeId* right_parameters = type_parameters(table, r, &count);
                            for (uint32_t i = 0; i < count; i += 1)
                            {
                                pushed[1 + i] = (TypeMergeFrame){
                                    .left = left_parameters[i],
                                    .right = right_parameters[i],
                                    .results = 0,
                                    .entered = false,
                                };
                            }
                        }
                        depth += 1 + count;
                        done = false;
                    }
                }
            }
            else
            {
                for (uint64_t i = frame->results; i < answer_count; i += 1)
                {
                    same = same && ((const uint8_t*)answers)[i] != 0;
                }
                answer_count = frame->results;
                fits = type_pair_add(known, key, same ? 1 : 0);
            }
            if (fits && done)
            {
                depth -= 1;
                fits = type_grow_stack(arena, &answers, &answer_capacity, answer_count + 1, sizeof(uint8_t));
                if (fits)
                {
                    ((uint8_t*)answers)[answer_count] = same ? 1 : 0;
                    answer_count += 1;
                }
            }
        }
        result = !fits ? TYPE_ANSWER_FULL : answer_count == 1 && ((const uint8_t*)answers)[0] != 0 ? TYPE_ANSWER_YES : TYPE_ANSWER_NO;
        scratch_end(stacks);
    }
    return result;
}

// The same, alone: its memo and stacks in scratch memory.
static TypeAnswer type_same_canonical(const TypeTable* table, TypeId left, TypeId right)
{
    Scratch scratch = scratch_begin(0);
    TypePairMemo known = {
        .arena = scratch.arena,
    };
    const TypeAnswer result = type_canonical_compare(table, left, right, &known, scratch.arena);
    scratch_end(scratch);
    return result;
}

// Whether a canonical comparison answered yes, noting in *full one that couldn't answer.
static bool type_answer_yes(TypeAnswer answer, bool* full)
{
    *full = *full || answer == TYPE_ANSWER_FULL;
    return answer == TYPE_ANSWER_YES;
}

// A pair whose inner pairs are merged (results, from type_merge_inner's order), as mergeTypes finishes it: the left
// or the right type when the merged inner types are already theirs (canonically: compared with the composite's memo,
// and stacks in `arena`), else a new type of them. 0 if one of those comparisons couldn't answer, with last_error
// TYPE_ERROR_FULL: a guess would choose or make a type of another alignment.
static TypeId type_merge_finish(TypeTable* table, TypeId left, TypeId right, const TypeId* results, TypePairMemo* canonical, Arena* arena)
{
    const TypeId l = type_strip_aligned(table, left);
    const TypeId r = type_strip_aligned(table, right);
    const uint8_t left_kind = type_kind(table, l);
    const uint8_t right_kind = type_kind(table, r);
    const TypeId merged = results[0];
    TypeId result = 0;
    bool full = false;
    if (left_kind == BUSTER_TYPE_KIND_POINTER || left_kind == BUSTER_TYPE_KIND_ATOMIC)
    {
        const bool is_left = type_answer_yes(type_canonical_compare(table, table->types.base[type_index(l)], merged, canonical, arena), &full);
        const bool is_right = !is_left && !full && type_answer_yes(type_canonical_compare(table, table->types.base[type_index(r)], merged, canonical, arena), &full);
        if (full)
        {
            result = 0;
        }
        else if (is_left)
        {
            result = left;
        }
        else if (is_right)
        {
            result = right;
        }
        else
        {
            // As clang's getPointerType and getAtomicType here: without the pair's own qualifiers.
            result = left_kind == BUSTER_TYPE_KIND_POINTER ? type_pointer(table, merged) : type_atomic(table, merged);
        }
    }
    else if (type_kind_is_array(left_kind))
    {
        const bool left_constant = left_kind == BUSTER_TYPE_KIND_ARRAY;
        const bool right_constant = right_kind == BUSTER_TYPE_KIND_ARRAY;
        const bool left_variable = left_kind == BUSTER_TYPE_KIND_VARIABLE_ARRAY;
        const bool right_variable = right_kind == BUSTER_TYPE_KIND_VARIABLE_ARRAY;
        const bool left_element = type_answer_yes(type_canonical_compare(table, type_element(table, l), merged, canonical, arena), &full);
        const bool right_element = !full && type_answer_yes(type_canonical_compare(table, type_element(table, r), merged, canonical, arena), &full);
        if (full)
        {
            result = 0;
        }
        else if (left_constant && left_element)
        {
            result = left;
        }
        else if (right_constant && right_element)
        {
            result = right;
        }
        else if (left_constant || right_constant)
        {
            result = type_array(table, merged, table->types.extent[type_index(left_constant ? l : r)]);
        }
        else if (left_variable && left_element)
        {
            result = left;
        }
        else if (right_variable && right_element)
        {
            result = right;
        }
        else if (left_variable || right_variable)
        {
            // clang's FIXME: the variable length array as it is, though its element may not be the composite.
            result = left_variable ? left : right;
        }
        else if (left_element)
        {
            result = left;
        }
        else if (right_element)
        {
            result = right;
        }
        else
        {
            result = type_incomplete_array(table, merged);
        }
    }
    else
    {
        const uint8_t left_flags = table->types.flags[type_index(l)];
        const uint8_t right_flags = table->types.flags[type_index(r)];
        const bool left_prototype = (left_flags & BUSTER_TYPE_FLAG_PROTOTYPE) != 0;
        const bool right_prototype = (right_flags & BUSTER_TYPE_FLAG_PROTOTYPE) != 0;
        bool all_left = type_answer_yes(type_canonical_compare(table, merged, table->types.base[type_index(l)], canonical, arena), &full);
        bool all_right = !full && type_answer_yes(type_canonical_compare(table, merged, table->types.base[type_index(r)], canonical, arena), &full);
        uint32_t count = 0;
        const TypeId* left_parameters = type_parameters(table, l, &count);
        const TypeId* right_parameters = type_parameters(table, r, &count);
        if (left_prototype && right_prototype)
        {
            for (uint32_t i = 0; !full && i < count; i += 1)
            {
                all_left = all_left && type_answer_yes(type_canonical_compare(table, results[1 + i], left_parameters[i], canonical, arena), &full);
                all_right = all_right && !full && type_answer_yes(type_canonical_compare(table, results[1 + i], right_parameters[i], canonical, arena), &full);
            }
        }
        else
        {
            all_left = all_left && !right_prototype;
            all_right = all_right && !left_prototype;
        }
        if (full)
        {
            result = 0;
        }
        else if (all_left)
        {
            result = left;
        }
        else if (all_right)
        {
            result = right;
        }
        else if (left_prototype && right_prototype)
        {
            result = type_function_like(table, merged, results + 1, count, left_flags & BUSTER_TYPE_FLAG_KEY, type_function_abi_kind(table, l), l);
        }
        else if (left_prototype || right_prototype)
        {
            const TypeId prototype = left_prototype ? l : r;
            const TypeId* parameters = type_parameters(table, prototype, &count);
            result = type_function_like(table, merged, parameters, count, table->types.flags[type_index(prototype)] & BUSTER_TYPE_FLAG_KEY, type_function_abi_kind(table, prototype), prototype);
        }
        else
        {
            result = type_function_abi(table, merged, 0, 0, 0, type_function_abi_kind(table, l));
        }
    }
    if (full)
    {
        type_set_error(table, TYPE_ERROR_FULL);
    }
    return result;
}

// clang's mergeTypes, with explicit stacks: frames of the pairs being merged, and the inner pairs' results, which a
// frame reads once all of them are done. A pair merged before takes its result from the memo. One that is canonically
// one type merges to its left type: the canonical comparisons, the frames' and the finishes', share one memo of their
// own. One that type_merge_shallow settles merges to its left type (or, for an enum against an integer type, the
// integer type) or fails. Any other pushes its inner pairs (the last first, so that their results come out in order)
// and finishes once they are merged, into the memo. So each pair is expanded once, however many types it is inside. A
// failure anywhere fails the whole; one of scratch memory, or a comparison that couldn't answer, with last_error
// TYPE_ERROR_FULL.
static TypeId type_composite_at(TypeTable* table, TypeId left, TypeId right, const uint32_t* activations, uint32_t activation_count, uint32_t source_position)
{
    Scratch frames_scratch = scratch_begin(0);
    Scratch results_scratch = scratch_begin(frames_scratch.arena);
    void* frames = 0;
    void* results = 0;
    uint64_t frame_capacity = 0;
    uint64_t result_capacity = 0;
    uint64_t depth = 0;
    uint64_t result_count = 0;
    TypePairMemo merges = {
        .arena = results_scratch.arena,
    };
    TypePairMemo canonical = {
        .arena = results_scratch.arena,
    };
    TypeTransparentSearch transparent =
    {
        .arena = results_scratch.arena,
        .activations = activations,
        .activation_count = activation_count,
        .source_position = source_position,
    };
    bool valid = type_is_valid(table, left) && type_is_valid(table, right);
    bool full = valid && !type_grow_stack(frames_scratch.arena, &frames, &frame_capacity, 1, sizeof(TypeMergeFrame));
    valid = valid && !full;
    if (valid)
    {
        ((TypeMergeFrame*)frames)[0] = (TypeMergeFrame){
            .left = left,
            .right = right,
            .results = 0,
            .entered = false,
        };
        depth = 1;
    }
    while (valid && depth != 0)
    {
        TypeMergeFrame* frame = (TypeMergeFrame*)frames + depth - 1;
        const uint64_t key = type_pair_key(frame->left, frame->right);
        TypeId merged = 0;
        bool done = true;
        if (!frame->entered)
        {
            bool leaf = true;
            TypeId known = 0;
            const bool merged_before = type_pair_find(&merges, key, &known);
            const TypeAnswer same = merged_before ? TYPE_ANSWER_NO : type_canonical_compare(table, frame->left, frame->right, &canonical, frames_scratch.arena);
            if (merged_before)
            {
                merged = known;
            }
            else if (same == TYPE_ANSWER_FULL)
            {
                full = true;
            }
            else if (same == TYPE_ANSWER_YES)
            {
                merged = frame->left;
            }
            else if (!type_merge_shallow(table, frame->left, frame->right, &leaf))
            {
                valid = false;
            }
            else if (leaf)
            {
                // An enum against an integer type: mergeEnumWithInteger gives the integer type.
                merged = type_kind(table, type_strip_aligned(table, frame->left)) == BUSTER_TYPE_KIND_ENUM ? frame->right : frame->left;
            }
            else
            {
                const uint64_t inner = type_merge_inner_count(table, frame->left);
                full = !type_grow_stack(frames_scratch.arena, &frames, &frame_capacity, depth + inner + inner, sizeof(TypeMergeFrame));
                if (!full)
                {
                    // The inner pairs go past the frames that will hold them, then become frames in reverse order.
                    frame = (TypeMergeFrame*)frames + depth - 1;
                    TypePair* pairs = (TypePair*)((TypeMergeFrame*)frames + depth + inner);
                    const uint32_t written = type_merge_inner(table, frame->left, frame->right, pairs);
                    if (type_has_transparent_parameters(table, frame->left, frame->right))
                    {
                        for (uint32_t i = 1; valid && !full && i < written; i += 1)
                        {
                            if (pairs[i].left != pairs[i].right &&
                                (type_is_transparent_union(table, pairs[i].left) || type_is_transparent_union(table, pairs[i].right)))
                            {
                                const TypeAnswer answer = type_transparent_query(&transparent, table, pairs[i].left, pairs[i].right, true, &pairs[i]);
                                valid = answer == TYPE_ANSWER_YES;
                                full = answer == TYPE_ANSWER_FULL;
                            }
                        }
                    }
                    frame->entered = true;
                    frame->results = result_count;
                    for (uint32_t i = 0; i < written; i += 1)
                    {
                        const TypePair pair = pairs[written - 1 - i];
                        ((TypeMergeFrame*)frames)[depth + i] = (TypeMergeFrame){
                            .left = pair.left,
                            .right = pair.right,
                            .results = 0,
                            .entered = false,
                        };
                    }
                    depth += written;
                    done = false;
                }
            }
        }
        else
        {
            // 0 when a type couldn't be made (the constructor said why in last_error) or a comparison couldn't answer
            // (TYPE_ERROR_FULL, from type_merge_finish).
            merged = type_merge_finish(table, frame->left, frame->right, (TypeId*)results + frame->results, &canonical, frames_scratch.arena);
            result_count = frame->results;
            valid = merged != 0;
            full = valid && !type_pair_add(&merges, key, merged);
        }
        valid = valid && !full;
        if (valid && done)
        {
            depth -= 1;
            full = !type_grow_stack(results_scratch.arena, &results, &result_capacity, result_count + 1, sizeof(TypeId));
            valid = !full;
            if (valid)
            {
                ((TypeId*)results)[result_count] = merged;
                result_count += 1;
            }
        }
    }
    if (full)
    {
        type_set_error(table, TYPE_ERROR_FULL);
    }
    const TypeId result = valid ? ((TypeId*)results)[0] : 0;
    scratch_end(results_scratch);
    scratch_end(frames_scratch);
    return result;
}

static TypeId type_composite(TypeTable* table, TypeId left, TypeId right)
{
    return type_composite_at(table, left, right, 0, 0, 0);
}

// ---------------------------------------------------------------------------------------------------------------------
// Printing

// clang's TypePrinter prints a type by recursion: printBefore (the specifier and what goes left of the declarator's
// name: qualifiers, '*' and the '(' a pointer to an array or function needs), the name, then printAfter (what goes
// right of it: ')', [N] and parameter lists), with one flag, HasEmptyPlaceHolder, saved and restored around the parts
// that need it (whether a space must separate the specifier from what follows). type_print runs the same steps from
// an explicit stack of frames: each frame is a step and its state, and a step that would recurse pushes the steps to
// do after the recursive one, then the recursive one. Its canonical types (ALIGNED looked through, as clang's
// canonical types have no typedefs) print as clang prints them. Two things it can't spell as clang does: a variable
// length array's size expression (it prints [] for one, and [*] for [*]), and an anonymous tag's location (it prints
// (unnamed) where clang names the file and line).
#define BUSTER_TYPE_PRINT_TYPE 0
#define BUSTER_TYPE_PRINT_RESTORE 1
#define BUSTER_TYPE_PRINT_BEFORE 2
#define BUSTER_TYPE_PRINT_BEFORE_END 3
#define BUSTER_TYPE_PRINT_AFTER 4
#define BUSTER_TYPE_PRINT_POINTER_END 5
#define BUSTER_TYPE_PRINT_FUNCTION_END 6
#define BUSTER_TYPE_PRINT_PARAMETERS 7
#define BUSTER_TYPE_PRINT_ATOMIC_END 8
#define BUSTER_TYPE_PRINT_VECTOR_END 9
#define BUSTER_TYPE_PRINT_EXT_VECTOR_END 10
#define BUSTER_TYPE_PRINT_NAME 11
#define BUSTER_TYPE_PRINT_FUNCTION_ABI 12
#define BUSTER_TYPE_PRINT_PAREN_END 13

typedef struct TypePrintFrame TypePrintFrame;
struct TypePrintFrame
{
    TypeId type;
    uint32_t step;
    uint8_t operation;
    // HasEmptyPlaceHolder as the step found it, to restore.
    bool saved;
    // printBefore's: whether the qualifiers go after the specifier.
    bool after;
    // The outer declarator's occurrence, whose TypeNames.parentheses may name a level; nested TYPE frames have none.
    bool root_spelling;
    // Placeholder state at this level in the matching AFTER traversal.
    bool after_empty;
};

typedef struct TypePrinter TypePrinter;
struct TypePrinter
{
    const TypeTable* table;
    Arena* arena;
    const TypeNames* names;
    String8 name;
    uint32_t options;
    bool empty;
    bool valid;
    bool root_spelling;
    bool after_empty;
    // Sorted, coalesced parenthesis levels for a large occurrence; small ones use names directly.
    TypePrintParen* parentheses;
    uint32_t parenthesis_count;
    char* begin;
    uint64_t length;
    void* frames;
    uint64_t capacity;
    uint64_t depth;
    Arena* scratch;
    // By place (TypeNames.alias_order): the specifiers printed so far, and the levels an alias named, which their
    // suffixes' frames ask after. A level is its TYPE frame's id (`frames_typed`, in the order they run) << 8 and its
    // depth below it: its BEFORE, AFTER and POINTER_END frames carry it as their step, `step` the running one's.
    uint32_t occurrence;
    uint32_t aliased[32];
    uint32_t aliased_count;
    uint32_t frames_typed;
    uint32_t step;
    // A qualified array level's qualifiers, which its element takes where a sugar alias names the qualified element
    // (type_print_element_qualifiers): the array kind's BEFORE and AFTER push that element.
    uint32_t element_qualifiers;
};

// The output's bytes, pushed in order so that they stay contiguous.
static void type_print_bytes(TypePrinter* printer, const char* bytes, uint64_t length)
{
    char* copy = length != 0 && printer->valid ? arena_push(printer->arena, length, 1) : 0;
    if (copy != 0)
    {
        memcpy(copy, bytes, length);
        if (printer->begin == 0)
        {
            printer->begin = copy;
        }
        printer->length += length;
    }
    else if (length != 0)
    {
        printer->valid = false;
    }
}

static void type_print_text(TypePrinter* printer, const char* text)
{
    type_print_bytes(printer, text, strlen(text));
}

static void type_print_number(TypePrinter* printer, uint64_t value)
{
    char digits[20];
    uint32_t count = 0;
    uint64_t rest = value;
    do
    {
        digits[sizeof(digits) - 1 - count] = (char)('0' + rest % 10);
        rest /= 10;
        count += 1;
    } while (rest != 0);
    type_print_bytes(printer, digits + sizeof(digits) - count, count);
}

// spaceBeforePlaceHolder.
static void type_print_space(TypePrinter* printer)
{
    if (!printer->empty)
    {
        type_print_text(printer, " ");
    }
}

// Qualifiers::print: AppendTypeQualList, in C99 and later (restrict spelled as such), then the extended qualifiers
// (#1447): an OpenCL address space by its keyword, a target's as its attribute, and the garbage collection qualifier.
// The space macro (TypeNames.space_macro_spaces) of a level's extended qualifiers, or UINT32_MAX.
static uint32_t type_print_space_macro(const TypePrinter* printer, uint32_t extended)
{
    const TypeNames* names = printer->names;
    const uint32_t space = type_address_space(extended);
    uint32_t result = UINT32_MAX;
    for (uint32_t i = 0; space >= BUSTER_TYPE_SPACE_TARGET && names != 0 && result == UINT32_MAX && i < names->space_macro_count; i += 1)
    {
        result = names->space_macro_spaces[i] == space ? i : UINT32_MAX;
    }
    return result;
}

static void type_print_qualifiers(TypePrinter* printer, uint32_t qualifiers, uint32_t extended)
{
    const char* names[3] = {
        "const",
        "volatile",
        "restrict",
    };
    static const char* const spaces[] = {
        "",
        "__global",
        "__local",
        "__constant",
        "__private",
        "__generic",
        "__global_device",
        "__global_host",
    };
    bool first = true;
    for (uint32_t i = 0; i < 3; i += 1)
    {
        if ((qualifiers >> i & 1) != 0)
        {
            type_print_text(printer, first ? "" : " ");
            type_print_text(printer, names[i]);
            first = false;
        }
    }
    const uint32_t space = type_address_space(extended);
    if (space != BUSTER_TYPE_SPACE_DEFAULT)
    {
        type_print_text(printer, first ? "" : " ");
        const uint32_t macro = type_print_space_macro(printer, extended);
        if (macro != UINT32_MAX)
        {
            type_print_bytes(printer, printer->names->space_macro_texts[macro].pointer, printer->names->space_macro_texts[macro].length);
        }
        else if (space >= BUSTER_TYPE_SPACE_TARGET)
        {
            type_print_text(printer, "__attribute__((address_space(");
            type_print_number(printer, space - BUSTER_TYPE_SPACE_TARGET);
            type_print_text(printer, ")))");
        }
        else
        {
            type_print_text(printer, spaces[space < sizeof(spaces) / sizeof(spaces[0]) ? space : 0]);
        }
        first = false;
    }
    const uint32_t gc = extended & BUSTER_TYPE_GC_MASK;
    if (gc != 0)
    {
        type_print_text(printer, first ? "" : " ");
        type_print_text(printer, gc == BUSTER_TYPE_GC_WEAK ? "__weak" : "__strong");
    }
}

static void type_print_push(TypePrinter* printer, uint8_t operation, TypeId type, bool saved, bool after, uint32_t step)
{
    if (printer->valid && type_grow_stack(printer->scratch, &printer->frames, &printer->capacity, printer->depth + 1, sizeof(TypePrintFrame)))
    {
        ((TypePrintFrame*)printer->frames)[printer->depth] = (TypePrintFrame){
            .type = type,
            .step = step,
            .operation = operation,
            .saved = saved,
            .after = after,
            .root_spelling = printer->root_spelling,
            .after_empty = printer->after_empty,
        };
        printer->depth += 1;
    }
    else
    {
        printer->valid = false;
    }
}

// canPrefixQualifiers: a specifier's qualifiers go before it, and an array's do where its element's would.
static bool type_print_can_prefix(const TypeTable* table, TypeId type)
{
    TypeId current = type_strip_aligned(table, type);
    while (type_kind_is_array(type_kind(table, current)))
    {
        current = type_strip_aligned(table, table->types.base[type_index(current)]);
    }
    const uint8_t kind = type_kind(table, current);
    return kind != BUSTER_TYPE_KIND_POINTER && kind != BUSTER_TYPE_KIND_FUNCTION && kind != BUSTER_TYPE_KIND_VECTOR && kind != BUSTER_TYPE_KIND_EXT_VECTOR;
}

// A sugar alias (TypeNames.alias_*) replaces a specifier level's whole spelling, as a typedef's name does in
// clang's diagnostics. Alignment wrappers do not print, so both sides compare without them, qualifiers kept (#2212).
// Returns its index, or NONE.
static uint32_t type_print_alias(const TypeTable* table, const TypeNames* names, TypeId type)
{
    uint32_t result = UINT32_MAX;
    const TypeId spelled = type_strip_alignment(table, type);
    for (uint32_t a = 0; names != 0 && a < names->alias_count; a += 1)
    {
        result = type_strip_alignment(table, names->alias_types[a]) == spelled ? a : result;
    }
    return result;
}

// Whether an alias's type names a level under more qualifiers than its own (#1447): `volatile CI`'s
// `const volatile int` of a const int's CI, which clang prints as the qualifier over the typedef's sugar.
static bool type_print_alias_under(const TypeTable* table, TypeId alias, TypeId type)
{
    alias = type_strip_alignment(table, alias);
    type = type_strip_alignment(table, type);
    const uint32_t own = type_qualifiers(alias);
    return own != 0 && type_unqualified(alias) == type_unqualified(type) && (own & ~type_qualifiers(type)) == 0;
}

static uint32_t type_print_alias_qualified(const TypeTable* table, const TypeNames* names, TypeId type)
{
    uint32_t result = UINT32_MAX;
    for (uint32_t a = 0; names != 0 && a < names->alias_count; a += 1)
    {
        result = type_print_alias_under(table, names->alias_types[a], type) ? a : result;
    }
    return result;
}

// The alias a level takes (`whole` where it matches the level with its qualifiers), or UINT32_MAX: by type, or by place
// (TypeNames.alias_order), the alias of the specifier the printer prints next.
static uint32_t type_print_sugar(const TypePrinter* printer, TypeId type, uint32_t* whole)
{
    const TypeNames* names = printer->names;
    type = type_strip_alignment(printer->table, type);
    const TypeId unqualified = type_unqualified(type);
    uint32_t sugar = UINT32_MAX;
    *whole = UINT32_MAX;
    if (names != 0 && names->alias_order != 0)
    {
        const uint32_t candidate = printer->occurrence < names->alias_order_count ? names->alias_order[printer->occurrence] : UINT32_MAX;
        const TypeId alias = candidate < names->alias_count ? type_strip_alignment(printer->table, names->alias_types[candidate]) : 0;
        *whole = candidate < names->alias_count && alias == type ? candidate : UINT32_MAX;
        sugar = candidate < names->alias_count && (alias == type || alias == unqualified) ? candidate : UINT32_MAX;
    }
    else
    {
        *whole = type_print_alias(printer->table, names, type);
        sugar = *whole != UINT32_MAX ? *whole : type_print_alias(printer->table, names, unqualified);
        sugar = sugar != UINT32_MAX ? sugar : type_print_alias_qualified(printer->table, names, type);
    }
    return sugar;
}

// The qualifiers a qualified array level (`type`, not ALIGNED) leaves to its element, as clang's array types keep them
// (#1447): where a sugar alias names that element with them, the typedef `CI` of `CI a[2]` or `CI b[2][3]` (`const int`), which the
// array's `const int[2]` would otherwise print as `const int[2]`; 0 where none does, or the level has extended
// qualifiers, which stay as they print.
static uint32_t type_print_element_qualifiers(const TypePrinter* printer, TypeId type)
{
    const TypeTable* table = printer->table;
    const uint32_t qualifiers = type_qualifiers(type);
    const bool array = qualifiers != 0 && type_kind_is_array(type_kind(table, type)) && type_extended_qualifiers(table, type) == 0;
    // Through the arrays of a multidimensional one (`CI b[2][3]`), whose element arrays pass them on in turn.
    TypeId element = array && printer->names != 0 ? type_element(table, type) : 0;
    bool named = false;
    for (uint32_t depth = 0; !named && depth < 64 && element != 0; depth += 1)
    {
        uint32_t whole = UINT32_MAX;
        named = type_print_sugar(printer, element, &whole) != UINT32_MAX;
        const bool deeper = !named && type_kind_is_array(type_kind(table, element)) && type_extended_qualifiers(table, element) == 0;
        element = deeper ? type_element(table, element) : 0;
    }
    return named ? qualifiers : 0;
}

// Whether an alias named a level, which then has no suffix of its own: by type, or by place, the level `step` whose
// BEFORE took one (another level of the same type may have taken none).
static bool type_print_aliased(const TypePrinter* printer, TypeId type, uint32_t step)
{
    const TypeNames* names = printer->names;
    bool result = false;
    if (names != 0 && names->alias_order != 0)
    {
        for (uint32_t a = 0; a < printer->aliased_count; a += 1)
        {
            result |= printer->aliased[a] == step;
        }
    }
    else
    {
        result = type_print_alias(printer->table, names, type) != UINT32_MAX || type_print_alias(printer->table, names, type_unqualified(type)) != UINT32_MAX ||
                 type_print_alias_qualified(printer->table, names, type) != UINT32_MAX;
    }
    return result;
}

// printParenBefore/After's groups: an empty placeholder suppresses them at the call site, and a function already
// supplies the innermost one. An alias at that level is a typedef occurrence rather than a function's own spelling.
static uint32_t type_print_parentheses(const TypePrinter* printer, TypeId type, bool aliased)
{
    uint32_t count = 0;
    const TypeNames* names = printer->names;
    if (printer->root_spelling && printer->parentheses != NULL)
    {
        uint32_t first = 0;
        uint32_t end = printer->parenthesis_count;
        while (first < end)
        {
            const uint32_t middle = first + (end - first) / 2;
            if (printer->parentheses[middle].type < type)
            {
                first = middle + 1;
            }
            else
            {
                end = middle;
            }
        }
        count = first < printer->parenthesis_count && printer->parentheses[first].type == type ? printer->parentheses[first].count : 0;
    }
    else
    {
        for (uint32_t i = 0; printer->root_spelling && names != NULL && i < names->parenthesis_count; i += 1)
        {
            count += names->parentheses[i].type == type ? names->parentheses[i].count : 0;
        }
    }
    const bool function = !aliased && type_kind(printer->table, type) == BUSTER_TYPE_KIND_FUNCTION;
    return function && count != 0 ? count - 1 : count;
}

// The syntax view can contain a group at every level of a deeply nested declarator. Sort it once by TypeId,
// preserving repeated levels by summing their counts, so each print step searches rather than rescans the view.
// Four byte-wise radix passes give bounded linear setup even when TypeIds arrive in an adversarial order.
static void type_print_index_parentheses(TypePrinter* printer)
{
    const TypeNames* names = printer->names;
    const uint32_t count = names != NULL ? names->parenthesis_count : 0;
    if (count > 8 && printer->valid)
    {
        TypePrintParen* first = BUSTER_ARENA_PUSH_ARRAY(printer->scratch, TypePrintParen, count);
        TypePrintParen* second = BUSTER_ARENA_PUSH_ARRAY(printer->scratch, TypePrintParen, count);
        printer->valid = first != NULL && second != NULL;
        if (printer->valid)
        {
            memcpy(first, names->parentheses, (uint64_t)count * sizeof(TypePrintParen));
            for (uint32_t shift = 0; shift < 32; shift += 8)
            {
                uint32_t offsets[256] = { 0 };
                for (uint32_t i = 0; i < count; i += 1)
                {
                    offsets[(first[i].type >> shift) & 255] += 1;
                }
                uint32_t position = 0;
                for (uint32_t i = 0; i < 256; i += 1)
                {
                    const uint32_t size = offsets[i];
                    offsets[i] = position;
                    position += size;
                }
                for (uint32_t i = 0; i < count; i += 1)
                {
                    second[offsets[(first[i].type >> shift) & 255]++] = first[i];
                }
                TypePrintParen* swap = first;
                first = second;
                second = swap;
            }
            uint32_t unique = 0;
            for (uint32_t i = 0; i < count; i += 1)
            {
                if (unique != 0 && first[unique - 1].type == first[i].type)
                {
                    first[unique - 1].count += first[i].count;
                }
                else
                {
                    first[unique++] = first[i];
                }
            }
            printer->parentheses = first;
            printer->parenthesis_count = unique;
        }
    }
}

// The specifiers, and printBefore of each kind; `type` is unqualified and not ALIGNED.
static void type_print_before_kind(TypePrinter* printer, TypeId type)
{
    const TypeTable* table = printer->table;
    const uint32_t index = type_index(type);
    const uint8_t kind = table->types.kind[index];
    const TypeId base = table->types.base[index];
    if (kind == BUSTER_TYPE_KIND_INTEGER || kind == BUSTER_TYPE_KIND_FLOATING || kind == BUSTER_TYPE_KIND_VOID || kind == BUSTER_TYPE_KIND_NULLPTR)
    {
        const bool c23_bool = type == BUSTER_TYPE_BOOL && (printer->options & BUSTER_TYPE_PRINT_C23) != 0;
        type_print_text(printer, c23_bool ? "bool" : type_builtins[index].name);
        type_print_space(printer);
        printer->occurrence += 1;
    }
    else if (kind == BUSTER_TYPE_KIND_BIT_INT)
    {
        type_print_text(printer, (table->types.flags[index] & BUSTER_TYPE_FLAG_UNSIGNED) != 0 ? "unsigned _BitInt(" : "_BitInt(");
        type_print_number(printer, table->types.extent[index]);
        type_print_text(printer, ")");
        type_print_space(printer);
        printer->occurrence += 1;
    }
    else if (kind == BUSTER_TYPE_KIND_COMPLEX)
    {
        type_print_text(printer, "_Complex ");
        type_print_push(printer, BUSTER_TYPE_PRINT_BEFORE, base, false, false, printer->step + 1);
    }
    else if (kind == BUSTER_TYPE_KIND_ATOMIC)
    {
        type_print_text(printer, "_Atomic(");
        type_print_push(printer, BUSTER_TYPE_PRINT_ATOMIC_END, type, false, false, 0);
        type_print_push(printer, BUSTER_TYPE_PRINT_TYPE, base, false, false, 0);
    }
    else if (kind == BUSTER_TYPE_KIND_STRUCT || kind == BUSTER_TYPE_KIND_UNION || kind == BUSTER_TYPE_KIND_ENUM)
    {
        const uint32_t name = table->tags.name[table->types.index[index]];
        const String8 spelling = printer->names != 0 && printer->names->anonymous != 0 &&
                                 table->types.index[index] < printer->names->anonymous_count ?
                                 printer->names->anonymous[table->types.index[index]] : (String8){ 0 };
        const bool keyword = (printer->options & BUSTER_TYPE_PRINT_NO_TAG_KEYWORD) == 0;
        if (name != 0 && printer->names != 0 && name < printer->names->count)
        {
            if (keyword)
            {
                type_print_text(printer, kind == BUSTER_TYPE_KIND_STRUCT ? "struct " : kind == BUSTER_TYPE_KIND_UNION ? "union " : "enum ");
            }
            type_print_bytes(printer, (const char*)printer->names->bytes[name], printer->names->lengths[name]);
        }
        else if (spelling.length != 0)
        {
            // A `(unnamed at file:line:col)` row keeps the keyword; a typedef's own name replaces it, as clang's
            // `T` does for the record it names (#992), but for an a.k.a. that spells the definition (anonymous_keyword).
            if (spelling.length != 0 && (*spelling.pointer == '(' || printer->names->anonymous_keyword) && keyword)
            {
                type_print_text(printer, kind == BUSTER_TYPE_KIND_STRUCT ? "struct " : kind == BUSTER_TYPE_KIND_UNION ? "union " : "enum ");
            }
            type_print_bytes(printer, spelling.pointer, spelling.length);
        }
        else
        {
            type_print_text(printer, kind == BUSTER_TYPE_KIND_STRUCT ? "struct " : kind == BUSTER_TYPE_KIND_UNION ? "union " : "enum ");
            type_print_text(printer, "(unnamed)");
        }
        type_print_space(printer);
        printer->occurrence += 1;
    }
    else if (kind == BUSTER_TYPE_KIND_POINTER)
    {
        type_print_push(printer, BUSTER_TYPE_PRINT_POINTER_END, type, printer->empty, false, printer->step);
        printer->empty = false;
        printer->after_empty = false;
        type_print_push(printer, BUSTER_TYPE_PRINT_BEFORE, base, false, false, printer->step + 1);
    }
    else if (type_kind_is_array(kind) || kind == BUSTER_TYPE_KIND_EXT_VECTOR)
    {
        type_print_push(printer, BUSTER_TYPE_PRINT_BEFORE, base | printer->element_qualifiers, false, false, printer->step + 1);
    }
    else if (kind == BUSTER_TYPE_KIND_FUNCTION)
    {
        type_print_push(printer, BUSTER_TYPE_PRINT_FUNCTION_END, type, printer->empty, false, 0);
        printer->empty = false;
        printer->after_empty = false;
        type_print_push(printer, BUSTER_TYPE_PRINT_BEFORE, base, false, false, printer->step + 1);
    }
    else if (kind == BUSTER_TYPE_KIND_VECTOR && (table->types.flags[index] & (BUSTER_TYPE_FLAG_NEON | BUSTER_TYPE_FLAG_NEON_POLY)) != 0)
    {
        // A NEON vector (#1447), as clang's printVectorBefore spells it: its attribute, then its element.
        const bool poly = (table->types.flags[index] & BUSTER_TYPE_FLAG_NEON_POLY) != 0;
        type_print_text(printer, poly ? "__attribute__((neon_polyvector_type(" : "__attribute__((neon_vector_type(");
        type_print_number(printer, table->types.extent[index]);
        type_print_text(printer, "))) ");
        type_print_push(printer, BUSTER_TYPE_PRINT_BEFORE, base, false, false, printer->step + 1);
    }
    else if (kind == BUSTER_TYPE_KIND_VECTOR)
    {
        type_print_text(printer, "__attribute__((__vector_size__(");
        type_print_number(printer, table->types.extent[index]);
        type_print_text(printer, " * sizeof(");
        type_print_push(printer, BUSTER_TYPE_PRINT_VECTOR_END, type, false, false, 0);
        type_print_push(printer, BUSTER_TYPE_PRINT_TYPE, base, false, false, 0);
    }
}

// printAfter of each kind; `type` is unqualified and not ALIGNED.
static void type_print_after_kind(TypePrinter* printer, TypeId type)
{
    const TypeTable* table = printer->table;
    const uint32_t index = type_index(type);
    const uint8_t kind = table->types.kind[index];
    const TypeId base = table->types.base[index];
    if (kind == BUSTER_TYPE_KIND_COMPLEX || kind == BUSTER_TYPE_KIND_VECTOR)
    {
        type_print_push(printer, BUSTER_TYPE_PRINT_AFTER, base, false, false, printer->step + 1);
    }
    else if (kind == BUSTER_TYPE_KIND_EXT_VECTOR)
    {
        type_print_push(printer, BUSTER_TYPE_PRINT_EXT_VECTOR_END, type, false, false, 0);
        type_print_push(printer, BUSTER_TYPE_PRINT_AFTER, base, false, false, printer->step + 1);
    }
    else if (kind == BUSTER_TYPE_KIND_POINTER)
    {
        type_print_push(printer, BUSTER_TYPE_PRINT_RESTORE, type, printer->empty, false, 0);
        printer->empty = false;
        const TypeId pointee = type_strip_aligned(table, base);
        if (type_kind_is_array(type_kind(table, pointee)) && !type_print_aliased(printer, pointee, printer->step + 1) &&
            type_print_parentheses(printer, pointee, false) == 0)
        {
            type_print_text(printer, ")");
        }
        type_print_push(printer, BUSTER_TYPE_PRINT_AFTER, base, false, false, printer->step + 1);
    }
    else if (type_kind_is_array(kind))
    {
        if (kind == BUSTER_TYPE_KIND_ARRAY)
        {
            type_print_text(printer, "[");
            type_print_number(printer, table->types.extent[index]);
            type_print_text(printer, "]");
        }
        else
        {
            const uint64_t extent = table->types.extent[index];
            const TypeNames* names = printer->names;
            uint32_t bound = UINT32_MAX;
            for (uint32_t i = 0; kind == BUSTER_TYPE_KIND_VARIABLE_ARRAY && extent != 0 && names != 0 && bound == UINT32_MAX && i < names->bound_count; i += 1)
            {
                bound = names->bound_ids[i] == extent ? i : UINT32_MAX;
            }
            type_print_text(printer, kind == BUSTER_TYPE_KIND_VARIABLE_ARRAY && extent == 0 ? "[*]" : bound != UINT32_MAX ? "[" : "[]");
            if (bound != UINT32_MAX)
            {
                type_print_bytes(printer, names->bound_texts[bound].pointer, names->bound_texts[bound].length);
                type_print_text(printer, "]");
            }
        }
        type_print_push(printer, BUSTER_TYPE_PRINT_AFTER, base | printer->element_qualifiers, false, false, printer->step + 1);
    }
    else if (kind == BUSTER_TYPE_KIND_FUNCTION)
    {
        if (!printer->empty)
        {
            type_print_text(printer, ")");
        }
        type_print_push(printer, BUSTER_TYPE_PRINT_RESTORE, type, printer->empty, false, 0);
        printer->empty = false;
        type_print_push(printer, BUSTER_TYPE_PRINT_AFTER, base, false, false, printer->step + 1);
        if (type_function_abi_kind(table, type) != TYPE_FUNCTION_ABI_SYSV)
        {
            type_print_push(printer, BUSTER_TYPE_PRINT_FUNCTION_ABI, type, false, false, 0);
        }
        if ((table->types.flags[index] & BUSTER_TYPE_FLAG_PROTOTYPE) != 0)
        {
            type_print_text(printer, "(");
            type_print_push(printer, BUSTER_TYPE_PRINT_PARAMETERS, type, false, false, 0);
        }
        else
        {
            type_print_text(printer, "()");
        }
    }
}

static void type_print_step(TypePrinter* printer, TypePrintFrame frame)
{
    const TypeTable* table = printer->table;
    printer->root_spelling = frame.root_spelling;
    printer->after_empty = frame.after_empty;
    if (frame.operation == BUSTER_TYPE_PRINT_TYPE)
    {
        // TypePrinter::print: the placeholder is empty but at the top, where it is the name.
        const bool named = frame.after && printer->name.length != 0;
        printer->root_spelling = frame.after;
        type_print_push(printer, BUSTER_TYPE_PRINT_RESTORE, frame.type, printer->empty, false, 0);
        printer->empty = !named;
        printer->after_empty = !named;
        const uint32_t level = printer->frames_typed++ << 8;
        type_print_push(printer, BUSTER_TYPE_PRINT_AFTER, frame.type, false, false, level);
        if (named)
        {
            type_print_push(printer, BUSTER_TYPE_PRINT_NAME, frame.type, false, false, 0);
        }
        type_print_push(printer, BUSTER_TYPE_PRINT_BEFORE, frame.type, false, false, level);
    }
    else if (frame.operation == BUSTER_TYPE_PRINT_RESTORE)
    {
        printer->empty = frame.saved;
    }
    else if (frame.operation == BUSTER_TYPE_PRINT_BEFORE)
    {
        // The level as a typedef's name may name it (its extended qualifiers kept), and as its kind prints it.
        const TypeId spelled = type_strip_alignment(table, frame.type);
        const TypeId type = type_strip_aligned(table, frame.type);
        const TypeId unqualified = type_unqualified(type);
        const uint32_t extended = type_extended_qualifiers(table, frame.type);
        uint32_t whole = UINT32_MAX;
        uint32_t sugar = type_print_sugar(printer, spelled, &whole);
        // A typedef's name under extended qualifiers prints after them (`__attribute__((address_space(1))) T`).
        const bool outside = sugar == UINT32_MAX && extended != 0 && (sugar = type_print_sugar(printer, type, &whole)) != UINT32_MAX;
        // Group exactly the levels whose matching AFTER frame sees a nonempty placeholder.
        const uint32_t parentheses = !printer->after_empty ? type_print_parentheses(printer, type, sugar != UINT32_MAX) : 0;
        if (parentheses != 0)
        {
            type_print_push(printer, BUSTER_TYPE_PRINT_PAREN_END, type, false, false, parentheses);
        }
        if (sugar != UINT32_MAX)
        {
            printer->occurrence += 1;
            if (printer->aliased_count < 32)
            {
                printer->aliased[printer->aliased_count++] = frame.step;
            }
            else
            {
                // No room to remember the level: the places can't be followed, which the count the caller compares
                // with says.
                printer->occurrence = UINT32_MAX / 2;
            }
            // A typedef's name is the specifier. When it covers the level itself, it covers its qualifiers too;
            // under outside qualifiers (`const P`), clang prints them before it, as for an ordinary specifier (#992).
            // Without a covered set, the alias's own qualifiers are what it covers; those its use writes again print too.
            const TypeId alias_type = printer->names->alias_types[sugar];
            const bool qualified_alias = whole == UINT32_MAX && type_print_alias_under(table, alias_type, type);
            const uint32_t covered = printer->names->alias_covered != 0 ? printer->names->alias_covered[sugar] :
                                     qualified_alias ? type_qualifiers(alias_type) : 0;
            const uint32_t written = printer->names->alias_written != 0 ? printer->names->alias_written[sugar] & type_qualifiers(type) : 0;
            const uint32_t outer = (whole == UINT32_MAX ? type_qualifiers(type) & ~covered : 0) | written;
            if (outer != 0 || outside)
            {
                type_print_qualifiers(printer, outer, outside ? extended : 0);
                type_print_text(printer, " ");
            }
            const String8 alias = printer->names->alias_names[sugar];
            type_print_bytes(printer, (const char*)alias.pointer, alias.length);
            type_print_space(printer);
        }
        else
        {
            // A qualified array whose element an alias names leaves its qualifiers to that element.
            const uint32_t delegated = type_print_element_qualifiers(printer, type);
            const uint32_t qualifiers = type_qualifiers(type) & ~delegated;
            // A space a macro writes names the level by the macro, before it, and its other qualifiers follow it, as
            // clang's MacroQualifiedType prints (`'AS1 int const *'`, `int *AS1 *`'s `'AS1 int **'`).
            const uint32_t macro = type_print_space_macro(printer, extended);
            if (macro != UINT32_MAX)
            {
                type_print_bytes(printer, printer->names->space_macro_texts[macro].pointer, printer->names->space_macro_texts[macro].length);
                type_print_text(printer, " ");
            }
            const uint32_t shown = macro != UINT32_MAX ? extended & ~BUSTER_TYPE_SPACE_MASK : extended;
            const bool prefix = macro == UINT32_MAX && type_print_can_prefix(table, type);
            if (prefix && (qualifiers | shown) != 0)
            {
                type_print_qualifiers(printer, qualifiers, shown);
                type_print_text(printer, " ");
            }
            const bool after = !prefix && (qualifiers | shown) != 0;
            type_print_push(printer, BUSTER_TYPE_PRINT_BEFORE_END, spelled, printer->empty, after, 0);
            if (after)
            {
                printer->empty = false;
            }
            printer->step = frame.step;
            printer->element_qualifiers = delegated;
            type_print_before_kind(printer, unqualified);
            printer->element_qualifiers = 0;
        }
    }
    else if (frame.operation == BUSTER_TYPE_PRINT_BEFORE_END)
    {
        if (frame.after)
        {
            // A macro's level (BEFORE) prints its qualifiers after its specifier, without its space.
            const uint32_t extended = type_extended_qualifiers(table, frame.type);
            const bool macro = type_print_space_macro(printer, extended) != UINT32_MAX;
            type_print_qualifiers(printer, type_qualifiers(frame.type), macro ? extended & ~BUSTER_TYPE_SPACE_MASK : extended);
            if (!frame.saved)
            {
                type_print_text(printer, " ");
            }
        }
        printer->empty = frame.saved;
    }
    else if (frame.operation == BUSTER_TYPE_PRINT_AFTER)
    {
        // An aliased level's name already covers its own declarator suffix (`A` for `int[4]`, `P` for `int *`),
        // including under another level's qualifiers (#992).
        const TypeId spelled = type_strip_alignment(table, frame.type);
        const TypeId type = type_strip_aligned(table, frame.type);
        const TypeId unqualified = type_unqualified(type);
        const bool aliased = type_print_aliased(printer, spelled, frame.step) || (spelled != type && type_print_aliased(printer, type, frame.step));
        const uint32_t parentheses = !printer->empty ? type_print_parentheses(printer, type, aliased) : 0;
        for (uint32_t i = 0; i < parentheses; i += 1)
        {
            type_print_text(printer, ")");
        }
        if (!aliased)
        {
            printer->step = frame.step;
            // (By place, the element's AFTER asks after its BEFORE's step, and the place has moved on since.)
            const bool by_type = printer->names != 0 && printer->names->alias_order == 0;
            printer->element_qualifiers = by_type ? type_print_element_qualifiers(printer, type) : 0;
            type_print_after_kind(printer, unqualified);
            printer->element_qualifiers = 0;
        }
    }
    else if (frame.operation == BUSTER_TYPE_PRINT_POINTER_END)
    {
        const TypeId pointee = type_strip_aligned(table, table->types.base[type_index(frame.type)]);
        const bool wrapped = type_kind_is_array(type_kind(table, pointee)) && !type_print_aliased(printer, pointee, frame.step + 1) &&
                             type_print_parentheses(printer, pointee, false) == 0;
        type_print_text(printer, wrapped ? "(*" : "*");
        printer->empty = frame.saved;
    }
    else if (frame.operation == BUSTER_TYPE_PRINT_FUNCTION_END)
    {
        if (!frame.saved)
        {
            type_print_text(printer, "(");
        }
        printer->empty = frame.saved;
    }
    else if (frame.operation == BUSTER_TYPE_PRINT_PAREN_END)
    {
        for (uint32_t i = 0; i < frame.step; i += 1)
        {
            type_print_text(printer, "(");
        }
    }
    else if (frame.operation == BUSTER_TYPE_PRINT_PARAMETERS)
    {
        uint32_t count = 0;
        const TypeId* parameters = type_parameters(table, frame.type, &count);
        if (frame.step < count)
        {
            // A parameter's ABI before its type, as clang's TypePrinter::printFunctionProtoAfter prints it.
            static const char* const parameter_spellings[] = {
                "",
                "__attribute__((swift_indirect_result)) ",
                "__attribute__((swift_error_result)) ",
                "__attribute__((swift_context)) ",
                "__attribute__((swift_async_context)) ",
            };
            type_print_text(printer, frame.step != 0 ? ", " : "");
            type_print_text(printer, parameter_spellings[type_parameter_abi(table, frame.type, frame.step)]);
            type_print_push(printer, BUSTER_TYPE_PRINT_PARAMETERS, frame.type, false, false, frame.step + 1);
            type_print_push(printer, BUSTER_TYPE_PRINT_TYPE, parameters[frame.step], false, false, 0);
        }
        else
        {
            if ((table->types.flags[type_index(frame.type)] & BUSTER_TYPE_FLAG_VARIADIC) != 0)
            {
                type_print_text(printer, count != 0 ? ", ..." : "...");
            }
            else if (count == 0)
            {
                type_print_text(printer, "void");
            }
            type_print_text(printer, ")");
        }
    }
    else if (frame.operation == BUSTER_TYPE_PRINT_FUNCTION_ABI)
    {
        static const char* const spellings[] = {
            "sysv_abi",
            "ms_abi",
            "vectorcall",
            "regcall",
            "preserve_most",
            "preserve_all",
            "preserve_none",
            "no_caller_saved_registers",
            // clang's TypePrinter spells CC_IntelOclBicc without its underscores.
            "inteloclbicc",
            "swiftcall",
            "swiftasynccall",
        };
        _Static_assert(sizeof(spellings) / sizeof(spellings[0]) == TYPE_FUNCTION_ABI_COUNT, "every convention has a spelling");
        // clang's printFunctionAfter: the convention, then no_caller_saved_registers' bit beside another (#1447).
        const TypeFunctionABI abi = type_function_convention(table, frame.type);
        const bool canonical = (printer->options & BUSTER_TYPE_PRINT_CANONICAL_CC) != 0;
        const bool inside = (printer->options & BUSTER_TYPE_PRINT_INSIDE_CC) != 0;
        if ((uint32_t)abi < sizeof(spellings) / sizeof(spellings[0]) && abi != TYPE_FUNCTION_ABI_SYSV && !inside)
        {
            type_print_text(printer, canonical && abi == TYPE_FUNCTION_ABI_SWIFT_ASYNC ? "__attribute__((" : " __attribute__((");
            type_print_text(printer, canonical && abi == TYPE_FUNCTION_ABI_INTEL_OCL ? "intel_ocl_bicc" : spellings[abi]);
            type_print_text(printer, "))");
        }
        if (abi != TYPE_FUNCTION_ABI_NO_CALLER_SAVED && type_function_keeps_registers(table, frame.type))
        {
            type_print_text(printer, " __attribute__((no_caller_saved_registers))");
        }
    }
    else if (frame.operation == BUSTER_TYPE_PRINT_ATOMIC_END)
    {
        type_print_text(printer, ")");
        type_print_space(printer);
    }
    else if (frame.operation == BUSTER_TYPE_PRINT_VECTOR_END)
    {
        type_print_text(printer, ")))) ");
        type_print_push(printer, BUSTER_TYPE_PRINT_BEFORE, table->types.base[type_index(frame.type)], false, false, 0);
    }
    else if (frame.operation == BUSTER_TYPE_PRINT_EXT_VECTOR_END)
    {
        type_print_text(printer, " __attribute__((ext_vector_type(");
        type_print_number(printer, table->types.extent[type_index(frame.type)]);
        type_print_text(printer, ")))");
    }
    else
    {
        type_print_bytes(printer, printer->name.pointer, printer->name.length);
    }
}

static String8 type_print(const TypeTable* table, Arena* arena, TypeId type, String8 name, const TypeNames* names, uint32_t options)
{
    Scratch scratch = scratch_begin(arena);
    TypePrinter printer = {
        .table = table,
        .arena = arena,
        .names = names,
        .name = name,
        .options = options,
        .empty = true,
        .valid = type_is_valid(table, type),
        .scratch = scratch.arena,
    };
    type_print_index_parentheses(&printer);
    // The top frame is the only one whose placeholder is the name (frame.after marks it).
    type_print_push(&printer, BUSTER_TYPE_PRINT_TYPE, type, false, true, 0);
    while (printer.valid && printer.depth != 0)
    {
        printer.depth -= 1;
        type_print_step(&printer, ((TypePrintFrame*)printer.frames)[printer.depth]);
    }
    String8 result = { 0 };
    if (printer.valid)
    {
        result = (String8){
            .pointer = printer.begin,
            .length = printer.length,
        };
    }
    if (names != 0 && names->alias_order_printed != 0)
    {
        *names->alias_order_printed = printer.occurrence;
    }
    scratch_end(scratch);
    return result;
}

#include "c_types_sugar.c"

// ---------------------------------------------------------------------------------------------------------------------
// Deferred bodies (#113)

static bool type_changes_initialize(TypeChanges* changes, Arena* arena, uint32_t count, uint32_t parameters, uint32_t members)
{
    const uint64_t mark = arena->length;
    *changes = (TypeChanges){ 0 };
    TypeChange* items = arena_push_array(arena, (uint64_t)count + 1, sizeof(TypeChange), _Alignof(TypeChange));
    TypeId* parameter_items = items != 0 ? arena_push_array(arena, (uint64_t)parameters + 1, sizeof(TypeId), _Alignof(TypeId)) : 0;
    TypeMember* member_items = parameter_items != 0 ? arena_push_array(arena, (uint64_t)members + 1, sizeof(TypeMember), _Alignof(TypeMember)) : 0;
    const bool valid = member_items != 0;
    if (valid)
    {
        *changes = (TypeChanges){
            .items = items,
            .capacity = count,
            .parameters = parameter_items,
            .parameter_capacity = parameters,
            .members = member_items,
            .member_capacity = members,
        };
    }
    else
    {
        arena_pop_to(arena, mark);
    }
    return valid;
}

static void type_changes_at(TypeTable* table, uint32_t position)
{
    TypeChanges* changes = table->changes;
    // Only the deferred bodies' tables record their changes (c_check_deferred.c, -flazy-system-headers), so the input
    // reader's walk, which calls this once a visit, nearly always has none (#1175).
    if (__builtin_expect(changes != 0, 0) && changes->position != position)
    {
        changes->position = position;
        changes->sequence = 0;
    }
}

// The index made again with `buckets` buckets from the first `types` types, in the memory it has and past it. If the
// arena can't hold it, the index stays as it was, as type_index_grow leaves it.
static bool type_index_rebuild(TypeTable* table, uint64_t buckets, uint32_t types)
{
    TypeIndex* index = &table->index;
    const uint64_t start = (uint64_t)((uint8_t*)index->slots - index->arena->base);
    arena_pop_to(index->arena, start);
    TypeSlot* slots = buckets <= UINT32_MAX ? arena_push_array(index->arena, buckets * BUSTER_TYPE_BUCKET_SLOTS, sizeof(TypeSlot), BUSTER_TYPE_COLUMN_ALIGNMENT) : 0;
    const bool result = slots != 0;
    if (result)
    {
        memset(slots, 0, buckets * BUSTER_TYPE_BUCKET_BYTES);
        index->slots = slots;
        index->bucket_count = (uint32_t)buckets;
        index->count = 0;
        const TypeColumns* columns = &table->types;
        for (uint32_t i = BUSTER_TYPE_BUILTIN_COUNT; i < types; i += 1)
        {
            if ((BUSTER_TYPE_HASHED_KINDS >> columns->kind[i] & 1) != 0)
            {
                const uint8_t flags = type_key_flags(columns->kind[i], columns->flags[i]);
                const uint64_t hash = columns->hash[i];
                type_index_place(index, i, columns->base[i], columns->extent[i], type_slot_key(columns->kind[i], flags, columns->extent[i], hash), hash);
            }
        }
    }
    else
    {
        arena_pop_to(index->arena, start);
        arena_push_array(index->arena, (uint64_t)index->bucket_count * BUSTER_TYPE_BUCKET_SLOTS, sizeof(TypeSlot), BUSTER_TYPE_COLUMN_ALIGNMENT);
    }
    return result;
}

// The index made the source's, slot for slot, in the memory it has and past it: the same types in the same slots, as a
// rebuild at the source's size would place them from the same columns. If the arena can't hold it, the index stays as
// it was, as type_index_rebuild leaves it.
static bool type_index_copy_slots(TypeTable* table, const TypeTable* source)
{
    TypeIndex* index = &table->index;
    const uint64_t start = (uint64_t)((uint8_t*)index->slots - index->arena->base);
    const uint64_t slots = (uint64_t)source->index.bucket_count * BUSTER_TYPE_BUCKET_SLOTS;
    arena_pop_to(index->arena, start);
    TypeSlot* copy = arena_push_array(index->arena, slots, sizeof(TypeSlot), BUSTER_TYPE_COLUMN_ALIGNMENT);
    const bool result = copy != 0;
    if (result)
    {
        memcpy(copy, source->index.slots, slots * sizeof(TypeSlot));
        index->slots = copy;
        index->bucket_count = source->index.bucket_count;
        index->count = source->index.count;
    }
    else
    {
        arena_pop_to(index->arena, start);
        arena_push_array(index->arena, (uint64_t)index->bucket_count * BUSTER_TYPE_BUCKET_SLOTS, sizeof(TypeSlot), BUSTER_TYPE_COLUMN_ALIGNMENT);
    }
    return result;
}

static bool type_copy(TypeTable* table, const TypeTable* source, Arena* arena, Arena* index_arena, TypeCapacity extra)
{
    const uint64_t length = arena->length;
    const uint64_t index_length = index_arena->length;
    const uint64_t types = (uint64_t)source->types.count - BUSTER_TYPE_BUILTIN_COUNT + extra.types;
    const uint64_t parameters = (uint64_t)source->parameters.count + extra.parameters;
    const uint64_t tags = (uint64_t)source->tags.count - 1 + extra.tags;
    const uint64_t members = (uint64_t)source->members.count - 1 + extra.members;
    const uint64_t lookups = (uint64_t)source->lookup.count - 1 + type_lookup_capacity(extra);
    const TypeCapacity capacity = {
        .types = types < UINT32_MAX ? (uint32_t)types : UINT32_MAX,
        .parameters = parameters < UINT32_MAX ? (uint32_t)parameters : UINT32_MAX,
        .tags = tags < UINT32_MAX ? (uint32_t)tags : UINT32_MAX,
        .members = members < UINT32_MAX ? (uint32_t)members : UINT32_MAX,
        .lookups = lookups < UINT32_MAX ? (uint32_t)lookups : UINT32_MAX,
    };
    bool valid = source->hidden_end == 0 && type_initialize(table, arena, index_arena, capacity);
    if (valid)
    {
        const uint64_t count = source->types.count;
        memcpy(table->types.kind, source->types.kind, count * sizeof(*table->types.kind));
        table->extended = source->extended;
        table->ext_vectors = source->ext_vectors;
        table->atomic_records = source->atomic_records;
        memcpy(table->types.code, source->types.code, count * sizeof(*table->types.code));
        memcpy(table->types.flags, source->types.flags, count * sizeof(*table->types.flags));
        memcpy(table->types.alignment_log2, source->types.alignment_log2, count * sizeof(*table->types.alignment_log2));
        memcpy(table->types.base, source->types.base, count * sizeof(*table->types.base));
        memcpy(table->types.index, source->types.index, count * sizeof(*table->types.index));
        memcpy(table->types.extent, source->types.extent, count * sizeof(*table->types.extent));
        memcpy(table->types.size, source->types.size, count * sizeof(*table->types.size));
        memcpy(table->types.hash, source->types.hash, count * sizeof(*table->types.hash));
        table->types.count = source->types.count;
        memcpy(table->parameters.type, source->parameters.type, (uint64_t)source->parameters.count * sizeof(TypeId));
        table->parameters.count = source->parameters.count;
        const uint64_t tag_count = source->tags.count;
        memcpy(table->tags.type, source->tags.type, tag_count * sizeof(*table->tags.type));
        memcpy(table->tags.name, source->tags.name, tag_count * sizeof(*table->tags.name));
        memcpy(table->tags.member_begin, source->tags.member_begin, tag_count * sizeof(*table->tags.member_begin));
        memcpy(table->tags.member_count, source->tags.member_count, tag_count * sizeof(*table->tags.member_count));
        memcpy(table->tags.completion, source->tags.completion, tag_count * sizeof(*table->tags.completion));
        memcpy(table->tags.packed, source->tags.packed, tag_count * sizeof(*table->tags.packed));
        memcpy(table->tags.ms_struct, source->tags.ms_struct, tag_count * sizeof(*table->tags.ms_struct));
        memcpy(table->tags.pack_log2, source->tags.pack_log2, tag_count * sizeof(*table->tags.pack_log2));
        memcpy(table->tags.alignment_log2, source->tags.alignment_log2, tag_count * sizeof(*table->tags.alignment_log2));
        memcpy(table->tags.lookup, source->tags.lookup, tag_count * sizeof(*table->tags.lookup));
        memcpy(table->tags.lookup_log2, source->tags.lookup_log2, tag_count * sizeof(*table->tags.lookup_log2));
        table->tags.count = source->tags.count;
        table->tags.completions = source->tags.completions;
        const uint64_t member_count = source->members.count;
        memcpy(table->members.name, source->members.name, member_count * sizeof(*table->members.name));
        memcpy(table->members.type, source->members.type, member_count * sizeof(*table->members.type));
        memcpy(table->members.bit_width, source->members.bit_width, member_count * sizeof(*table->members.bit_width));
        memcpy(table->members.alignment_log2, source->members.alignment_log2, member_count * sizeof(*table->members.alignment_log2));
        memcpy(table->members.flags, source->members.flags, member_count * sizeof(*table->members.flags));
        memcpy(table->members.offset, source->members.offset, member_count * sizeof(*table->members.offset));
        memcpy(table->members.count_name, source->members.count_name, member_count * sizeof(*table->members.count_name));
        table->members.count = source->members.count;
        memcpy(table->lookup.buckets, source->lookup.buckets, (uint64_t)source->lookup.count * sizeof(*table->lookup.buckets));
        table->lookup.count = source->lookup.count;
        table->last_error = source->last_error;
        table->char_unsigned = source->char_unsigned;
        table->atomic_inline_16 = source->atomic_inline_16;
        // type_find_or_add grows the index past 3 types a bucket. An index for every type the copy may hold would be mostly
        // empty, and its probes would miss the caches: #113's lanes each copy a unit's table with room for eight types a
        // token of its largest body, 232,048 for sqlite3.c's 3,256, an index of 8 MB a lane. So it has room for a slack
        // of types past the source's, no more than BUSTER_TYPE_COPY_INDEX_SLACK and than the source's own (512 at
        // least): a copy that outgrows it places its types again as any index does, in time proportional to those types.
        // A larger slack cost every copy its pages, a memset copy's first touches: 32 of the 55 to 63 page faults of a
        // lane's table on #697's units of a few hundred to a few thousand types. The source's index is copied as it is
        // where it has the room, its slots rather than its types placed again.
        const uint64_t own = source->index.count > BUSTER_TYPE_COPY_INDEX_MINIMUM ? source->index.count : BUSTER_TYPE_COPY_INDEX_MINIMUM;
        uint64_t slack = extra.types < BUSTER_TYPE_COPY_INDEX_SLACK ? extra.types : BUSTER_TYPE_COPY_INDEX_SLACK;
        slack = slack < own ? slack : own;
        if ((uint64_t)source->index.count + slack + 1 <= 3ull * source->index.bucket_count)
        {
            valid = type_index_copy_slots(table, source);
        }
        else
        {
            uint64_t buckets = BUSTER_TYPE_INITIAL_BUCKETS;
            while (buckets * 3 < (uint64_t)table->types.count + slack + 1)
            {
                buckets *= 2;
            }
            valid = type_index_rebuild(table, buckets, table->types.count);
        }
    }
    if (!valid)
    {
        arena_pop_to(arena, length);
        arena_pop_to(index_arena, index_length);
        *table = (TypeTable){
            .last_error = TYPE_ERROR_FULL,
        };
    }
    return valid;
}

static TypeViewMark type_view_mark(const TypeTable* table)
{
    return (TypeViewMark){
        .types = table->types.count,
        .parameters = table->parameters.count,
        .tags = table->tags.count,
        .members = table->members.count,
        .lookups = table->lookup.count,
        .completions = table->tags.completions,
        .buckets = table->index.bucket_count,
    };
}

static bool type_view_reset(TypeTable* table, TypeViewMark mark)
{
    bool valid = mark.types >= BUSTER_TYPE_BUILTIN_COUNT && mark.types <= table->types.count && mark.parameters <= table->parameters.count &&
                 mark.tags >= 1 && mark.tags <= table->tags.count && mark.members >= 1 && mark.members <= table->members.count &&
                 mark.lookups >= 1 && mark.lookups <= table->lookup.count;
    if (valid && table->index.bucket_count == mark.buckets)
    {
        // The types made since come out latest first: a later type's probe may pass an earlier one's slot, but no
        // earlier type's passes a later one's, which was empty when it was placed.
        TypeIndex* index = &table->index;
        const uint32_t wrap = index->bucket_count * BUSTER_TYPE_BUCKET_SLOTS - 1;
        for (uint32_t i = table->types.count; valid && i > mark.types; i -= 1)
        {
            const uint32_t type = i - 1;
            if ((BUSTER_TYPE_HASHED_KINDS >> table->types.kind[type] & 1) != 0)
            {
                uint32_t position = ((uint32_t)table->types.hash[type] & (index->bucket_count - 1)) * BUSTER_TYPE_BUCKET_SLOTS;
                uint32_t probes = 0;
                while (index->slots[position].index != type && probes <= wrap)
                {
                    position = (position + 1) & wrap;
                    probes += 1;
                }
                valid = probes <= wrap;
                if (valid)
                {
                    index->slots[position] = (TypeSlot){ 0 };
                    index->count -= 1;
                }
            }
        }
    }
    else if (valid)
    {
        valid = type_index_rebuild(table, mark.buckets, mark.types);
    }
    if (valid)
    {
        table->types.count = mark.types;
        table->parameters.count = mark.parameters;
        table->tags.count = mark.tags;
        table->members.count = mark.members;
        table->lookup.count = mark.lookups;
        table->tags.completions = mark.completions;
        table->hidden_begin = 0;
        table->hidden_end = 0;
    }
    return valid;
}

static void type_tag_withdraw(TypeTable* table, uint32_t tag, bool transparency_only)
{
    TypeColumns* types = &table->types;
    TypeTags* tags = &table->tags;
    const uint32_t index = type_index(tags->type[tag]);
    if (transparency_only)
    {
        types->flags[index] &= (uint8_t)~BUSTER_TYPE_FLAG_TRANSPARENT_UNION;
    }
    else
    {
        // As type_tag leaves a tag it has just made.
        types->code[index] = 0;
        types->flags[index] = 0;
        types->alignment_log2[index] = 0;
        types->base[index] = 0;
        types->size[index] = 0;
        tags->member_begin[tag] = 0;
        tags->member_count[tag] = 0;
        tags->completion[tag] = 0;
        tags->packed[tag] = 0;
        tags->ms_struct[tag] = 0;
        tags->pack_log2[tag] = 0;
        tags->alignment_log2[tag] = 0;
        tags->lookup[tag] = 0;
        tags->lookup_log2[tag] = 0;
    }
}

static void type_tag_restore(TypeTable* table, const TypeTable* source, uint32_t tag)
{
    TypeColumns* types = &table->types;
    TypeTags* tags = &table->tags;
    const uint32_t index = type_index(source->tags.type[tag]);
    types->code[index] = source->types.code[index];
    types->flags[index] = source->types.flags[index];
    types->alignment_log2[index] = source->types.alignment_log2[index];
    types->base[index] = source->types.base[index];
    types->size[index] = source->types.size[index];
    tags->member_begin[tag] = source->tags.member_begin[tag];
    tags->member_count[tag] = source->tags.member_count[tag];
    tags->completion[tag] = source->tags.completion[tag];
    tags->packed[tag] = source->tags.packed[tag];
    tags->ms_struct[tag] = source->tags.ms_struct[tag];
    tags->pack_log2[tag] = source->tags.pack_log2[tag];
    tags->alignment_log2[tag] = source->tags.alignment_log2[tag];
    tags->lookup[tag] = source->tags.lookup[tag];
    tags->lookup_log2[tag] = source->tags.lookup_log2[tag];
}

static TypeId type_replay_type(const TypeReplayPart* part, TypeId type)
{
    const uint32_t index = type_index(type);
    const TypeReplayPart* owner = part;
    while (owner != 0 && index < owner->base)
    {
        owner = owner->parent;
    }
    const uint32_t mapped = owner != 0 ? owner->types[index - owner->base] : index;
    return (TypeId)mapped << BUSTER_TYPE_INDEX_SHIFT | type_qualifiers(type);
}

static uint32_t type_replay_tag(const TypeReplayPart* part, uint32_t tag)
{
    const TypeReplayPart* owner = part;
    while (owner != 0 && tag < owner->tag_base)
    {
        owner = owner->parent;
    }
    return owner != 0 ? owner->tags[tag - owner->tag_base] : tag;
}

// Whether a tag change of `part` is for a tag the replay merged into an earlier one of its mark (TypeReplayPart.merged).
static bool type_replay_merged(const TypeReplayPart* part, const TypeChange* change)
{
    return change->tag >= part->tag_base && part->merged[change->tag - part->tag_base] != 0;
}

// Makes one change again in the replay's table, with `part`'s types mapped: whether it was made as it was recorded.
// marks and mark_tags are the replay's predefined types by mark.
static bool type_replay_change(TypeTable* table, TypeReplayPart* part, const TypeChange* change, TypeId* parameters, TypeMember* members, TypeId* marks,
                               uint32_t* mark_tags)
{
    const TypeChanges* changes = part->changes;
    const uint32_t local = type_index(change->type);
    bool valid = true;
    if (change->operation == BUSTER_TYPE_CHANGE_DERIVE)
    {
        // A function's parameters are types to map; its info word and parameter ABIs' words, after them, are not.
        const bool function = change->kind == BUSTER_TYPE_KIND_FUNCTION;
        const uint64_t parameter_count = function ? change->extent : 0;
        const uint64_t word_count = function ? type_function_words(changes->parameters + change->first, change->flags, change->extent) : 0;
        for (uint64_t i = 0; i < word_count; i += 1)
        {
            const TypeId word = changes->parameters[change->first + i];
            parameters[i] = i < parameter_count ? type_replay_type(part, word) : word;
        }
        const uint32_t made = type_find_or_add(table, change->kind, change->flags, type_replay_type(part, change->base), change->extent, parameters);
        valid = made != 0 && local >= part->base;
        if (valid)
        {
            part->types[local - part->base] = made;
        }
    }
    else if (change->operation == BUSTER_TYPE_CHANGE_TAG)
    {
        const uint32_t mark = change->predefined;
        valid = mark < BUSTER_TYPE_PREDEFINED_MARKS && local >= part->base && change->tag >= part->tag_base;
        const bool merged = valid && mark != 0 && marks[mark] != 0;
        if (merged)
        {
            part->types[local - part->base] = type_index(marks[mark]);
            part->tags[change->tag - part->tag_base] = mark_tags[mark];
        }
        else if (valid)
        {
            const TypeId made = type_tag(table, change->kind, change->name);
            valid = made != 0;
            if (valid)
            {
                part->types[local - part->base] = type_index(made);
                part->tags[change->tag - part->tag_base] = table->tags.count - 1;
                marks[mark] = mark != 0 ? made : 0;
                mark_tags[mark] = mark != 0 ? table->tags.count - 1 : 0;
            }
        }
        if (valid)
        {
            part->merged[change->tag - part->tag_base] = merged ? 1 : 0;
        }
    }
    else if (change->operation == BUSTER_TYPE_CHANGE_DEFINE && !type_replay_merged(part, change))
    {
        for (uint32_t i = 0; i < change->count; i += 1)
        {
            members[i] = changes->members[change->first + i];
            members[i].type = type_replay_type(part, members[i].type);
        }
        valid = type_record_define(table, type_replay_type(part, change->type), members, change->count, change->attributes);
    }
    else if (change->operation == BUSTER_TYPE_CHANGE_LAYOUT && !type_replay_merged(part, change))
    {
        valid = type_record_layout(table, type_replay_type(part, change->type));
    }
    else if (change->operation == BUSTER_TYPE_CHANGE_ENUM)
    {
        valid = type_enum_complete(table, type_replay_type(part, change->type), type_replay_type(part, change->base), change->alignment_log2);
    }
    else if (change->operation == BUSTER_TYPE_CHANGE_TRANSPARENT)
    {
        valid = type_mark_transparent_union(table, type_replay_type(part, change->type)).status == TYPE_TRANSPARENT_UNION_APPLIED;
    }
    return valid;
}

static bool type_replay(TypeTable* table, TypeReplayPart* parts, uint32_t count, Arena* scratch)
{
    const uint64_t mark = scratch->length;
    bool valid = count != 0 && table->changes == 0 && table->hidden_end == 0;
    uint64_t parameter_room = 0;
    uint32_t member_room = 0;
    for (uint32_t p = 0; valid && p < count; p += 1)
    {
        const TypeChanges* changes = parts[p].changes;
        valid = changes != 0 && !changes->full && (p == 0 || parts[p].parent == &parts[0]);
        for (uint32_t i = 0; valid && i < changes->count; i += 1)
        {
            const TypeChange* change = changes->items + i;
            // A function's words: its parameters, and at most an info word and a word per eight parameter ABIs.
            const uint64_t words = change->extent + 1 + (change->extent + BUSTER_TYPE_PARAMETER_ABIS_PER_WORD - 1) / BUSTER_TYPE_PARAMETER_ABIS_PER_WORD;
            parameter_room = change->operation == BUSTER_TYPE_CHANGE_DERIVE && change->kind == BUSTER_TYPE_KIND_FUNCTION && words > parameter_room ? words : parameter_room;
            member_room = change->operation == BUSTER_TYPE_CHANGE_DEFINE && change->count > member_room ? change->count : member_room;
        }
    }
    TypeId* parameters = valid ? arena_push_array(scratch, parameter_room + 1, sizeof(TypeId), _Alignof(TypeId)) : 0;
    TypeMember* members = parameters != 0 ? arena_push_array(scratch, (uint64_t)member_room + 1, sizeof(TypeMember), _Alignof(TypeMember)) : 0;
    valid = members != 0;
    TypeId marks[BUSTER_TYPE_PREDEFINED_MARKS] = { 0 };
    uint32_t mark_tags[BUSTER_TYPE_PREDEFINED_MARKS] = { 0 };
    // The top level's changes and the bodies' merged by position, the top level's first at a position both have.
    uint32_t top = 0;
    uint32_t body = 1;
    uint32_t item = 0;
    while (valid && (top < parts[0].changes->count || body < count))
    {
        if (body < count && item == parts[body].changes->count)
        {
            body += 1;
            item = 0;
        }
        else
        {
            const TypeChange* next_top = top < parts[0].changes->count ? parts[0].changes->items + top : 0;
            const TypeChange* next_body = body < count ? parts[body].changes->items + item : 0;
            const bool from_top = next_body == 0 || (next_top != 0 && next_top->position <= next_body->position);
            if (from_top)
            {
                valid = type_replay_change(table, &parts[0], next_top, parameters, members, marks, mark_tags);
                top += 1;
            }
            else
            {
                valid = type_replay_change(table, &parts[body], next_body, parameters, members, marks, mark_tags);
                item += 1;
            }
        }
    }
    // last_error's last write by key: a body's after the top level's at a position both have.
    bool written = false;
    uint32_t position = 0;
    uint32_t rank = 0;
    uint32_t sequence = 0;
    TypeError error = TYPE_ERROR_NONE;
    for (uint32_t p = 0; valid && p < count; p += 1)
    {
        const TypeChanges* changes = parts[p].changes;
        const uint32_t part_rank = p == 0 ? 0 : 1;
        const bool later = !written || changes->error_position > position ||
                           (changes->error_position == position && (part_rank > rank || (part_rank == rank && changes->error_sequence > sequence)));
        if (changes->error_written && later)
        {
            written = true;
            position = changes->error_position;
            rank = part_rank;
            sequence = changes->error_sequence;
            error = changes->error;
        }
    }
    if (valid && written)
    {
        table->last_error = error;
    }
    arena_pop_to(scratch, mark);
    return valid;
}
