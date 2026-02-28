#pragma once
#include <buster/compiler/backend/code_generation.h>
#include <buster/arena.h>
#include <buster/integer.h>
#include <buster/assertion.h>

BUSTER_IMPL CodeGeneration module_generation_initialize()
{
    return (CodeGeneration){
        .code_arena = arena_create((ArenaCreation){}),
        .data_arena = arena_create((ArenaCreation){}),
    };
}

BUSTER_GLOBAL_LOCAL void write_prologue(CodeGeneration* generation)
{
    u8 push_rbp[] = { 0x55 };
    u8 mov_rbp_rsp[] = { 0x48, 0x89, 0xe5 };
    let bytes = arena_allocate(generation->code_arena, u8, BUSTER_ARRAY_LENGTH(push_rbp) + BUSTER_ARRAY_LENGTH(mov_rbp_rsp));
    memcpy(bytes, push_rbp, sizeof(push_rbp));
    memcpy(bytes + sizeof(push_rbp), mov_rbp_rsp, sizeof(mov_rbp_rsp));
}

BUSTER_GLOBAL_LOCAL void write_epilogue(CodeGeneration* generation)
{
    u8 pop_rbp[] = { 0x5d };
    u8 ret[] = { 0xc3 };
    let bytes = arena_allocate(generation->code_arena, u8, BUSTER_ARRAY_LENGTH(pop_rbp) + BUSTER_ARRAY_LENGTH(ret));
    memcpy(bytes, pop_rbp, sizeof(pop_rbp));
    memcpy(bytes + sizeof(pop_rbp), ret, sizeof(ret));
}

ENUM_T(Register_x86_64, u8,
    REGISTER_A = 0,
    REGISTER_C = 1,
    REGISTER_D = 2,
    REGISTER_B = 3,
    REGISTER_SP = 4,
    REGISTER_BP = 5,
    REGISTER_SI = 6,
    REGISTER_DI = 7,
    REGISTER_08 = 8,
    REGISTER_09 = 9,
    REGISTER_10 = 10,
    REGISTER_11 = 11,
    REGISTER_12 = 12,
    REGISTER_13 = 13,
    REGISTER_14 = 14,
    REGISTER_15 = 15,
);

BUSTER_GLOBAL_LOCAL Register_x86_64 system_v_return_registers[] = {
    REGISTER_A,
    REGISTER_D,
};

ENUM_T(MachineInstructionId, u64,
    MACHINE_INSTRUCTION_CONSTANT_INTEGER_REGISTER,
);

UNION(MachineOperand)
{
    u64 constant_integer;
    f64 constant_float;
    u8 register_name;
};

STRUCT(MachineInstruction)
{
    MachineInstructionId id;
    MachineOperand operands[4];
};

BUSTER_IMPL bool function_generate(CodeGeneration* generation, Arena* arena, IrModule* module, IrFunction* function, CodeGenerationOptions options)
{
    bool result = true;
    let original_position = arena->position;

    BUSTER_UNUSED(generation);
    BUSTER_UNUSED(arena);
    BUSTER_UNUSED(module);
    BUSTER_UNUSED(options);

    function->code_position = generation->code_arena->position - sizeof(Arena);
    write_prologue(generation);

    let block = function->entry_block;

    // Instruction selection
    let machine_instruction_start = align_forward(arena->position, alignof(MachineInstruction));
    
    while (block)
    {
        for (IrInstruction* instruction = block->first; instruction; instruction = instruction->next)
        {
            switch (instruction->id)
            {
                break; case IR_INSTRUCTION_RETURN:
                {
                    let return_instruction = arena_allocate(arena, MachineInstruction, 1);
                    let return_value = instruction->value;

                    switch (return_value->id)
                    {
                        break; case IR_VALUE_ID_CONSTANT_INTEGER:
                        {
                            return_instruction->operands[0] = (MachineOperand) {
                                .register_name = system_v_return_registers[0],
                            };
                            return_instruction->operands[1] = (MachineOperand) {
                                .constant_integer = return_value->constant_integer,
                            };
                            return_instruction->id = MACHINE_INSTRUCTION_CONSTANT_INTEGER_REGISTER;
                        }
                    }

                    block = 0;
                }
            }
        }
    }

    let machine_instruction_end = arena->position;
    let instructions = (MachineInstruction*)((u8*)arena + machine_instruction_start);
    let byte_range = (machine_instruction_end - machine_instruction_start);
    BUSTER_CHECK(byte_range % sizeof(MachineInstruction) == 0);
    let instruction_count = byte_range / sizeof(MachineInstruction);

    // Register allocation

    // Emit

    for (u64 instruction_i = 0; instruction_i < instruction_count; instruction_i += 1)
    {
        MachineInstruction* instruction = &instructions[instruction_i];

        switch (instruction->id)
        {
            break; case MACHINE_INSTRUCTION_CONSTANT_INTEGER_REGISTER:
            {
                if (instruction->operands[0].constant_integer == 0)
                {
                    u8 my_buffer[2] = {0x31, 0xc0};
                    let buffer = arena_allocate(generation->code_arena, u8, BUSTER_ARRAY_LENGTH(my_buffer));
                    memcpy(buffer, my_buffer, sizeof(my_buffer));
                }
                else
                {
                    result = false;
                }
            }
        }
    }

    write_epilogue(generation);

    arena->position = original_position;

    return result;
}
