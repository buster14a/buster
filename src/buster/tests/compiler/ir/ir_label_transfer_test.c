// Label transfer and block-parameter provenance regressions for dynamic INDEX,
// ARRAY/AGGREGATE and parameter unions, included by ir_test.c. The oracles keep
// the original pairwise searches and are compared with the public validators on
// deterministic random metadata, including duplicated IDs and malformed flags.
// Geometric controls bound the recorded set/path work of metadata-rich inputs.
typedef struct IrTestRandom IrTestRandom;
struct IrTestRandom
{
    u64 state;
};

BUSTER_GLOBAL_LOCAL u32 ir_test_transfer_random(IrTestRandom* random, u32 bound)
{
    random->state ^= random->state >> 12;
    random->state ^= random->state << 25;
    random->state ^= random->state >> 27;
    return (u32)(((random->state * UINT64_C(0x2545f4914f6cdd1d)) >> 33) % bound);
}

enum
{
    IR_TEST_TRANSFER_PATHS = 16,
    IR_TEST_TRANSFER_BLOCKS = 8,
    IR_TEST_TRANSFER_OPERANDS = 12,
    IR_TEST_TRANSFER_POOL = 5,
};

typedef struct IrTestTransferMeta IrTestTransferMeta;
struct IrTestTransferMeta
{
    IrValueLabelMetadata metadata;
    IrLabelProvenancePath paths[IR_TEST_TRANSFER_PATHS];
    IrBlockId path_blocks[IR_TEST_TRANSFER_PATHS][IR_TEST_TRANSFER_BLOCKS];
    IrBlockId blocks[IR_TEST_TRANSFER_BLOCKS];
};

// Self-pointers are rebuilt after every edit, so a counted row can never be
// paired with a missing backing pointer: this file tests exactness, not backing.
BUSTER_GLOBAL_LOCAL void ir_test_transfer_finalize(IrTestTransferMeta* meta)
{
    IrValueLabelMetadata* value = &meta->metadata;
    value->label_blocks = value->label_block_count ? meta->blocks : 0;
    value->label_paths = value->label_path_count ? meta->paths : 0;
    for (u32 index = 0; index < value->label_path_count; index += 1)
    {
        meta->paths[index].label_blocks = meta->paths[index].label_block_count ? meta->path_blocks[index] : 0;
    }
}

BUSTER_GLOBAL_LOCAL void ir_test_transfer_union_blocks(IrTestTransferMeta* meta)
{
    IrValueLabelMetadata* value = &meta->metadata;
    value->label_block_count = 0;
    for (u32 path_index = 0; path_index < value->label_path_count; path_index += 1)
    {
        for (u32 block_index = 0; block_index < meta->paths[path_index].label_block_count; block_index += 1)
        {
            IrBlockId block = meta->path_blocks[path_index][block_index];
            bool found = false;
            for (u32 index = 0; index < value->label_block_count; index += 1)
            {
                found |= meta->blocks[index].value == block.value;
            }
            if (!found && value->label_block_count < IR_TEST_TRANSFER_BLOCKS)
            {
                meta->blocks[value->label_block_count] = block;
                value->label_block_count += 1;
            }
        }
    }
}

// Random metadata. Consistent mode derives the aggregate sets and flags from
// the paths the way shape validation expects; the rest is arbitrary.
BUSTER_GLOBAL_LOCAL void ir_test_transfer_random_meta(IrTestRandom* random, IrTestTransferMeta* meta, u32 slots, u64 slot_size, bool direct, u32 maximum_paths)
{
    *meta = (IrTestTransferMeta){0};
    IrValueLabelMetadata* value = &meta->metadata;
    bool consistent = ir_test_transfer_random(random, 10) < 7;
    value->label_path_count = ir_test_transfer_random(random, maximum_paths + 1);
    bool any_label = false;
    bool any_non_label = false;
    for (u32 index = 0; index < value->label_path_count; index += 1)
    {
        IrLabelProvenancePath* path = meta->paths + index;
        u32 slot = ir_test_transfer_random(random, slots + (ir_test_transfer_random(random, 12) == 0 ? 1 : 0));
        path->offset = (u64)slot * slot_size;
        path->size = ir_test_transfer_random(random, 8) == 0 ? slot_size / 2 : slot_size;
        path->is_non_label = ir_test_transfer_random(random, 3) == 0;
        path->label_block_count = path->is_non_label ? (ir_test_transfer_random(random, 8) == 0 ? 1 : 0) : 1 + ir_test_transfer_random(random, 3);
        for (u32 block_index = 0; block_index < path->label_block_count; block_index += 1)
        {
            meta->path_blocks[index][block_index].value = ir_test_transfer_random(random, IR_TEST_TRANSFER_POOL);
        }
        any_label |= !path->is_non_label;
        any_non_label |= path->is_non_label;
    }
    if (consistent)
    {
        ir_test_transfer_union_blocks(meta);
        value->has_label_provenance = any_label;
        value->has_non_label_provenance = any_non_label;
        if (direct && !value->label_path_count && ir_test_transfer_random(random, 3) == 0)
        {
            value->is_label_value = true;
            value->label_block_count = 1 + ir_test_transfer_random(random, 3);
            for (u32 index = 0; index < value->label_block_count; index += 1)
            {
                meta->blocks[index].value = ir_test_transfer_random(random, IR_TEST_TRANSFER_POOL);
            }
        }
    }
    else
    {
        value->label_block_count = ir_test_transfer_random(random, 5);
        for (u32 index = 0; index < value->label_block_count; index += 1)
        {
            meta->blocks[index].value = ir_test_transfer_random(random, IR_TEST_TRANSFER_POOL);
        }
        value->has_label_provenance = ir_test_transfer_random(random, 2) != 0;
        value->has_non_label_provenance = ir_test_transfer_random(random, 2) != 0;
        value->is_label_value = direct && ir_test_transfer_random(random, 6) == 0;
    }
    ir_test_transfer_finalize(meta);
}

// One small random edit; most edits break exactly one clause of the contract.
BUSTER_GLOBAL_LOCAL void ir_test_transfer_mutate(IrTestRandom* random, IrTestTransferMeta* meta)
{
    IrValueLabelMetadata* value = &meta->metadata;
    switch (ir_test_transfer_random(random, 10))
    {
    case 0: value->has_label_provenance = !value->has_label_provenance; break;
    case 1: value->has_non_label_provenance = !value->has_non_label_provenance; break;
    case 2:
        if (value->label_block_count)
        {
            value->label_block_count -= 1;
        }
        break;
    case 3:
        if (value->label_block_count < IR_TEST_TRANSFER_BLOCKS)
        {
            meta->blocks[value->label_block_count].value = ir_test_transfer_random(random, IR_TEST_TRANSFER_POOL);
            value->label_block_count += 1;
        }
        break;
    case 4:
        if (value->label_path_count)
        {
            meta->paths[ir_test_transfer_random(random, value->label_path_count)].is_non_label ^= true;
        }
        break;
    case 5:
        if (value->label_path_count)
        {
            IrLabelProvenancePath* path = meta->paths + ir_test_transfer_random(random, value->label_path_count);
            path->label_block_count = path->label_block_count ? path->label_block_count - 1 : 0;
        }
        break;
    case 6:
        if (value->label_path_count)
        {
            u32 path_index = ir_test_transfer_random(random, value->label_path_count);
            IrLabelProvenancePath* path = meta->paths + path_index;
            if (path->label_block_count < IR_TEST_TRANSFER_BLOCKS)
            {
                meta->path_blocks[path_index][path->label_block_count].value = ir_test_transfer_random(random, IR_TEST_TRANSFER_POOL);
                path->label_block_count += 1;
            }
        }
        break;
    case 7:
        if (value->label_path_count)
        {
            meta->paths[ir_test_transfer_random(random, value->label_path_count)].offset += 8 * ir_test_transfer_random(random, 3);
        }
        break;
    case 8:
        if (value->label_path_count)
        {
            meta->paths[ir_test_transfer_random(random, value->label_path_count)].size ^= 8;
        }
        break;
    default:
        if (value->label_path_count && value->label_path_count < IR_TEST_TRANSFER_PATHS)
        {
            u32 source = ir_test_transfer_random(random, value->label_path_count);
            meta->paths[value->label_path_count] = meta->paths[source];
            memcpy(meta->path_blocks[value->label_path_count], meta->path_blocks[source], sizeof(meta->path_blocks[source]));
            value->label_path_count += 1;
        }
        break;
    }
    ir_test_transfer_finalize(meta);
}

// ---- Original pairwise searches, retained as oracles --------------------

BUSTER_GLOBAL_LOCAL bool ir_test_oracle_contains(IrBlockId* blocks, u32 count, IrBlockId block)
{
    bool found = false;
    for (u32 index = 0; blocks && index < count; index += 1)
    {
        found |= blocks[index].value == block.value;
    }
    return found;
}

BUSTER_GLOBAL_LOCAL bool ir_test_oracle_has_label(IrValueLabelMetadata* value)
{
    bool result = value->is_label_value || value->has_label_provenance;
    for (u32 index = 0; index < value->label_path_count; index += 1)
    {
        result |= !value->label_paths[index].is_non_label && value->label_paths[index].label_block_count;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool ir_test_oracle_non_label_path(IrValueLabelMetadata* value)
{
    bool result = value->has_non_label_provenance;
    for (u32 index = 0; index < value->label_path_count; index += 1)
    {
        result |= value->label_paths[index].is_non_label;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool ir_test_oracle_path_contains_all(IrLabelProvenancePath* container, IrLabelProvenancePath* contained)
{
    bool all = true;
    for (u32 index = 0; all && index < contained->label_block_count; index += 1)
    {
        all = ir_test_oracle_contains(container->label_blocks, container->label_block_count, contained->label_blocks[index]);
    }
    return all;
}

BUSTER_GLOBAL_LOCAL bool ir_test_oracle_dynamic(IrValueLabelMetadata* result, IrValueLabelMetadata* source, u64 array_size, u64 element_size)
{
    bool valid = true;
    if (ir_test_oracle_has_label(result) || ir_test_oracle_has_label(source))
    {
        if (!element_size || array_size < element_size || !source->label_path_count)
        {
            valid = !ir_test_oracle_has_label(result);
        }
        else
        {
            bool source_non_label = source->has_non_label_provenance;
            bool source_label = false;
            for (u32 source_index = 0; valid && source_index < source->label_path_count; source_index += 1)
            {
                IrLabelProvenancePath* source_path = source->label_paths + source_index;
                if (source_path->offset > UINT64_MAX - source_path->size || source_path->offset + source_path->size > array_size)
                {
                    valid = false;
                }
                else
                {
                    source_non_label |= source_path->is_non_label;
                    source_label |= !source_path->is_non_label;
                    bool found = false;
                    for (u32 result_index = 0; result_index < result->label_path_count; result_index += 1)
                    {
                        IrLabelProvenancePath* result_path = result->label_paths + result_index;
                        if (result_path->offset == 0 && result_path->size == element_size)
                        {
                            if (source_path->is_non_label)
                            {
                                found |= result_path->is_non_label || result->has_non_label_provenance;
                            }
                            else
                            {
                                found |= !result_path->is_non_label && ir_test_oracle_path_contains_all(result_path, source_path);
                            }
                        }
                    }
                    valid = found;
                }
            }
            for (u32 result_index = 0; valid && result_index < result->label_path_count; result_index += 1)
            {
                IrLabelProvenancePath* result_path = result->label_paths + result_index;
                if (result_path->offset != 0 || result_path->size != element_size)
                {
                    valid = false;
                }
                else if (result_path->is_non_label)
                {
                    valid = source_non_label;
                }
                else
                {
                    for (u32 block_index = 0; valid && block_index < result_path->label_block_count; block_index += 1)
                    {
                        bool found = false;
                        for (u32 source_index = 0; source_index < source->label_path_count; source_index += 1)
                        {
                            IrLabelProvenancePath* source_path = source->label_paths + source_index;
                            found |= !source_path->is_non_label && ir_test_oracle_contains(source_path->label_blocks, source_path->label_block_count,
                                                                                         result_path->label_blocks[block_index]);
                        }
                        valid = found;
                    }
                }
            }
            valid = valid && (!source_non_label || result->has_non_label_provenance) && (!source_label || ir_test_oracle_has_label(result));
            for (u32 block_index = 0; valid && block_index < source->label_block_count; block_index += 1)
            {
                valid = ir_test_oracle_contains(result->label_blocks, result->label_block_count, source->label_blocks[block_index]);
            }
        }
    }
    return valid;
}

BUSTER_GLOBAL_LOCAL bool ir_test_oracle_index(IrValueLabelMetadata* result, IrValueLabelMetadata* source, u64 array_size, u64 element_size)
{
    bool valid = !result->is_label_value;
    if (valid && (ir_test_oracle_has_label(source) || ir_test_oracle_has_label(result)))
    {
        // The shared storage transfer precondition of every INDEX.
        valid = (!ir_test_oracle_has_label(result) || ir_test_oracle_has_label(source)) && (!result->label_path_count || source->label_path_count);
        for (u32 index = 0; valid && result->has_label_provenance && index < result->label_block_count; index += 1)
        {
            valid = ir_test_oracle_contains(source->label_blocks, source->label_block_count, result->label_blocks[index]);
        }
        valid = valid && ir_test_oracle_dynamic(result, source, array_size, element_size);
    }
    return valid;
}

// Array of operand_count elements of element_size bytes each.
BUSTER_GLOBAL_LOCAL bool ir_test_oracle_aggregate(IrValueLabelMetadata* result, IrValueLabelMetadata* sources, u32 operand_count, u64 element_size)
{
    bool valid = !result->is_label_value;
    bool result_has_label = ir_test_oracle_has_label(result);
    bool source_has_label = false;
    for (u32 operand_index = 0; operand_index < operand_count; operand_index += 1)
    {
        source_has_label |= ir_test_oracle_has_label(sources + operand_index);
    }
    if (valid && (result_has_label || source_has_label))
    {
        bool source_non_label = false;
        bool source_label = false;
        for (u32 operand_index = 0; valid && operand_index < operand_count; operand_index += 1)
        {
            IrValueLabelMetadata* source = sources + operand_index;
            u64 base_offset = operand_index * element_size;
            source_non_label |= ir_test_oracle_non_label_path(source);
            source_label |= ir_test_oracle_has_label(source);
            for (u32 path_index = 0; valid && path_index < source->label_path_count; path_index += 1)
            {
                IrLabelProvenancePath* source_path = source->label_paths + path_index;
                if (source_path->offset > UINT64_MAX - source_path->size || source_path->offset + source_path->size > element_size ||
                    base_offset > UINT64_MAX - source_path->offset)
                {
                    valid = false;
                }
                else
                {
                    bool found = false;
                    for (u32 result_index = 0; result_index < result->label_path_count; result_index += 1)
                    {
                        IrLabelProvenancePath* result_path = result->label_paths + result_index;
                        if (result_path->offset == base_offset + source_path->offset && result_path->size == source_path->size)
                        {
                            if (source_path->is_non_label)
                            {
                                found |= result_path->is_non_label || result->has_non_label_provenance;
                            }
                            else
                            {
                                found |= !result_path->is_non_label && ir_test_oracle_path_contains_all(result_path, source_path);
                            }
                        }
                    }
                    valid = found;
                }
            }
            if (valid && source->is_label_value)
            {
                bool found = false;
                for (u32 result_index = 0; result_index < result->label_path_count; result_index += 1)
                {
                    IrLabelProvenancePath* result_path = result->label_paths + result_index;
                    if (result_path->offset == base_offset && result_path->size == element_size && !result_path->is_non_label)
                    {
                        bool blocks_match = source->label_block_count <= result_path->label_block_count;
                        for (u32 block_index = 0; blocks_match && block_index < source->label_block_count; block_index += 1)
                        {
                            blocks_match = ir_test_oracle_contains(result_path->label_blocks, result_path->label_block_count, source->label_blocks[block_index]);
                        }
                        found |= blocks_match;
                    }
                }
                valid = found;
            }
        }
        valid = valid && (!source_non_label || result->has_non_label_provenance) && (!source_label || result_has_label);
        for (u32 result_index = 0; valid && result_index < result->label_path_count; result_index += 1)
        {
            IrLabelProvenancePath* result_path = result->label_paths + result_index;
            bool found = false;
            for (u32 operand_index = 0; operand_index < operand_count && !found; operand_index += 1)
            {
                IrValueLabelMetadata* source = sources + operand_index;
                u64 base_offset = operand_index * element_size;
                for (u32 path_index = 0; path_index < source->label_path_count; path_index += 1)
                {
                    IrLabelProvenancePath* source_path = source->label_paths + path_index;
                    if (result_path->offset == base_offset + source_path->offset && result_path->size == source_path->size)
                    {
                        if (source_path->is_non_label)
                        {
                            found |= result_path->is_non_label || result->has_non_label_provenance;
                        }
                        else
                        {
                            found |= !result_path->is_non_label && ir_test_oracle_path_contains_all(source_path, result_path);
                        }
                    }
                }
                if (source->is_label_value && result_path->offset == base_offset && result_path->size == element_size && !result_path->is_non_label)
                {
                    bool blocks_match = source->label_block_count <= result_path->label_block_count;
                    for (u32 block_index = 0; blocks_match && block_index < source->label_block_count; block_index += 1)
                    {
                        blocks_match = ir_test_oracle_contains(result_path->label_blocks, result_path->label_block_count, source->label_blocks[block_index]);
                    }
                    found |= blocks_match;
                }
            }
            valid = found;
        }
    }
    return valid;
}

BUSTER_GLOBAL_LOCAL bool ir_test_oracle_parameter(IrValueLabelMetadata* destination, IrValueLabelMetadata* incoming, u32 incoming_count)
{
    bool valid = true;
    bool incoming_non_label = false;
    bool incoming_label = false;
    bool all_incoming_pure_labels = true;
    bool incoming_paths = false;
    u32 incoming_block_count = 0;
    for (u32 incoming_index = 0; incoming_index < incoming_count; incoming_index += 1)
    {
        IrValueLabelMetadata* source = incoming + incoming_index;
        bool source_non_label = ir_test_oracle_non_label_path(source);
        incoming_label |= source->is_label_value || source->has_label_provenance || source->label_block_count != 0 || source->label_path_count != 0;
        incoming_non_label |= source_non_label;
        incoming_paths |= source->label_path_count != 0;
        all_incoming_pure_labels &= source->is_label_value && !source->has_label_provenance && !source_non_label && !source->label_path_count;
        for (u32 block_index = 0; block_index < source->label_block_count; block_index += 1)
        {
            bool found = false;
            for (u32 previous_index = 0; previous_index < incoming_index; previous_index += 1)
            {
                found |= ir_test_oracle_contains(incoming[previous_index].label_blocks, incoming[previous_index].label_block_count, source->label_blocks[block_index]);
            }
            incoming_block_count += found ? 0 : 1;
        }
        for (u32 path_index = 0; path_index < source->label_path_count; path_index += 1)
        {
            IrLabelProvenancePath* source_path = source->label_paths + path_index;
            bool found = false;
            for (u32 destination_index = 0; destination_index < destination->label_path_count; destination_index += 1)
            {
                IrLabelProvenancePath* destination_path = destination->label_paths + destination_index;
                bool blocks_match = source_path->is_non_label ||
                                    (!destination_path->is_non_label && source_path->label_block_count <= destination_path->label_block_count);
                if (blocks_match && destination_path->offset == source_path->offset && destination_path->size == source_path->size)
                {
                    found |= ir_test_oracle_path_contains_all(destination_path, source_path);
                }
            }
            valid &= found;
        }
    }
    bool destination_has_labels = destination->label_block_count != 0 || destination->is_label_value || destination->has_label_provenance || destination->label_path_count != 0;
    if (destination->is_label_value)
    {
        valid &= all_incoming_pure_labels && !destination->has_non_label_provenance && !destination->has_label_provenance && destination->label_path_count == 0;
    }
    else
    {
        valid &= destination_has_labels == incoming_label && destination->has_label_provenance == (incoming_block_count != 0) &&
                 destination->has_non_label_provenance == incoming_non_label;
    }
    valid &= destination->label_block_count == incoming_block_count && (incoming_paths || !destination->label_path_count);
    for (u32 block_index = 0; valid && block_index < destination->label_block_count; block_index += 1)
    {
        bool found = false;
        for (u32 incoming_index = 0; incoming_index < incoming_count; incoming_index += 1)
        {
            found |= ir_test_oracle_contains(incoming[incoming_index].label_blocks, incoming[incoming_index].label_block_count, destination->label_blocks[block_index]);
        }
        valid = found;
    }
    for (u32 destination_index = 0; valid && destination_index < destination->label_path_count; destination_index += 1)
    {
        IrLabelProvenancePath* destination_path = destination->label_paths + destination_index;
        bool incoming_path = false;
        bool incoming_non_label_path = false;
        bool incoming_label_path = false;
        for (u32 incoming_index = 0; incoming_index < incoming_count; incoming_index += 1)
        {
            for (u32 path_index = 0; path_index < incoming[incoming_index].label_path_count; path_index += 1)
            {
                IrLabelProvenancePath* source_path = incoming[incoming_index].label_paths + path_index;
                if (source_path->offset == destination_path->offset && source_path->size == destination_path->size)
                {
                    incoming_path = true;
                    incoming_non_label_path |= source_path->is_non_label;
                    incoming_label_path |= !source_path->is_non_label && source_path->label_block_count != 0;
                }
            }
        }
        valid = incoming_path && (destination_path->is_non_label ? incoming_non_label_path && !incoming_label_path : incoming_label_path);
        for (u32 block_index = 0; valid && !destination_path->is_non_label && block_index < destination_path->label_block_count; block_index += 1)
        {
            bool found = false;
            for (u32 incoming_index = 0; incoming_index < incoming_count; incoming_index += 1)
            {
                for (u32 path_index = 0; path_index < incoming[incoming_index].label_path_count; path_index += 1)
                {
                    IrLabelProvenancePath* source_path = incoming[incoming_index].label_paths + path_index;
                    found |= source_path->offset == destination_path->offset && source_path->size == destination_path->size && !source_path->is_non_label &&
                             ir_test_oracle_contains(source_path->label_blocks, source_path->label_block_count, destination_path->label_blocks[block_index]);
                }
            }
            valid = found;
        }
    }
    return valid;
}

// ---- Fixtures --------------------------------------------------------------

// Value layout: operands 0..operand_count-1, result at operand_count. Every
// value carries metadata, so function->label_metadata_count is never zero.
typedef struct IrTestTransferFixture IrTestTransferFixture;
struct IrTestTransferFixture
{
    IrProgram program;
    IrFunction function;
    IrType types[2];
    IrValue* values;
    IrValueId* metadata_values;
    IrValueLabelMetadata* metadata;
    IrValueId* operands;
    IrInstruction instruction;
    IrIncoming* incoming;
    IrBlockParameter parameter;
};

BUSTER_GLOBAL_LOCAL void ir_test_transfer_fixture(IrTestTransferFixture* fixture, Arena* arena, IrOpcode opcode, u32 operand_count, u64 element_count,
                                                  u64 element_size)
{
    *fixture = (IrTestTransferFixture){0};
    u32 value_count = operand_count + 1;
    fixture->values = arena_allocate(arena, IrValue, value_count);
    fixture->metadata_values = arena_allocate(arena, IrValueId, value_count);
    fixture->metadata = arena_allocate(arena, IrValueLabelMetadata, value_count);
    fixture->operands = arena_allocate(arena, IrValueId, operand_count ? operand_count : 1);
    fixture->incoming = arena_allocate(arena, IrIncoming, operand_count ? operand_count : 1);
    memset(fixture->metadata, 0, sizeof(*fixture->metadata) * value_count);
    // Type 0 is the aggregate (or array being indexed), type 1 its element.
    fixture->types[0] = (IrType){.kind = IR_TYPE_ARRAY, .element_type = {.value = 1}, .element_count = element_count,
        .layout = {.size = element_count * element_size, .alignment = 8, .resolved = true}};
    fixture->types[1] = (IrType){.kind = opcode == IR_OPCODE_INDEX ? IR_TYPE_POINTER : IR_TYPE_STRUCT,
        .layout = {.size = element_size, .alignment = 8, .resolved = true}};
    for (u32 index = 0; index < value_count; index += 1)
    {
        bool result = index == operand_count;
        // INDEX: operand 0 is the array, operand 1 a non-constant index and the
        // result an element. ARRAY: operands are elements, the result the array.
        u32 type = opcode == IR_OPCODE_INDEX ? (index == 0 ? 0 : 1) : (result ? 0 : 1);
        fixture->values[index] = (IrValue){.canonical_type = {.value = type},
            .definition = result ? (IrInstructionId){.value = 0} : IR_INSTRUCTION_ID_INVALID, .category = IR_VALUE_PLACE};
        fixture->metadata_values[index] = (IrValueId){.value = index};
    }
    for (u32 index = 0; index < operand_count; index += 1)
    {
        fixture->operands[index] = (IrValueId){.value = index};
        fixture->incoming[index] = (IrIncoming){.value = {.value = index}, .predecessor = {.value = index}};
        if (index + 1 < operand_count)
        {
            fixture->incoming[index].next = fixture->incoming + index + 1;
        }
    }
    fixture->instruction = (IrInstruction){.opcode = (u8)opcode, .canonical_type = {.value = 0}, .result = {.value = operand_count},
        .operands = fixture->operands, .operand_count = operand_count};
    fixture->function = (IrFunction){.values = fixture->values, .value_count = value_count, .instructions = &fixture->instruction, .instruction_count = 1,
        .label_metadata_values = fixture->metadata_values, .label_metadata = fixture->metadata, .label_metadata_count = value_count, .block_count = 1u << 20};
    fixture->program = (IrProgram){.arena = arena, .types = {.types = fixture->types, .count = 2}, .data_layout = {.pointer = {.size = 8}}};
    fixture->parameter = (IrBlockParameter){.value = {.value = operand_count}, .canonical_type = {.value = 1}, .incoming_count = operand_count,
        .first_incoming = operand_count ? fixture->incoming : 0, .last_incoming = operand_count ? fixture->incoming + operand_count - 1 : 0};
}

BUSTER_GLOBAL_LOCAL bool ir_test_transfer_result(IrTestTransferFixture* fixture)
{
    return ir_label_metadata_transfer_valid(&fixture->program, &fixture->function, (IrValueId){.value = fixture->function.value_count - 1});
}

// ---- Constructors that make the interesting cases likely valid ---------------

BUSTER_GLOBAL_LOCAL void ir_test_transfer_derive_dynamic(IrTestTransferMeta* result, IrTestTransferMeta* source, u64 element_size)
{
    *result = (IrTestTransferMeta){0};
    IrValueLabelMetadata* value = &result->metadata;
    bool any_label = false;
    bool any_non_label = source->metadata.has_non_label_provenance;
    for (u32 index = 0; index < source->metadata.label_path_count; index += 1)
    {
        any_label |= !source->paths[index].is_non_label;
        any_non_label |= source->paths[index].is_non_label;
    }
    if (source->metadata.label_path_count)
    {
        value->label_path_count = 1;
        result->paths[0] = (IrLabelProvenancePath){.size = element_size, .is_non_label = !any_label};
        for (u32 path_index = 0; path_index < source->metadata.label_path_count; path_index += 1)
        {
            for (u32 block_index = 0; !source->paths[path_index].is_non_label && block_index < source->paths[path_index].label_block_count; block_index += 1)
            {
                IrBlockId block = source->path_blocks[path_index][block_index];
                bool found = false;
                for (u32 index = 0; index < result->paths[0].label_block_count; index += 1)
                {
                    found |= result->path_blocks[0][index].value == block.value;
                }
                if (!found && result->paths[0].label_block_count < IR_TEST_TRANSFER_BLOCKS)
                {
                    result->path_blocks[0][result->paths[0].label_block_count] = block;
                    result->paths[0].label_block_count += 1;
                }
            }
        }
    }
    value->label_block_count = source->metadata.label_block_count;
    memcpy(result->blocks, source->blocks, sizeof(result->blocks));
    value->has_label_provenance = source->metadata.has_label_provenance;
    value->has_non_label_provenance = any_non_label;
    ir_test_transfer_finalize(result);
}

BUSTER_GLOBAL_LOCAL void ir_test_transfer_derive_aggregate(IrTestTransferMeta* result, IrTestTransferMeta* sources, u32 operand_count, u64 element_size)
{
    *result = (IrTestTransferMeta){0};
    IrValueLabelMetadata* value = &result->metadata;
    for (u32 operand_index = 0; operand_index < operand_count; operand_index += 1)
    {
        IrTestTransferMeta* source = sources + operand_index;
        value->has_non_label_provenance |= source->metadata.has_non_label_provenance;
        for (u32 path_index = 0; path_index < source->metadata.label_path_count && value->label_path_count < IR_TEST_TRANSFER_PATHS; path_index += 1)
        {
            u32 target = value->label_path_count;
            result->paths[target] = source->paths[path_index];
            result->paths[target].offset += operand_index * element_size;
            memcpy(result->path_blocks[target], source->path_blocks[path_index], sizeof(result->path_blocks[target]));
            value->has_non_label_provenance |= source->paths[path_index].is_non_label;
            value->has_label_provenance |= !source->paths[path_index].is_non_label;
            value->label_path_count += 1;
        }
        if (source->metadata.is_label_value && value->label_path_count < IR_TEST_TRANSFER_PATHS)
        {
            u32 target = value->label_path_count;
            result->paths[target] = (IrLabelProvenancePath){.offset = operand_index * element_size, .size = element_size,
                .label_block_count = source->metadata.label_block_count};
            memcpy(result->path_blocks[target], source->blocks, sizeof(source->blocks));
            value->has_label_provenance = true;
            value->label_path_count += 1;
        }
    }
    ir_test_transfer_union_blocks(result);
    ir_test_transfer_finalize(result);
}

BUSTER_GLOBAL_LOCAL void ir_test_transfer_derive_parameter(IrTestTransferMeta* destination, IrTestTransferMeta* incoming, u32 incoming_count)
{
    *destination = (IrTestTransferMeta){0};
    IrValueLabelMetadata* value = &destination->metadata;
    bool pure = true;
    for (u32 incoming_index = 0; incoming_index < incoming_count; incoming_index += 1)
    {
        IrTestTransferMeta* source = incoming + incoming_index;
        bool non_label = ir_test_oracle_non_label_path(&source->metadata);
        pure &= source->metadata.is_label_value && !source->metadata.has_label_provenance && !non_label && !source->metadata.label_path_count;
        value->has_non_label_provenance |= non_label;
        for (u32 block_index = 0; block_index < source->metadata.label_block_count; block_index += 1)
        {
            bool found = false;
            for (u32 previous = 0; previous < incoming_index; previous += 1)
            {
                found |= ir_test_oracle_contains(incoming[previous].blocks, incoming[previous].metadata.label_block_count, source->blocks[block_index]);
            }
            if (!found && value->label_block_count < IR_TEST_TRANSFER_BLOCKS)
            {
                destination->blocks[value->label_block_count] = source->blocks[block_index];
                value->label_block_count += 1;
            }
        }
        for (u32 path_index = 0; path_index < source->metadata.label_path_count; path_index += 1)
        {
            IrLabelProvenancePath* source_path = source->paths + path_index;
            u32 target = 0;
            while (target < value->label_path_count && (destination->paths[target].offset != source_path->offset || destination->paths[target].size != source_path->size))
            {
                target += 1;
            }
            if (target == value->label_path_count && target < IR_TEST_TRANSFER_PATHS)
            {
                destination->paths[target] = (IrLabelProvenancePath){.offset = source_path->offset, .size = source_path->size, .is_non_label = true};
                value->label_path_count += 1;
            }
            if (target < IR_TEST_TRANSFER_PATHS && !source_path->is_non_label)
            {
                destination->paths[target].is_non_label = false;
                for (u32 block_index = 0; block_index < source_path->label_block_count; block_index += 1)
                {
                    IrLabelProvenancePath* path = destination->paths + target;
                    if (!ir_test_oracle_contains(destination->path_blocks[target], path->label_block_count, source->path_blocks[path_index][block_index]) &&
                        path->label_block_count < IR_TEST_TRANSFER_BLOCKS)
                    {
                        destination->path_blocks[target][path->label_block_count] = source->path_blocks[path_index][block_index];
                        path->label_block_count += 1;
                    }
                }
            }
        }
    }
    value->is_label_value = pure && incoming_count;
    value->has_label_provenance = !value->is_label_value && value->label_block_count != 0;
    ir_test_transfer_finalize(destination);
}

// ---- Differential and scaling fixture -------------------------------------

#if BUSTER_BENCH_ALLOCATIONS
typedef struct IrTestTransferWork IrTestTransferWork;
struct IrTestTransferWork
{
    u64 work;
    u64 scratch;
    bool overflowed;
};

BUSTER_GLOBAL_LOCAL IrTestTransferWork ir_test_transfer_work(IrConstructionCounters before, IrConstructionCounters after)
{
    IrTestTransferWork work = {0};
    work.work = after.values[IR_CONSTRUCTION_VALIDATION_LABEL_SET_WORK] - before.values[IR_CONSTRUCTION_VALIDATION_LABEL_SET_WORK] +
                after.values[IR_CONSTRUCTION_VALIDATION_LABEL_PATH_WORK] - before.values[IR_CONSTRUCTION_VALIDATION_LABEL_PATH_WORK];
    work.scratch = after.values[IR_CONSTRUCTION_VALIDATION_LABEL_SCRATCH_BYTES] - before.values[IR_CONSTRUCTION_VALIDATION_LABEL_SCRATCH_BYTES];
    work.overflowed = before.overflowed || after.overflowed;
    return work;
}
#endif

BUSTER_GLOBAL_LOCAL void ir_test_transfer_set_metadata(IrTestTransferFixture* fixture, u32 value, IrTestTransferMeta* meta)
{
    fixture->metadata[value] = meta->metadata;
}

typedef struct IrTestTransferCounts IrTestTransferCounts;
struct IrTestTransferCounts
{
    u32 accepted;
    u32 rejected;
};

// Small shapes stay on the insertion-sort and linear-chain paths; the wide
// shapes push every index past IR_LABEL_SMALL_SET_COUNT into the radix path.
BUSTER_GLOBAL_LOCAL UnitTestResult ir_test_transfer_differential(UnitTestArguments* arguments, u32 operand_count, u32 iterations, u32 maximum_paths)
{
    UnitTestResult result = {0};
    IrTestRandom random = {.state = UINT64_C(0x9e3779b97f4a7c15) + operand_count};
    IrTestTransferCounts counts[3] = {0};
    u32 element_count = 4 * operand_count;
    TemporalArena temporary = arena_begin_temporal(arguments->arena);
    // Every case leaves the calling-thread scratch arena where it found it, on
    // acceptance and on every refusal alike.
    TemporalArena measurement = scratch_begin(0, 0);
    IrTestTransferMeta* metas = arena_allocate(arguments->arena, IrTestTransferMeta, IR_TEST_TRANSFER_OPERANDS);
    IrTestTransferMeta* derived = arena_allocate(arguments->arena, IrTestTransferMeta, 1);
    IrValueLabelMetadata* metadata = arena_allocate(arguments->arena, IrValueLabelMetadata, IR_TEST_TRANSFER_OPERANDS);
    for (u32 iteration = 0; iteration < iterations; iteration += 1)
    {
        {
            ir_test_transfer_random_meta(&random, metas, element_count, 8, false, maximum_paths);
            if (ir_test_transfer_random(&random, 4))
            {
                ir_test_transfer_derive_dynamic(derived, metas, 8);
                if (ir_test_transfer_random(&random, 2))
                {
                    ir_test_transfer_mutate(&random, derived);
                }
            }
            else
            {
                ir_test_transfer_random_meta(&random, derived, 1, 8, false, 2);
            }
            TemporalArena iteration_temporary = arena_begin_temporal(arguments->arena);
            IrTestTransferFixture fixture;
            ir_test_transfer_fixture(&fixture, arguments->arena, IR_OPCODE_INDEX, 2, element_count, 8);
            ir_test_transfer_set_metadata(&fixture, 0, metas);
            ir_test_transfer_set_metadata(&fixture, 2, derived);
            bool expected = ir_test_oracle_index(&derived->metadata, &metas->metadata, (u64)element_count * 8, 8);
            bool actual = ir_test_transfer_result(&fixture);
            BUSTER_TEST(arguments, actual == expected);
            BUSTER_TEST(arguments, measurement.arena->position == measurement.position);
            counts[0].accepted += expected;
            counts[0].rejected += !expected;
            scratch_end(iteration_temporary);
        }
        {
            for (u32 index = 0; index < operand_count; index += 1)
            {
                ir_test_transfer_random_meta(&random, metas + index, 2, 8, true, 2);
            }
            if (ir_test_transfer_random(&random, 4))
            {
                ir_test_transfer_derive_aggregate(derived, metas, operand_count, 16);
                if (ir_test_transfer_random(&random, 2))
                {
                    ir_test_transfer_mutate(&random, derived);
                }
            }
            else
            {
                ir_test_transfer_random_meta(&random, derived, 2 * operand_count, 8, false, maximum_paths);
            }
            for (u32 index = 0; index < operand_count; index += 1)
            {
                metadata[index] = metas[index].metadata;
            }
            TemporalArena iteration_temporary = arena_begin_temporal(arguments->arena);
            IrTestTransferFixture fixture;
            ir_test_transfer_fixture(&fixture, arguments->arena, IR_OPCODE_ARRAY, operand_count, operand_count, 16);
            for (u32 index = 0; index < operand_count; index += 1)
            {
                ir_test_transfer_set_metadata(&fixture, index, metas + index);
            }
            ir_test_transfer_set_metadata(&fixture, operand_count, derived);
            bool expected = ir_test_oracle_aggregate(&derived->metadata, metadata, operand_count, 16);
            bool actual = ir_test_transfer_result(&fixture);
            BUSTER_TEST(arguments, actual == expected);
            BUSTER_TEST(arguments, measurement.arena->position == measurement.position);
            counts[1].accepted += expected;
            counts[1].rejected += !expected;
            scratch_end(iteration_temporary);
        }
        {
            for (u32 index = 0; index < operand_count; index += 1)
            {
                ir_test_transfer_random_meta(&random, metas + index, 3, 8, true, 3);
            }
            if (ir_test_transfer_random(&random, 4))
            {
                ir_test_transfer_derive_parameter(derived, metas, operand_count);
                if (ir_test_transfer_random(&random, 2))
                {
                    ir_test_transfer_mutate(&random, derived);
                }
            }
            else
            {
                ir_test_transfer_random_meta(&random, derived, 3, 8, true, 3);
            }
            for (u32 index = 0; index < operand_count; index += 1)
            {
                metadata[index] = metas[index].metadata;
            }
            TemporalArena iteration_temporary = arena_begin_temporal(arguments->arena);
            IrTestTransferFixture fixture;
            ir_test_transfer_fixture(&fixture, arguments->arena, IR_OPCODE_ARRAY, operand_count, operand_count, 16);
            for (u32 index = 0; index < operand_count; index += 1)
            {
                ir_test_transfer_set_metadata(&fixture, index, metas + index);
            }
            ir_test_transfer_set_metadata(&fixture, operand_count, derived);
            bool expected = ir_test_oracle_parameter(&derived->metadata, metadata, operand_count);
            bool actual = ir_label_block_parameter_provenance_valid(&fixture.function, &fixture.parameter);
            BUSTER_TEST(arguments, actual == expected);
            BUSTER_TEST(arguments, measurement.arena->position == measurement.position);
            counts[2].accepted += expected;
            counts[2].rejected += !expected;
            scratch_end(iteration_temporary);
        }
    }
    scratch_end(measurement);
    // The controls must exercise both outcomes of every validator.
    for (u32 index = 0; index < 3; index += 1)
    {
        BUSTER_TEST(arguments, counts[index].accepted >= iterations / 50 && counts[index].rejected >= iterations / 50);
    }
    scratch_end(temporary);
    return result;
}

// ---- Geometric whole-validator controls ---------------------------------------

#if BUSTER_BENCH_ALLOCATIONS
BUSTER_GLOBAL_LOCAL IrBlockId* ir_test_transfer_shuffled_ids(Arena* arena, u32 count, u32 multiplier)
{
    IrBlockId* ids = arena_allocate(arena, IrBlockId, count);
    for (u32 index = 0; index < count; index += 1)
    {
        // multiplier is odd and count a power of two, so this permutes 0..count-1.
        ids[index].value = (index * multiplier + 1) & (count - 1);
    }
    return ids;
}

// One path per array element. Every path names blocks_per_path of the rows
// distinct IDs; the single result path names all of them. A linear contains per
// path block would be quadratic in rows here.
BUSTER_GLOBAL_LOCAL void ir_test_transfer_dynamic_rows(IrTestTransferFixture* fixture, Arena* arena, u32 rows, u32 blocks_per_path, u32 fault)
{
    ir_test_transfer_fixture(fixture, arena, IR_OPCODE_INDEX, 2, rows, 8);
    IrBlockId* all = ir_test_transfer_shuffled_ids(arena, rows, 5);
    IrBlockId* result_blocks = arena_allocate(arena, IrBlockId, rows);
    IrBlockId* path_blocks = arena_allocate(arena, IrBlockId, (u64)rows * blocks_per_path);
    IrLabelProvenancePath* source_paths = arena_allocate(arena, IrLabelProvenancePath, rows);
    IrLabelProvenancePath* result_path = arena_allocate(arena, IrLabelProvenancePath, 1);
    for (u32 index = 0; index < rows; index += 1)
    {
        for (u32 block_index = 0; block_index < blocks_per_path; block_index += 1)
        {
            path_blocks[(u64)index * blocks_per_path + block_index].value = (index * 3 + block_index) & (rows - 1);
        }
        // Reverse offsets force the immutable index to be built unordered.
        source_paths[index] = (IrLabelProvenancePath){.offset = (u64)(rows - 1 - index) * 8, .size = 8,
            .label_blocks = path_blocks + (u64)index * blocks_per_path, .label_block_count = blocks_per_path};
    }
    u32 result_count = 0;
    for (u32 index = 0; index < rows; index += 1)
    {
        // Fault 1 drops the first block of path 0 from the result path.
        if (fault != 1 || all[index].value != path_blocks[0].value)
        {
            result_blocks[result_count] = all[index];
            result_count += 1;
        }
    }
    *result_path = (IrLabelProvenancePath){.offset = 0, .size = 8, .label_blocks = result_blocks, .label_block_count = result_count};
    fixture->metadata[0] = (IrValueLabelMetadata){.label_blocks = all, .label_block_count = rows, .label_paths = source_paths, .label_path_count = rows,
        .has_label_provenance = true};
    fixture->metadata[2] = (IrValueLabelMetadata){.label_blocks = all, .label_block_count = fault == 2 ? rows - 1 : rows, .label_paths = result_path,
        .label_path_count = 1, .has_label_provenance = true};
}

// operands elements of paths_per_operand 8-byte paths; every path names four
// blocks of a shared pool and the result lists every path in reverse order.
BUSTER_GLOBAL_LOCAL void ir_test_transfer_aggregate_rows(IrTestTransferFixture* fixture, Arena* arena, u32 operands, u32 paths_per_operand, u32 fault)
{
    enum { BLOCKS_PER_PATH = 4 };
    u64 element_size = (u64)paths_per_operand * 8;
    u32 rows = operands * paths_per_operand;
    ir_test_transfer_fixture(fixture, arena, IR_OPCODE_ARRAY, operands, operands, element_size);
    IrBlockId* pool = ir_test_transfer_shuffled_ids(arena, rows * BLOCKS_PER_PATH, 5);
    IrLabelProvenancePath* source_paths = arena_allocate(arena, IrLabelProvenancePath, rows);
    IrLabelProvenancePath* result_paths = arena_allocate(arena, IrLabelProvenancePath, rows);
    for (u32 operand_index = 0; operand_index < operands; operand_index += 1)
    {
        for (u32 path_index = 0; path_index < paths_per_operand; path_index += 1)
        {
            u32 row = operand_index * paths_per_operand + path_index;
            IrBlockId* blocks = pool + (u64)row * BLOCKS_PER_PATH;
            u32 reversed = operand_index * paths_per_operand + (paths_per_operand - 1 - path_index);
            source_paths[reversed] = (IrLabelProvenancePath){.offset = (u64)path_index * 8, .size = 8, .label_blocks = blocks,
                .label_block_count = BLOCKS_PER_PATH};
            result_paths[rows - 1 - row] = (IrLabelProvenancePath){.offset = operand_index * element_size + (u64)path_index * 8, .size = 8,
                .label_blocks = blocks, .label_block_count = BLOCKS_PER_PATH};
        }
        fixture->metadata[operand_index] = (IrValueLabelMetadata){.label_blocks = pool + (u64)operand_index * paths_per_operand * BLOCKS_PER_PATH,
            .label_block_count = paths_per_operand * BLOCKS_PER_PATH, .label_paths = source_paths + (u64)operand_index * paths_per_operand,
            .label_path_count = paths_per_operand, .has_label_provenance = true};
    }
    fixture->metadata[operands] = (IrValueLabelMetadata){.label_blocks = pool, .label_block_count = rows * BLOCKS_PER_PATH, .label_paths = result_paths,
        .label_path_count = rows, .has_label_provenance = true};
    if (fault == 1)
    {
        result_paths[rows / 2].label_blocks = pool + (u64)((rows / 2 + 1) % rows) * BLOCKS_PER_PATH;
    }
    if (fault == 2)
    {
        result_paths[rows / 2].size = 4;
    }
}

// incoming values hold disjoint sets of ids_per_incoming IDs and the parameter
// holds their union. Direct form uses label values, path form one shared
// (0, 8) storage path per value.
BUSTER_GLOBAL_LOCAL void ir_test_transfer_parameter_rows(IrTestTransferFixture* fixture, Arena* arena, u32 incoming, u32 ids_per_incoming, bool paths, u32 fault)
{
    u32 total = incoming * ids_per_incoming;
    ir_test_transfer_fixture(fixture, arena, IR_OPCODE_ARRAY, incoming, incoming, 16);
    IrBlockId* all = ir_test_transfer_shuffled_ids(arena, total, 5);
    IrBlockId* destination_blocks = arena_allocate(arena, IrBlockId, total);
    IrBlockId* destination_path_blocks = arena_allocate(arena, IrBlockId, total);
    IrLabelProvenancePath* incoming_paths = arena_allocate(arena, IrLabelProvenancePath, incoming);
    IrLabelProvenancePath* destination_path = arena_allocate(arena, IrLabelProvenancePath, 1);
    memcpy(destination_blocks, all, sizeof(*all) * total);
    memcpy(destination_path_blocks, all, sizeof(*all) * total);
    for (u32 incoming_index = 0; incoming_index < incoming; incoming_index += 1)
    {
        IrBlockId* blocks = all + (u64)incoming_index * ids_per_incoming;
        incoming_paths[incoming_index] = (IrLabelProvenancePath){.offset = 0, .size = 8, .label_blocks = blocks, .label_block_count = ids_per_incoming};
        fixture->metadata[incoming_index] = (IrValueLabelMetadata){.label_blocks = blocks, .label_block_count = ids_per_incoming,
            .label_paths = paths ? incoming_paths + incoming_index : 0, .label_path_count = paths ? 1 : 0,
            .is_label_value = !paths, .has_label_provenance = paths};
    }
    // Fault 1 names an ID no incoming value holds, with the count unchanged.
    destination_blocks[total - 1].value = fault == 1 ? total + 7 : destination_blocks[total - 1].value;
    *destination_path = (IrLabelProvenancePath){.offset = 0, .size = 8, .label_blocks = destination_path_blocks,
        .label_block_count = fault == 2 ? total - 1 : total};
    fixture->metadata[incoming] = (IrValueLabelMetadata){.label_blocks = destination_blocks, .label_block_count = fault == 3 ? total - 1 : total,
        .label_paths = paths ? destination_path : 0, .label_path_count = paths ? 1 : 0, .is_label_value = !paths, .has_label_provenance = paths};
}

typedef enum IrTestTransferShape
{
    IR_TEST_TRANSFER_DYNAMIC,
    IR_TEST_TRANSFER_AGGREGATE_WIDE,
    IR_TEST_TRANSFER_AGGREGATE_DEEP,
    IR_TEST_TRANSFER_PARAMETER_DIRECT,
    IR_TEST_TRANSFER_PARAMETER_PATHS,
    IR_TEST_TRANSFER_SHAPE_COUNT,
} IrTestTransferShape;

BUSTER_GLOBAL_LOCAL void ir_test_transfer_build(IrTestTransferFixture* fixture, Arena* arena, IrTestTransferShape shape, u32 rows, u32 fault)
{
    switch (shape)
    {
    case IR_TEST_TRANSFER_DYNAMIC: ir_test_transfer_dynamic_rows(fixture, arena, rows, 8, fault); break;
    case IR_TEST_TRANSFER_AGGREGATE_WIDE: ir_test_transfer_aggregate_rows(fixture, arena, rows / 4, 4, fault); break;
    case IR_TEST_TRANSFER_AGGREGATE_DEEP: ir_test_transfer_aggregate_rows(fixture, arena, 1, rows, fault); break;
    case IR_TEST_TRANSFER_PARAMETER_DIRECT: ir_test_transfer_parameter_rows(fixture, arena, rows / 8, 8, false, fault); break;
    case IR_TEST_TRANSFER_PARAMETER_PATHS: ir_test_transfer_parameter_rows(fixture, arena, rows / 8, 8, true, fault); break;
    default: BUSTER_TODO(); break;
    }
}

BUSTER_GLOBAL_LOCAL bool ir_test_transfer_run(IrTestTransferFixture* fixture, IrTestTransferShape shape)
{
    bool result = shape == IR_TEST_TRANSFER_PARAMETER_DIRECT || shape == IR_TEST_TRANSFER_PARAMETER_PATHS
                      ? ir_label_block_parameter_provenance_valid(&fixture->function, &fixture->parameter)
                      : ir_test_transfer_result(fixture);
    return result;
}
#endif

BUSTER_GLOBAL_LOCAL UnitTestResult ir_test_label_transfer(UnitTestArguments* arguments)
{
    UnitTestResult result = ir_test_transfer_differential(arguments, 3, 6000, 3);
    UnitTestResult wide = ir_test_transfer_differential(arguments, IR_TEST_TRANSFER_OPERANDS, 1500, 6);
    result.test_count += wide.test_count;
    result.succeeded_test_count += wide.succeeded_test_count;
#if BUSTER_BENCH_ALLOCATIONS
    for (u32 shape = 0; shape < IR_TEST_TRANSFER_SHAPE_COUNT; shape += 1)
    {
        u64 previous_work = 0;
        u64 previous_scratch = 0;
        for (u32 rows = 256; rows <= 4096; rows *= 4)
        {
            TemporalArena temporary = arena_begin_temporal(arguments->arena);
            TemporalArena measurement = scratch_begin(0, 0);
            IrTestTransferFixture fixture;
            ir_test_transfer_build(&fixture, arguments->arena, (IrTestTransferShape)shape, rows, 0);
            IrConstructionCounters before = ir_construction_counters();
            bool valid = ir_test_transfer_run(&fixture, (IrTestTransferShape)shape);
            IrConstructionCounters after = ir_construction_counters();
            IrTestTransferWork work = ir_test_transfer_work(before, after);
            BUSTER_TEST(arguments, valid && !work.overflowed);
            BUSTER_TEST(arguments, measurement.arena->position == measurement.position);
            // Near-linear: a few passes over the rows, with a log factor for
            // binary-search queries, and no more than 6x per 4x rows.
            BUSTER_TEST(arguments, work.work >= rows && work.work <= 400 * (u64)rows);
            BUSTER_TEST(arguments, work.scratch <= 256 * (u64)rows);
            BUSTER_TEST(arguments, !previous_work || work.work <= 6 * previous_work);
            BUSTER_TEST(arguments, !previous_scratch || work.scratch <= 9 * previous_scratch / 2);
            previous_work = work.work;
            previous_scratch = work.scratch;
            // Each neighbour breaks one clause and is refused, releasing scratch.
            for (u32 fault = 1; fault <= 3; fault += 1)
            {
                bool applies = (shape == IR_TEST_TRANSFER_DYNAMIC && fault <= 2) || (shape >= IR_TEST_TRANSFER_AGGREGATE_WIDE &&
                               shape <= IR_TEST_TRANSFER_AGGREGATE_DEEP && fault <= 2) ||
                               shape == IR_TEST_TRANSFER_PARAMETER_DIRECT || shape == IR_TEST_TRANSFER_PARAMETER_PATHS;
                if (applies && !(shape == IR_TEST_TRANSFER_PARAMETER_DIRECT && fault == 2))
                {
                    TemporalArena fault_temporary = arena_begin_temporal(arguments->arena);
                    IrTestTransferFixture neighbour;
                    ir_test_transfer_build(&neighbour, arguments->arena, (IrTestTransferShape)shape, rows, fault);
                    BUSTER_TEST(arguments, !ir_test_transfer_run(&neighbour, (IrTestTransferShape)shape));
                    BUSTER_TEST(arguments, measurement.arena->position == measurement.position);
                    scratch_end(fault_temporary);
                }
            }
            scratch_end(measurement);
            scratch_end(temporary);
        }
    }
#endif
    return result;
}
