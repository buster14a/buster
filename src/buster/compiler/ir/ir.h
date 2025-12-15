#pragma once
#include <buster/lib.h>
#include <buster/target.h>

ENUM(IrTypeId,
    IR_TYPE_VOID,
    IR_TYPE_NORETURN,
    IR_TYPE_I1,
    IR_TYPE_I8,
    IR_TYPE_I16,
    IR_TYPE_I32,
    IR_TYPE_I64,
    IR_TYPE_F32,
    IR_TYPE_F64,
);

STRUCT(IrType)
{
    IrTypeId id;
};

ENUM(IrValueId,
    IR_VALUE_ID_CONSTANT_INTEGER,
);

STRUCT(IrValue)
{
    u64 constant_integer;
    IrType* type;
    IrValueId id;
};

ENUM(IrCallingConvention,
    IR_CALLING_CONVENTION_C,
    IR_CALLING_CONVENTION_SYSTEM_V,
    IR_CALLING_CONVENTION_WIN64,
);

ENUM(IrInstructionId,
    IR_INSTRUCTION_RETURN,
);

STRUCT(IrInstruction)
{
    IrValue* value;
    IrInstructionId id;
    IrInstruction* previous;
    IrInstruction* next;
};

STRUCT(IrBasicBlock)
{
    IrInstruction* first;
    IrInstruction* last;
};

STRUCT(IrFunctionTypeBase)
{
    IrType* return_type;
    IrType** argument_types;
    u64 argument_count;
    IrCallingConvention calling_convention;
    Target* target;
};

STRUCT(IrFunctionType)
{
    IrType type;
    IrType* return_type;
    IrType** argument_types;
    u64 argument_count;
    IrCallingConvention calling_convention;
    Target* target;
};

ENUM(IrGlobalSymbolId,
    IR_GLOBAL_SYMBOL_FUNCTION,
    IR_GLOBAL_SYMBOL_VARIABLE,
);

ENUM(IrLinkage, 
    IR_LINKAGE_INTERNAL,
    IR_LINKAGE_EXTERNAL,
);

STRUCT(IrGlobalSymbol)
{
    String8 name;
    IrType* type;
    IrGlobalSymbolId id;
    IrLinkage linkage;
};

STRUCT(IrGlobalVariable)
{
    IrGlobalSymbol symbol;
};

STRUCT(IrFunction)
{
    IrGlobalSymbol symbol;
    IrType* function_type;
    IrBasicBlock* entry_block;
    IrBasicBlock* current_basic_block;
    u64 code_position;
};

STRUCT(IrModule)
{
    Arena* arena;
    Arena* function_arena;
    Arena* global_variable_arena;
    Target* default_target;
    String8 name;
};

STRUCT(IrFunctions)
{
    IrFunction* pointer;
    u64 length;
};

BUSTER_DECL IrFunctions ir_module_get_functions(IrModule* module);

#if BUSTER_INCLUDE_TESTS
BUSTER_IMPL IrModule* ir_create_mock_module(Arena* arena);
BUSTER_IMPL bool ir_tests(TestArguments* arguments);
#endif
