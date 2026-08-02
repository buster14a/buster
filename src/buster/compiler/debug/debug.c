#include <buster/compiler/debug/debug.h>

#include <buster/compiler/ir/ir.h>
#include <buster/string.h>

BUSTER_GLOBAL_LOCAL String8 debug_string(Arena* arena, String8 string)
{
    return string.length ? string_duplicate_arena(arena, string, false) : (String8){0};
}

BUSTER_GLOBAL_LOCAL DebugSourceLocation debug_source_from_ir(Arena* arena, IrProgram* program, IrSourceRange range)
{
    DebugSourceLocation result = {
        .line = range.line ? range.line : 1,
        .column = range.column ? range.column : 1,
        .offset = range.offset,
        .length = range.length,
    };
    if (program && range.source.value < program->sources.count)
    {
        result.source = range.source.value;
        result.path = debug_string(arena, program->sources.sources[range.source.value].path);
    }
    return result;
}

BUSTER_GLOBAL_LOCAL DebugSourceLocation debug_source_from_analysis(Arena* arena, AnalysisResult* analysis, AnalysisSourceId source,
                                                                    ParserSourceRange range)
{
    DebugSourceLocation result = {
        .source = source.value,
        .line = range.line ? range.line : 1,
        .column = range.column ? range.column : 1,
        .offset = range.offset,
        .length = range.length,
    };
    if (analysis && source.value < analysis->module.source_count)
    {
        result.path = debug_string(arena, analysis->module.sources[source.value].path);
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

BUSTER_GLOBAL_LOCAL DebugTypeKind debug_type_kind_from_analysis(AnalysisTypeKind kind)
{
    switch (kind)
    {
    case ANALYSIS_TYPE_VOID: return DEBUG_TYPE_VOID;
    case ANALYSIS_TYPE_BOOL:
    case ANALYSIS_TYPE_INTEGER:
    case ANALYSIS_TYPE_FLOAT:
    case ANALYSIS_TYPE_VA_LIST: return DEBUG_TYPE_BASE;
    case ANALYSIS_TYPE_POINTER:
    case ANALYSIS_TYPE_SLICE:
    case ANALYSIS_TYPE_RANGE: return DEBUG_TYPE_POINTER;
    case ANALYSIS_TYPE_INFERRED_ARRAY:
    case ANALYSIS_TYPE_ARRAY: return DEBUG_TYPE_ARRAY;
    case ANALYSIS_TYPE_VECTOR: return DEBUG_TYPE_VECTOR;
    case ANALYSIS_TYPE_FUNCTION: return DEBUG_TYPE_FUNCTION;
    case ANALYSIS_TYPE_STRUCT: return DEBUG_TYPE_STRUCT;
    case ANALYSIS_TYPE_UNION: return DEBUG_TYPE_UNION;
    case ANALYSIS_TYPE_ENUM: return DEBUG_TYPE_ENUM;
    case ANALYSIS_TYPE_POISON:
    case ANALYSIS_TYPE_COMPILE_TIME_PARAMETER:
    case ANALYSIS_TYPE_COUNT: break;
    }
    return DEBUG_TYPE_BASE;
}

BUSTER_GLOBAL_LOCAL IrTypeId debug_canonical_type_for_frontend(IrModule* module, AnalysisTypeId type)
{
    if (module && type.value < module->frontend_type_count && module->frontend_type_map)
    {
        return module->frontend_type_map[type.value];
    }
    return IR_TYPE_ID_INVALID;
}

BUSTER_GLOBAL_LOCAL DebugTypeId debug_frontend_type_id(DebugModel* model, AnalysisTypeId type)
{
    return type.value < model->type_count ? type.value : DEBUG_ID_INVALID;
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
    // IrType::unqualified_type currently denotes the operand of an atomic
    // type, not a source-level const/volatile wrapper.  Canonical lowering
    // intentionally erases C qualifiers, and treating the zero-initialized
    // field on ordinary canonical types as a DWARF/CodeView qualifier would
    // manufacture const types (and often point them at type zero).
    *result = (DebugType){
        .name = debug_string(arena, source->name),
        .declaration_name = debug_string(arena, source->name),
        .canonical_type = source->id,
        .unqualified_type = source->is_atomic && source->unqualified_type.value != IR_ID_UNDERLYING_INVALID ? source->unqualified_type.value
                                                                                                                : DEBUG_ID_INVALID,
        .element_type = source->element_type.value == IR_ID_UNDERLYING_INVALID ? DEBUG_ID_INVALID : source->element_type.value,
        .return_type = source->return_type.value == IR_ID_UNDERLYING_INVALID ? DEBUG_ID_INVALID : source->return_type.value,
        .kind = debug_type_kind_from_ir(source->kind),
        .size = source->layout.size,
        .alignment = source->layout.alignment,
        .element_count = source->element_count,
        .field_count = source->field_count,
        .enum_member_count = source->enum_member_count,
        .parameter_count = source->parameter_count,
        .bit_width = source->bit_width,
        .is_signed = source->is_signed,
        .is_variadic = source->is_variadic,
        .is_const = false,
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
    (void)model;
}

BUSTER_GLOBAL_LOCAL DebugTypeField* debug_copy_analysis_fields(Arena* arena, AnalysisResult* analysis, AnalysisEntityId entity_id,
                                                               AnalysisEntitySemantic* semantic, u32* count_out)
{
    u32 count = semantic ? semantic->field_count : 0;
    DebugTypeField* result = count ? arena_allocate(arena, DebugTypeField, count) : 0;
    if (count_out)
    {
        *count_out = count;
    }
    for (u32 index = 0; index < count; index += 1)
    {
        AnalysisField* field = semantic->fields + index;
        result[index] = (DebugTypeField){
            .name = debug_string(arena, field->name),
            .type = field->type.value,
            .declaration = debug_source_from_analysis(arena, analysis, entity_id.index.value < analysis->module.entity_count
                                                                                  ? analysis->module.entities[entity_id.index.value].source
                                                                                  : ANALYSIS_SOURCE_ID_INVALID,
                                                       field->range),
            .offset = field->offset,
        };
    }
    return result;
}

BUSTER_GLOBAL_LOCAL DebugEnumMember* debug_copy_analysis_enum_members(Arena* arena, AnalysisResult* analysis, AnalysisEntityId entity_id,
                                                                       AnalysisEntitySemantic* semantic, u32* count_out)
{
    u32 count = semantic ? semantic->enum_member_count : 0;
    DebugEnumMember* result = count ? arena_allocate(arena, DebugEnumMember, count) : 0;
    if (count_out)
    {
        *count_out = count;
    }
    for (u32 index = 0; index < count; index += 1)
    {
        AnalysisEnumMember* member = semantic->enum_members + index;
        result[index] = (DebugEnumMember){
            .name = debug_string(arena, member->name),
            .declaration = debug_source_from_analysis(arena, analysis, entity_id.index.value < analysis->module.entity_count
                                                                                  ? analysis->module.entities[entity_id.index.value].source
                                                                                  : ANALYSIS_SOURCE_ID_INVALID,
                                                       member->range),
            .value = member->value,
        };
    }
    return result;
}

BUSTER_GLOBAL_LOCAL void debug_fill_analysis_type(Arena* arena, DebugModel* model, AnalysisResult* analysis, IrModule* module, u32 index,
                                                  DebugType* result)
{
    AnalysisType* source = analysis->types.types + index;
    *result = (DebugType){
        .name = debug_string(arena, source->name),
        .declaration_name = debug_string(arena, source->name),
        .canonical_type = debug_canonical_type_for_frontend(module, source->id),
        .unqualified_type = DEBUG_ID_INVALID,
        .element_type = DEBUG_ID_INVALID,
        .return_type = DEBUG_ID_INVALID,
        .kind = debug_type_kind_from_analysis(source->kind),
        .size = source->layout.size,
        .alignment = source->layout.alignment,
    };
    if (source->kind == ANALYSIS_TYPE_POINTER || source->kind == ANALYSIS_TYPE_SLICE || source->kind == ANALYSIS_TYPE_RANGE)
    {
        result->element_type = debug_frontend_type_id(model, source->as.element_type);
    }
    else if (source->kind == ANALYSIS_TYPE_ARRAY || source->kind == ANALYSIS_TYPE_INFERRED_ARRAY)
    {
        result->element_type = debug_frontend_type_id(model, source->as.array.element_type);
        result->element_count = source->as.array.count;
    }
    else if (source->kind == ANALYSIS_TYPE_VECTOR)
    {
        result->element_type = debug_frontend_type_id(model, source->as.vector.element_type);
        result->element_count = source->as.vector.count;
    }
    else if (source->kind == ANALYSIS_TYPE_FUNCTION)
    {
        result->return_type = debug_frontend_type_id(model, source->as.function.return_type);
        result->parameter_count = source->as.function.argument_count;
        result->is_variadic = source->as.function.is_variadic;
        if (result->parameter_count)
        {
            result->parameter_types = arena_allocate(arena, DebugTypeId, result->parameter_count);
            for (u32 argument_index = 0; argument_index < result->parameter_count; argument_index += 1)
            {
                result->parameter_types[argument_index] =
                    debug_frontend_type_id(model, source->as.function.argument_types[argument_index]);
            }
        }
    }
    else if (source->kind == ANALYSIS_TYPE_INTEGER)
    {
        result->bit_width = source->as.integer.bit_width;
        result->is_signed = source->as.integer.is_signed;
    }
    else if (source->kind == ANALYSIS_TYPE_FLOAT)
    {
        result->bit_width = source->as.float_bit_width;
    }
    if (source->kind == ANALYSIS_TYPE_STRUCT || source->kind == ANALYSIS_TYPE_UNION || source->kind == ANALYSIS_TYPE_ENUM)
    {
        AnalysisEntityId entity_id = source->as.declaration;
        if (entity_id.index.value < analysis->module.entity_count)
        {
            AnalysisEntity* entity = analysis->module.entities + entity_id.index.value;
            result->declaration = debug_source_from_analysis(arena, analysis, entity->source, entity->range);
            result->declaration_name = debug_string(arena, entity->name);
            AnalysisEntitySemantic* semantic = analysis->module.semantics + entity_id.index.value;
            if (source->kind == ANALYSIS_TYPE_ENUM)
            {
                result->enum_members = debug_copy_analysis_enum_members(arena, analysis, entity_id, semantic, &result->enum_member_count);
            }
            else
            {
                result->fields = debug_copy_analysis_fields(arena, analysis, entity_id, semantic, &result->field_count);
            }
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

BUSTER_GLOBAL_LOCAL bool debug_symbol_equal(IrSymbolId left, IrSymbolId right)
{
    return left.value == right.value;
}

BUSTER_GLOBAL_LOCAL DebugLocation debug_unavailable_location(void)
{
    return (DebugLocation){
        .kind = DEBUG_LOCATION_UNAVAILABLE,
    };
}

BUSTER_GLOBAL_LOCAL void debug_variable_add_location(Arena* arena, DebugModelInput* input, DebugVariable* variable, IrSymbolId symbol,
                                                     IrLocalId local, u32 start, u32 end)
{
    u32 matching_count = 0;
    if (input && input->locations)
    {
        for (u32 index = 0; index < input->location_count; index += 1)
        {
            DebugLocationSeed* seed = input->locations + index;
            if (debug_symbol_equal(seed->function_symbol, symbol) &&
                (local.value == IR_ID_UNDERLYING_INVALID || seed->local.value == local.value))
            {
                matching_count += 1;
            }
        }
    }
    variable->locations = arena_allocate(arena, DebugLocationRange, matching_count ? matching_count : 1);
    if (matching_count)
    {
        u32 output_count = 0;
        for (u32 index = 0; index < input->location_count; index += 1)
        {
            DebugLocationSeed* seed = input->locations + index;
            if (!debug_symbol_equal(seed->function_symbol, symbol) ||
                (local.value != IR_ID_UNDERLYING_INVALID && seed->local.value != local.value))
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

BUSTER_GLOBAL_LOCAL DebugScopeId debug_scope_add(Arena* arena, DebugModel* model, DebugScopeId parent, DebugScopeKind kind,
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

BUSTER_GLOBAL_LOCAL DebugVariableId debug_variable_add(Arena* arena, DebugModel* model, DebugModelInput* input, DebugScope* scope,
                                                       String8 name, DebugTypeId type, DebugSourceLocation declaration, DebugVariableKind kind,
                                                       IrSymbolId symbol, IrLocalId local, u32 start, u32 end)
{
    if (!name.length || model->variable_count == UINT32_MAX)
    {
        return DEBUG_ID_INVALID;
    }
    DebugVariableId id = model->variable_count++;
    DebugVariable* variable = model->variables + id;
    *variable = (DebugVariable){
        .name = debug_string(arena, name),
        .type = type,
        .declaration = declaration,
        .symbol = symbol,
        .local = local,
        .scope = (DebugScopeId)(scope - model->scopes),
        .kind = kind,
    };
    debug_variable_add_location(arena, input, variable, symbol, local, start, end);
    if (scope->variable_count < UINT32_MAX)
    {
        scope->variables[scope->variable_count++] = id;
    }
    return id;
}

BUSTER_GLOBAL_LOCAL AnalysisResult* debug_analysis_for_entity(DebugModelInput* input, AnalysisEntityId entity)
{
    if (!input->analysis)
    {
        return 0;
    }
    if (entity.module.value == input->analysis->module.id.value || !input->analysis->program_modules)
    {
        return input->analysis;
    }
    for (u32 index = 0; index < input->analysis->program_module_count; index += 1)
    {
        AnalysisResult* candidate = input->analysis->program_modules[index];
        if (candidate && candidate->module.id.value == entity.module.value)
        {
            return candidate;
        }
    }
    return input->analysis;
}

BUSTER_GLOBAL_LOCAL AnalysisBody* debug_body_for_function(DebugModelInput* input, DebugFunctionSeed* seed)
{
    AnalysisResult* analysis = debug_analysis_for_entity(input, seed->entity);
    if (!analysis || seed->entity.index.value >= analysis->module.entity_count || !analysis->module.bodies)
    {
        return 0;
    }
    return analysis->module.bodies + seed->entity.index.value;
}

BUSTER_GLOBAL_LOCAL void debug_add_analysis_locals(Arena* arena, DebugModel* model, DebugModelInput* input, DebugFunction* function,
                                                   DebugFunctionSeed* seed, u32 variable_capacity)
{
    AnalysisResult* analysis = debug_analysis_for_entity(input, seed->entity);
    AnalysisBody* body = debug_body_for_function(input, seed);
    if (!analysis || !body)
    {
        return;
    }
    DebugScopeId scopes[256] = {0};
    u32 scope_depth = 0;
    scopes[0] = function->scope;
    for (u32 local_index = 0; local_index < body->local_count; local_index += 1)
    {
        AnalysisLocal* local = body->locals + local_index;
        u32 desired_depth = BUSTER_MIN(local->scope_depth, (u32)(BUSTER_ARRAY_LENGTH(scopes) - 1));
        while (scope_depth < desired_depth && model->scope_count < model->function_count + variable_capacity + 1)
        {
            scope_depth += 1;
            scopes[scope_depth] = debug_scope_add(arena, model, scopes[scope_depth - 1], DEBUG_SCOPE_LEXICAL,
                                                  debug_source_from_analysis(arena, analysis, seed->entity.index.value < analysis->module.entity_count
                                                                                              ? analysis->module.entities[seed->entity.index.value].source
                                                                                              : ANALYSIS_SOURCE_ID_INVALID,
                                                                              local->range),
                                                  function->code_offset, function->code_offset + function->code_size, variable_capacity);
        }
        DebugScope* scope = model->scopes + scopes[desired_depth];
        DebugVariableKind kind = local->kind == ANALYSIS_LOCAL_ARGUMENT ? DEBUG_VARIABLE_PARAMETER : DEBUG_VARIABLE_LOCAL;
        DebugVariableId variable = debug_variable_add(arena, model, input, scope, local->name, debug_frontend_type_id(model, local->type),
                                                      debug_source_from_analysis(arena, analysis,
                                                                                  seed->entity.index.value < analysis->module.entity_count
                                                                                      ? analysis->module.entities[seed->entity.index.value].source
                                                                                      : ANALYSIS_SOURCE_ID_INVALID,
                                                                                  local->range),
                                                      kind, seed->symbol, (IrLocalId){.value = local->id.value}, function->code_offset,
                                                      function->code_offset + function->code_size);
        if (variable != DEBUG_ID_INVALID)
        {
            function->variable_count += 1;
        }
    }
}

BUSTER_GLOBAL_LOCAL void debug_add_canonical_locals(Arena* arena, DebugModel* model, DebugModelInput* input, DebugFunction* function,
                                                    DebugFunctionSeed* seed, IrFunction* ir_function, u32 variable_capacity)
{
    if (!ir_function)
    {
        return;
    }
    u32 scope_capacity = ir_function->debug_local_count + 1;
    DebugScopeId* scopes = arena_allocate(arena, DebugScopeId, scope_capacity);
    u32 scope_depth = 0;
    scopes[0] = function->scope;
    for (u32 local_index = 0; local_index < ir_function->debug_local_count; local_index += 1)
    {
        IrDebugLocal* local = ir_function->debug_locals + local_index;
        if (local->id.value == IR_ID_UNDERLYING_INVALID)
        {
            continue;
        }
        u32 desired_depth = BUSTER_MIN(local->scope_depth, scope_capacity - 1);
        while (scope_depth < desired_depth && model->scope_count < model->function_count + variable_capacity + 1)
        {
            scope_depth += 1;
            scopes[scope_depth] = debug_scope_add(arena, model, scopes[scope_depth - 1], DEBUG_SCOPE_LEXICAL, debug_source_from_ir(arena, input->program, local->source),
                                                  function->code_offset, function->code_offset + function->code_size, variable_capacity);
        }
        DebugScope* scope = model->scopes + scopes[desired_depth];
        DebugVariableKind kind = local->is_parameter ? DEBUG_VARIABLE_PARAMETER : DEBUG_VARIABLE_LOCAL;
        DebugVariableId variable = debug_variable_add(arena, model, input, scope, local->name,
                                                      debug_canonical_type_id(model, local->type), debug_source_from_ir(arena, input->program, local->source),
                                                      kind, seed->symbol, local->id, function->code_offset,
                                                      function->code_offset + function->code_size);
        if (variable != DEBUG_ID_INVALID)
        {
            function->variable_count += 1;
        }
    }
    (void)variable_capacity;
}

BUSTER_GLOBAL_LOCAL void debug_add_canonical_globals(Arena* arena, DebugModel* model, DebugModelInput* input, u32 variable_capacity)
{
    if (!input->module || !input->program)
    {
        return;
    }
    for (u32 global_index = 0; global_index < input->module->global_count; global_index += 1)
    {
        IrGlobal* global = input->module->globals + global_index;
        if (global->symbol.value == IR_ID_UNDERLYING_INVALID || global->symbol.value >= input->program->symbols.count)
        {
            continue;
        }
        IrSymbol* symbol = input->program->symbols.symbols + global->symbol.value;
        DebugScope* scope = model->root_scope < model->scope_count ? model->scopes + model->root_scope : 0;
        if (!scope)
        {
            continue;
        }
        DebugVariableId variable = debug_variable_add(arena, model, input, scope, symbol->name, debug_canonical_type_id(model, global->type),
                                                      debug_source_from_ir(arena, input->program, global->source), DEBUG_VARIABLE_GLOBAL, global->symbol,
                                                      IR_LOCAL_ID_INVALID, 0, 1);
        if (variable != DEBUG_ID_INVALID)
        {
            model->variables[variable].linkage_name = debug_string(arena, symbol->link_name.length ? symbol->link_name : symbol->name);
        }
    }
    (void)variable_capacity;
}

BUSTER_GLOBAL_LOCAL void debug_add_analysis_globals(Arena* arena, DebugModel* model, DebugModelInput* input, u32 variable_capacity)
{
    if (!input->analysis || !input->module || !input->module->frontend_symbol_map)
    {
        return;
    }
    AnalysisResult* analysis = input->analysis;
    DebugScope* scope = model->root_scope < model->scope_count ? model->scopes + model->root_scope : 0;
    for (u32 entity_index = 0; entity_index < analysis->module.entity_count; entity_index += 1)
    {
        AnalysisEntity* entity = analysis->module.entities + entity_index;
        if (entity->kind != ANALYSIS_ENTITY_DATA || entity_index >= input->module->frontend_symbol_count)
        {
            continue;
        }
        IrSymbolId symbol = input->module->frontend_symbol_map[entity_index];
        AnalysisTypeId type = analysis->module.semantics[entity_index].type;
        DebugVariableId variable = debug_variable_add(arena, model, input, scope, entity->name, debug_frontend_type_id(model, type),
                                                      debug_source_from_analysis(arena, analysis, entity->source, entity->range), DEBUG_VARIABLE_GLOBAL, symbol,
                                                      IR_LOCAL_ID_INVALID, 0, 1);
        if (variable != DEBUG_ID_INVALID)
        {
            model->variables[variable].linkage_name = debug_string(arena, entity->name);
        }
    }
    (void)variable_capacity;
}

BUSTER_GLOBAL_LOCAL DebugSourceLocation debug_function_declaration(Arena* arena, DebugModelInput* input, DebugFunctionSeed* seed)
{
    if (seed->declaration.path.length)
    {
        DebugSourceLocation result = seed->declaration;
        result.path = debug_string(arena, result.path);
        return result;
    }
    if (input->canonical && input->program && seed->symbol.value < input->program->symbols.count)
    {
        return debug_source_from_ir(arena, input->program, input->program->symbols.symbols[seed->symbol.value].source);
    }
    AnalysisResult* analysis = debug_analysis_for_entity(input, seed->entity);
    if (analysis && seed->entity.index.value < analysis->module.entity_count)
    {
        AnalysisEntity* entity = analysis->module.entities + seed->entity.index.value;
        return debug_source_from_analysis(arena, analysis, entity->source, entity->range);
    }
    return (DebugSourceLocation){0};
}

BUSTER_GLOBAL_LOCAL void debug_model_fill_sources(Arena* arena, DebugModel* model, DebugModelInput* input)
{
    if (input->canonical && input->program)
    {
        model->source_count = input->program->sources.count;
        model->source_paths = model->source_count ? arena_allocate(arena, String8, model->source_count) : 0;
        for (u32 index = 0; index < model->source_count; index += 1)
        {
            model->source_paths[index] = debug_string(arena, input->program->sources.sources[index].path);
        }
    }
    else if (input->analysis)
    {
        model->source_count = input->analysis->module.source_count;
        model->source_paths = model->source_count ? arena_allocate(arena, String8, model->source_count) : 0;
        for (u32 index = 0; index < model->source_count; index += 1)
        {
            model->source_paths[index] = debug_string(arena, input->analysis->module.sources[index].path);
        }
    }
}

DebugModel debug_model_build(Arena* arena, DebugModelInput input)
{
    DebugModel result = {
        .producer = debug_string(arena, input.producer),
        .comp_dir = debug_string(arena, input.comp_dir),
        .root_scope = DEBUG_SCOPE_INVALID,
    };
    if (!arena || (input.function_count && !input.functions))
    {
        return result;
    }
    if (input.canonical && input.program)
    {
        result.type_count = input.program->types.count;
    }
    else if (input.analysis)
    {
        result.type_count = input.analysis->types.count;
    }
    result.function_count = input.function_count;
    result.inline_site_count = input.inline_site_count;
    u32 variable_capacity = 1;
    if (input.canonical && input.module)
    {
        variable_capacity = input.module->global_count + 1;
        for (u32 function_index = 0; function_index < input.module->function_count; function_index += 1)
        {
            variable_capacity += input.module->functions[function_index].debug_local_count;
        }
    }
    else if (input.analysis)
    {
        variable_capacity = input.analysis->module.data_count + 1;
        for (u32 entity_index = 0; entity_index < input.analysis->module.entity_count; entity_index += 1)
        {
            if (input.analysis->module.entities[entity_index].kind == ANALYSIS_ENTITY_CODE)
            {
                variable_capacity += input.analysis->module.bodies[entity_index].local_count;
            }
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
    if (result.source_count)
    {
        root_declaration.path = result.source_paths[0];
    }
    result.root_scope = debug_scope_add(arena, &result, DEBUG_SCOPE_INVALID, DEBUG_SCOPE_LEXICAL, root_declaration, 0, 1, variable_capacity);

    if (input.canonical && input.program)
    {
        for (u32 type_index = 0; type_index < result.type_count; type_index += 1)
        {
            debug_fill_ir_type(arena, &result, input.program, input.program->types.types + type_index, result.types + type_index);
        }
        bool* visited = arena_allocate(arena, bool, result.type_count ? result.type_count : 1);
        DebugTypeId* worklist = arena_allocate(arena, DebugTypeId, result.type_count ? result.type_count : 1);
        memset(visited, 0, sizeof(bool) * (result.type_count ? result.type_count : 1));
        u32 work_count = 0;
        for (u32 root = 0; root < result.type_count; root += 1)
        {
            if (!visited[root])
            {
                visited[root] = true;
                worklist[work_count++] = root;
            }
        }
        while (work_count)
        {
            DebugType* type = result.types + worklist[--work_count];
            if (type->element_type != DEBUG_ID_INVALID && type->element_type < result.type_count && !visited[type->element_type])
            {
                visited[type->element_type] = true;
                worklist[work_count++] = type->element_type;
            }
            if (type->return_type != DEBUG_ID_INVALID && type->return_type < result.type_count && !visited[type->return_type])
            {
                visited[type->return_type] = true;
                worklist[work_count++] = type->return_type;
            }
            for (u32 field_index = 0; field_index < type->field_count; field_index += 1)
            {
                DebugTypeId child = type->fields[field_index].type;
                if (child != DEBUG_ID_INVALID && child < result.type_count && !visited[child])
                {
                    visited[child] = true;
                    worklist[work_count++] = child;
                }
            }
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
    }
    else if (input.analysis)
    {
        for (u32 type_index = 0; type_index < result.type_count; type_index += 1)
        {
            debug_fill_analysis_type(arena, &result, input.analysis, input.module, type_index, result.types + type_index);
        }
        bool* visited = arena_allocate(arena, bool, result.type_count ? result.type_count : 1);
        DebugTypeId* worklist = arena_allocate(arena, DebugTypeId, result.type_count ? result.type_count : 1);
        memset(visited, 0, sizeof(bool) * (result.type_count ? result.type_count : 1));
        u32 work_count = 0;
        for (u32 root = 0; root < result.type_count; root += 1)
        {
            if (!visited[root])
            {
                visited[root] = true;
                worklist[work_count++] = root;
            }
        }
        while (work_count)
        {
            DebugType* type = result.types + worklist[--work_count];
            if (type->element_type != DEBUG_ID_INVALID && type->element_type < result.type_count && !visited[type->element_type])
            {
                visited[type->element_type] = true;
                worklist[work_count++] = type->element_type;
            }
            if (type->return_type != DEBUG_ID_INVALID && type->return_type < result.type_count && !visited[type->return_type])
            {
                visited[type->return_type] = true;
                worklist[work_count++] = type->return_type;
            }
            for (u32 field_index = 0; field_index < type->field_count; field_index += 1)
            {
                DebugTypeId child = type->fields[field_index].type;
                if (child != DEBUG_ID_INVALID && child < result.type_count && !visited[child])
                {
                    visited[child] = true;
                    worklist[work_count++] = child;
                }
            }
        }
    }

    for (u32 function_index = 0; function_index < input.function_count; function_index += 1)
    {
        DebugFunctionSeed* seed = input.functions + function_index;
        DebugFunction* function = result.functions + function_index;
        DebugSourceLocation declaration = debug_function_declaration(arena, &input, seed);
        IrTypeId canonical_type = IR_TYPE_ID_INVALID;
        if (input.program && seed->symbol.value < input.program->symbols.count)
        {
            canonical_type = input.program->symbols.symbols[seed->symbol.value].type;
        }
        *function = (DebugFunction){
            .name = debug_string(arena, seed->name),
            .declaration = declaration,
            .symbol = seed->symbol,
            .entity = seed->entity,
            .instantiation = seed->instantiation,
            .type = input.canonical ? debug_canonical_type_id(&result, canonical_type)
                                    : debug_frontend_type_id(&result, seed->entity.index.value < (input.analysis ? input.analysis->module.entity_count : 0)
                                                                         ? input.analysis->module.semantics[seed->entity.index.value].type
                                                                         : ANALYSIS_TYPE_ID_INVALID),
            .code_offset = seed->code_offset,
            .code_size = seed->code_size,
            .variable_start = result.variable_count,
        };
        if (!function->name.length)
        {
            function->name = debug_string(arena, declaration.path);
        }
        function->scope = debug_scope_add(arena, &result, result.root_scope, DEBUG_SCOPE_FUNCTION, declaration, function->code_offset,
                                          function->code_offset + function->code_size, variable_capacity);
        if (input.canonical && input.module && function_index < input.module->function_count)
        {
            debug_add_canonical_locals(arena, &result, &input, function, seed, input.module->functions + function_index, variable_capacity);
        }
        else
        {
            debug_add_analysis_locals(arena, &result, &input, function, seed, variable_capacity);
        }
    }
    if (input.canonical)
    {
        debug_add_canonical_globals(arena, &result, &input, variable_capacity);
    }
    else
    {
        debug_add_analysis_globals(arena, &result, &input, variable_capacity);
    }
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
    return result;
}

DebugTypeId debug_model_find_canonical_type(DebugModel* model, IrTypeId type)
{
    if (!model)
    {
        return DEBUG_ID_INVALID;
    }
    for (u32 index = 0; index < model->type_count; index += 1)
    {
        if (model->types[index].canonical_type.value == type.value)
        {
            return index;
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

u32 debug_register_codeview_number(Target target, DebugRegister reg)
{
    u32 register_index = (u32)reg;
    if (target.cpu_arch == CPU_ARCH_X86_64)
    {
        if (register_index >= (u32)DEBUG_REGISTER_X86_RAX && register_index <= (u32)DEBUG_REGISTER_X86_R15)
        {
            return 328 + register_index - (u32)DEBUG_REGISTER_X86_RAX;
        }
        if (register_index >= (u32)DEBUG_REGISTER_X86_XMM0 && register_index <= (u32)DEBUG_REGISTER_X86_XMM15)
        {
            return 154 + register_index - (u32)DEBUG_REGISTER_X86_XMM0;
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

#if BUSTER_INCLUDE_TESTS
UnitTestResult debug_model_tests(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    BUSTER_TEST(arguments, debug_register_dwarf_number((Target){.cpu_arch = CPU_ARCH_X86_64}, DEBUG_REGISTER_X86_RAX) == 0);
    BUSTER_TEST(arguments, debug_register_dwarf_number((Target){.cpu_arch = CPU_ARCH_X86_64}, DEBUG_REGISTER_X86_RSP) == 7);
    BUSTER_TEST(arguments, debug_register_dwarf_number((Target){.cpu_arch = CPU_ARCH_AARCH64}, DEBUG_REGISTER_AARCH64_X29) == 29);
    BUSTER_TEST(arguments, debug_register_codeview_number((Target){.cpu_arch = CPU_ARCH_X86_64}, DEBUG_REGISTER_X86_R15) == 343);

    // Location transitions are intentionally tested independently of a
    // backend allocator: the neutral model must retain every range and every
    // aggregate piece the backend supplies.
    DebugLocationPiece pieces[] = {
        {.kind = DEBUG_LOCATION_FRAME, .frame_offset = -16, .value_offset = 0, .size = 4},
        {.kind = DEBUG_LOCATION_REGISTER, .reg = DEBUG_REGISTER_X86_RAX, .value_offset = 4, .size = 4},
    };
    DebugLocationSeed locations[] = {
        {.function_symbol = {.value = 7}, .local = {.value = 3}, .start = 0, .end = 8,
         .location = {.kind = DEBUG_LOCATION_REGISTER, .reg = DEBUG_REGISTER_X86_R10}},
        {.function_symbol = {.value = 7}, .local = {.value = 3}, .start = 8, .end = 16,
         .location = {.kind = DEBUG_LOCATION_FRAME, .frame_offset = -24}},
        {.function_symbol = {.value = 7}, .local = {.value = 3}, .start = 16, .end = 24,
         .location = {.kind = DEBUG_LOCATION_PIECEWISE, .pieces = pieces, .piece_count = BUSTER_ARRAY_LENGTH(pieces)}},
        {.function_symbol = {.value = 7}, .local = {.value = 3}, .start = 24, .end = 32,
         .location = {.kind = DEBUG_LOCATION_CONSTANT, .constant = 42}},
        {.function_symbol = {.value = 7}, .local = {.value = 3}, .start = 32, .end = 40,
         .location = {.kind = DEBUG_LOCATION_UNAVAILABLE}},
    };
    DebugModelInput location_input = {
        .locations = locations,
        .location_count = BUSTER_ARRAY_LENGTH(locations),
    };
    DebugVariable variable = {.local = {.value = 3}};
    debug_variable_add_location(arguments->arena, &location_input, &variable, (IrSymbolId){.value = 7}, (IrLocalId){.value = 3}, 0, 40);
    BUSTER_TEST(arguments, variable.location_count == BUSTER_ARRAY_LENGTH(locations));
    BUSTER_TEST(arguments, variable.locations[0].location.kind == DEBUG_LOCATION_REGISTER);
    BUSTER_TEST(arguments, variable.locations[1].location.kind == DEBUG_LOCATION_FRAME && variable.locations[1].location.frame_offset == -24);
    BUSTER_TEST(arguments, variable.locations[2].location.kind == DEBUG_LOCATION_PIECEWISE && variable.locations[2].location.piece_count == 2);
    BUSTER_TEST(arguments, variable.locations[3].location.kind == DEBUG_LOCATION_CONSTANT && variable.locations[3].location.constant == 42);
    BUSTER_TEST(arguments, variable.locations[4].location.kind == DEBUG_LOCATION_UNAVAILABLE);

    DebugModel scope_model = {
        .scopes = arena_allocate(arguments->arena, DebugScope, 4),
        .variables = arena_allocate(arguments->arena, DebugVariable, 4),
    };
    DebugScopeId function_scope = debug_scope_add(arguments->arena, &scope_model, DEBUG_SCOPE_INVALID, DEBUG_SCOPE_FUNCTION,
                                                   (DebugSourceLocation){.path = S8("source.c"), .line = 4}, 0, 40, 4);
    DebugScopeId lexical_scope = debug_scope_add(arguments->arena, &scope_model, function_scope, DEBUG_SCOPE_LEXICAL,
                                                  (DebugSourceLocation){.path = S8("source.c"), .line = 6}, 8, 32, 4);
    DebugVariableId scope_variable = debug_variable_add(arguments->arena, &scope_model, &location_input, scope_model.scopes + lexical_scope,
                                                         S8("value"), 0, (DebugSourceLocation){.path = S8("source.c"), .line = 7},
                                                         DEBUG_VARIABLE_LOCAL, (IrSymbolId){.value = 7}, (IrLocalId){.value = 3}, 8, 32);
    BUSTER_TEST(arguments, function_scope == 0 && lexical_scope == 1);
    BUSTER_TEST(arguments, scope_variable == 0 && scope_model.scopes[lexical_scope].variable_count == 1);
    BUSTER_TEST(arguments, scope_model.variables[scope_variable].declaration.line == 7);

    // Canonical IR type graphs may be recursive.  The model keeps the
    // frontend names while preserving the cycle through explicit IDs.
    IrField canonical_field = {.name = S8("next"), .type = {.value = 2}, .source = {.source = {.value = 0}, .line = 3}};
    IrType canonical_types[] = {
        {.name = S8("void"), .id = {.value = 0}, .unqualified_type = IR_TYPE_ID_INVALID, .kind = IR_TYPE_VOID},
        {.name = S8("Node"), .id = {.value = 1}, .unqualified_type = IR_TYPE_ID_INVALID, .fields = &canonical_field, .field_count = 1,
         .layout = {.size = 8, .alignment = 8}, .kind = IR_TYPE_STRUCT},
        {.name = S8("Node*"), .id = {.value = 2}, .unqualified_type = IR_TYPE_ID_INVALID, .element_type = {.value = 1},
         .layout = {.size = 8, .alignment = 8}, .kind = IR_TYPE_POINTER},
    };
    IrSource canonical_source = {.path = S8("node.bbb"), .id = {.value = 0}};
    IrSymbol canonical_symbol = {
        .name = S8("node_function"), .id = {.value = 0}, .type = {.value = 1}, .source = {.source = {.value = 0}, .line = 2},
        .kind = IR_SYMBOL_FUNCTION,
    };
    IrProgram canonical_program = {
        .types = {.types = canonical_types, .count = BUSTER_ARRAY_LENGTH(canonical_types)},
        .symbols = {.symbols = &canonical_symbol, .count = 1},
        .sources = {.sources = &canonical_source, .count = 1},
    };
    DebugFunctionSeed canonical_function = {.name = S8("node_function"), .symbol = {.value = 0}, .code_size = 16};
    DebugModel canonical_model = debug_model_build(arguments->arena, (DebugModelInput){
                                                                          .program = &canonical_program,
                                                                          .functions = &canonical_function,
                                                                          .function_count = 1,
                                                                          .canonical = true,
                                                                      });
    BUSTER_TEST(arguments, canonical_model.valid && canonical_model.type_count == 3);
    BUSTER_TEST(arguments, canonical_model.types[1].kind == DEBUG_TYPE_STRUCT && canonical_model.types[1].fields[0].type == 2);
    BUSTER_TEST(arguments, canonical_model.types[2].kind == DEBUG_TYPE_POINTER && canonical_model.types[2].element_type == 1);
    BUSTER_TEST(arguments, string_equal(canonical_model.types[1].name, S8("Node")));

    // The Buster frontend uses frontend type IDs before canonical lowering;
    // its aggregate graph is mapped through the same neutral representation.
    AnalysisSource analysis_source = {.path = S8("node.bbb"), .id = {.value = 0}};
    AnalysisEntity analysis_entity = {
        .name = S8("Node"), .id = {.module = {.value = 0}, .index = {.value = 0}}, .source = {.value = 0},
        .kind = ANALYSIS_ENTITY_TYPE,
    };
    AnalysisField analysis_field = {.name = S8("next"), .type = {.value = 2}, .range = {.line = 3}};
    AnalysisEntitySemantic analysis_semantic = {.fields = &analysis_field, .field_count = 1};
    AnalysisType analysis_types[] = {
        {.name = S8("void"), .id = {.value = 0}, .kind = ANALYSIS_TYPE_VOID},
        {.name = S8("Node"), .id = {.value = 1}, .kind = ANALYSIS_TYPE_STRUCT, .layout = {.size = 8, .alignment = 8},
         .as.declaration = {.module = {.value = 0}, .index = {.value = 0}}},
        {.name = S8("Node*"), .id = {.value = 2}, .kind = ANALYSIS_TYPE_POINTER, .layout = {.size = 8, .alignment = 8},
         .as.element_type = {.value = 1}},
    };
    AnalysisResult analysis = {
        .module = {.sources = &analysis_source, .entities = &analysis_entity, .semantics = &analysis_semantic, .id = {.value = 0},
                   .source_count = 1, .entity_count = 1},
        .types = {.types = analysis_types, .count = BUSTER_ARRAY_LENGTH(analysis_types)},
    };
    DebugFunctionSeed analysis_function = {
        .name = S8("analysis_function"), .declaration = {.path = S8("node.bbb"), .source = 0, .line = 1}, .code_size = 16,
    };
    DebugModel analysis_model = debug_model_build(arguments->arena, (DebugModelInput){
                                                                         .analysis = &analysis,
                                                                         .functions = &analysis_function,
                                                                         .function_count = 1,
                                                                     });
    BUSTER_TEST(arguments, analysis_model.valid && analysis_model.type_count == 3);
    BUSTER_TEST(arguments, analysis_model.types[1].kind == DEBUG_TYPE_STRUCT && analysis_model.types[1].field_count == 1);
    BUSTER_TEST(arguments, analysis_model.types[1].fields[0].type == 2 && analysis_model.types[2].element_type == 1);
    return result;
}
#endif
