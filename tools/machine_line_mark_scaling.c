// Linux/POSIX scaling probe for the production scheduler line-mark repair.
// One large block publishes one mark per instruction and forces an accepted,
// heavily permuted schedule. Build commands and paired measurements live in
// the corresponding performance audit.
#define _POSIX_C_SOURCE 200809L
#include <buster/lib/compiler/codegen/machine.h>
#include <buster/lib/arena.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

BUSTER_GLOBAL_LOCAL ProgramState machine_line_mark_program_state;
ProgramState* program_state = &machine_line_mark_program_state;

BUSTER_GLOBAL_LOCAL unsigned long long machine_line_mark_bench_now(void)
{
    struct timespec time;
    if (clock_gettime(CLOCK_MONOTONIC, &time) != 0)
    {
        perror("clock_gettime");
        abort();
    }
    return (unsigned long long)time.tv_sec * 1000000000ull + (unsigned long long)time.tv_nsec;
}

BUSTER_GLOBAL_LOCAL MachineFunction machine_line_mark_bench_function(Arena* arena, u32 leaf_count)
{
    MachineFunctionBuilder builder = machine_function_builder_begin(arena);
    u32* values = arena_allocate(arena, u32, leaf_count);
    machine_builder_block_begin(&builder);
    for (u32 leaf = 0; leaf < leaf_count; leaf += 1)
    {
        u32 row = builder.instructions.total_count;
        u32 value = machine_builder_virtual_register(&builder, (MachineVirtualRegister){
            .definition_point = machine_point_make(row, MACHINE_POINT_AFTER),
            .register_class = MACHINE_REGISTER_CLASS_GENERAL,
            .typed_origin = IR_ID_UNDERLYING_INVALID,
        });
        machine_builder_instruction(&builder, (MachineInstruction){
            .opcode = MACHINE_X64_MOV_RI,
            .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, value), machine_ref_make(MACHINE_REF_IMMEDIATE, 0)},
        });
        values[leaf] = value;
    }
    for (u32 width = leaf_count; width > 1; width /= 2)
    {
        for (u32 pair = 0; pair < width / 2; pair += 1)
        {
            u32 row = builder.instructions.total_count;
            u32 value = machine_builder_virtual_register(&builder, (MachineVirtualRegister){
                .definition_point = machine_point_make(row, MACHINE_POINT_AFTER),
                .register_class = MACHINE_REGISTER_CLASS_GENERAL,
                .typed_origin = IR_ID_UNDERLYING_INVALID,
            });
            machine_builder_instruction(&builder, (MachineInstruction){
                .opcode = MACHINE_X64_ADD64,
                .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, value),
                             machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, values[2 * pair]),
                             machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, values[2 * pair + 1])},
            });
            values[pair] = value;
        }
    }
    machine_builder_instruction(&builder, (MachineInstruction){
        .opcode = MACHINE_X64_MOV_RR,
        .operands = {machine_ref_make(MACHINE_REF_PHYSICAL_REGISTER, MACHINE_X64_RAX),
                     machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, values[0])},
    });
    machine_builder_instruction(&builder, (MachineInstruction){.opcode = MACHINE_X64_RET});
    machine_builder_block_end(&builder, (MachineBlock){0});

    MachineFunction function = machine_function_builder_finish(arena, &builder);
    function.target = machine_target_x86_64();
    function.immediates = arena_allocate(arena, u64, 1);
    function.immediates[0] = 1;
    function.immediate_count = 1;
    function.line_mark_count = function.instruction_count;
    function.line_marks = arena_allocate(arena, MachineLineMark, function.line_mark_count);
    for (u32 row = 0; row < function.instruction_count; row += 1)
    {
        function.line_marks[row] = (MachineLineMark){.row = row, .instruction = row};
    }
    return function;
}

BUSTER_GLOBAL_LOCAL u64 machine_line_mark_hash(MachineLineMark const* marks, u32 mark_count)
{
    u64 hash = 1469598103934665603ull;
    for (u32 mark_index = 0; mark_index < mark_count; mark_index += 1)
    {
        hash ^= ((u64)marks[mark_index].row << 32) | marks[mark_index].instruction;
        hash *= 1099511628211ull;
    }
    return hash;
}

int main(int argc, char** argv)
{
    unsigned long values[2] = {16384, 11};
    int status = argc > 3;
    for (int argument = 1; argument < argc && argument <= 2; argument += 1)
    {
        char* end = 0;
        errno = 0;
        values[argument - 1] = strtoul(argv[argument], &end, 10);
        status |= errno != 0 || !argv[argument][0] || !end || *end != 0;
    }
    status |= values[0] < 32 || values[0] > 65536 || (values[0] & (values[0] - 1)) != 0;
    status |= values[1] < 1 || values[1] > 10000;
    if (status)
    {
        fprintf(stderr, "usage: %s [power-of-two leaves:32..65536] [repeats:1..10000]\n", argv[0]);
    }
    else
    {
        u32 leaf_count = (u32)values[0];
        u32 repeats = (u32)values[1];
        ThreadContext* thread_context = thread_context_allocate();
        thread_context_select(thread_context);
        Arena* arena = arena_create((ArenaCreation){.reserved_size = BUSTER_GB(1)});
        MachineFunction function = machine_line_mark_bench_function(arena, leaf_count);
        MachineVerifyResult verified = machine_verify_function(&function);
        status |= verified.error != MACHINE_VERIFY_NONE;
        machine_opcode_rows_prewarm();
        u64 input_end = arena->position;
        u64 expected_hash = 0;
        for (u32 repeat = 0; repeat < repeats + 1; repeat += 1)
        {
            arena_set_position(arena, input_end);
            unsigned long long start = machine_line_mark_bench_now();
            MachineScheduleResult scheduled = machine_schedule_function(arena, &function);
            unsigned long long finish = machine_line_mark_bench_now();
            u32 permuted = 0;
            u64 displacement = 0;
            bool exact = scheduled.moved && scheduled.function.line_mark_count == function.instruction_count;
            for (u32 row = 0; exact && row < scheduled.function.line_mark_count; row += 1)
            {
                MachineLineMark mark = scheduled.function.line_marks[row];
                exact = mark.row == row && mark.instruction < function.instruction_count &&
                        memcmp(scheduled.function.instructions + row, function.instructions + mark.instruction,
                               sizeof(MachineInstruction)) == 0;
                permuted += mark.instruction != row;
                displacement += mark.instruction > row ? mark.instruction - row : row - mark.instruction;
            }
            for (u32 row = 0; exact && row < function.instruction_count; row += 1)
            {
                exact = function.line_marks[row].row == row && function.line_marks[row].instruction == row;
            }
            u64 hash = exact ? machine_line_mark_hash(scheduled.function.line_marks, scheduled.function.line_mark_count) : 0;
            expected_hash = repeat ? expected_hash : hash;
            status |= !exact || hash != expected_hash || permuted < function.instruction_count / 2;
            if (repeat)
            {
                printf("leaves=%u rows=%u schedule_ns=%llu retained_bytes=%llu permuted=%u displacement=%llu hash=%016llx\n",
                       leaf_count, function.instruction_count, finish - start, (unsigned long long)(arena->position - input_end),
                       permuted, (unsigned long long)displacement, (unsigned long long)hash);
            }
        }
        arena_destroy(arena, 1);
        thread_context_release(thread_context);
    }
    return status;
}
