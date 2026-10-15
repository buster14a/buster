// POSIX diagnostic for the production parameter-edge splitter (#296).
// Entry: main BLOCKS REPEATS chain|fanout|duplicates|no-split x64|a64.
// Reversed raw edges exercise stable ordering. Duplicates put a non-copy
// edge before two splitting edges. Check complete generated records against
// closed-form expectations; no quadratic oracle or production instrumentation.
// See docs/agents/benchmarking.md: Parameter-edge splitting populations.
#define _POSIX_C_SOURCE 200809L
#include <buster/lib/compiler/codegen/machine.h>
#include <buster/lib/arena.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

ProgramState* program_state;

BUSTER_GLOBAL_LOCAL u64 edge_split_now(void)
{
    struct timespec time;
    if (clock_gettime(CLOCK_MONOTONIC, &time) != 0)
    {
        perror("clock_gettime");
        abort();
    }
    return (u64)time.tv_sec * 1000000000ull + (u64)time.tv_nsec;
}

BUSTER_GLOBAL_LOCAL bool edge_split_parse(char const* text, u32 minimum, u32 maximum, u32* value)
{
    char* end = 0;
    errno = 0;
    unsigned long parsed = strtoul(text, &end, 10);
    bool valid = !errno && text[0] && end && !*end && parsed >= minimum && parsed <= maximum;
    if (valid)
    {
        *value = (u32)parsed;
    }
    return valid;
}

// Include every byte of each checked record, including zeroed reserved fields.
BUSTER_GLOBAL_LOCAL u64 edge_split_hash(u64 hash, void const* data, u64 size)
{
    u8 const* bytes = data;
    for (u64 index = 0; index < size; index += 1)
    {
        hash = (hash ^ bytes[index]) * 1099511628211ull;
    }
    return hash;
}

int main(int argc, char** argv)
{
    u32 count = 0;
    u32 repeats = 0;
    bool valid = argc == 5;
    if (valid)
    {
        valid = edge_split_parse(argv[1], 0, 16384, &count) && edge_split_parse(argv[2], 1, 1000, &repeats);
    }
    bool fanout = valid && (!strcmp(argv[3], "fanout") || !strcmp(argv[3], "duplicates"));
    bool duplicates = valid && !strcmp(argv[3], "duplicates");
    bool no_split = valid && !strcmp(argv[3], "no-split");
    bool a64 = valid && !strcmp(argv[4], "a64");
    valid = valid && (fanout || no_split || !strcmp(argv[3], "chain")) && (a64 || !strcmp(argv[4], "x64"));
    if (!valid)
    {
        fprintf(stderr, "usage: %s BLOCKS:0..16384 REPEATS:1..1000 chain|fanout|duplicates|no-split x64|a64\n", argv[0]);
    }
    else
    {
        Arena* arena = arena_create((ArenaCreation){.reserved_size = 256ull * 1024ull * 1024ull});
        MachineFunction input = {.target = a64 ? machine_target_aarch64() : machine_target_x86_64(),
            .block_count = count, .instruction_count = count};
        u32 destinations = count ? count - 1u : 0;
        u32 multiplicity = duplicates ? 3u : 1u;
        input.edge_count = destinations * multiplicity;
        input.switch_case_count = fanout ? destinations : 0;
        input.blocks = arena_allocate(arena, MachineBlock, count);
        input.instructions = arena_allocate(arena, MachineInstruction, count);
        input.edges = arena_allocate(arena, MachineEdge, input.edge_count);
        input.switch_cases = arena_allocate(arena, MachineSwitchCase, input.switch_case_count);
        for (u32 block = 0; block < count; block += 1)
        {
            input.blocks[block] = (MachineBlock){.first_instruction = block, .instruction_count = 1};
            input.instructions[block] = (MachineInstruction){.opcode = (u16)(a64 ? MACHINE_A64_RET : MACHINE_X64_RET)};
            if (!fanout && block < destinations)
            {
                MachineRef target = machine_ref_make(MACHINE_REF_BLOCK, block + 1u);
                input.instructions[block] = (MachineInstruction){.opcode = (u16)(a64 ? MACHINE_A64_BCC : MACHINE_X64_JCC),
                    .operands = {target, target}};
            }
        }
        if (fanout && destinations)
        {
            input.instructions[0] = (MachineInstruction){.opcode = input.target->switch_opcode, .flags = (u16)destinations,
                .operands = {machine_ref_make(MACHINE_REF_PHYSICAL_REGISTER, 0), machine_ref_make(MACHINE_REF_BLOCK, 1)}};
        }
        for (u32 edge = 0; edge < input.edge_count; edge += 1)
        {
            u32 destination = destinations - edge / multiplicity;
            input.edges[edge] = (MachineEdge){.source_block = fanout ? 0 : destination - 1u,
                .destination_block = destination, .copy_offset = edge,
                .copy_count = (u16)(!no_split && (!duplicates || edge % multiplicity != 0))};
        }
        for (u32 item = 0; item < input.switch_case_count; item += 1)
        {
            input.switch_cases[item] = (MachineSwitchCase){.value = item, .target_block = item + 1u};
        }
        machine_opcode_rows_prewarm();
        u64 input_end = arena->position;
        u32 splits = no_split ? 0 : destinations * (duplicates ? 2u : 1u);
        for (u32 repeat = 0; valid && repeat < repeats; repeat += 1)
        {
            arena_set_position(arena, input_end);
            arena->high_water = input_end;
            MachineFunction output = input;
            u64 start = edge_split_now();
            valid = machine_function_split_parameter_edges(arena, &output);
            u64 elapsed = edge_split_now() - start;
            u64 retained = arena->position - input_end;
            u64 peak = BUSTER_MAX(arena->high_water, arena->position) - input_end;
            u64 published = (u64)(count + splits) * (sizeof(MachineInstruction) + sizeof(MachineBlock)) +
                            (u64)(input.edge_count + splits) * sizeof(MachineEdge) +
                            (u64)input.switch_case_count * sizeof(MachineSwitchCase);
            u64 scratch = 4ull * (count + 2ull * input.edge_count + 3ull * count + 1ull);
            valid = valid && output.block_count == count + splits && output.instruction_count == count + splits &&
                    output.edge_count == input.edge_count + splits && output.switch_case_count == input.switch_case_count;
            valid = valid && (splits ? retained >= published && retained <= published + 64u &&
                             peak >= retained + scratch && peak <= retained + scratch + 64u :
                             retained == 0 && peak == 0 && !memcmp(&input, &output, sizeof(input)));
            u64 hash = 14695981039346656037ull;
            // Every original and inserted block has one row. Reconstruct the
            // complete output in emission order without consulting its records.
            for (u32 block = 0; valid && block < count; block += 1)
            {
                u32 mapped = !splits ? block : fanout ? (block ? splits + block : 0) : 2u * block;
                MachineBlock expected_block = {.first_instruction = mapped, .instruction_count = 1};
                MachineInstruction expected = input.instructions[block];
                if (splits && (fanout ? block == 0 : block < destinations))
                {
                    u32 target = fanout ? (duplicates ? 2u : 1u) * (destinations - 1u) + 1u : mapped + 1u;
                    expected.operands[fanout ? 1 : 0] = machine_ref_make(MACHINE_REF_BLOCK, target);
                    if (!fanout) { expected.operands[1] = expected.operands[0]; }
                }
                valid = !memcmp(output.blocks + mapped, &expected_block, sizeof(expected_block)) &&
                        !memcmp(output.instructions + mapped, &expected, sizeof(expected));
            }
            u32 cursor = 0;
            for (u32 edge = 0; valid && edge < input.edge_count; edge += 1)
            {
                MachineEdge expected = input.edges[edge];
                u32 destination = expected.destination_block;
                u32 mapped_destination = !splits ? destination : fanout ? splits + destination : 2u * destination;
                expected.source_block = fanout || !splits ? expected.source_block : 2u * expected.source_block;
                expected.destination_block = mapped_destination;
                if (splits && expected.copy_count)
                {
                    u32 split = fanout ? (duplicates ? 2u * (edge / 3u) + edge % 3u : edge + 1u) : expected.source_block + 1u;
                    expected.destination_block = split;
                    expected.copy_count = 0;
                    valid = !memcmp(output.edges + cursor++, &expected, sizeof(expected));
                    expected = (MachineEdge){.source_block = split, .destination_block = mapped_destination,
                        .copy_offset = input.edges[edge].copy_offset, .copy_count = 1};
                    MachineBlock expected_block = {.first_instruction = split, .instruction_count = 1};
                    MachineInstruction branch = {.opcode = input.target->unconditional_branch_opcode,
                        .operands = {machine_ref_make(MACHINE_REF_BLOCK, mapped_destination)}};
                    valid = valid && !memcmp(output.blocks + split, &expected_block, sizeof(expected_block)) &&
                            !memcmp(output.instructions + split, &branch, sizeof(branch));
                }
                valid = valid && !memcmp(output.edges + cursor++, &expected, sizeof(expected));
            }
            for (u32 item = 0; valid && item < input.switch_case_count; item += 1)
            {
                MachineSwitchCase expected = {.value = item,
                    .target_block = (duplicates ? 2u : 1u) * (destinations - item - 1u) + 1u};
                valid = !memcmp(output.switch_cases + item, &expected, sizeof(expected));
            }
            if (valid)
            {
                hash = edge_split_hash(hash, output.blocks, (u64)output.block_count * sizeof(MachineBlock));
                hash = edge_split_hash(hash, output.instructions, (u64)output.instruction_count * sizeof(MachineInstruction));
                hash = edge_split_hash(hash, output.edges, (u64)output.edge_count * sizeof(MachineEdge));
                hash = edge_split_hash(hash, output.switch_cases, (u64)output.switch_case_count * sizeof(MachineSwitchCase));
                printf("blocks=%u edges=%u shape=%s target=%s splits=%u retained_bytes=%llu peak_bytes=%llu hash=%016llx split_ns=%llu\n",
                       count, input.edge_count, argv[3], argv[4], splits, (unsigned long long)retained,
                       (unsigned long long)peak, (unsigned long long)hash, (unsigned long long)elapsed);
            }
        }
        if (!valid) { fprintf(stderr, "parameter-edge output or scratch check failed\n"); }
        arena_destroy(arena, 1);
    }
    return valid ? 0 : 1;
}
