#pragma once

// Canonical typed IR API over the record shapes in model.h: program/module/
// function construction, ABI classification, label-provenance queries,
// validation (run it before machine selection or Wasm emission), and
// printing. Everything is arena-owned and integer-ID based.

#include <buster/lib/arena.h>
#include <buster/lib/compiler/ir/model.h>
#include <buster/lib/target.h>

typedef struct IrFunctionId IrFunctionId;
struct IrFunctionId
{
    IrIdUnderlying value;
};

typedef struct IrBlockId IrBlockId;
struct IrBlockId
{
    IrIdUnderlying value;
};

typedef struct IrInstructionId IrInstructionId;
struct IrInstructionId
{
    IrIdUnderlying value;
};

typedef struct IrValueId IrValueId;
struct IrValueId
{
    IrIdUnderlying value;
};

#define IR_FUNCTION_ID_INVALID ((IrFunctionId){.value = IR_ID_UNDERLYING_INVALID})
#define IR_BLOCK_ID_INVALID ((IrBlockId){.value = IR_ID_UNDERLYING_INVALID})
#define IR_INSTRUCTION_ID_INVALID ((IrInstructionId){.value = IR_ID_UNDERLYING_INVALID})
#define IR_VALUE_ID_INVALID ((IrValueId){.value = IR_ID_UNDERLYING_INVALID})

BUSTER_CT_CHECK(sizeof(IrFunctionId) == sizeof(IrIdUnderlying));
BUSTER_CT_CHECK(sizeof(IrBlockId) == sizeof(IrIdUnderlying));
BUSTER_CT_CHECK(sizeof(IrInstructionId) == sizeof(IrIdUnderlying));
BUSTER_CT_CHECK(sizeof(IrValueId) == sizeof(IrIdUnderlying));

typedef enum IrValueCategory
{
    IR_VALUE_VALUE,
    IR_VALUE_PLACE,
    IR_VALUE_COUNT,
} IrValueCategory;

typedef enum IrOpcode
{
    IR_OPCODE_ARGUMENT,
    IR_OPCODE_LOCAL,
    IR_OPCODE_STACK_ALLOCATE,
    IR_OPCODE_STACK_SAVE,
    IR_OPCODE_STACK_RESTORE,
    IR_OPCODE_GLOBAL,
    IR_OPCODE_LOAD,
    IR_OPCODE_STORE,
    IR_OPCODE_ATOMIC_LOAD,
    IR_OPCODE_ATOMIC_STORE,
    IR_OPCODE_ATOMIC_READ_MODIFY_WRITE,
    IR_OPCODE_ATOMIC_COMPARE_EXCHANGE,
    IR_OPCODE_ATOMIC_FENCE,
    IR_OPCODE_CLEAR_INSTRUCTION_CACHE,
    IR_OPCODE_CONSTANT_INTEGER,
    IR_OPCODE_CONSTANT_FLOAT,
    IR_OPCODE_CONSTANT_STRING,
    IR_OPCODE_UNDEFINED,
    IR_OPCODE_FUNCTION,
    IR_OPCODE_ARRAY,
    IR_OPCODE_AGGREGATE,
    IR_OPCODE_LENGTH,
    IR_OPCODE_INDEX,
    IR_OPCODE_SLICE,
    IR_OPCODE_FIELD,
    IR_OPCODE_ENUM,
    IR_OPCODE_CALL,
    IR_OPCODE_CAST,
    IR_OPCODE_ADDRESS_OF,
    IR_OPCODE_DEREFERENCE,
    IR_OPCODE_UNARY,
    IR_OPCODE_BINARY,
    IR_OPCODE_REVERSE,
    IR_OPCODE_VA_START,
    IR_OPCODE_VA_COPY,
    IR_OPCODE_VA_END,
    IR_OPCODE_VA_ARG,
    IR_OPCODE_INLINE_ASSEMBLY,
    IR_OPCODE_SIMD,
    IR_OPCODE_LABEL_ADDRESS,
    IR_OPCODE_BRANCH,
    IR_OPCODE_BRANCH_IF,
    IR_OPCODE_SWITCH,
    IR_OPCODE_INDIRECT_BRANCH,
    IR_OPCODE_RETURN,
    IR_OPCODE_DEBUG_TRAP,
    IR_OPCODE_UNREACHABLE,
    IR_OPCODE_COUNT,
} IrOpcode;

typedef enum IrMemoryOrder
{
    IR_MEMORY_ORDER_RELAXED,
    IR_MEMORY_ORDER_CONSUME,
    IR_MEMORY_ORDER_ACQUIRE,
    IR_MEMORY_ORDER_RELEASE,
    IR_MEMORY_ORDER_ACQUIRE_RELEASE,
    IR_MEMORY_ORDER_SEQUENTIAL,
    IR_MEMORY_ORDER_COUNT,
} IrMemoryOrder;

typedef enum IrAtomicOperation
{
    IR_ATOMIC_ADD,
    IR_ATOMIC_SUBTRACT,
    IR_ATOMIC_BITWISE_AND,
    IR_ATOMIC_BITWISE_OR,
    IR_ATOMIC_BITWISE_XOR,
    IR_ATOMIC_EXCHANGE,
    IR_ATOMIC_OPERATION_COUNT,
} IrAtomicOperation;

// Every class below IR_INLINE_ASSEMBLY_CONSTRAINT_R names one architectural
// register the operand must occupy; R is the generic "any register" class the
// emitter allocates. Keeping the fixed classes contiguous and below R lets the
// "is this operand pinned" question stay a single comparison, so the order is
// load-bearing rather than cosmetic. SI/DI carry GNU's 'S'/'D' letters; the
// numbered classes have no constraint letter at all and are reachable only
// through a local register variable, which is how musl passes syscall
// arguments four through six. The callee-saved r12-r15 are deliberately absent:
// the emitter's asm operand pool is the caller-saved set, and pinning a
// callee-saved register would need prologue preservation that only the
// B/RBX path has.
typedef enum IrInlineAssemblyConstraint
{
    IR_INLINE_ASSEMBLY_CONSTRAINT_A,
    IR_INLINE_ASSEMBLY_CONSTRAINT_B,
    IR_INLINE_ASSEMBLY_CONSTRAINT_C,
    IR_INLINE_ASSEMBLY_CONSTRAINT_D,
    IR_INLINE_ASSEMBLY_CONSTRAINT_SI,
    IR_INLINE_ASSEMBLY_CONSTRAINT_DI,
    IR_INLINE_ASSEMBLY_CONSTRAINT_R8,
    IR_INLINE_ASSEMBLY_CONSTRAINT_R9,
    IR_INLINE_ASSEMBLY_CONSTRAINT_R10,
    IR_INLINE_ASSEMBLY_CONSTRAINT_R11,
    IR_INLINE_ASSEMBLY_CONSTRAINT_R,
    IR_INLINE_ASSEMBLY_CONSTRAINT_COUNT,
} IrInlineAssemblyConstraint;

#define IR_INLINE_ASSEMBLY_CONSTRAINT_IS_FIXED(class) ((class) < (u64)IR_INLINE_ASSEMBLY_CONSTRAINT_R)

#define IR_INLINE_ASSEMBLY_OPERAND_CLASS_INTEGER 0
#define IR_INLINE_ASSEMBLY_OPERAND_CLASS_POINTER 1
#define IR_INLINE_ASSEMBLY_OPERAND_CLASS_INVALID UINT32_MAX

#define IR_INLINE_ASSEMBLY_CONSTRAINT_OUTPUT ((u64)1 << 8)
#define IR_INLINE_ASSEMBLY_CONSTRAINT_READ_WRITE ((u64)1 << 9)
#define IR_INLINE_ASSEMBLY_CONSTRAINT_MATCH ((u64)1 << 10)
#define IR_INLINE_ASSEMBLY_CONSTRAINT_MATCH_INDEX_SHIFT 16
#define IR_INLINE_ASSEMBLY_CONSTRAINT_MATCH_INDEX_MASK (UINT64_C(0xffffffff) << IR_INLINE_ASSEMBLY_CONSTRAINT_MATCH_INDEX_SHIFT)
#define IR_INLINE_ASSEMBLY_CONSTRAINT_CLASS_MASK UINT64_C(0xff)
#define IR_INLINE_ASSEMBLY_CONSTRAINT_KNOWN_MASK                                                                                       \
    (IR_INLINE_ASSEMBLY_CONSTRAINT_CLASS_MASK | IR_INLINE_ASSEMBLY_CONSTRAINT_OUTPUT | IR_INLINE_ASSEMBLY_CONSTRAINT_READ_WRITE |       \
     IR_INLINE_ASSEMBLY_CONSTRAINT_MATCH | IR_INLINE_ASSEMBLY_CONSTRAINT_MATCH_INDEX_MASK)
#define IR_INLINE_ASSEMBLY_CONSTRAINT_MATCH_INDEX(constraint)                                                                          \
    ((u32)(((constraint) & IR_INLINE_ASSEMBLY_CONSTRAINT_MATCH_INDEX_MASK) >> IR_INLINE_ASSEMBLY_CONSTRAINT_MATCH_INDEX_SHIFT))

typedef enum IrConversionOperation
{
    IR_CONVERSION_IDENTITY,
    IR_CONVERSION_INTEGER_SIGN_EXTEND,
    IR_CONVERSION_INTEGER_ZERO_EXTEND,
    IR_CONVERSION_INTEGER_TRUNCATE,
    IR_CONVERSION_INTEGER_REINTERPRET,
    IR_CONVERSION_FLOAT_EXTEND,
    IR_CONVERSION_FLOAT_TRUNCATE,
    IR_CONVERSION_SIGNED_INTEGER_TO_FLOAT,
    IR_CONVERSION_UNSIGNED_INTEGER_TO_FLOAT,
    IR_CONVERSION_FLOAT_TO_SIGNED_INTEGER,
    IR_CONVERSION_FLOAT_TO_UNSIGNED_INTEGER,
    IR_CONVERSION_POINTER_REINTERPRET,
    IR_CONVERSION_POINTER_TO_INTEGER,
    IR_CONVERSION_INTEGER_TO_POINTER,
    IR_CONVERSION_COUNT,
} IrConversionOperation;

typedef enum IrUnaryOperation
{
    IR_UNARY_INTEGER_NEGATE,
    IR_UNARY_FLOAT_NEGATE,
    IR_UNARY_INTEGER_BITWISE_NOT,
    IR_UNARY_INTEGER_COUNT_LEADING_ZEROS,
    IR_UNARY_INTEGER_COUNT_TRAILING_ZEROS,
    IR_UNARY_INTEGER_POPULATION_COUNT,
    IR_UNARY_BOOLEAN_NOT,
    IR_UNARY_VECTOR_INTEGER_NEGATE,
    IR_UNARY_VECTOR_FLOAT_NEGATE,
    IR_UNARY_VECTOR_INTEGER_BITWISE_NOT,
    IR_UNARY_COUNT,
} IrUnaryOperation;

typedef enum IrBinaryOperation
{
    IR_BINARY_INTEGER_ADD,
    IR_BINARY_INTEGER_SUBTRACT,
    IR_BINARY_INTEGER_MULTIPLY,
    IR_BINARY_SIGNED_DIVIDE,
    IR_BINARY_UNSIGNED_DIVIDE,
    IR_BINARY_FLOAT_ADD,
    IR_BINARY_FLOAT_SUBTRACT,
    IR_BINARY_FLOAT_MULTIPLY,
    IR_BINARY_FLOAT_DIVIDE,
    IR_BINARY_SIGNED_REMAINDER,
    IR_BINARY_UNSIGNED_REMAINDER,
    IR_BINARY_SHIFT_LEFT,
    IR_BINARY_SIGNED_SHIFT_RIGHT,
    IR_BINARY_UNSIGNED_SHIFT_RIGHT,
    IR_BINARY_INTEGER_BITWISE_AND,
    IR_BINARY_INTEGER_BITWISE_OR,
    IR_BINARY_INTEGER_BITWISE_XOR,
    IR_BINARY_BOOLEAN_AND,
    IR_BINARY_BOOLEAN_OR,
    IR_BINARY_INTEGER_EQUAL,
    IR_BINARY_INTEGER_NOT_EQUAL,
    IR_BINARY_FLOAT_EQUAL,
    IR_BINARY_FLOAT_NOT_EQUAL,
    IR_BINARY_POINTER_EQUAL,
    IR_BINARY_POINTER_NOT_EQUAL,
    IR_BINARY_BOOLEAN_EQUAL,
    IR_BINARY_BOOLEAN_NOT_EQUAL,
    IR_BINARY_SIGNED_LESS,
    IR_BINARY_SIGNED_LESS_EQUAL,
    IR_BINARY_SIGNED_GREATER,
    IR_BINARY_SIGNED_GREATER_EQUAL,
    IR_BINARY_UNSIGNED_LESS,
    IR_BINARY_UNSIGNED_LESS_EQUAL,
    IR_BINARY_UNSIGNED_GREATER,
    IR_BINARY_UNSIGNED_GREATER_EQUAL,
    IR_BINARY_FLOAT_LESS,
    IR_BINARY_FLOAT_LESS_EQUAL,
    IR_BINARY_FLOAT_GREATER,
    IR_BINARY_FLOAT_GREATER_EQUAL,
    IR_BINARY_RANGE,
    IR_BINARY_VECTOR_INTEGER_ADD,
    IR_BINARY_VECTOR_INTEGER_SUBTRACT,
    IR_BINARY_VECTOR_INTEGER_MULTIPLY,
    IR_BINARY_VECTOR_SIGNED_DIVIDE,
    IR_BINARY_VECTOR_UNSIGNED_DIVIDE,
    IR_BINARY_VECTOR_FLOAT_ADD,
    IR_BINARY_VECTOR_FLOAT_SUBTRACT,
    IR_BINARY_VECTOR_FLOAT_MULTIPLY,
    IR_BINARY_VECTOR_FLOAT_DIVIDE,
    IR_BINARY_VECTOR_SIGNED_REMAINDER,
    IR_BINARY_VECTOR_UNSIGNED_REMAINDER,
    IR_BINARY_VECTOR_SHIFT_LEFT,
    IR_BINARY_VECTOR_SIGNED_SHIFT_RIGHT,
    IR_BINARY_VECTOR_UNSIGNED_SHIFT_RIGHT,
    IR_BINARY_VECTOR_INTEGER_BITWISE_AND,
    IR_BINARY_VECTOR_INTEGER_BITWISE_OR,
    IR_BINARY_VECTOR_INTEGER_BITWISE_XOR,
    IR_BINARY_VECTOR_INTEGER_EQUAL,
    IR_BINARY_VECTOR_INTEGER_NOT_EQUAL,
    IR_BINARY_VECTOR_SIGNED_LESS,
    IR_BINARY_VECTOR_SIGNED_LESS_EQUAL,
    IR_BINARY_VECTOR_SIGNED_GREATER,
    IR_BINARY_VECTOR_SIGNED_GREATER_EQUAL,
    IR_BINARY_VECTOR_UNSIGNED_LESS,
    IR_BINARY_VECTOR_UNSIGNED_LESS_EQUAL,
    IR_BINARY_VECTOR_UNSIGNED_GREATER,
    IR_BINARY_VECTOR_UNSIGNED_GREATER_EQUAL,
    IR_BINARY_VECTOR_FLOAT_EQUAL,
    IR_BINARY_VECTOR_FLOAT_NOT_EQUAL,
    IR_BINARY_VECTOR_FLOAT_LESS,
    IR_BINARY_VECTOR_FLOAT_LESS_EQUAL,
    IR_BINARY_VECTOR_FLOAT_GREATER,
    IR_BINARY_VECTOR_FLOAT_GREATER_EQUAL,
    IR_BINARY_COUNT,
} IrBinaryOperation;

// The target-fixed 512-bit byte vocabulary.  These are not portable vector
// operations and deliberately do not grow into one: each entry names a single
// AVX-512 instruction that the canonical backend emits directly, and code that
// wants them asks for them through `<buster/lib/simd.h>`, which keeps a scalar
// fallback for every other target.  The operand shapes are fixed per
// operation and checked by `ir_validate`.
typedef enum IrSimdOperation
{
    IR_SIMD_LOAD,               // (pointer) -> vector
    IR_SIMD_LOAD_MASKED,        // (pointer, mask) -> vector, lanes outside the mask zeroed
    IR_SIMD_STORE,              // (pointer, vector)
    IR_SIMD_STORE_MASKED,       // (pointer, mask, vector), lanes outside the mask untouched
    IR_SIMD_SPLAT_BYTE,         // (byte) -> vector
    IR_SIMD_COMPARE_EQUAL_BYTE, // (vector, vector) -> mask
    IR_SIMD_COMPARE_LESS_BYTE,  // (vector, vector) -> mask, unsigned
    IR_SIMD_SIGN_MASK_BYTE,     // (vector) -> mask of the per-byte high bits
    IR_SIMD_TEST_MASK_BYTE,     // (vector, vector) -> mask where the byte-wise AND is non-zero
    IR_SIMD_PERMUTE2_BYTE,      // (mask, low, indices, high) -> vector, zeroed outside the mask
    IR_SIMD_COMPRESS_BYTE,      // (mask, vector) -> vector, selected bytes packed down
    IR_SIMD_COMPRESS_STORE_BYTE,// (pointer, mask, vector), writes only the selected bytes
    IR_SIMD_WIDEN_BYTE_TO_WORD, // (vector, quarter) -> vector of 16 zero-extended u32 lanes
    IR_SIMD_SHIFT_LEFT_WORD,    // (vector, count) -> vector, per u32 lane
    IR_SIMD_TERNARY_WORD,       // (vector, vector, vector, table) -> vector, per-bit truth table
    IR_SIMD_COMPARE_EQUAL_WORD, // (vector, vector) -> mask of the 16 u32 lanes in the low bits
    IR_SIMD_SPLAT_WORD,         // (u32) -> vector holding that value in all 16 u32 lanes
    IR_SIMD_COMPARE_LESS_WORD,  // (vector, vector) -> mask of the 16 u32 lanes, unsigned
    IR_SIMD_COMPRESS_WORD,      // (mask, vector) -> vector, selected u32 lanes packed down
    IR_SIMD_COUNT,
} IrSimdOperation;

// The fixed operand/immediate arity of one SIMD operation. Every consumer —
// the frontend that builds the instruction, the validator, and the backend
// read the arity from here so a new operation
// cannot be half-taught to the pipeline.
typedef struct IrSimdShape IrSimdShape;
struct IrSimdShape
{
    u8 operand_count;
    u8 immediate_count;
    bool has_result;
    u8 reserved[1];
};

typedef struct IrValue IrValue;
typedef struct IrLabelProvenancePath IrLabelProvenancePath;
struct IrLabelProvenancePath
{
    IrBlockId* label_blocks;
    u64 offset;
    u64 size;
    u32 label_block_count;
    bool is_non_label;
    u8 reserved[3];
};

struct IrValue
{
    IrTypeId canonical_type;
    IrInstructionId definition;
    u32 alignment;
    // IrValueCategory; u8 keeps the record at 16 bytes (one million-plus
    // instances per compile).
    u8 category;
    bool is_read_only;
    bool points_to_read_only;
    bool is_volatile;
};

BUSTER_CT_CHECK((u32)IR_VALUE_COUNT <= UINT8_MAX);
BUSTER_CT_CHECK(sizeof(IrValue) == 16);

// Label metadata is populated only for values involved in address-of-label
// provenance tracking, so it lives in a sparse per-function side table
// (IrFunction label_metadata_values/label_metadata, sorted by value id)
// instead of occupying every IrValue. An absent entry means all-zero
// metadata.
typedef struct IrValueLabelMetadata IrValueLabelMetadata;
struct IrValueLabelMetadata
{
    IrBlockId* label_blocks;
    IrLabelProvenancePath* label_paths;
    u32 label_block_count;
    u32 label_path_count;
    bool is_label_value;
    bool has_label_provenance;
    bool has_non_label_provenance;
    u8 reserved;
};

typedef struct IrIncoming IrIncoming;
struct IrIncoming
{
    IrIncoming* next;
    IrBlockId predecessor;
    IrValueId value;
};

typedef struct IrBlockParameter IrBlockParameter;
struct IrBlockParameter
{
    IrBlockParameter* next;
    IrIncoming* first_incoming;
    IrIncoming* last_incoming;
    IrTypeId canonical_type;
    IrLocalId canonical_local;
    IrValueId value;
    u32 incoming_count;
};

typedef struct IrPredecessor IrPredecessor;
struct IrPredecessor
{
    IrPredecessor* next;
    IrBlockId block;
};

// Rare per-instruction payloads — the inline-assembly name arrays and the
// string/float literal bytes — live in a sorted per-function side table
// (ir_instruction_extra_find/ensure) instead of widening every instruction
// row. Every consumer walks instructions linearly, so the canonical source
// range remains in a dense parallel array indexed by instruction id.
typedef struct IrInstructionExtra IrInstructionExtra;
struct IrInstructionExtra
{
    String8* label_names;
    String8* operand_names;
    String8* clobbers;
    String8 literal;
    u32 label_name_count;
    u32 operand_name_count;
    u32 clobber_count;
    u32 reserved;
};

// The instruction array is the largest thing a compile allocates per
// function (over a million rows on the self-host unit), so the row is kept
// to exactly one cache line. Three deliberate narrowings pay for that:
// the operation/order enums are stored as u8 with their COUNT values as
// per-field "not this kind" sentinels (each enum is checked <= UINT8_MAX
// below), target/immediate counts are u16 (producers with unbounded case
// or initializer counts must diagnose overflow instead of truncating), and
// the row's own id is gone — ir_function_add_instruction has always stored
// row `n` at instructions[n], so ir_instruction_self_id recovers it from
// the row's address.
typedef struct IrInstruction IrInstruction;
struct IrInstruction
{
    IrValueId* operands;
    IrBlockId* targets;
    u64* immediates;
    IrTypeId canonical_type;
    IrSymbolId symbol;
    IrLocalId canonical_local;
    IrInstructionId next;
    IrValueId result;
    u32 operand_count;
    u16 target_count;
    u16 immediate_count;
    u8 opcode;
    u8 conversion_operation;
    u8 unary_operation;
    u8 binary_operation;
    u8 memory_order;
    u8 failure_memory_order;
    u8 atomic_operation;
    bool immediate_is_negative;
    bool atomic_signal_fence;
    bool volatile_access;
    // An IrSimdOperation, narrowed like the rest of the enum tail.
    u8 simd_operation;
};

BUSTER_CT_CHECK(IR_SIMD_COUNT <= UINT8_MAX);
BUSTER_CT_CHECK((u32)IR_OPCODE_COUNT <= UINT8_MAX);
BUSTER_CT_CHECK((u32)IR_CONVERSION_COUNT <= UINT8_MAX);
BUSTER_CT_CHECK((u32)IR_UNARY_COUNT <= UINT8_MAX);
BUSTER_CT_CHECK((u32)IR_BINARY_COUNT <= UINT8_MAX);
BUSTER_CT_CHECK((u32)IR_MEMORY_ORDER_COUNT <= UINT8_MAX);
BUSTER_CT_CHECK((u32)IR_ATOMIC_OPERATION_COUNT <= UINT8_MAX);
// One bit per opcode, so a whole function's opcode population fits in a word.
// Bit 63 is the marker that the word means anything at all: a function whose
// rows were written straight into `instructions` (hand-built IR in tests, the
// bitcode fixtures) never passes through ir_module_add_function, and a cleared
// bit there would claim an absence nobody established.
#define IR_OPCODE_BIT(opcode) ((u64)1 << (u32)(opcode))
#define IR_OPCODE_SUMMARY_KNOWN ((u64)1 << 63)
BUSTER_CT_CHECK((u32)IR_OPCODE_COUNT < 63);
// Only these opcodes are recorded, and each one is rare enough that the
// summary answers for nearly every function without a scan: six of the 3,814
// functions in a self-compile hold an atomic or an inline-assembly row. The
// appender's common path is therefore one bit test against this constant and
// no store at all. An opcode absent from this list is never recorded and must
// never be queried: add it here in the same change that adds the query.
#define IR_OPCODE_SUMMARY_TRACKED                                                                                                      \
    (IR_OPCODE_BIT(IR_OPCODE_STACK_ALLOCATE) | IR_OPCODE_BIT(IR_OPCODE_STACK_RESTORE) | IR_OPCODE_BIT(IR_OPCODE_ATOMIC_LOAD) |          \
     IR_OPCODE_BIT(IR_OPCODE_ATOMIC_STORE) | IR_OPCODE_BIT(IR_OPCODE_ATOMIC_READ_MODIFY_WRITE) |                                       \
     IR_OPCODE_BIT(IR_OPCODE_ATOMIC_COMPARE_EXCHANGE) | IR_OPCODE_BIT(IR_OPCODE_INLINE_ASSEMBLY))

BUSTER_CT_CHECK(sizeof(void*) != 8 || sizeof(IrInstruction) == 64);

typedef struct IrBlock IrBlock;
struct IrBlock
{
    IrBlockParameter* first_parameter;
    IrBlockParameter* last_parameter;
    IrPredecessor* first_predecessor;
    IrPredecessor* last_predecessor;
    IrValueId* local_values;
    IrInstructionId first_instruction;
    IrInstructionId last_instruction;
    IrBlockId id;
    u32 parameter_count;
    u32 predecessor_count;
    bool terminated;
    bool sealed;
    u8 reserved[2];
};

typedef enum IrFunctionState
{
    IR_FUNCTION_NOT_LOWERED,
    IR_FUNCTION_LOWERED,
    IR_FUNCTION_REJECTED,
    IR_FUNCTION_DECLARATION,
    IR_FUNCTION_STATE_COUNT,
} IrFunctionState;

typedef enum IrGlobalInitializerKind
{
    IR_GLOBAL_INITIALIZER_NONE,
    IR_GLOBAL_INITIALIZER_ZERO,
    IR_GLOBAL_INITIALIZER_INTEGER,
    IR_GLOBAL_INITIALIZER_FLOAT,
    IR_GLOBAL_INITIALIZER_BYTES,
    IR_GLOBAL_INITIALIZER_SYMBOL_ADDRESS,
    IR_GLOBAL_INITIALIZER_COUNT,
} IrGlobalInitializerKind;

typedef struct IrGlobalRelocation IrGlobalRelocation;
struct IrGlobalRelocation
{
    IrSymbolId symbol;
    IrBlockId label_block;
    s64 addend;
    u64 offset;
    bool is_label_address;
    u8 reserved[3];
};

typedef struct IrGlobal IrGlobal;
struct IrGlobal
{
    ByteSlice bytes;
    IrGlobalRelocation* relocations;
    IrSymbolId symbol;
    IrSymbolId initializer_symbol;
    IrTypeId type;
    IrSourceRange source;
    s64 initializer_addend;
    u64 initializer_bits;
    u32 relocation_count;
    u32 alignment;
    IrGlobalInitializerKind initializer_kind;
    bool initializer_is_negative;
    bool is_read_only;
    bool is_thread_local;
    u8 reserved;
};

typedef struct IrFunction IrFunction;
typedef struct IrDebugLocal IrDebugLocal;
struct IrDebugLocal
{
    String8 name;
    IrSourceRange source;
    IrTypeId type;
    IrLocalId id;
    u32 scope_depth;
    bool is_parameter;
    u8 reserved[3];
};

struct IrFunction
{
    String8 name;
    IrSymbolId symbol;
    IrSourceRange source;
    IrTypeId canonical_type;
    IrFunctionId id;
    IrBlockId entry;
    IrBlock* blocks;
    IrInstruction* instructions;
    IrValue* values;
    IrValueId* local_places;
    bool* local_uses_memory;
    IrDebugLocal* debug_locals;
    IrValueId* label_metadata_values;
    IrValueLabelMetadata* label_metadata;
    IrInstructionId* extra_instructions;
    IrInstructionExtra* extras;
    // Dense parallel array containing the canonical source range for every
    // instruction. Consumers that need it index by instruction id.
    IrSourceRange* instruction_canonical_sources;
    u32 block_count;
    u32 block_capacity;
    u32 instruction_count;
    u32 instruction_capacity;
    u32 value_count;
    u32 value_capacity;
    u32 local_count;
    u32 debug_local_count;
    u32 label_metadata_count;
    u32 label_metadata_capacity;
    u32 extra_count;
    u32 extra_capacity;
    IrFunctionState state;
    // Which IR_OPCODE_SUMMARY_TRACKED opcodes the builder appended, plus
    // IR_OPCODE_SUMMARY_KNOWN. Consumers ask ir_function_may_contain_opcodes
    // instead of rescanning every row for a handful of rare ones. The summary
    // only ever over-approximates: popping a lowered row leaves its bit set.
    u64 opcode_summary;
};

typedef struct IrModuleAssembly IrModuleAssembly;
struct IrModuleAssembly
{
    String8 source;
    IrSourceRange source_range;
};

typedef struct IrModule IrModule;
struct IrModule
{
    String8 name;
    IrFunction* functions;
    IrGlobal* globals;
    IrModuleAssembly* assemblies;
    u32 function_count;
    u32 function_capacity;
    u32 global_count;
    u32 global_capacity;
    u32 assembly_count;
    u32 assembly_capacity;
    u32 lowered_function_count;
    u32 rejected_function_count;
    // How many relocations across `globals` take a block label's address — the
    // `&&label` of a computed goto reaching a static initializer.
    // `ir_module_add_global` counts them as each global arrives, so a consumer
    // asking "can any global give this symbol label provenance?" answers no
    // without walking the global table; the count is zero for every
    // translation unit without such an initializer, which is nearly all.
    u32 label_address_relocation_count;
};

typedef struct IrProgram IrProgram;
struct IrProgram
{
    Arena* arena;
    TargetDataLayout data_layout;
    IrModule* modules;
    IrTypeTable types;
    IrSymbolTable symbols;
    IrSourceTable sources;
    // How the frontend's byte space maps back to lines, when the sources are
    // not indexed by their own bytes. Empty for frontends that fill
    // IrSource.text instead.
    IrSourceMap source_map;
    // Scratch for ir_source_position; a program is resolved by one consumer
    // at a time, walking in roughly ascending offset order.
    IrSourceMapCursor source_cursor;
    u32 module_count;
    u32 lowered_function_count;
    u32 rejected_function_count;
};

typedef enum IrValidationError
{
    IR_VALIDATION_NONE,
    IR_VALIDATION_INVALID_ID,
    IR_VALIDATION_UNTERMINATED_BLOCK,
    IR_VALIDATION_INSTRUCTION_AFTER_TERMINATOR,
    IR_VALIDATION_RESULT_TYPE,
    IR_VALIDATION_OPERAND_TYPE,
    IR_VALIDATION_BRANCH_TARGET,
    IR_VALIDATION_RETURN_TYPE,
    IR_VALIDATION_CALL_TARGET,
    IR_VALIDATION_CALL_SIGNATURE,
    IR_VALIDATION_BLOCK_PARAMETER,
    IR_VALIDATION_OPERATION,
    IR_VALIDATION_ALIGNMENT,
    IR_VALIDATION_INSTRUCTION_OWNERSHIP,
    IR_VALIDATION_COUNT,
} IrValidationError;

typedef struct IrValidationResult IrValidationResult;
struct IrValidationResult
{
    IrValidationError error;
    IrFunctionId function;
    IrBlockId block;
    IrInstructionId instruction;
};

// The result of proving that every instruction of a function belongs to
// exactly one block chain. Walking `next` alone cannot say that: a chain can
// cycle, two blocks can share a tail, `last_instruction` can name an
// instruction the chain never reaches, and an instruction can sit in no chain
// at all while later passes still read it out of the dense array.
typedef struct IrInstructionOwnership IrInstructionOwnership;
struct IrInstructionOwnership
{
    IrValidationError error;
    IrBlockId block;
    IrInstructionId instruction;
};

BUSTER_F_DECL IrProgram ir_program_initialize(Arena* arena, u32 module_count, u32 type_capacity, u32 symbol_capacity, u32 source_capacity);
BUSTER_F_DECL IrTypeId ir_program_add_type(IrProgram* program, IrType type);
BUSTER_F_DECL void ir_prepare_program_abi(IrProgram* program, IrAbiConvention convention);
BUSTER_F_DECL IrAbiConvention ir_abi_convention_for_target(Target target);
BUSTER_F_DECL IrAbiValue ir_type_abi_value(IrProgram* program, IrTypeId type, IrAbiConvention convention, IrAbiUse use);
// AAPCS64 starts a 16-byte, two-integer-part value at an even X-register
// number.  Keep this fact beside the ABI classifier so canonical and machine
// placement cannot silently diverge; results use the fixed X0:X1 pair and do
// not need the argument-file alignment rule.
BUSTER_F_DECL bool ir_abi_value_is_aarch64_even_integer_pair(IrProgram* program, IrTypeId type, IrAbiConvention convention, IrAbiUse use);
BUSTER_F_DECL IrSymbolId ir_program_add_symbol(IrProgram* program, IrSymbol symbol);
BUSTER_F_DECL IrSourceId ir_program_add_source(IrProgram* program, IrSource source);
BUSTER_F_DECL IrFunction* ir_module_add_function(Arena* arena, IrModule* module, IrFunction function);
BUSTER_F_DECL IrGlobal* ir_module_add_global(Arena* arena, IrModule* module, IrGlobal global);
BUSTER_F_DECL IrBlock* ir_function_add_block(Arena* arena, IrFunction* function, IrBlock block);
BUSTER_F_DECL IrValueId ir_function_add_value(Arena* arena, IrFunction* function, IrValue value);
BUSTER_F_DECL IrInstructionId ir_function_add_instruction(Arena* arena, IrFunction* function, IrInstruction instruction, IrSourceRange canonical_source);
// The id ir_function_add_instruction assigned to this row: its index in the
// function's instruction array. The row stopped storing it when the record
// was packed to one cache line.
BUSTER_F_DECL IrInstructionId ir_instruction_self_id(IrFunction* function, IrInstruction* instruction);
// True when `function` may hold one of the IR_OPCODE_BIT opcodes in `mask`,
// which must name only IR_OPCODE_SUMMARY_TRACKED opcodes. An unknown summary
// answers yes, so a caller's fallback scan is what a hand-built function gets
// rather than a wrong absence.
BUSTER_F_DECL bool ir_function_may_contain_opcodes(IrFunction* function, u64 mask);
BUSTER_F_DECL IrValueLabelMetadata* ir_value_label_metadata_find(IrFunction* function, IrValueId value);
BUSTER_F_DECL IrValueLabelMetadata ir_value_label_metadata(IrFunction* function, IrValueId value);
BUSTER_F_DECL IrValueLabelMetadata* ir_value_label_metadata_ensure(Arena* arena, IrFunction* function, IrValueId value);
BUSTER_F_DECL IrInstructionExtra* ir_instruction_extra_find(IrFunction* function, IrInstructionId instruction);
BUSTER_F_DECL IrInstructionExtra ir_instruction_extra(IrFunction* function, IrInstructionId instruction);
BUSTER_F_DECL IrInstructionExtra* ir_instruction_extra_ensure(Arena* arena, IrFunction* function, IrInstructionId instruction);
BUSTER_F_DECL IrSourceRange ir_instruction_canonical_source(IrFunction* function, IrInstructionId instruction);
// Where a range points, line and column included. This is the only place
// line and column are computed: everything upstream of it carries the source
// and the offset alone. A range that was never filled in resolves to line 0,
// which every consumer already treats as "no position".
BUSTER_F_DECL IrSourcePosition ir_source_position(IrProgram* program, IrSourceRange range);
BUSTER_F_DECL bool ir_label_provenance_valid(IrValueLabelMetadata* value);
BUSTER_F_DECL bool ir_label_storage_provenance_valid(IrValueLabelMetadata* value);
BUSTER_F_DECL bool ir_block_id_array_unique(IrBlockId* blocks, u32 count);
BUSTER_F_DECL bool ir_label_provenance_contains(IrValueLabelMetadata* value, IrBlockId block);
BUSTER_F_DECL void ir_label_provenance_copy(Arena* arena, IrFunction* function, IrValueId destination, IrValueId source);
BUSTER_F_DECL void ir_label_provenance_union(Arena* arena, IrFunction* function, IrValueId destination, IrValueId source);
BUSTER_F_DECL void ir_label_storage_provenance_copy(Arena* arena, IrFunction* function, IrValueId destination, IrValueId source);
BUSTER_F_DECL void ir_label_storage_provenance_union(Arena* arena, IrFunction* function, IrValueId destination, IrValueId source);
BUSTER_F_DECL void ir_label_provenance_load(Arena* arena, IrFunction* function, IrValueId destination, IrValueId source);
BUSTER_F_DECL bool ir_label_metadata_shape_valid(IrProgram* program, IrFunction* function, IrValueId value);
BUSTER_F_DECL bool ir_label_metadata_transfer_valid(IrProgram* program, IrFunction* function, IrValueId value);
BUSTER_F_DECL bool ir_label_block_parameter_provenance_valid(IrFunction* function, IrBlockParameter* parameter);
BUSTER_F_DECL IrSimdShape ir_simd_operation_shape(IrSimdOperation operation);
BUSTER_F_DECL String8 ir_simd_operation_name(IrSimdOperation operation);
BUSTER_F_DECL u32 ir_inline_assembly_label_operand_base(IrInstruction* instruction);
BUSTER_F_DECL bool ir_inline_assembly_jump_target(IrFunction* function, IrInstruction* instruction, String8 literal, String8 prefix, u32* target_index_out);
// Writes the owning block of every instruction into `owners`, which the
// caller sizes to function->instruction_count, and returns the first
// ownership violation. One O(instructions + blocks) pass: the array doubles
// as the visited set that bounds every chain walk, so consumers past
// validation - register allocation and the emitters - reuse it instead of
// re-deriving block membership or guarding their walks with a counter.
BUSTER_F_DECL IrInstructionOwnership ir_function_instruction_owners(IrFunction* function, IrBlockId* owners);
BUSTER_F_DECL IrValidationResult ir_validate_canonical_module(IrProgram* program, IrModule* module);
