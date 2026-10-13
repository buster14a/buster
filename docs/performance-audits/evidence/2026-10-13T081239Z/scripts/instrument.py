from pathlib import Path
root = Path(__file__).resolve().parent
p = root / 'buster/src/buster/lib/compiler/codegen/register_allocator_fast.c'
s = (root / 'register_allocator_fast.base.c').read_text()
def change(a,b):
    global s
    assert s.count(a)==1, (a[:100],s.count(a))
    s=s.replace(a,b)
change('struct MachineFastState\n{', 'static u32 diag_sequence;\nstruct MachineFastState\n{\n    u32 diag_id;\n    u32 diag_destination;\n    u32 diag_loss_kind;')
change('    state->owner[physical_register] = UINT32_MAX;\n    state->held_mask &=', '    fprintf(stderr, "R X %u %u %u %u\\n", state->diag_id, state->current_point, owner, (machine_instruction_opcode_row(state->function, state->function->instructions + (state->current_point >> 2)).flags & MACHINE_OPCODE_ROW_CALL) ? 2u : state->diag_loss_kind);\n    state->owner[physical_register] = UINT32_MAX;\n    state->held_mask &=')
change('            machine_fast_spill(state, best);', '            state->diag_loss_kind = 3;\n            machine_fast_spill(state, best);\n            state->diag_loss_kind = 4;')
change('    MachineEdit* edit = (MachineEdit*)machine_stream_append(state->arena, stream);\n    *edit', '    if (kind == MACHINE_EDIT_SPILL || kind == MACHINE_EDIT_RELOAD) fprintf(stderr, "R B %u %u %u %u %u\\n", state->diag_id, point, kind == MACHINE_EDIT_SPILL ? 1u : 2u, subject, state->diag_destination);\n    MachineEdit* edit = (MachineEdit*)machine_stream_append(state->arena, stream);\n    *edit')
for signature in ['    state->virtual_register_locations[virtual_register] = target;\n    state->age', '    state->virtual_register_locations[virtual_register] = target;\n    state->preferred_registers']:
    change(signature, '    fprintf(stderr, "R L %u %u %u\\n", state->diag_id, state->current_point, virtual_register);\n'+signature)
change('        u32 const* predecessor_offsets = prepass->predecessor_offsets;', '''        state.diag_id = ++diag_sequence;
        state.diag_loss_kind = 4;
        fprintf(stderr, "R A %u %u %u\\n", state.diag_id, function->block_count, function->virtual_register_count);
        for (u32 b = 0; b < function->block_count; ++b)
        {
            fprintf(stderr, "R P %u %u %u %u\\n", state.diag_id, b, function->blocks[b].first_instruction, function->blocks[b].instruction_count);
            for (u32 e = prepass->predecessor_offsets[b]; e < prepass->predecessor_offsets[b+1]; ++e)
                fprintf(stderr, "R E %u %u %u\\n", state.diag_id, prepass->predecessor_list[e], b);
        }
        for (u32 v = 0; v < function->virtual_register_count; ++v)
            if (prepass->rematerialize_immediates[v] == UINT32_MAX)
                fprintf(stderr, "R V %u %u %u\\n", state.diag_id, v, prepass->definition_blocks[v]);
        u32 const* predecessor_offsets = prepass->predecessor_offsets;''')
change('            contract_held[block_index] = entry_held;', '''            for (u64 remaining = entry_held; remaining; remaining &= remaining - 1u)
                fprintf(stderr, "R I %u %u %u\\n", state.diag_id, block_index, entry_owner[machine_fast_first_set(remaining)]);
            contract_held[block_index] = entry_held;''')
change('                    machine_fast_conform_edge_parameters(&state, &retro_edits,', '                    state.diag_destination = block_index;\n                    machine_fast_conform_edge_parameters(&state, &retro_edits,')
change('                if (is_terminator)\n                {\n                    // Successors', '                if (is_terminator)\n                {\n                    state.diag_destination = UINT32_MAX;\n                    // Successors')
change('                                machine_fast_conform_edge_parameters(&state, &edits,', '                                state.diag_destination = successor;\n                                machine_fast_conform_edge_parameters(&state, &edits,')
change('                            else if (cold_blocks[successor])\n                            {', '                            else if (cold_blocks[successor])\n                            {\n                                state.diag_destination = successor;')
change('                        recorded_owner[physical_register] = state.owner[physical_register];', '                        recorded_owner[physical_register] = state.owner[physical_register];\n                        fprintf(stderr, "R O %u %u %u\\n", state.diag_id, block_index, state.owner[physical_register]);')
change('    return placement;\n}', '    fprintf(stderr, "R Z %u %p %u %u\\n", diag_sequence, (void*)placement.edits, placement.boundary_spill_count, placement.boundary_reload_count);\n    return placement;\n}')
change('        owner[physical] = UINT32_MAX;', '        fprintf(stderr, "R X %u %u %u 5\\n", state->diag_id, point, previous);\n        owner[physical] = UINT32_MAX;')
change('                owner[contract_register] = UINT32_MAX;', '                fprintf(stderr, "R X %u %u %u 6\\n", state->diag_id, point, resident);\n                owner[contract_register] = UINT32_MAX;')
change('            owner[broken] = UINT32_MAX;', '            fprintf(stderr, "R X %u %u %u 6\\n", state->diag_id, point, resident);\n            owner[broken] = UINT32_MAX;')
change('            owner[contract_register] = value;', '            fprintf(stderr, "R L %u %u %u\\n", state->diag_id, point, value);\n            owner[contract_register] = value;')
change('            state->virtual_register_locations[source] = UINT32_MAX;', '            fprintf(stderr, "R X %u %u %u 7\\n", state->diag_id, state->current_point, source);\n            state->virtual_register_locations[source] = UINT32_MAX;')
a='state.virtual_register_locations[dying] = UINT32_MAX;'
assert s.count(a)==2
s=s.replace(a, 'fprintf(stderr, "R X %u %u %u 7\\n", state.diag_id, state.current_point, dying);\n                        '+a)
p.write_text(s)
p=root/'buster/src/buster/lib/compiler/codegen/codegen.c'
s=(root/'codegen.base.c').read_text()
a='                    WORK_LEDGER_RECORD(MACHINE_PLACEMENT_EDITS, placement.edit_count);'
assert s.count(a)==1
s=s.replace(a,a+'\n                    fprintf(stderr, "R F %p\\n", (void*)placement.edits);')
p.write_text(s)

p=root/'buster/src/buster/lib/compiler/codegen/register_allocator_predicate.c'
s=(root/'register_allocator_predicate.base.c').read_text()
a='        placement.edits = merged;'
assert s.count(a)==1
s=s.replace(a, '        fprintf(stderr, "R T %p %p\\n", (void*)placement.edits, (void*)merged);\n'+a)
p.write_text(s)
