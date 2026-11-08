// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// The call-free labels fixture's table base must execute once, even when block placement puts its dispatch after
// its last backward direct branch. Include after c_check_tier1_table_targets_test.c. This bounded object oracle
// checks an entry prefix and every direct and four table targets, rather than inferring a loop from physical order.

static bool check_test_tier1_table_boundary(const X86Decoded* records, uint32_t count, int64_t goal, uint32_t address)
{
    uint32_t low = 0;
    uint32_t high = count;
    while (low < high)
    {
        const uint32_t middle = low + (high - low) / 2;
        low = (int64_t)records[middle].offset < goal ? middle + 1 : low;
        high = (int64_t)records[middle].offset < goal ? high : middle;
    }
    return low < count && low > address && (int64_t)records[low].offset == goal;
}

// Entry reaches the sole RIP-relative LEA without a control transfer, and no direct or indirect edge can reenter
// that prefix. Calls and unknown control are refused: tier1_table_loop has neither. The separate dispatch check
// still owns the exact MOVSXD/ADD/JMP register relationship, and the ELF helper owns its sole PC32 association.
static bool check_test_tier1_table_entry(const X86Decoded* records, uint32_t count, uint32_t address, uint32_t dispatch, const uint32_t* targets)
{
    bool valid = address < count && dispatch < count;
    uint32_t addresses = 0;
    uint32_t jumps = 0;
    for (uint32_t i = 0; valid && i < count; i += 1)
    {
        const X86Instruction* instruction = &records[i].instruction;
        const uint32_t form = instruction->form & BUSTER_X86_FORM_NUMBER;
        const uint32_t control = (uint32_t)x86_effects(form)->kinds >> 4;
        const bool table_address = form == X86_FORM_LEA_R64_M && instruction->mode == BUSTER_X86_MEMORY_RIP;
        const bool indirect = form == X86_FORM_JMP_RM64 && instruction->mode == BUSTER_X86_REGISTER_DIRECT;
        addresses += table_address;
        jumps += indirect;
        valid = (!table_address || i == address) && (i >= address || control == BUSTER_X86_CONTROL_NONE) &&
                control != BUSTER_X86_CONTROL_CALL && control != BUSTER_X86_CONTROL_TRAP && control != BUSTER_X86_CONTROL_UNKNOWN;
        if (valid && x86_form_descriptions[form].relative_bytes != 0)
        {
            const int64_t goal = (int64_t)records[i].offset + records[i].length + instruction->displacement;
            valid = (control == BUSTER_X86_CONTROL_JUMP || control == BUSTER_X86_CONTROL_BRANCH) &&
                    check_test_tier1_table_boundary(records, count, goal, address);
        }
        else if (valid && (control == BUSTER_X86_CONTROL_JUMP || control == BUSTER_X86_CONTROL_BRANCH))
        {
            valid = indirect && i == dispatch;
        }
    }
    for (uint32_t t = 0; valid && t < 4; t += 1)
    {
        valid = check_test_tier1_table_boundary(records, count, targets[t], address);
    }
    return valid && addresses == 1 && jumps == 1;
}

// Mutations of the decoded object and one owned in-memory ELF copy make the refusal conditions executable. The
// indirect control changes table entry 1, so omitting its nonzero slot offset from S + A - slot cannot pass it.
static void check_test_tier1_table_entry_controls(CheckTest* test, const char* object, const X86Decoded* records, uint32_t count, uint32_t address, uint32_t dispatch,
                                                 const uint32_t* targets)
{
    X86Decoded* probe = malloc((uint64_t)count * sizeof(X86Decoded));
    uint32_t branch = count;
    for (uint32_t i = 0; i < count && branch == count; i += 1)
    {
        const uint32_t form = records[i].instruction.form & BUSTER_X86_FORM_NUMBER;
        const uint32_t control = (uint32_t)x86_effects(form)->kinds >> 4;
        branch = x86_form_descriptions[form].relative_bytes != 0 && (control == BUSTER_X86_CONTROL_JUMP || control == BUSTER_X86_CONTROL_BRANCH) ? i : branch;
    }
    const bool ready = probe != NULL && address + 1 < count && branch < count && records[address].length >= 4;
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
        check_test_expect(test, !check_test_tier1_table_entry(probe, count, address + 1, dispatch, targets), "the table entry proof rejects a LEA after an initial branch");
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
