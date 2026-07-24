#pragma once

#include <buster/compiler/frontend/buster/analysis.h>

typedef u32 IrIdUnderlying;

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

#define IR_ID_UNDERLYING_INVALID UINT32_MAX
#define IR_FUNCTION_ID_INVALID ((IrFunctionId){ .value = IR_ID_UNDERLYING_INVALID })
#define IR_BLOCK_ID_INVALID ((IrBlockId){ .value = IR_ID_UNDERLYING_INVALID })
#define IR_INSTRUCTION_ID_INVALID ((IrInstructionId){ .value = IR_ID_UNDERLYING_INVALID })
#define IR_VALUE_ID_INVALID ((IrValueId){ .value = IR_ID_UNDERLYING_INVALID })

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
    IR_OPCODE_LOAD,
    IR_OPCODE_STORE,
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
    IR_OPCODE_BRANCH,
    IR_OPCODE_BRANCH_IF,
    IR_OPCODE_SWITCH,
    IR_OPCODE_RETURN,
    IR_OPCODE_UNREACHABLE,
    IR_OPCODE_COUNT,
} IrOpcode;

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

typedef struct IrValue IrValue;
struct IrValue
{
    AnalysisTypeId type;
    IrInstructionId definition;
    IrValueCategory category;
    u32 reserved;
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
    AnalysisTypeId type;
    AnalysisLocalId local;
    IrValueId value;
    u32 incoming_count;
};

typedef struct IrPredecessor IrPredecessor;
struct IrPredecessor
{
    IrPredecessor* next;
    IrBlockId block;
};

typedef struct IrInstruction IrInstruction;
struct IrInstruction
{
    IrValueId* operands;
    IrBlockId* targets;
    u64* immediates;
    String8 literal;
    ParserSourceRange source;
    AnalysisTypeId type;
    AnalysisEntityId entity;
    AnalysisInstantiationId instantiation;
    AnalysisLocalId local;
    IrInstructionId id;
    IrInstructionId next;
    IrValueId result;
    IrOpcode opcode;
    IrConversionOperation conversion_operation;
    IrUnaryOperation unary_operation;
    IrBinaryOperation binary_operation;
    u32 operand_count;
    u32 target_count;
    u32 immediate_count;
    bool immediate_is_negative;
    u8 reserved[3];
};

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

typedef struct IrFunction IrFunction;
struct IrFunction
{
    String8 name;
    AnalysisEntityId entity;
    AnalysisInstantiationId instantiation;
    AnalysisTypeId type;
    IrFunctionId id;
    IrBlockId entry;
    IrBlock* blocks;
    IrInstruction* instructions;
    IrValue* values;
    IrValueId* local_places;
    bool* local_uses_memory;
    u32 block_count;
    u32 block_capacity;
    u32 instruction_count;
    u32 instruction_capacity;
    u32 value_count;
    u32 value_capacity;
    u32 local_count;
    IrFunctionState state;
};

typedef struct IrModule IrModule;
struct IrModule
{
    String8 name;
    IrFunction* functions;
    u32 function_count;
    u32 lowered_function_count;
    u32 rejected_function_count;
};

typedef struct IrProgram IrProgram;
struct IrProgram
{
    IrModule* modules;
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

BUSTER_F_DECL IrModule ir_generate_module(Arena* result_arena, AnalysisResult* analysis);
BUSTER_F_DECL IrModule ir_analyze_and_generate_module(Arena* result_arena, AnalysisResult* analysis);
BUSTER_F_DECL IrProgram ir_generate_program(
    Arena* result_arena,
    AnalysisProgram* analysis);
BUSTER_F_DECL IrValidationResult ir_validate_module(AnalysisResult* analysis, IrModule* module);
BUSTER_F_DECL String8 ir_print_module(Arena* arena, AnalysisResult* analysis, IrModule* module);

#if BUSTER_INCLUDE_TESTS
#include <buster/test.h>
BUSTER_F_DECL UnitTestResult ir_tests(UnitTestArguments* arguments);
#endif
