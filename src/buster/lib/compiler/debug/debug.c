#include <buster/lib/compiler/debug/debug.h>

#include <buster/lib/compiler/ir/ir.h>
#include <buster/lib/compiler/ir/ir_construction.h>
#include <buster/lib/hash.h>
#include <buster/lib/string.h>

BUSTER_GLOBAL_LOCAL String8 debug_string(Arena* arena, String8 string)
{
    return string.length ? string_duplicate_arena(arena, string, false) : (String8){0};
}

// One of the four places a range's line and column are worth recovering: a
// declaration site that reaches a DWARF DIE or a CodeView record.
BUSTER_GLOBAL_LOCAL DebugSourceLocation debug_source_from_ir(Arena* arena, IrProgram* program, IrSourceRange range)
{
    (void)arena;
    IrSourcePosition position = ir_source_position(program, range);
    DebugSourceLocation result = {
        .line = position.line ? position.line : 1,
        .column = position.column ? position.column : 1,
        .offset = position.offset,
        .length = range.length,
    };
    if (program && range.source.value < program->sources.count)
    {
        result.source = range.source.value;
    }
    return result;
}


BUSTER_GLOBAL_LOCAL DebugTypeKind debug_type_kind_from_ir(IrTypeKind kind)
{
    switch (kind)
    {
    case IR_TYPE_VOID: return DEBUG_TYPE_VOID;
    case IR_TYPE_BOOLEAN:
    case IR_TYPE_INTEGER:
    case IR_TYPE_FLOAT:
    case IR_TYPE_VA_LIST: return DEBUG_TYPE_BASE;
    case IR_TYPE_POINTER:
    case IR_TYPE_SLICE:
    case IR_TYPE_RANGE: return DEBUG_TYPE_POINTER;
    case IR_TYPE_ARRAY: return DEBUG_TYPE_ARRAY;
    case IR_TYPE_VECTOR: return DEBUG_TYPE_VECTOR;
    case IR_TYPE_FUNCTION: return DEBUG_TYPE_FUNCTION;
    case IR_TYPE_STRUCT: return DEBUG_TYPE_STRUCT;
    case IR_TYPE_UNION: return DEBUG_TYPE_UNION;
    case IR_TYPE_ENUM: return DEBUG_TYPE_ENUM;
    case IR_TYPE_COUNT: break;
    }
    return DEBUG_TYPE_BASE;
}




BUSTER_GLOBAL_LOCAL DebugTypeId debug_canonical_type_id(DebugModel* model, IrTypeId type)
{
    return type.value < model->type_count ? type.value : DEBUG_ID_INVALID;
}

BUSTER_GLOBAL_LOCAL DebugTypeField* debug_copy_ir_fields(Arena* arena, IrProgram* program, IrField* fields, u32 count)
{
    DebugTypeField* result = count ? arena_allocate(arena, DebugTypeField, count) : 0;
    for (u32 index = 0; index < count; index += 1)
    {
        IrField* field = fields + index;
        result[index] = (DebugTypeField){
            .name = debug_string(arena, field->name),
            .type = field->type.value,
            .declaration = debug_source_from_ir(arena, program, field->source),
            .offset = field->offset,
            .bit_offset = field->bit_offset,
            .bit_width = field->bit_width,
            .is_bit_field = field->is_bit_field,
        };
    }
    return result;
}

BUSTER_GLOBAL_LOCAL DebugEnumMember* debug_copy_ir_enum_members(Arena* arena, IrProgram* program, IrEnumMember* members, u32 count)
{
    DebugEnumMember* result = count ? arena_allocate(arena, DebugEnumMember, count) : 0;
    for (u32 index = 0; index < count; index += 1)
    {
        IrEnumMember* member = members + index;
        result[index] = (DebugEnumMember){
            .name = debug_string(arena, member->name),
            .declaration = debug_source_from_ir(arena, program, member->source),
            .value = member->value,
        };
    }
    return result;
}

BUSTER_GLOBAL_LOCAL void debug_fill_ir_type(Arena* arena, DebugModel* model, IrProgram* program, IrType* source, DebugType* result)
{
    // Canonical lowering retains volatile object types with their operand,
    // but erases const. Atomic types have different layout, so only a plain
    // volatile type can be emitted as a qualifier of its operand.
    String8 name = debug_string(arena, source->name);
    bool volatile_wrapper = source->is_volatile && !source->is_atomic &&
                            source->unqualified_type.value < model->type_count && source->unqualified_type.value != source->id.value;
    *result = (DebugType){
        .name = name,
        .declaration_name = name,
        .canonical_type = source->id,
        .unqualified_type = (source->is_atomic || volatile_wrapper) && source->unqualified_type.value < model->type_count
                                ? source->unqualified_type.value : DEBUG_ID_INVALID,
        .element_type = source->element_type.value == IR_ID_UNDERLYING_INVALID ? DEBUG_ID_INVALID : source->element_type.value,
        .return_type = source->return_type.value == IR_ID_UNDERLYING_INVALID ? DEBUG_ID_INVALID : source->return_type.value,
        .kind = volatile_wrapper ? DEBUG_TYPE_QUALIFIED : debug_type_kind_from_ir(source->kind),
        .size = source->layout.size,
        .alignment = source->layout.alignment,
        .element_count = source->element_count,
        .field_count = source->field_count,
        .enum_member_count = source->enum_member_count,
        .parameter_count = source->parameter_count,
        .bit_width = source->bit_width,
        .is_signed = source->is_signed,
        .is_float = source->kind == IR_TYPE_FLOAT,
        .is_declaration = (source->kind == IR_TYPE_STRUCT || source->kind == IR_TYPE_UNION) && !volatile_wrapper && !source->layout.resolved,
        .is_variadic = source->is_variadic,
        .is_const = false,
        .is_volatile = volatile_wrapper,
    };
    result->fields = debug_copy_ir_fields(arena, program, source->fields, source->field_count);
    result->enum_members = debug_copy_ir_enum_members(arena, program, source->enum_members, source->enum_member_count);
    if (source->parameter_count)
    {
        result->parameter_types = arena_allocate(arena, DebugTypeId, source->parameter_count);
        for (u32 index = 0; index < source->parameter_count; index += 1)
        {
            result->parameter_types[index] = source->parameter_types[index].value;
        }
    }
}




BUSTER_GLOBAL_LOCAL DebugLocation debug_location_copy(Arena* arena, DebugLocation source)
{
    DebugLocation result = source;
    if (source.piece_count)
    {
        result.pieces = arena_allocate(arena, DebugLocationPiece, source.piece_count);
        memcpy(result.pieces, source.pieces, sizeof(DebugLocationPiece) * source.piece_count);
    }
    return result;
}

BUSTER_GLOBAL_LOCAL DebugLocation debug_unavailable_location(void)
{
    return (DebugLocation){
        .kind = DEBUG_LOCATION_UNAVAILABLE,
    };
}

BUSTER_GLOBAL_LOCAL u32 debug_location_bucket(IrSymbolId symbol, u32 bucket_mask)
{
    // Symbol values are dense indexes, but a caller may hand over sparse ones;
    // mixing keeps the buckets balanced either way.
    u32 value = symbol.value;
    value ^= value >> 16;
    value *= 0x7feb352du;
    value ^= value >> 15;
    return value & bucket_mask;
}

BUSTER_GLOBAL_LOCAL u32 debug_location_local_bucket(IrSymbolId symbol, IrLocalId local, u32 bucket_mask)
{
    // Hash-combine the two dense IDs before applying the same final mixer as
    // the symbol-only partition. Unsigned overflow is intentional here.
    u32 value = symbol.value;
    value ^= local.value + 0x9e3779b9u + (value << 6) + (value >> 2);
    value ^= value >> 16;
    value *= 0x7feb352du;
    value ^= value >> 15;
    return value & bucket_mask;
}

DebugLocationIndex debug_location_index_build(Arena* arena, DebugLocationSeed* locations, u32 location_count)
{
    DebugLocationIndex result = {0};
    if (arena && locations && location_count)
    {
        u32 bucket_count = 1;
        while (bucket_count < location_count && bucket_count < (1u << 30))
        {
            bucket_count <<= 1;
        }
        u32 bucket_mask = bucket_count - 1;
        u32* bucket_ends = arena_allocate(arena, u32, bucket_count);
        u32* order = arena_allocate(arena, u32, location_count);
        u32* local_bucket_ends = arena_allocate(arena, u32, bucket_count);
        u32* local_order = arena_allocate(arena, u32, location_count);
        if (bucket_ends && order && local_bucket_ends && local_order)
        {
            memset(bucket_ends, 0, sizeof(u32) * bucket_count);
            memset(local_bucket_ends, 0, sizeof(u32) * bucket_count);
            for (u32 index = 0; index < location_count; index += 1)
            {
                DebugLocationSeed* seed = locations + index;
                bucket_ends[debug_location_bucket(seed->function_symbol, bucket_mask)] += 1;
                local_bucket_ends[debug_location_local_bucket(seed->function_symbol, seed->local, bucket_mask)] += 1;
            }
            u32 running = 0;
            u32 local_running = 0;
            for (u32 bucket = 0; bucket < bucket_count; bucket += 1)
            {
                u32 count = bucket_ends[bucket];
                bucket_ends[bucket] = running;
                running += count;
                u32 local_count = local_bucket_ends[bucket];
                local_bucket_ends[bucket] = local_running;
                local_running += local_count;
            }
            // Filling through the exclusive prefix sums advances every entry to
            // its bucket's end offset. Iterating seeds in source order makes both
            // counting sorts stable, preserving emitted range order.
            for (u32 index = 0; index < location_count; index += 1)
            {
                DebugLocationSeed* seed = locations + index;
                order[bucket_ends[debug_location_bucket(seed->function_symbol, bucket_mask)]++] = index;
                local_order[local_bucket_ends[debug_location_local_bucket(seed->function_symbol, seed->local, bucket_mask)]++] = index;
            }
            result = (DebugLocationIndex){
                .locations = locations,
                .bucket_ends = bucket_ends,
                .order = order,
                .local_bucket_ends = local_bucket_ends,
                .local_order = local_order,
                .bucket_count = bucket_count,
                .location_count = location_count,
            };
        }
    }

    return result;
}

BUSTER_GLOBAL_LOCAL bool debug_location_index_partition_valid(DebugLocationIndex* index, u32* bucket_ends, u32* order,
                                                               bool exact_local)
{
    bool valid = bucket_ends && order;
    u32 first = 0;
    for (u32 bucket = 0; valid && bucket < index->bucket_count; bucket += 1)
    {
        u32 end = bucket_ends[bucket];
        valid = first <= end && end <= index->location_count;
        u32 previous = 0;
        for (u32 scan = first; valid && scan < end; scan += 1)
        {
            u32 seed_index = order[scan];
            valid = seed_index < index->location_count && (scan == first || previous < seed_index);
            if (valid)
            {
                DebugLocationSeed* seed = index->locations + seed_index;
                u32 expected_bucket = exact_local ? debug_location_local_bucket(seed->function_symbol, seed->local, index->bucket_count - 1)
                                                  : debug_location_bucket(seed->function_symbol, index->bucket_count - 1);
                valid = expected_bucket == bucket;
            }
            previous = seed_index;
        }
        first = end;
    }
    return valid && first == index->location_count;
}

// Only model construction accepts a caller-populated index. Prove both
// complete partitions, bucket membership and stable seed order once; the
// per-variable lookup below remains constant-time plus its matching bucket.
BUSTER_GLOBAL_LOCAL bool debug_location_index_valid(DebugLocationIndex* index)
{
    bool valid = index && index->locations && index->location_count && index->bucket_count &&
                 !(index->bucket_count & (index->bucket_count - 1));
    if (valid)
    {
        valid = debug_location_index_partition_valid(index, index->bucket_ends, index->order, false) &&
                debug_location_index_partition_valid(index, index->local_bucket_ends, index->local_order, true);
    }
    return valid;
}

BUSTER_GLOBAL_LOCAL bool debug_model_locations_prepare(Arena* arena, DebugModelInput* input, DebugLocationIndex* storage)
{
    bool valid = (!input->location_count || input->locations) && (!input->inline_site_count || input->inline_sites);
    for (u32 seed = 0; valid && seed < input->location_count; seed += 1)
    {
        DebugLocation* location = &input->locations[seed].location;
        valid = !location->piece_count || location->pieces;
    }
    if (valid && input->location_count)
    {
        DebugLocationIndex* index = input->location_index;
        if (index && index->locations == input->locations && index->location_count == input->location_count)
        {
            valid = debug_location_index_valid(index);
        }
        else
        {
            // An index for a different slice is stale, not a reason to omit
            // locations. Rebuild it without reading any partition arrays.
            *storage = debug_location_index_build(arena, input->locations, input->location_count);
            input->location_index = storage;
        }
    }
    else
    {
        input->location_index = 0;
    }
    return valid;
}

BUSTER_GLOBAL_LOCAL DebugLocationIndex* debug_location_index_for(DebugModelInput* input)
{
    DebugLocationIndex* result = 0;
    if (input && input->locations && input->location_index)
    {
        DebugLocationIndex* index = input->location_index;
        if (index->locations == input->locations && index->location_count == input->location_count && index->bucket_count &&
            index->bucket_ends && index->order && index->local_bucket_ends && index->local_order)
        {
            // The model boundary has already validated matching indexes.
            // Test-only direct calls may still provide a stale index.
            result = index;
        }
    }
    return result;
}

void debug_variable_add_location(Arena* arena, DebugModelInput* input, DebugVariable* variable, IrSymbolId symbol,
                                                     IrLocalId local, u32 start, u32 end)
{
    DebugLocationIndex* index = debug_location_index_for(input);
    bool exact_local = local.value != IR_ID_UNDERLYING_INVALID;
    u32* scan_order = 0;
    u32 scan_first = 0;
    u32 scan_count = input && input->locations ? input->location_count : 0;
    if (index)
    {
        u32* bucket_ends = index->bucket_ends;
        u32 bucket = debug_location_bucket(symbol, index->bucket_count - 1);
        scan_order = index->order;
        if (exact_local)
        {
            bucket_ends = index->local_bucket_ends;
            bucket = debug_location_local_bucket(symbol, local, index->bucket_count - 1);
            scan_order = index->local_order;
        }
        scan_first = bucket ? bucket_ends[bucket - 1] : 0;
        scan_count = bucket_ends[bucket] - scan_first;
    }
    u32 matching_count = 0;
    for (u32 scan = 0; scan < scan_count; scan += 1)
    {
        u32 seed_index = scan_order ? scan_order[scan_first + scan] : scan;
        DebugLocationSeed* seed = input->locations + seed_index;
        if (seed->function_symbol.value == symbol.value && (!exact_local || seed->local.value == local.value))
        {
            matching_count += 1;
        }
    }
    variable->locations = arena_allocate(arena, DebugLocationRange, matching_count ? matching_count : 1);
    if (matching_count)
    {
        u32 output_count = 0;
        // Both index partitions are stable, so filtering a hash bucket retains
        // the original seed order and therefore byte-identical emitted ranges.
        for (u32 scan = 0; scan < scan_count; scan += 1)
        {
            u32 seed_index = scan_order ? scan_order[scan_first + scan] : scan;
            DebugLocationSeed* seed = input->locations + seed_index;
            if (seed->function_symbol.value != symbol.value || (exact_local && seed->local.value != local.value))
            {
                continue;
            }
            variable->locations[output_count++] = (DebugLocationRange){
                .start = seed->start,
                .end = seed->end > seed->start ? seed->end : seed->start + 1,
                .location = debug_location_copy(arena, seed->location),
            };
        }
        variable->location_count = output_count;
    }
    else
    {
        variable->locations[0] = (DebugLocationRange){
            .start = start,
            .end = end > start ? end : start + 1,
            .location = debug_unavailable_location(),
        };
        variable->location_count = 1;
    }
}

DebugScopeId debug_scope_add(Arena* arena, DebugModel* model, DebugScopeId parent, DebugScopeKind kind,
                                                 DebugSourceLocation declaration, u32 start, u32 end, u32 variable_capacity)
{
    if (model->scope_count == UINT32_MAX)
    {
        return DEBUG_SCOPE_INVALID;
    }
    DebugScopeId id = model->scope_count++;
    DebugScope* scope = model->scopes + id;
    *scope = (DebugScope){
        .parent = parent,
        .declaration = declaration,
        .kind = kind,
        .start = start,
        .end = end > start ? end : start + 1,
        .variables = arena_allocate(arena, DebugVariableId, variable_capacity ? variable_capacity : 1),
    };
    return id;
}

DebugVariableId debug_variable_add(Arena* arena, DebugModel* model, DebugModelInput* input, DebugScopeId scope_id,
                                                       String8 name, DebugTypeId type, DebugSourceLocation declaration, DebugVariableKind kind,
                                                       IrSymbolId symbol, IrLocalId local, u32 start, u32 end)
{
    // Scope ownership is a dense ID, not a relational comparison between a
    // possibly unrelated pointer and the model's storage. Validate before
    // forming the pointer, without a linear scan on each variable.
    DebugVariableId result = DEBUG_ID_INVALID;
    if (name.length && model->variable_count < UINT32_MAX && model->scopes && scope_id < model->scope_count)
    {
        DebugScope* scope = model->scopes + scope_id;
        result = model->variable_count++;
        DebugVariable* variable = model->variables + result;
        *variable = (DebugVariable){
            .name = debug_string(arena, name),
            .type = type,
            .declaration = declaration,
            .symbol = symbol,
            .local = local,
            .scope = scope_id,
            .kind = kind,
        };
        debug_variable_add_location(arena, input, variable, symbol, local, start, end);
        if (scope->variable_count < UINT32_MAX)
        {
            scope->variables[scope->variable_count++] = result;
        }
    }
    return result;
}




BUSTER_GLOBAL_LOCAL void debug_add_canonical_locals(Arena* arena, DebugModel* model, DebugModelInput* input, DebugFunction* function,
                                                    DebugFunctionSeed* seed, IrFunction* ir_function, u32 model_scope_capacity)
{
    if (!ir_function)
    {
        return;
    }
    // Ordinal 0 is the function scope; every other ordinal names one lexical
    // block of the function, numbered parent first. A hand-built table whose
    // parent is not an earlier ordinal attaches to the function scope.
    //
    // Blocks have no code range of their own yet (#2241), so every block covers
    // the whole function. Two sibling blocks with the same range would overlap,
    // which DWARF consumers reject, so the blocks sharing a parent become one
    // model scope. Blocks with no sibling keep their own scope, and a block that
    // follows a nested one hangs off its own enclosing scope.
    u32 ir_scope_count = ir_function->debug_scope_count;
    TemporalArena temporary = scratch_begin(&arena, 1);
    u64 ordinal_slots = (u64)ir_scope_count + 1;
    u32* representative = arena_allocate(temporary.arena, u32, ordinal_slots);
    u32* merged_child = arena_allocate(temporary.arena, u32, ordinal_slots);
    u32* scope_variable_counts = arena_allocate(temporary.arena, u32, ordinal_slots);
    DebugScopeId* scopes = arena_allocate(temporary.arena, DebugScopeId, ordinal_slots);
    memset(merged_child, 0, sizeof(*merged_child) * ordinal_slots);
    memset(scope_variable_counts, 0, sizeof(*scope_variable_counts) * ordinal_slots);
    representative[0] = 0;
    for (u32 scope_index = 0; scope_index < ir_scope_count; scope_index += 1)
    {
        u32 ordinal = scope_index + 1;
        u32 parent_ordinal = ir_function->debug_scopes[scope_index].parent;
        u32 parent = representative[parent_ordinal <= scope_index ? parent_ordinal : 0];
        if (!merged_child[parent])
        {
            merged_child[parent] = ordinal;
        }
        representative[ordinal] = merged_child[parent];
    }
    for (u32 local_index = 0; local_index < ir_function->debug_local_count; local_index += 1)
    {
        u32 local_scope = ir_function->debug_locals[local_index].scope;
        scope_variable_counts[representative[local_scope <= ir_scope_count ? local_scope : 0]] += 1;
    }
    scopes[0] = function->scope;
    for (u32 scope_index = 0; scope_index < ir_scope_count; scope_index += 1)
    {
        u32 ordinal = scope_index + 1;
        DebugScopeId added = DEBUG_SCOPE_INVALID;
        if (representative[ordinal] != ordinal)
        {
            added = scopes[representative[ordinal]];
        }
        else
        {
            u32 parent_ordinal = ir_function->debug_scopes[scope_index].parent;
            DebugScopeId parent = scopes[representative[parent_ordinal <= scope_index ? parent_ordinal : 0]];
            if (parent != DEBUG_SCOPE_INVALID && model->scope_count < model_scope_capacity)
            {
                added = debug_scope_add(arena, model, parent, DEBUG_SCOPE_LEXICAL, (DebugSourceLocation){0}, function->code_offset,
                                        function->code_offset + function->code_size, scope_variable_counts[ordinal]);
            }
            // When only the model's capacity ran out the locals go to the
            // function scope instead; a block's locals are never dropped.
            if (added == DEBUG_SCOPE_INVALID && parent != DEBUG_SCOPE_INVALID)
            {
                added = scopes[0];
            }
        }
        scopes[ordinal] = added;
    }
    for (u32 local_index = 0; local_index < ir_function->debug_local_count; local_index += 1)
    {
        IrDebugLocal* local = ir_function->debug_locals + local_index;
        if (local->id.value == IR_ID_UNDERLYING_INVALID)
        {
            continue;
        }
        u32 local_scope = representative[local->scope <= ir_scope_count ? local->scope : 0];
        if (scopes[local_scope] == DEBUG_SCOPE_INVALID && local_scope)
        {
            continue;
        }
        DebugScope* declaring_scope = scopes[local_scope] < model->scope_count ? model->scopes + scopes[local_scope] : 0;
        if (declaring_scope && declaring_scope->kind == DEBUG_SCOPE_LEXICAL && !declaring_scope->variable_count)
        {
            declaring_scope->declaration = debug_source_from_ir(arena, input->program, local->source);
        }
        DebugVariableKind kind = local->is_parameter ? DEBUG_VARIABLE_PARAMETER : DEBUG_VARIABLE_LOCAL;
        DebugVariableId variable = debug_variable_add(arena, model, input, scopes[local_scope], local->name,
                                                      debug_canonical_type_id(model, local->type), debug_source_from_ir(arena, input->program, local->source),
                                                      kind, seed->symbol, local->id, function->code_offset,
                                                      function->code_offset + function->code_size);
        if (variable != DEBUG_ID_INVALID)
        {
            function->variable_count += 1;
        }
    }
    scratch_end(temporary);
}

BUSTER_GLOBAL_LOCAL void debug_add_canonical_global(Arena* arena, DebugModel* model, DebugModelInput* input, IrGlobal* global, bool static_local,
                                                    bool keep_link_name)
{
    IrSymbol* symbol = input->program->symbols.symbols + global->symbol.value;
    String8 name = keep_link_name && symbol->link_name.length ? symbol->link_name : symbol->name;
    DebugVariableId variable = debug_variable_add(arena, model, input, model->root_scope, name, debug_canonical_type_id(model, global->type),
                                                  debug_source_from_ir(arena, input->program, global->source), DEBUG_VARIABLE_GLOBAL, global->symbol,
                                                  IR_LOCAL_ID_INVALID, 0, 1);
    if (variable != DEBUG_ID_INVALID)
    {
        model->variables[variable].linkage_name = debug_string(arena, symbol->link_name.length ? symbol->link_name : symbol->name);
        model->variables[variable].is_internal = symbol->linkage == IR_LINKAGE_INTERNAL;
        model->variables[variable].is_static_local = static_local;
    }
}

BUSTER_GLOBAL_LOCAL u32 debug_name_hash(String8 name)
{
    return (u32)buster_hash_64((u8*)name.pointer, name.length);
}

// Open-addressing set of file-scope names; a slot holds a global's index plus
// one, zero when empty. The table is at least twice the entry count, so a probe
// always reaches an empty slot.
BUSTER_GLOBAL_LOCAL bool debug_file_scope_name_present(DebugModelInput* input, u32* slots, u32 slot_mask, String8 name)
{
    bool result = false;
    u32 slot = debug_name_hash(name) & slot_mask;
    while (!result && slots[slot])
    {
        result = string_equal(input->program->symbols.symbols[input->module->globals[slots[slot] - 1].symbol.value].name, name);
        slot = (slot + 1) & slot_mask;
    }
    return result;
}

// Per-function name census: distinct names with how many parameters, locals and
// statics spell each. Slots hold an entry index plus one. The table is at least
// twice the name count, so every probe reaches an empty slot.
typedef struct DebugNameCensus
{
    String8* names;
    u32* counts;
    u32* slots;
    u32 slot_mask;
    u32 count;
} DebugNameCensus;

BUSTER_GLOBAL_LOCAL u32 debug_name_census_find(DebugNameCensus* census, String8 name)
{
    u32 slot = debug_name_hash(name) & census->slot_mask;
    while (census->slots[slot] && !string_equal(census->names[census->slots[slot] - 1], name))
    {
        slot = (slot + 1) & census->slot_mask;
    }
    return slot;
}

BUSTER_GLOBAL_LOCAL void debug_name_census_add(DebugNameCensus* census, String8 name)
{
    u32 slot = debug_name_census_find(census, name);
    if (census->slots[slot])
    {
        census->counts[census->slots[slot] - 1] += 1;
    }
    else
    {
        census->names[census->count] = name;
        census->counts[census->count] = 1;
        census->count += 1;
        census->slots[slot] = census->count;
    }
}

// A function-scope static is nested at subprogram level, not in its lexical
// block, so a debugger cannot tell it from a same-named parameter, local,
// sibling static or file-scope object that the block was meant to shadow. Such
// a static keeps its unique link spelling as its debug name instead (#2719).
// Each static is counted in the census, so a count above one is a collision.
BUSTER_GLOBAL_LOCAL bool debug_static_name_collides(DebugModelInput* input, DebugNameCensus* census, String8 name, u32* file_slots, u32 file_slot_mask)
{
    bool result = debug_file_scope_name_present(input, file_slots, file_slot_mask, name);
    if (!result)
    {
        u32 slot = debug_name_census_find(census, name);
        result = census->slots[slot] && census->counts[census->slots[slot] - 1] > 1;
    }
    return result;
}

// File-scope data goes to the root scope in module order. A function-scope
// static whose function has a debug function is appended after them, grouped
// by owner with a counting sort (stable, so module order within one function),
// so each DebugFunction names one contiguous run (#2719). A static whose owner
// has no debug function keeps the file-scope form.
BUSTER_GLOBAL_LOCAL void debug_add_canonical_globals(Arena* arena, DebugModel* model, DebugModelInput* input, u32 variable_capacity)
{
    if (input->module && input->program && model->root_scope < model->scope_count)
    {
        TemporalArena temporary = scratch_begin(&arena, 1);
        u32 global_count = input->module->global_count;
        u32 symbol_count = input->program->symbols.count;
        u32* function_by_symbol = arena_allocate(temporary.arena, u32, symbol_count ? symbol_count : 1);
        memset(function_by_symbol, 0xff, sizeof(*function_by_symbol) * (u64)symbol_count);
        for (u32 function_index = model->function_count; function_index; function_index -= 1)
        {
            u32 function_symbol = model->functions[function_index - 1].symbol.value;
            if (function_symbol < symbol_count)
            {
                function_by_symbol[function_symbol] = function_index - 1;
            }
        }
        u32* owners = arena_allocate(temporary.arena, u32, global_count ? global_count : 1);
        u32* run_ends = arena_allocate(temporary.arena, u32, (u64)model->function_count + 1);
        memset(run_ends, 0, sizeof(*run_ends) * ((u64)model->function_count + 1));
        u32 static_total = 0;
        for (u32 global_index = 0; global_index < global_count; global_index += 1)
        {
            IrGlobal* global = input->module->globals + global_index;
            owners[global_index] = UINT32_MAX;
            if (global->symbol.value != IR_ID_UNDERLYING_INVALID && global->symbol.value < symbol_count)
            {
                IrSymbol* symbol = input->program->symbols.symbols + global->symbol.value;
                if (symbol->has_owner_function && symbol->owner_function.value < symbol_count)
                {
                    owners[global_index] = function_by_symbol[symbol->owner_function.value];
                }
                if (owners[global_index] != UINT32_MAX)
                {
                    run_ends[owners[global_index] + 1] += 1;
                    static_total += 1;
                }
            }
        }
        u32 slot_count = 2;
        while (static_total && slot_count < global_count * 2u)
        {
            slot_count <<= 1;
        }
        u32* slots = arena_allocate(temporary.arena, u32, static_total ? slot_count : 1);
        memset(slots, 0, sizeof(*slots) * (static_total ? slot_count : 1));
        for (u32 global_index = 0; global_index < global_count; global_index += 1)
        {
            IrGlobal* global = input->module->globals + global_index;
            if (global->symbol.value != IR_ID_UNDERLYING_INVALID && global->symbol.value < symbol_count && owners[global_index] == UINT32_MAX)
            {
                debug_add_canonical_global(arena, model, input, global, false, false);
                if (static_total)
                {
                    u32 slot = debug_name_hash(input->program->symbols.symbols[global->symbol.value].name) & (slot_count - 1);
                    while (slots[slot])
                    {
                        slot = (slot + 1) & (slot_count - 1);
                    }
                    slots[slot] = global_index + 1;
                }
            }
        }
        for (u32 function_index = 0; function_index < model->function_count; function_index += 1)
        {
            run_ends[function_index + 1] += run_ends[function_index];
        }
        u32* order = arena_allocate(temporary.arena, u32, static_total ? static_total : 1);
        u32* cursors = arena_allocate(temporary.arena, u32, (u64)model->function_count + 1);
        memcpy(cursors, run_ends, sizeof(*cursors) * ((u64)model->function_count + 1));
        for (u32 global_index = 0; global_index < global_count; global_index += 1)
        {
            if (owners[global_index] != UINT32_MAX)
            {
                order[cursors[owners[global_index]]++] = global_index;
            }
        }
        u32 census_capacity = 1;
        for (u32 function_index = 0; function_index < model->function_count; function_index += 1)
        {
            DebugFunction* function = model->functions + function_index;
            u32 statics = run_ends[function_index + 1] - run_ends[function_index];
            u32 variables = function->variable_start < model->variable_count ? model->variable_count - function->variable_start : 0;
            variables = function->variable_count < variables ? function->variable_count : variables;
            if (statics && statics + variables > census_capacity)
            {
                census_capacity = statics + variables;
            }
        }
        u32 census_slot_count = 2;
        while (census_slot_count < census_capacity * 2u)
        {
            census_slot_count <<= 1;
        }
        DebugNameCensus census = {
            .names = arena_allocate(temporary.arena, String8, census_capacity),
            .counts = arena_allocate(temporary.arena, u32, census_capacity),
            .slots = arena_allocate(temporary.arena, u32, census_slot_count),
        };
        for (u32 function_index = 0; function_index < model->function_count; function_index += 1)
        {
            DebugFunction* function = model->functions + function_index;
            u32 run_begin = run_ends[function_index];
            u32 run_end = run_ends[function_index + 1];
            function->static_start = model->variable_count;
            if (run_begin != run_end)
            {
                u32 variables = function->variable_start < model->variable_count ? model->variable_count - function->variable_start : 0;
                variables = function->variable_count < variables ? function->variable_count : variables;
                census.slot_mask = 1;
                while (census.slot_mask + 1 < (run_end - run_begin + variables) * 2u)
                {
                    census.slot_mask = census.slot_mask * 2 + 1;
                }
                memset(census.slots, 0, sizeof(*census.slots) * ((u64)census.slot_mask + 1));
                census.count = 0;
                for (u32 offset = 0; offset < variables; offset += 1)
                {
                    debug_name_census_add(&census, model->variables[function->variable_start + offset].name);
                }
                for (u32 run_index = run_begin; run_index < run_end; run_index += 1)
                {
                    debug_name_census_add(&census, input->program->symbols.symbols[input->module->globals[order[run_index]].symbol.value].name);
                }
            }
            for (u32 run_index = run_begin; run_index < run_end; run_index += 1)
            {
                IrGlobal* global = input->module->globals + order[run_index];
                String8 name = input->program->symbols.symbols[global->symbol.value].name;
                bool collides = debug_static_name_collides(input, &census, name, slots, slot_count - 1);
                debug_add_canonical_global(arena, model, input, global, true, collides);
            }
            function->static_count = model->variable_count - function->static_start;
        }
        scratch_end(temporary);
    }
    (void)variable_capacity;
}





BUSTER_GLOBAL_LOCAL DebugSourceLocation debug_function_declaration(Arena* arena, DebugModelInput* input, DebugFunctionSeed* seed)
{
    DebugSourceLocation result;
    if (seed->declaration.line)
    {
        result = seed->declaration;
    }
    else if (input->program && seed->symbol.value < input->program->symbols.count)
    {
        result = debug_source_from_ir(arena, input->program, input->program->symbols.symbols[seed->symbol.value].source);
    }
    else
    {
        result = (DebugSourceLocation){0};
    }

    return result;
}

BUSTER_GLOBAL_LOCAL void debug_model_fill_sources(Arena* arena, DebugModel* model, DebugModelInput* input)
{
    if (!input->program)
    {
        return;
    }
    model->source_count = input->program->sources.count;
    model->source_paths = model->source_count ? arena_allocate(arena, String8, model->source_count) : 0;
    for (u32 index = 0; index < model->source_count; index += 1)
    {
        model->source_paths[index] = debug_string(arena, input->program->sources.sources[index].path);
    }
}

DebugModel debug_model_build(Arena* arena, DebugModelInput input)
{
    DebugModel result = {
        .producer = debug_string(arena, input.producer),
        .comp_dir = debug_string(arena, input.comp_dir),
        .root_scope = DEBUG_SCOPE_INVALID,
    };
    DebugLocationIndex location_index = {0};
    if (arena && input.program && (!input.function_count || input.functions) &&
        debug_model_locations_prepare(arena, &input, &location_index))
    {
        result.type_count = input.program->types.count;
        result.function_count = input.function_count;
        result.inline_site_count = input.inline_site_count;

        u32 variable_capacity = 1;
        if (input.module)
        {
            variable_capacity = input.module->global_count + 1;
            for (u32 function_index = 0; function_index < input.module->function_count; function_index += 1)
            {
                variable_capacity += input.module->functions[function_index].debug_local_count;
            }
        }
        u32 scope_capacity = input.function_count + variable_capacity + 1;
        result.types = result.type_count ? arena_allocate(arena, DebugType, result.type_count) : 0;
        result.functions = input.function_count ? arena_allocate(arena, DebugFunction, input.function_count) : 0;
        result.scopes = arena_allocate(arena, DebugScope, scope_capacity ? scope_capacity : 1);
        result.variables = arena_allocate(arena, DebugVariable, variable_capacity ? variable_capacity : 1);
        result.inline_sites = input.inline_site_count ? arena_allocate(arena, DebugInlineSite, input.inline_site_count) : 0;
        debug_model_fill_sources(arena, &result, &input);

        DebugSourceLocation root_declaration = {
            .source = 0,
            .line = 1,
            .column = 1,
        };
        u32 root_variable_capacity = input.module ? input.module->global_count + 1 : variable_capacity;
        result.root_scope = debug_scope_add(arena, &result, DEBUG_SCOPE_INVALID, DEBUG_SCOPE_LEXICAL, root_declaration, 0, 1, root_variable_capacity);

        for (u32 type_index = 0; type_index < result.type_count; type_index += 1)
        {
            debug_fill_ir_type(arena, &result, input.program, input.program->types.types + type_index, result.types + type_index);
        }

        for (u32 symbol_index = 0; symbol_index < input.program->symbols.count; symbol_index += 1)
        {
            IrSymbol* symbol = input.program->symbols.symbols + symbol_index;
            if (symbol->kind == IR_SYMBOL_TYPE && symbol->type.value < result.type_count)
            {
                result.types[symbol->type.value].declaration = debug_source_from_ir(arena, input.program, symbol->source);
                result.types[symbol->type.value].declaration_name = debug_string(arena, symbol->name);
                if (!result.types[symbol->type.value].name.length)
                {
                    result.types[symbol->type.value].name = debug_string(arena, symbol->name);
                }
            }
        }

        // Emitted debug seeds follow code layout, while the IR module retains
        // declaration order, so each seed finds its canonical locals by
        // symbol. Index the module's functions by dense symbol id once --
        // walking backwards leaves the first function per symbol, the one a
        // forward search stops at -- instead of searching the module per seed.
        TemporalArena temporary = scratch_begin(&arena, 1);
        u32 symbol_count = input.program->symbols.count;
        u32* function_by_symbol = 0;
        if (input.module)
        {
            function_by_symbol = arena_allocate(temporary.arena, u32, symbol_count ? symbol_count : 1);
            memset(function_by_symbol, 0xff, sizeof(*function_by_symbol) * (u64)symbol_count);
            for (u32 module_index = input.module->function_count; module_index; module_index -= 1)
            {
                IR_CONSTRUCTION_RECORD(DEBUG_FUNCTION_INDEX_ROWS, 1);
                u32 symbol = input.module->functions[module_index - 1].symbol.value;
                if (symbol < symbol_count)
                {
                    function_by_symbol[symbol] = module_index - 1;
                }
            }
        }
        for (u32 function_index = 0; function_index < input.function_count; function_index += 1)
        {
            DebugFunctionSeed* seed = input.functions + function_index;
            DebugFunction* function = result.functions + function_index;
            DebugSourceLocation declaration = debug_function_declaration(arena, &input, seed);
            IrTypeId canonical_type = IR_TYPE_ID_INVALID;
            if (seed->symbol.value < input.program->symbols.count)
            {
                canonical_type = input.program->symbols.symbols[seed->symbol.value].type;
            }
            *function = (DebugFunction){
                .name = debug_string(arena, seed->name),
                .declaration = declaration,
                .symbol = seed->symbol,
                .type = debug_canonical_type_id(&result, canonical_type),
                .code_offset = seed->code_offset,
                .code_size = seed->code_size,
                .variable_start = result.variable_count,
                .is_internal = seed->symbol.value < input.program->symbols.count &&
                               input.program->symbols.symbols[seed->symbol.value].linkage == IR_LINKAGE_INTERNAL,
            };
            if (!function->name.length && declaration.source < result.source_count)
            {
                function->name = result.source_paths[declaration.source];
            }
            IrFunction* module_function = 0;
            if (function_by_symbol && seed->symbol.value < symbol_count)
            {
                u32 module_index = function_by_symbol[seed->symbol.value];
                module_function = module_index != UINT32_MAX ? input.module->functions + module_index : 0;
            }
            else if (input.module)
            {
                // Codegen seeds always name a program symbol; only a
                // hand-built seed outside the table reaches this search.
                for (u32 module_index = 0; module_index < input.module->function_count; module_index += 1)
                {
                    IR_CONSTRUCTION_RECORD(DEBUG_FUNCTION_SEED_SCAN_ROWS, 1);
                    IrFunction* candidate = input.module->functions + module_index;
                    if (candidate->symbol.value == seed->symbol.value)
                    {
                        module_function = candidate;
                        break;
                    }
                }
            }
            u32 function_variable_capacity = module_function && module_function->debug_local_count ? module_function->debug_local_count : 1;
            function->scope = debug_scope_add(arena, &result, result.root_scope, DEBUG_SCOPE_FUNCTION, declaration, function->code_offset,
                                              function->code_offset + function->code_size, function_variable_capacity);
            if (module_function)
            {
                debug_add_canonical_locals(arena, &result, &input, function, seed, module_function, scope_capacity);
            }
        }
        scratch_end(temporary);
        debug_add_canonical_globals(arena, &result, &input, variable_capacity);

        for (u32 inline_index = 0; inline_index < input.inline_site_count; inline_index += 1)
        {
            DebugInlineSeed* seed = input.inline_sites + inline_index;
            DebugInlineSite* site = result.inline_sites + inline_index;
            *site = (DebugInlineSite){
                .function = seed->function_index < result.function_count ? result.functions + seed->function_index : 0,
                .parent = seed->parent_index < result.inline_site_count && seed->parent_index != UINT32_MAX ? result.inline_sites + seed->parent_index : 0,
                .call_site = seed->call_site,
                .start = seed->start,
                .end = seed->end,
                .has_ranges = seed->end > seed->start,
            };
        }
        result.valid = true;
    }

    return result;
}

DebugTypeId debug_model_find_canonical_type(DebugModel* model, IrTypeId type)
{
    if (model)
    {
        for (u32 index = 0; index < model->type_count; index += 1)
        {
            if (model->types[index].canonical_type.value == type.value)
            {
                return index;
            }
        }
    }

    return DEBUG_ID_INVALID;
}

u32 debug_register_dwarf_number(Target target, DebugRegister reg)
{
    u32 register_index = (u32)reg;
    if (target.cpu_arch == CPU_ARCH_X86_64)
    {
        static const u32 gpr[] = {UINT32_MAX, 0, 2, 1, 3, 7, 6, 4, 5, 8, 9, 10, 11, 12, 13, 14, 15};
        if (register_index < (u32)BUSTER_ARRAY_LENGTH(gpr))
        {
            return gpr[register_index];
        }
        if (register_index >= (u32)DEBUG_REGISTER_X86_XMM0 && register_index <= (u32)DEBUG_REGISTER_X86_XMM15)
        {
            return 17 + register_index - (u32)DEBUG_REGISTER_X86_XMM0;
        }
    }
    else if (target.cpu_arch == CPU_ARCH_AARCH64)
    {
        if (register_index >= (u32)DEBUG_REGISTER_AARCH64_X0 && register_index <= (u32)DEBUG_REGISTER_AARCH64_X30)
        {
            return register_index - (u32)DEBUG_REGISTER_AARCH64_X0;
        }
        if (register_index == (u32)DEBUG_REGISTER_AARCH64_SP)
        {
            return 31;
        }
        if (register_index >= (u32)DEBUG_REGISTER_AARCH64_V0 && register_index <= (u32)DEBUG_REGISTER_AARCH64_V31)
        {
            return 64 + register_index - (u32)DEBUG_REGISTER_AARCH64_V0;
        }
    }
    return UINT32_MAX;
}

// CodeView numbers registers in its own order (CV_HREG_e in Microsoft's
// cvconst.h, mirrored by LLVM's CodeViewRegisters.def), which is neither the
// hardware encoding order DebugRegister follows nor DWARF's: RBX precedes RCX,
// XMM8-15 are not contiguous with XMM0-7, and ARM64 X0 is 50, not 0 (#1440).
// The tables below are that enumeration, not arithmetic over ours.
u32 debug_register_codeview_number(Target target, DebugRegister reg)
{
    u32 register_index = (u32)reg;
    u32 result = UINT32_MAX;
    if (target.cpu_arch == CPU_ARCH_X86_64)
    {
        // RAX RCX RDX RBX RSP RBP RSI RDI R8..R15, in DebugRegister order.
        static const u16 gpr[] = {328, 330, 331, 329, 335, 334, 332, 333, 336, 337, 338, 339, 340, 341, 342, 343};
        if (register_index >= (u32)DEBUG_REGISTER_X86_RAX && register_index <= (u32)DEBUG_REGISTER_X86_R15)
        {
            result = gpr[register_index - (u32)DEBUG_REGISTER_X86_RAX];
        }
        else if (register_index >= (u32)DEBUG_REGISTER_X86_XMM0 && register_index <= (u32)DEBUG_REGISTER_X86_XMM7)
        {
            result = 154 + register_index - (u32)DEBUG_REGISTER_X86_XMM0;
        }
        else if (register_index >= (u32)DEBUG_REGISTER_X86_XMM8 && register_index <= (u32)DEBUG_REGISTER_X86_XMM15)
        {
            result = 252 + register_index - (u32)DEBUG_REGISTER_X86_XMM8;
        }
    }
    else if (target.cpu_arch == CPU_ARCH_AARCH64)
    {
        // X0..X28 are 50..78, then FP (X29) 79, LR (X30) 80, SP 81; a vector
        // register is named by its full 128-bit Q view, Q0..Q31 180..211.
        if (register_index >= (u32)DEBUG_REGISTER_AARCH64_X0 && register_index <= (u32)DEBUG_REGISTER_AARCH64_X30)
        {
            result = 50 + register_index - (u32)DEBUG_REGISTER_AARCH64_X0;
        }
        else if (register_index == (u32)DEBUG_REGISTER_AARCH64_SP)
        {
            result = 81;
        }
        else if (register_index >= (u32)DEBUG_REGISTER_AARCH64_V0 && register_index <= (u32)DEBUG_REGISTER_AARCH64_V31)
        {
            result = 180 + register_index - (u32)DEBUG_REGISTER_AARCH64_V0;
        }
    }
    return result;
}
