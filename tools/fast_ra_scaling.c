// Linux/POSIX scaling probe for the real FAST prepass and placement.
// Empty-value chain/fanout CFGs isolate edge lookup; they are not a code-quality workload.
// Build commands and paired full-compiler measurements live in the audit.
#define _POSIX_C_SOURCE 200809L
#include <buster/lib/compiler/codegen/machine.h>
#include <buster/lib/arena.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>

BUSTER_GLOBAL_LOCAL unsigned long long fast_ra_bench_now(void)
{
    struct timespec t;
    if (clock_gettime(CLOCK_MONOTONIC, &t) != 0)
    {
        perror("clock_gettime");
        abort();
    }
    return (unsigned long long)t.tv_sec * 1000000000ull + (unsigned long long)t.tv_nsec;
}

int main(int argc, char** argv)
{
    unsigned long values[2] = {4096, 11};
    int status = argc > 4;
    for (int i = 1; i < argc && i <= 2; i += 1)
    {
        char* end = 0;
        errno = 0;
        values[i - 1] = strtoul(argv[i], &end, 10);
        status |= errno != 0 || !argv[i][0] || !end || *end != 0;
    }
    status |= values[0] < 2 || values[0] > 65536 || values[1] < 1 || values[1] > 10000;
    int fanout = argc > 3 && strcmp(argv[3], "fanout") == 0;
    status |= argc > 3 && !fanout && strcmp(argv[3], "chain") != 0;
    if (status)
    {
        fprintf(stderr, "usage: %s [blocks:2..65536] [repeats:1..10000] [chain|fanout]\n", argv[0]);
    }
    else
    {
        unsigned n = (unsigned)values[0];
        unsigned repeats = (unsigned)values[1];
        Arena* arena = arena_create((ArenaCreation){.reserved_size = 1024ull * 1024ull * 1024ull});
        MachineFunction f = {0};
        f.target = machine_target_x86_64();
        f.block_count = f.instruction_count = n;
        f.edge_count = n - 1u;
        f.blocks = arena_allocate(arena, MachineBlock, n);
        f.instructions = arena_allocate(arena, MachineInstruction, n);
        f.edges = arena_allocate(arena, MachineEdge, n - 1u);
        for (unsigned b = 0; b < n; ++b)
        {
            f.blocks[b] = (MachineBlock){.first_instruction = b, .instruction_count = 1};
            f.instructions[b] = (MachineInstruction){.opcode = MACHINE_X64_RET};
            if (b + 1u < n)
            {
                f.instructions[b].opcode = MACHINE_X64_JMP;
                f.instructions[b].operands[0] = machine_ref_make(MACHINE_REF_BLOCK, b + 1u);
                // Reversing raw edges exposes the full scan while preserving the CFG.
                f.edges[n - 2u - b] = (MachineEdge){.source_block = b, .destination_block = b + 1u};
            }
        }
        if (fanout)
        {
            f.switch_case_count = n - 1u;
            f.switch_cases = arena_allocate(arena, MachineSwitchCase, f.switch_case_count);
            f.instructions[0] = (MachineInstruction){.opcode = MACHINE_X64_INDIRECT_BRANCH,
                .operands = {machine_ref_make(MACHINE_REF_PHYSICAL_REGISTER, MACHINE_X64_RAX)}, .flags = (u16)(n - 1u)};
            for (unsigned destination = 1; destination < n; destination += 1)
            {
                f.instructions[destination] = (MachineInstruction){.opcode = MACHINE_X64_RET};
                f.switch_cases[destination - 1u] = (MachineSwitchCase){.target_block = destination};
                f.edges[n - 1u - destination] = (MachineEdge){.source_block = 0, .destination_block = destination};
            }
        }
        machine_opcode_rows_prewarm();
        unsigned long long input_end = arena->position;
        for (unsigned r = 0; r < repeats + 1u; ++r)
        {
            arena_set_position(arena, input_end);
            unsigned long long start = fast_ra_bench_now();
            MachineFastPrepass prepass = machine_fast_prepass_build(arena, &f, false);
            unsigned long long middle = fast_ra_bench_now();
            unsigned long long prepass_bytes = arena->position - input_end;
            MachineStackPlacement place = machine_fast_placement_build_prepassed(arena, &f, &prepass, 0, 0, 0, 0, 0, 0, 0, 0, 0);
            unsigned long long finish = fast_ra_bench_now();
            status |= !prepass.valid || !place.valid;
            if (r)
                printf("blocks=%u prepass_ns=%llu placement_ns=%llu total_ns=%llu prepass_bytes=%llu total_bytes=%llu spills=%u reloads=%u copies=%u edits=%u frame=%u\n", n, middle-start, finish-middle, finish-start, prepass_bytes, (unsigned long long)arena->position-input_end, place.spill_count, place.reload_count, place.copy_count, place.edit_count, place.frame_size);
        }
        arena_destroy(arena, 1);
    }
    return status;
}

// The standalone benchmark has no IDE entry point. The OS failure path's
// debugger query accepts a null program-state pointer.
ProgramState* program_state;
