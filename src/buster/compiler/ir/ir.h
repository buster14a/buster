#pragma once
#include <buster/base.h>
#include <buster/target.h>
#include <buster/compiler/intern_table.h>

ENUM(IrValueId,
    ConstantInteger);

STRUCT(IrTypeRef)
{
    u32 v;

    BUSTER_INLINE bool is_valid()
    {
        return v != 0;
    }
};

STRUCT(IrValue)
{
    u64 constant_integer;
    IrTypeRef type;
    IrValueId id;
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

ENUM(IrFunctionAttribute,
    CallingConvention);

STRUCT(IrFunctionAttributes)
{
    IrCallingConvention calling_convention;
};

ENUM_T(IrLinkage, u8,
    Internal,
    External);

ENUM_T(IrSymbolAttribute, u8,
      Export);

STRUCT(IrSymbolAttributes)
{
    IrLinkage linkage;
    bool exported;
};

STRUCT(IrFunctionType)
{
    // IrType* return_type;
    // IrType** argument_types;
    u64 argument_count;
    Target* target;
    // IrType type;
    IrFunctionAttributes attributes;
    u8 reserved[7];
};

ENUM_T(IrGlobalSymbolId, u8,
    Function,
    Variable);

STRUCT(IrGlobalSymbol)
{
    String8 name;
    IrTypeRef type;
    IrGlobalSymbolId id;
    IrLinkage linkage;
    u8 reserved[2];
};

STRUCT(IrGlobalVariable)
{
    IrGlobalSymbol symbol;
};

STRUCT(IrFunction)
{
    IrGlobalSymbol symbol;
    IrBasicBlock* entry_block;
    IrBasicBlock* current_basic_block;
    u64 code_position;
    IrTypeRef function_type;
    u8 reserved[4];
};

BUSTER_GLOBAL_LOCAL constexpr u64 builtin_per_sign_integer_ir_type_count = 64;
BUSTER_GLOBAL_LOCAL constexpr u64 builtin_ir_float_type_count = 2;
BUSTER_GLOBAL_LOCAL constexpr u64 ir_void_type_count = 1;
BUSTER_GLOBAL_LOCAL constexpr u64 ir_builtin_type_count = builtin_per_sign_integer_ir_type_count + builtin_ir_float_type_count + ir_void_type_count;

ENUM(BuiltinIrTypeId,
        IntegerFirst = 0,
        FloatFirst = IntegerFirst + builtin_per_sign_integer_ir_type_count,
        VoidTypeFirst = FloatFirst + builtin_ir_float_type_count);

static_assert((u64)BuiltinIrTypeId::Count == ir_builtin_type_count);

STRUCT(BuiltinIrTypes)
{
    IrTypeRef v[ir_builtin_type_count];
};

STRUCT(DebugTypeRef)
{
    u32 v;
};

BUSTER_GLOBAL_LOCAL constexpr u64 builtin_per_sign_integer_debug_type_count = 64;
BUSTER_GLOBAL_LOCAL constexpr u64 builtin_debug_float_type_count = 2;
BUSTER_GLOBAL_LOCAL constexpr u64 debug_void_type_count = 1;
BUSTER_GLOBAL_LOCAL constexpr u64 debug_builtin_type_count = builtin_per_sign_integer_debug_type_count * 2+ builtin_debug_float_type_count + debug_void_type_count;

ENUM(BuiltinDebugTypeId,
        UnsignedFirst = 0,
        SignedFirst = builtin_per_sign_integer_ir_type_count,
        FloatFirst = SignedFirst + builtin_per_sign_integer_ir_type_count,
        VoidTypeFirst = FloatFirst + builtin_ir_float_type_count);

static_assert((u64)BuiltinIrTypeId::Count == ir_builtin_type_count);

STRUCT(BuiltinDebugTypes)
{
    DebugTypeRef v[debug_builtin_type_count];
};

STRUCT(IrModule)
{
    Arena* arena;
    Arena* function_arena;
    Arena* global_variable_arena;
    Arena* ir_type_arena;
    Arena* debug_type_arena;
    Target* default_target;
    InternTable* intern_table;
    String8 name;
    struct
    {
        BuiltinIrTypes ir_types;
        BuiltinDebugTypes debug_types;
    } builtin;
};

BUSTER_F_DECL Slice<IrFunction> ir_module_get_functions(IrModule* module);
BUSTER_F_DECL IrModule* ir_create_mock_module(Arena* arena);
BUSTER_F_DECL IrModule* ir_module_create(Arena* arena, Target* target, String8 name, InternTable* table);

#if BUSTER_INCLUDE_TESTS
#include <buster/test.h>
BUSTER_F_DECL bool ir_tests(UnitTestArguments* arguments);
#endif
