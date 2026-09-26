#include <buster/lib/compiler/codegen/machine_select.h>
#include <buster/lib/compiler/codegen/machine.h>
#include <buster/lib/compiler/codegen/machine_schedule_internal.h>
#include <buster/lib/integer.h>

// Shared selector storage and validation; target opcode switches own lowering.
// machine_selection_value_facts_allocate supplies the AArch64 row walk,
// machine_type_classes_build projects the shared module type table, and
// machine_selection_validate_function guards the unvalidated public entry.
// machine_selection_certify_stack_memory publishes optional frame-object
// provenance before the selector's canonical-to-machine row spans are remapped.
// machine_selection_address memoizes only demanded canonical address chains;
// both native selectors consume its field/index shapes and normalized bases.
// machine_selection_is_compiler_barrier recognizes operand-free empty assembly;
// target selectors publish its ordering effects through their barrier rows.
// machine_selection_is_operand_free_assembly shares its clobber contract with
// literal hint selectors, while each target owns the admitted spellings.
// machine_selection_assembly_identity_plan binds empty generic-register
// outputs to their read/write value or matching input before target lowering.
// machine_selection_assembly_label_plan replaces asm-goto label operands with
// private assembler symbols and retains their canonical target indices.
// machine_selection_assembly_label_reference_capacity gives block expansion a
// parse-independent upper bound for per-reference landing continuations.
// machine_selection_finish_canonical_edges prunes unexecuted assembly targets
// and remaps canonical edges after target-specific block expansion.

bool machine_selection_is_operand_free_assembly(IrFunction* function, IrInstruction* instruction)
{
    bool selected = instruction->opcode == IR_OPCODE_INLINE_ASSEMBLY && !instruction->operand_count &&
                    !instruction->immediate_count && !instruction->target_count;
    if (selected)
    {
        IrInstructionExtra extra = ir_instruction_extra(function, ir_instruction_self_id(function, instruction));
        selected = !extra.clobber_count || extra.clobbers;
        for (u32 index = 0; selected && index < extra.clobber_count; index += 1)
        {
            selected = string_equal(extra.clobbers[index], S8("memory")) || string_equal(extra.clobbers[index], S8("cc"));
        }
    }
    return selected;
}

bool machine_selection_is_compiler_barrier(IrFunction* function, IrInstruction* instruction)
{
    IrInstructionExtra extra = ir_instruction_extra(function, ir_instruction_self_id(function, instruction));
    return !extra.literal.length && machine_selection_is_operand_free_assembly(function, instruction);
}

bool machine_selection_assembly_label_reference_capacity(String8 literal, u32* capacity_out)
{
    bool valid = capacity_out && (!literal.length || literal.pointer);
    u32 capacity = 0;
    for (u64 index = 0; valid && index < literal.length; index += 1)
    {
        if (literal.pointer[index] != '%' || index + 1 >= literal.length)
        {
            continue;
        }
        if (literal.pointer[index + 1] == '%')
        {
            index += 1;
        }
        else if (literal.pointer[index + 1] == 'l')
        {
            valid = capacity != UINT32_MAX;
            capacity += valid ? 1u : 0u;
        }
    }
    if (valid)
    {
        *capacity_out = capacity;
    }
    return valid;
}

bool machine_selection_assembly_identity_plan(IrProgram* program, IrFunction* function, IrInstruction* instruction,
                                              String8 jump_prefix, MachineAssemblyIdentityPlan* plan)
{
    IrInstructionExtra extra = ir_instruction_extra(function, ir_instruction_self_id(function, instruction));
    *plan = (MachineAssemblyIdentityPlan){.target_index = UINT32_MAX};
    bool selected = instruction->opcode == IR_OPCODE_INLINE_ASSEMBLY &&
                    (instruction->operand_count || instruction->target_count) &&
                    instruction->operand_count <= MACHINE_ASSEMBLY_IDENTITY_MAX_OPERANDS &&
                    instruction->operand_count == instruction->immediate_count;
    if (selected && instruction->target_count)
    {
        plan->target_index = 0;
        selected = jump_prefix.length && (!extra.literal.length ||
                   ir_inline_assembly_jump_target(function, instruction, extra.literal, jump_prefix, &plan->target_index));
    }
    else
    {
        selected = selected && !extra.literal.length;
    }
    for (u32 index = 0; selected && index < extra.clobber_count; index += 1)
    {
        selected = string_equal(extra.clobbers[index], S8("memory")) || string_equal(extra.clobbers[index], S8("cc"));
    }
    for (u32 index = 0; selected && index < instruction->operand_count; index += 1)
    {
        IrValueId operand = instruction->operands[index];
        u64 constraint = instruction->immediates[index];
        selected = operand.value < function->value_count &&
                   (constraint & IR_INLINE_ASSEMBLY_CONSTRAINT_CLASS_MASK) == IR_INLINE_ASSEMBLY_CONSTRAINT_R &&
                   !IR_INLINE_ASSEMBLY_CONSTRAINT_HAS_PHYSICAL_REGISTER(constraint) &&
                   !(constraint & ~IR_INLINE_ASSEMBLY_CONSTRAINT_KNOWN_MASK);
        IrType* type = selected ? ir_type_from_id(&program->types, function->values[operand.value].canonical_type) : 0;
        selected = selected && type && type->layout.resolved &&
                   (type->kind == IR_TYPE_INTEGER || type->kind == IR_TYPE_POINTER || type->kind == IR_TYPE_BOOLEAN) &&
                   (type->layout.size == 1 || type->layout.size == 2 || type->layout.size == 4 || type->layout.size == 8);
        if (selected)
        {
            bool output = (constraint & IR_INLINE_ASSEMBLY_CONSTRAINT_OUTPUT) != 0;
            bool read_write = (constraint & IR_INLINE_ASSEMBLY_CONSTRAINT_READ_WRITE) != 0;
            bool matching = (constraint & IR_INLINE_ASSEMBLY_CONSTRAINT_MATCH) != 0;
            plan->sizes[index] = (u8)type->layout.size;
            plan->source_operands[index] = (u8)(output && !read_write ? UINT8_MAX : index);
            plan->outputs |= (u16)((u32)output << index);
            plan->read_write |= (u16)((u32)read_write << index);
            plan->matching_inputs |= (u16)((u32)matching << index);
            selected = (!read_write || output) && (!matching || !output);
            if (selected && matching)
            {
                u32 match = IR_INLINE_ASSEMBLY_CONSTRAINT_MATCH_INDEX(constraint);
                selected = match < index && (((u32)plan->outputs >> match) & 1u) && !(((u32)plan->read_write >> match) & 1u) &&
                           plan->source_operands[match] == UINT8_MAX && plan->sizes[match] == type->layout.size;
                if (selected)
                {
                    IrType* output_type = ir_type_from_id(&program->types, function->values[instruction->operands[match].value].canonical_type);
                    selected = output_type && output_type->kind == type->kind;
                    plan->source_operands[match] = (u8)index;
                }
            }
        }
    }
    for (u32 index = 0; selected && index < instruction->operand_count; index += 1)
    {
        selected = plan->source_operands[index] != UINT8_MAX;
    }
    return selected;
}

BUSTER_GLOBAL_LOCAL bool machine_selection_assembly_label_reference(IrInstruction* instruction, IrInstructionExtra extra,
                                                                     String8 source, u64 start, u32* target_out, u64* end_out)
{
    bool valid = instruction && target_out && end_out && start + 2 < source.length && source.pointer[start] == '%' &&
                 source.pointer[start + 1] == 'l' && instruction->target_count >= 2;
    u64 cursor = start + 2;
    u32 target = UINT32_MAX;
    if (valid && source.pointer[cursor] == '[')
    {
        u64 name_start = ++cursor;
        while (cursor < source.length && source.pointer[cursor] != ']')
        {
            cursor += 1;
        }
        valid = cursor < source.length && cursor != name_start && extra.label_name_count == instruction->target_count - 1u &&
                extra.label_names;
        String8 name = valid ? (String8){.pointer = source.pointer + name_start, .length = cursor - name_start} : (String8){0};
        for (u32 index = 0; valid && target == UINT32_MAX && index < extra.label_name_count; index += 1)
        {
            if (string_equal(name, extra.label_names[index]))
            {
                target = index + 1u;
            }
        }
        valid = valid && target != UINT32_MAX;
        cursor += valid ? 1u : 0u;
    }
    else if (valid)
    {
        u64 operand = 0;
        u64 begin = cursor;
        while (cursor < source.length && source.pointer[cursor] >= '0' && source.pointer[cursor] <= '9')
        {
            u8 digit = (u8)(source.pointer[cursor] - '0');
            valid = valid && operand <= (UINT64_MAX - digit) / 10u;
            operand = valid ? operand * 10u + digit : operand;
            cursor += 1;
        }
        u32 base = ir_inline_assembly_label_operand_base(instruction);
        valid = valid && cursor != begin && base != UINT32_MAX && operand >= base && operand - base < instruction->target_count - 1u;
        target = valid ? 1u + (u32)(operand - base) : UINT32_MAX;
    }
    if (valid)
    {
        *target_out = target;
        *end_out = cursor;
    }
    return valid;
}

bool machine_selection_assembly_label_plan(Arena* arena, IrFunction* function, IrInstruction* instruction,
                                           IrInstructionExtra extra, MachineAssemblyLabelPlan* plan)
{
    bool valid = arena && function && instruction && plan && instruction->opcode == IR_OPCODE_INLINE_ASSEMBLY &&
                 instruction->target_count >= 2 && instruction->targets &&
                 extra.label_name_count == instruction->target_count - 1u && extra.label_names;
    MachineAssemblyLabelPlan result = {.literal = extra.literal};
    if (valid)
    {
        result.target_symbols = arena_allocate(arena, String8, instruction->target_count);
        result.target_count = instruction->target_count;
        for (u32 target = 1; valid && target < instruction->target_count; target += 1)
        {
            String8 symbol = string_format(arena, S8(".Lbuster.inline.asm.{u32}.{u32}"),
                                           ir_instruction_self_id(function, instruction).value, target);
            // A private spelling must not alias a symbol the original template
            // deliberately names. Appending '$' remains a local assembler name
            // and converges after at most source-length candidates.
            while (symbol.length && string_first_sequence(extra.literal, symbol) != BUSTER_STRING_NO_MATCH)
            {
                symbol = string_format(arena, S8("{S8}$"), symbol);
            }
            valid = symbol.length != 0;
            result.target_symbols[target] = symbol;
        }
    }
    u64 output_length = extra.literal.length;
    for (u64 read = 0; valid && read < extra.literal.length;)
    {
        if (extra.literal.pointer[read] == '%' && read + 1 < extra.literal.length && extra.literal.pointer[read + 1] == '%')
        {
            read += 2;
        }
        else if (extra.literal.pointer[read] == '%' && read + 1 < extra.literal.length && extra.literal.pointer[read + 1] == 'l')
        {
            u32 target = UINT32_MAX;
            u64 end = 0;
            valid = machine_selection_assembly_label_reference(instruction, extra, extra.literal, read, &target, &end);
            u64 removed = valid ? end - read : 0;
            valid = valid && result.target_symbols[target].length <= UINT64_MAX - (output_length - removed);
            output_length = valid ? output_length - removed + result.target_symbols[target].length : output_length;
            read = valid ? end : extra.literal.length;
        }
        else
        {
            read += 1;
        }
    }
    char8* output = valid ? arena_allocate(arena, char8, output_length ? output_length : 1u) : 0;
    u64 write = 0;
    for (u64 read = 0; valid && read < extra.literal.length;)
    {
        if (extra.literal.pointer[read] == '%' && read + 1 < extra.literal.length && extra.literal.pointer[read + 1] == '%')
        {
            output[write++] = extra.literal.pointer[read++];
            output[write++] = extra.literal.pointer[read++];
        }
        else if (extra.literal.pointer[read] == '%' && read + 1 < extra.literal.length && extra.literal.pointer[read + 1] == 'l')
        {
            u32 target = UINT32_MAX;
            u64 end = 0;
            valid = machine_selection_assembly_label_reference(instruction, extra, extra.literal, read, &target, &end);
            if (valid)
            {
                String8 symbol = result.target_symbols[target];
                memcpy(output + write, symbol.pointer, symbol.length);
                write += symbol.length;
                read = end;
            }
        }
        else
        {
            output[write++] = extra.literal.pointer[read++];
        }
    }
    if (valid)
    {
        result.literal = (String8){.pointer = output, .length = write};
        *plan = result;
    }
    return valid;
}

bool machine_selection_assembly_label_target(MachineAssemblyLabelPlan const* plan, String8 symbol, u32* target_index_out)
{
    bool found = false;
    if (plan && target_index_out && plan->target_symbols)
    {
        for (u32 target = 1; !found && target < plan->target_count; target += 1)
        {
            if (string_equal(symbol, plan->target_symbols[target]))
            {
                *target_index_out = target;
                found = true;
            }
        }
    }
    return found;
}

bool machine_selection_finish_canonical_edges(Arena* arena, MachineFunction* machine, IrFunction* source,
                                               u32 canonical_edge_offset, u32 const* block_entries,
                                               u32 const* block_exits, u32 const* asm_goto_continuations)
{
    bool valid = arena && machine && source && source->published_cfg && canonical_edge_offset <= machine->edge_count &&
                 source->published_cfg->edge_count == machine->edge_count - canonical_edge_offset;
    bool remap = valid && (block_entries || ir_function_may_contain_opcodes(source, IR_OPCODE_BIT(IR_OPCODE_INLINE_ASSEMBLY)));
    if (remap)
    {
        IrPublishedCfg const* cfg = source->published_cfg;
        u64 capacity = canonical_edge_offset;
        for (u32 canonical_source = 0; valid && canonical_source < source->block_count; canonical_source += 1)
        {
            IrInstruction* canonical_terminator = source->instructions + source->blocks[canonical_source].last_instruction.value;
            u32 continuation = asm_goto_continuations ? asm_goto_continuations[canonical_source] : UINT32_MAX;
            if (canonical_terminator->opcode == IR_OPCODE_INLINE_ASSEMBLY && continuation != UINT32_MAX)
            {
                u32 machine_source = continuation ? continuation - 1u : UINT32_MAX;
                bool descriptor_valid = machine_source < machine->block_count && machine->blocks[machine_source].instruction_count;
                MachineInstruction* instruction = descriptor_valid
                                                      ? machine->instructions + machine->blocks[machine_source].first_instruction +
                                                            machine->blocks[machine_source].instruction_count - 1u
                                                      : 0;
                descriptor_valid = descriptor_valid &&
                                   (instruction->opcode == MACHINE_X64_INLINE_ASSEMBLY || instruction->opcode == MACHINE_A64_INLINE_ASSEMBLY) &&
                                   instruction->payload < machine->inline_assembly_count;
                MachineInlineAssembly* assembly = descriptor_valid ? machine->inline_assemblies + instruction->payload : 0;
                valid = descriptor_valid && assembly->declared_successor_count == canonical_terminator->target_count &&
                        assembly->successor_count >= assembly->declared_successor_count;
                capacity += valid ? assembly->successor_count : 0u;
            }
            else
            {
                capacity += cfg->blocks[canonical_source].successor_count;
            }
            valid = capacity <= MACHINE_REF_PAYLOAD_LIMIT;
        }
        MachineEdge* edges = valid ? arena_allocate(arena, MachineEdge, capacity) : 0;
        u64 scratch_position = arena->position;
        u32* edges_by_destination = valid ? arena_allocate(arena, u32, source->block_count) : 0;
        if (valid)
        {
            memset(edges_by_destination, 0xff, sizeof(*edges_by_destination) * source->block_count);
            if (canonical_edge_offset)
            {
                memcpy(edges, machine->edges, sizeof(*edges) * canonical_edge_offset);
            }
        }
        u32 kept_edges = canonical_edge_offset;
        for (u32 canonical_source = 0; valid && canonical_source < source->block_count; canonical_source += 1)
        {
            IrInstruction* canonical_terminator = source->instructions + source->blocks[canonical_source].last_instruction.value;
            IrCfgBlock const* cfg_block = cfg->blocks + canonical_source;
            u32 continuation = asm_goto_continuations ? asm_goto_continuations[canonical_source] : UINT32_MAX;
            bool general_goto = canonical_terminator->opcode == IR_OPCODE_INLINE_ASSEMBLY && continuation != UINT32_MAX;
            if (general_goto)
            {
                u32 machine_source = continuation ? continuation - 1u : UINT32_MAX;
                valid = machine_source < machine->block_count && machine->blocks[machine_source].instruction_count;
                MachineInstruction* instruction = valid
                                                      ? machine->instructions + machine->blocks[machine_source].first_instruction +
                                                            machine->blocks[machine_source].instruction_count - 1u
                                                      : 0;
                valid = valid &&
                        (instruction->opcode == MACHINE_X64_INLINE_ASSEMBLY || instruction->opcode == MACHINE_A64_INLINE_ASSEMBLY) &&
                        instruction->payload < machine->inline_assembly_count;
                if (valid)
                {
                    MachineInlineAssembly* assembly = machine->inline_assemblies + instruction->payload;
                    valid = assembly->fallthrough_block == continuation &&
                            assembly->declared_successor_count == canonical_terminator->target_count &&
                            assembly->successor_count >= assembly->declared_successor_count;
                }
                for (u32 successor = 0; valid && successor < cfg_block->successor_count; successor += 1)
                {
                    u32 cfg_edge_index = cfg_block->successor_offset + successor;
                    MachineEdge edge = machine->edges[canonical_edge_offset + cfg_edge_index];
                    valid = edge.source_block == canonical_source && edge.destination_block < source->block_count;
                    if (valid)
                    {
                        edges_by_destination[edge.destination_block] = canonical_edge_offset + cfg_edge_index;
                    }
                }
                for (u32 target = 0; valid && target < canonical_terminator->target_count; target += 1)
                {
                    u32 destination = canonical_terminator->targets[target].value;
                    u32 edge_index = destination < source->block_count ? edges_by_destination[destination] : UINT32_MAX;
                    valid = edge_index != UINT32_MAX && continuation <= MACHINE_REF_PAYLOAD_LIMIT - target;
                    if (valid)
                    {
                        MachineEdge edge = machine->edges[edge_index];
                        edge.source_block = continuation + target;
                        edge.destination_block = block_entries ? block_entries[destination] : destination;
                        edges[kept_edges++] = edge;
                    }
                }
                MachineInlineAssembly* assembly = valid ? machine->inline_assemblies + instruction->payload : 0;
                u32 next_control_continuation = continuation + canonical_terminator->target_count;
                for (u32 relocation_index = 0; valid && relocation_index < assembly->relocation_count; relocation_index += 1)
                {
                    MachineInlineAssemblyRelocation* relocation =
                        machine->inline_assembly_relocations + assembly->first_relocation + relocation_index;
                    if (!relocation->is_control)
                    {
                        continue;
                    }
                    u32 target = relocation->target_index;
                    u32 destination = target < canonical_terminator->target_count
                                          ? canonical_terminator->targets[target].value
                                          : UINT32_MAX;
                    u32 edge_index = destination < source->block_count ? edges_by_destination[destination] : UINT32_MAX;
                    valid = edge_index != UINT32_MAX && relocation->continuation_block == next_control_continuation &&
                            relocation->block == (block_entries ? block_entries[destination] : destination);
                    if (valid)
                    {
                        MachineEdge edge = machine->edges[edge_index];
                        edge.source_block = relocation->continuation_block;
                        edge.destination_block = relocation->block;
                        edges[kept_edges++] = edge;
                        next_control_continuation += 1;
                    }
                }
                valid = valid && next_control_continuation == continuation + assembly->successor_count;
                for (u32 successor = 0; successor < cfg_block->successor_count; successor += 1)
                {
                    u32 cfg_edge_index = cfg_block->successor_offset + successor;
                    u32 destination = machine->edges[canonical_edge_offset + cfg_edge_index].destination_block;
                    if (destination < source->block_count)
                    {
                        edges_by_destination[destination] = UINT32_MAX;
                    }
                }
            }
            else
            {
                for (u32 successor = 0; valid && successor < cfg_block->successor_count; successor += 1)
                {
                    u32 cfg_edge_index = cfg_block->successor_offset + successor;
                    MachineEdge edge = machine->edges[canonical_edge_offset + cfg_edge_index];
                    valid = edge.source_block == canonical_source && edge.destination_block < source->block_count;
                    u32 canonical_destination = edge.destination_block;
                    edge.source_block = block_exits ? block_exits[canonical_source] : canonical_source;
                    edge.destination_block = block_entries ? block_entries[canonical_destination] : canonical_destination;
                    bool keep = valid && edge.source_block < machine->block_count;
                    if (keep && canonical_terminator->opcode == IR_OPCODE_INLINE_ASSEMBLY)
                    {
                        // These selected templates have one executable successor.
                        // Other canonical asm-goto destinations must not assign their
                        // joins before the emitted unconditional branch.
                        MachineBlock* block = machine->blocks + edge.source_block;
                        MachineInstruction* branch = machine->instructions + block->first_instruction + block->instruction_count - 1u;
                        keep = machine_ref_kind(branch->operands[0]) == MACHINE_REF_BLOCK &&
                               machine_ref_payload(branch->operands[0]) == edge.destination_block;
                    }
                    if (keep)
                    {
                        edges[kept_edges++] = edge;
                    }
                }
            }
        }
        valid = valid && kept_edges <= capacity;
        if (valid)
        {
            machine->edges = edges;
            machine->edge_count = kept_edges;
        }
        arena_set_position(arena, scratch_position);
    }
    return valid;
}

#define MACHINE_SELECTION_ADDRESS_CACHE_CAPACITY 64u
#define MACHINE_SELECTION_ADDRESS_CHAIN_LIMIT 64u

struct MachineSelectionAddressEntry
{
    MachineSelectionAddress address;
    u32 value_plus_one;
};
BUSTER_CT_CHECK(sizeof(MachineSelectionAddressEntry) == 64);

// Decode one canonical definition. The returned base is the immediate parent
// only for transparent address operations; all other values are opaque leaves.
BUSTER_GLOBAL_LOCAL MachineSelectionAddress machine_selection_address_step(IrProgram* program, IrFunction* function,
                                                                          MachineSelectionAddressCache const* cache, IrValueId id)
{
    MachineSelectionAddress result = {
        .base_value = id,
        .index_value = IR_VALUE_ID_INVALID,
        .object_value = IR_VALUE_ID_INVALID,
        .expression_value = id,
        .symbol = IR_SYMBOL_ID_INVALID,
        .opcode = IR_OPCODE_COUNT,
    };
    if (id.value < function->value_count)
    {
        IrValue* value = function->values + id.value;
        result.alignment = value->alignment;
        result.flags = (u16)((value->is_read_only || value->points_to_read_only ? MACHINE_SELECTION_ADDRESS_READ_ONLY : 0) |
                             (value->is_volatile ? MACHINE_SELECTION_ADDRESS_VOLATILE : 0));
        if (value->category == IR_VALUE_VALUE)
        {
            u32 kind = IR_TYPE_COUNT;
            if (cache->type_classes && value->canonical_type.value < cache->type_count)
            {
                IR_ACCESS_AUDIT_TYPE(value->canonical_type.value);
                kind = cache->type_classes[value->canonical_type.value].kind;
            }
            else
            {
                IrType* type = ir_type_from_id(&program->types, value->canonical_type);
                kind = type ? (u32)type->kind : (u32)IR_TYPE_COUNT;
            }
            result.flags |= kind == IR_TYPE_ARRAY || kind == IR_TYPE_VECTOR ? MACHINE_SELECTION_ADDRESS_STORAGE : 0;
        }
        IrInstruction* definition = value->definition.value < function->instruction_count ? function->instructions + value->definition.value : 0;
        if (definition)
        {
            result.opcode = definition->opcode;
            result.flags |= definition->volatile_access ? MACHINE_SELECTION_ADDRESS_VOLATILE : 0;
            if (definition->opcode == IR_OPCODE_LOCAL)
            {
                result.object_value = id;
                result.flags |= MACHINE_SELECTION_ADDRESS_STORAGE;
            }
            else if (definition->opcode == IR_OPCODE_GLOBAL || definition->opcode == IR_OPCODE_FUNCTION)
            {
                result.symbol = definition->symbol;
                result.object_value = definition->opcode == IR_OPCODE_GLOBAL ? id : IR_VALUE_ID_INVALID;
                IrSymbol* symbol = ir_symbol_from_id(&program->symbols, definition->symbol);
                if (symbol)
                {
                    result.flags |= symbol->is_thread_local ? MACHINE_SELECTION_ADDRESS_THREAD_LOCAL : 0;
                    result.flags |= symbol->is_definition ? MACHINE_SELECTION_ADDRESS_SYMBOL_DEFINITION : 0;
                }
            }
            else if (definition->operand_count && definition->operands && definition->operands[0].value < function->value_count)
            {
                IrValueId parent = definition->operands[0];
                if (definition->opcode == IR_OPCODE_ADDRESS_OF || definition->opcode == IR_OPCODE_DEREFERENCE)
                {
                    result.base_value = parent;
                }
                else if (definition->opcode == IR_OPCODE_FIELD && definition->immediate_count && definition->immediates)
                {
                    IrType* aggregate = ir_type_from_id(&program->types, function->values[parent.value].canonical_type);
                    u64 field = definition->immediates[0];
                    if (aggregate && aggregate->fields && field < aggregate->field_count)
                    {
                        result.base_value = parent;
                        result.field_offset = aggregate->fields[field].offset;
                        result.displacement = result.field_offset;
                        result.flags |= MACHINE_SELECTION_ADDRESS_FIELD;
                    }
                }
                else if (definition->opcode == IR_OPCODE_INDEX && definition->operand_count >= 2 &&
                         definition->operands[1].value < function->value_count)
                {
                    IrTypeId index_type_id = function->values[definition->operands[1].value].canonical_type;
                    IR_ACCESS_AUDIT_TYPE(index_type_id.value);
                    u32 index_bits = 0;
                    bool index_signed = false;
                    if (cache->type_classes && index_type_id.value < cache->type_count &&
                        cache->type_classes[index_type_id.value].kind == IR_TYPE_INTEGER &&
                        cache->type_classes[index_type_id.value].bit_width_log2 <= 6)
                    {
                        MachineTypeClass index_class = cache->type_classes[index_type_id.value];
                        index_bits = 1u << index_class.bit_width_log2;
                        index_signed = (index_class.flags & MACHINE_TYPE_CLASS_SIGNED) != 0;
                    }
                    else
                    {
                        IrType* index_type = ir_type_from_id(&program->types, index_type_id);
                        if (index_type && index_type->kind == IR_TYPE_INTEGER)
                        {
                            index_bits = index_type->bit_width;
                            index_signed = index_type->is_signed;
                        }
                    }
                    IrType* element = ir_type_from_id(&program->types, definition->canonical_type);
                    if (index_bits && index_bits <= 64 && element && element->layout.resolved)
                    {
                        result.base_value = parent;
                        result.index_value = definition->operands[1];
                        result.scale = element->layout.size;
                        result.index_bit_width = (u8)index_bits;
                        result.flags |= MACHINE_SELECTION_ADDRESS_INDEX;
                        result.flags |= index_signed ? MACHINE_SELECTION_ADDRESS_INDEX_SIGNED : 0;
                    }
                }
            }
        }
    }
    return result;
}

// Join only additive address operations. A second dynamic index or a u64
// displacement overflow retains the immediate parent as the materialized base.
// In particular, this never reassociates signed pointer arithmetic, changes an
// index extension, or combines a symbol relocation with a numeric addend.
BUSTER_GLOBAL_LOCAL MachineSelectionAddress machine_selection_address_join(MachineSelectionAddress step, MachineSelectionAddress parent)
{
    step.object_value = parent.object_value;
    step.symbol = parent.symbol;
    step.flags |= parent.flags & (MACHINE_SELECTION_ADDRESS_THREAD_LOCAL | MACHINE_SELECTION_ADDRESS_READ_ONLY |
                                  MACHINE_SELECTION_ADDRESS_VOLATILE | MACHINE_SELECTION_ADDRESS_SYMBOL_DEFINITION);
    bool one_index = step.index_value.value == IR_ID_UNDERLYING_INVALID || parent.index_value.value == IR_ID_UNDERLYING_INVALID;
    if (one_index && step.displacement <= UINT64_MAX - parent.displacement)
    {
        step.base_value = parent.base_value;
        step.displacement += parent.displacement;
        if (step.index_value.value == IR_ID_UNDERLYING_INVALID)
        {
            step.index_value = parent.index_value;
            step.scale = parent.scale;
            step.index_bit_width = parent.index_bit_width;
            step.flags |= parent.flags & MACHINE_SELECTION_ADDRESS_INDEX_SIGNED;
        }
    }
    // Preserve only the alignment explicitly certified for this exact value.
    // Parent type alignment is not a pointee alignment proof for an opaque load.
    return step;
}

MachineSelectionAddress machine_selection_address(Arena* arena, IrProgram* program, IrFunction* function,
                                                   MachineSelectionAddressCache* cache, IrValueId value)
{
    MachineSelectionAddress result = {
        .base_value = value,
        .index_value = IR_VALUE_ID_INVALID,
        .object_value = IR_VALUE_ID_INVALID,
        .expression_value = value,
        .symbol = IR_SYMBOL_ID_INVALID,
        .opcode = IR_OPCODE_COUNT,
    };
    if (value.value < function->value_count)
    {
        if (!cache->entries)
        {
            cache->entries = arena_allocate(arena, MachineSelectionAddressEntry, MACHINE_SELECTION_ADDRESS_CACHE_CAPACITY);
            memset(cache->entries, 0, sizeof(*cache->entries) * MACHINE_SELECTION_ADDRESS_CACHE_CAPACITY);
        }
        MachineSelectionAddress pending[MACHINE_SELECTION_ADDRESS_CHAIN_LIMIT];
        u32 count = 0;
        IrValueId current = value;
        bool complete = false;
        while (!complete)
        {
            MachineSelectionAddressEntry* entry = cache->entries + (current.value & (MACHINE_SELECTION_ADDRESS_CACHE_CAPACITY - 1));
            if (entry->value_plus_one == current.value + 1)
            {
                result = entry->address;
                complete = true;
            }
            else
            {
                result = machine_selection_address_step(program, function, cache, current);
                if (result.base_value.value == current.value || count == MACHINE_SELECTION_ADDRESS_CHAIN_LIMIT)
                {
                    // At the depth bound retain the original expression;
                    // preserving its producer also preserves any cycle in
                    // malformed unvalidated input without recursive traversal.
                    if (count == MACHINE_SELECTION_ADDRESS_CHAIN_LIMIT)
                    {
                        result.base_value = current;
                        result.index_value = IR_VALUE_ID_INVALID;
                        result.displacement = 0;
                        result.object_value = IR_VALUE_ID_INVALID;
                        result.symbol = IR_SYMBOL_ID_INVALID;
                    }
                    complete = true;
                }
                else
                {
                    pending[count++] = result;
                    current = result.base_value;
                }
            }
        }
        while (count)
        {
            MachineSelectionAddressEntry* entry = cache->entries + (result.expression_value.value & (MACHINE_SELECTION_ADDRESS_CACHE_CAPACITY - 1));
            entry->address = result;
            entry->value_plus_one = result.expression_value.value + 1;
            result = machine_selection_address_join(pending[--count], result);
        }
        MachineSelectionAddressEntry* entry = cache->entries + (value.value & (MACHINE_SELECTION_ADDRESS_CACHE_CAPACITY - 1));
        entry->address = result;
        entry->value_plus_one = value.value + 1;
    }
    return result;
}

MachineSelectionValueFacts machine_selection_value_facts_allocate(Arena* arena, u32 value_count)
{
    u32 capacity = value_count ? value_count : 1;
    MachineSelectionValueFacts result = {
        .definition_blocks = arena_allocate(arena, u32, capacity),
        .use_counts = arena_allocate(arena, u32, capacity),
        .use_blocks = arena_allocate(arena, u32, capacity),
    };
    if (value_count)
    {
        memset(result.definition_blocks, 0xff, (u64)value_count * sizeof(*result.definition_blocks));
        memset(result.use_counts, 0, (u64)value_count * sizeof(*result.use_counts));
        memset(result.use_blocks, 0xff, (u64)value_count * sizeof(*result.use_blocks));
    }
    return result;
}

MachineTypeClass* machine_type_classes_build(Arena* arena, IrTypeTable const* types)
{
    MachineTypeClass* classes = arena_allocate(arena, MachineTypeClass, types->count ? types->count : 1);
    for (u32 type_index = 0; type_index < types->count; type_index += 1)
    {
        IrType const* type = types->types + type_index;
        bool resolved = type->layout.resolved;
        bool scalar_kind = type->kind == IR_TYPE_BOOLEAN || type->kind == IR_TYPE_INTEGER || type->kind == IR_TYPE_POINTER || type->kind == IR_TYPE_ENUM;
        u32 flags = 0;
        flags |= resolved ? MACHINE_TYPE_CLASS_RESOLVED : 0;
        flags |= resolved && scalar_kind && type->layout.size <= 8 ? MACHINE_TYPE_CLASS_SCALAR_REGISTER : 0;
        flags |= type->kind == IR_TYPE_FLOAT &&
                         ((type->bit_width == 16 && type->float_format == IR_FLOAT_FORMAT_IEEE) || type->bit_width == 32 || type->bit_width == 64)
                     ? MACHINE_TYPE_CLASS_FLOAT_SCALAR
                     : 0;
        bool vector_register = false;
        if (resolved && type->kind == IR_TYPE_VECTOR && type->layout.size == 64 && type->element_type.value < types->count)
        {
            IrType const* element = types->types + type->element_type.value;
            vector_register = element->layout.resolved && element->layout.size &&
                              type->element_count <= UINT64_MAX / element->layout.size &&
                              type->element_count * element->layout.size == type->layout.size;
        }
        flags |= vector_register ? MACHINE_TYPE_CLASS_VECTOR_REGISTER : 0;
        flags |= type->kind == IR_TYPE_POINTER || type->kind == IR_TYPE_FUNCTION || (type->kind == IR_TYPE_INTEGER && type->bit_width > 32)
                     ? MACHINE_TYPE_CLASS_WIDE
                     : 0;
        flags |= type->is_signed ? MACHINE_TYPE_CLASS_SIGNED : 0;
        flags |= type->kind == IR_TYPE_INTEGER && type->bit_width == 128 ? MACHINE_TYPE_CLASS_INTEGER128 : 0;
        flags |= type->kind == IR_TYPE_STRUCT || type->kind == IR_TYPE_UNION || type->kind == IR_TYPE_SLICE ? MACHINE_TYPE_CLASS_AGGREGATE : 0;
        u32 size_log2 = MACHINE_TYPE_CLASS_NO_LOG2;
        if (resolved && BUSTER_IS_POWER_OF_TWO(type->layout.size))
        {
            size_log2 = trailing_zeroes_u64(type->layout.size);
        }
        u32 bit_width_log2 = MACHINE_TYPE_CLASS_NO_LOG2;
        if (BUSTER_IS_POWER_OF_TWO(type->bit_width))
        {
            bit_width_log2 = trailing_zeroes_u64(type->bit_width);
        }
        classes[type_index] = (MachineTypeClass){
            .flags = (u8)flags,
            .kind = (u8)type->kind,
            .size_log2 = (u8)size_log2,
            .bit_width_log2 = (u8)bit_width_log2,
        };
    }
    return classes;
}

MachineSelectionValidationError machine_selection_validate_function(Arena* arena, IrProgram* program, IrFunction* function)
{
    MachineSelectionValidationError error = MACHINE_SELECTION_VALIDATION_NONE;
    if (!arena || !program || !function || (function->instruction_count && !function->instructions) ||
        (function->block_count && !function->blocks) || (function->value_count && !function->values))
    {
        error = MACHINE_SELECTION_VALIDATION_INVALID_ARGUMENT;
    }
    else
    {
        // Validation needs presence, not definition ids or use facts. These
        // marks are private scratch; selectors gather the facts they consume
        // in their own existing row walks after validation succeeds.
        u32 instruction_capacity = function->instruction_count ? function->instruction_count : 1;
        u32 value_capacity = function->value_count ? function->value_count : 1;
        u64 scratch_count = (u64)instruction_capacity + value_capacity;
        u8* visited = arena_allocate(arena, u8, scratch_count);
        u8* defined = visited + instruction_capacity;
        memset(visited, 0, scratch_count);
        u32 visited_count = 0;
        for (u32 block_index = 0; block_index < function->block_count && error == MACHINE_SELECTION_VALIDATION_NONE; block_index += 1)
        {
            IrBlock* block = function->blocks + block_index;
            IrInstructionId tail = IR_INSTRUCTION_ID_INVALID;
            IrInstructionId id = block->first_instruction;
            while (id.value != IR_ID_UNDERLYING_INVALID && error == MACHINE_SELECTION_VALIDATION_NONE)
            {
                if (id.value >= function->instruction_count || visited[id.value])
                {
                    error = MACHINE_SELECTION_VALIDATION_OWNERSHIP;
                }
                else
                {
                    visited[id.value] = 1;
                    visited_count += 1;
                    IrInstruction* instruction = function->instructions + id.value;
                    if (instruction->opcode >= IR_OPCODE_COUNT)
                    {
                        error = MACHINE_SELECTION_VALIDATION_INVALID_OPCODE;
                    }
                    else if (instruction->operand_count && !instruction->operands)
                    {
                        error = MACHINE_SELECTION_VALIDATION_INVALID_VALUE;
                    }
                    else
                    {
                        if (instruction->result.value != IR_ID_UNDERLYING_INVALID)
                        {
                            u32 value_index = instruction->result.value;
                            if (value_index >= function->value_count)
                            {
                                error = MACHINE_SELECTION_VALIDATION_INVALID_VALUE;
                            }
                            else if (defined[value_index])
                            {
                                error = MACHINE_SELECTION_VALIDATION_DUPLICATE_DEFINITION;
                            }
                            else
                            {
                                defined[value_index] = 1;
                            }
                        }
                        for (u32 operand_index = 0; operand_index < instruction->operand_count && error == MACHINE_SELECTION_VALIDATION_NONE;
                             operand_index += 1)
                        {
                            if (instruction->operands[operand_index].value >= function->value_count)
                            {
                                error = MACHINE_SELECTION_VALIDATION_INVALID_VALUE;
                            }
                        }
                    }
                    tail = id;
                    id = ir_block_next_instruction(function, block, id);
                }
            }
            if (error == MACHINE_SELECTION_VALIDATION_NONE && tail.value != block->last_instruction.value)
            {
                error = MACHINE_SELECTION_VALIDATION_OWNERSHIP;
            }
        }
        if (error == MACHINE_SELECTION_VALIDATION_NONE && visited_count != function->instruction_count)
        {
            error = MACHINE_SELECTION_VALIDATION_OWNERSHIP;
        }
    }
    return error;
}

void machine_selection_certify_stack_memory(Arena* arena, MachineFunction* machine, IrFunction const* source)
{
    machine->stack_slot_memory_flags = 0;
    bool valid = !machine->nonvolatile_memory_certified && machine->stack_slot_count && machine->stack_slot_sizes &&
                 machine->instructions && machine->line_mark_count && machine->line_marks && source && source->instructions;
    bool volatile_seen = false;
    for (u32 mark_index = 0; valid && mark_index < machine->line_mark_count; mark_index += 1)
    {
        MachineLineMark mark = machine->line_marks[mark_index];
        valid = mark.instruction < source->instruction_count && mark.row <= machine->instruction_count &&
                (!mark_index || machine->line_marks[mark_index - 1].row <= mark.row);
        if (valid)
        {
            volatile_seen |= source->instructions[mark.instruction].volatile_access;
        }
    }
    if (valid && volatile_seen)
    {
        u8* flags = arena_allocate(arena, u8, machine->stack_slot_count);
        memset(flags, MACHINE_STACK_SLOT_MEMORY_NONVOLATILE, machine->stack_slot_count);
        for (u32 mark_index = 0; mark_index < machine->line_mark_count; mark_index += 1)
        {
            MachineLineMark mark = machine->line_marks[mark_index];
            if (source->instructions[mark.instruction].volatile_access)
            {
                u32 end = mark_index + 1 < machine->line_mark_count ? machine->line_marks[mark_index + 1].row : machine->instruction_count;
                for (u32 row = mark.row; row < end; row += 1)
                {
                    // All frame operands are tainted, including helper loads,
                    // stores and addresses. This may lose precision, but cannot
                    // accidentally certify one piece of a split volatile access.
                    MachineInstruction const* instruction = machine->instructions + row;
                    MachineScheduleMemoryAccess access = machine_schedule_frame_access(machine, instruction);
                    if (access.kind == MACHINE_SCHEDULE_MEMORY_STACK_RANGE)
                    {
                        flags[access.stack_slot] = 0;
                    }
                    for (u32 operand = 0; operand < MACHINE_INSTRUCTION_OPERAND_COUNT; operand += 1)
                    {
                        MachineRef reference = instruction->operands[operand];
                        if (machine_ref_kind(reference) == MACHINE_REF_STACK_SLOT)
                        {
                            u32 slot = machine_ref_payload(reference);
                            if (slot < machine->stack_slot_count)
                            {
                                flags[slot] = 0;
                            }
                        }
                    }
                }
            }
        }
        // Incoming ABI rows before the first source mark are nonvolatile;
        // volatile accesses through pointers remain UNKNOWN to the scheduler.
        machine->stack_slot_memory_flags = flags;
    }
}
