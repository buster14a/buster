
#include <stdio.h>
int main(void)
{
    unsigned tagged = 0, effect_rows = 0, attribute_only = 0;
    for (u32 opcode = 0; opcode < MACHINE_OPCODE_COUNT; opcode += 1)
    {
        MachineOpcodeInfo const* info = machine_opcode_infos + opcode;
        bool bit = (info->attributes & (1u << 7)) != 0;
        bool effect = info->memory_effect > MACHINE_MEMORY_EFFECT_NONE && info->memory_effect < MACHINE_MEMORY_EFFECT_COUNT;
        tagged += bit; effect_rows += effect; attribute_only += bit && !effect;
        printf("%u %u %u %u\n", opcode, bit, info->memory_effect, bit || effect);
    }
    fprintf(stderr, "opcodes=%u tagged=%u effect_rows=%u attribute_only=%u\n", MACHINE_OPCODE_COUNT, tagged, effect_rows, attribute_only);
    return attribute_only != 0;
}
