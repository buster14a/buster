        .clobber_mask = (1u << MACHINE_X64_RAX) | (1u << MACHINE_X64_RDX),
        .fixed_register_mask = 0x1, .fixed_registers = {MACHINE_X64_RCX},
    },
    [MACHINE_X64_COMPILER_BARRIER] = {
        .attributes = MACHINE_OPCODE_ATTRIBUTE_SIDE_EFFECTS,
        .memory_effect = MACHINE_MEMORY_EFFECT_BARRIER,
    },
    [MACHINE_X64_SHL32] = MACHINE_INFO_SHIFT(),
    [MACHINE_X64_SHL64] = MACHINE_INFO_SHIFT(),
    [MACHINE_X64_SAR32] = MACHINE_INFO_SHIFT(),
    [MACHINE_A64_ATOMIC_FENCE] = {
        .attributes = MACHINE_OPCODE_ATTRIBUTE_SIDE_EFFECTS,
    },
    [MACHINE_A64_COMPILER_BARRIER] = {
        .attributes = MACHINE_OPCODE_ATTRIBUTE_SIDE_EFFECTS,
        .memory_effect = MACHINE_MEMORY_EFFECT_BARRIER,
    },
    [MACHINE_A64_LEA_TLS] = {
        .operand_count = 1,
        .operand_info = {MACHINE_OPERAND_DEFINE_GENERAL},
    },
