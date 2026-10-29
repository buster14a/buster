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
// A typedef's aligned attribute: its base type with another alignment, higher or lower (clang's __m128i_u).
#define BUSTER_TYPE_KIND_ALIGNED 15
#define BUSTER_TYPE_KIND_STRUCT 16
#define BUSTER_TYPE_KIND_UNION 17
#define BUSTER_TYPE_KIND_ENUM 18
#define BUSTER_TYPE_KIND_COUNT 19

// Flags. The first two are part of a derived type's key. Functions additionally use the three record-only high
// bits for calling convention; pointers retain it through their base TypeId, never through inherited flags.
#define BUSTER_TYPE_FLAG_UNSIGNED 0x01
#define BUSTER_TYPE_FLAG_VARIADIC 0x01
#define BUSTER_TYPE_FLAG_PROTOTYPE 0x02
#define BUSTER_TYPE_FLAG_KEY 0x03
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
#define BUSTER_TYPE_FUNCTION_ABI_SHIFT 5
#define BUSTER_TYPE_FUNCTION_ABI_MASK 0xe0
#define BUSTER_TYPE_FLAG_FUNCTION_KEY (BUSTER_TYPE_FLAG_KEY | BUSTER_TYPE_FUNCTION_ABI_MASK)

// Semantic function identity on the supported x86-64 target. This is not a claim that code generation implements
// every convention. The ordinary constructor and explicit sysv_abi/cdecl have the same default identity.
typedef enum TypeFunctionABI
{
    TYPE_FUNCTION_ABI_SYSV,
    TYPE_FUNCTION_ABI_MICROSOFT,
    TYPE_FUNCTION_ABI_VECTOR,
    TYPE_FUNCTION_ABI_REG,
    TYPE_FUNCTION_ABI_PRESERVE_MOST,
    TYPE_FUNCTION_ABI_PRESERVE_ALL,
    TYPE_FUNCTION_ABI_PRESERVE_NONE,
    TYPE_FUNCTION_ABI_COUNT,
    TYPE_FUNCTION_ABI_INVALID = 255,
} TypeFunctionABI;

// A member's flags.
#define BUSTER_TYPE_MEMBER_PACKED 0x01
#define BUSTER_TYPE_MEMBER_BIT_FIELD 0x02
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
// pack_log2 and alignment_log2 are the layout's inputs (an enum's alignment too); the last two hold log2 + 1, 0 for
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
};

// A struct's or union's layout attributes: GNU's packed, #pragma pack's value (#124 gives it) and the aligned
// attribute, the last two as log2 + 1 or 0 for none.
typedef struct TypeRecordAttributes TypeRecordAttributes;
struct TypeRecordAttributes
{
    bool packed;
    uint8_t pack_log2;
    uint8_t alignment_log2;
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

// The spellings of identifier ids, for the printer: id i's bytes are bytes[i], lengths[i] of them, for i below count.
typedef struct TypeNames TypeNames;
struct TypeNames
{
    const uint8_t* const* bytes;
    const uint32_t* lengths;
    uint32_t count;
};

// The printer's options: BUSTER_TYPE_PRINT_C23 spells bool as C23 and clang in C23 mode do (bool, not _Bool).
#define BUSTER_TYPE_PRINT_C23 0x01

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
// The type without ALIGNED at its top: its base, with the ALIGNED type's qualifiers.
static __attribute__((unused)) TypeId type_strip_aligned(const TypeTable* table, TypeId type);

// The derived types, hash-consed: the same request gets the same TypeId, and so does an equal structure built in
// another order. Each returns 0, with last_error saying why, if the request breaks one of clang's rules (TypeError)
// or the table is full. The batch gives each request what the one-at-a-time constructors would, in request order.
static __attribute__((unused)) TypeId type_pointer(TypeTable* table, TypeId pointee);
static __attribute__((unused)) TypeId type_array(TypeTable* table, TypeId element, uint64_t length);
static __attribute__((unused)) TypeId type_incomplete_array(TypeTable* table, TypeId element);
// expression is the caller's id for the size expression; 0 is [*].
static __attribute__((unused)) TypeId type_variable_array(TypeTable* table, TypeId element, uint32_t expression);
// flags: BUSTER_TYPE_FLAG_PROTOTYPE and BUSTER_TYPE_FLAG_VARIADIC. Parameters must be adjusted (type_adjust_parameter);
// their qualifiers are dropped.
static __attribute__((unused)) TypeId type_function(TypeTable* table, TypeId result, const TypeId* parameters, uint32_t count, uint8_t flags);
// As type_function, with an explicit calling convention. Vectorcall and regcall cannot be variadic.
static __attribute__((unused)) TypeId type_function_abi(TypeTable* table, TypeId result, const TypeId* parameters, uint32_t count, uint8_t flags, TypeFunctionABI abi);
// A vector of `count` elements; ext chooses clang's ext_vector_type over GNU's vector_size (type_vector_size converts
// the latter's size in bytes).
static __attribute__((unused)) TypeId type_vector(TypeTable* table, TypeId element, uint32_t count, bool ext);
static __attribute__((unused)) TypeId type_vector_size(TypeTable* table, TypeId element, uint64_t bytes);
// GNU mode replaces a declaration's type, including its existing qualifiers and aligned typedef wrappers, as
// clang does. This is NOT a qualifier-preserving derivation; the checker owns any later declaration qualifiers.
// An enum typedef becomes an integer without changing its tag. For a mode on an enum tag itself, the caller passes
// its underlying type (int before completion) and supplies the result to type_enum_complete. mode is a bounded,
// non-NUL-terminated identifier, with either plain or paired-underscore spelling. No source diagnostics are printed.
static __attribute__((unused)) TypeId type_machine_mode(TypeTable* table, TypeId base, String8 mode);
static __attribute__((unused)) TypeId type_atomic(TypeTable* table, TypeId value);
static __attribute__((unused)) TypeId type_aligned(TypeTable* table, TypeId base, uint32_t alignment_log2);
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
#define BUSTER_TYPE_TAG_BYTES (sizeof(TypeId) + 5 * sizeof(uint32_t) + 4 * sizeof(uint8_t))
#define BUSTER_TYPE_TAG_COLUMNS 10
#define BUSTER_TYPE_MEMBER_BYTES (sizeof(uint32_t) + sizeof(TypeId) + sizeof(uint32_t) + 2 * sizeof(uint8_t) + sizeof(uint64_t))
#define BUSTER_TYPE_MEMBER_COLUMNS 6
// The index: four slots per 64-byte bucket, at most three quarters full, from 64 buckets (4 KB).
#define BUSTER_TYPE_BUCKET_SLOTS 4
#define BUSTER_TYPE_BUCKET_BYTES 64
#define BUSTER_TYPE_INITIAL_BUCKETS 64
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

// The index whose size, code and completeness a type has: an ALIGNED type's base's.
static uint32_t type_resolved_index(const TypeTable* table, TypeId type)
{
    uint32_t index = type >> BUSTER_TYPE_INDEX_SHIFT;
    if (table->types.kind[index] == BUSTER_TYPE_KIND_ALIGNED)
    {
        index = table->types.base[index] >> BUSTER_TYPE_INDEX_SHIFT;
    }
    return index;
}

static uint64_t type_size(const TypeTable* table, TypeId type)
{
    return table->types.size[type_resolved_index(table, type)];
}

static uint32_t type_alignment_log2(const TypeTable* table, TypeId type)
{
    return table->types.alignment_log2[type >> BUSTER_TYPE_INDEX_SHIFT];
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

static TypeId type_strip_aligned(const TypeTable* table, TypeId type)
{
    TypeId result = type;
    if (type_kind(table, type) == BUSTER_TYPE_KIND_ALIGNED)
    {
        result = table->types.base[type >> BUSTER_TYPE_INDEX_SHIFT] | type_qualifiers(type);
    }
    return result;
}

static bool type_kind_is_array(uint8_t kind)
{
    return kind == BUSTER_TYPE_KIND_ARRAY || kind == BUSTER_TYPE_KIND_INCOMPLETE_ARRAY || kind == BUSTER_TYPE_KIND_VARIABLE_ARRAY;
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
// qualified arrays, as clang's canonical types have them) and ALIGNED.
#define BUSTER_TYPE_HOISTING_KINDS                                                                                     \
    ((1ull << BUSTER_TYPE_KIND_ARRAY) | (1ull << BUSTER_TYPE_KIND_INCOMPLETE_ARRAY) | (1ull << BUSTER_TYPE_KIND_VARIABLE_ARRAY) | \
     (1ull << BUSTER_TYPE_KIND_ALIGNED))
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
    // Functions have a 32-bit parameter count. Their ABI can share the WIDE bit because key matching dispatches
    // on FUNCTION before reading WIDE; non-functions never carry the function-only high flag bits.
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
        result = extent == 0 || memcmp(table->parameters.type + table->types.index[index], parameters, extent * sizeof(TypeId)) == 0;
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
        const uint8_t abi = (flags & BUSTER_TYPE_FUNCTION_ABI_MASK) >> BUSTER_TYPE_FUNCTION_ABI_SHIFT;
        const bool variadic = (flags & BUSTER_TYPE_FLAG_VARIADIC) != 0;
        if ((flags & ~BUSTER_TYPE_FLAG_FUNCTION_KEY) != 0 || abi >= TYPE_FUNCTION_ABI_COUNT ||
            (!prototype && ((flags & BUSTER_TYPE_FLAG_KEY) != 0 || extent != 0)) || extent > UINT32_MAX ||
            (variadic && (abi == TYPE_FUNCTION_ABI_VECTOR || abi == TYPE_FUNCTION_ABI_REG)))
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
        if (flags != 0 || extent == 0 || extent > UINT32_MAX || !type_vector_element_is_valid(table, element, ext))
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
        else if (type_qualifiers(base) != 0 || type_kind_is_array(value_kind) || value_kind == BUSTER_TYPE_KIND_FUNCTION || value_kind == BUSTER_TYPE_KIND_ATOMIC ||
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
    const uint64_t parameter_count = kind == BUSTER_TYPE_KIND_FUNCTION ? extent : 0;
    uint32_t result = 0;
    if (types->count < types->capacity && parameter_count <= parameter_column->capacity - parameter_column->count)
    {
        result = types->count;
        types->kind[result] = kind;
        types->code[result] = properties.code;
        types->flags[result] = flags | properties.flags;
        types->alignment_log2[result] = properties.alignment_log2;
        types->base[result] = base;
        types->index[result] = kind == BUSTER_TYPE_KIND_FUNCTION ? parameter_column->count : 0;
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
    const uint64_t parameters_hash = kind == BUSTER_TYPE_KIND_FUNCTION ? type_parameters_hash(parameters, (uint32_t)extent) : 0;
    const uint64_t hash = type_key_hash(kind, flags, base, extent, parameters_hash);
    const uint32_t key = type_slot_key(kind, flags, extent, hash);
    uint32_t slot = 0;
    uint32_t result = type_index_find(table, key, base, extent, parameters, hash, &slot);
    if (result == 0)
    {
        TypeProperties properties;
        TypeError error = type_check_derivation(table, kind, flags, base, extent, parameters, &properties);
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
            type_change_record(table, change, parameters, kind == BUSTER_TYPE_KIND_FUNCTION ? extent : 0, 0, 0);
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
                    // clang's mode produces a GNU vector even when the old type was an ext_vector_type.
                    result = type_vector(table, result, (uint32_t)vector_count, false);
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

static TypeFunctionABI type_function_abi_kind(const TypeTable* table, TypeId function)
{
    TypeFunctionABI result = TYPE_FUNCTION_ABI_INVALID;
    if (type_is_valid(table, function))
    {
        const TypeId stripped = type_strip_aligned(table, function);
        if (type_kind(table, stripped) == BUSTER_TYPE_KIND_FUNCTION)
        {
            result = (TypeFunctionABI)((table->types.flags[type_index(stripped)] & BUSTER_TYPE_FUNCTION_ABI_MASK) >> BUSTER_TYPE_FUNCTION_ABI_SHIFT);
        }
    }
    return result;
}

// Parameters are stored unqualified: a qualified one is copied without its qualifiers into scratch memory first.
static TypeId type_function_abi(TypeTable* table, TypeId result_type, const TypeId* parameters, uint32_t count, uint8_t flags, TypeFunctionABI abi)
{
    TypeId result = 0;
    Scratch scratch = scratch_begin(0);
    const TypeId* normal = parameters;
    bool qualified = false;
    const bool valid = (flags & ~BUSTER_TYPE_FLAG_KEY) == 0 && (uint32_t)abi < TYPE_FUNCTION_ABI_COUNT && (count == 0 || parameters != 0);
    for (uint32_t i = 0; valid && i < count; i += 1)
    {
        qualified = qualified || type_qualifiers(parameters[i]) != 0;
    }
    if (qualified)
    {
        TypeId* copy = BUSTER_ARENA_PUSH_ARRAY(scratch.arena, TypeId, count);
        if (copy != 0)
        {
            for (uint32_t i = 0; i < count; i += 1)
            {
                copy[i] = type_unqualified(parameters[i]);
            }
        }
        normal = copy;
    }
    if (!valid)
    {
        type_set_error(table, TYPE_ERROR_OPERAND);
    }
    else if (normal == 0 && count != 0)
    {
        type_set_error(table, TYPE_ERROR_FULL);
    }
    else if (!type_is_valid(table, result_type))
    {
        type_set_error(table, TYPE_ERROR_OPERAND);
    }
    else
    {
        const uint8_t key_flags = flags | (uint8_t)((uint32_t)abi << BUSTER_TYPE_FUNCTION_ABI_SHIFT);
        const uint32_t index = type_find_or_add(table, BUSTER_TYPE_KIND_FUNCTION, key_flags, result_type, count, normal);
        result = (TypeId)index << BUSTER_TYPE_INDEX_SHIFT;
    }
    scratch_end(scratch);
    return result;
}

static TypeId type_function(TypeTable* table, TypeId result_type, const TypeId* parameters, uint32_t count, uint8_t flags)
{
    return type_function_abi(table, result_type, parameters, count, flags, TYPE_FUNCTION_ABI_SYSV);
}

static TypeId type_adjust_parameter(TypeTable* table, TypeId type)
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
// buckets prefetched, then each looked up with type_index_find_vector. The requests it can't settle that way (a miss,
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
        const uint32_t mask = table->index.bucket_count - 1;
        for (uint32_t lane = 0; lane < batch; lane += 1)
        {
            _mm_prefetch((const char*)(table->index.slots + (uint64_t)((uint32_t)hashes[lane] & mask) * BUSTER_TYPE_BUCKET_SLOTS), _MM_HINT_T0);
        }
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
        if (kind == BUSTER_TYPE_KIND_FUNCTION || kind == BUSTER_TYPE_KIND_VOID || (table->types.flags[type_index(member->type)] & BUSTER_TYPE_FLAG_VARIABLY_MODIFIED) != 0)
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
            columns->flags[begin + i] = (member->packed ? BUSTER_TYPE_MEMBER_PACKED : 0) | (bit_field ? BUSTER_TYPE_MEMBER_BIT_FIELD : 0);
            columns->offset[begin + i] = 0;
        }
        columns->count += count;
        table->tags.member_begin[tag] = begin;
        table->tags.member_count[tag] = count;
        table->tags.packed[tag] = attributes.packed ? 1 : 0;
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

static uint32_t type_lookup_member(const TypeTable* table, TypeId record, uint32_t name, uint64_t* offset, uint32_t* qualifiers)
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
// LayoutBitField and FinishLayout, without what only ms_struct, AIX and C++ records do). In bits, from a data size
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

// A member's inputs: its type's size (an aligned typedef's base's) and alignment, in bits.
BUSTER_INLINE TypeLayoutInput type_layout_input(const TypeLayoutColumns* columns, uint32_t member)
{
    const uint32_t index = columns->member_type[member] >> BUSTER_TYPE_INDEX_SHIFT;
    const uint32_t resolved = columns->kind[index] == BUSTER_TYPE_KIND_ALIGNED ? columns->base[index] >> BUSTER_TYPE_INDEX_SHIFT : index;
    const uint8_t alignment_log2 = columns->member_alignment_log2[member];
    const uint8_t flags = columns->member_flags[member];
    TypeLayoutInput result = {
        .size = columns->size[resolved] * 8,
        .alignment = 8ull << columns->alignment_log2[index],
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
    const uint64_t pack = tags->pack_log2[tag] != 0 ? 8ull << (tags->pack_log2[tag] - 1) : 0;
    const uint32_t begin = tags->member_begin[tag];
    const uint32_t count = tags->member_count[tag];
    uint64_t alignment = tags->alignment_log2[tag] != 0 ? 1ull << (tags->alignment_log2[tag] - 1) : 1;
    uint64_t data_size = 0;
    uint64_t unfilled = 0;
    uint64_t size = 0;
    bool fits = true;
    for (uint32_t i = 0; fits && i < count; i += 1)
    {
        const uint32_t member = begin + i;
        const TypeLayoutInput input = type_layout_input(&columns, member);
        const bool packed = record_packed || input.packed;
        uint64_t offset = 0;
        uint64_t raise = 0;
        if (input.bit_field)
        {
            const uint64_t width = input.width;
            offset = is_union ? 0 : data_size - unfilled;
            uint64_t field_alignment = packed && width != 0 ? 1 : input.alignment;
            uint64_t unpacked_alignment = input.alignment;
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
            if (width == 0 || (pack == 0 && (offset & (field_alignment - 1)) + width > input.size))
            {
                offset = type_align_up(offset, field_alignment);
            }
            else if (input.explicit_alignment != 0 && (pack == 0 || input.explicit_alignment <= pack))
            {
                offset = type_align_up(offset, input.explicit_alignment);
            }
            raise = input.named ? field_alignment : 1;
            if (is_union)
            {
                data_size = type_maximum(data_size, type_align_up(width, 8));
            }
            else
            {
                const uint64_t end = offset + width;
                data_size = type_align_up(end, 8);
                unfilled = data_size - end;
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
                for (uint32_t member = 1; result.status == TYPE_TRANSPARENT_UNION_APPLIED && member < count; member += 1)
                {
                    const TypeId type = table->members.type[begin + member];
                    if (type_size(table, type) != size || type_alignment_log2(table, type) != alignment)
                    {
                        result.status = type_size(table, type) != size ? TYPE_TRANSPARENT_UNION_SIZE : TYPE_TRANSPARENT_UNION_ALIGNMENT;
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
            layout_log2[first + m] = columns.alignment_log2[index];
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
    const __mmask16 pack_lanes = _mm512_test_epi32_mask(pack, pack);
    __mmask16 overflow = (__mmask16)too_aligned;
    __m512i data_size = zero;
    __m512i unfilled = zero;
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
            const __m512i bit_data = _mm512_mask_max_epu32(bit_end_bytes, union_lanes, data_size, type_align_up_vector(width, byte));
            const __m512i bit_unfilled = _mm512_mask_mov_epi32(_mm512_sub_epi32(bit_end_bytes, bit_end), union_lanes, unfilled);
            const __m512i bit_raise = _mm512_mask_mov_epi32(one, named, field_alignment);
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
static TypeId type_default_argument_promote(const TypeTable* table, TypeId type)
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
static bool type_merge_shallow(const TypeTable* table, TypeId left, TypeId right, bool* leaf)
{
    const TypeId l = type_strip_aligned(table, left);
    const TypeId r = type_strip_aligned(table, right);
    const uint8_t left_kind = type_kind(table, l);
    const uint8_t right_kind = type_kind(table, r);
    bool result = true;
    *leaf = true;
    if (l == r)
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
        result = result && type_function_abi_kind(table, l) == type_function_abi_kind(table, r);
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
           types->extent[li] == types->extent[ri] && (kind == BUSTER_TYPE_KIND_POINTER || type_kind_is_array(kind) || kind == BUSTER_TYPE_KIND_FUNCTION || kind == BUSTER_TYPE_KIND_ATOMIC);
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
                if (l == r)
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
            result = type_function_abi(table, merged, results + 1, count, left_flags & BUSTER_TYPE_FLAG_KEY, type_function_abi_kind(table, l));
        }
        else if (left_prototype || right_prototype)
        {
            const TypeId prototype = left_prototype ? l : r;
            const TypeId* parameters = type_parameters(table, prototype, &count);
            result = type_function_abi(table, merged, parameters, count, table->types.flags[type_index(prototype)] & BUSTER_TYPE_FLAG_KEY, type_function_abi_kind(table, prototype));
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
    char* begin;
    uint64_t length;
    void* frames;
    uint64_t capacity;
    uint64_t depth;
    Arena* scratch;
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

// AppendTypeQualList, in C99 and later (restrict spelled as such).
static void type_print_qualifiers(TypePrinter* printer, uint32_t qualifiers)
{
    const char* names[3] = {
        "const",
        "volatile",
        "restrict",
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
    }
    else if (kind == BUSTER_TYPE_KIND_BIT_INT)
    {
        type_print_text(printer, (table->types.flags[index] & BUSTER_TYPE_FLAG_UNSIGNED) != 0 ? "unsigned _BitInt(" : "_BitInt(");
        type_print_number(printer, table->types.extent[index]);
        type_print_text(printer, ")");
        type_print_space(printer);
    }
    else if (kind == BUSTER_TYPE_KIND_COMPLEX)
    {
        type_print_text(printer, "_Complex ");
        type_print_push(printer, BUSTER_TYPE_PRINT_BEFORE, base, false, false, 0);
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
        type_print_text(printer, kind == BUSTER_TYPE_KIND_STRUCT ? "struct " : kind == BUSTER_TYPE_KIND_UNION ? "union " : "enum ");
        if (name != 0 && printer->names != 0 && name < printer->names->count)
        {
            type_print_bytes(printer, (const char*)printer->names->bytes[name], printer->names->lengths[name]);
        }
        else
        {
            type_print_text(printer, "(unnamed)");
        }
        type_print_space(printer);
    }
    else if (kind == BUSTER_TYPE_KIND_POINTER)
    {
        type_print_push(printer, BUSTER_TYPE_PRINT_POINTER_END, type, printer->empty, false, 0);
        printer->empty = false;
        type_print_push(printer, BUSTER_TYPE_PRINT_BEFORE, base, false, false, 0);
    }
    else if (type_kind_is_array(kind) || kind == BUSTER_TYPE_KIND_EXT_VECTOR)
    {
        type_print_push(printer, BUSTER_TYPE_PRINT_BEFORE, base, false, false, 0);
    }
    else if (kind == BUSTER_TYPE_KIND_FUNCTION)
    {
        type_print_push(printer, BUSTER_TYPE_PRINT_FUNCTION_END, type, printer->empty, false, 0);
        printer->empty = false;
        type_print_push(printer, BUSTER_TYPE_PRINT_BEFORE, base, false, false, 0);
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
        type_print_push(printer, BUSTER_TYPE_PRINT_AFTER, base, false, false, 0);
    }
    else if (kind == BUSTER_TYPE_KIND_EXT_VECTOR)
    {
        type_print_push(printer, BUSTER_TYPE_PRINT_EXT_VECTOR_END, type, false, false, 0);
        type_print_push(printer, BUSTER_TYPE_PRINT_AFTER, base, false, false, 0);
    }
    else if (kind == BUSTER_TYPE_KIND_POINTER)
    {
        type_print_push(printer, BUSTER_TYPE_PRINT_RESTORE, type, printer->empty, false, 0);
        printer->empty = false;
        if (type_kind_is_array(type_kind(table, type_strip_aligned(table, base))))
        {
            type_print_text(printer, ")");
        }
        type_print_push(printer, BUSTER_TYPE_PRINT_AFTER, base, false, false, 0);
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
            type_print_text(printer, kind == BUSTER_TYPE_KIND_VARIABLE_ARRAY && table->types.extent[index] == 0 ? "[*]" : "[]");
        }
        type_print_push(printer, BUSTER_TYPE_PRINT_AFTER, base, false, false, 0);
    }
    else if (kind == BUSTER_TYPE_KIND_FUNCTION)
    {
        if (!printer->empty)
        {
            type_print_text(printer, ")");
        }
        type_print_push(printer, BUSTER_TYPE_PRINT_RESTORE, type, printer->empty, false, 0);
        printer->empty = false;
        type_print_push(printer, BUSTER_TYPE_PRINT_AFTER, base, false, false, 0);
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
    if (frame.operation == BUSTER_TYPE_PRINT_TYPE)
    {
        // TypePrinter::print: the placeholder is empty but at the top, where it is the name.
        const bool named = frame.after && printer->name.length != 0;
        type_print_push(printer, BUSTER_TYPE_PRINT_RESTORE, frame.type, printer->empty, false, 0);
        printer->empty = !named;
        type_print_push(printer, BUSTER_TYPE_PRINT_AFTER, frame.type, false, false, 0);
        if (named)
        {
            type_print_push(printer, BUSTER_TYPE_PRINT_NAME, frame.type, false, false, 0);
        }
        type_print_push(printer, BUSTER_TYPE_PRINT_BEFORE, frame.type, false, false, 0);
    }
    else if (frame.operation == BUSTER_TYPE_PRINT_RESTORE)
    {
        printer->empty = frame.saved;
    }
    else if (frame.operation == BUSTER_TYPE_PRINT_BEFORE)
    {
        const TypeId type = type_strip_aligned(table, frame.type);
        const uint32_t qualifiers = type_qualifiers(type);
        const bool prefix = type_print_can_prefix(table, type);
        if (prefix && qualifiers != 0)
        {
            type_print_qualifiers(printer, qualifiers);
            type_print_text(printer, " ");
        }
        const bool after = !prefix && qualifiers != 0;
        type_print_push(printer, BUSTER_TYPE_PRINT_BEFORE_END, type, printer->empty, after, 0);
        if (after)
        {
            printer->empty = false;
        }
        type_print_before_kind(printer, type_unqualified(type));
    }
    else if (frame.operation == BUSTER_TYPE_PRINT_BEFORE_END)
    {
        if (frame.after)
        {
            type_print_qualifiers(printer, type_qualifiers(frame.type));
            if (!frame.saved)
            {
                type_print_text(printer, " ");
            }
        }
        printer->empty = frame.saved;
    }
    else if (frame.operation == BUSTER_TYPE_PRINT_AFTER)
    {
        type_print_after_kind(printer, type_unqualified(type_strip_aligned(table, frame.type)));
    }
    else if (frame.operation == BUSTER_TYPE_PRINT_POINTER_END)
    {
        const TypeId pointee = type_strip_aligned(table, table->types.base[type_index(frame.type)]);
        type_print_text(printer, type_kind_is_array(type_kind(table, pointee)) ? "(*" : "*");
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
    else if (frame.operation == BUSTER_TYPE_PRINT_PARAMETERS)
    {
        uint32_t count = 0;
        const TypeId* parameters = type_parameters(table, frame.type, &count);
        if (frame.step < count)
        {
            type_print_text(printer, frame.step != 0 ? ", " : "");
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
        };
        const TypeFunctionABI abi = type_function_abi_kind(table, frame.type);
        if ((uint32_t)abi < sizeof(spellings) / sizeof(spellings[0]))
        {
            type_print_text(printer, " __attribute__((");
            type_print_text(printer, spellings[abi]);
            type_print_text(printer, "))");
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
    scratch_end(scratch);
    return result;
}

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
    if (changes != 0 && changes->position != position)
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
        table->members.count = source->members.count;
        memcpy(table->lookup.buckets, source->lookup.buckets, (uint64_t)source->lookup.count * sizeof(*table->lookup.buckets));
        table->lookup.count = source->lookup.count;
        table->last_error = source->last_error;
        table->char_unsigned = source->char_unsigned;
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
        const uint64_t parameter_count = change->kind == BUSTER_TYPE_KIND_FUNCTION ? change->extent : 0;
        for (uint64_t i = 0; i < parameter_count; i += 1)
        {
            parameters[i] = type_replay_type(part, changes->parameters[change->first + i]);
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
            parameter_room = change->operation == BUSTER_TYPE_CHANGE_DERIVE && change->kind == BUSTER_TYPE_KIND_FUNCTION && change->extent > parameter_room ?
                             change->extent : parameter_room;
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
