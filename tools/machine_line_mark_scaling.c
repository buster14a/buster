// Linux/POSIX scaling probe for the production scheduler line-mark repair.
// The schedule mode drives the accepted machine_schedule_function path with
// one mark per instruction. Optional remap modes isolate the ordered fast path,
// the sixteen-mark crossover, and large disordered repair without timer noise.
#define _POSIX_C_SOURCE 200809L
#include <buster/lib/compiler/codegen/machine.h>
#ifndef BUSTER_LINE_MARK_HELPER_BENCH
#define BUSTER_LINE_MARK_HELPER_BENCH 1
#endif
#if BUSTER_LINE_MARK_HELPER_BENCH
#include <buster/lib/compiler/codegen/machine_schedule_internal.h>
#endif
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

BUSTER_GLOBAL_LOCAL bool machine_line_mark_parse(char const* text, unsigned long minimum, unsigned long maximum,
                                                 unsigned long* value)
{
    char* end = 0;
    errno = 0;
    unsigned long parsed = strtoul(text, &end, 10);
    bool valid = errno == 0 && text[0] && end && !*end && parsed >= minimum && parsed <= maximum;
    if (valid)
    {
        *value = parsed;
    }
    return valid;
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

BUSTER_GLOBAL_LOCAL int machine_line_mark_schedule(Arena* arena, u32 leaf_count, u32 samples, u32 batch)
{
    int status = 0;
    MachineFunction function = machine_line_mark_bench_function(arena, leaf_count);
    MachineVerifyResult verified = machine_verify_function(&function);
    status |= verified.error != MACHINE_VERIFY_NONE;
    machine_opcode_rows_prewarm();
    MachineInstruction* input_instructions = arena_allocate(arena, MachineInstruction, function.instruction_count);
    memcpy(input_instructions, function.instructions, sizeof(*input_instructions) * function.instruction_count);
    u64 input_end = arena->position;
    u64 expected_hash = 0;
    for (u32 sample = 0; sample < samples + 1; sample += 1)
    {
        MachineScheduleResult scheduled = {0};
        unsigned long long start = machine_line_mark_bench_now();
        for (u32 iteration = 0; iteration < batch; iteration += 1)
        {
            arena_set_position(arena, input_end);
            scheduled = machine_schedule_function(arena, &function);
        }
        unsigned long long finish = machine_line_mark_bench_now();
        u32 permuted = 0;
        u64 displacement = 0;
        bool exact = scheduled.moved && scheduled.function.line_mark_count == function.instruction_count &&
                     memcmp(function.instructions, input_instructions,
                            sizeof(*input_instructions) * function.instruction_count) == 0;
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
        expected_hash = sample ? expected_hash : hash;
        status |= !exact || hash != expected_hash || permuted < function.instruction_count / 2;
        if (sample)
        {
            printf("mode=schedule leaves=%u rows=%u batch=%u schedule_ns=%llu retained_bytes=%llu "
                   "permuted=%u displacement=%llu hash=%016llx\n",
                   leaf_count, function.instruction_count, batch, (finish - start) / batch,
                   (unsigned long long)(arena->position - input_end), permuted,
                   (unsigned long long)displacement, (unsigned long long)hash);
        }
    }
    return status;
}

#if BUSTER_LINE_MARK_HELPER_BENCH
BUSTER_GLOBAL_LOCAL int machine_line_mark_remap(Arena* arena, char const* mode, u32 mark_count, u32 samples, u32 batch)
{
    int status = 0;
    MachineLineMark* marks = arena_allocate(arena, MachineLineMark, mark_count ? mark_count : 1);
    u32* new_rows = arena_allocate(arena, u32, mark_count ? mark_count : 1);
    u32* inverse_rows = arena_allocate(arena, u32, mark_count ? mark_count : 1);
    bool reverse = strcmp(mode, "remap-reverse") == 0;
    bool nearly = strcmp(mode, "remap-nearly") == 0;
    for (u32 index = 0; index < mark_count; index += 1)
    {
        marks[index] = (MachineLineMark){.row = index, .instruction = index};
        new_rows[index] = reverse ? mark_count - 1 - index : index;
    }
    if (nearly && mark_count > 1)
    {
        u32 swap = new_rows[mark_count - 2];
        new_rows[mark_count - 2] = new_rows[mark_count - 1];
        new_rows[mark_count - 1] = swap;
    }
    for (u32 index = 0; index < mark_count; index += 1)
    {
        inverse_rows[new_rows[index]] = index;
    }

    u64 input_end = arena->position;
    arena_allocate(arena, MachineLineMark, mark_count ? mark_count : 1);
    u64 retained_end = arena->position;
    arena_set_position(arena, input_end);
    u64 expected_hash = 0;
    for (u32 sample = 0; sample < samples + 1; sample += 1)
    {
        MachineLineMark* result = 0;
        unsigned long long start = machine_line_mark_bench_now();
        for (u32 iteration = 0; iteration < batch; iteration += 1)
        {
            arena_set_position(arena, input_end);
            result = machine_schedule_remap_line_marks(arena, arena, marks, mark_count, new_rows, mark_count);
        }
        unsigned long long finish = machine_line_mark_bench_now();
        u32 permuted = 0;
        u64 displacement = 0;
        bool exact = result != 0 && arena->position == retained_end;
        for (u32 row = 0; exact && row < mark_count; row += 1)
        {
            exact = result[row].row == row && result[row].instruction == inverse_rows[row];
            permuted += inverse_rows[row] != row;
            displacement += inverse_rows[row] > row ? inverse_rows[row] - row : row - inverse_rows[row];
        }
        for (u32 index = 0; exact && index < mark_count; index += 1)
        {
            exact = marks[index].row == index && marks[index].instruction == index;
        }
        u64 hash = exact ? machine_line_mark_hash(result, mark_count) : 0;
        expected_hash = sample ? expected_hash : hash;
        status |= !exact || hash != expected_hash;
        if (sample)
        {
            printf("mode=%s marks=%u batch=%u remap_ns=%llu retained_bytes=%llu "
                   "permuted=%u displacement=%llu hash=%016llx\n",
                   mode, mark_count, batch, (finish - start) / batch,
                   (unsigned long long)(arena->position - input_end), permuted,
                   (unsigned long long)displacement, (unsigned long long)hash);
        }
    }
    return status;
}
#endif

int main(int argc, char** argv)
{
    char const* mode = "schedule";
    int value_argument = 1;
    if (argc > 1 && (argv[1][0] < '0' || argv[1][0] > '9'))
    {
        mode = argv[1];
        value_argument = 2;
    }
    bool schedule = strcmp(mode, "schedule") == 0;
#if BUSTER_LINE_MARK_HELPER_BENCH
    bool remap = strcmp(mode, "remap-ordered") == 0 || strcmp(mode, "remap-reverse") == 0 ||
                 strcmp(mode, "remap-nearly") == 0;
#else
    bool remap = false;
#endif
    unsigned long values[3] = {schedule ? 16384ul : 65536ul, 11, 1};
    int value_count = argc - value_argument;
    bool valid = (schedule || remap) && value_count >= 0 && value_count <= 3;
    for (int index = 0; valid && index < value_count; index += 1)
    {
        unsigned long maximum = index == 0 ? (schedule ? 65536ul : 1048576ul) : (index == 1 ? 10000ul : 1000000ul);
        unsigned long minimum = index == 0 ? (schedule ? 32ul : 1ul) : 1ul;
        valid = machine_line_mark_parse(argv[value_argument + index], minimum, maximum, values + index);
    }
    if (schedule)
    {
        valid = valid && !(values[0] & (values[0] - 1));
    }
    int status = 2;
    if (!valid)
    {
        fprintf(stderr, "usage: %s [schedule] [power-of-two leaves:32..65536] [samples:1..10000] [batch:1..1000000]\n",
                argv[0]);
#if BUSTER_LINE_MARK_HELPER_BENCH
        fprintf(stderr, "       %s remap-{ordered,reverse,nearly} [marks:1..1048576] [samples:1..10000] "
                        "[batch:1..1000000]\n",
                argv[0]);
#endif
    }
    else
    {
        ThreadContext* thread_context = thread_context_allocate();
        thread_context_select(thread_context);
        Arena* arena = arena_create((ArenaCreation){.reserved_size = BUSTER_GB(1)});
        status = schedule ? machine_line_mark_schedule(arena, (u32)values[0], (u32)values[1], (u32)values[2]) : 0;
#if BUSTER_LINE_MARK_HELPER_BENCH
        if (remap)
        {
            status = machine_line_mark_remap(arena, mode, (u32)values[0], (u32)values[1], (u32)values[2]);
        }
#endif
        arena_destroy(arena, 1);
        thread_context_release(thread_context);
    }
    return status;
}
