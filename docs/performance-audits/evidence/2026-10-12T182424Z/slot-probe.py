f='/home/user/buster/src/buster/lib/compiler/codegen/register_allocator_fast.c'
s=open(f).read()
anchor='        u32 shareable_slots = 0;\n        for (u32 slot = 0; coalesce_slots && slot < function->stack_slot_count; slot += 1)\n'
assert s.count(anchor)==1
probe='''        if (coalesce_slots)
        {
            static FILE* slot_probe_file;
            static int slot_probe_opened;
            if (!slot_probe_opened) { slot_probe_opened = 1; char const* path = getenv("SLOT_PROBE"); slot_probe_file = path ? fopen(path, "w") : 0; }
            if (slot_probe_file)
            {
                u32 counts[5] = {0};
                for (u32 slot = 0; slot < function->stack_slot_count; slot += 1)
                {
                    bool is_static = (function->stack_slot_memory_flags && function->stack_slot_memory_flags[slot] != MACHINE_STACK_SLOT_MEMORY_NONVOLATILE) ||
                                     (function->outgoing_bytes && slot == function->outgoing_slot);
                    for (u32 i = 0; i < function->inline_assembly_operand_count; i += 1) is_static |= function->inline_assembly_operands[i].stack_slot == slot;
                    for (u32 i = 0; i < function->va_arg_count; i += 1) is_static |= function->va_args[i].result_is_frame && function->va_args[i].result_slot == slot;
                    bool addr_unsafe = slot_addressed[slot] && !address_safe[slot];
                    u32 category = is_static ? 0u : addr_unsafe ? 1u : slot_fixed[slot] ? 2u : slot_starts[slot] == UINT32_MAX ? 3u : 4u;
                    counts[category] += 1;
                }
                fprintf(slot_probe_file, "S rows=%u slots=%u static=%u addr_unsafe=%u unproven=%u untouched=%u shareable=%u\\n", function->instruction_count,
                        function->stack_slot_count, counts[0], counts[1], counts[2], counts[3], counts[4]);
            }
        }
'''
s=s.replace(anchor, probe+anchor)
s=s.replace('#include <buster/lib/os.h>\n','#include <buster/lib/os.h>\n#include <stdio.h>\n#include <stdlib.h>\n',1)
open(f,'w').write(s)
