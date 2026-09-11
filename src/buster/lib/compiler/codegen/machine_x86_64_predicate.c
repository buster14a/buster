// Predicate selection finishes the existing native MIR stream, preserving its
// CFG and source marks. It discovers compare/copy/bitwise predicate values and
// inserts integer bridges only where an integer, storage, call, or edge user
// actually requires them. This file is included by machine_x86_64.c.

BUSTER_GLOBAL_LOCAL u32 machine_x64_predicate_input_slot(u16 opcode)
{
    u32 slot = UINT32_MAX;
    switch (opcode)
    {
        case MACHINE_X64_VLOAD_PTR_MASKED: slot = 2; break;
        case MACHINE_X64_VSTORE_PTR_MASKED:
        case MACHINE_X64_VCOMPRESS_STORE_PTR:
        case MACHINE_X64_VPERMT2B:
        case MACHINE_X64_VCOMPRESSB: slot = 1; break;
        default: break;
    }
    return slot;
}

BUSTER_GLOBAL_LOCAL void machine_x64_select_predicates(Arena* arena, MachineFunction* function)
{
    u32 old_count = function->virtual_register_count;
    u32 row_count = function->instruction_count;
    u32* predicate = arena_allocate(arena, u32, old_count);
    u8* width = arena_allocate_zeroed(arena, u8, old_count);
    u8* integer_use = arena_allocate_zeroed(arena, u8, old_count);
    u8* predicate_row = arena_allocate_zeroed(arena, u8, row_count);
    memset(predicate, 0xff, (u64)old_count * sizeof(u32));
    MachineVirtualRegister* values = arena_allocate(arena, MachineVirtualRegister, old_count + row_count * 2u);
    memcpy(values, function->virtual_registers, (u64)old_count * sizeof(MachineVirtualRegister));
    u32 value_count = old_count;
    for (u32 row = 0; row < row_count; row += 1)
    {
        MachineInstruction const* instruction = function->instructions + row;
        bool produces = instruction->opcode == MACHINE_X64_VPCMP_MASK || instruction->opcode == MACHINE_X64_VPMOVB2M;
        bool copy = instruction->opcode == MACHINE_X64_MOV_RR;
        bool logic = instruction->opcode == MACHINE_X64_AND64 || instruction->opcode == MACHINE_X64_OR64 || instruction->opcode == MACHINE_X64_XOR64;
        bool all_predicates = copy || logic;
        for (u32 slot = 1; all_predicates && slot < (copy ? 2u : 3u); slot += 1)
        {
            MachineRef source = instruction->operands[slot];
            all_predicates = machine_ref_kind(source) == MACHINE_REF_VIRTUAL_REGISTER && predicate[machine_ref_payload(source)] != UINT32_MAX;
        }
        MachineRef destination = instruction->operands[0];
        bool ordinary_destination = machine_ref_kind(destination) == MACHINE_REF_VIRTUAL_REGISTER &&
            !(values[machine_ref_payload(destination)].flags & MACHINE_VIRTUAL_REGISTER_FLAG_MUTABLE);
        if (ordinary_destination && (produces || all_predicates))
        {
            u32 original = machine_ref_payload(destination);
            predicate[original] = value_count;
            values[value_count] = values[original];
            values[value_count].register_class = MACHINE_REGISTER_CLASS_MASK;
            values[value_count].definition_point = MACHINE_POINT_INVALID;
            value_count += 1;
            width[original] = instruction->opcode == MACHINE_X64_VPCMP_MASK && instruction->payload >= 3 ? 16 :
                copy ? width[machine_ref_payload(instruction->operands[1])] : 64;
            predicate_row[row] = 1;
        }
    }
    // Any non-predicate use demands exactly one integer representation at its
    // defining row. Escaping predicates remain SSA; no lazy bridge can end up
    // in a sibling block that fails to dominate another integer use.
    for (u32 row = 0; row < row_count; row += 1)
    {
        MachineInstruction const* instruction = function->instructions + row;
        MachineOpcodeInfo const* info = machine_opcode_info(instruction->opcode);
        u32 mask_slot = machine_x64_predicate_input_slot(instruction->opcode);
        for (u32 slot = 0; slot < info->operand_count; slot += 1)
        {
            MachineRef ref = instruction->operands[slot];
            if ((info->operand_info[slot] & MACHINE_OPERAND_ROLE_USE) && machine_ref_kind(ref) == MACHINE_REF_VIRTUAL_REGISTER &&
                slot != mask_slot && !(predicate_row[row] && slot > 0)) integer_use[machine_ref_payload(ref)] = 1;
        }
    }
    for (u32 index = 0; index < function->edge_copy_source_count; index += 1)
    {
        MachineRef ref = function->edge_copy_sources[index];
        if (machine_ref_kind(ref) == MACHINE_REF_VIRTUAL_REGISTER) integer_use[machine_ref_payload(ref)] = 1;
    }
    MachineInstruction* instructions = arena_allocate(arena, MachineInstruction, row_count * 2u);
    u32* first_rows = arena_allocate(arena, u32, row_count + 1u);
    u32* core_rows = arena_allocate(arena, u32, row_count);
    u32 count = 0;
    for (u32 row = 0; row < row_count; row += 1)
    {
        MachineInstruction instruction = function->instructions[row];
        u32 mask_slot = machine_x64_predicate_input_slot(instruction.opcode);
        first_rows[row] = count;
        if (mask_slot != UINT32_MAX)
        {
            MachineRef ref = instruction.operands[mask_slot];
            u32 mask = machine_ref_kind(ref) == MACHINE_REF_VIRTUAL_REGISTER ? predicate[machine_ref_payload(ref)] : UINT32_MAX;
            if (mask == UINT32_MAX)
            {
                mask = value_count++;
                values[mask] = (MachineVirtualRegister){.register_class = MACHINE_REGISTER_CLASS_MASK, .typed_origin = IR_ID_UNDERLYING_INVALID,
                    .definition_point = machine_point_make(count, MACHINE_POINT_AFTER)};
                instructions[count++] = (MachineInstruction){.opcode = MACHINE_X64_KMOV_FROM_GENERAL, .payload = 64,
                    .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, mask), ref}};
            }
            instruction.operands[mask_slot] = machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, mask);
            switch (instruction.opcode)
            {
                case MACHINE_X64_VLOAD_PTR_MASKED: instruction.opcode = MACHINE_X64_VLOAD_PTR_K; break;
                case MACHINE_X64_VSTORE_PTR_MASKED: instruction.opcode = MACHINE_X64_VSTORE_PTR_K; break;
                case MACHINE_X64_VCOMPRESS_STORE_PTR: instruction.opcode = MACHINE_X64_VCOMPRESS_STORE_PTR_K; break;
                case MACHINE_X64_VPERMT2B: instruction.opcode = MACHINE_X64_VPERMT2B_K; break;
                case MACHINE_X64_VCOMPRESSB: instruction.opcode = MACHINE_X64_VCOMPRESSB_K; break;
                default: break;
            }
        }
        u32 original = UINT32_MAX;
        if (predicate_row[row])
        {
            original = machine_ref_payload(instruction.operands[0]);
            instruction.operands[0] = machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, predicate[original]);
            if (instruction.opcode == MACHINE_X64_VPCMP_MASK) instruction.opcode = MACHINE_X64_VPCMP_K;
            else if (instruction.opcode == MACHINE_X64_VPMOVB2M) instruction.opcode = MACHINE_X64_VPMOVB2K;
            else
            {
                bool copy = instruction.opcode == MACHINE_X64_MOV_RR;
                for (u32 slot = 1; slot < (copy ? 2u : 3u); slot += 1)
                    instruction.operands[slot] = machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, predicate[machine_ref_payload(instruction.operands[slot])]);
                instruction.opcode = copy ? MACHINE_X64_KMOV : instruction.opcode == MACHINE_X64_AND64 ? MACHINE_X64_KAND :
                    instruction.opcode == MACHINE_X64_OR64 ? MACHINE_X64_KOR : MACHINE_X64_KXOR;
                instruction.payload = copy ? width[original] : 64;
            }
            values[predicate[original]].definition_point = machine_point_make(count, MACHINE_POINT_AFTER);
        }
        core_rows[row] = count;
        instructions[count++] = instruction;
        if (original != UINT32_MAX && integer_use[original])
        {
            values[original].definition_point = machine_point_make(count, MACHINE_POINT_AFTER);
            instructions[count++] = (MachineInstruction){.opcode = MACHINE_X64_KMOV_TO_GENERAL, .payload = width[original],
                .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, original), machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, predicate[original])}};
        }
    }
    first_rows[row_count] = count;
    for (u32 value = 0; value < old_count; value += 1)
    {
        MachinePoint point = values[value].definition_point;
        if (point != MACHINE_POINT_INVALID && predicate[value] == UINT32_MAX)
            values[value].definition_point = machine_point_make(core_rows[machine_point_instruction(point)], machine_point_phase(point));
    }
    for (u32 block = 0; block < function->block_count; block += 1)
    {
        MachineBlock* info = function->blocks + block;
        u32 end = info->first_instruction + info->instruction_count;
        info->first_instruction = first_rows[info->first_instruction];
        info->instruction_count = first_rows[end] - info->first_instruction;
    }
    for (u32 mark = 0; mark < function->line_mark_count; mark += 1) function->line_marks[mark].row = first_rows[function->line_marks[mark].row];
    function->instructions = instructions;
    function->instruction_count = count;
    function->virtual_registers = values;
    function->virtual_register_count = value_count;
}
