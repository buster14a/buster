// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// The call-free labels fixture's table base must execute once, even when block placement puts its dispatch after
// its last backward direct branch. Include after c_check_tier1_table_targets_test.c. This bounded object oracle
// checks an entry prefix and every direct and four table targets, rather than inferring a loop from physical order.

// A direct branch's target is any exact instruction boundary; a table entry additionally
// must target an instruction strictly after the single table-base LEA.
static uint32_t check_test_tier1_table_index(const X86Decoded* records, uint32_t count, int64_t goal)
{
    uint32_t low = 0;
    uint32_t high = count;
    while (low < high)
    {
        const uint32_t middle = low + (high - low) / 2;
        low = (int64_t)records[middle].offset < goal ? middle + 1 : low;
        high = (int64_t)records[middle].offset < goal ? high : middle;
    }
    return low < count && (int64_t)records[low].offset == goal ? low : count;
}

static bool check_test_tier1_table_boundary(const X86Decoded* records, uint32_t count, int64_t goal, uint32_t address)
{
    const uint32_t target = check_test_tier1_table_index(records, count, goal);
    return target < count && target > address;
}

// The base must run exactly once on every path to the indirect dispatch, even when an entry
// guard branches directly to the return before materializing it. Walk the decoded CFG with
// two states (before and after the LEA), including all four ELF-resolved indirect targets.
// A path that dispatches without the LEA or visits it a second time is rejected. Check every
// direct branch's boundary, including unreachable ones, and refuse unknown controls.
static bool check_test_tier1_table_entry(const X86Decoded* records, uint32_t count, uint32_t address, uint32_t dispatch, const uint32_t* targets)
{
    bool valid = records != NULL && targets != NULL && count != 0 && address < count && dispatch < count;
    uint32_t addresses = 0;
    uint32_t jumps = 0;
    for (uint32_t i = 0; valid && i < count; i += 1)
    {
        const X86Instruction* instruction = &records[i].instruction;
        const uint32_t form = instruction->form & BUSTER_X86_FORM_NUMBER;
        const uint32_t control = (uint32_t)x86_effects(form)->kinds >> 4;
        const bool table_address = form == X86_FORM_LEA_R64_M && instruction->mode == BUSTER_X86_MEMORY_RIP;
        const bool indirect = form == X86_FORM_JMP_RM64 && instruction->mode == BUSTER_X86_REGISTER_DIRECT;
        const bool relative = x86_form_descriptions[form].relative_bytes != 0;
        addresses += table_address;
        jumps += indirect;
        valid = (!table_address || i == address) &&
                (control == BUSTER_X86_CONTROL_NONE || control == BUSTER_X86_CONTROL_BRANCH ||
                 control == BUSTER_X86_CONTROL_JUMP || control == BUSTER_X86_CONTROL_RETURN);
        if (valid && relative)
        {
            const int64_t goal = (int64_t)records[i].offset + records[i].length + instruction->displacement;
            valid = (control == BUSTER_X86_CONTROL_JUMP || control == BUSTER_X86_CONTROL_BRANCH) &&
                    check_test_tier1_table_index(records, count, goal) < count;
        }
        else if (valid && (control == BUSTER_X86_CONTROL_JUMP || control == BUSTER_X86_CONTROL_BRANCH))
        {
            valid = control == BUSTER_X86_CONTROL_JUMP && indirect && i == dispatch;
        }
    }
    for (uint32_t t = 0; valid && t < 4; t += 1)
    {
        valid = check_test_tier1_table_boundary(records, count, targets[t], address);
    }

    // State bit 0: reached without executing the LEA. Bit 1: reached after executing it.
    // A monotone fixed point adds at most two bits per instruction, so 2 * count rounds suffice.
    uint8_t* reached = valid && addresses == 1 && jumps == 1 ? calloc(count, sizeof(uint8_t)) : NULL;
    valid = valid && reached != NULL;
    if (valid)
    {
        reached[0] = 1;
        bool settled = false;
        for (uint64_t round = 0; valid && round <= (uint64_t)count * 2; round += 1)
        {
            bool changed = false;
            for (uint32_t i = 0; valid && i < count; i += 1)
            {
                uint8_t state = reached[i];
                if (state == 0)
                {
                    continue;
                }
                if (i == address)
                {
                    valid = (state & 2) == 0;
                    state = 2;
                }
                if (i == dispatch)
                {
                    valid = valid && (state & 1) == 0;
                }
                if (!valid)
                {
                    break;
                }
                const X86Instruction* instruction = &records[i].instruction;
                const uint32_t form = instruction->form & BUSTER_X86_FORM_NUMBER;
                const uint32_t control = (uint32_t)x86_effects(form)->kinds >> 4;
                const bool relative = x86_form_descriptions[form].relative_bytes != 0;
                if (relative)
                {
                    const int64_t goal = (int64_t)records[i].offset + records[i].length + instruction->displacement;
                    const uint32_t target = check_test_tier1_table_index(records, count, goal);
                    const uint8_t prior = reached[target];
                    reached[target] |= state;
                    changed |= prior != reached[target];
                }
                else if (control == BUSTER_X86_CONTROL_JUMP)
                {
                    for (uint32_t t = 0; t < 4; t += 1)
                    {
                        const uint32_t target = check_test_tier1_table_index(records, count, targets[t]);
                        const uint8_t prior = reached[target];
                        reached[target] |= state;
                        changed |= prior != reached[target];
                    }
                }
                if (control == BUSTER_X86_CONTROL_NONE || control == BUSTER_X86_CONTROL_BRANCH)
                {
                    valid = i + 1 < count;
                    if (valid)
                    {
                        const uint8_t prior = reached[i + 1];
                        reached[i + 1] |= state;
                        changed |= prior != reached[i + 1];
                    }
                }
            }
            if (valid && !changed)
            {
                settled = true;
                break;
            }
        }
        valid = valid && settled && (reached[address] & 1) != 0 && (reached[dispatch] & 2) != 0;
    }
    free(reached);
    return valid && addresses == 1 && jumps == 1;
}

// Mutations of the decoded object and one owned in-memory ELF copy make the refusal conditions executable. The
// indirect control changes table entry 1, so omitting its nonzero slot offset from S + A - slot cannot pass it.
static void check_test_tier1_table_entry_controls(CheckTest* test, const char* object, const X86Decoded* records, uint32_t count, uint32_t address, uint32_t dispatch,
                                                 const uint32_t* targets)
{
    X86Decoded* probe = malloc((uint64_t)count * sizeof(X86Decoded));
    uint32_t branch = count;
    for (uint32_t i = address + 1; i < count && branch == count; i += 1)
    {
        const uint32_t form = records[i].instruction.form & BUSTER_X86_FORM_NUMBER;
        const uint32_t control = (uint32_t)x86_effects(form)->kinds >> 4;
        branch = x86_form_descriptions[form].relative_bytes != 0 && (control == BUSTER_X86_CONTROL_JUMP || control == BUSTER_X86_CONTROL_BRANCH) ? i : branch;
    }
    const bool ready = probe != NULL && address + 1 < count && branch < count && dispatch < count && records[address].length >= 4;
    check_test_expect(test, ready, "the table entry proof's decoded controls have room and a direct branch");
    if (ready)
    {
        check_test_expect(test, check_test_tier1_table_entry(records, count, address, dispatch, targets),
                          "the table base's entry prefix accepts the actual reordered object");
        memcpy(probe, records, (uint64_t)count * sizeof(X86Decoded));
        probe[address + 1].instruction = records[address].instruction;
        check_test_expect(test, !check_test_tier1_table_entry(probe, count, address, dispatch, targets), "the table entry proof rejects a repeated table LEA");
        memcpy(probe, records, (uint64_t)count * sizeof(X86Decoded));
        probe[address + 1].instruction = records[address].instruction;
        probe[address].instruction = records[branch].instruction;
        probe[address].instruction.displacement = (int64_t)records[dispatch].offset - records[address].offset - records[address].length;
        check_test_expect(test, !check_test_tier1_table_entry(probe, count, address + 1, dispatch, targets),
                          "the table entry proof rejects a direct branch that can bypass its LEA into dispatch");
        memcpy(probe, records, (uint64_t)count * sizeof(X86Decoded));
        probe[branch].instruction.displacement = (int64_t)records[address].offset - records[branch].offset - records[branch].length;
        check_test_expect(test, !check_test_tier1_table_entry(probe, count, address, dispatch, targets), "the table entry proof rejects direct reentry into its LEA");
        uint32_t reentry[4] = { 0 };
        const uint32_t field = records[address].offset + records[address].length - 4;
        const bool resolved = check_test_tier1_table_targets(object, "tier1_table_loop", records[address].offset, field, reentry, true);
        check_test_expect(test, resolved && reentry[1] == records[address].offset && !check_test_tier1_table_entry(records, count, address, dispatch, reentry),
                          "the table entry proof rejects indirect reentry from the second PC32 table entry");
        check_test_expect(test, !check_test_tier1_table_targets(object, "tier1_table_loop", records[address].offset, field + 1, reentry, false),
                          "the table's sole PC32 relocation is bound to the LEA displacement field");
    }
    free(probe);
}
