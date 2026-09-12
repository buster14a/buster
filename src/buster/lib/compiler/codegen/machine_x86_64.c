    return selected;
}

// A GNU compiler barrier has no architectural instruction. Its machine row
// exists solely so scheduling cannot move memory operations across it.
BUSTER_GLOBAL_LOCAL bool machine_x64_select_compiler_barrier(MachineX64Selector* selector, IrInstruction* instruction)
{
    IrInstructionExtra extra = ir_instruction_extra(selector->function, ir_instruction_self_id(selector->function, instruction));
    bool selected = !extra.literal.length && !instruction->operand_count && !instruction->target_count && extra.clobber_count == 1 &&
                    string_equal(extra.clobbers[0], S8("memory"));
    if (selected)
    {
        machine_x64_select_row(selector, (MachineInstruction){.opcode = MACHINE_X64_COMPILER_BARRIER});
    }
    return selected;
}

// These fixed literal forms have more architectural results than the hot
