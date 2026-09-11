#include <stdio.h>
int main(void)
{
    for (unsigned opcode = 0; opcode < MACHINE_OPCODE_COUNT; opcode += 1)
    {
        MachineOpcodeInfo const* p = machine_opcode_infos + opcode;
        unsigned attributes = p->attributes & ((1u << 0) | (1u << 1) | (1u << 2) | (1u << 4) | (1u << 5) | (1u << 6));
        printf("%u,%llu,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u\n", opcode, (unsigned long long)p->clobber_mask,
            attributes,p->operand_count,p->operand_info[0],p->operand_info[1],p->operand_info[2],p->operand_info[3],
            p->tied_pair,p->early_clobber_mask,p->fixed_register_mask,p->fixed_registers[0],p->fixed_registers[1],p->fixed_registers[2],p->fixed_registers[3],p->memory_effect,
            (p->schedule_class == MACHINE_SCHEDULE_CLASS_BARRIER) | ((((p->implicit_resource_uses | p->implicit_resource_defs) & MACHINE_RESOURCE_VECTOR_STATE_MASK) != 0) << 1));
    }
    return 0;
}
