// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.

// The C driver's --tier1-schedule=post (#172): Tier 1's published code of a function, each block's records reordered by
// the postallocation scheduler (tier1_schedule.c) before branch sizing and encoding. Include in c_check_tier1.c, after
// tier1_schedule.c.
//
// A block here is a run of records between cuts: the code's start and end, each branch record and the record after it,
// each branch's target, each label's first record, each frame event's boundary and the source events' boundaries (the
// body's start and the epilogue's, -g's). Nothing moves across a cut, so the branch columns, the labels, the frame
// events and the source events keep their records; a block longer than the scheduler's bound is scheduled in pieces of
// it, which only keeps more order. Calls, returns, traps and every other record that may leave the block are the
// scheduler's own barriers. The symbolic references follow their records, sorted by them again as the encoder's fixup
// ends expect (link_add_code_fixups pairs them in order).
//
// With `verify` (--verify), each scheduled block's order must pass tier1_schedule_verify, the scalar reference, which
// rebuilds every dependency from the original records: a refusal is the compile's internal failure.

typedef struct CheckTier1ScheduleCode CheckTier1ScheduleCode;
struct CheckTier1ScheduleCode
{
    X86Instruction* records;
    uint32_t count;
    const uint32_t* branches;
    const uint32_t* targets;
    uint32_t branch_count;
    LinkReference* references;
    uint32_t reference_count;
    const uint32_t* labels;
    uint32_t label_count;
    const EmissionFrameEvent* frames;
    uint32_t frame_count;
    // Further boundaries nothing moves across: the source events'.
    const uint32_t* anchors;
    uint32_t anchor_count;
    // Optional output for source/debug metadata: new record index -> old record index, plus the terminal boundary.
    // The caller supplies count + 1 entries. This adds no cuts and changes none of the scheduler's decisions.
    uint32_t* record_order;
};

typedef struct CheckTier1ScheduleResult CheckTier1ScheduleResult;
struct CheckTier1ScheduleResult
{
    // The blocks scheduled (of two records or more), and the records that changed position.
    uint32_t blocks;
    uint32_t moved;
    // Whether every scheduled block passed the reference (with `verify`), and whether there was room.
    bool verified;
    bool ready;
};

static void check_tier1_schedule_cut(uint8_t* cuts, uint32_t count, uint32_t boundary)
{
    if (boundary <= count)
    {
        cuts[boundary] = 1;
    }
}

// Schedules `code` in place, its records and references, with scratch from `scratch` (popped before it returns).
static CheckTier1ScheduleResult check_tier1_schedule_code(Arena* scratch, const CheckTier1ScheduleCode* code, bool vector, bool verify)
{
    CheckTier1ScheduleResult result = { .verified = true };
    const uint64_t mark = scratch->length;
    const uint32_t count = code->count;
    uint8_t* cuts = BUSTER_ARENA_PUSH_ARRAY(scratch, uint8_t, (uint64_t)count + 1);
    uint32_t* places = code->reference_count != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)count + 1) : 0;
    Tier1ScheduleGraph* graph = arena_push(scratch, sizeof(Tier1ScheduleGraph), 64);
    uint16_t* order = BUSTER_ARENA_PUSH_ARRAY(scratch, uint16_t, BUSTER_TIER1_SCHEDULE_MAX_RECORDS);
    X86Instruction* block = BUSTER_ARENA_PUSH_ARRAY(scratch, X86Instruction, BUSTER_TIER1_SCHEDULE_MAX_RECORDS);
    result.ready = cuts != 0 && graph != 0 && order != 0 && block != 0 && (code->reference_count == 0 || places != 0);
    if (result.ready)
    {
        memset(cuts, 0, (uint64_t)count + 1);
        cuts[0] = 1;
        cuts[count] = 1;
        for (uint32_t k = 0; k < code->branch_count; k += 1)
        {
            check_tier1_schedule_cut(cuts, count, code->branches[k]);
            check_tier1_schedule_cut(cuts, count, code->branches[k] + 1);
            check_tier1_schedule_cut(cuts, count, code->targets[k]);
        }
        for (uint32_t k = 0; k < code->label_count; k += 1)
        {
            check_tier1_schedule_cut(cuts, count, code->labels[k]);
        }
        for (uint32_t k = 0; k < code->frame_count; k += 1)
        {
            check_tier1_schedule_cut(cuts, count, code->frames[k].at.boundary);
        }
        for (uint32_t k = 0; k < code->anchor_count; k += 1)
        {
            check_tier1_schedule_cut(cuts, count, code->anchors[k]);
        }
        for (uint32_t i = 0; places != 0 && i <= count; i += 1)
        {
            places[i] = i;
        }
        for (uint32_t i = 0; code->record_order != NULL && i <= count; i += 1)
        {
            code->record_order[i] = i;
        }
        uint32_t start = 0;
        for (uint32_t end = 1; end <= count; end += 1)
        {
            if (cuts[end] != 0 || end - start == BUSTER_TIER1_SCHEDULE_MAX_RECORDS)
            {
                const Tier1ScheduleInput input = {
                    .instructions = code->records + start,
                    .count = end - start,
                };
                Tier1ScheduleStats stats = { 0 };
                const bool scheduled = input.count >= 2 && tier1_schedule_block(input, graph, order, BUSTER_TIER1_SCHEDULE_MAX_RECORDS, vector, &stats) == TIER1_SCHEDULE_OK;
                result.blocks += scheduled;
                if (scheduled && verify)
                {
                    result.verified = result.verified && tier1_schedule_verify(input, order);
                }
                if (scheduled && stats.moved != 0)
                {
                    result.moved += stats.moved;
                    memcpy(block, input.instructions, input.count * sizeof(X86Instruction));
                    for (uint32_t position = 0; position < input.count; position += 1)
                    {
                        code->records[start + position] = block[order[position]];
                        if (code->record_order != NULL)
                        {
                            code->record_order[start + position] = start + order[position];
                        }
                        if (places != 0)
                        {
                            places[start + order[position]] = start + position;
                        }
                    }
                }
                start = end;
            }
        }
        if (result.moved != 0 && places != 0)
        {
            // Each reference to its record's new place, then in the records' order again: stable, and each block's
            // references only change places among themselves, so the insertion's moves are few.
            for (uint32_t k = 0; k < code->reference_count; k += 1)
            {
                code->references[k].instruction = code->references[k].instruction <= count ? places[code->references[k].instruction] : code->references[k].instruction;
            }
            for (uint32_t k = 1; k < code->reference_count; k += 1)
            {
                const LinkReference reference = code->references[k];
                uint32_t place = k;
                while (place != 0 && code->references[place - 1].instruction > reference.instruction)
                {
                    code->references[place] = code->references[place - 1];
                    place -= 1;
                }
                code->references[place] = reference;
            }
        }
    }
    arena_pop_to(scratch, mark);
    return result;
}
