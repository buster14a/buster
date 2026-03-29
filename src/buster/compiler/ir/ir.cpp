#pragma once
#include <buster/compiler/ir/ir.h>
#include <buster/arena.h>
#include <buster/string.h>

ENUM_T(IrTypeId, u8,
    Void,
    NoReturn,
    Integer,
    Float,
    Vector);

STRUCT(IrType)
{
    InternIndex name;
    IrTypeId id;
    u8 reserved[3];
};

ENUM_T(DebugTypeId, u8,
        Void,
        Unsigned,
        Signed,
        Bool,
        Float);

STRUCT(DebugType)
{
    InternIndex name;
    DebugTypeId id;
    u8 reserved[3];
};

BUSTER_F_IMPL IrModule* ir_module_create(Arena* arena, Target* target, String8 name, InternTable* table)
{
    let module = arena_allocate(arena, IrModule, 1);
    *module = (IrModule){
        .arena = arena,
        .default_target = target ? target : &target_native,
        .function_arena = arena_create({}),
        .global_variable_arena = arena_create({}),
        .ir_type_arena = arena_create({}),
        .debug_type_arena = arena_create({}),
        .name = name,
    };

    char8 buffer[3];
    String8 slice = BUSTER_ARRAY_TO_SLICE(buffer);
    bool is_signed_values[] = { false, true };

    let builtin_debug_types = arena_allocate(module->debug_type_arena, DebugType, builtin_per_sign_integer_debug_type_count * 2);
    for (bool is_signed : is_signed_values)
    {
        char8 signed_char = is_signed ? 's' : 'u';
        DebugTypeId id = is_signed ? DebugTypeId::Signed : DebugTypeId::Unsigned;

        for (u8 i = 0; i < builtin_per_sign_integer_debug_type_count; i += 1)
        {
            let bit_count = i + 1;
            buffer[0] = signed_char;
            buffer[1] = '0' + BUSTER_SELECT(bit_count > 9, (char8)(bit_count / 10), (char8)bit_count);
            buffer[2] = '0' + (char8)(bit_count % 10);
            let string = slice;
            string.length = 2 + (bit_count > 9);

            let type_name = table_intern(table, string);
            let index = (u64)is_signed * builtin_per_sign_integer_debug_type_count + i;
            builtin_debug_types[index] = {
                .name = type_name,
                .id = id,
            };
            module->builtin.debug_types.v[index] = { .v = (u32)index + 1 };
        }
    }

    return module;
}

BUSTER_GLOBAL_LOCAL IrTypeRef ir_function_type_get(IrModule* module, IrFunctionType create)
{
    BUSTER_UNUSED(module);
    BUSTER_UNUSED(create);
    return {};
    // if (!create.target)
    // {
    //     create.target = module->default_target;
    // }
    //
    // let function_type = arena_allocate(module->ir_type_arena, IrType, 1);
    // // *function_type = create;
    // return function_type;
}

BUSTER_GLOBAL_LOCAL void ir_block_append_instruction(IrBasicBlock* basic_block, IrInstruction* instruction)
{
    if (!basic_block->first)
    {
        basic_block->first = instruction;
        basic_block->last = instruction;
    }
    else
    {
        let last = basic_block->last;
        last->next = instruction;
        instruction->previous = last;
    }
}

BUSTER_GLOBAL_LOCAL void ir_function_append_instruction(IrFunction* function, IrInstruction* instruction)
{
    ir_block_append_instruction(function->current_basic_block, instruction);
}

BUSTER_GLOBAL_LOCAL IrInstruction* ir_create_return(IrModule* module, IrFunction* function, IrValue* value)
{
    let result = arena_allocate(module->arena, IrInstruction, 1);
    *result = (IrInstruction) {
        .value = value,
        .id = IrInstructionId::Return,
    };
    ir_function_append_instruction(function, result);
    return result;
}

BUSTER_GLOBAL_LOCAL IrValue* ir_constant_integer(IrModule* module, IrTypeRef type, u64 value)
{
    let result = arena_allocate(module->arena, IrValue, 1);
    *result = (IrValue) {
        .constant_integer = value,
        .type = type,
        .id = IrValueId::ConstantInteger,
    };
    return result;
}

BUSTER_GLOBAL_LOCAL IrFunction* ir_function_create(IrModule* module, IrGlobalSymbol symbol)
{
    IrFunction* function = 0;
    symbol.id = IrGlobalSymbolId::Function;

    function = arena_allocate(module->function_arena, IrFunction, 1);
    let entry_basic_block = arena_allocate(module->arena, IrBasicBlock, 1);
    *function = (IrFunction) {
        .symbol = symbol,
            .entry_block = entry_basic_block,
            .current_basic_block = entry_basic_block,
    };

    return function;
}

BUSTER_GLOBAL_LOCAL IrGlobalVariable* ir_global_variable_create(IrModule* module, IrGlobalSymbol symbol)
{
    IrGlobalVariable* result = 0;
    symbol.id = IrGlobalSymbolId::Variable;

    if (symbol.type.is_valid())
    {
        result = arena_allocate(module->global_variable_arena, IrGlobalVariable, 1);
        *result = (IrGlobalVariable) {
            .symbol = symbol,
        };
    }

    return result;
}

BUSTER_F_IMPL Slice<IrFunction> ir_module_get_functions(IrModule* module)
{
    let arena_pointer = (u8*)module->function_arena;
    let start = (u8*)(module->function_arena + 1);
    return {
        .pointer = (IrFunction*)start,
        .length = (u64)((arena_pointer + module->function_arena->position) - start) / sizeof(IrFunction),
    };
}

BUSTER_GLOBAL_LOCAL IrTypeRef ir_get_integer_type(IrModule* module, u32 bit_count)
{
    IrTypeRef result = {};
    if (bit_count > 0 && bit_count <= builtin_per_sign_integer_ir_type_count)
    {
        result = module->builtin.ir_types.v[bit_count - 1];
    }
    return result;
}

BUSTER_F_IMPL IrModule* ir_create_mock_module(Arena* arena)
{
    let intern_table = intern_table_create();
    let module = ir_module_create(arena, 0, S8("basic"), &intern_table);
    let i32_type = ir_get_integer_type(module, 32);
    let function = ir_function_create(module, (IrGlobalSymbol) {
        .linkage = IrLinkage::External,
        .type = ir_function_type_get(module, {})
        });
    ir_create_return(module, function, ir_constant_integer(module, i32_type, 0));

    return module;
}

#if BUSTER_INCLUDE_TESTS
BUSTER_F_IMPL bool ir_tests(UnitTestArguments* arguments)
{
    BUSTER_UNUSED(arguments);
    return true;
}
#endif
