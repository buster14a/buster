    return true;
}

// A GNU compiler barrier has no architectural instruction. Its machine row
// exists solely so scheduling cannot move memory operations across it.
BUSTER_GLOBAL_LOCAL bool machine_a64_select_compiler_barrier(MachineA64Selector* selector, IrInstruction* instruction)
{
    IrInstructionExtra extra = ir_instruction_extra(selector->function, ir_instruction_self_id(selector->function, instruction));
    bool selected = !extra.literal.length && !instruction->operand_count && !instruction->target_count && extra.clobber_count == 1 &&
                    string_equal(extra.clobbers[0], S8("memory"));
    if (selected)
    {
        machine_a64_select_row(selector, (MachineInstruction){.opcode = MACHINE_A64_COMPILER_BARRIER});
    }
    return selected;
}

BUSTER_GLOBAL_LOCAL u32 machine_a64_block_entry(MachineA64Selector* selector, u32 canonical_block)
