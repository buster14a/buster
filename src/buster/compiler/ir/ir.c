#pragma once
#include <buster/compiler/ir/ir.h>

BUSTER_LOCAL IrModule* ir_module_create(Arena* arena, Target* target, String8 name)
{
    let module = arena_allocate(arena, IrModule, 1);
    *module = (IrModule){
        .arena = arena,
        .default_target = target ? target : &target_native,
        .function_arena = arena_create((ArenaInitialization){}),
        .global_variable_arena = arena_create((ArenaInitialization){}),
        .name = name,
    };
    return module;
}

BUSTER_LOCAL IrFunctionType* ir_function_type_get(IrModule* module, IrFunctionType create)
{
    if (!create.target)
    {
        create.target = module->default_target;
    }

    let function_type = arena_allocate(module->arena, IrFunctionType, 1);
    *function_type = create;
    return function_type;
}

BUSTER_LOCAL IrType* ir_create_type(IrModule* module, IrType type)
{
    let result = arena_allocate(module->arena, IrType, 1);
    *result = type;
    return result;
}

BUSTER_LOCAL void ir_block_append_instruction(IrBasicBlock* basic_block, IrInstruction* instruction)
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

BUSTER_LOCAL void ir_function_append_instruction(IrFunction* function, IrInstruction* instruction)
{
    ir_block_append_instruction(function->current_basic_block, instruction);
}

BUSTER_LOCAL IrInstruction* ir_create_return(IrModule* module, IrFunction* function, IrValue* value)
{
    let result = arena_allocate(module->arena, IrInstruction, 1);
    *result = (IrInstruction) {
        .value = value,
        .id = IR_INSTRUCTION_RETURN,
    };
    ir_function_append_instruction(function, result);
    return result;
}

BUSTER_LOCAL IrValue* ir_constant_integer(IrModule* module, IrType* type, u64 value)
{
    let result = arena_allocate(module->arena, IrValue, 1);
    *result = (IrValue) {
        .constant_integer = value,
        .type = type,
        .id = IR_VALUE_ID_CONSTANT_INTEGER,
    };
    return result;
}

BUSTER_LOCAL IrFunction* ir_function_create(IrModule* module, IrGlobalSymbol symbol)
{
    IrFunction* function = 0;
    symbol.id = IR_GLOBAL_SYMBOL_FUNCTION;

    if (symbol.type)
    {
        function = arena_allocate(module->function_arena, IrFunction, 1);
        let entry_basic_block = arena_allocate(module->arena, IrBasicBlock, 1);
        *function = (IrFunction) {
            .symbol = symbol,
            .entry_block = entry_basic_block,
            .current_basic_block = entry_basic_block,
        };
    }

    return function;
}

BUSTER_LOCAL IrGlobalVariable* ir_global_variable_create(IrModule* module, IrGlobalSymbol symbol)
{
    IrGlobalVariable* result = 0;
    symbol.id = IR_GLOBAL_SYMBOL_VARIABLE;

    if (symbol.type)
    {
        result = arena_allocate(module->global_variable_arena, IrGlobalVariable, 1);
        *result = (IrGlobalVariable) {
            .symbol = symbol,
        };
    }

    return result;
}

BUSTER_DECL IrFunctions ir_module_get_functions(IrModule* module)
{
    let arena_pointer = (u8*)module->function_arena;
    let start = (u8*)(module->function_arena + 1);
    return (IrFunctions) {
        .pointer = (IrFunction*)start,
        .length = ((arena_pointer + module->function_arena->position) - start) / sizeof(IrFunction),
    };
}

#if BUSTER_INCLUDE_TESTS
BUSTER_IMPL IrModule* ir_create_mock_module(Arena* arena)
{
    let module = ir_module_create(arena, 0, S8("basic"));
    let i32_type = ir_create_type(module, (IrType) {
        .id = IR_TYPE_I32,
    });
    let function = ir_function_create(module, (IrGlobalSymbol) {
        .linkage = IR_LINKAGE_EXTERNAL,
        .type = &ir_function_type_get(module, (IrFunctionType) {
            .return_type = i32_type,
            .calling_convention = IR_CALLING_CONVENTION_C,
        })->type,
    });
    ir_create_return(module, function, ir_constant_integer(module, i32_type, 0));

    return module;
}

BUSTER_IMPL bool ir_tests(TestArguments* arguments)
{
    BUSTER_UNUSED(arguments);
    return true;
}
#endif
