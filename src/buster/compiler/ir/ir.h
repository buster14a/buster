#pragma once
#include <buster/base.h>
#include <buster/target.h>

ENUM_T(IrTypeId, u8,
    Void,
    Noreturn,
    I1,
    I8,
    I16,
    I32,
    I64,
    F32,
    F64);

STRUCT(IrType)
{
    IrTypeId id;
};

ENUM(IrValueId,
    ConstantInteger);

STRUCT(IrValue)
{
    u64 constant_integer;
    IrType* type;
    IrValueId id;
    u8 reserved[4];
};

ENUM_T(IrCallingConvention, u8,
    C,
    SystemV,
    Win64);

ENUM(IrInstructionId,
    Return);

STRUCT(IrInstruction)
{
    IrValue* value;
    IrInstruction* previous;
    IrInstruction* next;
    IrInstructionId id;
    u8 reserved[4];
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
    IrType* return_type;
    IrType** argument_types;
    u64 argument_count;
    Target* target;
    IrType type;
    IrCallingConvention calling_convention;
    u8 reserved[6];
};

ENUM(IrGlobalSymbolId,
    Function,
    Variable);

ENUM(IrLinkage, 
    Internal,
    External);

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

BUSTER_F_DECL Slice<IrFunction> ir_module_get_functions(IrModule* module);
BUSTER_F_DECL IrModule* ir_create_mock_module(Arena* arena);

#if BUSTER_INCLUDE_TESTS
#include <buster/test.h>
BUSTER_DECL bool ir_tests(UnitTestArguments* arguments);
#endif
