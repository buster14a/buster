#pragma once

#include <buster/lib/compiler/ir/ir.h>

typedef enum IrExecutionTrap
{
    IR_EXECUTION_TRAP_NONE,
    IR_EXECUTION_TRAP_INVALID_PROGRAM,
    IR_EXECUTION_TRAP_FUNCTION_NOT_FOUND,
    IR_EXECUTION_TRAP_FUNCTION_NOT_LOWERED,
    IR_EXECUTION_TRAP_ARGUMENT_COUNT,
    IR_EXECUTION_TRAP_UNINITIALIZED_VALUE,
    IR_EXECUTION_TRAP_DIVISION_BY_ZERO,
    IR_EXECUTION_TRAP_INVALID_SHIFT,
    IR_EXECUTION_TRAP_OUT_OF_BOUNDS,
    IR_EXECUTION_TRAP_INVALID_MEMORY,
    IR_EXECUTION_TRAP_DEBUG,
    IR_EXECUTION_TRAP_UNREACHABLE,
    IR_EXECUTION_TRAP_UNSUPPORTED_INSTRUCTION,
    IR_EXECUTION_TRAP_STEP_LIMIT,
    IR_EXECUTION_TRAP_CALL_DEPTH_LIMIT,
    IR_EXECUTION_TRAP_COUNT,
} IrExecutionTrap;

typedef struct IrExecutionArgument IrExecutionArgument;
struct IrExecutionArgument
{
    u64 bits;
};

typedef struct IrExecutionOptions IrExecutionOptions;
struct IrExecutionOptions
{
    u64 max_steps;
    u32 max_call_depth;
    u32 reserved;
};

typedef struct IrExecutionResult IrExecutionResult;
typedef enum IrExecutionValueKind
{
    IR_EXECUTION_VALUE_NONE,
    IR_EXECUTION_VALUE_SCALAR,
    IR_EXECUTION_VALUE_AGGREGATE,
    IR_EXECUTION_VALUE_VECTOR,
    IR_EXECUTION_VALUE_ADDRESS,
    IR_EXECUTION_VALUE_FUNCTION,
    IR_EXECUTION_VALUE_COUNT,
} IrExecutionValueKind;

typedef struct IrExecutionValue IrExecutionValue;
struct IrExecutionValue
{
    IrExecutionValueKind kind;
    ByteSlice bytes;
    ByteSlice initialized;
    u64 bits;
    u64 address_offset;
    u32 address_object;
    bool has_value;
    u8 reserved[3];
};

struct IrExecutionResult
{
    AnalysisEntityId function;
    AnalysisInstantiationId instantiation;
    AnalysisModuleId type_module;
    AnalysisTypeId type;
    IrInstructionId instruction;
    IrExecutionValue value;
    u64 bits;
    u64 step_count;
    IrExecutionTrap trap;
    bool has_value;
    u8 reserved[3];
};

BUSTER_F_DECL IrExecutionResult ir_execute(Arena* execution_arena, AnalysisProgram* analysis, IrProgram* program, AnalysisEntityId entry,
                                           AnalysisInstantiationId instantiation, IrExecutionArgument* arguments, u32 argument_count,
                                           IrExecutionOptions options);
