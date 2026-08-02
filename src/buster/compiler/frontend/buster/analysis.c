#include <buster/compiler/frontend/buster/analysis.h>

#include <buster/file.h>
#include <buster/string.h>

typedef struct AnalysisProgramDiscovery AnalysisProgramDiscovery;
struct AnalysisProgramDiscovery
{
    AnalysisProgramDiscovery* next;
    String8 name;
    String8 path;
    String8 source;
    FileMapRead source_map;
    ParserResult parser;
};

BUSTER_GLOBAL_LOCAL s32 analysis_string_compare(String8 left, String8 right);

BUSTER_GLOBAL_LOCAL bool analysis_path_has_suffix(String8 path, String8 suffix)
{
    return path.length >= suffix.length && string_equal(string_slice(path, path.length - suffix.length, path.length), suffix);
}

BUSTER_GLOBAL_LOCAL String8 analysis_module_path(Arena* arena, String8 module_root, String8 module_name)
{
    bool root_has_separator = module_root.length && (module_root.pointer[module_root.length - 1] == '/' || module_root.pointer[module_root.length - 1] == '\\');
    bool has_extension = analysis_path_has_suffix(module_name, S8(".bbb"));
    if (!module_root.length)
    {
        return has_extension ? string_duplicate_arena(arena, module_name, true)
                             : string_duplicate_arena(arena, string_format(arena, S8("{S8}.bbb"), module_name), true);
    }
    String8 formatted = string_format(
        arena, root_has_separator ? (has_extension ? S8("{S8}{S8}") : S8("{S8}{S8}.bbb")) : (has_extension ? S8("{S8}/{S8}") : S8("{S8}/{S8}.bbb")),
        module_root, module_name);
    return string_duplicate_arena(arena, formatted, true);
}

BUSTER_GLOBAL_LOCAL AnalysisProgramDiscovery* analysis_program_discovery_find(AnalysisProgramDiscovery* first, String8 name)
{
    for (AnalysisProgramDiscovery* it = first; it; it = it->next)
    {
        if (string_equal(it->name, name))
        {
            return it;
        }
    }
    return 0;
}

BUSTER_GLOBAL_LOCAL bool analysis_program_module_after(AnalysisProgramModule left, AnalysisProgramModule right)
{
    return analysis_string_compare(left.name, right.name) > 0;
}

BUSTER_GLOBAL_LOCAL s32 analysis_string_compare(String8 left, String8 right)
{
    u64 common_length = BUSTER_MIN(left.length, right.length);
    for (u64 index = 0; index < common_length; index += 1)
    {
        u8 left_byte = (u8)left.pointer[index];
        u8 right_byte = (u8)right.pointer[index];
        if (left_byte != right_byte)
        {
            return left_byte < right_byte ? -1 : 1;
        }
    }
    if (left.length == right.length)
    {
        return 0;
    }
    return left.length < right.length ? -1 : 1;
}

BUSTER_GLOBAL_LOCAL bool analysis_source_after(AnalysisSource left, AnalysisSource right)
{
    s32 path_order = analysis_string_compare(left.path, right.path);
    if (path_order != 0)
    {
        return path_order > 0;
    }
    return left.original_input_index > right.original_input_index;
}

BUSTER_GLOBAL_LOCAL bool analysis_entity_after(AnalysisEntity left, AnalysisEntity right)
{
    if (left.source.value != right.source.value)
    {
        return left.source.value > right.source.value;
    }
    if (left.range.offset != right.range.offset)
    {
        return left.range.offset > right.range.offset;
    }
    return left.kind > right.kind;
}

BUSTER_GLOBAL_LOCAL void analysis_diagnostic_append(AnalysisResult* result, AnalysisDiagnostic* diagnostic)
{
    if (result->last_diagnostic)
    {
        result->last_diagnostic->next = diagnostic;
    }
    else
    {
        result->first_diagnostic = diagnostic;
    }
    result->last_diagnostic = diagnostic;
    result->diagnostic_count += 1;
}

BUSTER_GLOBAL_LOCAL void analysis_duplicate_diagnostic_push(Arena* arena, AnalysisResult* result, AnalysisEntity* entity, AnalysisEntity* previous)
{
    AnalysisDiagnostic* diagnostic = arena_allocate(arena, AnalysisDiagnostic, 1);
    *diagnostic = (AnalysisDiagnostic){
        .message = S8("duplicate declaration in module"),
        .range = entity->range,
        .entity = entity->id,
        .previous_entity = previous->id,
        .source = entity->source,
        .kind = ANALYSIS_DIAGNOSTIC_DUPLICATE_DECLARATION,
        .subject = entity->name,
    };
    AnalysisDiagnosticNote* note = arena_allocate(arena, AnalysisDiagnosticNote, 1);
    *note = (AnalysisDiagnosticNote){
        .message = S8("previous declaration is here"),
        .range = previous->range,
        .entity = previous->id,
        .source = previous->source,
    };
    diagnostic->first_note = note;
    diagnostic->last_note = note;
    analysis_diagnostic_append(result, diagnostic);
}

BUSTER_GLOBAL_LOCAL void analysis_import_diagnostic_push(Arena* arena, AnalysisResult* result, AnalysisImport* import, AnalysisDiagnosticKind kind,
                                                         String8 message, AnalysisImport* previous)
{
    AnalysisDiagnostic* diagnostic = arena_allocate(arena, AnalysisDiagnostic, 1);
    *diagnostic = (AnalysisDiagnostic){
        .message = message,
        .range = import->range,
        .entity = ANALYSIS_ENTITY_ID_INVALID,
        .previous_entity = ANALYSIS_ENTITY_ID_INVALID,
        .source = import->source,
        .kind = kind,
        .subject = kind == ANALYSIS_DIAGNOSTIC_DUPLICATE_IMPORT_NAMESPACE ? import->name_space : import->path,
        .expected_type = ANALYSIS_TYPE_ID_INVALID,
        .actual_type = ANALYSIS_TYPE_ID_INVALID,
    };
    if (previous)
    {
        AnalysisDiagnosticNote* note = arena_allocate(arena, AnalysisDiagnosticNote, 1);
        *note = (AnalysisDiagnosticNote){
            .message = S8("previous import namespace is here"),
            .range = previous->range,
            .entity = ANALYSIS_ENTITY_ID_INVALID,
            .source = previous->source,
        };
        diagnostic->first_note = note;
        diagnostic->last_note = note;
    }
    analysis_diagnostic_append(result, diagnostic);
}

BUSTER_GLOBAL_LOCAL void analysis_type_diagnostic_push(Arena* arena, AnalysisResult* result, AnalysisEntity* entity, ParserSourceRange range,
                                                       AnalysisDiagnosticKind kind, String8 subject)
{
    String8 message = S8("unknown type");
    if (kind == ANALYSIS_DIAGNOSTIC_TYPE_ALIAS_CYCLE)
    {
        message = S8("type alias cycle");
    }
    else if (kind == ANALYSIS_DIAGNOSTIC_INVALID_VECTOR_TYPE)
    {
        message = S8("vector type must contain 64, 128, 256, or 512 bits of integer or floating-point lanes");
    }
    AnalysisDiagnostic* diagnostic = arena_allocate(arena, AnalysisDiagnostic, 1);
    *diagnostic = (AnalysisDiagnostic){
        .message = message,
        .range = range,
        .entity = entity->id,
        .previous_entity = ANALYSIS_ENTITY_ID_INVALID,
        .source = entity->source,
        .kind = kind,
        .subject = subject,
    };
    analysis_diagnostic_append(result, diagnostic);
}

BUSTER_GLOBAL_LOCAL void analysis_entity_diagnostic_push(Arena* arena, AnalysisResult* result, AnalysisEntity* entity, ParserSourceRange range,
                                                         AnalysisDiagnosticKind kind, String8 subject, String8 message)
{
    AnalysisDiagnostic* diagnostic = arena_allocate(arena, AnalysisDiagnostic, 1);
    *diagnostic = (AnalysisDiagnostic){
        .message = message,
        .range = range,
        .entity = entity->id,
        .previous_entity = ANALYSIS_ENTITY_ID_INVALID,
        .source = entity->source,
        .kind = kind,
        .subject = subject,
        .expected_type = ANALYSIS_TYPE_ID_INVALID,
        .actual_type = ANALYSIS_TYPE_ID_INVALID,
    };
    analysis_diagnostic_append(result, diagnostic);
}

AnalysisResult analysis_index_module(Arena* result_arena, AnalysisModuleId module_id, String8 module_name, AnalysisSourceInput* inputs, u32 input_count)
{
    AnalysisResult result = {0};
    result.module.id = module_id;
    result.module.name = string_duplicate_arena(result_arena, module_name, false);
    result.module.source_count = input_count;
    result.module.sources = arena_allocate(result_arena, AnalysisSource, input_count);

    u32 entity_count = 0;
    u32 import_count = 0;
    for (u32 input_index = 0; input_index < input_count; input_index += 1)
    {
        AnalysisSourceInput* input = inputs + input_index;
        result.module.sources[input_index] = (AnalysisSource){
            .path = string_duplicate_arena(result_arena, input->path, false),
            .parser = input->parser,
            .id = ANALYSIS_SOURCE_ID_INVALID,
            .original_input_index = input_index,
        };
        if (input->parser)
        {
            import_count += input->parser->import_count;
            entity_count += input->parser->type_declaration_count;
            entity_count += input->parser->code_count;
            entity_count += input->parser->data_declaration_count;
        }
    }

    for (u32 index = 1; index < input_count; index += 1)
    {
        AnalysisSource value = result.module.sources[index];
        u32 insertion = index;
        while (insertion && analysis_source_after(result.module.sources[insertion - 1], value))
        {
            result.module.sources[insertion] = result.module.sources[insertion - 1];
            insertion -= 1;
        }
        result.module.sources[insertion] = value;
    }

    result.module.import_count = import_count;
    result.module.imports = arena_allocate(result_arena, AnalysisImport, import_count);
    result.module.entity_count = entity_count;
    result.module.entities = arena_allocate(result_arena, AnalysisEntity, entity_count);
    result.module.semantics = arena_allocate(result_arena, AnalysisEntitySemantic, entity_count);
    result.module.bodies = arena_allocate(result_arena, AnalysisBody, entity_count);
    u32 import_index = 0;
    u32 entity_index = 0;
    for (u32 source_index = 0; source_index < input_count; source_index += 1)
    {
        AnalysisSource* source = result.module.sources + source_index;
        source->id = (AnalysisSourceId){.value = source_index};
        if (!source->parser)
        {
            continue;
        }

        for (AstImport* import = source->parser->first_import; import; import = import->next)
        {
            result.module.imports[import_index] = (AnalysisImport){
                .name_space = string_duplicate_arena(result_arena, import->name_space.text, false),
                .path = string_duplicate_arena(result_arena, import->path, false),
                .range = import->range,
                .path_range = import->path_range,
                .source = source->id,
                .target_id = ANALYSIS_MODULE_ID_INVALID,
                .state = ANALYSIS_IMPORT_UNRESOLVED,
            };
            import_index += 1;
        }

        for (AstTypeDeclaration* type = source->parser->first_type_declaration; type; type = type->next)
        {
            result.module.entities[entity_index] = (AnalysisEntity){
                .name = type->name.text,
                .range = type->range,
                .source = source->id,
                .kind = ANALYSIS_ENTITY_TYPE,
                .name_space = ANALYSIS_NAMESPACE_TYPE,
                .ast.type_declaration = type,
            };
            entity_index += 1;
            result.module.type_count += 1;
        }
        for (AstCode* code = source->parser->first_code; code; code = code->next)
        {
            result.module.entities[entity_index] = (AnalysisEntity){
                .name = code->name,
                .range = code->range,
                .source = source->id,
                .kind = ANALYSIS_ENTITY_CODE,
                .name_space = ANALYSIS_NAMESPACE_VALUE,
                .ast.code = code,
            };
            entity_index += 1;
            result.module.code_count += 1;
        }
        for (AstDataDeclaration* data = source->parser->first_data_declaration; data; data = data->next)
        {
            result.module.entities[entity_index] = (AnalysisEntity){
                .name = data->name.text,
                .range = data->range,
                .source = source->id,
                .kind = ANALYSIS_ENTITY_DATA,
                .name_space = ANALYSIS_NAMESPACE_VALUE,
                .ast.data = data,
            };
            entity_index += 1;
            result.module.data_count += 1;
        }
    }

    BUSTER_CHECK(import_index == import_count);
    BUSTER_CHECK(entity_index == entity_count);
    for (u32 index = 0; index < import_count; index += 1)
    {
        AnalysisImport* import = result.module.imports + index;
        for (u32 previous_index = 0; previous_index < index; previous_index += 1)
        {
            AnalysisImport* previous = result.module.imports + previous_index;
            if (string_equal(import->name_space, previous->name_space))
            {
                analysis_import_diagnostic_push(result_arena, &result, import, ANALYSIS_DIAGNOSTIC_DUPLICATE_IMPORT_NAMESPACE, S8("duplicate import namespace"),
                                                previous);
                break;
            }
        }
    }
    for (u32 index = 1; index < entity_count; index += 1)
    {
        AnalysisEntity value = result.module.entities[index];
        u32 insertion = index;
        while (insertion && analysis_entity_after(result.module.entities[insertion - 1], value))
        {
            result.module.entities[insertion] = result.module.entities[insertion - 1];
            insertion -= 1;
        }
        result.module.entities[insertion] = value;
    }

    for (u32 index = 0; index < entity_count; index += 1)
    {
        AnalysisEntity* entity = result.module.entities + index;
        result.module.semantics[index] = (AnalysisEntitySemantic){
            .type = ANALYSIS_TYPE_ID_INVALID,
            .state = ANALYSIS_RESOLUTION_UNRESOLVED,
        };
        result.module.bodies[index] = (AnalysisBody){0};
        entity->id = (AnalysisEntityId){
            .module = module_id,
            .index = {.value = index},
        };
        for (u32 previous_index = 0; previous_index < index; previous_index += 1)
        {
            AnalysisEntity* previous = result.module.entities + previous_index;
            if (entity->name_space == previous->name_space && string_equal(entity->name, previous->name))
            {
                analysis_duplicate_diagnostic_push(result_arena, &result, entity, previous);
                break;
            }
        }
    }

    return result;
}

typedef struct AnalysisImportTraversalFrame AnalysisImportTraversalFrame;
struct AnalysisImportTraversalFrame
{
    u32 module_index;
    u32 next_import;
};

void analysis_resolve_imports(Arena* result_arena, AnalysisResult** modules, u32 module_count)
{
    AnalysisResult** program_modules = arena_allocate(result_arena, AnalysisResult*, module_count);
    for (u32 module_index = 0; module_index < module_count; module_index += 1)
    {
        program_modules[module_index] = modules[module_index];
    }
    for (u32 module_index = 0; module_index < module_count; module_index += 1)
    {
        AnalysisResult* module = modules[module_index];
        if (!module)
        {
            continue;
        }
        module->program_modules = program_modules;
        module->program_module_count = module_count;

        for (u32 import_index = 0; import_index < module->module.import_count; import_index += 1)
        {
            AnalysisImport* import = module->module.imports + import_index;
            import->target = 0;
            import->target_id = ANALYSIS_MODULE_ID_INVALID;
            import->state = ANALYSIS_IMPORT_UNRESOLVED;
            u32 match_count = 0;
            for (u32 candidate_index = 0; candidate_index < module_count; candidate_index += 1)
            {
                AnalysisResult* candidate = modules[candidate_index];
                if (candidate && string_equal(candidate->module.name, import->path))
                {
                    import->target = candidate;
                    import->target_id = candidate->module.id;
                    match_count += 1;
                }
            }

            if (match_count == 0)
            {
                import->state = ANALYSIS_IMPORT_MISSING;
                analysis_import_diagnostic_push(result_arena, module, import, ANALYSIS_DIAGNOSTIC_MISSING_IMPORTED_MODULE, S8("imported module was not found"),
                                                0);
            }
            else if (match_count > 1)
            {
                import->target = 0;
                import->target_id = ANALYSIS_MODULE_ID_INVALID;
                import->state = ANALYSIS_IMPORT_AMBIGUOUS;
                analysis_import_diagnostic_push(result_arena, module, import, ANALYSIS_DIAGNOSTIC_AMBIGUOUS_IMPORTED_MODULE,
                                                S8("imported module name is ambiguous"), 0);
            }
            else
            {
                import->state = ANALYSIS_IMPORT_RESOLVED;
            }
        }
    }

    Arena* conflicts[] = {result_arena};
    TemporalArena scratch = scratch_begin(conflicts, BUSTER_ARRAY_LENGTH(conflicts));
    u8* colors = arena_allocate(scratch.arena, u8, module_count);
    AnalysisImportTraversalFrame* frames = arena_allocate(scratch.arena, AnalysisImportTraversalFrame, module_count);
    for (u32 module_index = 0; module_index < module_count; module_index += 1)
    {
        colors[module_index] = 0;
    }

    for (u32 root = 0; root < module_count; root += 1)
    {
        if (!modules[root] || colors[root])
        {
            continue;
        }

        u32 frame_count = 1;
        frames[0] = (AnalysisImportTraversalFrame){.module_index = root};
        colors[root] = 1;
        while (frame_count)
        {
            AnalysisImportTraversalFrame* frame = frames + frame_count - 1;
            AnalysisResult* module = modules[frame->module_index];
            if (frame->next_import >= module->module.import_count)
            {
                colors[frame->module_index] = 2;
                frame_count -= 1;
                continue;
            }

            AnalysisImport* import = module->module.imports + frame->next_import;
            frame->next_import += 1;
            if (import->state != ANALYSIS_IMPORT_RESOLVED)
            {
                continue;
            }

            u32 target_index = module_count;
            for (u32 candidate_index = 0; candidate_index < module_count; candidate_index += 1)
            {
                if (modules[candidate_index] == import->target)
                {
                    target_index = candidate_index;
                    break;
                }
            }
            BUSTER_CHECK(target_index < module_count);

            if (colors[target_index] == 1)
            {
                import->state = ANALYSIS_IMPORT_CYCLE;
                analysis_import_diagnostic_push(result_arena, module, import, ANALYSIS_DIAGNOSTIC_IMPORT_CYCLE, S8("module import cycle"), 0);
            }
            else if (colors[target_index] == 0)
            {
                BUSTER_CHECK(frame_count < module_count);
                frames[frame_count] = (AnalysisImportTraversalFrame){
                    .module_index = target_index,
                };
                frame_count += 1;
                colors[target_index] = 1;
            }
        }
    }
    scratch_end(scratch);
}

void analysis_resolve_program_interfaces(Arena* result_arena, AnalysisResult** modules, u32 module_count)
{
    analysis_resolve_imports(result_arena, modules, module_count);
    Arena* conflicts[] = {result_arena};
    TemporalArena scratch = scratch_begin(conflicts, BUSTER_ARRAY_LENGTH(conflicts));
    bool* resolved = arena_allocate(scratch.arena, bool, module_count);
    u32 resolved_count = 0;
    for (u32 module_index = 0; module_index < module_count; module_index += 1)
    {
        resolved[module_index] = false;
        if (!modules[module_index])
        {
            resolved[module_index] = true;
            resolved_count += 1;
        }
    }
    while (resolved_count < module_count)
    {
        bool progressed = false;
        for (u32 module_index = 0; module_index < module_count; module_index += 1)
        {
            AnalysisResult* module = modules[module_index];
            if (!module || resolved[module_index])
            {
                continue;
            }

            bool ready = true;
            for (u32 import_index = 0; import_index < module->module.import_count; import_index += 1)
            {
                AnalysisImport* import = module->module.imports + import_index;
                if (import->state == ANALYSIS_IMPORT_RESOLVED && import->target && !import->target->types.types)
                {
                    ready = false;
                    break;
                }
            }
            if (!ready)
            {
                continue;
            }

            if (!module->types.types)
            {
                analysis_resolve_module_interfaces(result_arena, module);
            }
            resolved[module_index] = true;
            resolved_count += 1;
            progressed = true;
        }
        if (!progressed)
        {
            for (u32 module_index = 0; module_index < module_count; module_index += 1)
            {
                AnalysisResult* module = modules[module_index];
                if (module && !resolved[module_index])
                {
                    analysis_resolve_module_interfaces(result_arena, module);
                    resolved[module_index] = true;
                    resolved_count += 1;
                }
            }
        }
    }
    scratch_end(scratch);
}

AnalysisEntity* analysis_find_qualified_entity(AnalysisResult* module, String8 import_name_space, String8 entity_name, AnalysisNamespace name_space)
{
    AnalysisResult* target = 0;
    for (u32 import_index = 0; import_index < module->module.import_count; import_index += 1)
    {
        AnalysisImport* import = module->module.imports + import_index;
        if (import->state == ANALYSIS_IMPORT_RESOLVED && string_equal(import->name_space, import_name_space))
        {
            if (target)
            {
                return 0;
            }
            target = import->target;
        }
    }
    if (!target)
    {
        return 0;
    }

    for (u32 entity_index = 0; entity_index < target->module.entity_count; entity_index += 1)
    {
        AnalysisEntity* entity = target->module.entities + entity_index;
        if (entity->name_space == name_space && string_equal(entity->name, entity_name))
        {
            return entity;
        }
    }
    return 0;
}

typedef enum AnalysisCanonicalTaskKind
{
    ANALYSIS_CANONICAL_TASK_TEXT,
    ANALYSIS_CANONICAL_TASK_TYPE,
    ANALYSIS_CANONICAL_TASK_CONSTANT,
} AnalysisCanonicalTaskKind;

typedef struct AnalysisCanonicalTask AnalysisCanonicalTask;
struct AnalysisCanonicalTask
{
    AnalysisCanonicalTask* previous;
    String8 text;
    AnalysisTypeId type;
    AnalysisConstant constant;
    AnalysisCanonicalTaskKind kind;
};

typedef struct AnalysisCanonicalPart AnalysisCanonicalPart;
struct AnalysisCanonicalPart
{
    AnalysisCanonicalPart* next;
    String8 text;
};

BUSTER_GLOBAL_LOCAL void analysis_canonical_task_push(Arena* scratch_arena, AnalysisCanonicalTask** top, AnalysisCanonicalTask task)
{
    AnalysisCanonicalTask* pushed = arena_allocate(scratch_arena, AnalysisCanonicalTask, 1);
    task.previous = *top;
    *pushed = task;
    *top = pushed;
}

BUSTER_GLOBAL_LOCAL void analysis_canonical_part_push(Arena* scratch_arena, AnalysisCanonicalPart** first, AnalysisCanonicalPart** last, u32* count,
                                                      String8 text)
{
    AnalysisCanonicalPart* part = arena_allocate(scratch_arena, AnalysisCanonicalPart, 1);
    *part = (AnalysisCanonicalPart){.text = text};
    if (*last)
    {
        (*last)->next = part;
    }
    else
    {
        *first = part;
    }
    *last = part;
    *count += 1;
}

BUSTER_GLOBAL_LOCAL AnalysisResult* analysis_canonical_module_from_id(AnalysisResult* result, AnalysisModuleId id)
{
    if (result->module.id.value == id.value)
    {
        return result;
    }
    for (u32 index = 0; index < result->program_module_count; index += 1)
    {
        AnalysisResult* candidate = result->program_modules[index];
        if (candidate && candidate->module.id.value == id.value)
        {
            return candidate;
        }
    }
    return 0;
}

BUSTER_GLOBAL_LOCAL String8 analysis_canonical_tasks_join(Arena* result_arena, Arena* scratch_arena, AnalysisResult* result, AnalysisCanonicalTask* top)
{
    AnalysisCanonicalPart* first = 0;
    AnalysisCanonicalPart* last = 0;
    u32 part_count = 0;
    while (top)
    {
        AnalysisCanonicalTask task = *top;
        top = top->previous;
        if (task.kind == ANALYSIS_CANONICAL_TASK_TEXT)
        {
            analysis_canonical_part_push(scratch_arena, &first, &last, &part_count, task.text);
            continue;
        }
        if (task.kind == ANALYSIS_CANONICAL_TASK_CONSTANT)
        {
            AnalysisConstant constant = task.constant;
            if (constant.kind == ANALYSIS_CONSTANT_ARRAY || constant.kind == ANALYSIS_CONSTANT_AGGREGATE)
            {
                analysis_canonical_part_push(scratch_arena, &first, &last, &part_count,
                                             string_format(result_arena, S8("c{u32}:{u32}["), (u32)constant.kind, constant.aggregate.element_count));
                analysis_canonical_task_push(scratch_arena, &top,
                                             (AnalysisCanonicalTask){
                                                 .text = S8("]"),
                                                 .kind = ANALYSIS_CANONICAL_TASK_TEXT,
                                             });
                for (u32 index = constant.aggregate.element_count; index > 0; index -= 1)
                {
                    if (index < constant.aggregate.element_count)
                    {
                        analysis_canonical_task_push(scratch_arena, &top,
                                                     (AnalysisCanonicalTask){
                                                         .text = S8(","),
                                                         .kind = ANALYSIS_CANONICAL_TASK_TEXT,
                                                     });
                    }
                    analysis_canonical_task_push(scratch_arena, &top,
                                                 (AnalysisCanonicalTask){
                                                     .constant = constant.aggregate.elements[index - 1],
                                                     .kind = ANALYSIS_CANONICAL_TASK_CONSTANT,
                                                 });
                }
                continue;
            }
            u64 bits = constant.integer;
            if (constant.kind == ANALYSIS_CONSTANT_FLOAT)
            {
                BUSTER_CT_CHECK(sizeof(bits) == sizeof(constant.floating));
                memcpy(&bits, &constant.floating, sizeof(bits));
            }
            analysis_canonical_part_push(
                scratch_arena, &first, &last, &part_count,
                string_format(result_arena, S8("c{u32}:{u32}:{u64:x,no_prefix}"), (u32)constant.kind, (u32)constant.is_negative, bits));
            continue;
        }

        AnalysisType* type = analysis_type_from_id(result, task.type);
        switch (type->kind)
        {
        case ANALYSIS_TYPE_POINTER:
        case ANALYSIS_TYPE_SLICE:
        case ANALYSIS_TYPE_INFERRED_ARRAY:
        case ANALYSIS_TYPE_RANGE:
        {
            analysis_canonical_part_push(scratch_arena, &first, &last, &part_count, string_format(result_arena, S8("t{u32}("), (u32)type->kind));
            analysis_canonical_task_push(scratch_arena, &top,
                                         (AnalysisCanonicalTask){
                                             .text = S8(")"),
                                             .kind = ANALYSIS_CANONICAL_TASK_TEXT,
                                         });
            analysis_canonical_task_push(scratch_arena, &top,
                                         (AnalysisCanonicalTask){
                                             .type = type->as.element_type,
                                             .kind = ANALYSIS_CANONICAL_TASK_TYPE,
                                         });
        }
        break;
        case ANALYSIS_TYPE_ARRAY:
        {
            analysis_canonical_part_push(scratch_arena, &first, &last, &part_count,
                                         string_format(result_arena, S8("t{u32}:{u64}("), (u32)type->kind, type->as.array.count));
            analysis_canonical_task_push(scratch_arena, &top,
                                         (AnalysisCanonicalTask){
                                             .text = S8(")"),
                                             .kind = ANALYSIS_CANONICAL_TASK_TEXT,
                                         });
            analysis_canonical_task_push(scratch_arena, &top,
                                         (AnalysisCanonicalTask){
                                             .type = type->as.array.element_type,
                                             .kind = ANALYSIS_CANONICAL_TASK_TYPE,
                                         });
        }
        break;
        case ANALYSIS_TYPE_VECTOR:
        {
            analysis_canonical_part_push(scratch_arena, &first, &last, &part_count,
                                         string_format(result_arena, S8("t{u32}:{u64}("), (u32)type->kind, type->as.vector.count));
            analysis_canonical_task_push(scratch_arena, &top,
                                         (AnalysisCanonicalTask){
                                             .text = S8(")"),
                                             .kind = ANALYSIS_CANONICAL_TASK_TEXT,
                                         });
            analysis_canonical_task_push(scratch_arena, &top,
                                         (AnalysisCanonicalTask){
                                             .type = type->as.vector.element_type,
                                             .kind = ANALYSIS_CANONICAL_TASK_TYPE,
                                         });
        }
        break;
        case ANALYSIS_TYPE_FUNCTION:
        {
            analysis_canonical_part_push(scratch_arena, &first, &last, &part_count,
                                         string_format(result_arena, S8("fn{u32}("), (u32)type->as.function.calling_convention));
            if (type->as.function.is_variadic)
            {
                analysis_canonical_part_push(scratch_arena, &first, &last, &part_count, S8("..."));
            }
            analysis_canonical_task_push(scratch_arena, &top,
                                         (AnalysisCanonicalTask){
                                             .type = type->as.function.return_type,
                                             .kind = ANALYSIS_CANONICAL_TASK_TYPE,
                                         });
            analysis_canonical_task_push(scratch_arena, &top,
                                         (AnalysisCanonicalTask){
                                             .text = S8(")->"),
                                             .kind = ANALYSIS_CANONICAL_TASK_TEXT,
                                         });
            for (u32 index = type->as.function.argument_count; index > 0; index -= 1)
            {
                if (index < type->as.function.argument_count)
                {
                    analysis_canonical_task_push(scratch_arena, &top,
                                                 (AnalysisCanonicalTask){
                                                     .text = S8(","),
                                                     .kind = ANALYSIS_CANONICAL_TASK_TEXT,
                                                 });
                }
                analysis_canonical_task_push(scratch_arena, &top,
                                             (AnalysisCanonicalTask){
                                                 .type = type->as.function.argument_types[index - 1],
                                                 .kind = ANALYSIS_CANONICAL_TASK_TYPE,
                                             });
            }
        }
        break;
        case ANALYSIS_TYPE_STRUCT:
        case ANALYSIS_TYPE_UNION:
        case ANALYSIS_TYPE_ENUM:
        {
            AnalysisResult* declaration_module = analysis_canonical_module_from_id(result, type->as.declaration.module);
            AnalysisEntity* declaration = declaration_module && type->as.declaration.index.value < declaration_module->module.entity_count
                                              ? declaration_module->module.entities + type->as.declaration.index.value
                                              : 0;
            analysis_canonical_part_push(scratch_arena, &first, &last, &part_count,
                                         string_format(result_arena, S8("nominal{u32}:{S8}:{S8}"), (u32)type->kind,
                                                       declaration_module ? declaration_module->module.name : S8(""),
                                                       declaration ? declaration->name : type->name));
        }
        break;
        case ANALYSIS_TYPE_POISON:
        case ANALYSIS_TYPE_VOID:
        case ANALYSIS_TYPE_BOOL:
        case ANALYSIS_TYPE_INTEGER:
        case ANALYSIS_TYPE_FLOAT:
        case ANALYSIS_TYPE_VA_LIST:
        case ANALYSIS_TYPE_COMPILE_TIME_PARAMETER:
        {
            analysis_canonical_part_push(scratch_arena, &first, &last, &part_count,
                                         string_format(result_arena, S8("scalar{u32}:{S8}"), (u32)type->kind, type->name.pointer ? type->name : S8("")));
        }
        break;
        case ANALYSIS_TYPE_COUNT:
            BUSTER_UNREACHABLE();
        }
    }
    String8* parts = arena_allocate(scratch_arena, String8, part_count);
    u32 index = 0;
    for (AnalysisCanonicalPart* part = first; part; part = part->next)
    {
        parts[index++] = part->text;
    }
    BUSTER_CHECK(index == part_count);
    return string_join_arena(result_arena, (SliceString8){.pointer = parts, .length = part_count}, false);
}

BUSTER_GLOBAL_LOCAL String8 analysis_type_canonical(Arena* result_arena, Arena* scratch_arena, AnalysisResult* result, AnalysisTypeId type)
{
    AnalysisCanonicalTask* top = 0;
    analysis_canonical_task_push(scratch_arena, &top,
                                 (AnalysisCanonicalTask){
                                     .type = type,
                                     .kind = ANALYSIS_CANONICAL_TASK_TYPE,
                                 });
    return analysis_canonical_tasks_join(result_arena, scratch_arena, result, top);
}

BUSTER_GLOBAL_LOCAL String8 analysis_constant_canonical(Arena* result_arena, Arena* scratch_arena, AnalysisResult* result, AnalysisConstant constant)
{
    AnalysisCanonicalTask* top = 0;
    analysis_canonical_task_push(scratch_arena, &top,
                                 (AnalysisCanonicalTask){
                                     .constant = constant,
                                     .kind = ANALYSIS_CANONICAL_TASK_CONSTANT,
                                 });
    return analysis_canonical_tasks_join(result_arena, scratch_arena, result, top);
}

BUSTER_GLOBAL_LOCAL u64 analysis_bytes_hash(String8 bytes)
{
    u64 hash = UINT64_C(1469598103934665603);
    for (u64 index = 0; index < bytes.length; index += 1)
    {
        hash ^= (u8)bytes.pointer[index];
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

String8 analysis_serialize_module_interface(Arena* arena, AnalysisResult* result)
{
    Arena* conflicts[] = {arena};
    TemporalArena scratch = scratch_begin(conflicts, BUSTER_ARRAY_LENGTH(conflicts));
    u32 function_argument_count = 0;
    u32 specialization_request_count = 0;
    for (AnalysisInstantiation* instantiation = result->first_instantiation; instantiation; instantiation = instantiation->next)
    {
        specialization_request_count += instantiation->requester_count;
    }
    for (u32 type_index = 0; type_index < result->types.count; type_index += 1)
    {
        AnalysisType* type = result->types.types + type_index;
        if (type->kind == ANALYSIS_TYPE_FUNCTION)
        {
            function_argument_count += type->as.function.argument_count;
        }
    }
    u32 part_capacity = 3 + result->module.source_count + result->module.import_count + result->module.entity_count + result->types.count +
                        function_argument_count + result->instantiation_count + specialization_request_count;
    String8* parts = arena_allocate(arena, String8, part_capacity);
    u32 part_count = 0;
    TargetDataLayout data_layout = target_data_layout_is_valid(result->data_layout) ? result->data_layout : target_data_layout(target_native);
    parts[part_count++] = string_format(arena, S8("buster-interface version={u32}\n"), ANALYSIS_INTERFACE_SCHEMA_VERSION);
    parts[part_count++] = string_format(arena,
                                        S8("layout endian={u32} char_signed={u32} pointer={u32}:{u32} long={u32}:{u32} long_double={u32}:{u32} va_list={u32}:{u32} "
                                           "atomic={u32}:{u32}:{u32} abi={u32}:{u32}\n"),
                                        (u32)data_layout.endianness, (u32)data_layout.plain_char_is_signed, data_layout.pointer.size,
                                        data_layout.pointer.alignment, data_layout.long_integer.size, data_layout.long_integer.alignment,
                                        data_layout.long_double_type.size, data_layout.long_double_type.alignment, data_layout.va_list.size,
                                        data_layout.va_list.alignment, data_layout.atomic_min_width, data_layout.atomic_max_width,
                                        data_layout.atomic_alignment, data_layout.abi_stack_alignment, data_layout.abi_max_alignment);
    parts[part_count++] = string_format(arena, S8("module {S8}\n"), result->module.name);
    for (u32 source_index = 0; source_index < result->module.source_count; source_index += 1)
    {
        AnalysisSource* source = result->module.sources + source_index;
        parts[part_count++] = string_format(arena, S8("source {u32} {S8}\n"), source->id.value, source->path);
    }
    for (u32 import_index = 0; import_index < result->module.import_count; import_index += 1)
    {
        AnalysisImport* import = result->module.imports + import_index;
        parts[part_count++] = string_format(arena, S8("import {S8}={S8} source={u32} state={u32} target={S8}\n"), import->name_space, import->path,
                                            import->source.value, (u32)import->state, import->target ? import->target->module.name : S8(""));
    }
    for (u32 entity_index = 0; entity_index < result->module.entity_count; entity_index += 1)
    {
        AnalysisEntity* entity = result->module.entities + entity_index;
        AnalysisEntitySemantic* semantic = result->module.semantics + entity_index;
        AnalysisConstant constant = semantic->constant ? *semantic->constant : (AnalysisConstant){0};
        parts[part_count++] = string_format(
            arena,
            S8("entity {u32} source={u32} kind={u32} namespace={u32} name={S8} type={u32} constant_kind={u32} constant_value={u64} constant_negative={u32}\n"),
            entity->id.index.value, entity->source.value, (u32)entity->kind, (u32)entity->name_space, entity->name, semantic->type.value, (u32)constant.kind,
            constant.kind == ANALYSIS_CONSTANT_INTEGER || constant.kind == ANALYSIS_CONSTANT_BOOLEAN || constant.kind == ANALYSIS_CONSTANT_ENUM
                ? constant.integer
                : 0,
            (u32)constant.is_negative);
    }
    for (u32 type_index = 0; type_index < result->types.count; type_index += 1)
    {
        AnalysisType* type = result->types.types + type_index;
        AnalysisTypeId element = ANALYSIS_TYPE_ID_INVALID;
        u64 array_count = 0;
        AnalysisTypeId return_type = ANALYSIS_TYPE_ID_INVALID;
        AnalysisEntityId declaration = ANALYSIS_ENTITY_ID_INVALID;
        String8 declaration_module_name = {0};
        String8 declaration_entity_name = {0};
        u32 argument_count = 0;
        u32 calling_convention = 0;
        u32 is_variadic = 0;
        if (type->kind == ANALYSIS_TYPE_POINTER || type->kind == ANALYSIS_TYPE_SLICE || type->kind == ANALYSIS_TYPE_INFERRED_ARRAY ||
            type->kind == ANALYSIS_TYPE_RANGE)
        {
            element = type->as.element_type;
        }
        else if (type->kind == ANALYSIS_TYPE_ARRAY)
        {
            element = type->as.array.element_type;
            array_count = type->as.array.count;
        }
        else if (type->kind == ANALYSIS_TYPE_VECTOR)
        {
            element = type->as.vector.element_type;
            array_count = type->as.vector.count;
        }
        else if (type->kind == ANALYSIS_TYPE_FUNCTION)
        {
            return_type = type->as.function.return_type;
            argument_count = type->as.function.argument_count;
            calling_convention = (u32)type->as.function.calling_convention;
            is_variadic = (u32)type->as.function.is_variadic;
        }
        else if (type->kind == ANALYSIS_TYPE_STRUCT || type->kind == ANALYSIS_TYPE_UNION || type->kind == ANALYSIS_TYPE_ENUM)
        {
            declaration = type->as.declaration;
            AnalysisResult* declaration_module = analysis_canonical_module_from_id(result, declaration.module);
            if (declaration_module)
            {
                declaration_module_name = declaration_module->module.name;
                if (declaration.index.value < declaration_module->module.entity_count)
                {
                    declaration_entity_name = declaration_module->module.entities[declaration.index.value].name;
                }
            }
        }
        parts[part_count++] = string_format(
            arena, S8("type {u32} kind={u32} name={S8} element={u32} count={u64} return={u32} arguments={u32} variadic={u32} cc={u32} declaration={S8}:{S8}\n"),
            type->id.value, (u32)type->kind, type->name.pointer ? type->name : S8(""), element.value, array_count, return_type.value, argument_count,
            is_variadic, calling_convention, declaration_module_name.pointer ? declaration_module_name : S8(""),
            declaration_entity_name.pointer ? declaration_entity_name : S8(""));
        for (u32 argument_index = 0; argument_index < argument_count; argument_index += 1)
        {
            parts[part_count++] = string_format(arena, S8("type_argument type={u32} index={u32} value={u32}\n"), type->id.value, argument_index,
                                                type->as.function.argument_types[argument_index].value);
        }
    }
    AnalysisInstantiation** specializations = arena_allocate(scratch.arena, AnalysisInstantiation*, result->instantiation_count);
    u32 specialization_index = 0;
    for (AnalysisInstantiation* instantiation = result->first_instantiation; instantiation; instantiation = instantiation->next)
    {
        specializations[specialization_index++] = instantiation;
    }
    BUSTER_CHECK(specialization_index == result->instantiation_count);
    for (u32 index = 1; index < result->instantiation_count; index += 1)
    {
        AnalysisInstantiation* instantiation = specializations[index];
        u32 insertion = index;
        while (insertion && analysis_string_compare(specializations[insertion - 1]->canonical_key, instantiation->canonical_key) > 0)
        {
            specializations[insertion] = specializations[insertion - 1];
            insertion -= 1;
        }
        specializations[insertion] = instantiation;
    }
    for (u32 index = 0; index < result->instantiation_count; index += 1)
    {
        AnalysisInstantiation* instantiation = specializations[index];
        AnalysisResult* owner = analysis_canonical_module_from_id(result, instantiation->codegen_owner);
        parts[part_count++] = string_format(arena, S8("specialization hash={u64:x,no_prefix} symbol={S8} owner={S8} type={S8} key={S8}\n"),
                                            instantiation->canonical_hash, instantiation->symbol_name, owner ? owner->module.name : result->module.name,
                                            analysis_type_canonical(arena, scratch.arena, result, instantiation->function_type), instantiation->canonical_key);
        String8* requester_names = arena_allocate(scratch.arena, String8, instantiation->requester_count);
        u32 requester_index = 0;
        for (AnalysisInstantiationRequester* requester = instantiation->first_requester; requester; requester = requester->next)
        {
            AnalysisResult* requester_module = analysis_canonical_module_from_id(result, requester->module);
            requester_names[requester_index++] = requester_module ? requester_module->module.name : S8("");
        }
        BUSTER_CHECK(requester_index == instantiation->requester_count);
        for (u32 requester = 1; requester < instantiation->requester_count; requester += 1)
        {
            String8 name = requester_names[requester];
            u32 insertion = requester;
            while (insertion && analysis_string_compare(requester_names[insertion - 1], name) > 0)
            {
                requester_names[insertion] = requester_names[insertion - 1];
                insertion -= 1;
            }
            requester_names[insertion] = name;
        }
        for (u32 requester = 0; requester < instantiation->requester_count; requester += 1)
        {
            parts[part_count++] = string_format(arena, S8("specialization_request hash={u64:x,no_prefix} requester={S8}\n"), instantiation->canonical_hash,
                                                requester_names[requester]);
        }
    }
    BUSTER_CHECK(part_count == part_capacity);
    String8 serialized = string_join_arena(arena, (SliceString8){.pointer = parts, .length = part_count}, false);
    scratch_end(scratch);
    return serialized;
}

AnalysisInterfaceSummary analysis_module_interface_summary(Arena* arena, AnalysisResult* result)
{
    AnalysisInterfaceSummary summary = {
        .bytes = analysis_serialize_module_interface(arena, result),
        .hash = 0,
        .schema_version = ANALYSIS_INTERFACE_SCHEMA_VERSION,
    };
    summary.hash = analysis_bytes_hash(summary.bytes);
    return summary;
}

bool analysis_interface_summary_is_valid(AnalysisInterfaceSummary summary)
{
    return summary.schema_version == ANALYSIS_INTERFACE_SCHEMA_VERSION && summary.bytes.length && summary.bytes.pointer && summary.hash == analysis_bytes_hash(summary.bytes) &&
           string_starts_with_sequence(summary.bytes, S8("buster-interface version=1\n"));
}

AnalysisInterfaceCacheEntry* analysis_interface_cache_find(AnalysisInterfaceCache* cache, String8 module_name)
{
    for (AnalysisInterfaceCacheEntry* entry = cache->first; entry; entry = entry->next)
    {
        if (string_equal(entry->module_name, module_name))
        {
            return entry;
        }
    }
    return 0;
}

bool analysis_interface_cache_store(Arena* arena, AnalysisInterfaceCache* cache, String8 module_name, AnalysisInterfaceSummary summary)
{
    AnalysisInterfaceCacheEntry* entry = analysis_interface_cache_find(cache, module_name);
    if (!analysis_interface_summary_is_valid(summary))
    {
        return false;
    }
    bool changed = !entry || entry->summary.schema_version != summary.schema_version || entry->summary.hash != summary.hash ||
                   !string_equal(entry->summary.bytes, summary.bytes);
    if (!entry)
    {
        entry = arena_allocate(arena, AnalysisInterfaceCacheEntry, 1);
        *entry = (AnalysisInterfaceCacheEntry){
            .module_name = string_duplicate_arena(arena, module_name, false),
        };
        if (cache->last)
        {
            cache->last->next = entry;
        }
        else
        {
            cache->first = entry;
        }
        cache->last = entry;
        cache->count += 1;
    }
    if (changed)
    {
        entry->summary = (AnalysisInterfaceSummary){
            .bytes = string_duplicate_arena(arena, summary.bytes, false),
            .hash = summary.hash,
            .schema_version = summary.schema_version,
        };
    }
    return changed;
}

BUSTER_GLOBAL_LOCAL String8 analysis_root_module_name(Arena* arena, AnalysisProgramOptions options)
{
    if (options.root_module_name.length)
    {
        return string_duplicate_arena(arena, options.root_module_name, false);
    }

    u64 start = 0;
    for (u64 index = 0; index < options.root_path.length; index += 1)
    {
        if (options.root_path.pointer[index] == '/' || options.root_path.pointer[index] == '\\')
        {
            start = index + 1;
        }
    }
    u64 end = options.root_path.length;
    if (end >= start + 4 && string_equal(string_slice(options.root_path, end - 4, end), S8(".bbb")))
    {
        end -= 4;
    }
    return string_duplicate_arena(arena, string_slice(options.root_path, start, end), false);
}

AnalysisProgram analysis_program_load(Arena* result_arena, Arena* expression_arena, AnalysisProgramOptions options)
{
    BUSTER_CHECK(result_arena);
    BUSTER_CHECK(expression_arena);
    BUSTER_CHECK(result_arena != expression_arena);

    AnalysisProgram program = {0};
    String8 root_name = analysis_root_module_name(result_arena, options);
    if (!root_name.length || !options.root_path.length)
    {
        program.load_failed = true;
        return program;
    }

    AnalysisProgramDiscovery* first = arena_allocate(result_arena, AnalysisProgramDiscovery, 1);
    *first = (AnalysisProgramDiscovery){
        .name = root_name,
        .path = string_duplicate_arena(result_arena, options.root_path, true),
    };
    AnalysisProgramDiscovery* last = first;
    u32 discovery_count = 1;

    for (AnalysisProgramDiscovery* discovery = first; discovery; discovery = discovery->next)
    {
        FileMapRead source_map = file_map_read(result_arena, discovery->path, (FileReadOptions){0});
        ByteSlice bytes = source_map.bytes;
        if (!bytes.pointer)
        {
            file_map_unmap(source_map);
            program.load_failed = true;
            continue;
        }
        discovery->source = BYTE_SLICE_TO_STRING(8, bytes);
        discovery->source_map = source_map;
        TokenizerResult tokenizer = tokenize(result_arena, discovery->source.pointer, discovery->source.length);
        discovery->parser = parser_parse(result_arena, expression_arena, discovery->source, tokenizer);
        program.parser_diagnostic_count += tokenizer.error_count + discovery->parser.diagnostic_count;

        for (AstImport* import = discovery->parser.first_import; import; import = import->next)
        {
            if (analysis_program_discovery_find(first, import->path))
            {
                continue;
            }
            AnalysisProgramDiscovery* appended = arena_allocate(result_arena, AnalysisProgramDiscovery, 1);
            *appended = (AnalysisProgramDiscovery){
                .name = string_duplicate_arena(result_arena, import->path, false),
                .path = analysis_module_path(result_arena, options.module_root, import->path),
            };
            last->next = appended;
            last = appended;
            discovery_count += 1;
        }
    }

    program.module_count = discovery_count;
    program.modules = arena_allocate(result_arena, AnalysisProgramModule, discovery_count);
    u32 module_index = 0;
    for (AnalysisProgramDiscovery* discovery = first; discovery; discovery = discovery->next)
    {
        program.modules[module_index] = (AnalysisProgramModule){
            .name = discovery->name,
            .path = discovery->path,
            .source = discovery->source,
            .source_map = discovery->source_map,
            .parser = discovery->parser,
        };
        module_index += 1;
    }
    BUSTER_CHECK(module_index == discovery_count);

    for (u32 index = 1; index < discovery_count; index += 1)
    {
        AnalysisProgramModule value = program.modules[index];
        u32 insertion = index;
        while (insertion && analysis_program_module_after(program.modules[insertion - 1], value))
        {
            program.modules[insertion] = program.modules[insertion - 1];
            insertion -= 1;
        }
        program.modules[insertion] = value;
    }

    program.module_results = arena_allocate(result_arena, AnalysisResult*, discovery_count);
    for (u32 index = 0; index < discovery_count; index += 1)
    {
        AnalysisProgramModule* module = program.modules + index;
        if (!module->source.pointer)
        {
            program.module_results[index] = 0;
            continue;
        }
        AnalysisSourceInput input = {
            .path = module->path,
            .parser = &module->parser,
        };
        module->analysis = arena_allocate(result_arena, AnalysisResult, 1);
        *module->analysis = analysis_index_module(result_arena, (AnalysisModuleId){.value = index}, module->name, &input, 1);
        program.module_results[index] = module->analysis;
        if (string_equal(module->name, root_name))
        {
            program.root = module;
        }
    }
    BUSTER_CHECK(program.root);

    analysis_resolve_program_interfaces(result_arena, program.module_results, discovery_count);
    TargetDataLayout data_layout = options.data_layout;
    if (!target_data_layout_is_valid(data_layout))
    {
        data_layout = target_data_layout(target_native);
        if (options.pointer_size)
        {
            data_layout.pointer.size = options.pointer_size;
            data_layout.pointer.bit_width = options.pointer_size * 8;
        }
        if (options.pointer_alignment)
        {
            data_layout.pointer.alignment = options.pointer_alignment;
        }
    }
    AnalysisLayoutOptions layout = {
        .data_layout = data_layout,
        .pointer_size = data_layout.pointer.size,
        .pointer_alignment = data_layout.pointer.alignment,
    };
    for (u32 index = 0; index < discovery_count; index += 1)
    {
        AnalysisResult* analysis = program.module_results[index];
        if (!analysis)
        {
            continue;
        }
        analysis_analyze_bodies(result_arena, analysis);
    }
    bool pending = true;
    while (pending)
    {
        pending = false;
        for (u32 index = 0; index < discovery_count; index += 1)
        {
            AnalysisResult* analysis = program.module_results[index];
            if (!analysis)
            {
                continue;
            }
            for (AnalysisInstantiation* instantiation = analysis->first_instantiation; instantiation; instantiation = instantiation->next)
            {
                pending |= analysis->module.entities[instantiation->generic_entity.index.value].ast.code->has_body && !instantiation->analyzed;
            }
            if (pending)
            {
                analysis_analyze_bodies(result_arena, analysis);
            }
        }
    }
    for (u32 index = 0; index < discovery_count; index += 1)
    {
        AnalysisResult* analysis = program.module_results[index];
        if (!analysis)
        {
            continue;
        }
        analysis_compute_layouts(analysis, layout);
        analysis_build_jobs(result_arena, analysis);
        program.analysis_diagnostic_count += analysis->diagnostic_count;
    }
    return program;
}

void analysis_program_unmap_sources(AnalysisProgram* program)
{
    if (!program)
    {
        return;
    }
    for (u32 index = 0; index < program->module_count; index += 1)
    {
        AnalysisProgramModule* module = program->modules + index;
        file_map_unmap(module->source_map);
    }
}

typedef struct AnalysisAstTypeLink AnalysisAstTypeLink;
struct AnalysisAstTypeLink
{
    AnalysisAstTypeLink* previous;
    AstType* type;
};

typedef struct AnalysisAstStatementLink AnalysisAstStatementLink;
struct AnalysisAstStatementLink
{
    AnalysisAstStatementLink* previous;
    AstStatement* statement;
};

BUSTER_GLOBAL_LOCAL void analysis_ast_statement_link_push(Arena* arena, AnalysisAstStatementLink** top, AstStatement* statement)
{
    for (; statement; statement = statement->next)
    {
        AnalysisAstStatementLink* link = arena_allocate(arena, AnalysisAstStatementLink, 1);
        *link = (AnalysisAstStatementLink){.previous = *top, .statement = statement};
        *top = link;
    }
}

BUSTER_GLOBAL_LOCAL void analysis_ast_type_link_push(Arena* arena, AnalysisAstTypeLink** top, AstType* type)
{
    if (type)
    {
        AnalysisAstTypeLink* link = arena_allocate(arena, AnalysisAstTypeLink, 1);
        *link = (AnalysisAstTypeLink){.previous = *top, .type = type};
        *top = link;
    }
}

BUSTER_GLOBAL_LOCAL u32 analysis_ast_type_count(Arena* scratch_arena, AnalysisResult* result)
{
    TemporalArena temporary = arena_begin_temporal(scratch_arena);
    AnalysisAstTypeLink* top = 0;
    AnalysisAstStatementLink* statement_top = 0;
    for (u32 index = 0; index < result->module.entity_count; index += 1)
    {
        AnalysisEntity* entity = result->module.entities + index;
        if (entity->kind == ANALYSIS_ENTITY_CODE)
        {
            analysis_ast_type_link_push(scratch_arena, &top, entity->ast.code->type);
            analysis_ast_statement_link_push(scratch_arena, &statement_top, entity->ast.code->body.first_statement);
            continue;
        }
        if (entity->kind == ANALYSIS_ENTITY_DATA)
        {
            analysis_ast_type_link_push(scratch_arena, &top, entity->ast.data->type);
            continue;
        }

        AstTypeDeclaration* declaration = entity->ast.type_declaration;
        if (declaration->kind == AST_TYPE_DECLARATION_ALIAS)
        {
            analysis_ast_type_link_push(scratch_arena, &top, declaration->alias_type);
        }
        else
        {
            for (AstTypeField* field = declaration->first_field; field; field = field->next)
            {
                analysis_ast_type_link_push(scratch_arena, &top, field->type);
            }
        }
    }

    while (statement_top)
    {
        AstStatement* statement = statement_top->statement;
        statement_top = statement_top->previous;
        switch (statement->id)
        {
        case AST_STATEMENT_DATA:
        {
            analysis_ast_type_link_push(scratch_arena, &top, statement->data_statement.type);
        }
        break;
        case AST_STATEMENT_IF:
        {
            analysis_ast_statement_link_push(scratch_arena, &statement_top, statement->if_statement.then_block.first_statement);
            if (statement->if_statement.alternative == AST_IF_ALTERNATIVE_BLOCK)
            {
                analysis_ast_statement_link_push(scratch_arena, &statement_top, statement->if_statement.else_block.first_statement);
            }
            else if (statement->if_statement.alternative == AST_IF_ALTERNATIVE_IF)
            {
                analysis_ast_statement_link_push(scratch_arena, &statement_top, statement->if_statement.else_if);
            }
        }
        break;
        case AST_STATEMENT_SWITCH:
        {
            for (AstSwitchCase* switch_case = statement->switch_statement.first_case; switch_case; switch_case = switch_case->next)
            {
                analysis_ast_statement_link_push(scratch_arena, &statement_top, switch_case->body.first_statement);
            }
        }
        break;
        case AST_STATEMENT_FOR:
        {
            analysis_ast_type_link_push(scratch_arena, &top, statement->for_statement.type);
            analysis_ast_statement_link_push(scratch_arena, &statement_top, statement->for_statement.body.first_statement);
        }
        break;
        case AST_STATEMENT_LOOP:
        {
            analysis_ast_statement_link_push(scratch_arena, &statement_top, statement->loop_statement.body.first_statement);
        }
        break;
        case AST_STATEMENT_RETURN:
        case AST_STATEMENT_EXPRESSION:
        case AST_STATEMENT_ASSIGNMENT:
        case AST_STATEMENT_BREAK:
        case AST_STATEMENT_CONTINUE:
        case AST_STATEMENT_COUNT:
            break;
        }
    }

    u32 count = 0;
    while (top)
    {
        AstType* type = top->type;
        top = top->previous;
        count += 1;
        switch (type->id)
        {
        case AST_TYPE_NAMED:
        case AST_TYPE_QUALIFIED_NAMED:
            break;
        case AST_TYPE_POINTER:
        case AST_TYPE_SLICE:
        case AST_TYPE_INFERRED_ARRAY:
        {
            analysis_ast_type_link_push(scratch_arena, &top, type->element_type);
        }
        break;
        case AST_TYPE_ARRAY:
        {
            analysis_ast_type_link_push(scratch_arena, &top, type->array.element_type);
        }
        break;
        case AST_TYPE_VECTOR:
        {
            analysis_ast_type_link_push(scratch_arena, &top, type->vector.element_type);
        }
        break;
        case AST_TYPE_FUNCTION:
        {
            analysis_ast_type_link_push(scratch_arena, &top, type->function.return_type);
            for (AstTypeArgument* argument = type->function.first_argument; argument; argument = argument->next)
            {
                analysis_ast_type_link_push(scratch_arena, &top, argument->type);
            }
        }
        break;
        case AST_TYPE_COUNT:
            break;
        }
    }
    arena_set_position(temporary.arena, temporary.position);
    return count;
}

BUSTER_GLOBAL_LOCAL void analysis_body_capacity_measure(Arena* scratch_arena, AstCode* code, u32* local_count, u32* expression_node_count)
{
    AnalysisAstStatementLink* top = 0;
    analysis_ast_statement_link_push(scratch_arena, &top, code->body.first_statement);
    for (AstTypeArgument* argument = code->type->function.first_argument; argument; argument = argument->next)
    {
        *local_count += 1;
    }
    while (top)
    {
        AstStatement* statement = top->statement;
        top = top->previous;
        switch (statement->id)
        {
        case AST_STATEMENT_RETURN:
        {
            *expression_node_count += statement->return_statement.expression.count;
        }
        break;
        case AST_STATEMENT_DATA:
        {
            *local_count += 1;
            *expression_node_count += statement->data_statement.initializer.count;
        }
        break;
        case AST_STATEMENT_EXPRESSION:
        {
            *expression_node_count += statement->expression_statement.expression.count;
        }
        break;
        case AST_STATEMENT_ASSIGNMENT:
        {
            *expression_node_count += statement->assignment_statement.target.count;
            *expression_node_count += statement->assignment_statement.value.count;
        }
        break;
        case AST_STATEMENT_IF:
        {
            *expression_node_count += statement->if_statement.condition.count;
            analysis_ast_statement_link_push(scratch_arena, &top, statement->if_statement.then_block.first_statement);
            if (statement->if_statement.alternative == AST_IF_ALTERNATIVE_BLOCK)
            {
                analysis_ast_statement_link_push(scratch_arena, &top, statement->if_statement.else_block.first_statement);
            }
            else if (statement->if_statement.alternative == AST_IF_ALTERNATIVE_IF)
            {
                analysis_ast_statement_link_push(scratch_arena, &top, statement->if_statement.else_if);
            }
        }
        break;
        case AST_STATEMENT_SWITCH:
        {
            *expression_node_count += statement->switch_statement.expression.count;
            for (AstSwitchCase* switch_case = statement->switch_statement.first_case; switch_case; switch_case = switch_case->next)
            {
                if (!switch_case->is_else)
                {
                    *expression_node_count += switch_case->expression.count;
                }
                analysis_ast_statement_link_push(scratch_arena, &top, switch_case->body.first_statement);
            }
        }
        break;
        case AST_STATEMENT_FOR:
        {
            *local_count += 1;
            *expression_node_count += statement->for_statement.iterable.count;
            analysis_ast_statement_link_push(scratch_arena, &top, statement->for_statement.body.first_statement);
        }
        break;
        case AST_STATEMENT_LOOP:
        {
            if (statement->loop_statement.has_condition)
            {
                *expression_node_count += statement->loop_statement.condition.count;
            }
            analysis_ast_statement_link_push(scratch_arena, &top, statement->loop_statement.body.first_statement);
        }
        break;
        case AST_STATEMENT_BREAK:
        case AST_STATEMENT_CONTINUE:
        case AST_STATEMENT_COUNT:
            break;
        }
    }
}

BUSTER_GLOBAL_LOCAL AnalysisTypeId analysis_type_add(AnalysisTypeTable* table, AnalysisType type)
{
    BUSTER_CHECK(table->count < table->capacity);
    type.id = (AnalysisTypeId){.value = table->count};
    table->types[table->count] = type;
    table->count += 1;
    return type.id;
}

BUSTER_GLOBAL_LOCAL AnalysisTypeId analysis_builtin_add(AnalysisTypeTable* table, String8 name, AnalysisTypeKind kind, u32 bit_width, bool is_signed)
{
    AnalysisType type = {.name = name, .kind = kind};
    if (kind == ANALYSIS_TYPE_INTEGER)
    {
        type.as.integer.bit_width = bit_width;
        type.as.integer.is_signed = is_signed;
    }
    else if (kind == ANALYSIS_TYPE_FLOAT)
    {
        type.as.float_bit_width = bit_width;
    }
    return analysis_type_add(table, type);
}

BUSTER_GLOBAL_LOCAL void analysis_builtin_types_initialize(AnalysisTypeTable* table)
{
    table->builtin.poison = analysis_builtin_add(table, S8(""), ANALYSIS_TYPE_POISON, 0, false);
    table->builtin.void_type = analysis_builtin_add(table, S8("void"), ANALYSIS_TYPE_VOID, 0, false);
    table->builtin.bool_type = analysis_builtin_add(table, S8("bool"), ANALYSIS_TYPE_BOOL, 0, false);
    table->builtin.u8_type = analysis_builtin_add(table, S8("u8"), ANALYSIS_TYPE_INTEGER, 8, false);
    table->builtin.u16_type = analysis_builtin_add(table, S8("u16"), ANALYSIS_TYPE_INTEGER, 16, false);
    table->builtin.u32_type = analysis_builtin_add(table, S8("u32"), ANALYSIS_TYPE_INTEGER, 32, false);
    table->builtin.u64_type = analysis_builtin_add(table, S8("u64"), ANALYSIS_TYPE_INTEGER, 64, false);
    table->builtin.s8_type = analysis_builtin_add(table, S8("s8"), ANALYSIS_TYPE_INTEGER, 8, true);
    table->builtin.s16_type = analysis_builtin_add(table, S8("s16"), ANALYSIS_TYPE_INTEGER, 16, true);
    table->builtin.s32_type = analysis_builtin_add(table, S8("s32"), ANALYSIS_TYPE_INTEGER, 32, true);
    table->builtin.s64_type = analysis_builtin_add(table, S8("s64"), ANALYSIS_TYPE_INTEGER, 64, true);
    table->builtin.f32_type = analysis_builtin_add(table, S8("f32"), ANALYSIS_TYPE_FLOAT, 32, true);
    table->builtin.f64_type = analysis_builtin_add(table, S8("f64"), ANALYSIS_TYPE_FLOAT, 64, true);
    table->builtin.va_list_type = analysis_builtin_add(table, S8("va_list"), ANALYSIS_TYPE_VA_LIST, 0, false);
}

BUSTER_GLOBAL_LOCAL bool analysis_type_id_equal(AnalysisTypeId left, AnalysisTypeId right)
{
    return left.value == right.value;
}

AnalysisType* analysis_type_from_id(AnalysisResult* result, AnalysisTypeId id)
{
    BUSTER_CHECK(id.value < result->types.count);
    return result->types.types + id.value;
}

BUSTER_GLOBAL_LOCAL bool analysis_type_matches(AnalysisType* existing, AnalysisType* candidate)
{
    if (existing->kind != candidate->kind)
    {
        return false;
    }
    switch (candidate->kind)
    {
    case ANALYSIS_TYPE_POINTER:
    case ANALYSIS_TYPE_SLICE:
    case ANALYSIS_TYPE_INFERRED_ARRAY:
    case ANALYSIS_TYPE_RANGE:
    {
        return analysis_type_id_equal(existing->as.element_type, candidate->as.element_type);
    }
    case ANALYSIS_TYPE_ARRAY:
    {
        return analysis_type_id_equal(existing->as.array.element_type, candidate->as.array.element_type) &&
               existing->as.array.count == candidate->as.array.count;
    }
    case ANALYSIS_TYPE_VECTOR:
    {
        return analysis_type_id_equal(existing->as.vector.element_type, candidate->as.vector.element_type) &&
               existing->as.vector.count == candidate->as.vector.count;
    }
    case ANALYSIS_TYPE_FUNCTION:
    {
        if (existing->as.function.argument_count != candidate->as.function.argument_count ||
            existing->as.function.is_variadic != candidate->as.function.is_variadic ||
            existing->as.function.calling_convention != candidate->as.function.calling_convention ||
            !analysis_type_id_equal(existing->as.function.return_type, candidate->as.function.return_type))
        {
            return false;
        }
        for (u32 index = 0; index < candidate->as.function.argument_count; index += 1)
        {
            if (!analysis_type_id_equal(existing->as.function.argument_types[index], candidate->as.function.argument_types[index]))
            {
                return false;
            }
        }
        return true;
    }
    case ANALYSIS_TYPE_COMPILE_TIME_PARAMETER:
    {
        return string_equal(existing->name, candidate->name);
    }
    case ANALYSIS_TYPE_STRUCT:
    case ANALYSIS_TYPE_UNION:
    case ANALYSIS_TYPE_ENUM:
    {
        return existing->as.declaration.module.value == candidate->as.declaration.module.value &&
               existing->as.declaration.index.value == candidate->as.declaration.index.value;
    }
    case ANALYSIS_TYPE_POISON:
    case ANALYSIS_TYPE_VOID:
    case ANALYSIS_TYPE_BOOL:
    case ANALYSIS_TYPE_INTEGER:
    case ANALYSIS_TYPE_FLOAT:
    case ANALYSIS_TYPE_VA_LIST:
    case ANALYSIS_TYPE_COUNT:
        break;
    }
    return false;
}

BUSTER_GLOBAL_LOCAL AnalysisTypeId analysis_type_intern(Arena* result_arena, AnalysisTypeTable* table, AnalysisType candidate)
{
    for (u32 index = 0; index < table->count; index += 1)
    {
        if (analysis_type_matches(table->types + index, &candidate))
        {
            return (AnalysisTypeId){.value = index};
        }
    }
    if (candidate.kind == ANALYSIS_TYPE_FUNCTION && candidate.as.function.argument_count)
    {
        u32 count = candidate.as.function.argument_count;
        AnalysisTypeId* argument_types = arena_allocate(result_arena, AnalysisTypeId, count);
        for (u32 index = 0; index < count; index += 1)
        {
            argument_types[index] = candidate.as.function.argument_types[index];
        }
        candidate.as.function.argument_types = argument_types;
    }
    if (table->count == table->capacity)
    {
        u32 new_capacity = table->capacity ? table->capacity * 2 : 16;
        BUSTER_CHECK(new_capacity > table->capacity);
        AnalysisType* types = arena_allocate(result_arena, AnalysisType, new_capacity);
        for (u32 index = 0; index < table->count; index += 1)
        {
            types[index] = table->types[index];
        }
        table->types = types;
        table->capacity = new_capacity;
    }
    return analysis_type_add(table, candidate);
}

BUSTER_GLOBAL_LOCAL AnalysisTypeId analysis_builtin_type_find(AnalysisTypeTable* table, String8 name);

BUSTER_GLOBAL_LOCAL AnalysisResult* analysis_program_module_from_id(AnalysisResult* result, AnalysisModuleId id)
{
    if (result->module.id.value == id.value)
    {
        return result;
    }
    for (u32 index = 0; index < result->program_module_count; index += 1)
    {
        AnalysisResult* candidate = result->program_modules[index];
        if (candidate && candidate->module.id.value == id.value)
        {
            return candidate;
        }
    }
    return 0;
}

String8 analysis_source_path(AnalysisResult* result, AnalysisModuleId module, AnalysisSourceId source)
{
    AnalysisResult* owner = analysis_program_module_from_id(result, module);
    if (!owner || source.value >= owner->module.source_count)
    {
        return (String8){0};
    }
    return owner->module.sources[source.value].path;
}

String8 analysis_format_diagnostic(Arena* arena, AnalysisResult* result, AnalysisDiagnostic* diagnostic)
{
    u32 note_count = 0;
    for (AnalysisDiagnosticNote* note = diagnostic->first_note; note; note = note->next)
    {
        note_count += 1;
    }
    String8* parts = arena_allocate(arena, String8, 1 + note_count);
    AnalysisModuleId diagnostic_module = diagnostic->entity.module;
    if (diagnostic_module.value == ANALYSIS_ID_UNDERLYING_INVALID)
    {
        diagnostic_module = result->module.id;
    }
    String8 path = analysis_source_path(result, diagnostic_module, diagnostic->source);
    parts[0] = string_format(arena, diagnostic->subject.length ? S8("{S8}:{u32}:{u32}: error: {S8}: {S8}\n") : S8("{S8}:{u32}:{u32}: error: {S8}{S8}\n"), path,
                             diagnostic->range.line + 1, diagnostic->range.column + 1, diagnostic->message, diagnostic->subject);
    u32 part_index = 1;
    for (AnalysisDiagnosticNote* note = diagnostic->first_note; note; note = note->next)
    {
        AnalysisModuleId note_module = note->entity.module;
        if (note_module.value == ANALYSIS_ID_UNDERLYING_INVALID)
        {
            note_module = diagnostic_module;
        }
        parts[part_index] = string_format(arena, S8("{S8}:{u32}:{u32}: note: {S8}\n"), analysis_source_path(result, note_module, note->source),
                                          note->range.line + 1, note->range.column + 1, note->message);
        part_index += 1;
    }
    return string_join_arena(arena, (SliceString8){.pointer = parts, .length = part_index}, false);
}

BUSTER_GLOBAL_LOCAL AnalysisEntitySemantic* analysis_declaration_semantic(AnalysisResult* result, AnalysisEntityId declaration,
                                                                          AnalysisResult** declaration_module)
{
    AnalysisResult* module = analysis_program_module_from_id(result, declaration.module);
    if (!module || declaration.index.value >= module->module.entity_count)
    {
        return 0;
    }
    if (declaration_module)
    {
        *declaration_module = module;
    }
    return module->module.semantics + declaration.index.value;
}

typedef enum AnalysisImportTypeTaskKind
{
    ANALYSIS_IMPORT_TYPE_VISIT,
    ANALYSIS_IMPORT_TYPE_FINISH_ELEMENT,
    ANALYSIS_IMPORT_TYPE_FINISH_ARRAY,
    ANALYSIS_IMPORT_TYPE_FINISH_FUNCTION,
} AnalysisImportTypeTaskKind;

typedef struct AnalysisImportTypeTask AnalysisImportTypeTask;
struct AnalysisImportTypeTask
{
    AnalysisImportTypeTask* previous;
    AnalysisTypeId* destination;
    AnalysisImportTypeTaskKind kind;
    union
    {
        AnalysisTypeId source;
        struct
        {
            AnalysisTypeKind kind;
            AnalysisTypeId element;
        } element;
        struct
        {
            AnalysisTypeKind kind;
            AnalysisTypeId element;
            u64 count;
        } array;
        struct
        {
            AnalysisTypeId* arguments;
            AnalysisTypeId return_type;
            AstCallingConvention calling_convention;
            u32 argument_count;
            bool is_variadic;
            u8 reserved[3];
        } function;
    } as;
};

BUSTER_GLOBAL_LOCAL void analysis_import_type_task_push(Arena* scratch_arena, AnalysisImportTypeTask** top, AnalysisImportTypeTask task)
{
    AnalysisImportTypeTask* pushed = arena_allocate(scratch_arena, AnalysisImportTypeTask, 1);
    task.previous = *top;
    *pushed = task;
    *top = pushed;
}

BUSTER_GLOBAL_LOCAL AnalysisTypeId analysis_type_import(Arena* result_arena, Arena* scratch_arena, AnalysisResult* destination, AnalysisResult* source,
                                                        AnalysisTypeId source_id)
{
    if (source == destination)
    {
        return source_id;
    }

    AnalysisTypeId result = destination->types.builtin.poison;
    AnalysisImportTypeTask* top = 0;
    analysis_import_type_task_push(scratch_arena, &top,
                                   (AnalysisImportTypeTask){
                                       .destination = &result,
                                       .kind = ANALYSIS_IMPORT_TYPE_VISIT,
                                       .as.source = source_id,
                                   });
    while (top)
    {
        AnalysisImportTypeTask* task = top;
        top = task->previous;
        if (task->kind == ANALYSIS_IMPORT_TYPE_VISIT)
        {
            AnalysisType* type = analysis_type_from_id(source, task->as.source);
            switch (type->kind)
            {
            case ANALYSIS_TYPE_POISON:
            case ANALYSIS_TYPE_VOID:
            case ANALYSIS_TYPE_BOOL:
            case ANALYSIS_TYPE_INTEGER:
            case ANALYSIS_TYPE_FLOAT:
            case ANALYSIS_TYPE_VA_LIST:
            {
                AnalysisTypeId builtin = analysis_builtin_type_find(&destination->types, type->name);
                *task->destination = builtin.value == ANALYSIS_ID_UNDERLYING_INVALID ? destination->types.builtin.poison : builtin;
            }
            break;
            case ANALYSIS_TYPE_COMPILE_TIME_PARAMETER:
            {
                *task->destination = analysis_type_intern(result_arena, &destination->types,
                                                          (AnalysisType){
                                                              .name = type->name,
                                                              .kind = ANALYSIS_TYPE_COMPILE_TIME_PARAMETER,
                                                          });
            }
            break;
            case ANALYSIS_TYPE_STRUCT:
            case ANALYSIS_TYPE_UNION:
            case ANALYSIS_TYPE_ENUM:
            {
                *task->destination = analysis_type_intern(result_arena, &destination->types,
                                                          (AnalysisType){
                                                              .name = type->name,
                                                              .kind = type->kind,
                                                              .as.declaration = type->as.declaration,
                                                          });
            }
            break;
            case ANALYSIS_TYPE_POINTER:
            case ANALYSIS_TYPE_SLICE:
            case ANALYSIS_TYPE_INFERRED_ARRAY:
            case ANALYSIS_TYPE_RANGE:
            {
                AnalysisImportTypeTask* finish = arena_allocate(scratch_arena, AnalysisImportTypeTask, 1);
                *finish = (AnalysisImportTypeTask){
                    .previous = top,
                    .destination = task->destination,
                    .kind = ANALYSIS_IMPORT_TYPE_FINISH_ELEMENT,
                    .as.element =
                        {
                            .kind = type->kind,
                            .element = destination->types.builtin.poison,
                        },
                };
                top = finish;
                analysis_import_type_task_push(scratch_arena, &top,
                                               (AnalysisImportTypeTask){
                                                   .destination = &finish->as.element.element,
                                                   .kind = ANALYSIS_IMPORT_TYPE_VISIT,
                                                   .as.source = type->as.element_type,
                                               });
            }
            break;
            case ANALYSIS_TYPE_ARRAY:
            case ANALYSIS_TYPE_VECTOR:
            {
                AnalysisImportTypeTask* finish = arena_allocate(scratch_arena, AnalysisImportTypeTask, 1);
                *finish = (AnalysisImportTypeTask){
                    .previous = top,
                    .destination = task->destination,
                    .kind = ANALYSIS_IMPORT_TYPE_FINISH_ARRAY,
                    .as.array =
                        {
                            .kind = type->kind,
                            .element = destination->types.builtin.poison,
                            .count = type->kind == ANALYSIS_TYPE_ARRAY ? type->as.array.count : type->as.vector.count,
                        },
                };
                top = finish;
                analysis_import_type_task_push(scratch_arena, &top,
                                               (AnalysisImportTypeTask){
                                                   .destination = &finish->as.array.element,
                                                   .kind = ANALYSIS_IMPORT_TYPE_VISIT,
                                                   .as.source = type->kind == ANALYSIS_TYPE_ARRAY ? type->as.array.element_type : type->as.vector.element_type,
                                               });
            }
            break;
            case ANALYSIS_TYPE_FUNCTION:
            {
                u32 count = type->as.function.argument_count;
                AnalysisTypeId* arguments = arena_allocate(scratch_arena, AnalysisTypeId, count);
                AnalysisImportTypeTask* finish = arena_allocate(scratch_arena, AnalysisImportTypeTask, 1);
                *finish = (AnalysisImportTypeTask){
                    .previous = top,
                    .destination = task->destination,
                    .kind = ANALYSIS_IMPORT_TYPE_FINISH_FUNCTION,
                    .as.function =
                        {
                            .arguments = arguments,
                            .return_type = destination->types.builtin.poison,
                            .calling_convention = type->as.function.calling_convention,
                            .argument_count = count,
                            .is_variadic = type->as.function.is_variadic,
                        },
                };
                top = finish;
                analysis_import_type_task_push(scratch_arena, &top,
                                               (AnalysisImportTypeTask){
                                                   .destination = &finish->as.function.return_type,
                                                   .kind = ANALYSIS_IMPORT_TYPE_VISIT,
                                                   .as.source = type->as.function.return_type,
                                               });
                for (u32 index = count; index > 0; index -= 1)
                {
                    analysis_import_type_task_push(scratch_arena, &top,
                                                   (AnalysisImportTypeTask){
                                                       .destination = arguments + index - 1,
                                                       .kind = ANALYSIS_IMPORT_TYPE_VISIT,
                                                       .as.source = type->as.function.argument_types[index - 1],
                                                   });
                }
            }
            break;
            case ANALYSIS_TYPE_COUNT:
                BUSTER_UNREACHABLE();
            }
            continue;
        }

        if (task->kind == ANALYSIS_IMPORT_TYPE_FINISH_ELEMENT)
        {
            *task->destination = analysis_type_intern(result_arena, &destination->types,
                                                      (AnalysisType){
                                                          .kind = task->as.element.kind,
                                                          .as.element_type = task->as.element.element,
                                                      });
        }
        else if (task->kind == ANALYSIS_IMPORT_TYPE_FINISH_ARRAY)
        {
            *task->destination = analysis_type_intern(
                    result_arena,
                    &destination->types,
                    task->as.array.kind == ANALYSIS_TYPE_ARRAY ?
                        (AnalysisType){
                            .kind = ANALYSIS_TYPE_ARRAY,
                            .as.array = {
                                .element_type = task->as.array.element,
                                .count = task->as.array.count,
                            },
                        } :
                        (AnalysisType){
                            .kind = ANALYSIS_TYPE_VECTOR,
                            .as.vector = {
                                .element_type = task->as.array.element,
                                .count = task->as.array.count,
                            },
                        });
        }
        else
        {
            BUSTER_CHECK(task->kind == ANALYSIS_IMPORT_TYPE_FINISH_FUNCTION);
            *task->destination = analysis_type_intern(result_arena, &destination->types,
                                                      (AnalysisType){
                                                          .kind = ANALYSIS_TYPE_FUNCTION,
                                                          .as.function =
                                                              {
                                                                  .argument_types = task->as.function.arguments,
                                                                  .return_type = task->as.function.return_type,
                                                                  .calling_convention = task->as.function.calling_convention,
                                                                  .argument_count = task->as.function.argument_count,
                                                                  .is_variadic = task->as.function.is_variadic,
                                                              },
                                                      });
        }
    }
    return result;
}

typedef enum AnalysisSubstituteTaskKind
{
    ANALYSIS_SUBSTITUTE_VISIT,
    ANALYSIS_SUBSTITUTE_FINISH_ELEMENT,
    ANALYSIS_SUBSTITUTE_FINISH_ARRAY,
    ANALYSIS_SUBSTITUTE_FINISH_FUNCTION,
} AnalysisSubstituteTaskKind;

typedef struct AnalysisSubstituteTask AnalysisSubstituteTask;
struct AnalysisSubstituteTask
{
    AnalysisSubstituteTask* previous;
    AnalysisTypeId* destination;
    AnalysisSubstituteTaskKind kind;
    union
    {
        AnalysisTypeId source;
        struct
        {
            AnalysisTypeKind kind;
            AnalysisTypeId element;
        } element;
        struct
        {
            AnalysisTypeKind kind;
            AnalysisTypeId element;
            u64 count;
        } array;
        struct
        {
            AnalysisTypeId* arguments;
            AnalysisTypeId return_type;
            AstCallingConvention calling_convention;
            u32 argument_count;
            bool is_variadic;
            u8 reserved[3];
        } function;
    } as;
};

BUSTER_GLOBAL_LOCAL void analysis_substitute_task_push(Arena* scratch_arena, AnalysisSubstituteTask** top, AnalysisSubstituteTask task)
{
    AnalysisSubstituteTask* pushed = arena_allocate(scratch_arena, AnalysisSubstituteTask, 1);
    task.previous = *top;
    *pushed = task;
    *top = pushed;
}

BUSTER_GLOBAL_LOCAL AnalysisTypeId analysis_generic_binding_find(AnalysisGenericTypeBinding* bindings, u32 binding_count, String8 name)
{
    for (u32 index = 0; index < binding_count; index += 1)
    {
        if (string_equal(bindings[index].name, name))
        {
            return bindings[index].type;
        }
    }
    return ANALYSIS_TYPE_ID_INVALID;
}

BUSTER_GLOBAL_LOCAL AnalysisTypeId analysis_type_substitute(Arena* result_arena, Arena* scratch_arena, AnalysisResult* result, AnalysisTypeId source,
                                                            AnalysisGenericTypeBinding* bindings, u32 binding_count)
{
    AnalysisTypeId substituted = result->types.builtin.poison;
    AnalysisSubstituteTask* top = 0;
    analysis_substitute_task_push(scratch_arena, &top,
                                  (AnalysisSubstituteTask){
                                      .destination = &substituted,
                                      .kind = ANALYSIS_SUBSTITUTE_VISIT,
                                      .as.source = source,
                                  });
    while (top)
    {
        AnalysisSubstituteTask* task = top;
        top = task->previous;
        if (task->kind == ANALYSIS_SUBSTITUTE_VISIT)
        {
            AnalysisType* type = analysis_type_from_id(result, task->as.source);
            switch (type->kind)
            {
            case ANALYSIS_TYPE_COMPILE_TIME_PARAMETER:
            {
                AnalysisTypeId binding = analysis_generic_binding_find(bindings, binding_count, type->name);
                *task->destination = binding.value == ANALYSIS_ID_UNDERLYING_INVALID ? task->as.source : binding;
            }
            break;
            case ANALYSIS_TYPE_POINTER:
            case ANALYSIS_TYPE_SLICE:
            case ANALYSIS_TYPE_INFERRED_ARRAY:
            case ANALYSIS_TYPE_RANGE:
            {
                AnalysisSubstituteTask* finish = arena_allocate(scratch_arena, AnalysisSubstituteTask, 1);
                *finish = (AnalysisSubstituteTask){
                    .previous = top,
                    .destination = task->destination,
                    .kind = ANALYSIS_SUBSTITUTE_FINISH_ELEMENT,
                    .as.element =
                        {
                            .kind = type->kind,
                            .element = result->types.builtin.poison,
                        },
                };
                top = finish;
                analysis_substitute_task_push(scratch_arena, &top,
                                              (AnalysisSubstituteTask){
                                                  .destination = &finish->as.element.element,
                                                  .kind = ANALYSIS_SUBSTITUTE_VISIT,
                                                  .as.source = type->as.element_type,
                                              });
            }
            break;
            case ANALYSIS_TYPE_ARRAY:
            case ANALYSIS_TYPE_VECTOR:
            {
                AnalysisSubstituteTask* finish = arena_allocate(scratch_arena, AnalysisSubstituteTask, 1);
                *finish = (AnalysisSubstituteTask){
                    .previous = top,
                    .destination = task->destination,
                    .kind = ANALYSIS_SUBSTITUTE_FINISH_ARRAY,
                    .as.array =
                        {
                            .kind = type->kind,
                            .element = result->types.builtin.poison,
                            .count = type->kind == ANALYSIS_TYPE_ARRAY ? type->as.array.count : type->as.vector.count,
                        },
                };
                top = finish;
                analysis_substitute_task_push(scratch_arena, &top,
                                              (AnalysisSubstituteTask){
                                                  .destination = &finish->as.array.element,
                                                  .kind = ANALYSIS_SUBSTITUTE_VISIT,
                                                  .as.source = type->kind == ANALYSIS_TYPE_ARRAY ? type->as.array.element_type : type->as.vector.element_type,
                                              });
            }
            break;
            case ANALYSIS_TYPE_FUNCTION:
            {
                u32 count = type->as.function.argument_count;
                AnalysisTypeId* arguments = arena_allocate(scratch_arena, AnalysisTypeId, count);
                AnalysisSubstituteTask* finish = arena_allocate(scratch_arena, AnalysisSubstituteTask, 1);
                *finish = (AnalysisSubstituteTask){
                    .previous = top,
                    .destination = task->destination,
                    .kind = ANALYSIS_SUBSTITUTE_FINISH_FUNCTION,
                    .as.function =
                        {
                            .arguments = arguments,
                            .return_type = result->types.builtin.poison,
                            .calling_convention = type->as.function.calling_convention,
                            .argument_count = count,
                            .is_variadic = type->as.function.is_variadic,
                        },
                };
                top = finish;
                analysis_substitute_task_push(scratch_arena, &top,
                                              (AnalysisSubstituteTask){
                                                  .destination = &finish->as.function.return_type,
                                                  .kind = ANALYSIS_SUBSTITUTE_VISIT,
                                                  .as.source = type->as.function.return_type,
                                              });
                for (u32 index = count; index > 0; index -= 1)
                {
                    analysis_substitute_task_push(scratch_arena, &top,
                                                  (AnalysisSubstituteTask){
                                                      .destination = arguments + index - 1,
                                                      .kind = ANALYSIS_SUBSTITUTE_VISIT,
                                                      .as.source = type->as.function.argument_types[index - 1],
                                                  });
                }
            }
            break;
            case ANALYSIS_TYPE_POISON:
            case ANALYSIS_TYPE_VOID:
            case ANALYSIS_TYPE_BOOL:
            case ANALYSIS_TYPE_INTEGER:
            case ANALYSIS_TYPE_FLOAT:
            case ANALYSIS_TYPE_VA_LIST:
            case ANALYSIS_TYPE_STRUCT:
            case ANALYSIS_TYPE_UNION:
            case ANALYSIS_TYPE_ENUM:
            {
                *task->destination = task->as.source;
            }
            break;
            case ANALYSIS_TYPE_COUNT:
                BUSTER_UNREACHABLE();
            }
            continue;
        }

        if (task->kind == ANALYSIS_SUBSTITUTE_FINISH_ELEMENT)
        {
            *task->destination = analysis_type_intern(result_arena, &result->types,
                                                      (AnalysisType){
                                                          .kind = task->as.element.kind,
                                                          .as.element_type = task->as.element.element,
                                                      });
        }
        else if (task->kind == ANALYSIS_SUBSTITUTE_FINISH_ARRAY)
        {
            *task->destination = analysis_type_intern(
                result_arena,
                &result->types,
                task->as.array.kind == ANALYSIS_TYPE_ARRAY ?
                    (AnalysisType){
                        .kind = ANALYSIS_TYPE_ARRAY,
                        .as.array = {
                            .element_type = task->as.array.element,
                            .count = task->as.array.count,
                        },
                    } :
                    (AnalysisType){
                        .kind = ANALYSIS_TYPE_VECTOR,
                        .as.vector = {
                            .element_type = task->as.array.element,
                            .count = task->as.array.count,
                        },
                    });
        }
        else
        {
            BUSTER_CHECK(task->kind == ANALYSIS_SUBSTITUTE_FINISH_FUNCTION);
            *task->destination = analysis_type_intern(result_arena, &result->types,
                                                      (AnalysisType){
                                                          .kind = ANALYSIS_TYPE_FUNCTION,
                                                          .as.function =
                                                              {
                                                                  .argument_types = task->as.function.arguments,
                                                                  .return_type = task->as.function.return_type,
                                                                  .calling_convention = task->as.function.calling_convention,
                                                                  .argument_count = task->as.function.argument_count,
                                                                  .is_variadic = task->as.function.is_variadic,
                                                              },
                                                      });
        }
    }
    return substituted;
}

BUSTER_GLOBAL_LOCAL AnalysisTypeId analysis_builtin_type_find(AnalysisTypeTable* table, String8 name)
{
    for (u32 index = 0; index < table->count; index += 1)
    {
        AnalysisType* type = table->types + index;
        if (type->name.length && string_equal(type->name, name))
        {
            return type->id;
        }
    }
    return ANALYSIS_TYPE_ID_INVALID;
}

BUSTER_GLOBAL_LOCAL AnalysisEntity* analysis_named_type_find(AnalysisResult* result, String8 name)
{
    for (u32 index = 0; index < result->module.entity_count; index += 1)
    {
        AnalysisEntity* entity = result->module.entities + index;
        if (entity->name_space == ANALYSIS_NAMESPACE_TYPE && string_equal(entity->name, name))
        {
            return entity;
        }
    }
    return 0;
}

BUSTER_GLOBAL_LOCAL AnalysisImport* analysis_import_find(AnalysisResult* result, String8 name_space)
{
    AnalysisImport* found = 0;
    for (u32 index = 0; index < result->module.import_count; index += 1)
    {
        AnalysisImport* import = result->module.imports + index;
        if (string_equal(import->name_space, name_space))
        {
            if (found)
            {
                return 0;
            }
            found = import;
        }
    }
    return found;
}

typedef enum AnalysisTypeTaskKind
{
    ANALYSIS_TYPE_TASK_AST,
    ANALYSIS_TYPE_TASK_ENTITY,
    ANALYSIS_TYPE_TASK_FINISH_ELEMENT,
    ANALYSIS_TYPE_TASK_FINISH_ARRAY,
    ANALYSIS_TYPE_TASK_FINISH_VECTOR,
    ANALYSIS_TYPE_TASK_FINISH_FUNCTION,
    ANALYSIS_TYPE_TASK_FINISH_ENTITY,
    ANALYSIS_TYPE_TASK_FINISH_FIELDS,
} AnalysisTypeTaskKind;

typedef struct AnalysisTypeTask AnalysisTypeTask;
struct AnalysisTypeTask
{
    AnalysisTypeTask* previous;
    AnalysisEntity* owner;
    AnalysisTypeId* destination;
    ParserSourceRange use_range;
    AnalysisTypeTaskKind kind;
    union
    {
        AstType* ast;
        AnalysisEntity* entity;
        struct
        {
            AnalysisTypeKind kind;
            AnalysisTypeId element;
        } element;
        struct
        {
            AnalysisTypeId element;
            u64 count;
        } array;
        struct
        {
            AnalysisTypeId* arguments;
            AnalysisTypeId return_type;
            AstCallingConvention calling_convention;
            u32 argument_count;
            bool is_variadic;
            u8 reserved[3];
        } function;
        struct
        {
            AnalysisEntity* entity;
            AnalysisTypeId resolved_type;
        } finish_entity;
    } as;
};

typedef struct AnalysisResolutionContext AnalysisResolutionContext;
struct AnalysisResolutionContext
{
    Arena* result_arena;
    Arena* scratch_arena;
    AnalysisResult* result;
    AnalysisTypeTask* top;
};

BUSTER_GLOBAL_LOCAL AnalysisTypeTask* analysis_type_task_push(AnalysisResolutionContext* context, AnalysisTypeTask task)
{
    AnalysisTypeTask* result = arena_allocate(context->scratch_arena, AnalysisTypeTask, 1);
    task.previous = context->top;
    *result = task;
    context->top = result;
    return result;
}

BUSTER_GLOBAL_LOCAL void analysis_type_task_ast_push(AnalysisResolutionContext* context, AnalysisEntity* owner, AstType* ast, AnalysisTypeId* destination)
{
    analysis_type_task_push(context, (AnalysisTypeTask){
                                         .owner = owner,
                                         .destination = destination,
                                         .use_range = ast ? ast->range : owner->range,
                                         .kind = ANALYSIS_TYPE_TASK_AST,
                                         .as.ast = ast,
                                     });
}

BUSTER_GLOBAL_LOCAL void analysis_type_task_entity_push(AnalysisResolutionContext* context, AnalysisEntity* owner, AnalysisEntity* entity,
                                                        ParserSourceRange use_range, AnalysisTypeId* destination)
{
    analysis_type_task_push(context, (AnalysisTypeTask){
                                         .owner = owner,
                                         .destination = destination,
                                         .use_range = use_range,
                                         .kind = ANALYSIS_TYPE_TASK_ENTITY,
                                         .as.entity = entity,
                                     });
}

BUSTER_GLOBAL_LOCAL bool analysis_type_is_poison(AnalysisResolutionContext* context, AnalysisTypeId id)
{
    return analysis_type_id_equal(id, context->result->types.builtin.poison);
}

BUSTER_GLOBAL_LOCAL void analysis_resolve_ast_task(AnalysisResolutionContext* context, AnalysisTypeTask* task)
{
    AnalysisTypeId poison = context->result->types.builtin.poison;
    AstType* ast = task->as.ast;
    if (!ast)
    {
        *task->destination = poison;
        return;
    }

    switch (ast->id)
    {
    case AST_TYPE_NAMED:
    {
        if (ast->is_compile_time)
        {
            *task->destination = analysis_type_intern(context->result_arena, &context->result->types,
                                                      (AnalysisType){
                                                          .name = ast->name,
                                                          .kind = ANALYSIS_TYPE_COMPILE_TIME_PARAMETER,
                                                      });
            return;
        }
        AnalysisTypeId builtin = analysis_builtin_type_find(&context->result->types, ast->name);
        if (builtin.value != ANALYSIS_ID_UNDERLYING_INVALID)
        {
            *task->destination = builtin;
            return;
        }

        AnalysisEntity* entity = analysis_named_type_find(context->result, ast->name);
        if (!entity)
        {
            analysis_type_diagnostic_push(context->result_arena, context->result, task->owner, ast->range, ANALYSIS_DIAGNOSTIC_UNKNOWN_TYPE, ast->name);
            *task->destination = poison;
            return;
        }

        AstTypeDeclaration* declaration = entity->ast.type_declaration;
        AnalysisEntitySemantic* semantic = context->result->module.semantics + entity->id.index.value;
        if (declaration->kind != AST_TYPE_DECLARATION_ALIAS)
        {
            *task->destination = semantic->type;
            return;
        }
        analysis_type_task_entity_push(context, task->owner, entity, ast->range, task->destination);
    }
    break;
    case AST_TYPE_QUALIFIED_NAMED:
    {
        AnalysisImport* import = analysis_import_find(context->result, ast->qualified.name_space.text);
        if (import && import->state != ANALYSIS_IMPORT_RESOLVED)
        {
            *task->destination = poison;
            return;
        }
        AnalysisEntity* entity =
            import ? analysis_find_qualified_entity(context->result, ast->qualified.name_space.text, ast->qualified.name.text, ANALYSIS_NAMESPACE_TYPE) : 0;
        if (!entity || !import->target->types.types)
        {
            analysis_type_diagnostic_push(context->result_arena, context->result, task->owner, ast->range, ANALYSIS_DIAGNOSTIC_UNKNOWN_TYPE,
                                          ast->qualified.name.text);
            *task->destination = poison;
            return;
        }

        AnalysisEntitySemantic* semantic = import->target->module.semantics + entity->id.index.value;
        if (semantic->state != ANALYSIS_RESOLUTION_RESOLVED)
        {
            analysis_type_diagnostic_push(context->result_arena, context->result, task->owner, ast->range, ANALYSIS_DIAGNOSTIC_UNKNOWN_TYPE,
                                          ast->qualified.name.text);
            *task->destination = poison;
            return;
        }
        *task->destination = analysis_type_import(context->result_arena, context->scratch_arena, context->result, import->target, semantic->type);
    }
    break;
    case AST_TYPE_POINTER:
    case AST_TYPE_SLICE:
    case AST_TYPE_INFERRED_ARRAY:
    {
        AnalysisTypeKind kind = ANALYSIS_TYPE_POINTER;
        if (ast->id == AST_TYPE_SLICE)
        {
            kind = ANALYSIS_TYPE_SLICE;
        }
        else if (ast->id == AST_TYPE_INFERRED_ARRAY)
        {
            kind = ANALYSIS_TYPE_INFERRED_ARRAY;
        }
        AnalysisTypeTask* finish = analysis_type_task_push(context, (AnalysisTypeTask){
                                                                        .owner = task->owner,
                                                                        .destination = task->destination,
                                                                        .use_range = ast->range,
                                                                        .kind = ANALYSIS_TYPE_TASK_FINISH_ELEMENT,
                                                                        .as.element = {.kind = kind, .element = poison},
                                                                    });
        analysis_type_task_ast_push(context, task->owner, ast->element_type, &finish->as.element.element);
    }
    break;
    case AST_TYPE_ARRAY:
    {
        AnalysisTypeTask* finish = analysis_type_task_push(context, (AnalysisTypeTask){
                                                                        .owner = task->owner,
                                                                        .destination = task->destination,
                                                                        .use_range = ast->range,
                                                                        .kind = ANALYSIS_TYPE_TASK_FINISH_ARRAY,
                                                                        .as.array = {.element = poison, .count = ast->array.count.value},
                                                                    });
        analysis_type_task_ast_push(context, task->owner, ast->array.element_type, &finish->as.array.element);
    }
    break;
    case AST_TYPE_VECTOR:
    {
        AnalysisTypeTask* finish = analysis_type_task_push(context, (AnalysisTypeTask){
                                                                        .owner = task->owner,
                                                                        .destination = task->destination,
                                                                        .use_range = ast->range,
                                                                        .kind = ANALYSIS_TYPE_TASK_FINISH_VECTOR,
                                                                        .as.array = {.element = poison, .count = ast->vector.count.value},
                                                                    });
        analysis_type_task_ast_push(context, task->owner, ast->vector.element_type, &finish->as.array.element);
    }
    break;
    case AST_TYPE_FUNCTION:
    {
        u32 argument_count = 0;
        for (AstTypeArgument* argument = ast->function.first_argument; argument; argument = argument->next)
        {
            argument_count += 1;
        }
        AnalysisTypeId* arguments = arena_allocate(context->scratch_arena, AnalysisTypeId, argument_count);
        for (u32 index = 0; index < argument_count; index += 1)
        {
            arguments[index] = poison;
        }
        AnalysisTypeTask* finish = analysis_type_task_push(context, (AnalysisTypeTask){
                                                                        .owner = task->owner,
                                                                        .destination = task->destination,
                                                                        .use_range = ast->range,
                                                                        .kind = ANALYSIS_TYPE_TASK_FINISH_FUNCTION,
                                                                        .as.function =
                                                                            {
                                                                                .arguments = arguments,
                                                                                .return_type = poison,
                                                                                .calling_convention = ast->function.calling_convention,
                                                                                .argument_count = argument_count,
                                                                                .is_variadic = ast->function.is_variadic,
                                                                            },
                                                                    });
        analysis_type_task_ast_push(context, task->owner, ast->function.return_type, &finish->as.function.return_type);
        u32 argument_index = 0;
        for (AstTypeArgument* argument = ast->function.first_argument; argument; argument = argument->next)
        {
            analysis_type_task_ast_push(context, task->owner, argument->type, arguments + argument_index);
            argument_index += 1;
        }
    }
    break;
    case AST_TYPE_COUNT:
    {
        *task->destination = poison;
    }
    break;
    }
}

BUSTER_GLOBAL_LOCAL void analysis_resolve_entity_task(AnalysisResolutionContext* context, AnalysisTypeTask* task)
{
    AnalysisEntity* entity = task->as.entity;
    AnalysisEntitySemantic* semantic = context->result->module.semantics + entity->id.index.value;
    AnalysisTypeId poison = context->result->types.builtin.poison;
    if (semantic->state == ANALYSIS_RESOLUTION_RESOLVED)
    {
        if (task->destination)
        {
            *task->destination = semantic->type;
        }
        return;
    }
    if (semantic->state == ANALYSIS_RESOLUTION_ERROR)
    {
        if (task->destination)
        {
            *task->destination = poison;
        }
        return;
    }
    if (semantic->state == ANALYSIS_RESOLUTION_RESOLVING)
    {
        analysis_type_diagnostic_push(context->result_arena, context->result, task->owner, task->use_range, ANALYSIS_DIAGNOSTIC_TYPE_ALIAS_CYCLE, entity->name);
        semantic->state = ANALYSIS_RESOLUTION_ERROR;
        if (task->destination)
        {
            *task->destination = poison;
        }
        return;
    }

    semantic->state = ANALYSIS_RESOLUTION_RESOLVING;
    if (entity->kind == ANALYSIS_ENTITY_CODE)
    {
        AnalysisTypeTask* finish = analysis_type_task_push(context, (AnalysisTypeTask){
                                                                        .owner = entity,
                                                                        .destination = task->destination,
                                                                        .use_range = entity->range,
                                                                        .kind = ANALYSIS_TYPE_TASK_FINISH_ENTITY,
                                                                        .as.finish_entity = {.entity = entity, .resolved_type = poison},
                                                                    });
        analysis_type_task_ast_push(context, entity, entity->ast.code->type, &finish->as.finish_entity.resolved_type);
        return;
    }
    if (entity->kind == ANALYSIS_ENTITY_DATA)
    {
        AnalysisTypeTask* finish = analysis_type_task_push(context, (AnalysisTypeTask){
                                                                        .owner = entity,
                                                                        .destination = task->destination,
                                                                        .use_range = entity->range,
                                                                        .kind = ANALYSIS_TYPE_TASK_FINISH_ENTITY,
                                                                        .as.finish_entity =
                                                                            {
                                                                                .entity = entity,
                                                                                .resolved_type = poison,
                                                                            },
                                                                    });
        analysis_type_task_ast_push(context, entity, entity->ast.data->type, &finish->as.finish_entity.resolved_type);
        return;
    }

    AstTypeDeclaration* declaration = entity->ast.type_declaration;
    if (declaration->kind == AST_TYPE_DECLARATION_ALIAS)
    {
        AnalysisTypeTask* finish = analysis_type_task_push(context, (AnalysisTypeTask){
                                                                        .owner = entity,
                                                                        .destination = task->destination,
                                                                        .use_range = entity->range,
                                                                        .kind = ANALYSIS_TYPE_TASK_FINISH_ENTITY,
                                                                        .as.finish_entity = {.entity = entity, .resolved_type = poison},
                                                                    });
        analysis_type_task_ast_push(context, entity, declaration->alias_type, &finish->as.finish_entity.resolved_type);
        return;
    }

    if (declaration->kind == AST_TYPE_DECLARATION_ENUM)
    {
        semantic->state = ANALYSIS_RESOLUTION_RESOLVED;
        if (task->destination)
        {
            *task->destination = semantic->type;
        }
        return;
    }

    semantic->field_count = declaration->field_count;
    semantic->fields = arena_allocate(context->result_arena, AnalysisField, semantic->field_count);
    AnalysisTypeTask* finish = analysis_type_task_push(context, (AnalysisTypeTask){
                                                                    .owner = entity,
                                                                    .destination = task->destination,
                                                                    .use_range = entity->range,
                                                                    .kind = ANALYSIS_TYPE_TASK_FINISH_FIELDS,
                                                                    .as.entity = entity,
                                                                });
    BUSTER_UNUSED(finish);
    u32 field_index = 0;
    for (AstTypeField* field = declaration->first_field; field; field = field->next)
    {
        BUSTER_CHECK(field_index < semantic->field_count);
        semantic->fields[field_index] = (AnalysisField){
            .name = field->name.text,
            .range = field->range,
            .type = poison,
        };
        analysis_type_task_ast_push(context, entity, field->type, &semantic->fields[field_index].type);
        field_index += 1;
    }
    BUSTER_CHECK(field_index == semantic->field_count);
}

BUSTER_GLOBAL_LOCAL void analysis_finish_type_task(AnalysisResolutionContext* context, AnalysisTypeTask* task)
{
    AnalysisTypeId poison = context->result->types.builtin.poison;
    switch (task->kind)
    {
    case ANALYSIS_TYPE_TASK_FINISH_ELEMENT:
    {
        if (analysis_type_is_poison(context, task->as.element.element))
        {
            *task->destination = poison;
            return;
        }
        *task->destination = analysis_type_intern(context->result_arena, &context->result->types,
                                                  (AnalysisType){
                                                      .kind = task->as.element.kind,
                                                      .as.element_type = task->as.element.element,
                                                  });
    }
    break;
    case ANALYSIS_TYPE_TASK_FINISH_ARRAY:
    {
        if (analysis_type_is_poison(context, task->as.array.element))
        {
            *task->destination = poison;
            return;
        }
        *task->destination = analysis_type_intern(context->result_arena, &context->result->types,
                                                  (AnalysisType){
                                                      .kind = ANALYSIS_TYPE_ARRAY,
                                                      .as.array =
                                                          {
                                                              .element_type = task->as.array.element,
                                                              .count = task->as.array.count,
                                                          },
                                                  });
    }
    break;
    case ANALYSIS_TYPE_TASK_FINISH_VECTOR:
    {
        if (analysis_type_is_poison(context, task->as.array.element))
        {
            *task->destination = poison;
            return;
        }
        AnalysisType* element = analysis_type_from_id(context->result, task->as.array.element);
        u64 element_bits = element->kind == ANALYSIS_TYPE_INTEGER ? element->as.integer.bit_width
                           : element->kind == ANALYSIS_TYPE_FLOAT ? element->as.float_bit_width
                                                                  : 0;
        u64 total_bits = 0;
        bool overflow = task->as.array.count && element_bits > UINT64_MAX / task->as.array.count;
        if (!overflow)
        {
            total_bits = element_bits * task->as.array.count;
        }
        bool valid_size = total_bits == 64 || total_bits == 128 || total_bits == 256 || total_bits == 512;
        if (!element_bits || overflow || !valid_size)
        {
            analysis_type_diagnostic_push(context->result_arena, context->result, task->owner, task->use_range, ANALYSIS_DIAGNOSTIC_INVALID_VECTOR_TYPE,
                                          (String8){0});
            *task->destination = poison;
            return;
        }
        *task->destination = analysis_type_intern(context->result_arena, &context->result->types,
                                                  (AnalysisType){
                                                      .kind = ANALYSIS_TYPE_VECTOR,
                                                      .as.vector =
                                                          {
                                                              .element_type = task->as.array.element,
                                                              .count = task->as.array.count,
                                                          },
                                                  });
    }
    break;
    case ANALYSIS_TYPE_TASK_FINISH_FUNCTION:
    {
        bool poisoned = analysis_type_is_poison(context, task->as.function.return_type);
        for (u32 index = 0; index < task->as.function.argument_count; index += 1)
        {
            poisoned = poisoned || analysis_type_is_poison(context, task->as.function.arguments[index]);
        }
        if (poisoned)
        {
            *task->destination = poison;
            return;
        }
        *task->destination = analysis_type_intern(context->result_arena, &context->result->types,
                                                  (AnalysisType){
                                                      .kind = ANALYSIS_TYPE_FUNCTION,
                                                      .as.function =
                                                          {
                                                              .argument_types = task->as.function.arguments,
                                                              .return_type = task->as.function.return_type,
                                                              .calling_convention = task->as.function.calling_convention,
                                                              .argument_count = task->as.function.argument_count,
                                                              .is_variadic = task->as.function.is_variadic,
                                                          },
                                                  });
    }
    break;
    case ANALYSIS_TYPE_TASK_FINISH_ENTITY:
    {
        AnalysisEntity* entity = task->as.finish_entity.entity;
        AnalysisEntitySemantic* semantic = context->result->module.semantics + entity->id.index.value;
        semantic->type = task->as.finish_entity.resolved_type;
        semantic->state = analysis_type_is_poison(context, semantic->type) ? ANALYSIS_RESOLUTION_ERROR : ANALYSIS_RESOLUTION_RESOLVED;
        if (task->destination)
        {
            *task->destination = semantic->type;
        }
    }
    break;
    case ANALYSIS_TYPE_TASK_FINISH_FIELDS:
    {
        AnalysisEntity* entity = task->as.entity;
        AnalysisEntitySemantic* semantic = context->result->module.semantics + entity->id.index.value;
        bool poisoned = false;
        for (u32 index = 0; index < semantic->field_count; index += 1)
        {
            poisoned = poisoned || analysis_type_is_poison(context, semantic->fields[index].type);
        }
        semantic->state = poisoned ? ANALYSIS_RESOLUTION_ERROR : ANALYSIS_RESOLUTION_RESOLVED;
        if (task->destination)
        {
            *task->destination = semantic->type;
        }
    }
    break;
    case ANALYSIS_TYPE_TASK_AST:
    case ANALYSIS_TYPE_TASK_ENTITY:
        break;
    }
}

BUSTER_GLOBAL_LOCAL void analysis_resolution_run(AnalysisResolutionContext* context)
{
    while (context->top)
    {
        AnalysisTypeTask* task = context->top;
        context->top = task->previous;
        if (task->kind == ANALYSIS_TYPE_TASK_AST)
        {
            analysis_resolve_ast_task(context, task);
        }
        else if (task->kind == ANALYSIS_TYPE_TASK_ENTITY)
        {
            analysis_resolve_entity_task(context, task);
        }
        else
        {
            analysis_finish_type_task(context, task);
        }
    }
}

BUSTER_GLOBAL_LOCAL u32 analysis_node_arity(AstNode* node);
BUSTER_GLOBAL_LOCAL AnalysisConstant analysis_constant_integer_binary(AstNodeId operation, AnalysisConstant left, AnalysisConstant right);

BUSTER_GLOBAL_LOCAL bool analysis_enum_constant_evaluate(Arena* scratch_arena, AstExpression expression, u64* value)
{
    AnalysisConstant* stack = arena_allocate(scratch_arena, AnalysisConstant, expression.count);
    u32 count = 0;
    for (u32 index = 0; index < expression.count; index += 1)
    {
        stack[index] = (AnalysisConstant){0};
    }
    for (u32 index = 0; index < expression.count; index += 1)
    {
        AstNode* node = expression.nodes + index;
        if (node->id == AST_NODE_CONSTANT_INTEGER)
        {
            stack[count] = (AnalysisConstant){
                .integer = node->integer.value,
                .kind = ANALYSIS_CONSTANT_INTEGER,
            };
            count += 1;
        }
        else if ((node->id == AST_NODE_UNARY_MINUS || node->id == AST_NODE_UNARY_PLUS) && count)
        {
            if (node->id == AST_NODE_UNARY_MINUS)
            {
                stack[count - 1].is_negative = !stack[count - 1].is_negative;
            }
        }
        else if (analysis_node_arity(node) == 2 && count >= 2)
        {
            AnalysisConstant right = stack[count - 1];
            AnalysisConstant left = stack[count - 2];
            count -= 2;
            AnalysisConstant folded = analysis_constant_integer_binary(node->id, left, right);
            if (folded.kind != ANALYSIS_CONSTANT_INTEGER)
            {
                return false;
            }
            stack[count] = folded;
            count += 1;
        }
        else
        {
            return false;
        }
    }
    if (count != 1 || stack[0].kind != ANALYSIS_CONSTANT_INTEGER || stack[0].is_negative)
    {
        return false;
    }
    *value = stack[0].integer;
    return true;
}

BUSTER_GLOBAL_LOCAL void analysis_validate_module_declarations(Arena* result_arena, Arena* scratch_arena, AnalysisResult* result)
{
    for (u32 entity_index = 0; entity_index < result->module.entity_count; entity_index += 1)
    {
        AnalysisEntity* entity = result->module.entities + entity_index;
        if (entity->kind == ANALYSIS_ENTITY_CODE)
        {
            AstType* function = entity->ast.code->type;
            for (AstTypeArgument* argument = function->function.first_argument; argument; argument = argument->next)
            {
                for (AstTypeArgument* previous = function->function.first_argument; previous && previous != argument; previous = previous->next)
                {
                    if (string_equal(previous->name, argument->name))
                    {
                        analysis_entity_diagnostic_push(result_arena, result, entity, argument->range, ANALYSIS_DIAGNOSTIC_DUPLICATE_LOCAL, argument->name,
                                                        S8("duplicate function argument"));
                        break;
                    }
                }
            }
            continue;
        }
        if (entity->kind != ANALYSIS_ENTITY_TYPE)
        {
            continue;
        }
        AstTypeDeclaration* declaration = entity->ast.type_declaration;
        AnalysisEntitySemantic* semantic = result->module.semantics + entity_index;
        if (declaration->kind == AST_TYPE_DECLARATION_STRUCT || declaration->kind == AST_TYPE_DECLARATION_UNION)
        {
            u32 field_index = 0;
            for (AstTypeField* field = declaration->first_field; field; field = field->next)
            {
                u32 previous_index = 0;
                for (AstTypeField* previous = declaration->first_field; previous && previous != field; previous = previous->next, previous_index += 1)
                {
                    if (string_equal(previous->name.text, field->name.text))
                    {
                        analysis_entity_diagnostic_push(result_arena, result, entity, field->name.range, ANALYSIS_DIAGNOSTIC_DUPLICATE_FIELD, field->name.text,
                                                        S8("duplicate field declaration"));
                        break;
                    }
                }
                BUSTER_UNUSED(previous_index);
                field_index += 1;
            }
            BUSTER_CHECK(field_index == declaration->field_count);
        }
        else if (declaration->kind == AST_TYPE_DECLARATION_ENUM)
        {
            semantic->enum_member_count = declaration->enum_member_count;
            semantic->enum_members = arena_allocate(result_arena, AnalysisEnumMember, semantic->enum_member_count);
            u64 next_value = 0;
            u32 member_index = 0;
            for (AstEnumMember* member = declaration->first_enum_member; member; member = member->next, member_index += 1)
            {
                for (AstEnumMember* previous = declaration->first_enum_member; previous && previous != member; previous = previous->next)
                {
                    if (string_equal(previous->name.text, member->name.text))
                    {
                        analysis_entity_diagnostic_push(result_arena, result, entity, member->name.range, ANALYSIS_DIAGNOSTIC_DUPLICATE_ENUM_MEMBER,
                                                        member->name.text, S8("duplicate enum member"));
                        break;
                    }
                }
                u64 member_value = next_value;
                if (member->has_explicit_value && !analysis_enum_constant_evaluate(scratch_arena, member->value, &member_value))
                {
                    analysis_entity_diagnostic_push(result_arena, result, entity, member->range, ANALYSIS_DIAGNOSTIC_INVALID_CONSTANT, member->name.text,
                                                    S8("enum value is not a constant non-negative integer"));
                }
                semantic->enum_members[member_index] = (AnalysisEnumMember){
                    .name = member->name.text,
                    .range = member->range,
                    .value = member_value,
                };
                next_value = member_value + 1;
            }
            BUSTER_CHECK(member_index == semantic->enum_member_count);
        }
    }
}

void analysis_resolve_module_interfaces(Arena* result_arena, AnalysisResult* result)
{
    BUSTER_CHECK(result->types.types == 0);
    Arena* conflicts[] = {result_arena};
    TemporalArena scratch = scratch_begin(conflicts, BUSTER_ARRAY_LENGTH(conflicts));
    u32 ast_type_count = analysis_ast_type_count(scratch.arena, result);
    u32 body_expression_node_count = 0;
    u32 ignored_local_count = 0;
    for (u32 index = 0; index < result->module.entity_count; index += 1)
    {
        AnalysisEntity* entity = result->module.entities + index;
        if (entity->kind == ANALYSIS_ENTITY_CODE && entity->ast.code->has_body)
        {
            analysis_body_capacity_measure(scratch.arena, entity->ast.code, &ignored_local_count, &body_expression_node_count);
        }
    }
    u32 builtin_count = 13;
    result->types.capacity = builtin_count + result->module.type_count + ast_type_count + body_expression_node_count;
    for (u32 import_index = 0; import_index < result->module.import_count; import_index += 1)
    {
        AnalysisImport* import = result->module.imports + import_index;
        if (import->state == ANALYSIS_IMPORT_RESOLVED && import->target && import->target->types.types)
        {
            BUSTER_CHECK(result->types.capacity <= UINT32_MAX - import->target->types.count);
            result->types.capacity += import->target->types.count;
        }
    }
    result->types.types = arena_allocate(result_arena, AnalysisType, result->types.capacity);
    analysis_builtin_types_initialize(&result->types);

    for (u32 index = 0; index < result->module.entity_count; index += 1)
    {
        AnalysisEntity* entity = result->module.entities + index;
        if (entity->kind != ANALYSIS_ENTITY_TYPE)
        {
            continue;
        }
        AstTypeDeclaration* declaration = entity->ast.type_declaration;
        if (declaration->kind == AST_TYPE_DECLARATION_ALIAS)
        {
            continue;
        }
        AnalysisTypeKind kind = ANALYSIS_TYPE_STRUCT;
        if (declaration->kind == AST_TYPE_DECLARATION_UNION)
        {
            kind = ANALYSIS_TYPE_UNION;
        }
        else if (declaration->kind == AST_TYPE_DECLARATION_ENUM)
        {
            kind = ANALYSIS_TYPE_ENUM;
        }
        AnalysisTypeId type = analysis_type_add(&result->types, (AnalysisType){
                                                                    .kind = kind,
                                                                    .as.declaration = entity->id,
                                                                });
        result->module.semantics[index].type = type;
    }

    AnalysisResolutionContext context = {
        .result_arena = result_arena,
        .scratch_arena = scratch.arena,
        .result = result,
    };
    for (u32 index = result->module.entity_count; index > 0; index -= 1)
    {
        AnalysisEntity* entity = result->module.entities + index - 1;
        analysis_type_task_entity_push(&context, entity, entity, entity->range, 0);
    }
    analysis_resolution_run(&context);
    analysis_validate_module_declarations(result_arena, scratch.arena, result);
    scratch_end(scratch);
}

typedef struct AnalysisBinding AnalysisBinding;
struct AnalysisBinding
{
    AnalysisBinding* previous;
    String8 name;
    AnalysisLocalId local;
    u32 scope_depth;
};

typedef struct AnalysisBodyTask AnalysisBodyTask;
struct AnalysisBodyTask
{
    AnalysisBodyTask* previous;
    AstStatement* statement;
    AnalysisBinding* bindings;
    u32 scope_depth;
    u32 loop_depth;
};

typedef struct AnalysisBodyContext AnalysisBodyContext;
struct AnalysisBodyContext
{
    Arena* result_arena;
    Arena* scratch_arena;
    AnalysisResult* result;
    AnalysisEntity* owner;
    AnalysisBody* body;
    AnalysisInstantiation* instantiation;
    AnalysisResolutionContext resolution;
};

typedef struct AnalysisFlow AnalysisFlow;
struct AnalysisFlow
{
    bool can_fall_through;
    bool has_break;
};

typedef struct AnalysisFlowStatement AnalysisFlowStatement;
struct AnalysisFlowStatement
{
    AstStatement* statement;
    AnalysisFlow flow;
};

BUSTER_GLOBAL_LOCAL String8 analysis_diagnostic_message(AnalysisDiagnosticKind kind)
{
    switch (kind)
    {
    case ANALYSIS_DIAGNOSTIC_UNKNOWN_IDENTIFIER:
        return S8("unknown identifier");
    case ANALYSIS_DIAGNOSTIC_USE_BEFORE_INITIALIZATION:
        return S8("local is used before it is initialized");
    case ANALYSIS_DIAGNOSTIC_DUPLICATE_LOCAL:
        return S8("duplicate local declaration in scope");
    case ANALYSIS_DIAGNOSTIC_TYPE_MISMATCH:
        return S8("type mismatch");
    case ANALYSIS_DIAGNOSTIC_EXPECTED_PLACE:
        return S8("expression is not assignable");
    case ANALYSIS_DIAGNOSTIC_NOT_CALLABLE:
        return S8("expression is not callable");
    case ANALYSIS_DIAGNOSTIC_ARGUMENT_COUNT:
        return S8("incorrect argument count");
    case ANALYSIS_DIAGNOSTIC_COMPILE_TIME_ARGUMENT_REQUIRED:
        return S8("compile-time argument must be a constant");
    case ANALYSIS_DIAGNOSTIC_UNKNOWN_MEMBER:
        return S8("unknown member");
    case ANALYSIS_DIAGNOSTIC_INVALID_OPERAND:
        return S8("invalid operand type");
    case ANALYSIS_DIAGNOSTIC_EXPECTED_CONTEXTUAL_TYPE:
        return S8("expression requires a contextual type");
    case ANALYSIS_DIAGNOSTIC_INVALID_CONTROL_FLOW:
        return S8("control-flow statement is outside a loop");
    case ANALYSIS_DIAGNOSTIC_DUPLICATE_FIELD:
        return S8("duplicate field declaration");
    case ANALYSIS_DIAGNOSTIC_DUPLICATE_ENUM_MEMBER:
        return S8("duplicate enum member");
    case ANALYSIS_DIAGNOSTIC_DUPLICATE_AGGREGATE_FIELD:
        return S8("duplicate aggregate literal field");
    case ANALYSIS_DIAGNOSTIC_MISSING_AGGREGATE_FIELD:
        return S8("missing aggregate literal field");
    case ANALYSIS_DIAGNOSTIC_INVALID_CONSTANT:
        return S8("constant value is not representable by its type");
    case ANALYSIS_DIAGNOSTIC_MISSING_RETURN:
        return S8("not every control-flow path returns a value");
    case ANALYSIS_DIAGNOSTIC_UNREACHABLE_STATEMENT:
        return S8("unreachable statement");
    case ANALYSIS_DIAGNOSTIC_DUPLICATE_SWITCH_CASE:
        return S8("duplicate switch case");
    case ANALYSIS_DIAGNOSTIC_NONEXHAUSTIVE_SWITCH:
        return S8("switch does not cover every enum member");
    case ANALYSIS_DIAGNOSTIC_DUPLICATE_DECLARATION:
        return S8("duplicate declaration");
    case ANALYSIS_DIAGNOSTIC_DUPLICATE_IMPORT_NAMESPACE:
        return S8("duplicate import namespace");
    case ANALYSIS_DIAGNOSTIC_MISSING_IMPORTED_MODULE:
        return S8("imported module was not found");
    case ANALYSIS_DIAGNOSTIC_AMBIGUOUS_IMPORTED_MODULE:
        return S8("imported module name is ambiguous");
    case ANALYSIS_DIAGNOSTIC_IMPORT_CYCLE:
        return S8("module import cycle");
    case ANALYSIS_DIAGNOSTIC_UNKNOWN_TYPE:
        return S8("unknown type");
    case ANALYSIS_DIAGNOSTIC_TYPE_ALIAS_CYCLE:
        return S8("type alias cycle");
    case ANALYSIS_DIAGNOSTIC_INVALID_VECTOR_TYPE:
        return S8("vector type must contain 64, 128, 256, or 512 bits of integer or floating-point lanes");
    case ANALYSIS_DIAGNOSTIC_COUNT:
        break;
    }
    return S8("semantic error");
}

BUSTER_GLOBAL_LOCAL void analysis_body_diagnostic_push(AnalysisBodyContext* context, ParserSourceRange range, AnalysisDiagnosticKind kind, String8 subject)
{
    AnalysisDiagnostic* diagnostic = arena_allocate(context->result_arena, AnalysisDiagnostic, 1);
    *diagnostic = (AnalysisDiagnostic){
        .message = analysis_diagnostic_message(kind),
        .range = range,
        .entity = context->owner->id,
        .previous_entity = ANALYSIS_ENTITY_ID_INVALID,
        .source = context->owner->source,
        .kind = kind,
        .subject = subject,
        .expected_type = ANALYSIS_TYPE_ID_INVALID,
        .actual_type = ANALYSIS_TYPE_ID_INVALID,
    };
    analysis_diagnostic_append(context->result, diagnostic);
}

BUSTER_GLOBAL_LOCAL void analysis_mismatch_diagnostic_push(AnalysisBodyContext* context, ParserSourceRange range, AnalysisTypeId expected,
                                                           AnalysisTypeId actual)
{
    AnalysisDiagnostic* diagnostic = arena_allocate(context->result_arena, AnalysisDiagnostic, 1);
    *diagnostic = (AnalysisDiagnostic){
        .message = analysis_diagnostic_message(ANALYSIS_DIAGNOSTIC_TYPE_MISMATCH),
        .range = range,
        .entity = context->owner->id,
        .previous_entity = ANALYSIS_ENTITY_ID_INVALID,
        .source = context->owner->source,
        .kind = ANALYSIS_DIAGNOSTIC_TYPE_MISMATCH,
        .expected_type = expected,
        .actual_type = actual,
        .expected_type_name = analysis_type_from_id(context->result, expected)->name,
        .actual_type_name = analysis_type_from_id(context->result, actual)->name,
    };
    analysis_diagnostic_append(context->result, diagnostic);
}

BUSTER_GLOBAL_LOCAL AnalysisTypeId analysis_body_type_resolve(AnalysisBodyContext* context, AstType* ast)
{
    AnalysisTypeId result = context->result->types.builtin.poison;
    analysis_type_task_ast_push(&context->resolution, context->owner, ast, &result);
    analysis_resolution_run(&context->resolution);
    if (context->instantiation)
    {
        result = analysis_type_substitute(context->result_arena, context->scratch_arena, context->result, result, context->instantiation->type_bindings,
                                          context->instantiation->type_binding_count);
    }
    return result;
}

BUSTER_GLOBAL_LOCAL AnalysisConversionKind analysis_explicit_conversion_kind(AnalysisResult* result, AnalysisTypeId source_id, AnalysisTypeId target_id)
{
    AnalysisType* source = analysis_type_from_id(result, source_id);
    AnalysisType* target = analysis_type_from_id(result, target_id);
    if (source->kind == ANALYSIS_TYPE_INTEGER && target->kind == ANALYSIS_TYPE_INTEGER)
    {
        if (source->as.integer.bit_width < target->as.integer.bit_width)
        {
            return ANALYSIS_CONVERSION_INTEGER_WIDEN;
        }
        if (source->as.integer.bit_width > target->as.integer.bit_width)
        {
            return ANALYSIS_CONVERSION_INTEGER_NARROW;
        }
        return ANALYSIS_CONVERSION_EXPLICIT;
    }
    if (source->kind == ANALYSIS_TYPE_FLOAT && target->kind == ANALYSIS_TYPE_FLOAT)
    {
        if (source->as.float_bit_width < target->as.float_bit_width)
        {
            return ANALYSIS_CONVERSION_FLOAT_WIDEN;
        }
        if (source->as.float_bit_width > target->as.float_bit_width)
        {
            return ANALYSIS_CONVERSION_FLOAT_NARROW;
        }
        return ANALYSIS_CONVERSION_EXPLICIT;
    }
    if (source->kind == ANALYSIS_TYPE_POINTER || target->kind == ANALYSIS_TYPE_POINTER)
    {
        return ANALYSIS_CONVERSION_POINTER;
    }
    return ANALYSIS_CONVERSION_EXPLICIT;
}

BUSTER_GLOBAL_LOCAL bool analysis_type_compatible(AnalysisResult* result, AnalysisTypeId expected, AnalysisTypeId actual)
{
    AnalysisTypeId poison = result->types.builtin.poison;
    if (analysis_type_id_equal(expected, poison) || analysis_type_id_equal(actual, poison))
    {
        return true;
    }
    if (analysis_type_id_equal(expected, actual))
    {
        return true;
    }
    AnalysisType* expected_type = analysis_type_from_id(result, expected);
    AnalysisType* actual_type = analysis_type_from_id(result, actual);
    bool expected_array = expected_type->kind == ANALYSIS_TYPE_ARRAY || expected_type->kind == ANALYSIS_TYPE_INFERRED_ARRAY;
    bool actual_array = actual_type->kind == ANALYSIS_TYPE_ARRAY || actual_type->kind == ANALYSIS_TYPE_INFERRED_ARRAY;
    if (expected_array && actual_array)
    {
        AnalysisTypeId expected_element = expected_type->kind == ANALYSIS_TYPE_ARRAY ? expected_type->as.array.element_type : expected_type->as.element_type;
        AnalysisTypeId actual_element = actual_type->kind == ANALYSIS_TYPE_ARRAY ? actual_type->as.array.element_type : actual_type->as.element_type;
        if (!analysis_type_id_equal(expected_element, actual_element))
        {
            return false;
        }
        if (expected_type->kind == ANALYSIS_TYPE_ARRAY && actual_type->kind == ANALYSIS_TYPE_ARRAY)
        {
            return expected_type->as.array.count == actual_type->as.array.count;
        }
        return true;
    }
    return false;
}

BUSTER_GLOBAL_LOCAL bool analysis_type_is_numeric(AnalysisResult* result, AnalysisTypeId id)
{
    AnalysisType* type = analysis_type_from_id(result, id);
    if (type->kind == ANALYSIS_TYPE_VECTOR)
    {
        type = analysis_type_from_id(result, type->as.vector.element_type);
    }
    return type->kind == ANALYSIS_TYPE_INTEGER || type->kind == ANALYSIS_TYPE_FLOAT;
}

BUSTER_GLOBAL_LOCAL bool analysis_type_is_integer(AnalysisResult* result, AnalysisTypeId id)
{
    AnalysisType* type = analysis_type_from_id(result, id);
    if (type->kind == ANALYSIS_TYPE_VECTOR)
    {
        type = analysis_type_from_id(result, type->as.vector.element_type);
    }
    return type->kind == ANALYSIS_TYPE_INTEGER;
}

BUSTER_GLOBAL_LOCAL AnalysisTypeId analysis_vector_mask_type(Arena* arena, AnalysisResult* result, AnalysisType* vector)
{
    AnalysisType* element = analysis_type_from_id(result, vector->as.vector.element_type);
    u32 width = element->kind == ANALYSIS_TYPE_FLOAT ? element->as.float_bit_width : element->kind == ANALYSIS_TYPE_INTEGER ? element->as.integer.bit_width : 0;
    AnalysisTypeId mask_element = width == 8    ? result->types.builtin.u8_type
                                  : width == 16 ? result->types.builtin.u16_type
                                  : width == 32 ? result->types.builtin.u32_type
                                  : width == 64 ? result->types.builtin.u64_type
                                                : result->types.builtin.poison;
    if (analysis_type_id_equal(mask_element, result->types.builtin.poison))
    {
        return mask_element;
    }
    return analysis_type_intern(arena, &result->types,
                                (AnalysisType){
                                    .kind = ANALYSIS_TYPE_VECTOR,
                                    .as.vector =
                                        {
                                            .element_type = mask_element,
                                            .count = vector->as.vector.count,
                                        },
                                });
}

BUSTER_GLOBAL_LOCAL bool analysis_type_has_compile_time_parameter(Arena* scratch_arena, AnalysisResult* result, AnalysisTypeId root)
{
    typedef struct AnalysisTypeLink AnalysisTypeLink;
    struct AnalysisTypeLink
    {
        AnalysisTypeLink* previous;
        AnalysisTypeId type;
    };
    AnalysisTypeLink* top = arena_allocate(scratch_arena, AnalysisTypeLink, 1);
    *top = (AnalysisTypeLink){.type = root};
    while (top)
    {
        AnalysisTypeId id = top->type;
        top = top->previous;
        AnalysisType* type = analysis_type_from_id(result, id);
        if (type->kind == ANALYSIS_TYPE_COMPILE_TIME_PARAMETER)
        {
            return true;
        }
        if (type->kind == ANALYSIS_TYPE_POINTER || type->kind == ANALYSIS_TYPE_SLICE || type->kind == ANALYSIS_TYPE_INFERRED_ARRAY ||
            type->kind == ANALYSIS_TYPE_RANGE)
        {
            AnalysisTypeLink* link = arena_allocate(scratch_arena, AnalysisTypeLink, 1);
            *link = (AnalysisTypeLink){.previous = top, .type = type->as.element_type};
            top = link;
        }
        else if (type->kind == ANALYSIS_TYPE_ARRAY)
        {
            AnalysisTypeLink* link = arena_allocate(scratch_arena, AnalysisTypeLink, 1);
            *link = (AnalysisTypeLink){.previous = top, .type = type->as.array.element_type};
            top = link;
        }
        else if (type->kind == ANALYSIS_TYPE_VECTOR)
        {
            AnalysisTypeLink* link = arena_allocate(scratch_arena, AnalysisTypeLink, 1);
            *link = (AnalysisTypeLink){
                .previous = top,
                .type = type->as.vector.element_type,
            };
            top = link;
        }
        else if (type->kind == ANALYSIS_TYPE_FUNCTION)
        {
            AnalysisTypeLink* link = arena_allocate(scratch_arena, AnalysisTypeLink, 1);
            *link = (AnalysisTypeLink){
                .previous = top,
                .type = type->as.function.return_type,
            };
            top = link;
            for (u32 index = 0; index < type->as.function.argument_count; index += 1)
            {
                link = arena_allocate(scratch_arena, AnalysisTypeLink, 1);
                *link = (AnalysisTypeLink){
                    .previous = top,
                    .type = type->as.function.argument_types[index],
                };
                top = link;
            }
        }
    }
    return false;
}

bool analysis_entity_is_generic(Arena* scratch_arena, AnalysisResult* result, AnalysisEntity* entity)
{
    if (!entity || entity->kind != ANALYSIS_ENTITY_CODE)
    {
        return false;
    }
    for (AstTypeArgument* argument = entity->ast.code->type->function.first_argument; argument; argument = argument->next)
    {
        if (argument->is_compile_time)
        {
            return true;
        }
    }
    AnalysisTypeId type = result->module.semantics[entity->id.index.value].type;
    return analysis_type_has_compile_time_parameter(scratch_arena, result, type);
}

BUSTER_GLOBAL_LOCAL bool analysis_generic_type_infer(Arena* scratch_arena, AnalysisResult* result, AnalysisTypeId pattern_root, AnalysisTypeId actual_root,
                                                     AnalysisGenericTypeBinding* bindings, u32* binding_count, u32 binding_capacity)
{
    typedef struct AnalysisTypePair AnalysisTypePair;
    struct AnalysisTypePair
    {
        AnalysisTypePair* previous;
        AnalysisTypeId pattern;
        AnalysisTypeId actual;
    };
    AnalysisTypePair* top = arena_allocate(scratch_arena, AnalysisTypePair, 1);
    *top = (AnalysisTypePair){.pattern = pattern_root, .actual = actual_root};
    while (top)
    {
        AnalysisTypePair pair = *top;
        top = top->previous;
        AnalysisType* pattern = analysis_type_from_id(result, pair.pattern);
        AnalysisType* actual = analysis_type_from_id(result, pair.actual);
        if (pattern->kind == ANALYSIS_TYPE_COMPILE_TIME_PARAMETER)
        {
            AnalysisTypeId existing = analysis_generic_binding_find(bindings, *binding_count, pattern->name);
            if (existing.value != ANALYSIS_ID_UNDERLYING_INVALID)
            {
                if (!analysis_type_id_equal(existing, pair.actual))
                {
                    return false;
                }
            }
            else
            {
                BUSTER_CHECK(*binding_count < binding_capacity);
                bindings[*binding_count] = (AnalysisGenericTypeBinding){
                    .name = pattern->name,
                    .type = pair.actual,
                };
                *binding_count += 1;
            }
            continue;
        }
        if (pattern->kind != actual->kind)
        {
            return false;
        }
        if (pattern->kind == ANALYSIS_TYPE_POINTER || pattern->kind == ANALYSIS_TYPE_SLICE || pattern->kind == ANALYSIS_TYPE_INFERRED_ARRAY ||
            pattern->kind == ANALYSIS_TYPE_RANGE)
        {
            AnalysisTypePair* pushed = arena_allocate(scratch_arena, AnalysisTypePair, 1);
            *pushed = (AnalysisTypePair){
                .previous = top,
                .pattern = pattern->as.element_type,
                .actual = actual->as.element_type,
            };
            top = pushed;
        }
        else if (pattern->kind == ANALYSIS_TYPE_ARRAY)
        {
            if (pattern->as.array.count != actual->as.array.count)
            {
                return false;
            }
            AnalysisTypePair* pushed = arena_allocate(scratch_arena, AnalysisTypePair, 1);
            *pushed = (AnalysisTypePair){
                .previous = top,
                .pattern = pattern->as.array.element_type,
                .actual = actual->as.array.element_type,
            };
            top = pushed;
        }
        else if (pattern->kind == ANALYSIS_TYPE_VECTOR)
        {
            if (pattern->as.vector.count != actual->as.vector.count)
            {
                return false;
            }
            AnalysisTypePair* pushed = arena_allocate(scratch_arena, AnalysisTypePair, 1);
            *pushed = (AnalysisTypePair){
                .previous = top,
                .pattern = pattern->as.vector.element_type,
                .actual = actual->as.vector.element_type,
            };
            top = pushed;
        }
        else if (pattern->kind == ANALYSIS_TYPE_FUNCTION)
        {
            if (pattern->as.function.argument_count != actual->as.function.argument_count ||
                pattern->as.function.is_variadic != actual->as.function.is_variadic ||
                pattern->as.function.calling_convention != actual->as.function.calling_convention)
            {
                return false;
            }
            AnalysisTypePair* pushed = arena_allocate(scratch_arena, AnalysisTypePair, 1);
            *pushed = (AnalysisTypePair){
                .previous = top,
                .pattern = pattern->as.function.return_type,
                .actual = actual->as.function.return_type,
            };
            top = pushed;
            for (u32 index = 0; index < pattern->as.function.argument_count; index += 1)
            {
                pushed = arena_allocate(scratch_arena, AnalysisTypePair, 1);
                *pushed = (AnalysisTypePair){
                    .previous = top,
                    .pattern = pattern->as.function.argument_types[index],
                    .actual = actual->as.function.argument_types[index],
                };
                top = pushed;
            }
        }
        else if (!analysis_type_id_equal(pair.pattern, pair.actual))
        {
            return false;
        }
    }
    return true;
}

BUSTER_GLOBAL_LOCAL AnalysisEntity* analysis_value_entity_find(AnalysisResult* result, String8 name)
{
    for (u32 index = 0; index < result->module.entity_count; index += 1)
    {
        AnalysisEntity* entity = result->module.entities + index;
        if (entity->name_space == ANALYSIS_NAMESPACE_VALUE && string_equal(entity->name, name))
        {
            return entity;
        }
    }
    return 0;
}

BUSTER_GLOBAL_LOCAL AnalysisBinding* analysis_binding_find(AnalysisBinding* binding, String8 name)
{
    for (; binding; binding = binding->previous)
    {
        if (string_equal(binding->name, name))
        {
            return binding;
        }
    }
    return 0;
}

BUSTER_GLOBAL_LOCAL void analysis_body_dependency_add(AnalysisBodyContext* context, AnalysisEntityId entity)
{
    for (u32 index = 0; index < context->body->dependency_count; index += 1)
    {
        AnalysisEntityId existing = context->body->dependencies[index];
        if (existing.module.value == entity.module.value && existing.index.value == entity.index.value)
        {
            return;
        }
    }
    BUSTER_CHECK(context->body->dependency_count < context->body->dependency_capacity);
    context->body->dependencies[context->body->dependency_count] = entity;
    context->body->dependency_count += 1;
}

BUSTER_GLOBAL_LOCAL AnalysisLocal* analysis_local_add(AnalysisBodyContext* context, AnalysisBinding** bindings, AstIdentifier identifier, AnalysisTypeId type,
                                                      AnalysisLocalKind kind, u32 scope_depth, bool is_compile_time)
{
    for (AnalysisBinding* binding = *bindings; binding && binding->scope_depth == scope_depth; binding = binding->previous)
    {
        if (string_equal(binding->name, identifier.text))
        {
            if (kind != ANALYSIS_LOCAL_ARGUMENT)
            {
                analysis_body_diagnostic_push(context, identifier.range, ANALYSIS_DIAGNOSTIC_DUPLICATE_LOCAL, identifier.text);
            }
            break;
        }
    }
    BUSTER_CHECK(context->body->local_count < context->body->local_capacity);
    AnalysisLocalId id = {.value = context->body->local_count};
    AnalysisLocal* local = context->body->locals + context->body->local_count;
    *local = (AnalysisLocal){
        .name = identifier.text,
        .range = identifier.range,
        .type = type,
        .id = id,
        .kind = kind,
        .scope_depth = scope_depth,
        .is_mutable = !is_compile_time,
        .is_initialized = true,
        .is_compile_time = is_compile_time,
    };
    context->body->local_count += 1;
    AnalysisBinding* binding = arena_allocate(context->scratch_arena, AnalysisBinding, 1);
    *binding = (AnalysisBinding){
        .previous = *bindings,
        .name = identifier.text,
        .local = id,
        .scope_depth = scope_depth,
    };
    *bindings = binding;
    return local;
}

BUSTER_GLOBAL_LOCAL ParserSourceRange analysis_node_range(AnalysisEntity* owner, AstNode* node)
{
    switch (node->id)
    {
    case AST_NODE_IDENTIFIER:
        return node->identifier.range;
    case AST_NODE_ARRAY_LITERAL:
        return node->array_literal.range;
    case AST_NODE_ARRAY_INDEX:
        return node->array_index.range;
    case AST_NODE_ARRAY_SLICE:
        return node->array_slice.range;
    case AST_NODE_AGGREGATE_LITERAL:
        return node->aggregate_literal.range;
    case AST_NODE_MEMBER_ACCESS:
        return node->member_access.range;
    case AST_NODE_ENUM_LITERAL:
        return node->enum_literal.range;
    case AST_NODE_CALL:
        return node->call.range;
    case AST_NODE_INTRINSIC_CALL:
        return node->intrinsic_call.range;
    case AST_NODE_ADDRESS_OF:
    case AST_NODE_DEREFERENCE:
        return node->pointer_operator.range;
    case AST_NODE_CONSTANT_INTEGER:
    case AST_NODE_CONSTANT_FLOAT:
    case AST_NODE_CONSTANT_CHARACTER:
    case AST_NODE_CONSTANT_STRING:
    case AST_NODE_UNDEFINED:
    case AST_NODE_UNARY_MINUS:
    case AST_NODE_UNARY_PLUS:
    case AST_NODE_UNARY_LOGICAL_NOT:
    case AST_NODE_UNARY_BITWISE_NOT:
    case AST_NODE_BINARY_PLUS:
    case AST_NODE_BINARY_MINUS:
    case AST_NODE_BINARY_ASTERISK:
    case AST_NODE_BINARY_SLASH:
    case AST_NODE_BINARY_PERCENT:
    case AST_NODE_BINARY_SHIFT_LEFT:
    case AST_NODE_BINARY_SHIFT_RIGHT:
    case AST_NODE_BINARY_EQUAL:
    case AST_NODE_BINARY_NOT_EQUAL:
    case AST_NODE_BINARY_LESS:
    case AST_NODE_BINARY_LESS_EQUAL:
    case AST_NODE_BINARY_GREATER:
    case AST_NODE_BINARY_GREATER_EQUAL:
    case AST_NODE_BINARY_AMPERSAND:
    case AST_NODE_BINARY_BAR:
    case AST_NODE_BINARY_CARET:
    case AST_NODE_BINARY_BOOLEAN_AND:
    case AST_NODE_BINARY_BOOLEAN_OR:
    case AST_NODE_BINARY_BOOLEAN_AND_SHORT_CIRCUIT:
    case AST_NODE_BINARY_BOOLEAN_OR_SHORT_CIRCUIT:
    case AST_NODE_BINARY_RANGE:
    case AST_NODE_COUNT:
        break;
    }
    return owner->range;
}

BUSTER_GLOBAL_LOCAL u32 analysis_node_arity(AstNode* node)
{
    switch (node->id)
    {
    case AST_NODE_CONSTANT_INTEGER:
    case AST_NODE_CONSTANT_FLOAT:
    case AST_NODE_CONSTANT_CHARACTER:
    case AST_NODE_CONSTANT_STRING:
    case AST_NODE_IDENTIFIER:
    case AST_NODE_UNDEFINED:
    case AST_NODE_ENUM_LITERAL:
        return 0;
    case AST_NODE_ARRAY_LITERAL:
        return node->array_literal.element_count;
    case AST_NODE_ARRAY_INDEX:
        return 2;
    case AST_NODE_ARRAY_SLICE:
    {
        return 1 + (u32)node->array_slice.has_start + (u32)node->array_slice.has_end;
    }
    case AST_NODE_AGGREGATE_LITERAL:
        return node->aggregate_literal.field_count;
    case AST_NODE_CALL:
        return 1 + node->call.argument_count;
    case AST_NODE_INTRINSIC_CALL:
        return node->intrinsic_call.argument_count;
    case AST_NODE_MEMBER_ACCESS:
    case AST_NODE_UNARY_MINUS:
    case AST_NODE_UNARY_PLUS:
    case AST_NODE_UNARY_LOGICAL_NOT:
    case AST_NODE_UNARY_BITWISE_NOT:
    case AST_NODE_ADDRESS_OF:
    case AST_NODE_DEREFERENCE:
        return 1;
    case AST_NODE_BINARY_PLUS:
    case AST_NODE_BINARY_MINUS:
    case AST_NODE_BINARY_ASTERISK:
    case AST_NODE_BINARY_SLASH:
    case AST_NODE_BINARY_PERCENT:
    case AST_NODE_BINARY_SHIFT_LEFT:
    case AST_NODE_BINARY_SHIFT_RIGHT:
    case AST_NODE_BINARY_EQUAL:
    case AST_NODE_BINARY_NOT_EQUAL:
    case AST_NODE_BINARY_LESS:
    case AST_NODE_BINARY_LESS_EQUAL:
    case AST_NODE_BINARY_GREATER:
    case AST_NODE_BINARY_GREATER_EQUAL:
    case AST_NODE_BINARY_AMPERSAND:
    case AST_NODE_BINARY_BAR:
    case AST_NODE_BINARY_CARET:
    case AST_NODE_BINARY_BOOLEAN_AND:
    case AST_NODE_BINARY_BOOLEAN_OR:
    case AST_NODE_BINARY_BOOLEAN_AND_SHORT_CIRCUIT:
    case AST_NODE_BINARY_BOOLEAN_OR_SHORT_CIRCUIT:
    case AST_NODE_BINARY_RANGE:
        return 2;
    case AST_NODE_COUNT:
        break;
    }
    return 0;
}

BUSTER_GLOBAL_LOCAL bool analysis_constant_integer_fits(AnalysisResult* result, AnalysisConstant constant, AnalysisTypeId type_id)
{
    AnalysisType* type = analysis_type_from_id(result, type_id);
    if (constant.kind != ANALYSIS_CONSTANT_INTEGER || type->kind != ANALYSIS_TYPE_INTEGER)
    {
        return true;
    }
    u32 width = type->as.integer.bit_width;
    if (!type->as.integer.is_signed)
    {
        if (constant.is_negative)
        {
            return false;
        }
        return width == 64 || constant.integer < ((u64)1 << width);
    }
    u64 limit = (u64)1 << (width - 1);
    return constant.is_negative ? constant.integer <= limit : constant.integer < limit;
}

BUSTER_GLOBAL_LOCAL bool analysis_constant_fits(AnalysisResult* result, AnalysisConstant constant, AnalysisTypeId type_id)
{
    if (!analysis_constant_integer_fits(result, constant, type_id))
    {
        return false;
    }
    AnalysisType* type = analysis_type_from_id(result, type_id);
    if (constant.kind == ANALYSIS_CONSTANT_FLOAT && type->kind == ANALYSIS_TYPE_FLOAT)
    {
        if (constant.floating != constant.floating)
        {
            return false;
        }
        f64 magnitude = constant.floating < 0.0 ? -constant.floating : constant.floating;
        return type->as.float_bit_width == 64 || magnitude <= 3.4028234663852886e38;
    }
    return true;
}

BUSTER_GLOBAL_LOCAL AnalysisConstant analysis_constant_cast(AnalysisResult* result, AnalysisConstant constant, AnalysisTypeId target_id)
{
    AnalysisType* target = analysis_type_from_id(result, target_id);
    if (constant.kind == ANALYSIS_CONSTANT_INTEGER && target->kind == ANALYSIS_TYPE_FLOAT)
    {
        f64 value = (f64)constant.integer;
        return (AnalysisConstant){
            .floating = constant.is_negative ? -value : value,
            .kind = ANALYSIS_CONSTANT_FLOAT,
        };
    }
    if (constant.kind == ANALYSIS_CONSTANT_FLOAT && target->kind == ANALYSIS_TYPE_INTEGER)
    {
        f64 minimum = target->as.integer.is_signed
                          ? (target->as.integer.bit_width == 64 ? -9223372036854775808.0 : -(f64)((u64)1 << (target->as.integer.bit_width - 1)))
                          : 0.0;
        f64 maximum = target->as.integer.is_signed
                          ? (target->as.integer.bit_width == 64 ? 9223372036854775808.0 : (f64)(((u64)1 << (target->as.integer.bit_width - 1)) - 1))
                          : (target->as.integer.bit_width == 64 ? 18446744073709551616.0 : (f64)(((u64)1 << target->as.integer.bit_width) - 1));
        if (constant.floating != constant.floating || constant.floating < minimum ||
            (target->as.integer.bit_width == 64 ? constant.floating >= maximum : constant.floating > maximum))
        {
            return (AnalysisConstant){0};
        }
        if (constant.floating < 0.0)
        {
            s64 signed_value = (s64)constant.floating;
            return (AnalysisConstant){
                .integer = (u64)(-(signed_value + 1)) + 1,
                .kind = ANALYSIS_CONSTANT_INTEGER,
                .is_negative = true,
            };
        }
        return (AnalysisConstant){
            .integer = (u64)constant.floating,
            .kind = ANALYSIS_CONSTANT_INTEGER,
        };
    }
    return constant;
}

BUSTER_GLOBAL_LOCAL bool analysis_float_literal_parse(AstFloatLiteral literal, f64* value_out)
{
    String8 spelling = literal.spelling;
    u64 index = literal.base == 16 ? 2 : 0;
    f64 value = 0.0;
    f64 fraction_scale = 1.0;
    bool fraction = false;
    bool saw_digit = false;
    while (index < spelling.length)
    {
        u8 byte = spelling.pointer[index];
        if (byte == '.')
        {
            if (fraction)
            {
                return false;
            }
            fraction = true;
            index += 1;
            continue;
        }
        if (byte == 'e' || byte == 'E' || byte == 'p' || byte == 'P')
        {
            break;
        }
        u32 digit = byte >= '0' && byte <= '9'   ? (u32)(byte - '0')
                    : byte >= 'a' && byte <= 'f' ? (u32)(byte - 'a') + 10
                    : byte >= 'A' && byte <= 'F' ? (u32)(byte - 'A') + 10
                                                 : UINT32_MAX;
        if (digit >= literal.base)
        {
            return false;
        }
        saw_digit = true;
        if (fraction)
        {
            fraction_scale /= (f64)literal.base;
            value += (f64)digit * fraction_scale;
        }
        else
        {
            value = value * (f64)literal.base + (f64)digit;
        }
        index += 1;
    }
    if (!saw_digit)
    {
        return false;
    }
    if (index < spelling.length)
    {
        index += 1;
        bool negative = false;
        if (index < spelling.length && (spelling.pointer[index] == '+' || spelling.pointer[index] == '-'))
        {
            negative = spelling.pointer[index] == '-';
            index += 1;
        }
        u32 exponent = 0;
        bool saw_exponent = false;
        while (index < spelling.length)
        {
            u8 byte = spelling.pointer[index++];
            if (byte < '0' || byte > '9' || exponent > 100000)
            {
                return false;
            }
            saw_exponent = true;
            exponent = exponent * 10 + (u32)(byte - '0');
        }
        if (!saw_exponent)
        {
            return false;
        }
        f64 factor = 1.0;
        f64 base = literal.base == 16 ? 2.0 : 10.0;
        f64 power = base;
        while (exponent)
        {
            if (exponent & 1)
            {
                factor *= power;
            }
            exponent >>= 1;
            if (exponent)
            {
                power *= power;
            }
        }
        value = negative ? value / factor : value * factor;
    }
    *value_out = value;
    return true;
}

BUSTER_GLOBAL_LOCAL AnalysisConstant analysis_constant_float_binary(AstNodeId operation, AnalysisConstant left, AnalysisConstant right)
{
    AnalysisConstant result = {0};
    if (left.kind != ANALYSIS_CONSTANT_FLOAT || right.kind != ANALYSIS_CONSTANT_FLOAT)
    {
        return result;
    }
    f64 value = 0.0;
    switch (operation)
    {
    case AST_NODE_BINARY_PLUS:
        value = left.floating + right.floating;
        break;
    case AST_NODE_BINARY_MINUS:
        value = left.floating - right.floating;
        break;
    case AST_NODE_BINARY_ASTERISK:
        value = left.floating * right.floating;
        break;
    case AST_NODE_BINARY_SLASH:
    {
        if (right.floating == 0.0)
        {
            return result;
        }
        value = left.floating / right.floating;
    }
    break;
    default:
        return result;
    }
    result.kind = ANALYSIS_CONSTANT_FLOAT;
    result.floating = value;
    return result;
}

BUSTER_GLOBAL_LOCAL AnalysisConstant analysis_constant_integer_binary(AstNodeId operation, AnalysisConstant left, AnalysisConstant right)
{
    AnalysisConstant result = {0};
    if (left.kind != ANALYSIS_CONSTANT_INTEGER || right.kind != ANALYSIS_CONSTANT_INTEGER || left.integer > (u64)INT64_MAX || right.integer > (u64)INT64_MAX)
    {
        return result;
    }
    s64 left_value = left.is_negative ? -(s64)left.integer : (s64)left.integer;
    s64 right_value = right.is_negative ? -(s64)right.integer : (s64)right.integer;
    s64 value = 0;
    bool known = true;
    switch (operation)
    {
    case AST_NODE_BINARY_PLUS:
    {
        known = right_value >= 0 ? left_value <= INT64_MAX - right_value : left_value >= INT64_MIN - right_value;
        if (known)
            value = left_value + right_value;
    }
    break;
    case AST_NODE_BINARY_MINUS:
    {
        known = right_value >= 0 ? left_value >= INT64_MIN + right_value : left_value <= INT64_MAX + right_value;
        if (known)
            value = left_value - right_value;
    }
    break;
    case AST_NODE_BINARY_ASTERISK:
    {
        if (!left_value || !right_value)
        {
            value = 0;
        }
        else if ((left_value == INT64_MIN && right_value == -1) || (right_value == INT64_MIN && left_value == -1))
        {
            known = false;
        }
        else
        {
            value = left_value * right_value;
            known = value / right_value == left_value;
        }
    }
    break;
    case AST_NODE_BINARY_SLASH:
    {
        known = right_value != 0 && !(left_value == INT64_MIN && right_value == -1);
        if (known)
            value = left_value / right_value;
    }
    break;
    case AST_NODE_BINARY_PERCENT:
    {
        known = right_value != 0 && !(left_value == INT64_MIN && right_value == -1);
        if (known)
            value = left_value % right_value;
    }
    break;
    case AST_NODE_BINARY_SHIFT_LEFT:
    {
        known = right_value >= 0 && right_value < 64;
        if (known)
            value = (s64)((u64)left_value << (u32)right_value);
    }
    break;
    case AST_NODE_BINARY_SHIFT_RIGHT:
    {
        known = right_value >= 0 && right_value < 64;
        if (known)
            value = left_value >> (u32)right_value;
    }
    break;
    case AST_NODE_BINARY_AMPERSAND:
        value = left_value & right_value;
        break;
    case AST_NODE_BINARY_BAR:
        value = left_value | right_value;
        break;
    case AST_NODE_BINARY_CARET:
        value = left_value ^ right_value;
        break;
    case AST_NODE_CONSTANT_INTEGER:
    case AST_NODE_CONSTANT_FLOAT:
    case AST_NODE_CONSTANT_CHARACTER:
    case AST_NODE_CONSTANT_STRING:
    case AST_NODE_IDENTIFIER:
    case AST_NODE_UNDEFINED:
    case AST_NODE_ARRAY_LITERAL:
    case AST_NODE_ARRAY_INDEX:
    case AST_NODE_ARRAY_SLICE:
    case AST_NODE_AGGREGATE_LITERAL:
    case AST_NODE_MEMBER_ACCESS:
    case AST_NODE_ENUM_LITERAL:
    case AST_NODE_CALL:
    case AST_NODE_INTRINSIC_CALL:
    case AST_NODE_UNARY_MINUS:
    case AST_NODE_UNARY_PLUS:
    case AST_NODE_UNARY_LOGICAL_NOT:
    case AST_NODE_UNARY_BITWISE_NOT:
    case AST_NODE_ADDRESS_OF:
    case AST_NODE_DEREFERENCE:
    case AST_NODE_BINARY_EQUAL:
    case AST_NODE_BINARY_NOT_EQUAL:
    case AST_NODE_BINARY_LESS:
    case AST_NODE_BINARY_LESS_EQUAL:
    case AST_NODE_BINARY_GREATER:
    case AST_NODE_BINARY_GREATER_EQUAL:
    case AST_NODE_BINARY_BOOLEAN_AND:
    case AST_NODE_BINARY_BOOLEAN_OR:
    case AST_NODE_BINARY_BOOLEAN_AND_SHORT_CIRCUIT:
    case AST_NODE_BINARY_BOOLEAN_OR_SHORT_CIRCUIT:
    case AST_NODE_BINARY_RANGE:
    case AST_NODE_COUNT:
        known = false;
        break;
    }
    if (known)
    {
        result.kind = ANALYSIS_CONSTANT_INTEGER;
        result.is_negative = value < 0;
        result.integer = value < 0 ? (u64)(-(value + 1)) + 1 : (u64)value;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL void analysis_expression_expect(AnalysisBodyContext* context, AnalysisTypedExpression* expression, u32 node_index, AnalysisTypeId expected)
{
    typedef struct AnalysisExpectation AnalysisExpectation;
    struct AnalysisExpectation
    {
        AnalysisTypeId type;
        u32 node_index;
    };
    AnalysisExpectation* top = arena_allocate(context->scratch_arena, AnalysisExpectation, expression->ast.count);
    u32 count = 1;
    bool failed = false;
    top[0] = (AnalysisExpectation){.type = expected, .node_index = node_index};
    while (count)
    {
        AnalysisExpectation expectation = top[count - 1];
        count -= 1;
        u32 current_index = expectation.node_index;
        AnalysisTypeId current_expected = expectation.type;
        AnalysisTypeKind expected_kind = analysis_type_from_id(context->result, current_expected)->kind;
        AnalysisTypedNode* current = expression->nodes + current_index;
        AstNode* node = expression->ast.nodes + current_index;
        if (!analysis_constant_fits(context->result, current->constant, current_expected))
        {
            analysis_body_diagnostic_push(context, analysis_node_range(context->owner, node), ANALYSIS_DIAGNOSTIC_INVALID_CONSTANT, (String8){0});
        }
        bool integer_literal = node->id == AST_NODE_CONSTANT_INTEGER || node->id == AST_NODE_CONSTANT_CHARACTER;
        if ((integer_literal && expected_kind == ANALYSIS_TYPE_INTEGER) || (node->id == AST_NODE_CONSTANT_FLOAT && expected_kind == ANALYSIS_TYPE_FLOAT) ||
            node->id == AST_NODE_UNDEFINED || (node->id == AST_NODE_INTRINSIC_CALL && string_equal(node->intrinsic_call.name.text, S8("cast"))))
        {
            current->type = current_expected;
            if (node->id == AST_NODE_UNDEFINED)
            {
                current->conversion = ANALYSIS_CONVERSION_UNDEFINED;
            }
            else if (node->id == AST_NODE_INTRINSIC_CALL)
            {
                AnalysisTypeId source_type = current_index ? expression->nodes[current_index - 1].type : context->result->types.builtin.poison;
                current->conversion = analysis_explicit_conversion_kind(context->result, source_type, current_expected);
                current->constant = analysis_constant_cast(context->result, current->constant, current_expected);
                if (expression->nodes[current_index - 1].constant.kind != ANALYSIS_CONSTANT_NONE && current->constant.kind == ANALYSIS_CONSTANT_NONE)
                {
                    analysis_body_diagnostic_push(context, analysis_node_range(context->owner, node), ANALYSIS_DIAGNOSTIC_INVALID_CONSTANT, (String8){0});
                }
            }
            else
            {
                current->conversion = ANALYSIS_CONVERSION_LITERAL;
            }
            continue;
        }
        if (node->id == AST_NODE_ENUM_LITERAL)
        {
            if (expected_kind != ANALYSIS_TYPE_ENUM)
            {
                failed = true;
                continue;
            }
            current->type = current_expected;
            AnalysisType* enum_type = analysis_type_from_id(context->result, current_expected);
            AnalysisEntitySemantic* semantic = analysis_declaration_semantic(context->result, enum_type->as.declaration, 0);
            BUSTER_CHECK(semantic);
            bool found = false;
            for (u32 member_index = 0; member_index < semantic->enum_member_count; member_index += 1)
            {
                if (string_equal(semantic->enum_members[member_index].name, node->enum_literal.member.text))
                {
                    found = true;
                    current->constant = (AnalysisConstant){
                        .integer = semantic->enum_members[member_index].value,
                        .kind = ANALYSIS_CONSTANT_ENUM,
                    };
                    break;
                }
            }
            if (!found)
            {
                analysis_body_diagnostic_push(context, node->enum_literal.member.range, ANALYSIS_DIAGNOSTIC_UNKNOWN_MEMBER, node->enum_literal.member.text);
            }
            continue;
        }
        if (node->id == AST_NODE_AGGREGATE_LITERAL)
        {
            AnalysisType* aggregate = analysis_type_from_id(context->result, current_expected);
            if (aggregate->kind != ANALYSIS_TYPE_STRUCT && aggregate->kind != ANALYSIS_TYPE_UNION)
            {
                failed = true;
                continue;
            }
            current->type = current_expected;
            AnalysisResult* declaration_module = 0;
            AnalysisEntitySemantic* semantic = analysis_declaration_semantic(context->result, aggregate->as.declaration, &declaration_module);
            BUSTER_CHECK(semantic);
            bool* seen = arena_allocate(context->scratch_arena, bool, semantic->field_count);
            for (u32 field_index = 0; field_index < semantic->field_count; field_index += 1)
            {
                seen[field_index] = false;
            }
            u32 arity = node->aggregate_literal.field_count;
            u32* roots = arena_allocate(context->scratch_arena, u32, arity);
            u32 cursor = current_index;
            for (u32 child = arity; child > 0; child -= 1)
            {
                cursor -= 1;
                roots[child - 1] = cursor;
                cursor = expression->nodes[cursor].subtree_start;
            }
            u32 child = 0;
            for (AstAggregateLiteralField* literal_field = node->aggregate_literal.first_field; literal_field; literal_field = literal_field->next, child += 1)
            {
                AnalysisField* field = 0;
                u32 found_index = 0;
                for (u32 field_index = 0; field_index < semantic->field_count; field_index += 1)
                {
                    if (string_equal(semantic->fields[field_index].name, literal_field->name.text))
                    {
                        field = semantic->fields + field_index;
                        found_index = field_index;
                        break;
                    }
                }
                if (!field)
                {
                    analysis_body_diagnostic_push(context, literal_field->name.range, ANALYSIS_DIAGNOSTIC_UNKNOWN_MEMBER, literal_field->name.text);
                }
                else
                {
                    if (seen[found_index])
                    {
                        analysis_body_diagnostic_push(context, literal_field->name.range, ANALYSIS_DIAGNOSTIC_DUPLICATE_AGGREGATE_FIELD,
                                                      literal_field->name.text);
                    }
                    seen[found_index] = true;
                    AnalysisTypeId field_type =
                        analysis_type_import(context->result_arena, context->scratch_arena, context->result, declaration_module, field->type);
                    BUSTER_CHECK(count < expression->ast.count);
                    top[count] = (AnalysisExpectation){
                        .type = field_type,
                        .node_index = roots[child],
                    };
                    count += 1;
                }
            }
            if (aggregate->kind == ANALYSIS_TYPE_STRUCT)
            {
                for (u32 field_index = 0; field_index < semantic->field_count; field_index += 1)
                {
                    if (!seen[field_index])
                    {
                        analysis_body_diagnostic_push(context, node->aggregate_literal.range, ANALYSIS_DIAGNOSTIC_MISSING_AGGREGATE_FIELD,
                                                      semantic->fields[field_index].name);
                    }
                }
            }
            else if (arity != 1)
            {
                analysis_body_diagnostic_push(context, node->aggregate_literal.range, ANALYSIS_DIAGNOSTIC_INVALID_OPERAND, (String8){0});
            }
            continue;
        }
        if (node->id == AST_NODE_ARRAY_LITERAL &&
            (expected_kind == ANALYSIS_TYPE_ARRAY || expected_kind == ANALYSIS_TYPE_INFERRED_ARRAY || expected_kind == ANALYSIS_TYPE_VECTOR))
        {
            AnalysisType* array = analysis_type_from_id(context->result, current_expected);
            AnalysisTypeId element = expected_kind == ANALYSIS_TYPE_ARRAY    ? array->as.array.element_type
                                     : expected_kind == ANALYSIS_TYPE_VECTOR ? array->as.vector.element_type
                                                                             : array->as.element_type;
            u64 expected_count = expected_kind == ANALYSIS_TYPE_ARRAY    ? array->as.array.count
                                 : expected_kind == ANALYSIS_TYPE_VECTOR ? array->as.vector.count
                                                                         : node->array_literal.element_count;
            if (expected_count != node->array_literal.element_count)
            {
                failed = true;
            }
            current->type = current_expected;
            u32 cursor = current_index;
            for (u32 child = node->array_literal.element_count; child > 0; child -= 1)
            {
                cursor -= 1;
                BUSTER_CHECK(count < expression->ast.count);
                top[count] = (AnalysisExpectation){.type = element, .node_index = cursor};
                count += 1;
                cursor = expression->nodes[cursor].subtree_start;
            }
            continue;
        }
        bool numeric_unary = node->id == AST_NODE_UNARY_MINUS || node->id == AST_NODE_UNARY_PLUS;
        bool numeric_binary = (node->id >= AST_NODE_BINARY_PLUS && node->id <= AST_NODE_BINARY_PERCENT) ||
                              ((expected_kind == ANALYSIS_TYPE_INTEGER || expected_kind == ANALYSIS_TYPE_VECTOR) &&
                               ((node->id >= AST_NODE_BINARY_SHIFT_LEFT && node->id <= AST_NODE_BINARY_SHIFT_RIGHT) ||
                                (node->id >= AST_NODE_BINARY_AMPERSAND && node->id <= AST_NODE_BINARY_CARET)));
        if ((expected_kind == ANALYSIS_TYPE_INTEGER || expected_kind == ANALYSIS_TYPE_FLOAT || expected_kind == ANALYSIS_TYPE_VECTOR) &&
            (numeric_unary || numeric_binary))
        {
            current->type = current_expected;
            u32 right = current_index - 1;
            BUSTER_CHECK(count < expression->ast.count);
            top[count] = (AnalysisExpectation){.type = current_expected, .node_index = right};
            count += 1;
            if (numeric_binary)
            {
                u32 left = expression->nodes[right].subtree_start - 1;
                BUSTER_CHECK(count < expression->ast.count);
                top[count] = (AnalysisExpectation){.type = current_expected, .node_index = left};
                count += 1;
            }
            continue;
        }
        if (!analysis_type_compatible(context->result, current_expected, current->type))
        {
            failed = true;
        }
    }
    AnalysisTypedNode* root = expression->nodes + node_index;
    if (failed || !analysis_type_compatible(context->result, expected, root->type))
    {
        analysis_mismatch_diagnostic_push(context, analysis_node_range(context->owner, expression->ast.nodes + node_index), expected, root->type);
    }
}

BUSTER_GLOBAL_LOCAL String8 analysis_instantiation_key(Arena* result_arena, Arena* scratch_arena, AnalysisResult* result, AnalysisEntity* entity,
                                                       AnalysisGenericTypeBinding* bindings, u32 binding_count,
                                                       AnalysisCompileTimeArgument* compile_time_arguments, u32 compile_time_argument_count)
{
    AnalysisTypeId generic_type = result->module.semantics[entity->id.index.value].type;
    AnalysisGenericTypeBinding** sorted_bindings = arena_allocate(scratch_arena, AnalysisGenericTypeBinding*, binding_count);
    for (u32 index = 0; index < binding_count; index += 1)
    {
        sorted_bindings[index] = bindings + index;
    }
    for (u32 index = 1; index < binding_count; index += 1)
    {
        AnalysisGenericTypeBinding* binding = sorted_bindings[index];
        u32 insertion = index;
        while (insertion && analysis_string_compare(sorted_bindings[insertion - 1]->name, binding->name) > 0)
        {
            sorted_bindings[insertion] = sorted_bindings[insertion - 1];
            insertion -= 1;
        }
        sorted_bindings[insertion] = binding;
    }
    AnalysisCompileTimeArgument** sorted_arguments = arena_allocate(scratch_arena, AnalysisCompileTimeArgument*, compile_time_argument_count);
    for (u32 index = 0; index < compile_time_argument_count; index += 1)
    {
        sorted_arguments[index] = compile_time_arguments + index;
    }
    for (u32 index = 1; index < compile_time_argument_count; index += 1)
    {
        AnalysisCompileTimeArgument* argument = sorted_arguments[index];
        u32 insertion = index;
        while (insertion && sorted_arguments[insertion - 1]->source_argument_index > argument->source_argument_index)
        {
            sorted_arguments[insertion] = sorted_arguments[insertion - 1];
            insertion -= 1;
        }
        sorted_arguments[insertion] = argument;
    }

    u32 part_capacity = 4 + entity->ast.code->type->function.argument_count + binding_count * 3 + compile_time_argument_count * 4;
    String8* parts = arena_allocate(scratch_arena, String8, part_capacity);
    u32 part_count = 0;
    parts[part_count++] = string_format(result_arena, S8("module={S8};entity={S8};signature={S8};"), result->module.name, entity->name,
                                        analysis_type_canonical(result_arena, scratch_arena, result, generic_type));
    u32 argument_index = 0;
    for (AstTypeArgument* argument = entity->ast.code->type->function.first_argument; argument; argument = argument->next, argument_index += 1)
    {
        parts[part_count++] = string_format(result_arena, S8("parameter={u32}:{u32};"), argument_index, (u32)argument->is_compile_time);
    }
    for (u32 index = 0; index < binding_count; index += 1)
    {
        AnalysisGenericTypeBinding* binding = sorted_bindings[index];
        parts[part_count++] = S8("binding=");
        parts[part_count++] = binding->name;
        parts[part_count++] = string_format(result_arena, S8(":{S8};"), analysis_type_canonical(result_arena, scratch_arena, result, binding->type));
    }
    for (u32 index = 0; index < compile_time_argument_count; index += 1)
    {
        AnalysisCompileTimeArgument* argument = sorted_arguments[index];
        parts[part_count++] = string_format(result_arena, S8("constant={u32}:"), argument->source_argument_index);
        parts[part_count++] = analysis_type_canonical(result_arena, scratch_arena, result, argument->type);
        parts[part_count++] = S8(":");
        parts[part_count++] = string_format(result_arena, S8("{S8};"), analysis_constant_canonical(result_arena, scratch_arena, result, argument->constant));
    }
    BUSTER_CHECK(part_count <= part_capacity);
    return string_join_arena(result_arena, (SliceString8){.pointer = parts, .length = part_count}, false);
}

BUSTER_GLOBAL_LOCAL void analysis_instantiation_requester_add(Arena* result_arena, AnalysisInstantiation* instantiation, AnalysisModuleId requester)
{
    for (AnalysisInstantiationRequester* existing = instantiation->first_requester; existing; existing = existing->next)
    {
        if (existing->module.value == requester.value)
        {
            return;
        }
    }
    AnalysisInstantiationRequester* added = arena_allocate(result_arena, AnalysisInstantiationRequester, 1);
    *added = (AnalysisInstantiationRequester){.module = requester};
    if (instantiation->last_requester)
    {
        instantiation->last_requester->next = added;
    }
    else
    {
        instantiation->first_requester = added;
    }
    instantiation->last_requester = added;
    instantiation->requester_count += 1;
}

BUSTER_GLOBAL_LOCAL AnalysisInstantiation* analysis_instantiation_find(AnalysisResult* result, AnalysisEntityId entity, String8 canonical_key,
                                                                       u64 canonical_hash)
{
    for (AnalysisInstantiation* instantiation = result->first_instantiation; instantiation; instantiation = instantiation->next)
    {
        if (instantiation->generic_entity.module.value != entity.module.value || instantiation->generic_entity.index.value != entity.index.value ||
            instantiation->canonical_hash != canonical_hash)
        {
            continue;
        }
        if (string_equal(instantiation->canonical_key, canonical_key))
        {
            return instantiation;
        }
    }
    return 0;
}

BUSTER_GLOBAL_LOCAL AnalysisInstantiation* analysis_generic_call_resolve(AnalysisBodyContext* context, AnalysisTypedExpression* expression, AstNode* call,
                                                                         AnalysisTypedNode* callee, u32* operands, u32 first_operand)
{
    AnalysisResult* owner = analysis_program_module_from_id(context->result, callee->entity.module);
    if (!owner || callee->entity.index.value >= owner->module.entity_count)
    {
        return 0;
    }
    AnalysisEntity* entity = owner->module.entities + callee->entity.index.value;
    AnalysisType* generic_function = analysis_type_from_id(owner, owner->module.semantics[callee->entity.index.value].type);
    if (entity->kind != ANALYSIS_ENTITY_CODE || generic_function->kind != ANALYSIS_TYPE_FUNCTION)
    {
        return 0;
    }

    u32 source_argument_count = generic_function->as.function.argument_count;
    if (call->call.argument_count != source_argument_count)
    {
        analysis_body_diagnostic_push(context, call->call.range, ANALYSIS_DIAGNOSTIC_ARGUMENT_COUNT, (String8){0});
        return 0;
    }

    AnalysisGenericTypeBinding* bindings = arena_allocate(context->scratch_arena, AnalysisGenericTypeBinding, owner->types.count);
    AnalysisCompileTimeArgument* compile_time_arguments = arena_allocate(context->scratch_arena, AnalysisCompileTimeArgument, source_argument_count);
    u32 binding_count = 0;
    u32 compile_time_argument_count = 0;
    bool valid = true;
    AstTypeArgument* source_argument = entity->ast.code->type->function.first_argument;
    for (u32 argument_index = 0; argument_index < source_argument_count; argument_index += 1, source_argument = source_argument->next)
    {
        BUSTER_CHECK(source_argument);
        AnalysisDiagnostic* previous_diagnostic = context->result->last_diagnostic;
        u32 node_index = operands[first_operand + 1 + argument_index];
        AnalysisTypeId pattern = generic_function->as.function.argument_types[argument_index];
        if (!analysis_type_has_compile_time_parameter(context->scratch_arena, owner, pattern))
        {
            AnalysisTypeId expected = analysis_type_import(context->result_arena, context->scratch_arena, context->result, owner, pattern);
            analysis_expression_expect(context, expression, node_index, expected);
        }
        AnalysisTypedNode* actual_node = expression->nodes + node_index;
        AnalysisTypeId actual = analysis_type_import(context->result_arena, context->scratch_arena, owner, context->result, actual_node->type);
        if (!analysis_generic_type_infer(context->scratch_arena, owner, pattern, actual, bindings, &binding_count, owner->types.count))
        {
            valid = false;
            analysis_mismatch_diagnostic_push(context, analysis_node_range(context->owner, expression->ast.nodes + node_index),
                                              analysis_type_import(context->result_arena, context->scratch_arena, context->result, owner, pattern),
                                              actual_node->type);
        }
        if (source_argument->is_compile_time)
        {
            if (actual_node->constant.kind == ANALYSIS_CONSTANT_NONE)
            {
                valid = false;
                analysis_body_diagnostic_push(context, analysis_node_range(context->owner, expression->ast.nodes + node_index),
                                              ANALYSIS_DIAGNOSTIC_COMPILE_TIME_ARGUMENT_REQUIRED, source_argument->name);
            }
            compile_time_arguments[compile_time_argument_count] = (AnalysisCompileTimeArgument){
                .constant = actual_node->constant,
                .type = actual,
                .source_argument_index = argument_index,
            };
            compile_time_argument_count += 1;
        }
        AnalysisDiagnostic* argument_diagnostic = previous_diagnostic ? previous_diagnostic->next : context->result->first_diagnostic;
        while (argument_diagnostic)
        {
            argument_diagnostic->argument_index = argument_index;
            argument_diagnostic->has_argument_index = true;
            argument_diagnostic = argument_diagnostic->next;
        }
    }
    BUSTER_CHECK(!source_argument);
    if (!valid)
    {
        return 0;
    }

    u32 runtime_argument_count = source_argument_count - compile_time_argument_count;
    AnalysisTypeId* runtime_argument_types = arena_allocate(context->scratch_arena, AnalysisTypeId, runtime_argument_count);
    u32 runtime_argument_index = 0;
    source_argument = entity->ast.code->type->function.first_argument;
    for (u32 argument_index = 0; argument_index < source_argument_count; argument_index += 1, source_argument = source_argument->next)
    {
        if (source_argument->is_compile_time)
        {
            continue;
        }
        runtime_argument_types[runtime_argument_index] = analysis_type_substitute(
            context->result_arena, context->scratch_arena, owner, generic_function->as.function.argument_types[argument_index], bindings, binding_count);
        runtime_argument_index += 1;
    }
    AnalysisTypeId return_type =
        analysis_type_substitute(context->result_arena, context->scratch_arena, owner, generic_function->as.function.return_type, bindings, binding_count);
    if (analysis_type_has_compile_time_parameter(context->scratch_arena, owner, return_type))
    {
        analysis_body_diagnostic_push(context, call->call.range, ANALYSIS_DIAGNOSTIC_EXPECTED_CONTEXTUAL_TYPE, entity->name);
        return 0;
    }

    String8 canonical_key = analysis_instantiation_key(context->result_arena, context->scratch_arena, owner, entity, bindings, binding_count,
                                                       compile_time_arguments, compile_time_argument_count);
    u64 canonical_hash = analysis_bytes_hash(canonical_key);
    AnalysisInstantiation* instantiation = analysis_instantiation_find(owner, entity->id, canonical_key, canonical_hash);
    if (instantiation)
    {
        analysis_instantiation_requester_add(context->result_arena, instantiation, context->result->module.id);
        return instantiation;
    }

    instantiation = arena_allocate(context->result_arena, AnalysisInstantiation, 1);
    *instantiation = (AnalysisInstantiation){
        .generic_entity = entity->id,
        .codegen_owner = owner->module.id,
        .canonical_key = canonical_key,
        .canonical_hash = canonical_hash,
        .symbol_name = string_format(context->result_arena, S8("{S8}${u64:x,no_prefix}"), entity->name, canonical_hash),
        .function_type = analysis_type_intern(context->result_arena, &owner->types,
                                              (AnalysisType){
                                                  .kind = ANALYSIS_TYPE_FUNCTION,
                                                  .as.function =
                                                      {
                                                          .argument_types = runtime_argument_types,
                                                          .return_type = return_type,
                                                          .calling_convention = generic_function->as.function.calling_convention,
                                                          .argument_count = runtime_argument_count,
                                                          .is_variadic = generic_function->as.function.is_variadic,
                                                      },
                                              }),
        .id = {.value = owner->instantiation_count},
        .type_binding_count = binding_count,
        .compile_time_argument_count = compile_time_argument_count,
    };
    instantiation->type_bindings = arena_allocate(context->result_arena, AnalysisGenericTypeBinding, binding_count);
    for (u32 index = 0; index < binding_count; index += 1)
    {
        instantiation->type_bindings[index] = bindings[index];
    }
    instantiation->compile_time_arguments = arena_allocate(context->result_arena, AnalysisCompileTimeArgument, compile_time_argument_count);
    for (u32 index = 0; index < compile_time_argument_count; index += 1)
    {
        instantiation->compile_time_arguments[index] = compile_time_arguments[index];
    }
    analysis_instantiation_requester_add(context->result_arena, instantiation, context->result->module.id);
    if (owner->last_instantiation)
    {
        owner->last_instantiation->next = instantiation;
    }
    else
    {
        owner->first_instantiation = instantiation;
    }
    owner->last_instantiation = instantiation;
    owner->instantiation_count += 1;
    return instantiation;
}

BUSTER_GLOBAL_LOCAL AnalysisTypedExpression* analysis_expression(AnalysisBodyContext* context, AnalysisBinding* bindings, AstExpression ast,
                                                                 AnalysisTypeId expected)
{
    AnalysisTypeId poison = context->result->types.builtin.poison;
    AnalysisTypedExpression* expression = arena_allocate(context->result_arena, AnalysisTypedExpression, 1);
    *expression = (AnalysisTypedExpression){
        .ast = ast,
        .expected_type = expected,
        .type = poison,
    };
    expression->nodes = arena_allocate(context->result_arena, AnalysisTypedNode, ast.count);
    if (context->body->last_expression)
    {
        context->body->last_expression->next = expression;
    }
    else
    {
        context->body->first_expression = expression;
    }
    context->body->last_expression = expression;
    context->body->expression_count += 1;

    u32* operands = arena_allocate(context->scratch_arena, u32, ast.count);
    u32 operand_count = 0;
    for (u32 index = 0; index < ast.count; index += 1)
    {
        AstNode* node = ast.nodes + index;
        u32 arity = analysis_node_arity(node);
        BUSTER_CHECK(arity <= operand_count);
        u32 first_operand = operand_count - arity;
        AnalysisTypeId contextual = index + 1 == ast.count ? expected : ANALYSIS_TYPE_ID_INVALID;
        AnalysisTypedNode typed = {
            .type = poison,
            .category = ANALYSIS_VALUE_CATEGORY_TEMPORARY,
            .local = ANALYSIS_LOCAL_ID_INVALID,
            .entity = ANALYSIS_ENTITY_ID_INVALID,
            .namespace_module = ANALYSIS_MODULE_ID_INVALID,
            .instantiation = ANALYSIS_INSTANTIATION_ID_INVALID,
            .subtree_start = arity ? expression->nodes[operands[first_operand]].subtree_start : index,
        };
        switch (node->id)
        {
        case AST_NODE_CONSTANT_INTEGER:
        {
            typed.type = context->result->types.builtin.s32_type;
            typed.constant = (AnalysisConstant){
                .integer = node->integer.value,
                .kind = ANALYSIS_CONSTANT_INTEGER,
            };
        }
        break;
        case AST_NODE_CONSTANT_FLOAT:
        {
            typed.type = context->result->types.builtin.f32_type;
            f64 value = 0.0;
            if (analysis_float_literal_parse(node->floating, &value))
            {
                typed.constant = (AnalysisConstant){
                    .floating = value,
                    .kind = ANALYSIS_CONSTANT_FLOAT,
                };
            }
            else
            {
                analysis_body_diagnostic_push(context, analysis_node_range(context->owner, node), ANALYSIS_DIAGNOSTIC_INVALID_CONSTANT,
                                              node->floating.spelling);
            }
        }
        break;
        case AST_NODE_CONSTANT_CHARACTER:
        {
            typed.type = context->result->types.builtin.u8_type;
            typed.constant = (AnalysisConstant){
                .integer = node->character.code_point,
                .kind = ANALYSIS_CONSTANT_INTEGER,
            };
        }
        break;
        case AST_NODE_CONSTANT_STRING:
        {
            typed.type = analysis_type_intern(context->result_arena, &context->result->types,
                                              (AnalysisType){
                                                  .kind = ANALYSIS_TYPE_ARRAY,
                                                  .as.array =
                                                      {
                                                          .element_type = context->result->types.builtin.u8_type,
                                                          .count = node->string.value.length,
                                                      },
                                              });
            AnalysisConstant* bytes = arena_allocate(context->result_arena, AnalysisConstant, node->string.value.length);
            for (u64 byte_index = 0; byte_index < node->string.value.length; byte_index += 1)
            {
                bytes[byte_index] = (AnalysisConstant){
                    .integer = node->string.value.pointer[byte_index],
                    .kind = ANALYSIS_CONSTANT_INTEGER,
                };
            }
            typed.constant = (AnalysisConstant){
                .aggregate =
                    {
                        .elements = bytes,
                        .element_count = (u32)node->string.value.length,
                    },
                .kind = ANALYSIS_CONSTANT_ARRAY,
            };
        }
        break;
        case AST_NODE_IDENTIFIER:
        {
            AnalysisBinding* binding = analysis_binding_find(bindings, node->identifier.text);
            if (binding)
            {
                AnalysisLocal* local = context->body->locals + binding->local.value;
                typed.type = local->type;
                typed.category = local->is_mutable ? ANALYSIS_VALUE_CATEGORY_MUTABLE_PLACE : ANALYSIS_VALUE_CATEGORY_IMMUTABLE_PLACE;
                typed.local = local->id;
                typed.is_addressable = !local->is_compile_time;
                typed.constant = local->constant;
            }
            else if (string_equal(node->identifier.text, S8("true")) || string_equal(node->identifier.text, S8("false")))
            {
                typed.type = context->result->types.builtin.bool_type;
                typed.constant = (AnalysisConstant){
                    .integer = string_equal(node->identifier.text, S8("true")),
                    .kind = ANALYSIS_CONSTANT_BOOLEAN,
                };
            }
            else
            {
                AnalysisEntity* entity = analysis_value_entity_find(context->result, node->identifier.text);
                if (entity)
                {
                    AnalysisEntitySemantic* semantic = context->result->module.semantics + entity->id.index.value;
                    typed.type = semantic->type;
                    typed.entity = entity->id;
                    typed.category = ANALYSIS_VALUE_CATEGORY_VALUE;
                    if (entity->kind == ANALYSIS_ENTITY_DATA && semantic->constant)
                    {
                        typed.constant = *semantic->constant;
                    }
                    analysis_body_dependency_add(context, entity->id);
                }
                else
                {
                    AnalysisImport* import = analysis_import_find(context->result, node->identifier.text);
                    if (import)
                    {
                        typed.is_namespace = true;
                        typed.namespace_module = import->target_id;
                        typed.category = ANALYSIS_VALUE_CATEGORY_VALUE;
                    }
                    else
                    {
                        analysis_body_diagnostic_push(context, node->identifier.range, ANALYSIS_DIAGNOSTIC_UNKNOWN_IDENTIFIER, node->identifier.text);
                    }
                }
            }
        }
        break;
        case AST_NODE_UNDEFINED:
        case AST_NODE_ENUM_LITERAL:
            break;
        case AST_NODE_AGGREGATE_LITERAL:
        {
            bool constant = true;
            AnalysisConstant* fields = arena_allocate(context->result_arena, AnalysisConstant, arity);
            for (u32 child = 0; child < arity; child += 1)
            {
                fields[child] = expression->nodes[operands[first_operand + child]].constant;
                constant &= fields[child].kind != ANALYSIS_CONSTANT_NONE;
            }
            if (constant)
            {
                typed.constant = (AnalysisConstant){
                    .aggregate = {.elements = fields, .element_count = arity},
                    .kind = ANALYSIS_CONSTANT_AGGREGATE,
                };
            }
        }
        break;
        case AST_NODE_ARRAY_LITERAL:
        {
            AnalysisTypeId element = poison;
            if (arity)
            {
                element = expression->nodes[operands[first_operand]].type;
                for (u32 child = 1; child < arity; child += 1)
                {
                    analysis_expression_expect(context, expression, operands[first_operand + child], element);
                }
            }
            typed.type = analysis_type_intern(context->result_arena, &context->result->types,
                                              (AnalysisType){
                                                  .kind = ANALYSIS_TYPE_ARRAY,
                                                  .as.array = {.element_type = element, .count = arity},
                                              });
            bool constant = true;
            AnalysisConstant* elements = arena_allocate(context->result_arena, AnalysisConstant, arity);
            for (u32 child = 0; child < arity; child += 1)
            {
                elements[child] = expression->nodes[operands[first_operand + child]].constant;
                constant &= elements[child].kind != ANALYSIS_CONSTANT_NONE;
            }
            if (constant)
            {
                typed.constant = (AnalysisConstant){
                    .aggregate = {.elements = elements, .element_count = arity},
                    .kind = ANALYSIS_CONSTANT_ARRAY,
                };
            }
        }
        break;
        case AST_NODE_ARRAY_INDEX:
        {
            AnalysisTypedNode* base = expression->nodes + operands[first_operand];
            AnalysisType* base_type = analysis_type_from_id(context->result, base->type);
            analysis_expression_expect(context, expression, operands[first_operand + 1], context->result->types.builtin.s32_type);
            if (base_type->kind == ANALYSIS_TYPE_ARRAY)
            {
                typed.type = base_type->as.array.element_type;
                typed.category = base->category;
                typed.local = base->local;
                typed.is_addressable = base->is_addressable;
                if (base->local.value != ANALYSIS_ID_UNDERLYING_INVALID)
                {
                    context->body->locals[base->local.value].requires_storage = true;
                }
            }
            else if (base_type->kind == ANALYSIS_TYPE_VECTOR)
            {
                typed.type = base_type->as.vector.element_type;
                typed.category = base->category;
                typed.local = base->local;
                typed.is_addressable = base->is_addressable;
                if (base->local.value != ANALYSIS_ID_UNDERLYING_INVALID)
                {
                    context->body->locals[base->local.value].requires_storage = true;
                }
            }
            else if (base_type->kind == ANALYSIS_TYPE_INFERRED_ARRAY || base_type->kind == ANALYSIS_TYPE_SLICE)
            {
                typed.type = base_type->as.element_type;
                typed.category = base->category;
                typed.local = base->local;
                typed.is_addressable = base->is_addressable;
                if (base->local.value != ANALYSIS_ID_UNDERLYING_INVALID)
                {
                    context->body->locals[base->local.value].requires_storage = true;
                }
            }
            else if (base_type->kind != ANALYSIS_TYPE_POISON)
            {
                analysis_body_diagnostic_push(context, node->array_index.range, ANALYSIS_DIAGNOSTIC_INVALID_OPERAND, (String8){0});
            }
        }
        break;
        case AST_NODE_ARRAY_SLICE:
        {
            AnalysisTypedNode* base = expression->nodes + operands[first_operand];
            AnalysisType* base_type = analysis_type_from_id(context->result, base->type);
            AnalysisTypeId element = poison;
            if (base_type->kind == ANALYSIS_TYPE_ARRAY)
            {
                element = base_type->as.array.element_type;
            }
            else if (base_type->kind == ANALYSIS_TYPE_INFERRED_ARRAY || base_type->kind == ANALYSIS_TYPE_SLICE)
            {
                element = base_type->as.element_type;
            }
            else if (base_type->kind != ANALYSIS_TYPE_POISON)
            {
                analysis_body_diagnostic_push(context, node->array_slice.range, ANALYSIS_DIAGNOSTIC_INVALID_OPERAND, (String8){0});
            }
            for (u32 child = 1; child < arity; child += 1)
            {
                analysis_expression_expect(context, expression, operands[first_operand + child], context->result->types.builtin.s32_type);
            }
            typed.type =
                analysis_type_intern(context->result_arena, &context->result->types, (AnalysisType){.kind = ANALYSIS_TYPE_SLICE, .as.element_type = element});
            if (base->local.value != ANALYSIS_ID_UNDERLYING_INVALID)
            {
                context->body->locals[base->local.value].requires_storage = true;
            }
        }
        break;
        case AST_NODE_MEMBER_ACCESS:
        {
            AnalysisTypedNode* base = expression->nodes + operands[first_operand];
            if (base->is_namespace)
            {
                AnalysisResult* target = analysis_program_module_from_id(context->result, base->namespace_module);
                AnalysisEntity* entity = target ? analysis_value_entity_find(target, node->member_access.member.text) : 0;
                if (entity)
                {
                    AnalysisEntitySemantic* semantic = target->module.semantics + entity->id.index.value;
                    if (semantic->state == ANALYSIS_RESOLUTION_RESOLVED)
                    {
                        typed.type = analysis_type_import(context->result_arena, context->scratch_arena, context->result, target, semantic->type);
                        typed.entity = entity->id;
                        typed.category = ANALYSIS_VALUE_CATEGORY_VALUE;
                        if (entity->kind == ANALYSIS_ENTITY_DATA && semantic->constant)
                        {
                            typed.constant = *semantic->constant;
                        }
                        analysis_body_dependency_add(context, entity->id);
                    }
                }
                if (!entity && target)
                {
                    analysis_body_diagnostic_push(context, node->member_access.member.range, ANALYSIS_DIAGNOSTIC_UNKNOWN_MEMBER,
                                                  node->member_access.member.text);
                }
                break;
            }
            AnalysisType* base_type = analysis_type_from_id(context->result, base->type);
            if (base_type->kind == ANALYSIS_TYPE_STRUCT || base_type->kind == ANALYSIS_TYPE_UNION)
            {
                AnalysisResult* declaration_module = 0;
                AnalysisEntitySemantic* semantic = analysis_declaration_semantic(context->result, base_type->as.declaration, &declaration_module);
                BUSTER_CHECK(semantic);
                for (u32 field_index = 0; field_index < semantic->field_count; field_index += 1)
                {
                    if (string_equal(semantic->fields[field_index].name, node->member_access.member.text))
                    {
                        typed.type = analysis_type_import(context->result_arena, context->scratch_arena, context->result, declaration_module,
                                                          semantic->fields[field_index].type);
                        typed.category = base->category;
                        typed.local = base->local;
                        typed.is_addressable = base->is_addressable;
                        if (base->local.value != ANALYSIS_ID_UNDERLYING_INVALID)
                        {
                            context->body->locals[base->local.value].requires_storage = true;
                        }
                        break;
                    }
                }
                if (analysis_type_is_poison(&context->resolution, typed.type))
                {
                    analysis_body_diagnostic_push(context, node->member_access.member.range, ANALYSIS_DIAGNOSTIC_UNKNOWN_MEMBER,
                                                  node->member_access.member.text);
                }
            }
            else if (base_type->kind != ANALYSIS_TYPE_POISON)
            {
                analysis_body_diagnostic_push(context, node->member_access.range, ANALYSIS_DIAGNOSTIC_INVALID_OPERAND, node->member_access.member.text);
            }
        }
        break;
        case AST_NODE_CALL:
        {
            AnalysisTypedNode* callee = expression->nodes + operands[first_operand];
            AnalysisType* callee_type = analysis_type_from_id(context->result, callee->type);
            if (callee_type->kind == ANALYSIS_TYPE_FUNCTION)
            {
                AnalysisResult* callee_module = analysis_program_module_from_id(context->result, callee->entity.module);
                AnalysisEntity* callee_entity = callee_module && callee->entity.index.value < callee_module->module.entity_count
                                                    ? callee_module->module.entities + callee->entity.index.value
                                                    : 0;
                if (callee_entity && analysis_entity_is_generic(context->scratch_arena, callee_module, callee_entity))
                {
                    AnalysisInstantiation* instantiation = analysis_generic_call_resolve(context, expression, node, callee, operands, first_operand);
                    if (instantiation)
                    {
                        AnalysisType* concrete = analysis_type_from_id(callee_module, instantiation->function_type);
                        callee->type =
                            analysis_type_import(context->result_arena, context->scratch_arena, context->result, callee_module, instantiation->function_type);
                        typed.type = analysis_type_import(context->result_arena, context->scratch_arena, context->result, callee_module,
                                                          concrete->as.function.return_type);
                        typed.instantiation = instantiation->id;
                    }
                    break;
                }
                typed.type = callee_type->as.function.return_type;
                if ((!callee_type->as.function.is_variadic && node->call.argument_count != callee_type->as.function.argument_count) ||
                    (callee_type->as.function.is_variadic && node->call.argument_count < callee_type->as.function.argument_count))
                {
                    analysis_body_diagnostic_push(context, node->call.range, ANALYSIS_DIAGNOSTIC_ARGUMENT_COUNT, (String8){0});
                }
                u32 common = BUSTER_MIN(node->call.argument_count, callee_type->as.function.argument_count);
                for (u32 argument = 0; argument < common; argument += 1)
                {
                    AnalysisDiagnostic* previous_diagnostic = context->result->last_diagnostic;
                    analysis_expression_expect(context, expression, operands[first_operand + 1 + argument], callee_type->as.function.argument_types[argument]);
                    AnalysisDiagnostic* argument_diagnostic = previous_diagnostic ? previous_diagnostic->next : context->result->first_diagnostic;
                    while (argument_diagnostic)
                    {
                        argument_diagnostic->argument_index = argument;
                        argument_diagnostic->has_argument_index = true;
                        argument_diagnostic = argument_diagnostic->next;
                    }
                }
            }
            else if (callee_type->kind != ANALYSIS_TYPE_POISON)
            {
                analysis_body_diagnostic_push(context, node->call.range, ANALYSIS_DIAGNOSTIC_NOT_CALLABLE, (String8){0});
            }
        }
        break;
        case AST_NODE_INTRINSIC_CALL:
        {
            if (string_equal(node->intrinsic_call.name.text, S8("reverse")))
            {
                if (arity == 1)
                {
                    typed.type = expression->nodes[operands[first_operand]].type;
                }
                else
                {
                    analysis_body_diagnostic_push(context, node->intrinsic_call.range, ANALYSIS_DIAGNOSTIC_ARGUMENT_COUNT, node->intrinsic_call.name.text);
                }
            }
            else if (string_equal(node->intrinsic_call.name.text, S8("cast")))
            {
                if (arity == 1)
                {
                    typed.constant = expression->nodes[operands[first_operand]].constant;
                }
                if (arity != 1)
                {
                    analysis_body_diagnostic_push(context, node->intrinsic_call.range, ANALYSIS_DIAGNOSTIC_ARGUMENT_COUNT, node->intrinsic_call.name.text);
                }
                else if (contextual.value != ANALYSIS_ID_UNDERLYING_INVALID)
                {
                    typed.type = contextual;
                }
            }
            else if (string_equal(node->intrinsic_call.name.text, S8("va_start")))
            {
                AnalysisTypeId function_type =
                    context->instantiation ? context->instantiation->function_type : context->result->module.semantics[context->owner->id.index.value].type;
                AnalysisType* function = analysis_type_from_id(context->result, function_type);
                if (arity != 0 || node->intrinsic_call.type_argument)
                {
                    analysis_body_diagnostic_push(context, node->intrinsic_call.range, ANALYSIS_DIAGNOSTIC_ARGUMENT_COUNT, node->intrinsic_call.name.text);
                }
                else if (function->kind != ANALYSIS_TYPE_FUNCTION || !function->as.function.is_variadic)
                {
                    analysis_body_diagnostic_push(context, node->intrinsic_call.range, ANALYSIS_DIAGNOSTIC_INVALID_OPERAND, node->intrinsic_call.name.text);
                }
                else
                {
                    typed.type = context->result->types.builtin.va_list_type;
                }
            }
            else if (string_equal(node->intrinsic_call.name.text, S8("va_copy")) || string_equal(node->intrinsic_call.name.text, S8("va_end")) ||
                     string_equal(node->intrinsic_call.name.text, S8("va_arg")))
            {
                bool is_arg = string_equal(node->intrinsic_call.name.text, S8("va_arg"));
                bool valid_arguments = arity == 1 && (is_arg == (node->intrinsic_call.type_argument != 0));
                if (!valid_arguments)
                {
                    analysis_body_diagnostic_push(context, node->intrinsic_call.range, ANALYSIS_DIAGNOSTIC_ARGUMENT_COUNT, node->intrinsic_call.name.text);
                }
                else
                {
                    AnalysisTypedNode* operand = expression->nodes + operands[first_operand];
                    AnalysisType* operand_type = analysis_type_from_id(context->result, operand->type);
                    bool valid_list = operand_type->kind == ANALYSIS_TYPE_POINTER &&
                                      analysis_type_id_equal(operand_type->as.element_type, context->result->types.builtin.va_list_type);
                    if (!valid_list)
                    {
                        analysis_body_diagnostic_push(context, node->intrinsic_call.range, ANALYSIS_DIAGNOSTIC_INVALID_OPERAND, node->intrinsic_call.name.text);
                    }
                    else if (is_arg)
                    {
                        typed.type = analysis_body_type_resolve(context, node->intrinsic_call.type_argument);
                    }
                    else if (string_equal(node->intrinsic_call.name.text, S8("va_copy")))
                    {
                        typed.type = context->result->types.builtin.va_list_type;
                    }
                    else
                    {
                        typed.type = context->result->types.builtin.void_type;
                    }
                }
            }
            else
            {
                analysis_body_diagnostic_push(context, node->intrinsic_call.name.range, ANALYSIS_DIAGNOSTIC_UNKNOWN_IDENTIFIER, node->intrinsic_call.name.text);
            }
        }
        break;
        case AST_NODE_ADDRESS_OF:
        {
            AnalysisTypedNode* operand = expression->nodes + operands[first_operand];
            if (!operand->is_addressable && !analysis_type_id_equal(operand->type, poison))
            {
                analysis_body_diagnostic_push(context, node->pointer_operator.range, ANALYSIS_DIAGNOSTIC_EXPECTED_PLACE, (String8){0});
            }
            typed.type = analysis_type_intern(context->result_arena, &context->result->types,
                                              (AnalysisType){.kind = ANALYSIS_TYPE_POINTER, .as.element_type = operand->type});
            if (operand->local.value != ANALYSIS_ID_UNDERLYING_INVALID)
            {
                context->body->locals[operand->local.value].address_taken = true;
                context->body->locals[operand->local.value].requires_storage = true;
            }
        }
        break;
        case AST_NODE_DEREFERENCE:
        {
            AnalysisTypedNode* operand = expression->nodes + operands[first_operand];
            AnalysisType* operand_type = analysis_type_from_id(context->result, operand->type);
            if (operand_type->kind == ANALYSIS_TYPE_POINTER)
            {
                typed.type = operand_type->as.element_type;
                typed.category = ANALYSIS_VALUE_CATEGORY_MUTABLE_PLACE;
                typed.is_addressable = true;
            }
            else if (operand_type->kind != ANALYSIS_TYPE_POISON)
            {
                analysis_body_diagnostic_push(context, node->pointer_operator.range, ANALYSIS_DIAGNOSTIC_INVALID_OPERAND, (String8){0});
            }
        }
        break;
        case AST_NODE_UNARY_MINUS:
        case AST_NODE_UNARY_PLUS:
        {
            AnalysisTypedNode* operand = expression->nodes + operands[first_operand];
            typed.type = operand->type;
            typed.constant = operand->constant;
            if (node->id == AST_NODE_UNARY_MINUS && typed.constant.kind == ANALYSIS_CONSTANT_INTEGER)
            {
                typed.constant.is_negative = !typed.constant.is_negative;
            }
            else if (node->id == AST_NODE_UNARY_MINUS && typed.constant.kind == ANALYSIS_CONSTANT_FLOAT)
            {
                typed.constant.floating = -typed.constant.floating;
            }
            if (!analysis_type_is_numeric(context->result, typed.type) && !analysis_type_id_equal(typed.type, poison))
            {
                analysis_body_diagnostic_push(context, analysis_node_range(context->owner, node), ANALYSIS_DIAGNOSTIC_INVALID_OPERAND, (String8){0});
            }
        }
        break;
        case AST_NODE_UNARY_LOGICAL_NOT:
        {
            analysis_expression_expect(context, expression, operands[first_operand], context->result->types.builtin.bool_type);
            typed.type = context->result->types.builtin.bool_type;
            AnalysisTypedNode* operand = expression->nodes + operands[first_operand];
            if (operand->constant.kind == ANALYSIS_CONSTANT_BOOLEAN)
            {
                typed.constant = (AnalysisConstant){
                    .integer = !operand->constant.integer,
                    .kind = ANALYSIS_CONSTANT_BOOLEAN,
                };
            }
        }
        break;
        case AST_NODE_UNARY_BITWISE_NOT:
        {
            typed.type = expression->nodes[operands[first_operand]].type;
            AnalysisConstant operand_constant = expression->nodes[operands[first_operand]].constant;
            if (operand_constant.kind == ANALYSIS_CONSTANT_INTEGER && !operand_constant.is_negative)
            {
                AnalysisType* integer_type = analysis_type_from_id(context->result, typed.type);
                if (integer_type->kind == ANALYSIS_TYPE_INTEGER && integer_type->as.integer.is_signed && operand_constant.integer <= (u64)INT64_MAX)
                {
                    s64 value = ~(s64)operand_constant.integer;
                    typed.constant = (AnalysisConstant){
                        .integer = value < 0 ? (u64)(-(value + 1)) + 1 : (u64)value,
                        .kind = ANALYSIS_CONSTANT_INTEGER,
                        .is_negative = value < 0,
                    };
                }
                else
                {
                    u32 width = integer_type->as.integer.bit_width;
                    u64 mask = width == 64 ? UINT64_MAX : ((u64)1 << width) - 1;
                    typed.constant = (AnalysisConstant){
                        .integer = ~operand_constant.integer & mask,
                        .kind = ANALYSIS_CONSTANT_INTEGER,
                    };
                }
            }
            if (!analysis_type_is_integer(context->result, typed.type) && !analysis_type_id_equal(typed.type, poison))
            {
                analysis_body_diagnostic_push(context, analysis_node_range(context->owner, node), ANALYSIS_DIAGNOSTIC_INVALID_OPERAND, (String8){0});
            }
        }
        break;
        case AST_NODE_BINARY_PLUS:
        case AST_NODE_BINARY_MINUS:
        case AST_NODE_BINARY_ASTERISK:
        case AST_NODE_BINARY_SLASH:
        case AST_NODE_BINARY_PERCENT:
        case AST_NODE_BINARY_SHIFT_LEFT:
        case AST_NODE_BINARY_SHIFT_RIGHT:
        case AST_NODE_BINARY_AMPERSAND:
        case AST_NODE_BINARY_BAR:
        case AST_NODE_BINARY_CARET:
        case AST_NODE_BINARY_EQUAL:
        case AST_NODE_BINARY_NOT_EQUAL:
        case AST_NODE_BINARY_LESS:
        case AST_NODE_BINARY_LESS_EQUAL:
        case AST_NODE_BINARY_GREATER:
        case AST_NODE_BINARY_GREATER_EQUAL:
        case AST_NODE_BINARY_BOOLEAN_AND:
        case AST_NODE_BINARY_BOOLEAN_OR:
        case AST_NODE_BINARY_BOOLEAN_AND_SHORT_CIRCUIT:
        case AST_NODE_BINARY_BOOLEAN_OR_SHORT_CIRCUIT:
        case AST_NODE_BINARY_RANGE:
        {
            AnalysisTypeId left = expression->nodes[operands[first_operand]].type;
            AnalysisConstant left_constant = expression->nodes[operands[first_operand]].constant;
            AnalysisConstant right_constant = expression->nodes[operands[first_operand + 1]].constant;
            analysis_expression_expect(context, expression, operands[first_operand + 1], left);
            bool comparison = node->id >= AST_NODE_BINARY_EQUAL && node->id <= AST_NODE_BINARY_GREATER_EQUAL;
            bool equality = node->id == AST_NODE_BINARY_EQUAL || node->id == AST_NODE_BINARY_NOT_EQUAL;
            bool boolean_operation = node->id >= AST_NODE_BINARY_BOOLEAN_AND && node->id <= AST_NODE_BINARY_BOOLEAN_OR_SHORT_CIRCUIT;
            bool bitwise = node->id == AST_NODE_BINARY_PERCENT || (node->id >= AST_NODE_BINARY_SHIFT_LEFT && node->id <= AST_NODE_BINARY_SHIFT_RIGHT) ||
                           (node->id >= AST_NODE_BINARY_AMPERSAND && node->id <= AST_NODE_BINARY_CARET);
            AnalysisTypeKind left_kind = analysis_type_from_id(context->result, left)->kind;
            bool equality_type = left_kind == ANALYSIS_TYPE_INTEGER || left_kind == ANALYSIS_TYPE_FLOAT || left_kind == ANALYSIS_TYPE_BOOL ||
                                 left_kind == ANALYSIS_TYPE_POINTER || left_kind == ANALYSIS_TYPE_ENUM;
            bool valid = boolean_operation                              ? analysis_type_id_equal(left, context->result->types.builtin.bool_type)
                         : bitwise || node->id == AST_NODE_BINARY_RANGE ? analysis_type_is_integer(context->result, left)
                         : equality                                     ? equality_type
                                                                        : analysis_type_is_numeric(context->result, left);
            if (left_kind == ANALYSIS_TYPE_VECTOR)
            {
                AnalysisType* vector = analysis_type_from_id(context->result, left);
                AnalysisType* element = analysis_type_from_id(context->result, vector->as.vector.element_type);
                bool add_or_subtract = node->id == AST_NODE_BINARY_PLUS || node->id == AST_NODE_BINARY_MINUS;
                bool float_arithmetic = element->kind == ANALYSIS_TYPE_FLOAT && node->id >= AST_NODE_BINARY_PLUS && node->id <= AST_NODE_BINARY_SLASH;
                bool integer_bitwise = element->kind == ANALYSIS_TYPE_INTEGER && node->id >= AST_NODE_BINARY_AMPERSAND && node->id <= AST_NODE_BINARY_CARET;
                bool vector_comparison = comparison && (element->kind == ANALYSIS_TYPE_INTEGER || element->kind == ANALYSIS_TYPE_FLOAT);
                valid = add_or_subtract || float_arithmetic || integer_bitwise || vector_comparison;
            }
            if (!valid && !analysis_type_id_equal(left, poison))
            {
                analysis_body_diagnostic_push(context, analysis_node_range(context->owner, node), ANALYSIS_DIAGNOSTIC_INVALID_OPERAND, (String8){0});
            }
            if (node->id == AST_NODE_BINARY_RANGE)
            {
                typed.type =
                    analysis_type_intern(context->result_arena, &context->result->types, (AnalysisType){.kind = ANALYSIS_TYPE_RANGE, .as.element_type = left});
            }
            else
            {
                if (comparison && left_kind == ANALYSIS_TYPE_VECTOR)
                {
                    typed.type = analysis_vector_mask_type(context->result_arena, context->result, analysis_type_from_id(context->result, left));
                }
                else
                {
                    typed.type = comparison ? context->result->types.builtin.bool_type : left;
                }
                if (boolean_operation)
                {
                    typed.type = context->result->types.builtin.bool_type;
                }
            }
            if (comparison && left_constant.kind == ANALYSIS_CONSTANT_INTEGER && right_constant.kind == ANALYSIS_CONSTANT_INTEGER &&
                left_constant.integer <= (u64)INT64_MAX && right_constant.integer <= (u64)INT64_MAX)
            {
                s64 left_value = left_constant.is_negative ? -(s64)left_constant.integer : (s64)left_constant.integer;
                s64 right_value = right_constant.is_negative ? -(s64)right_constant.integer : (s64)right_constant.integer;
                bool value = false;
                switch (node->id)
                {
                case AST_NODE_BINARY_EQUAL:
                    value = left_value == right_value;
                    break;
                case AST_NODE_BINARY_NOT_EQUAL:
                    value = left_value != right_value;
                    break;
                case AST_NODE_BINARY_LESS:
                    value = left_value < right_value;
                    break;
                case AST_NODE_BINARY_LESS_EQUAL:
                    value = left_value <= right_value;
                    break;
                case AST_NODE_BINARY_GREATER:
                    value = left_value > right_value;
                    break;
                case AST_NODE_BINARY_GREATER_EQUAL:
                    value = left_value >= right_value;
                    break;
                default:
                    break;
                }
                typed.constant = (AnalysisConstant){
                    .integer = value,
                    .kind = ANALYSIS_CONSTANT_BOOLEAN,
                };
            }
            else if (comparison && left_constant.kind == ANALYSIS_CONSTANT_FLOAT && right_constant.kind == ANALYSIS_CONSTANT_FLOAT)
            {
                bool value = false;
                switch (node->id)
                {
                case AST_NODE_BINARY_EQUAL:
                    value = left_constant.floating == right_constant.floating;
                    break;
                case AST_NODE_BINARY_NOT_EQUAL:
                    value = left_constant.floating != right_constant.floating;
                    break;
                case AST_NODE_BINARY_LESS:
                    value = left_constant.floating < right_constant.floating;
                    break;
                case AST_NODE_BINARY_LESS_EQUAL:
                    value = left_constant.floating <= right_constant.floating;
                    break;
                case AST_NODE_BINARY_GREATER:
                    value = left_constant.floating > right_constant.floating;
                    break;
                case AST_NODE_BINARY_GREATER_EQUAL:
                    value = left_constant.floating >= right_constant.floating;
                    break;
                default:
                    break;
                }
                typed.constant = (AnalysisConstant){
                    .integer = value,
                    .kind = ANALYSIS_CONSTANT_BOOLEAN,
                };
            }
            else if (boolean_operation && left_constant.kind == ANALYSIS_CONSTANT_BOOLEAN && right_constant.kind == ANALYSIS_CONSTANT_BOOLEAN)
            {
                bool is_and = node->id == AST_NODE_BINARY_BOOLEAN_AND || node->id == AST_NODE_BINARY_BOOLEAN_AND_SHORT_CIRCUIT;
                typed.constant = (AnalysisConstant){
                    .integer = is_and ? left_constant.integer && right_constant.integer : left_constant.integer || right_constant.integer,
                    .kind = ANALYSIS_CONSTANT_BOOLEAN,
                };
            }
            else if (!comparison && !boolean_operation && node->id != AST_NODE_BINARY_RANGE)
            {
                typed.constant = left_constant.kind == ANALYSIS_CONSTANT_FLOAT ? analysis_constant_float_binary(node->id, left_constant, right_constant)
                                                                               : analysis_constant_integer_binary(node->id, left_constant, right_constant);
            }
        }
        break;
        case AST_NODE_COUNT:
            break;
        }
        expression->nodes[index] = typed;
        operand_count = first_operand;
        operands[operand_count] = index;
        operand_count += 1;
    }
    BUSTER_CHECK(operand_count == 1 || ast.count == 0);
    if (ast.count)
    {
        u32 root = operands[0];
        if (expected.value != ANALYSIS_ID_UNDERLYING_INVALID)
        {
            analysis_expression_expect(context, expression, root, expected);
        }
        expression->type = expression->nodes[root].type;
        for (u32 index = 0; index < ast.count; index += 1)
        {
            AstNode* node = ast.nodes + index;
            AnalysisTypedNode* typed = expression->nodes + index;
            bool empty_array = node->id == AST_NODE_ARRAY_LITERAL && !node->array_literal.element_count;
            bool needs_context = node->id == AST_NODE_UNDEFINED || node->id == AST_NODE_ENUM_LITERAL || node->id == AST_NODE_AGGREGATE_LITERAL ||
                                 (node->id == AST_NODE_INTRINSIC_CALL && string_equal(node->intrinsic_call.name.text, S8("cast"))) || empty_array;
            bool unresolved = analysis_type_id_equal(typed->type, poison);
            if (empty_array)
            {
                AnalysisType* array_type = analysis_type_from_id(context->result, typed->type);
                unresolved = analysis_type_is_poison(&context->resolution, array_type->as.array.element_type);
            }
            if (needs_context && unresolved)
            {
                analysis_body_diagnostic_push(context, analysis_node_range(context->owner, node), ANALYSIS_DIAGNOSTIC_EXPECTED_CONTEXTUAL_TYPE, (String8){0});
            }
        }
    }
    return expression;
}

BUSTER_GLOBAL_LOCAL AnalysisBodyTask* analysis_body_task_push(AnalysisBodyContext* context, AnalysisBodyTask** top, AstStatement* statement,
                                                              AnalysisBinding* bindings, u32 scope_depth, u32 loop_depth)
{
    if (!statement)
    {
        return 0;
    }
    AnalysisBodyTask* task = arena_allocate(context->scratch_arena, AnalysisBodyTask, 1);
    *task = (AnalysisBodyTask){
        .previous = *top,
        .statement = statement,
        .bindings = bindings,
        .scope_depth = scope_depth,
        .loop_depth = loop_depth,
    };
    *top = task;
    return task;
}

BUSTER_GLOBAL_LOCAL void analysis_body_task_continue(AnalysisBodyContext* context, AnalysisBodyTask** top, AnalysisBodyTask* task, AnalysisBinding* bindings)
{
    analysis_body_task_push(context, top, task->statement->next, bindings, task->scope_depth, task->loop_depth);
}

BUSTER_GLOBAL_LOCAL void analysis_body_statement(AnalysisBodyContext* context, AnalysisBodyTask** top, AnalysisBodyTask* task, AnalysisTypeId return_type)
{
    AstStatement* statement = task->statement;
    AnalysisBinding* bindings = task->bindings;
    AnalysisTypeId invalid = ANALYSIS_TYPE_ID_INVALID;
    switch (statement->id)
    {
    case AST_STATEMENT_RETURN:
    {
        analysis_expression(context, bindings, statement->return_statement.expression, return_type);
        analysis_body_task_continue(context, top, task, bindings);
    }
    break;
    case AST_STATEMENT_DATA:
    {
        AstDataStatement* data = &statement->data_statement;
        AnalysisTypeId declared = data->type ? analysis_body_type_resolve(context, data->type) : invalid;
        AnalysisTypedExpression* initializer = analysis_expression(context, bindings, data->initializer, declared);
        AnalysisTypeId type = data->type ? declared : initializer->type;
        if (data->type)
        {
            AnalysisType* declared_type = analysis_type_from_id(context->result, declared);
            AnalysisType* initializer_type = analysis_type_from_id(context->result, initializer->type);
            if (declared_type->kind == ANALYSIS_TYPE_INFERRED_ARRAY && initializer_type->kind == ANALYSIS_TYPE_ARRAY &&
                analysis_type_id_equal(declared_type->as.element_type, initializer_type->as.array.element_type))
            {
                type = initializer->type;
            }
        }
        AnalysisLocal* local = analysis_local_add(context, &bindings, data->name, type, ANALYSIS_LOCAL_DATA, task->scope_depth, data->is_compile_time);
        local->is_initialized = !(data->initializer.count == 1 && data->initializer.nodes[0].id == AST_NODE_UNDEFINED);
        if (data->is_compile_time && data->initializer.count)
        {
            local->constant = initializer->nodes[data->initializer.count - 1].constant;
            if (local->constant.kind == ANALYSIS_CONSTANT_NONE)
            {
                analysis_body_diagnostic_push(context, data->name.range, ANALYSIS_DIAGNOSTIC_INVALID_CONSTANT, data->name.text);
            }
        }
        analysis_body_task_continue(context, top, task, bindings);
    }
    break;
    case AST_STATEMENT_EXPRESSION:
    {
        analysis_expression(context, bindings, statement->expression_statement.expression, invalid);
        analysis_body_task_continue(context, top, task, bindings);
    }
    break;
    case AST_STATEMENT_ASSIGNMENT:
    {
        AstAssignmentStatement* assignment = &statement->assignment_statement;
        AnalysisTypedExpression* target = analysis_expression(context, bindings, assignment->target, invalid);
        if (target->ast.count)
        {
            AnalysisTypedNode* root = target->nodes + target->ast.count - 1;
            if (root->category != ANALYSIS_VALUE_CATEGORY_MUTABLE_PLACE && !analysis_type_id_equal(root->type, context->result->types.builtin.poison))
            {
                analysis_body_diagnostic_push(context, statement->range, ANALYSIS_DIAGNOSTIC_EXPECTED_PLACE, (String8){0});
            }
        }
        analysis_expression(context, bindings, assignment->value, target->type);
        bool integer_assignment = assignment->operator>= AST_ASSIGNMENT_MODULO_EQUAL;
        bool valid_assignment =
            integer_assignment ? analysis_type_is_integer(context->result, target->type) : analysis_type_is_numeric(context->result, target->type);
        if (assignment->operator!= AST_ASSIGNMENT_EQUAL && !valid_assignment && !analysis_type_id_equal(target->type, context->result->types.builtin.poison))
        {
            analysis_body_diagnostic_push(context, statement->range, ANALYSIS_DIAGNOSTIC_INVALID_OPERAND, (String8){0});
        }
        analysis_body_task_continue(context, top, task, bindings);
    }
    break;
    case AST_STATEMENT_IF:
    {
        AstIfStatement* if_statement = &statement->if_statement;
        analysis_expression(context, bindings, if_statement->condition, context->result->types.builtin.bool_type);
        analysis_body_task_continue(context, top, task, bindings);
        if (if_statement->alternative == AST_IF_ALTERNATIVE_BLOCK)
        {
            analysis_body_task_push(context, top, if_statement->else_block.first_statement, bindings, task->scope_depth + 1, task->loop_depth);
        }
        else if (if_statement->alternative == AST_IF_ALTERNATIVE_IF)
        {
            analysis_body_task_push(context, top, if_statement->else_if, bindings, task->scope_depth + 1, task->loop_depth);
        }
        analysis_body_task_push(context, top, if_statement->then_block.first_statement, bindings, task->scope_depth + 1, task->loop_depth);
    }
    break;
    case AST_STATEMENT_SWITCH:
    {
        AstSwitchStatement* switch_statement = &statement->switch_statement;
        AnalysisTypedExpression* switched = analysis_expression(context, bindings, switch_statement->expression, invalid);
        analysis_body_task_continue(context, top, task, bindings);
        AstSwitchCase** cases = arena_allocate(context->scratch_arena, AstSwitchCase*, switch_statement->case_count);
        u32 case_count = 0;
        AnalysisConstant* case_constants = arena_allocate(context->scratch_arena, AnalysisConstant, switch_statement->case_count);
        u32 constant_count = 0;
        for (AstSwitchCase* switch_case = switch_statement->first_case; switch_case; switch_case = switch_case->next)
        {
            if (!switch_case->is_else)
            {
                AnalysisTypedExpression* case_expression = analysis_expression(context, bindings, switch_case->expression, switched->type);
                AnalysisConstant constant =
                    case_expression->ast.count ? case_expression->nodes[case_expression->ast.count - 1].constant : (AnalysisConstant){0};
                if (constant.kind == ANALYSIS_CONSTANT_NONE)
                {
                    analysis_body_diagnostic_push(context, switch_case->range, ANALYSIS_DIAGNOSTIC_INVALID_CONSTANT, (String8){0});
                }
                else
                {
                    bool duplicate = false;
                    for (u32 index = 0; index < constant_count; index += 1)
                    {
                        if (case_constants[index].kind == constant.kind && case_constants[index].integer == constant.integer &&
                            case_constants[index].is_negative == constant.is_negative)
                        {
                            duplicate = true;
                            analysis_body_diagnostic_push(context, switch_case->range, ANALYSIS_DIAGNOSTIC_DUPLICATE_SWITCH_CASE, (String8){0});
                            break;
                        }
                    }
                    if (!duplicate)
                    {
                        case_constants[constant_count] = constant;
                        constant_count += 1;
                    }
                }
            }
            cases[case_count] = switch_case;
            case_count += 1;
        }
        AnalysisType* switched_type = analysis_type_from_id(context->result, switched->type);
        if (switched_type->kind == ANALYSIS_TYPE_ENUM && !switch_statement->else_case)
        {
            AnalysisEntitySemantic* enum_semantic = analysis_declaration_semantic(context->result, switched_type->as.declaration, 0);
            BUSTER_CHECK(enum_semantic);
            if (constant_count < enum_semantic->enum_member_count)
            {
                analysis_body_diagnostic_push(context, statement->range, ANALYSIS_DIAGNOSTIC_NONEXHAUSTIVE_SWITCH, (String8){0});
            }
        }
        for (u32 index = case_count; index > 0; index -= 1)
        {
            analysis_body_task_push(context, top, cases[index - 1]->body.first_statement, bindings, task->scope_depth + 1, task->loop_depth);
        }
    }
    break;
    case AST_STATEMENT_FOR:
    {
        AstForStatement* for_statement = &statement->for_statement;
        AnalysisTypedExpression* iterable = analysis_expression(context, bindings, for_statement->iterable, invalid);
        AnalysisType* iterable_type = analysis_type_from_id(context->result, iterable->type);
        AnalysisTypeId element = context->result->types.builtin.poison;
        if (iterable_type->kind == ANALYSIS_TYPE_RANGE || iterable_type->kind == ANALYSIS_TYPE_SLICE || iterable_type->kind == ANALYSIS_TYPE_INFERRED_ARRAY)
        {
            element = iterable_type->as.element_type;
        }
        else if (iterable_type->kind == ANALYSIS_TYPE_ARRAY)
        {
            element = iterable_type->as.array.element_type;
        }
        else if (iterable_type->kind != ANALYSIS_TYPE_POISON)
        {
            analysis_body_diagnostic_push(context, statement->range, ANALYSIS_DIAGNOSTIC_INVALID_OPERAND, for_statement->name.text);
        }
        AnalysisTypeId declared = for_statement->type ? analysis_body_type_resolve(context, for_statement->type) : element;
        if (for_statement->type && !analysis_type_compatible(context->result, declared, element))
        {
            analysis_body_diagnostic_push(context, for_statement->name.range, ANALYSIS_DIAGNOSTIC_TYPE_MISMATCH, for_statement->name.text);
        }
        AnalysisBinding* body_bindings = bindings;
        analysis_local_add(context, &body_bindings, for_statement->name, declared, ANALYSIS_LOCAL_FOR, task->scope_depth + 1, false);
        analysis_body_task_continue(context, top, task, bindings);
        analysis_body_task_push(context, top, for_statement->body.first_statement, body_bindings, task->scope_depth + 1, task->loop_depth + 1);
    }
    break;
    case AST_STATEMENT_LOOP:
    {
        AstLoopStatement* loop = &statement->loop_statement;
        if (loop->has_condition)
        {
            analysis_expression(context, bindings, loop->condition, context->result->types.builtin.bool_type);
        }
        analysis_body_task_continue(context, top, task, bindings);
        analysis_body_task_push(context, top, loop->body.first_statement, bindings, task->scope_depth + 1, task->loop_depth + 1);
    }
    break;
    case AST_STATEMENT_BREAK:
    case AST_STATEMENT_CONTINUE:
    {
        if (!task->loop_depth)
        {
            analysis_body_diagnostic_push(context, statement->range, ANALYSIS_DIAGNOSTIC_INVALID_CONTROL_FLOW, (String8){0});
        }
        analysis_body_task_continue(context, top, task, bindings);
    }
    break;
    case AST_STATEMENT_COUNT:
        break;
    }
}

BUSTER_GLOBAL_LOCAL AnalysisFlow* analysis_flow_find(AnalysisFlowStatement* statements, u32 statement_count, AstStatement* statement)
{
    for (u32 index = 0; index < statement_count; index += 1)
    {
        if (statements[index].statement == statement)
        {
            return &statements[index].flow;
        }
    }
    return 0;
}

BUSTER_GLOBAL_LOCAL AnalysisFlow analysis_flow_block(AnalysisFlowStatement* statements, u32 statement_count, AstBlock* block)
{
    AnalysisFlow result = {.can_fall_through = true};
    for (AstStatement* statement = block->first_statement; statement && result.can_fall_through; statement = statement->next)
    {
        AnalysisFlow* flow = analysis_flow_find(statements, statement_count, statement);
        BUSTER_CHECK(flow);
        result.has_break = result.has_break || flow->has_break;
        result.can_fall_through = flow->can_fall_through;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool analysis_switch_is_exhaustive(AnalysisBodyContext* context, AstSwitchStatement* switch_statement)
{
    if (switch_statement->else_case)
    {
        return true;
    }
    AnalysisTypedExpression* switched = 0;
    for (AnalysisTypedExpression* expression = context->body->first_expression; expression; expression = expression->next)
    {
        if (expression->ast.nodes == switch_statement->expression.nodes)
        {
            switched = expression;
            break;
        }
    }
    if (!switched)
    {
        return false;
    }
    AnalysisType* type = analysis_type_from_id(context->result, switched->type);
    if (type->kind != ANALYSIS_TYPE_ENUM)
    {
        return false;
    }
    AnalysisEntitySemantic* semantic = analysis_declaration_semantic(context->result, type->as.declaration, 0);
    BUSTER_CHECK(semantic);
    bool* covered = arena_allocate(context->scratch_arena, bool, semantic->enum_member_count);
    for (u32 index = 0; index < semantic->enum_member_count; index += 1)
    {
        covered[index] = false;
    }
    for (AstSwitchCase* switch_case = switch_statement->first_case; switch_case; switch_case = switch_case->next)
    {
        if (switch_case->is_else)
        {
            continue;
        }
        AnalysisTypedExpression* case_expression = 0;
        for (AnalysisTypedExpression* expression = context->body->first_expression; expression; expression = expression->next)
        {
            if (expression->ast.nodes == switch_case->expression.nodes)
            {
                case_expression = expression;
                break;
            }
        }
        if (!case_expression || !case_expression->ast.count)
        {
            continue;
        }
        AnalysisConstant constant = case_expression->nodes[case_expression->ast.count - 1].constant;
        for (u32 member_index = 0; member_index < semantic->enum_member_count; member_index += 1)
        {
            if (constant.kind == ANALYSIS_CONSTANT_ENUM && constant.integer == semantic->enum_members[member_index].value)
            {
                covered[member_index] = true;
            }
        }
    }
    for (u32 index = 0; index < semantic->enum_member_count; index += 1)
    {
        if (!covered[index])
        {
            return false;
        }
    }
    return true;
}

BUSTER_GLOBAL_LOCAL void analysis_control_flow(AnalysisBodyContext* context, AnalysisTypeId return_type)
{
    AstBlock* root = &context->owner->ast.code->body;
    u32 maximum = 1;
    AnalysisAstStatementLink* count_top = 0;
    analysis_ast_statement_link_push(context->scratch_arena, &count_top, root->first_statement);
    while (count_top)
    {
        AstStatement* statement = count_top->statement;
        count_top = count_top->previous;
        maximum += 1;
        switch (statement->id)
        {
        case AST_STATEMENT_IF:
        {
            maximum += 1;
            analysis_ast_statement_link_push(context->scratch_arena, &count_top, statement->if_statement.then_block.first_statement);
            if (statement->if_statement.alternative == AST_IF_ALTERNATIVE_BLOCK)
            {
                maximum += 1;
                analysis_ast_statement_link_push(context->scratch_arena, &count_top, statement->if_statement.else_block.first_statement);
            }
            else if (statement->if_statement.alternative == AST_IF_ALTERNATIVE_IF)
            {
                analysis_ast_statement_link_push(context->scratch_arena, &count_top, statement->if_statement.else_if);
            }
        }
        break;
        case AST_STATEMENT_SWITCH:
        {
            for (AstSwitchCase* switch_case = statement->switch_statement.first_case; switch_case; switch_case = switch_case->next)
            {
                maximum += 1;
                analysis_ast_statement_link_push(context->scratch_arena, &count_top, switch_case->body.first_statement);
            }
        }
        break;
        case AST_STATEMENT_FOR:
        {
            maximum += 1;
            analysis_ast_statement_link_push(context->scratch_arena, &count_top, statement->for_statement.body.first_statement);
        }
        break;
        case AST_STATEMENT_LOOP:
        {
            maximum += 1;
            analysis_ast_statement_link_push(context->scratch_arena, &count_top, statement->loop_statement.body.first_statement);
        }
        break;
        case AST_STATEMENT_RETURN:
        case AST_STATEMENT_DATA:
        case AST_STATEMENT_EXPRESSION:
        case AST_STATEMENT_ASSIGNMENT:
        case AST_STATEMENT_BREAK:
        case AST_STATEMENT_CONTINUE:
        case AST_STATEMENT_COUNT:
            break;
        }
    }

    AnalysisFlowStatement* statements = arena_allocate(context->scratch_arena, AnalysisFlowStatement, maximum);
    AstBlock** blocks = arena_allocate(context->scratch_arena, AstBlock*, maximum);
    u32 statement_count = 0;
    u32 block_count = 1;
    blocks[0] = root;
    AnalysisAstStatementLink* top = 0;
    analysis_ast_statement_link_push(context->scratch_arena, &top, root->first_statement);
    while (top)
    {
        AstStatement* statement = top->statement;
        top = top->previous;
        statements[statement_count] = (AnalysisFlowStatement){
            .statement = statement,
            .flow = {.can_fall_through = true},
        };
        statement_count += 1;
        switch (statement->id)
        {
        case AST_STATEMENT_IF:
        {
            blocks[block_count] = &statement->if_statement.then_block;
            block_count += 1;
            analysis_ast_statement_link_push(context->scratch_arena, &top, statement->if_statement.then_block.first_statement);
            if (statement->if_statement.alternative == AST_IF_ALTERNATIVE_BLOCK)
            {
                blocks[block_count] = &statement->if_statement.else_block;
                block_count += 1;
                analysis_ast_statement_link_push(context->scratch_arena, &top, statement->if_statement.else_block.first_statement);
            }
            else if (statement->if_statement.alternative == AST_IF_ALTERNATIVE_IF)
            {
                analysis_ast_statement_link_push(context->scratch_arena, &top, statement->if_statement.else_if);
            }
        }
        break;
        case AST_STATEMENT_SWITCH:
        {
            for (AstSwitchCase* switch_case = statement->switch_statement.first_case; switch_case; switch_case = switch_case->next)
            {
                blocks[block_count] = &switch_case->body;
                block_count += 1;
                analysis_ast_statement_link_push(context->scratch_arena, &top, switch_case->body.first_statement);
            }
        }
        break;
        case AST_STATEMENT_FOR:
        {
            blocks[block_count] = &statement->for_statement.body;
            block_count += 1;
            analysis_ast_statement_link_push(context->scratch_arena, &top, statement->for_statement.body.first_statement);
        }
        break;
        case AST_STATEMENT_LOOP:
        {
            blocks[block_count] = &statement->loop_statement.body;
            block_count += 1;
            analysis_ast_statement_link_push(context->scratch_arena, &top, statement->loop_statement.body.first_statement);
        }
        break;
        case AST_STATEMENT_RETURN:
        case AST_STATEMENT_DATA:
        case AST_STATEMENT_EXPRESSION:
        case AST_STATEMENT_ASSIGNMENT:
        case AST_STATEMENT_BREAK:
        case AST_STATEMENT_CONTINUE:
        case AST_STATEMENT_COUNT:
            break;
        }
    }
    BUSTER_CHECK(statement_count < maximum);
    BUSTER_CHECK(block_count <= maximum);

    for (u32 pass = 0; pass < statement_count + 1; pass += 1)
    {
        for (u32 index = statement_count; index > 0; index -= 1)
        {
            AstStatement* statement = statements[index - 1].statement;
            AnalysisFlow flow = {.can_fall_through = true};
            switch (statement->id)
            {
            case AST_STATEMENT_RETURN:
            case AST_STATEMENT_CONTINUE:
            {
                flow.can_fall_through = false;
            }
            break;
            case AST_STATEMENT_BREAK:
            {
                flow.can_fall_through = false;
                flow.has_break = true;
            }
            break;
            case AST_STATEMENT_IF:
            {
                AnalysisFlow then_flow = analysis_flow_block(statements, statement_count, &statement->if_statement.then_block);
                AnalysisFlow else_flow = {.can_fall_through = true};
                if (statement->if_statement.alternative == AST_IF_ALTERNATIVE_BLOCK)
                {
                    else_flow = analysis_flow_block(statements, statement_count, &statement->if_statement.else_block);
                }
                else if (statement->if_statement.alternative == AST_IF_ALTERNATIVE_IF)
                {
                    AnalysisFlow* nested = analysis_flow_find(statements, statement_count, statement->if_statement.else_if);
                    BUSTER_CHECK(nested);
                    else_flow = *nested;
                }
                flow.can_fall_through = then_flow.can_fall_through || else_flow.can_fall_through;
                flow.has_break = then_flow.has_break || else_flow.has_break;
            }
            break;
            case AST_STATEMENT_SWITCH:
            {
                flow.can_fall_through = !analysis_switch_is_exhaustive(context, &statement->switch_statement);
                for (AstSwitchCase* switch_case = statement->switch_statement.first_case; switch_case; switch_case = switch_case->next)
                {
                    AnalysisFlow case_flow = analysis_flow_block(statements, statement_count, &switch_case->body);
                    flow.can_fall_through = flow.can_fall_through || case_flow.can_fall_through;
                    flow.has_break = flow.has_break || case_flow.has_break;
                }
            }
            break;
            case AST_STATEMENT_LOOP:
            {
                AnalysisFlow loop_flow = analysis_flow_block(statements, statement_count, &statement->loop_statement.body);
                flow.can_fall_through = statement->loop_statement.has_condition || loop_flow.has_break;
            }
            break;
            case AST_STATEMENT_FOR:
            case AST_STATEMENT_DATA:
            case AST_STATEMENT_EXPRESSION:
            case AST_STATEMENT_ASSIGNMENT:
                break;
            case AST_STATEMENT_COUNT:
                break;
            }
            statements[index - 1].flow = flow;
        }
    }

    for (u32 block_index = 0; block_index < block_count; block_index += 1)
    {
        bool reachable = true;
        for (AstStatement* statement = blocks[block_index]->first_statement; statement; statement = statement->next)
        {
            if (!reachable)
            {
                context->body->has_unreachable = true;
                analysis_body_diagnostic_push(context, statement->range, ANALYSIS_DIAGNOSTIC_UNREACHABLE_STATEMENT, (String8){0});
            }
            AnalysisFlow* flow = analysis_flow_find(statements, statement_count, statement);
            BUSTER_CHECK(flow);
            reachable = reachable && flow->can_fall_through;
        }
    }
    AnalysisFlow root_flow = analysis_flow_block(statements, statement_count, root);
    context->body->can_fall_through = root_flow.can_fall_through;
    if (analysis_type_from_id(context->result, return_type)->kind != ANALYSIS_TYPE_VOID && root_flow.can_fall_through)
    {
        analysis_body_diagnostic_push(context, context->owner->ast.code->body.range, ANALYSIS_DIAGNOSTIC_MISSING_RETURN, context->owner->name);
    }
}

typedef struct AnalysisInitializationLoop AnalysisInitializationLoop;
typedef struct AnalysisInitializationBreak AnalysisInitializationBreak;
struct AnalysisInitializationBreak
{
    AnalysisInitializationBreak* next;
    bool* state;
};

struct AnalysisInitializationLoop
{
    AnalysisInitializationBreak* first_break;
    AnalysisInitializationBreak* last_break;
    bool* input_state;
    bool include_input;
    u8 reserved[7];
};

typedef struct AnalysisInitializationPath AnalysisInitializationPath;
struct AnalysisInitializationPath
{
    bool* state;
    AnalysisInitializationLoop* loop;
    bool falls_through;
    u8 reserved[7];
};

typedef enum AnalysisInitializationTaskKind
{
    ANALYSIS_INITIALIZATION_TASK_STATEMENT,
    ANALYSIS_INITIALIZATION_TASK_MERGE,
    ANALYSIS_INITIALIZATION_TASK_SWITCH_MERGE,
    ANALYSIS_INITIALIZATION_TASK_LOOP_MERGE,
} AnalysisInitializationTaskKind;

typedef struct AnalysisInitializationTask AnalysisInitializationTask;
struct AnalysisInitializationTask
{
    AnalysisInitializationTask* previous;
    AstStatement* statement;
    AnalysisInitializationPath* path;
    AnalysisInitializationPath* left;
    AnalysisInitializationPath* right;
    AnalysisInitializationLoop* loop;
    AnalysisInitializationPath** paths;
    bool* input_state;
    u32 path_count;
    bool include_input;
    u8 reserved[3];
    AnalysisInitializationTaskKind kind;
};

BUSTER_GLOBAL_LOCAL bool* analysis_initialization_state_copy(AnalysisBodyContext* context, bool* source)
{
    bool* result = arena_allocate(context->scratch_arena, bool, context->body->local_count);
    memcpy(result, source, sizeof(*result) * context->body->local_count);
    return result;
}

BUSTER_GLOBAL_LOCAL AnalysisTypedExpression* analysis_expression_find(AnalysisBody* body, AstExpression ast)
{
    for (AnalysisTypedExpression* expression = body->first_expression; expression; expression = expression->next)
    {
        if (expression->ast.nodes == ast.nodes && expression->ast.count == ast.count)
        {
            return expression;
        }
    }
    return 0;
}

BUSTER_GLOBAL_LOCAL void analysis_initialization_diagnostic_push(AnalysisBodyContext* context, AnalysisLocal* local, ParserSourceRange use_range)
{
    analysis_body_diagnostic_push(context, use_range, ANALYSIS_DIAGNOSTIC_USE_BEFORE_INITIALIZATION, local->name);
    AnalysisDiagnosticNote* note = arena_allocate(context->result_arena, AnalysisDiagnosticNote, 1);
    *note = (AnalysisDiagnosticNote){
        .message = S8("local is declared uninitialized here"),
        .range = local->range,
        .entity = context->owner->id,
        .source = context->owner->source,
    };
    context->result->last_diagnostic->first_note = note;
    context->result->last_diagnostic->last_note = note;
}

BUSTER_GLOBAL_LOCAL void analysis_initialization_expression(AnalysisBodyContext* context, AstExpression ast, bool* state, AnalysisLocalId ignored_place)
{
    AnalysisTypedExpression* expression = analysis_expression_find(context->body, ast);
    if (!expression)
    {
        return;
    }
    if (ignored_place.value == ANALYSIS_ID_UNDERLYING_INVALID && expression->ast.count > 1 &&
        expression->ast.nodes[expression->ast.count - 1].id == AST_NODE_ADDRESS_OF)
    {
        ignored_place = expression->nodes[expression->ast.count - 2].local;
    }
    bool* reported = arena_allocate(context->scratch_arena, bool, context->body->local_count);
    memset(reported, 0, sizeof(*reported) * context->body->local_count);
    for (u32 index = 0; index < expression->ast.count; index += 1)
    {
        AstNode* node = expression->ast.nodes + index;
        AnalysisTypedNode* typed = expression->nodes + index;
        if (node->id != AST_NODE_IDENTIFIER || typed->local.value == ANALYSIS_ID_UNDERLYING_INVALID || typed->local.value == ignored_place.value)
        {
            continue;
        }
        BUSTER_CHECK(typed->local.value < context->body->local_count);
        if (!state[typed->local.value] && !reported[typed->local.value])
        {
            reported[typed->local.value] = true;
            analysis_initialization_diagnostic_push(context, context->body->locals + typed->local.value, node->identifier.range);
        }
    }
}

BUSTER_GLOBAL_LOCAL AnalysisLocal* analysis_initialization_declaration_local(AnalysisBodyContext* context, AstDataStatement* data)
{
    for (u32 index = 0; index < context->body->local_count; index += 1)
    {
        AnalysisLocal* local = context->body->locals + index;
        if (local->kind == ANALYSIS_LOCAL_DATA && local->range.offset == data->name.range.offset && string_equal(local->name, data->name.text))
        {
            return local;
        }
    }
    return 0;
}

BUSTER_GLOBAL_LOCAL void analysis_initialization_task_push(AnalysisBodyContext* context, AnalysisInitializationTask** top, AnalysisInitializationTask task)
{
    AnalysisInitializationTask* pushed = arena_allocate(context->scratch_arena, AnalysisInitializationTask, 1);
    *pushed = task;
    pushed->previous = *top;
    *top = pushed;
}

BUSTER_GLOBAL_LOCAL void analysis_definite_initialization(AnalysisBodyContext* context)
{
    bool* initial = arena_allocate(context->scratch_arena, bool, context->body->local_count);
    memset(initial, 0, sizeof(*initial) * context->body->local_count);
    for (u32 local_index = 0; local_index < context->body->local_count; local_index += 1)
    {
        AnalysisLocal* local = context->body->locals + local_index;
        initial[local_index] = local->kind == ANALYSIS_LOCAL_ARGUMENT;
    }
    AnalysisInitializationPath root = {.state = initial, .falls_through = true};
    AnalysisInitializationTask* top = 0;
    analysis_initialization_task_push(context, &top,
                                      (AnalysisInitializationTask){
                                          .statement = context->owner->ast.code->body.first_statement,
                                          .path = &root,
                                          .kind = ANALYSIS_INITIALIZATION_TASK_STATEMENT,
                                      });
    while (top)
    {
        AnalysisInitializationTask* task = top;
        top = task->previous;
        if (task->kind == ANALYSIS_INITIALIZATION_TASK_MERGE)
        {
            task->path->falls_through = task->left->falls_through || task->right->falls_through;
            if (task->path->falls_through)
            {
                for (u32 local = 0; local < context->body->local_count; local += 1)
                {
                    bool initialized = true;
                    if (task->left->falls_through)
                    {
                        initialized &= task->left->state[local];
                    }
                    if (task->right->falls_through)
                    {
                        initialized &= task->right->state[local];
                    }
                    task->path->state[local] = initialized;
                }
            }
            continue;
        }
        if (task->kind == ANALYSIS_INITIALIZATION_TASK_SWITCH_MERGE)
        {
            bool any_falls_through = task->include_input;
            for (u32 path_index = 0; path_index < task->path_count; path_index += 1)
            {
                any_falls_through |= task->paths[path_index]->falls_through;
            }
            task->path->falls_through = any_falls_through;
            for (u32 local = 0; local < context->body->local_count; local += 1)
            {
                bool initialized = true;
                if (task->include_input)
                {
                    initialized &= task->input_state[local];
                }
                for (u32 path_index = 0; path_index < task->path_count; path_index += 1)
                {
                    if (task->paths[path_index]->falls_through)
                    {
                        initialized &= task->paths[path_index]->state[local];
                    }
                }
                task->path->state[local] = initialized;
            }
            continue;
        }
        if (task->kind == ANALYSIS_INITIALIZATION_TASK_LOOP_MERGE)
        {
            bool falls_through = task->loop->include_input || task->loop->first_break != 0;
            task->path->falls_through = falls_through;
            for (u32 local = 0; local < context->body->local_count; local += 1)
            {
                bool initialized = true;
                if (task->loop->include_input)
                {
                    initialized &= task->loop->input_state[local];
                }
                for (AnalysisInitializationBreak* loop_break = task->loop->first_break; loop_break; loop_break = loop_break->next)
                {
                    initialized &= loop_break->state[local];
                }
                task->path->state[local] = initialized;
            }
            continue;
        }
        if (!task->statement || !task->path->falls_through)
        {
            continue;
        }
        AstStatement* statement = task->statement;
        AnalysisInitializationTask continuation = {
            .statement = statement->next,
            .path = task->path,
            .kind = ANALYSIS_INITIALIZATION_TASK_STATEMENT,
        };
        switch (statement->id)
        {
        case AST_STATEMENT_RETURN:
        {
            analysis_initialization_expression(context, statement->return_statement.expression, task->path->state, ANALYSIS_LOCAL_ID_INVALID);
            task->path->falls_through = false;
        }
        break;
        case AST_STATEMENT_DATA:
        {
            AstDataStatement* data = &statement->data_statement;
            bool undefined = data->initializer.count == 1 && data->initializer.nodes[0].id == AST_NODE_UNDEFINED;
            if (!undefined)
            {
                analysis_initialization_expression(context, data->initializer, task->path->state, ANALYSIS_LOCAL_ID_INVALID);
            }
            AnalysisLocal* local = analysis_initialization_declaration_local(context, data);
            BUSTER_CHECK(local);
            task->path->state[local->id.value] = !undefined;
            analysis_initialization_task_push(context, &top, continuation);
        }
        break;
        case AST_STATEMENT_EXPRESSION:
        {
            analysis_initialization_expression(context, statement->expression_statement.expression, task->path->state, ANALYSIS_LOCAL_ID_INVALID);
            analysis_initialization_task_push(context, &top, continuation);
        }
        break;
        case AST_STATEMENT_ASSIGNMENT:
        {
            AstAssignmentStatement* assignment = &statement->assignment_statement;
            AnalysisTypedExpression* target = analysis_expression_find(context->body, assignment->target);
            AnalysisLocalId place = ANALYSIS_LOCAL_ID_INVALID;
            if (target && target->ast.count)
            {
                place = target->nodes[target->ast.count - 1].local;
            }
            analysis_initialization_expression(context, assignment->target, task->path->state,
                                               assignment->operator== AST_ASSIGNMENT_EQUAL ? place : ANALYSIS_LOCAL_ID_INVALID);
            analysis_initialization_expression(context, assignment->value, task->path->state, ANALYSIS_LOCAL_ID_INVALID);
            if (assignment->operator== AST_ASSIGNMENT_EQUAL && target && target->ast.count && target->ast.nodes[target->ast.count - 1]
                    .id == AST_NODE_IDENTIFIER && place.value != ANALYSIS_ID_UNDERLYING_INVALID)
            {
                task->path->state[place.value] = true;
            }
            analysis_initialization_task_push(context, &top, continuation);
        }
        break;
        case AST_STATEMENT_IF:
        {
            AstIfStatement* conditional = &statement->if_statement;
            analysis_initialization_expression(context, conditional->condition, task->path->state, ANALYSIS_LOCAL_ID_INVALID);
            AnalysisInitializationPath* then_path = arena_allocate(context->scratch_arena, AnalysisInitializationPath, 1);
            *then_path = (AnalysisInitializationPath){
                .state = analysis_initialization_state_copy(context, task->path->state),
                .loop = task->path->loop,
                .falls_through = true,
            };
            AnalysisInitializationPath* else_path = arena_allocate(context->scratch_arena, AnalysisInitializationPath, 1);
            *else_path = (AnalysisInitializationPath){
                .state = analysis_initialization_state_copy(context, task->path->state),
                .loop = task->path->loop,
                .falls_through = true,
            };
            analysis_initialization_task_push(context, &top, continuation);
            analysis_initialization_task_push(context, &top,
                                              (AnalysisInitializationTask){
                                                  .path = task->path,
                                                  .left = then_path,
                                                  .right = else_path,
                                                  .kind = ANALYSIS_INITIALIZATION_TASK_MERGE,
                                              });
            if (conditional->alternative != AST_IF_ALTERNATIVE_NONE)
            {
                AstStatement* alternative =
                    conditional->alternative == AST_IF_ALTERNATIVE_BLOCK ? conditional->else_block.first_statement : conditional->else_if;
                analysis_initialization_task_push(context, &top,
                                                  (AnalysisInitializationTask){
                                                      .statement = alternative,
                                                      .path = else_path,
                                                      .kind = ANALYSIS_INITIALIZATION_TASK_STATEMENT,
                                                  });
            }
            analysis_initialization_task_push(context, &top,
                                              (AnalysisInitializationTask){
                                                  .statement = conditional->then_block.first_statement,
                                                  .path = then_path,
                                                  .kind = ANALYSIS_INITIALIZATION_TASK_STATEMENT,
                                              });
        }
        break;
        case AST_STATEMENT_SWITCH:
        {
            AstSwitchStatement* switch_statement = &statement->switch_statement;
            analysis_initialization_expression(context, switch_statement->expression, task->path->state, ANALYSIS_LOCAL_ID_INVALID);
            AnalysisInitializationPath** paths = arena_allocate(context->scratch_arena, AnalysisInitializationPath*, switch_statement->case_count);
            AstSwitchCase** cases = arena_allocate(context->scratch_arena, AstSwitchCase*, switch_statement->case_count);
            u32 case_count = 0;
            for (AstSwitchCase* switch_case = switch_statement->first_case; switch_case; switch_case = switch_case->next)
            {
                cases[case_count] = switch_case;
                paths[case_count] = arena_allocate(context->scratch_arena, AnalysisInitializationPath, 1);
                *paths[case_count] = (AnalysisInitializationPath){
                    .state = analysis_initialization_state_copy(context, task->path->state),
                    .loop = task->path->loop,
                    .falls_through = true,
                };
                if (!switch_case->is_else)
                {
                    analysis_initialization_expression(context, switch_case->expression, task->path->state, ANALYSIS_LOCAL_ID_INVALID);
                }
                case_count += 1;
            }
            analysis_initialization_task_push(context, &top, continuation);
            analysis_initialization_task_push(context, &top,
                                              (AnalysisInitializationTask){
                                                  .path = task->path,
                                                  .paths = paths,
                                                  .input_state = task->path->state,
                                                  .path_count = case_count,
                                                  .include_input = !switch_statement->else_case,
                                                  .kind = ANALYSIS_INITIALIZATION_TASK_SWITCH_MERGE,
                                              });
            for (u32 case_index = case_count; case_index > 0; case_index -= 1)
            {
                analysis_initialization_task_push(context, &top,
                                                  (AnalysisInitializationTask){
                                                      .statement = cases[case_index - 1]->body.first_statement,
                                                      .path = paths[case_index - 1],
                                                      .kind = ANALYSIS_INITIALIZATION_TASK_STATEMENT,
                                                  });
            }
        }
        break;
        case AST_STATEMENT_FOR:
        {
            analysis_initialization_expression(context, statement->for_statement.iterable, task->path->state, ANALYSIS_LOCAL_ID_INVALID);
            AnalysisInitializationPath* body_path = arena_allocate(context->scratch_arena, AnalysisInitializationPath, 1);
            *body_path = (AnalysisInitializationPath){
                .state = analysis_initialization_state_copy(context, task->path->state),
                .falls_through = true,
            };
            AnalysisInitializationLoop* loop = arena_allocate(context->scratch_arena, AnalysisInitializationLoop, 1);
            *loop = (AnalysisInitializationLoop){
                .input_state = task->path->state,
                .include_input = true,
            };
            body_path->loop = loop;
            for (u32 local = 0; local < context->body->local_count; local += 1)
            {
                if (context->body->locals[local].kind == ANALYSIS_LOCAL_FOR &&
                    context->body->locals[local].range.offset == statement->for_statement.name.range.offset)
                {
                    body_path->state[local] = true;
                }
            }
            analysis_initialization_task_push(context, &top, continuation);
            analysis_initialization_task_push(context, &top,
                                              (AnalysisInitializationTask){
                                                  .path = task->path,
                                                  .loop = loop,
                                                  .kind = ANALYSIS_INITIALIZATION_TASK_LOOP_MERGE,
                                              });
            analysis_initialization_task_push(context, &top,
                                              (AnalysisInitializationTask){
                                                  .statement = statement->for_statement.body.first_statement,
                                                  .path = body_path,
                                                  .kind = ANALYSIS_INITIALIZATION_TASK_STATEMENT,
                                              });
        }
        break;
        case AST_STATEMENT_LOOP:
        {
            if (statement->loop_statement.has_condition)
            {
                analysis_initialization_expression(context, statement->loop_statement.condition, task->path->state, ANALYSIS_LOCAL_ID_INVALID);
            }
            AnalysisInitializationPath* body_path = arena_allocate(context->scratch_arena, AnalysisInitializationPath, 1);
            *body_path = (AnalysisInitializationPath){
                .state = analysis_initialization_state_copy(context, task->path->state),
                .falls_through = true,
            };
            AnalysisInitializationLoop* loop = arena_allocate(context->scratch_arena, AnalysisInitializationLoop, 1);
            *loop = (AnalysisInitializationLoop){
                .input_state = task->path->state,
                .include_input = statement->loop_statement.has_condition,
            };
            body_path->loop = loop;
            analysis_initialization_task_push(context, &top, continuation);
            analysis_initialization_task_push(context, &top,
                                              (AnalysisInitializationTask){
                                                  .path = task->path,
                                                  .loop = loop,
                                                  .kind = ANALYSIS_INITIALIZATION_TASK_LOOP_MERGE,
                                              });
            analysis_initialization_task_push(context, &top,
                                              (AnalysisInitializationTask){
                                                  .statement = statement->loop_statement.body.first_statement,
                                                  .path = body_path,
                                                  .kind = ANALYSIS_INITIALIZATION_TASK_STATEMENT,
                                              });
        }
        break;
        case AST_STATEMENT_BREAK:
        {
            if (task->path->loop)
            {
                AnalysisInitializationBreak* loop_break = arena_allocate(context->scratch_arena, AnalysisInitializationBreak, 1);
                *loop_break = (AnalysisInitializationBreak){
                    .state = analysis_initialization_state_copy(context, task->path->state),
                };
                if (task->path->loop->last_break)
                {
                    task->path->loop->last_break->next = loop_break;
                }
                else
                {
                    task->path->loop->first_break = loop_break;
                }
                task->path->loop->last_break = loop_break;
            }
            task->path->falls_through = false;
        }
        break;
        case AST_STATEMENT_CONTINUE:
        {
            task->path->falls_through = false;
        }
        break;
        case AST_STATEMENT_COUNT:
            break;
        }
    }
}

BUSTER_GLOBAL_LOCAL void analysis_analyze_module_constants(Arena* result_arena, Arena* scratch_arena, AnalysisResult* result)
{
    for (u32 entity_index = 0; entity_index < result->module.entity_count; entity_index += 1)
    {
        AnalysisEntity* entity = result->module.entities + entity_index;
        if (entity->kind != ANALYSIS_ENTITY_DATA)
        {
            continue;
        }
        AnalysisEntitySemantic* semantic = result->module.semantics + entity_index;
        if (semantic->constant)
        {
            continue;
        }
        AnalysisBody* body = result->module.bodies + entity_index;
        body->dependency_capacity = entity->ast.data->initializer.count;
        body->dependencies = arena_allocate(result_arena, AnalysisEntityId, body->dependency_capacity);
        AnalysisBodyContext context = {
            .result_arena = result_arena,
            .scratch_arena = scratch_arena,
            .result = result,
            .owner = entity,
            .body = body,
        };
        context.resolution = (AnalysisResolutionContext){
            .result_arena = result_arena,
            .scratch_arena = scratch_arena,
            .result = result,
        };
        AnalysisTypedExpression* expression = analysis_expression(&context, 0, entity->ast.data->initializer, semantic->type);
        semantic->constant = arena_allocate(result_arena, AnalysisConstant, 1);
        if (expression->ast.count)
        {
            *semantic->constant = expression->nodes[expression->ast.count - 1].constant;
        }
        if (semantic->constant->kind == ANALYSIS_CONSTANT_NONE)
        {
            analysis_entity_diagnostic_push(result_arena, result, entity, entity->ast.data->range, ANALYSIS_DIAGNOSTIC_INVALID_CONSTANT, entity->name,
                                            S8("compile-time data initializer is not a constant"));
        }
    }
}

BUSTER_GLOBAL_LOCAL void analysis_analyze_code_body(Arena* result_arena, Arena* scratch_arena, AnalysisResult* result, AnalysisEntity* entity,
                                                    AnalysisBody* body, AnalysisInstantiation* instantiation)
{
    u32 expression_node_count = 0;
    analysis_body_capacity_measure(scratch_arena, entity->ast.code, &body->local_capacity, &expression_node_count);
    body->locals = arena_allocate(result_arena, AnalysisLocal, body->local_capacity);
    body->dependency_capacity = expression_node_count;
    body->dependencies = arena_allocate(result_arena, AnalysisEntityId, body->dependency_capacity);
    AnalysisBodyContext context = {
        .result_arena = result_arena,
        .scratch_arena = scratch_arena,
        .result = result,
        .owner = entity,
        .body = body,
        .instantiation = instantiation,
    };
    context.resolution = (AnalysisResolutionContext){
        .result_arena = result_arena,
        .scratch_arena = scratch_arena,
        .result = result,
    };
    AnalysisTypeId function_type = instantiation ? instantiation->function_type : result->module.semantics[entity->id.index.value].type;
    AnalysisType* function = analysis_type_from_id(result, function_type);
    if (function->kind != ANALYSIS_TYPE_FUNCTION)
    {
        body->analyzed = true;
        return;
    }

    AnalysisBinding* bindings = 0;
    u32 source_argument_index = 0;
    u32 runtime_argument_index = 0;
    for (AstTypeArgument* argument = entity->ast.code->type->function.first_argument; argument; argument = argument->next, source_argument_index += 1)
    {
        AnalysisTypeId argument_type = result->module.semantics[entity->id.index.value].type;
        AnalysisType* generic_function = analysis_type_from_id(result, argument_type);
        argument_type = generic_function->as.function.argument_types[source_argument_index];
        if (instantiation)
        {
            argument_type = argument->is_compile_time ? analysis_type_substitute(result_arena, scratch_arena, result, argument_type,
                                                                                 instantiation->type_bindings, instantiation->type_binding_count)
                                                      : function->as.function.argument_types[runtime_argument_index++];
        }
        AstIdentifier identifier = {.text = argument->name, .range = argument->range};
        AnalysisLocal* local = analysis_local_add(&context, &bindings, identifier, argument_type, ANALYSIS_LOCAL_ARGUMENT, 0, argument->is_compile_time);
        if (instantiation && argument->is_compile_time)
        {
            for (u32 index = 0; index < instantiation->compile_time_argument_count; index += 1)
            {
                AnalysisCompileTimeArgument* compile_time = instantiation->compile_time_arguments + index;
                if (compile_time->source_argument_index == source_argument_index)
                {
                    local->constant = compile_time->constant;
                    break;
                }
            }
        }
    }
    BUSTER_CHECK(!instantiation || runtime_argument_index == function->as.function.argument_count);

    AnalysisBodyTask* top = 0;
    analysis_body_task_push(&context, &top, entity->ast.code->body.first_statement, bindings, 0, 0);
    while (top)
    {
        AnalysisBodyTask* task = top;
        top = task->previous;
        analysis_body_statement(&context, &top, task, function->as.function.return_type);
    }
    analysis_control_flow(&context, function->as.function.return_type);
    analysis_definite_initialization(&context);
    body->analyzed = true;
    if (instantiation)
    {
        instantiation->analyzed = true;
    }
}

void analysis_analyze_bodies_with_consumer(Arena* result_arena, AnalysisResult* result, AnalysisBodyConsumer* consumer, void* user_data)
{
    BUSTER_CHECK(result->types.types != 0);
    Arena* conflicts[] = {result_arena};
    TemporalArena scratch = scratch_begin(conflicts, BUSTER_ARRAY_LENGTH(conflicts));
    analysis_analyze_module_constants(result_arena, scratch.arena, result);
    for (u32 entity_index = 0; entity_index < result->module.entity_count; entity_index += 1)
    {
        AnalysisEntity* entity = result->module.entities + entity_index;
        if (entity->kind != ANALYSIS_ENTITY_CODE || !entity->ast.code->has_body)
        {
            continue;
        }
        if (analysis_entity_is_generic(scratch.arena, result, entity))
        {
            continue;
        }
        AnalysisBody* body = result->module.bodies + entity_index;
        if (!body->analyzed)
        {
            analysis_analyze_code_body(result_arena, scratch.arena, result, entity, body, 0);
            if (consumer)
            {
                consumer(result_arena, scratch.arena, result, entity_index, user_data);
            }
        }
    }
    for (AnalysisInstantiation* instantiation = result->first_instantiation; instantiation; instantiation = instantiation->next)
    {
        AnalysisEntity* entity = result->module.entities + instantiation->generic_entity.index.value;
        if (entity->ast.code->has_body && !instantiation->analyzed)
        {
            analysis_analyze_code_body(result_arena, scratch.arena, result, entity, &instantiation->body, instantiation);
        }
    }
    scratch_end(scratch);
}

void analysis_analyze_bodies(Arena* result_arena, AnalysisResult* result)
{
    analysis_analyze_bodies_with_consumer(result_arena, result, 0, 0);
}

BUSTER_GLOBAL_LOCAL u64 analysis_layout_align(u64 value, u32 alignment)
{
    if (!alignment || (alignment & (alignment - 1)) || value > UINT64_MAX - (alignment - 1))
    {
        return UINT64_MAX;
    }
    return (value + alignment - 1) & ~((u64)alignment - 1);
}

BUSTER_GLOBAL_LOCAL bool analysis_layout_product(u64 left, u64 right, u64* result)
{
    if (right && left > UINT64_MAX / right)
    {
        return false;
    }
    *result = left * right;
    return true;
}

BUSTER_GLOBAL_LOCAL bool analysis_layout_sum(u64 left, u64 right, u64* result)
{
    if (left > UINT64_MAX - right)
    {
        return false;
    }
    *result = left + right;
    return true;
}

BUSTER_GLOBAL_LOCAL bool analysis_layout_alignment_valid(u64 alignment)
{
    return alignment && alignment <= UINT32_MAX && !(alignment & (alignment - 1));
}

void analysis_compute_layouts(AnalysisResult* result, AnalysisLayoutOptions options)
{
    TargetDataLayout data_layout = options.data_layout;
    if (!target_data_layout_is_valid(data_layout))
    {
        data_layout = target_data_layout(target_native);
        if (options.pointer_size)
        {
            data_layout.pointer.size = options.pointer_size;
            data_layout.pointer.bit_width = options.pointer_size * 8;
        }
        if (options.pointer_alignment)
        {
            data_layout.pointer.alignment = options.pointer_alignment;
        }
    }
    options.pointer_size = data_layout.pointer.size;
    options.pointer_alignment = data_layout.pointer.alignment;
    BUSTER_CHECK(options.pointer_size && options.pointer_alignment);
    result->data_layout = data_layout;
    for (u32 index = 0; index < result->types.count; index += 1)
    {
        result->types.types[index].layout = (AnalysisTypeLayout){0};
    }
    for (u32 pass = 0; pass < result->types.count + 1; pass += 1)
    {
        bool progressed = false;
        for (u32 index = 0; index < result->types.count; index += 1)
        {
            AnalysisType* type = result->types.types + index;
            if (type->layout.state == ANALYSIS_LAYOUT_RESOLVED || type->layout.state == ANALYSIS_LAYOUT_ERROR)
            {
                continue;
            }
            AnalysisTypeLayout layout = {.state = ANALYSIS_LAYOUT_RESOLVED};
            bool ready = true;
            switch (type->kind)
            {
            case ANALYSIS_TYPE_POISON:
            case ANALYSIS_TYPE_COMPILE_TIME_PARAMETER:
            {
                layout.state = ANALYSIS_LAYOUT_ERROR;
            }
            break;
            case ANALYSIS_TYPE_VOID:
            {
                layout.alignment = 1;
                layout.abi_class = ANALYSIS_ABI_CLASS_NONE;
            }
            break;
            case ANALYSIS_TYPE_BOOL:
            {
                layout.size = 1;
                layout.alignment = 1;
                layout.abi_class = ANALYSIS_ABI_CLASS_INTEGER;
            }
            break;
            case ANALYSIS_TYPE_INTEGER:
            {
                layout.size = type->as.integer.bit_width / 8;
                if (!layout.size || !analysis_layout_alignment_valid(layout.size))
                {
                    layout.state = ANALYSIS_LAYOUT_ERROR;
                }
                else
                {
                    layout.alignment = (u32)layout.size;
                    layout.abi_class = ANALYSIS_ABI_CLASS_INTEGER;
                }
            }
            break;
            case ANALYSIS_TYPE_FLOAT:
            {
                layout.size = type->as.float_bit_width / 8;
                if (!layout.size || !analysis_layout_alignment_valid(layout.size))
                {
                    layout.state = ANALYSIS_LAYOUT_ERROR;
                }
                else
                {
                    layout.alignment = (u32)layout.size;
                    layout.abi_class = ANALYSIS_ABI_CLASS_FLOAT;
                }
            }
            break;
            case ANALYSIS_TYPE_VA_LIST:
            {
                layout.size = data_layout.va_list.size;
                layout.alignment = data_layout.va_list.alignment;
                layout.abi_class = ANALYSIS_ABI_CLASS_MEMORY;
            }
            break;
            case ANALYSIS_TYPE_POINTER:
            case ANALYSIS_TYPE_FUNCTION:
            {
                layout.size = options.pointer_size;
                layout.alignment = options.pointer_alignment;
                layout.abi_class = ANALYSIS_ABI_CLASS_POINTER;
            }
            break;
            case ANALYSIS_TYPE_SLICE:
            {
                if (!analysis_layout_product(options.pointer_size, 2, &layout.size))
                {
                    layout.state = ANALYSIS_LAYOUT_ERROR;
                }
                else
                {
                    layout.alignment = options.pointer_alignment;
                    layout.abi_class = ANALYSIS_ABI_CLASS_AGGREGATE;
                }
            }
            break;
            case ANALYSIS_TYPE_INFERRED_ARRAY:
            {
                layout.state = ANALYSIS_LAYOUT_ERROR;
            }
            break;
            case ANALYSIS_TYPE_ARRAY:
            {
                AnalysisTypeLayout element = analysis_type_from_id(result, type->as.array.element_type)->layout;
                if (element.state == ANALYSIS_LAYOUT_RESOLVED)
                {
                    if (!analysis_layout_product(element.size, type->as.array.count, &layout.size))
                    {
                        layout.state = ANALYSIS_LAYOUT_ERROR;
                    }
                    else
                    {
                        layout.alignment = element.alignment;
                        layout.abi_class = layout.size <= 16 ? ANALYSIS_ABI_CLASS_AGGREGATE : ANALYSIS_ABI_CLASS_MEMORY;
                    }
                }
                else if (element.state == ANALYSIS_LAYOUT_ERROR)
                {
                    layout.state = ANALYSIS_LAYOUT_ERROR;
                }
                else
                {
                    ready = false;
                }
            }
            break;
            case ANALYSIS_TYPE_VECTOR:
            {
                AnalysisType* element_type = analysis_type_from_id(result, type->as.vector.element_type);
                AnalysisTypeLayout element = element_type->layout;
                if (element.state == ANALYSIS_LAYOUT_RESOLVED)
                {
                    if (!analysis_layout_product(element.size, type->as.vector.count, &layout.size))
                    {
                        layout.state = ANALYSIS_LAYOUT_ERROR;
                        break;
                    }
                    bool valid_element = element_type->kind == ANALYSIS_TYPE_INTEGER || element_type->kind == ANALYSIS_TYPE_FLOAT;
                    bool valid_size = layout.size == 8 || layout.size == 16 || layout.size == 32 || layout.size == 64;
                    if (!valid_element || !valid_size)
                    {
                        layout.state = ANALYSIS_LAYOUT_ERROR;
                    }
                    else
                    {
                        layout.alignment = (u32)layout.size;
                        layout.abi_class = ANALYSIS_ABI_CLASS_VECTOR;
                    }
                }
                else if (element.state == ANALYSIS_LAYOUT_ERROR)
                {
                    layout.state = ANALYSIS_LAYOUT_ERROR;
                }
                else
                {
                    ready = false;
                }
            }
            break;
            case ANALYSIS_TYPE_RANGE:
            {
                AnalysisTypeLayout element = analysis_type_from_id(result, type->as.element_type)->layout;
                if (element.state == ANALYSIS_LAYOUT_RESOLVED)
                {
                    if (!analysis_layout_product(element.size, 2, &layout.size))
                    {
                        layout.state = ANALYSIS_LAYOUT_ERROR;
                    }
                    else
                    {
                        layout.alignment = element.alignment;
                        layout.abi_class = ANALYSIS_ABI_CLASS_AGGREGATE;
                    }
                }
                else if (element.state == ANALYSIS_LAYOUT_ERROR)
                {
                    layout.state = ANALYSIS_LAYOUT_ERROR;
                }
                else
                {
                    ready = false;
                }
            }
            break;
            case ANALYSIS_TYPE_STRUCT:
            case ANALYSIS_TYPE_UNION:
            {
                AnalysisResult* declaration_module = 0;
                AnalysisEntitySemantic* semantic = analysis_declaration_semantic(result, type->as.declaration, &declaration_module);
                BUSTER_CHECK(semantic);
                if (declaration_module != result)
                {
                    AnalysisTypeLayout imported_layout = analysis_type_from_id(declaration_module, semantic->type)->layout;
                    if (imported_layout.state == ANALYSIS_LAYOUT_RESOLVED || imported_layout.state == ANALYSIS_LAYOUT_ERROR)
                    {
                        layout = imported_layout;
                    }
                    else
                    {
                        ready = false;
                    }
                    break;
                }
                u64 size = 0;
                u32 alignment = 1;
                for (u32 field_index = 0; field_index < semantic->field_count; field_index += 1)
                {
                    AnalysisTypeLayout field_layout = analysis_type_from_id(result, semantic->fields[field_index].type)->layout;
                    if (field_layout.state == ANALYSIS_LAYOUT_ERROR)
                    {
                        layout.state = ANALYSIS_LAYOUT_ERROR;
                        break;
                    }
                    if (field_layout.state != ANALYSIS_LAYOUT_RESOLVED)
                    {
                        ready = false;
                        break;
                    }
                    alignment = BUSTER_MAX(alignment, field_layout.alignment);
                    if (type->kind == ANALYSIS_TYPE_STRUCT)
                    {
                        u64 field_offset = analysis_layout_align(size, field_layout.alignment);
                        if (field_offset == UINT64_MAX || !analysis_layout_sum(field_offset, field_layout.size, &size))
                        {
                            layout.state = ANALYSIS_LAYOUT_ERROR;
                            break;
                        }
                        semantic->fields[field_index].offset = field_offset;
                    }
                    else
                    {
                        semantic->fields[field_index].offset = 0;
                        size = BUSTER_MAX(size, field_layout.size);
                    }
                }
                if (ready && layout.state != ANALYSIS_LAYOUT_ERROR)
                {
                    layout.size = analysis_layout_align(size, alignment);
                    if (layout.size == UINT64_MAX)
                    {
                        layout.state = ANALYSIS_LAYOUT_ERROR;
                    }
                    else
                    {
                        layout.alignment = alignment;
                        layout.abi_class = layout.size <= 16 ? ANALYSIS_ABI_CLASS_AGGREGATE : ANALYSIS_ABI_CLASS_MEMORY;
                    }
                }
            }
            break;
            case ANALYSIS_TYPE_ENUM:
            {
                layout.size = 4;
                layout.alignment = 4;
                layout.abi_class = ANALYSIS_ABI_CLASS_INTEGER;
            }
            break;
            case ANALYSIS_TYPE_COUNT:
                layout.state = ANALYSIS_LAYOUT_ERROR;
                break;
            }
            if (ready)
            {
                type->layout = layout;
                progressed = true;
            }
        }
        if (!progressed)
        {
            break;
        }
    }
    for (u32 index = 0; index < result->types.count; index += 1)
    {
        if (result->types.types[index].layout.state == ANALYSIS_LAYOUT_UNRESOLVED)
        {
            result->types.types[index].layout.state = ANALYSIS_LAYOUT_ERROR;
        }
    }
}

BUSTER_GLOBAL_LOCAL AnalysisAbiConvention analysis_abi_convention(AnalysisType* function, Target target)
{
    if (function->as.function.calling_convention == AST_CALLING_CONVENTION_SYSTEMV)
    {
        return ANALYSIS_ABI_CONVENTION_SYSTEMV_X86_64;
    }
    if (function->as.function.calling_convention == AST_CALLING_CONVENTION_WIN64)
    {
        return ANALYSIS_ABI_CONVENTION_WIN64_X86_64;
    }
    if (target.cpu_arch == CPU_ARCH_X86_64)
    {
        return target.os == OPERATING_SYSTEM_WINDOWS || target.os == OPERATING_SYSTEM_UEFI ? ANALYSIS_ABI_CONVENTION_WIN64_X86_64
                                                                                           : ANALYSIS_ABI_CONVENTION_SYSTEMV_X86_64;
    }
    if (target.os == OPERATING_SYSTEM_WINDOWS)
    {
        return ANALYSIS_ABI_CONVENTION_WINDOWS_AARCH64;
    }
    return target.os == OPERATING_SYSTEM_MACOS || target.os == OPERATING_SYSTEM_IOS ? ANALYSIS_ABI_CONVENTION_APPLE_AARCH64 : ANALYSIS_ABI_CONVENTION_AAPCS64;
}

BUSTER_GLOBAL_LOCAL bool analysis_abi_homogeneous_float(Arena* arena, AnalysisResult* result, AnalysisTypeId type_id, AnalysisTypeId* element_out,
                                                        u32* count_out)
{
    typedef struct AnalysisAbiHomogeneousTask
    {
        AnalysisResult* module;
        AnalysisTypeId type;
    } AnalysisAbiHomogeneousTask;
    u32 capacity = BUSTER_MAX(result->types.count * 16, 16);
    AnalysisAbiHomogeneousTask* tasks = arena_allocate(arena, AnalysisAbiHomogeneousTask, capacity);
    u32 task_count = 1;
    tasks[0] = (AnalysisAbiHomogeneousTask){
        .module = result,
        .type = type_id,
    };
    AnalysisTypeId element = ANALYSIS_TYPE_ID_INVALID;
    u32 count = 0;
    String8 element_name = {0};
    while (task_count)
    {
        AnalysisAbiHomogeneousTask task = tasks[--task_count];
        AnalysisType* type = analysis_type_from_id(task.module, task.type);
        if (type->kind == ANALYSIS_TYPE_FLOAT)
        {
            if (count && !string_equal(element_name, type->name))
            {
                return false;
            }
            element_name = type->name;
            count += 1;
            if (count > 4)
            {
                return false;
            }
            continue;
        }
        if (type->kind == ANALYSIS_TYPE_ARRAY)
        {
            if (!type->as.array.count || task_count + type->as.array.count > capacity)
            {
                return false;
            }
            for (u64 index = 0; index < type->as.array.count; index += 1)
            {
                tasks[task_count++] = (AnalysisAbiHomogeneousTask){
                    .module = task.module,
                    .type = type->as.array.element_type,
                };
            }
            continue;
        }
        if (type->kind == ANALYSIS_TYPE_RANGE)
        {
            if (task_count + 2 > capacity)
            {
                return false;
            }
            tasks[task_count++] = (AnalysisAbiHomogeneousTask){
                .module = task.module,
                .type = type->as.element_type,
            };
            tasks[task_count++] = (AnalysisAbiHomogeneousTask){
                .module = task.module,
                .type = type->as.element_type,
            };
            continue;
        }
        if (type->kind != ANALYSIS_TYPE_STRUCT)
        {
            return false;
        }
        AnalysisResult* declaration_module = 0;
        AnalysisEntitySemantic* semantic = analysis_declaration_semantic(task.module, type->as.declaration, &declaration_module);
        if (!semantic || !semantic->field_count || task_count + semantic->field_count > capacity)
        {
            return false;
        }
        for (u32 field_index = 0; field_index < semantic->field_count; field_index += 1)
        {
            tasks[task_count++] = (AnalysisAbiHomogeneousTask){
                .module = declaration_module,
                .type = semantic->fields[field_index].type,
            };
        }
    }
    if (!count)
    {
        return false;
    }
    element = analysis_builtin_type_find(&result->types, element_name);
    if (element.value == ANALYSIS_ID_UNDERLYING_INVALID)
    {
        return false;
    }
    *element_out = element;
    *count_out = count;
    return true;
}

BUSTER_GLOBAL_LOCAL bool analysis_abi_systemv_classify(Arena* arena, AnalysisResult* result, AnalysisTypeId root_type, AnalysisAbiClass classes[2])
{
    typedef struct AnalysisAbiClassificationTask
    {
        AnalysisResult* module;
        AnalysisTypeId type;
        u64 offset;
    } AnalysisAbiClassificationTask;
    TargetDataLayout data_layout = target_data_layout_is_valid(result->data_layout) ? result->data_layout : target_data_layout(target_native);
    u32 abi_word_size = data_layout.pointer.size;
    u32 capacity = BUSTER_MAX(result->types.count * 16, 16);
    AnalysisAbiClassificationTask* tasks = arena_allocate(arena, AnalysisAbiClassificationTask, capacity);
    u32 count = 1;
    tasks[0] = (AnalysisAbiClassificationTask){
        .module = result,
        .type = root_type,
    };
    while (count)
    {
        AnalysisAbiClassificationTask task = tasks[--count];
        AnalysisType* type = analysis_type_from_id(task.module, task.type);
        if (task.offset > UINT64_MAX - type->layout.size || task.offset + type->layout.size > abi_word_size * 2)
        {
            return false;
        }
        if (type->layout.alignment && task.offset % type->layout.alignment)
        {
            return false;
        }
        if (type->kind == ANALYSIS_TYPE_STRUCT || type->kind == ANALYSIS_TYPE_UNION)
        {
            AnalysisResult* declaration_module = 0;
            AnalysisEntitySemantic* semantic = analysis_declaration_semantic(task.module, type->as.declaration, &declaration_module);
            if (!semantic)
            {
                return false;
            }
            if (count + semantic->field_count > capacity)
            {
                return false;
            }
            for (u32 index = 0; index < semantic->field_count; index += 1)
            {
                AnalysisField* field = semantic->fields + index;
                tasks[count++] = (AnalysisAbiClassificationTask){
                    .module = declaration_module,
                    .type = field->type,
                    .offset = task.offset > UINT64_MAX - field->offset ? UINT64_MAX : task.offset + field->offset,
                };
            }
            continue;
        }
        if (type->kind == ANALYSIS_TYPE_ARRAY)
        {
            AnalysisType* element = analysis_type_from_id(task.module, type->as.array.element_type);
            if (count + type->as.array.count > capacity)
            {
                return false;
            }
            for (u64 index = 0; index < type->as.array.count; index += 1)
            {
                tasks[count++] = (AnalysisAbiClassificationTask){
                    .module = task.module,
                    .type = type->as.array.element_type,
                    .offset = index && element->layout.size > UINT64_MAX / index ? UINT64_MAX
                              : task.offset > UINT64_MAX - index * element->layout.size ? UINT64_MAX
                                                                                           : task.offset + index * element->layout.size,
                };
            }
            continue;
        }
        if (type->kind == ANALYSIS_TYPE_RANGE)
        {
            if (count + 2 > capacity)
            {
                return false;
            }
            AnalysisType* range_element = analysis_type_from_id(task.module, type->as.element_type);
            tasks[count++] = (AnalysisAbiClassificationTask){
                .module = task.module,
                .type = type->as.element_type,
                .offset = task.offset,
            };
            tasks[count++] = (AnalysisAbiClassificationTask){
                .module = task.module,
                .type = type->as.element_type,
                .offset = task.offset > UINT64_MAX - range_element->layout.size ? UINT64_MAX : task.offset + range_element->layout.size,
            };
            continue;
        }
        AnalysisAbiClass abi_class = type->kind == ANALYSIS_TYPE_FLOAT ? ANALYSIS_ABI_CLASS_FLOAT : ANALYSIS_ABI_CLASS_INTEGER;
        u64 extent = BUSTER_MAX(type->layout.size, 1);
        if (task.offset > UINT64_MAX - (extent - 1))
        {
            return false;
        }
        u32 first = (u32)(task.offset / abi_word_size);
        u32 last = (u32)((task.offset + extent - 1) / abi_word_size);
        for (u32 part = first; part <= last; part += 1)
        {
            if (part >= 2)
            {
                return false;
            }
            if (classes[part] == ANALYSIS_ABI_CLASS_NONE)
            {
                classes[part] = abi_class;
            }
            else if (classes[part] != abi_class)
            {
                classes[part] = ANALYSIS_ABI_CLASS_INTEGER;
            }
        }
    }
    return true;
}

BUSTER_GLOBAL_LOCAL AnalysisAbiValue analysis_abi_value_classify_context(Arena* arena, AnalysisResult* result, AnalysisTypeId type_id,
                                                                         AnalysisAbiConvention convention, bool is_result, bool variadic_argument,
                                                                         u32 vector_register_size)
{
    AnalysisAbiValue value = {0};
    TargetDataLayout data_layout = target_data_layout_is_valid(result->data_layout) ? result->data_layout : target_data_layout(target_native);
    u32 abi_word_size = data_layout.pointer.size;
    AnalysisType* type = analysis_type_from_id(result, type_id);
    AnalysisTypeLayout layout = type->layout;
    if (type->kind == ANALYSIS_TYPE_VOID)
    {
        return value;
    }
    if (type->kind == ANALYSIS_TYPE_FLOAT)
    {
        value.part_count = 1;
        value.parts[0] = (AnalysisAbiPart){
            .abi_class = convention == ANALYSIS_ABI_CONVENTION_WINDOWS_AARCH64 && variadic_argument ? ANALYSIS_ABI_CLASS_INTEGER : ANALYSIS_ABI_CLASS_FLOAT,
            .location = ANALYSIS_ABI_LOCATION_REGISTER,
            .size = (u32)layout.size,
            .value_offset = 0,
        };
        return value;
    }
    if (type->kind == ANALYSIS_TYPE_VECTOR)
    {
        if (convention == ANALYSIS_ABI_CONVENTION_SYSTEMV_X86_64 && layout.size > vector_register_size)
        {
            value.indirect = is_result;
            value.part_count = 1;
            value.parts[0] = (AnalysisAbiPart){
                .abi_class = is_result ? ANALYSIS_ABI_CLASS_POINTER : ANALYSIS_ABI_CLASS_MEMORY,
                .location = is_result ? ANALYSIS_ABI_LOCATION_INDIRECT : ANALYSIS_ABI_LOCATION_STACK,
                .size = is_result ? abi_word_size : (u32)layout.size,
            };
        }
        else if (convention == ANALYSIS_ABI_CONVENTION_WIN64_X86_64)
        {
            value.indirect = true;
            value.part_count = 1;
            value.parts[0] = (AnalysisAbiPart){
                .abi_class = ANALYSIS_ABI_CLASS_POINTER,
                .location = ANALYSIS_ABI_LOCATION_INDIRECT,
                .size = abi_word_size,
            };
        }
        else if ((convention == ANALYSIS_ABI_CONVENTION_AAPCS64 || convention == ANALYSIS_ABI_CONVENTION_APPLE_AARCH64 ||
                  convention == ANALYSIS_ABI_CONVENTION_WINDOWS_AARCH64) &&
                 layout.size > abi_word_size * 2)
        {
            value.indirect = true;
            value.part_count = 1;
            value.parts[0] = (AnalysisAbiPart){
                .abi_class = ANALYSIS_ABI_CLASS_POINTER,
                .location = ANALYSIS_ABI_LOCATION_INDIRECT,
                .size = abi_word_size,
            };
        }
        else if (convention == ANALYSIS_ABI_CONVENTION_WINDOWS_AARCH64 && variadic_argument)
        {
            value.part_count = (u32)((layout.size + abi_word_size - 1) / abi_word_size);
            for (u32 part = 0; part < value.part_count; part += 1)
            {
                value.parts[part] = (AnalysisAbiPart){
                    .abi_class = ANALYSIS_ABI_CLASS_INTEGER,
                    .location = ANALYSIS_ABI_LOCATION_REGISTER,
                    .size = (u32)BUSTER_MIN((u64)abi_word_size, layout.size - (u64)part * abi_word_size),
                    .value_offset = part * abi_word_size,
                };
            }
        }
        else if (convention == ANALYSIS_ABI_CONVENTION_SYSTEMV_X86_64 && variadic_argument && layout.size > abi_word_size * 2)
        {
            value.part_count = 1;
            value.parts[0] = (AnalysisAbiPart){
                .abi_class = ANALYSIS_ABI_CLASS_MEMORY,
                .location = ANALYSIS_ABI_LOCATION_STACK,
                .size = (u32)layout.size,
            };
        }
        else
        {
            value.part_count = 1;
            value.parts[0] = (AnalysisAbiPart){
                .abi_class = ANALYSIS_ABI_CLASS_VECTOR,
                .location = ANALYSIS_ABI_LOCATION_REGISTER,
                .size = (u32)layout.size,
            };
        }
        return value;
    }
    bool scalar = type->kind == ANALYSIS_TYPE_BOOL || type->kind == ANALYSIS_TYPE_INTEGER || type->kind == ANALYSIS_TYPE_POINTER ||
                  type->kind == ANALYSIS_TYPE_FUNCTION || type->kind == ANALYSIS_TYPE_ENUM;
    if (scalar)
    {
        value.part_count = 1;
        value.parts[0] = (AnalysisAbiPart){
            .abi_class = type->kind == ANALYSIS_TYPE_POINTER || type->kind == ANALYSIS_TYPE_FUNCTION ? ANALYSIS_ABI_CLASS_POINTER : ANALYSIS_ABI_CLASS_INTEGER,
            .location = ANALYSIS_ABI_LOCATION_REGISTER,
            .size = (u32)layout.size,
            .value_offset = 0,
        };
        return value;
    }
    if (convention == ANALYSIS_ABI_CONVENTION_WIN64_X86_64)
    {
        if (layout.size == 1 || layout.size == 2 || layout.size == 4 || layout.size == 8)
        {
            value.part_count = 1;
            value.parts[0] = (AnalysisAbiPart){
                .abi_class = ANALYSIS_ABI_CLASS_INTEGER,
                .location = ANALYSIS_ABI_LOCATION_REGISTER,
                .size = (u32)layout.size,
                .value_offset = 0,
            };
        }
        else
        {
            value.indirect = true;
            value.part_count = 1;
            value.parts[0] = (AnalysisAbiPart){
                .abi_class = ANALYSIS_ABI_CLASS_POINTER,
                .location = ANALYSIS_ABI_LOCATION_INDIRECT,
                .size = abi_word_size,
                .value_offset = 0,
            };
        }
        return value;
    }
    if (convention == ANALYSIS_ABI_CONVENTION_AAPCS64 || convention == ANALYSIS_ABI_CONVENTION_APPLE_AARCH64 ||
        convention == ANALYSIS_ABI_CONVENTION_WINDOWS_AARCH64)
    {
        AnalysisTypeId homogeneous_type = ANALYSIS_TYPE_ID_INVALID;
        u32 homogeneous_count = 0;
        if (!(convention == ANALYSIS_ABI_CONVENTION_WINDOWS_AARCH64 && variadic_argument) &&
            analysis_abi_homogeneous_float(arena, result, type_id, &homogeneous_type, &homogeneous_count))
        {
            AnalysisType* element = analysis_type_from_id(result, homogeneous_type);
            value.part_count = homogeneous_count;
            for (u32 part = 0; part < homogeneous_count; part += 1)
            {
                value.parts[part] = (AnalysisAbiPart){
                    .abi_class = ANALYSIS_ABI_CLASS_FLOAT,
                    .location = ANALYSIS_ABI_LOCATION_REGISTER,
                    .size = (u32)element->layout.size,
                    .value_offset = part * (u32)element->layout.size,
                };
            }
        }
        else if (layout.size <= abi_word_size * 2)
        {
            value.part_count = (u32)((layout.size + abi_word_size - 1) / abi_word_size);
            for (u32 part = 0; part < value.part_count; part += 1)
            {
                value.parts[part] = (AnalysisAbiPart){
                    .abi_class = ANALYSIS_ABI_CLASS_INTEGER,
                    .location = ANALYSIS_ABI_LOCATION_REGISTER,
                    .size = (u32)BUSTER_MIN((u64)abi_word_size, layout.size - (u64)part * abi_word_size),
                    .value_offset = part * abi_word_size,
                };
            }
        }
        else
        {
            value.indirect = true;
            value.part_count = 1;
            value.parts[0] = (AnalysisAbiPart){
                .abi_class = ANALYSIS_ABI_CLASS_POINTER,
                .location = ANALYSIS_ABI_LOCATION_INDIRECT,
                .size = abi_word_size,
                .value_offset = 0,
            };
        }
        return value;
    }
    if (layout.size > abi_word_size * 2)
    {
        value.indirect = is_result;
        value.part_count = 1;
        value.parts[0] = (AnalysisAbiPart){
            .abi_class = is_result ? ANALYSIS_ABI_CLASS_POINTER : ANALYSIS_ABI_CLASS_MEMORY,
            .location = is_result ? ANALYSIS_ABI_LOCATION_INDIRECT : ANALYSIS_ABI_LOCATION_STACK,
            .size = is_result ? abi_word_size : (u32)layout.size,
            .value_offset = 0,
        };
        return value;
    }
    AnalysisAbiClass classes[2] = {0};
    if (!analysis_abi_systemv_classify(arena, result, type_id, classes))
    {
        value.part_count = 1;
        value.parts[0] = (AnalysisAbiPart){
            .abi_class = is_result ? ANALYSIS_ABI_CLASS_POINTER : ANALYSIS_ABI_CLASS_MEMORY,
            .location = is_result ? ANALYSIS_ABI_LOCATION_INDIRECT : ANALYSIS_ABI_LOCATION_STACK,
            .size = is_result ? abi_word_size : (u32)layout.size,
        };
        value.indirect = is_result;
        return value;
    }
    value.part_count = (u32)((layout.size + abi_word_size - 1) / abi_word_size);
    for (u32 part = 0; part < value.part_count; part += 1)
    {
        value.parts[part] = (AnalysisAbiPart){
            .abi_class = classes[part] == ANALYSIS_ABI_CLASS_NONE ? ANALYSIS_ABI_CLASS_INTEGER : classes[part],
            .location = ANALYSIS_ABI_LOCATION_REGISTER,
            .size = (u32)BUSTER_MIN((u64)abi_word_size, layout.size - (u64)part * abi_word_size),
            .value_offset = part * abi_word_size,
        };
    }
    return value;
}

AnalysisAbiValue analysis_abi_value_classify(Arena* arena, AnalysisResult* result, AnalysisTypeId type_id, AnalysisAbiConvention convention, bool is_result)
{
    return analysis_abi_value_classify_context(arena, result, type_id, convention, is_result, false,
                                               convention == ANALYSIS_ABI_CONVENTION_SYSTEMV_X86_64 ? 64 : 16);
}

AnalysisAbiValue analysis_abi_value_classify_variadic_argument(Arena* arena, AnalysisResult* result, AnalysisTypeId type_id, AnalysisAbiConvention convention)
{
    return analysis_abi_value_classify_context(arena, result, type_id, convention, false, true, convention == ANALYSIS_ABI_CONVENTION_SYSTEMV_X86_64 ? 64 : 16);
}

BUSTER_GLOBAL_LOCAL AnalysisFunctionAbi analysis_classify_abi(Arena* result_arena, AnalysisResult* result, AnalysisTypeId function_type_id,
                                                              AnalysisTypeId* argument_types, u32 argument_count, Target target)
{
    AnalysisType* function = analysis_type_from_id(result, function_type_id);
    BUSTER_CHECK(function->kind == ANALYSIS_TYPE_FUNCTION);
    AnalysisFunctionAbi abi = {
        .convention = analysis_abi_convention(function, target),
        .argument_count = argument_count,
        .indirect_result_register = UINT32_MAX,
        .fixed_argument_count = function->as.function.argument_count,
        .is_variadic = function->as.function.is_variadic,
    };
    TargetDataLayout data_layout = target_data_layout_is_valid(result->data_layout) ? result->data_layout : target_data_layout(target_native);
    u32 abi_word_size = data_layout.pointer.size;
    u32 abi_stack_alignment = data_layout.abi_stack_alignment;
    u32 vector_register_size = target_vector_register_size(target);
    abi.arguments = arena_allocate(result_arena, AnalysisAbiValue, abi.argument_count);
    abi.result =
        analysis_abi_value_classify_context(result_arena, result, function->as.function.return_type, abi.convention, true, false, vector_register_size);
    u32 result_integer_register = 0;
    u32 result_float_register = 0;
    for (u32 part = 0; part < abi.result.part_count; part += 1)
    {
        abi.result.parts[part].register_index =
            abi.result.parts[part].abi_class == ANALYSIS_ABI_CLASS_FLOAT || abi.result.parts[part].abi_class == ANALYSIS_ABI_CLASS_VECTOR
                ? result_float_register++
                : result_integer_register++;
    }
    u32 integer_register = 0;
    u32 float_register = 0;
    u32 windows_slot = 0;
    u32 stack_offset = abi.convention == ANALYSIS_ABI_CONVENTION_WIN64_X86_64 ? abi_word_size * 4 : 0;
    u32 integer_limit = abi.convention == ANALYSIS_ABI_CONVENTION_SYSTEMV_X86_64 ? 6 : abi.convention == ANALYSIS_ABI_CONVENTION_WIN64_X86_64 ? 4 : 8;
    u32 float_limit = abi.convention == ANALYSIS_ABI_CONVENTION_SYSTEMV_X86_64 ? 8 : abi.convention == ANALYSIS_ABI_CONVENTION_WIN64_X86_64 ? 4 : 8;
    if (abi.result.indirect)
    {
        if (abi.convention == ANALYSIS_ABI_CONVENTION_SYSTEMV_X86_64)
        {
            abi.indirect_result_register = 0;
            integer_register += 1;
        }
        else if (abi.convention == ANALYSIS_ABI_CONVENTION_WIN64_X86_64)
        {
            abi.indirect_result_register = 0;
            windows_slot += 1;
        }
        else
        {
            abi.indirect_result_register = 8;
        }
    }
    for (u32 argument = 0; argument < abi.argument_count; argument += 1)
    {
        AnalysisTypeId argument_type = argument_types[argument];
        AnalysisType* argument_semantic = analysis_type_from_id(result, argument_type);
        AnalysisTypeLayout layout = argument_semantic->layout;
        AnalysisAbiValue value = analysis_abi_value_classify_context(result_arena, result, argument_type, abi.convention, false,
                                                                     abi.is_variadic && argument >= abi.fixed_argument_count, vector_register_size);
        if (abi.convention == ANALYSIS_ABI_CONVENTION_WIN64_X86_64)
        {
            AnalysisAbiPart* part = value.parts;
            if (windows_slot < 4)
            {
                part->register_index = windows_slot;
            }
            else
            {
                part->location = ANALYSIS_ABI_LOCATION_STACK;
                part->stack_offset = stack_offset;
                stack_offset += abi_word_size;
            }
            windows_slot += 1;
        }
        else
        {
            bool aarch64_convention = abi.convention == ANALYSIS_ABI_CONVENTION_AAPCS64 || abi.convention == ANALYSIS_ABI_CONVENTION_APPLE_AARCH64 ||
                                      abi.convention == ANALYSIS_ABI_CONVENTION_WINDOWS_AARCH64;
            u32 required_integer = 0;
            u32 required_float = 0;
            for (u32 part = 0; part < value.part_count; part += 1)
            {
                bool vector_register = value.parts[part].abi_class == ANALYSIS_ABI_CLASS_FLOAT || value.parts[part].abi_class == ANALYSIS_ABI_CLASS_VECTOR;
                required_float += vector_register;
                required_integer += !vector_register;
            }
            if (aarch64_convention && required_integer > 1 && !value.indirect && layout.alignment >= 16)
            {
                integer_register = (u32)analysis_layout_align(integer_register, 2);
            }
            bool registers_fit = integer_register + required_integer <= integer_limit && float_register + required_float <= float_limit &&
                                 value.parts[0].location != ANALYSIS_ABI_LOCATION_STACK &&
                                 !(abi.convention == ANALYSIS_ABI_CONVENTION_APPLE_AARCH64 && abi.is_variadic && argument >= abi.fixed_argument_count);
            if (registers_fit)
            {
                for (u32 part = 0; part < value.part_count; part += 1)
                {
                    if (value.parts[part].abi_class == ANALYSIS_ABI_CLASS_FLOAT || value.parts[part].abi_class == ANALYSIS_ABI_CLASS_VECTOR)
                    {
                        value.parts[part].register_index = float_register++;
                    }
                    else
                    {
                        value.parts[part].register_index = integer_register++;
                    }
                }
            }
            else
            {
                if (aarch64_convention && value.parts[0].location != ANALYSIS_ABI_LOCATION_STACK)
                {
                    if (required_float)
                    {
                        float_register = float_limit;
                    }
                    if (required_integer)
                    {
                        integer_register = integer_limit;
                    }
                }
                u32 value_alignment = value.indirect ? abi_word_size : layout.alignment;
                u64 value_size = value.indirect ? abi_word_size : layout.size;
                u32 alignment = abi.convention == ANALYSIS_ABI_CONVENTION_APPLE_AARCH64 ? value_alignment : BUSTER_MAX(value_alignment, abi_word_size);
                stack_offset = (u32)analysis_layout_align(stack_offset, alignment);
                for (u32 part = 0; part < value.part_count; part += 1)
                {
                    value.parts[part].location = ANALYSIS_ABI_LOCATION_STACK;
                    value.parts[part].stack_offset = stack_offset + value.parts[part].value_offset;
                }
                stack_offset +=
                    (u32)analysis_layout_align(value_size,
                                               abi.convention == ANALYSIS_ABI_CONVENTION_APPLE_AARCH64 ? BUSTER_MAX(value_alignment, 1) : abi_word_size);
            }
        }
        abi.arguments[argument] = value;
    }
    for (u32 argument = 0; argument < abi.argument_count; argument += 1)
    {
        AnalysisAbiValue* value = abi.arguments + argument;
        if (!value->indirect)
        {
            continue;
        }
        AnalysisType* argument_type = analysis_type_from_id(result, argument_types[argument]);
        stack_offset = (u32)analysis_layout_align(stack_offset, BUSTER_MAX(argument_type->layout.alignment, abi_word_size));
        value->indirect_copy_offset = stack_offset;
        stack_offset += (u32)analysis_layout_align(argument_type->layout.size, abi_word_size);
    }
    abi.stack_size = (u32)analysis_layout_align(stack_offset, abi_stack_alignment);
    return abi;
}

AnalysisFunctionAbi analysis_classify_function_abi(Arena* result_arena, AnalysisResult* result, AnalysisTypeId function_type_id, Target target)
{
    AnalysisType* function = analysis_type_from_id(result, function_type_id);
    BUSTER_CHECK(function->kind == ANALYSIS_TYPE_FUNCTION);
    return analysis_classify_abi(result_arena, result, function_type_id, function->as.function.argument_types, function->as.function.argument_count, target);
}

AnalysisFunctionAbi analysis_classify_call_abi(Arena* result_arena, AnalysisResult* result, AnalysisTypeId function_type_id, AnalysisTypeId* argument_types,
                                               u32 argument_count, Target target)
{
    AnalysisType* function = analysis_type_from_id(result, function_type_id);
    BUSTER_CHECK(function->kind == ANALYSIS_TYPE_FUNCTION);
    BUSTER_CHECK(function->as.function.is_variadic ? argument_count >= function->as.function.argument_count
                                                   : argument_count == function->as.function.argument_count);
    return analysis_classify_abi(result_arena, result, function_type_id, argument_types, argument_count, target);
}

BUSTER_GLOBAL_LOCAL void analysis_body_job_build(Arena* result_arena, AnalysisResult* result, AnalysisEntity* entity, AnalysisBody* body,
                                                 AnalysisInstantiationId instantiation, u32 job_index)
{
    u32 dependency_capacity = body->dependency_count + 1;
    AnalysisJobId* dependencies = arena_allocate(result_arena, AnalysisJobId, dependency_capacity);
    AnalysisDependencyKind* dependency_kinds = arena_allocate(result_arena, AnalysisDependencyKind, dependency_capacity);
    u32 dependency_count = 0;
    dependencies[dependency_count] = (AnalysisJobId){
        .value = entity->id.index.value,
    };
    dependency_kinds[dependency_count] = ANALYSIS_DEPENDENCY_INTERFACE;
    dependency_count += 1;
    for (u32 body_dependency_index = 0; body_dependency_index < body->dependency_count; body_dependency_index += 1)
    {
        AnalysisEntityId dependency_entity = body->dependencies[body_dependency_index];
        if (dependency_entity.module.value != result->module.id.value)
        {
            continue;
        }
        bool duplicate = false;
        for (u32 existing = 0; existing < dependency_count; existing += 1)
        {
            duplicate |= dependencies[existing].value == dependency_entity.index.value;
        }
        if (!duplicate)
        {
            dependencies[dependency_count] = (AnalysisJobId){
                .value = dependency_entity.index.value,
            };
            dependency_kinds[dependency_count] = ANALYSIS_DEPENDENCY_INTERFACE;
            dependency_count += 1;
        }
    }
    result->jobs[job_index] = (AnalysisJob){
        .dependencies = dependencies,
        .dependency_kinds = dependency_kinds,
        .entity = entity->id,
        .instantiation = instantiation,
        .id = {.value = job_index},
        .kind = ANALYSIS_JOB_BODY,
        .dependency_count = dependency_count,
    };
}

void analysis_build_jobs(Arena* result_arena, AnalysisResult* result)
{
    BUSTER_CHECK(result->jobs == 0);
    Arena* conflicts[] = {result_arena};
    TemporalArena scratch = scratch_begin(conflicts, BUSTER_ARRAY_LENGTH(conflicts));
    u32 ordinary_code_count = 0;
    for (u32 entity_index = 0; entity_index < result->module.entity_count; entity_index += 1)
    {
        AnalysisEntity* entity = result->module.entities + entity_index;
        ordinary_code_count += entity->kind == ANALYSIS_ENTITY_CODE && !analysis_entity_is_generic(scratch.arena, result, entity);
    }
    result->job_count = result->module.entity_count + result->module.type_count + ordinary_code_count + result->instantiation_count;
    result->jobs = arena_allocate(result_arena, AnalysisJob, result->job_count);
    u32 job_index = 0;
    for (u32 entity_index = 0; entity_index < result->module.entity_count; entity_index += 1)
    {
        AnalysisEntity* entity = result->module.entities + entity_index;
        result->jobs[job_index] = (AnalysisJob){
            .entity = entity->id,
            .instantiation = ANALYSIS_INSTANTIATION_ID_INVALID,
            .id = {.value = job_index},
            .kind = ANALYSIS_JOB_INTERFACE,
        };
        job_index += 1;
    }
    for (u32 entity_index = 0; entity_index < result->module.entity_count; entity_index += 1)
    {
        AnalysisEntity* entity = result->module.entities + entity_index;
        if (entity->kind != ANALYSIS_ENTITY_TYPE)
        {
            continue;
        }
        AnalysisJobId* dependency = arena_allocate(result_arena, AnalysisJobId, 1);
        AnalysisDependencyKind* dependency_kind = arena_allocate(result_arena, AnalysisDependencyKind, 1);
        dependency[0] = (AnalysisJobId){.value = entity_index};
        dependency_kind[0] = ANALYSIS_DEPENDENCY_INTERFACE;
        result->jobs[job_index] = (AnalysisJob){
            .dependencies = dependency,
            .dependency_kinds = dependency_kind,
            .entity = entity->id,
            .instantiation = ANALYSIS_INSTANTIATION_ID_INVALID,
            .id = {.value = job_index},
            .kind = ANALYSIS_JOB_LAYOUT,
            .dependency_count = 1,
        };
        job_index += 1;
    }
    for (u32 entity_index = 0; entity_index < result->module.entity_count; entity_index += 1)
    {
        AnalysisEntity* entity = result->module.entities + entity_index;
        if (entity->kind != ANALYSIS_ENTITY_CODE)
        {
            continue;
        }
        if (analysis_entity_is_generic(scratch.arena, result, entity))
        {
            continue;
        }
        analysis_body_job_build(result_arena, result, entity, result->module.bodies + entity_index, ANALYSIS_INSTANTIATION_ID_INVALID, job_index);
        job_index += 1;
    }
    for (AnalysisInstantiation* instantiation = result->first_instantiation; instantiation; instantiation = instantiation->next)
    {
        AnalysisEntity* entity = result->module.entities + instantiation->generic_entity.index.value;
        analysis_body_job_build(result_arena, result, entity, &instantiation->body, instantiation->id, job_index);
        job_index += 1;
    }
    BUSTER_CHECK(job_index == result->job_count);
    scratch_end(scratch);
}

typedef struct AnalysisScheduleWorker AnalysisScheduleWorker;
struct AnalysisScheduleWorker
{
    AnalysisResult* result;
    AnalysisJobId* ready;
    AnalysisJobCallback* callback;
    void* user_data;
    u32 ready_count;
    u32 worker_index;
    u32 worker_count;
};

BUSTER_GLOBAL_LOCAL void analysis_schedule_worker(void* raw_worker)
{
    AnalysisScheduleWorker* worker = (AnalysisScheduleWorker*)raw_worker;
    for (u32 index = worker->worker_index; index < worker->ready_count; index += worker->worker_count)
    {
        worker->callback(worker->result->jobs + worker->ready[index].value, worker->worker_index, worker->user_data);
    }
}

AnalysisScheduleResult analysis_execute_jobs(Arena* result_arena, AnalysisResult* result, u32 worker_count, AnalysisJobCallback* callback, void* user_data)
{
    AnalysisScheduleResult schedule = {0};
    schedule.execution_order = arena_allocate(result_arena, AnalysisJobId, result->job_count);
    u32* remaining = arena_allocate(result_arena, u32, result->job_count);
    memset(remaining, 0, sizeof(*remaining) * result->job_count);
    u32* id_to_index = arena_allocate(result_arena, u32, result->job_count);
    memset(id_to_index, 0xff, sizeof(*id_to_index) * result->job_count);
    u64 edge_capacity_u64 = 0;
    for (u32 job_index = 0; job_index < result->job_count; job_index += 1)
    {
        AnalysisJob* job = result->jobs + job_index;
        if (job->id.value >= result->job_count || id_to_index[job->id.value] != UINT32_MAX)
        {
            schedule.has_cycle = true;
            return schedule;
        }
        id_to_index[job->id.value] = job_index;
        if (edge_capacity_u64 > UINT32_MAX - job->dependency_count)
        {
            schedule.has_cycle = true;
            return schedule;
        }
        edge_capacity_u64 += job->dependency_count;
    }
    typedef struct AnalysisJobDependencyEdge AnalysisJobDependencyEdge;
    struct AnalysisJobDependencyEdge
    {
        u32 source;
        u32 dependent;
    };
    u32 edge_capacity = (u32)edge_capacity_u64;
    AnalysisJobDependencyEdge* edges = arena_allocate(result_arena, AnalysisJobDependencyEdge, edge_capacity);
    u32 edge_count = 0;
    for (u32 job_index = 0; job_index < result->job_count; job_index += 1)
    {
        AnalysisJob* job = result->jobs + job_index;
        for (u32 dependency_index = 0; dependency_index < job->dependency_count; dependency_index += 1)
        {
            AnalysisJobId dependency = job->dependencies[dependency_index];
            if (dependency.value >= result->job_count || id_to_index[dependency.value] == UINT32_MAX)
            {
                remaining[job_index] += 1;
                continue;
            }
            edges[edge_count++] = (AnalysisJobDependencyEdge){
                .source = id_to_index[dependency.value],
                .dependent = job_index,
            };
            remaining[job_index] += 1;
        }
    }
    u32* source_counts = arena_allocate(result_arena, u32, result->job_count);
    memset(source_counts, 0, sizeof(*source_counts) * result->job_count);
    for (u32 edge_index = 0; edge_index < edge_count; edge_index += 1)
    {
        source_counts[edges[edge_index].source] += 1;
    }
    u32* source_offsets = arena_allocate(result_arena, u32, result->job_count + 1);
    source_offsets[0] = 0;
    for (u32 job_index = 0; job_index < result->job_count; job_index += 1)
    {
        source_offsets[job_index + 1] = source_offsets[job_index] + source_counts[job_index];
    }
    u32* source_cursor = arena_allocate(result_arena, u32, result->job_count);
    memcpy(source_cursor, source_offsets, sizeof(*source_cursor) * result->job_count);
    u32* dependents = arena_allocate(result_arena, u32, edge_count);
    for (u32 edge_index = 0; edge_index < edge_count; edge_index += 1)
    {
        AnalysisJobDependencyEdge edge = edges[edge_index];
        dependents[source_cursor[edge.source]++] = edge.dependent;
    }
    bool* complete = arena_allocate(result_arena, bool, result->job_count);
    memset(complete, 0, sizeof(*complete) * result->job_count);
    AnalysisJobId* ready = arena_allocate(result_arena, AnalysisJobId, result->job_count);
    u32 head = 0;
    u32 tail = 0;
    for (u32 job_index = 0; job_index < result->job_count; job_index += 1)
    {
        if (!remaining[job_index])
        {
            ready[tail++] = result->jobs[job_index].id;
        }
    }
    worker_count = BUSTER_MAX(worker_count, 1);
    while (head < tail)
    {
        u32 batch_start = head;
        u32 batch_end = tail;
        u32 ready_count = batch_end - batch_start;
        u32 active_workers = BUSTER_MIN(worker_count, ready_count);
        if (callback && active_workers > 1)
        {
            AnalysisScheduleWorker* workers = arena_allocate(result_arena, AnalysisScheduleWorker, active_workers);
            OsThreadHandle** handles = arena_allocate(result_arena, OsThreadHandle*, active_workers);
            for (u32 worker_index = 0; worker_index < active_workers; worker_index += 1)
            {
                workers[worker_index] = (AnalysisScheduleWorker){
                    .result = result,
                    .ready = ready + batch_start,
                    .callback = callback,
                    .user_data = user_data,
                    .ready_count = ready_count,
                    .worker_index = worker_index,
                    .worker_count = active_workers,
                };
                handles[worker_index] = os_thread_create((ThreadCreateOptions){
                    .callback = analysis_schedule_worker,
                    .argument = workers + worker_index,
                });
            }
            for (u32 worker_index = 0; worker_index < active_workers; worker_index += 1)
            {
                BUSTER_CHECK(os_thread_join(handles[worker_index]));
            }
        }
        else if (callback)
        {
            AnalysisScheduleWorker worker = {
                .result = result,
                .ready = ready + batch_start,
                .callback = callback,
                .user_data = user_data,
                .ready_count = ready_count,
                .worker_count = 1,
            };
            analysis_schedule_worker(&worker);
        }
        head = batch_end;
        for (u32 index = batch_start; index < batch_end; index += 1)
        {
            u32 job_index = id_to_index[ready[index].value];
            if (job_index >= result->job_count || complete[job_index])
            {
                schedule.has_cycle = true;
                return schedule;
            }
            complete[job_index] = true;
            schedule.execution_order[schedule.execution_count++] = ready[index];
            for (u32 dependent_index = source_offsets[job_index]; dependent_index < source_offsets[job_index + 1]; dependent_index += 1)
            {
                u32 dependent = dependents[dependent_index];
                if (remaining[dependent] && --remaining[dependent] == 0)
                {
                    ready[tail++] = result->jobs[dependent].id;
                }
            }
        }
        schedule.wave_count += 1;
    }
    schedule.has_cycle = schedule.execution_count != result->job_count;
    return schedule;
}

typedef struct AnalysisProgramScheduleWorker AnalysisProgramScheduleWorker;
struct AnalysisProgramScheduleWorker
{
    AnalysisProgram* program;
    AnalysisProgramJob* ready;
    AnalysisProgramJobCallback* callback;
    void* user_data;
    u32 ready_count;
    u32 worker_index;
    u32 worker_count;
};

BUSTER_GLOBAL_LOCAL AnalysisInstantiation* analysis_instantiation_from_id(AnalysisResult* result, AnalysisInstantiationId id)
{
    for (AnalysisInstantiation* instantiation = result->first_instantiation; instantiation; instantiation = instantiation->next)
    {
        if (instantiation->id.value == id.value)
        {
            return instantiation;
        }
    }
    return 0;
}

BUSTER_GLOBAL_LOCAL void analysis_program_schedule_worker(void* raw_worker)
{
    AnalysisProgramScheduleWorker* worker = (AnalysisProgramScheduleWorker*)raw_worker;
    for (u32 index = worker->worker_index; index < worker->ready_count; index += worker->worker_count)
    {
        AnalysisProgramJob ready = worker->ready[index];
        AnalysisResult* module = 0;
        for (u32 module_index = 0; module_index < worker->program->module_count; module_index += 1)
        {
            AnalysisResult* candidate = worker->program->module_results[module_index];
            if (candidate && candidate->module.id.value == ready.module.value)
            {
                module = candidate;
                break;
            }
        }
        BUSTER_CHECK(module && ready.job.value < module->job_count);
        worker->callback(module, module->jobs + ready.job.value, worker->worker_index, worker->user_data);
    }
}

BUSTER_GLOBAL_LOCAL u32 analysis_program_module_index(AnalysisProgram* program, AnalysisModuleId id)
{
    for (u32 index = 0; index < program->module_count; index += 1)
    {
        AnalysisResult* candidate = program->module_results[index];
        if (candidate && candidate->module.id.value == id.value)
        {
            return index;
        }
    }
    return UINT32_MAX;
}

BUSTER_GLOBAL_LOCAL u32 analysis_program_job_flat_index(AnalysisProgram* program, u32* offsets, AnalysisModuleId module_id, AnalysisJobId job_id)
{
    u32 module_index = analysis_program_module_index(program, module_id);
    if (module_index == UINT32_MAX)
    {
        return UINT32_MAX;
    }
    AnalysisResult* module = program->module_results[module_index];
    if (!module)
    {
        return UINT32_MAX;
    }
    for (u32 job_index = 0; job_index < module->job_count; job_index += 1)
    {
        if (module->jobs[job_index].id.value == job_id.value)
        {
            return offsets[module_index] + job_index;
        }
    }
    return UINT32_MAX;
}

typedef struct AnalysisProgramDependencyEdge AnalysisProgramDependencyEdge;
struct AnalysisProgramDependencyEdge
{
    u32 source;
    u32 dependent;
    AnalysisProgramJob dependent_job;
};

BUSTER_GLOBAL_LOCAL bool analysis_program_schedule_add_edge(AnalysisProgramDependencyEdge* edges, u32 edge_capacity, u32* edge_count, u32* remaining,
                                                             u32 source, u32 dependent, AnalysisProgramJob dependent_job)
{
    if (source == UINT32_MAX || dependent == UINT32_MAX || *edge_count >= edge_capacity)
    {
        return false;
    }
    edges[*edge_count] = (AnalysisProgramDependencyEdge){
        .source = source,
        .dependent = dependent,
        .dependent_job = dependent_job,
    };
    *edge_count += 1;
    remaining[dependent] += 1;
    return true;
}

AnalysisProgramScheduleResult analysis_execute_program_jobs(Arena* result_arena, AnalysisProgram* program, u32 worker_count,
                                                            AnalysisProgramJobCallback* callback, void* user_data)
{
    AnalysisProgramScheduleResult schedule = {0};
    u32* offsets = arena_allocate(result_arena, u32, program->module_count + 1);
    offsets[0] = 0;
    for (u32 module_index = 0; module_index < program->module_count; module_index += 1)
    {
        AnalysisResult* module = program->module_results[module_index];
        offsets[module_index + 1] = offsets[module_index] + (module ? module->job_count : 0);
    }
    u32 total = offsets[program->module_count];
    schedule.execution_order = arena_allocate(result_arena, AnalysisProgramJob, total);
    u32* remaining = arena_allocate(result_arena, u32, total);
    memset(remaining, 0, sizeof(*remaining) * total);
    u64 edge_capacity_u64 = 0;
    for (u32 module_index = 0; module_index < program->module_count; module_index += 1)
    {
        AnalysisResult* module = program->module_results[module_index];
        if (!module)
        {
            continue;
        }
        for (u32 job_index = 0; job_index < module->job_count; job_index += 1)
        {
            AnalysisJob* job = module->jobs + job_index;
            if (edge_capacity_u64 > UINT32_MAX - job->dependency_count)
            {
                schedule.has_cycle = true;
                return schedule;
            }
            edge_capacity_u64 += job->dependency_count;
            if (job->kind == ANALYSIS_JOB_INTERFACE)
            {
                for (u32 import_index = 0; import_index < module->module.import_count; import_index += 1)
                {
                    AnalysisImport* import = module->module.imports + import_index;
                    if (import->state != ANALYSIS_IMPORT_RESOLVED || !import->target)
                    {
                        continue;
                    }
                    for (u32 target_job = 0; target_job < import->target->job_count; target_job += 1)
                    {
                        edge_capacity_u64 += import->target->jobs[target_job].kind == ANALYSIS_JOB_INTERFACE;
                        if (edge_capacity_u64 > UINT32_MAX)
                        {
                            schedule.has_cycle = true;
                            return schedule;
                        }
                    }
                }
            }
            else if (job->kind == ANALYSIS_JOB_BODY && job->entity.index.value < module->module.entity_count)
            {
                AnalysisInstantiation* instantiation =
                    job->instantiation.value == ANALYSIS_ID_UNDERLYING_INVALID ? 0 : analysis_instantiation_from_id(module, job->instantiation);
                AnalysisBody* body = instantiation ? &instantiation->body : module->module.bodies + job->entity.index.value;
                if (body)
                {
                    for (u32 dependency_index = 0; dependency_index < body->dependency_count; dependency_index += 1)
                    {
                        edge_capacity_u64 += body->dependencies[dependency_index].module.value != module->module.id.value;
                        if (edge_capacity_u64 > UINT32_MAX)
                        {
                            schedule.has_cycle = true;
                            return schedule;
                        }
                    }
                }
            }
        }
    }
    u32 edge_capacity = (u32)edge_capacity_u64;
    AnalysisProgramDependencyEdge* edges = arena_allocate(result_arena, AnalysisProgramDependencyEdge, edge_capacity);
    u32 edge_count = 0;
    for (u32 module_index = 0; module_index < program->module_count; module_index += 1)
    {
        AnalysisResult* module = program->module_results[module_index];
        if (!module)
        {
            continue;
        }
        for (u32 job_index = 0; job_index < module->job_count; job_index += 1)
        {
            AnalysisJob* job = module->jobs + job_index;
            u32 dependent = offsets[module_index] + job_index;
            AnalysisProgramJob dependent_job = {
                .module = module->module.id,
                .job = job->id,
            };
            for (u32 dependency_index = 0; dependency_index < job->dependency_count; dependency_index += 1)
            {
                u32 source = analysis_program_job_flat_index(program, offsets, module->module.id, job->dependencies[dependency_index]);
                if (!analysis_program_schedule_add_edge(edges, edge_capacity, &edge_count, remaining, source, dependent, dependent_job))
                {
                    remaining[dependent] += 1;
                }
            }
            if (job->kind == ANALYSIS_JOB_INTERFACE)
            {
                for (u32 import_index = 0; import_index < module->module.import_count; import_index += 1)
                {
                    AnalysisImport* import = module->module.imports + import_index;
                    if (import->state != ANALYSIS_IMPORT_RESOLVED || !import->target)
                    {
                        continue;
                    }
                    for (u32 target_job = 0; target_job < import->target->job_count; target_job += 1)
                    {
                        if (import->target->jobs[target_job].kind != ANALYSIS_JOB_INTERFACE)
                        {
                            continue;
                        }
                        u32 source = analysis_program_job_flat_index(program, offsets, import->target_id, import->target->jobs[target_job].id);
                        if (!analysis_program_schedule_add_edge(edges, edge_capacity, &edge_count, remaining, source, dependent, dependent_job))
                        {
                            remaining[dependent] += 1;
                        }
                    }
                }
            }
            else if (job->kind == ANALYSIS_JOB_BODY && job->entity.index.value < module->module.entity_count)
            {
                AnalysisInstantiation* instantiation =
                    job->instantiation.value == ANALYSIS_ID_UNDERLYING_INVALID ? 0 : analysis_instantiation_from_id(module, job->instantiation);
                AnalysisBody* body = instantiation ? &instantiation->body : module->module.bodies + job->entity.index.value;
                if (body)
                {
                    for (u32 dependency_index = 0; dependency_index < body->dependency_count; dependency_index += 1)
                    {
                        AnalysisEntityId dependency = body->dependencies[dependency_index];
                        if (dependency.module.value == module->module.id.value)
                        {
                            continue;
                        }
                        u32 source = analysis_program_job_flat_index(program, offsets, dependency.module,
                                                                      (AnalysisJobId){.value = dependency.index.value});
                        if (!analysis_program_schedule_add_edge(edges, edge_capacity, &edge_count, remaining, source, dependent, dependent_job))
                        {
                            remaining[dependent] += 1;
                        }
                    }
                }
            }
        }
    }
    u32* source_counts = arena_allocate(result_arena, u32, total);
    memset(source_counts, 0, sizeof(*source_counts) * total);
    for (u32 edge_index = 0; edge_index < edge_count; edge_index += 1)
    {
        source_counts[edges[edge_index].source] += 1;
    }
    u32* source_offsets = arena_allocate(result_arena, u32, total + 1);
    source_offsets[0] = 0;
    for (u32 job_index = 0; job_index < total; job_index += 1)
    {
        source_offsets[job_index + 1] = source_offsets[job_index] + source_counts[job_index];
    }
    u32* source_cursor = arena_allocate(result_arena, u32, total);
    memcpy(source_cursor, source_offsets, sizeof(*source_cursor) * total);
    u32* dependent_edges = arena_allocate(result_arena, u32, edge_count);
    for (u32 edge_index = 0; edge_index < edge_count; edge_index += 1)
    {
        AnalysisProgramDependencyEdge edge = edges[edge_index];
        dependent_edges[source_cursor[edge.source]++] = edge_index;
    }
    bool* complete = arena_allocate(result_arena, bool, total);
    memset(complete, 0, sizeof(*complete) * total);
    AnalysisProgramJob* ready = arena_allocate(result_arena, AnalysisProgramJob, total);
    u32 head = 0;
    u32 tail = 0;
    for (u32 module_index = 0; module_index < program->module_count; module_index += 1)
    {
        AnalysisResult* module = program->module_results[module_index];
        if (!module)
        {
            continue;
        }
        for (u32 job_index = 0; job_index < module->job_count; job_index += 1)
        {
            if (!remaining[offsets[module_index] + job_index])
            {
                ready[tail++] = (AnalysisProgramJob){
                    .module = module->module.id,
                    .job = module->jobs[job_index].id,
                };
            }
        }
    }
    worker_count = BUSTER_MAX(worker_count, 1);
    while (head < tail)
    {
        u32 batch_start = head;
        u32 batch_end = tail;
        u32 ready_count = batch_end - batch_start;
        u32 active_workers = BUSTER_MIN(worker_count, ready_count);
        if (callback && active_workers > 1)
        {
            AnalysisProgramScheduleWorker* workers = arena_allocate(result_arena, AnalysisProgramScheduleWorker, active_workers);
            OsThreadHandle** handles = arena_allocate(result_arena, OsThreadHandle*, active_workers);
            for (u32 worker_index = 0; worker_index < active_workers; worker_index += 1)
            {
                workers[worker_index] = (AnalysisProgramScheduleWorker){
                    .program = program,
                    .ready = ready + batch_start,
                    .callback = callback,
                    .user_data = user_data,
                    .ready_count = ready_count,
                    .worker_index = worker_index,
                    .worker_count = active_workers,
                };
                handles[worker_index] = os_thread_create((ThreadCreateOptions){
                    .callback = analysis_program_schedule_worker,
                    .argument = workers + worker_index,
                });
            }
            for (u32 worker_index = 0; worker_index < active_workers; worker_index += 1)
            {
                BUSTER_CHECK(os_thread_join(handles[worker_index]));
            }
        }
        else if (callback)
        {
            AnalysisProgramScheduleWorker worker = {
                .program = program,
                .ready = ready + batch_start,
                .callback = callback,
                .user_data = user_data,
                .ready_count = ready_count,
                .worker_count = 1,
            };
            analysis_program_schedule_worker(&worker);
        }

        head = batch_end;
        for (u32 ready_index = batch_start; ready_index < batch_end; ready_index += 1)
        {
            AnalysisProgramJob item = ready[ready_index];
            u32 flat = analysis_program_job_flat_index(program, offsets, item.module, item.job);
            if (flat == UINT32_MAX || complete[flat])
            {
                schedule.has_cycle = true;
                return schedule;
            }
            complete[flat] = true;
            schedule.execution_order[schedule.execution_count++] = item;
            for (u32 dependent_index = source_offsets[flat]; dependent_index < source_offsets[flat + 1]; dependent_index += 1)
            {
                AnalysisProgramDependencyEdge edge = edges[dependent_edges[dependent_index]];
                if (remaining[edge.dependent] && --remaining[edge.dependent] == 0)
                {
                    ready[tail++] = edge.dependent_job;
                }
            }
        }
        schedule.wave_count += 1;
    }
    schedule.has_cycle = schedule.execution_count != total;
    return schedule;
}

#if BUSTER_INCLUDE_TESTS
BUSTER_GLOBAL_LOCAL ParserSourceRange analysis_test_range(u32 offset)
{
    return (ParserSourceRange){.offset = offset, .length = 1, .line = 1, .column = offset + 1};
}

BUSTER_GLOBAL_LOCAL bool analysis_test_has_diagnostic(AnalysisResult* result, AnalysisDiagnosticKind kind)
{
    for (AnalysisDiagnostic* diagnostic = result->first_diagnostic; diagnostic; diagnostic = diagnostic->next)
    {
        if (diagnostic->kind == kind)
        {
            return true;
        }
    }
    return false;
}

typedef struct AnalysisFixtureTest AnalysisFixtureTest;
struct AnalysisFixtureTest
{
    String8 path;
    u32 expected_diagnostic_count;
};

BUSTER_GLOBAL_LOCAL AnalysisFixtureTest analysis_fixture_tests[] = {
    {S8_INITIALIZER("tests/basic_vector.bbb"), 0},
    {S8_INITIALIZER("tests/basic_vector_error.bbb"), 1},
    {S8_INITIALIZER("tests/basic_variadic.bbb"), 0},
    {S8_INITIALIZER("tests/basic_variadic_error.bbb"), 1},
    {S8_INITIALIZER("tests/array_slices.bbb"), 0},
    {S8_INITIALIZER("tests/basic_array_literal.bbb"), 0},
    {S8_INITIALIZER("tests/basic_assignment.bbb"), 0},
    {S8_INITIALIZER("tests/basic_binary_literal.bbb"), 0},
    {S8_INITIALIZER("tests/basic_bitwise_not.bbb"), 0},
    {S8_INITIALIZER("tests/basic_boolean_operators.bbb"), 1},
    {S8_INITIALIZER("tests/basic_break.bbb"), 0},
    {S8_INITIALIZER("tests/basic_character_literal.bbb"), 0},
    {S8_INITIALIZER("tests/basic_comment.bbb"), 0},
    {S8_INITIALIZER("tests/basic_compile_time.bbb"), 0},
    {S8_INITIALIZER("tests/compile_time_argument_error.bbb"), 1},
    {S8_INITIALIZER("tests/modules/core/math.bbb"), 0},
    {S8_INITIALIZER("tests/modules/system/platform.bbb"), 0},
    {S8_INITIALIZER("tests/basic_continue.bbb"), 0},
    {S8_INITIALIZER("tests/basic_else_if.bbb"), 0},
    {S8_INITIALIZER("tests/basic_enum.bbb"), 0},
    {S8_INITIALIZER("tests/basic_float.bbb"), 0},
    {S8_INITIALIZER("tests/basic_for.bbb"), 0},
    {S8_INITIALIZER("tests/basic_function_call.bbb"), 1},
    {S8_INITIALIZER("tests/basic_hexadecimal_literal.bbb"), 0},
    {S8_INITIALIZER("tests/basic_if_else.bbb"), 0},
    {S8_INITIALIZER("tests/basic_integer_literal_add.bbb"), 0},
    {S8_INITIALIZER("tests/basic_integer_literal_and.bbb"), 0},
    {S8_INITIALIZER("tests/basic_integer_literal_compare.bbb"), 6},
    {S8_INITIALIZER("tests/basic_integer_literal_divide.bbb"), 0},
    {S8_INITIALIZER("tests/basic_integer_literal_mod.bbb"), 0},
    {S8_INITIALIZER("tests/basic_integer_literal_multiply.bbb"), 0},
    {S8_INITIALIZER("tests/basic_integer_literal_or.bbb"), 0},
    {S8_INITIALIZER("tests/basic_integer_literal_precedence.bbb"), 0},
    {S8_INITIALIZER("tests/basic_integer_literal_shift_left.bbb"), 0},
    {S8_INITIALIZER("tests/basic_integer_literal_shift_right.bbb"), 0},
    {S8_INITIALIZER("tests/basic_integer_literal_sub.bbb"), 0},
    {S8_INITIALIZER("tests/basic_integer_literal_xor.bbb"), 0},
    {S8_INITIALIZER("tests/basic_logical_not.bbb"), 2},
    {S8_INITIALIZER("tests/basic_loop.bbb"), 1},
    {S8_INITIALIZER("tests/basic_import.bbb"), 0},
    {S8_INITIALIZER("tests/basic_minimal.bbb"), 0},
    {S8_INITIALIZER("tests/basic_octal_literal.bbb"), 0},
    {S8_INITIALIZER("tests/basic_pointer.bbb"), 0},
    {S8_INITIALIZER("tests/basic_string_literal.bbb"), 0},
    {S8_INITIALIZER("tests/basic_struct.bbb"), 0},
    {S8_INITIALIZER("tests/basic_switch.bbb"), 0},
    {S8_INITIALIZER("tests/basic_type_alias.bbb"), 0},
    {S8_INITIALIZER("tests/basic_unary_minus.bbb"), 0},
    {S8_INITIALIZER("tests/basic_unary_plus.bbb"), 0},
    {S8_INITIALIZER("tests/basic_union.bbb"), 0},
    {S8_INITIALIZER("tests/basic_variable.bbb"), 0},
};

UnitTestResult analysis_tests(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    TemporalArena temporary = arena_begin_temporal(arguments->arena);

    AstTypeDeclaration alpha_type = {
        .name = {.text = S8("Shared"), .range = {0}},
        .range = analysis_test_range(20),
    };
    AstCode alpha_code = {
        .name = S8("duplicate"),
        .range = analysis_test_range(10),
    };
    ParserResult alpha_parser = {
        .first_code = &alpha_code,
        .last_code = &alpha_code,
        .first_type_declaration = &alpha_type,
        .last_type_declaration = &alpha_type,
        .code_count = 1,
        .type_declaration_count = 1,
    };

    AstCode zeta_shared_code = {
        .name = S8("Shared"),
        .range = analysis_test_range(5),
    };
    AstCode zeta_duplicate_code = {
        .name = S8("duplicate"),
        .range = analysis_test_range(30),
    };
    zeta_shared_code.next = &zeta_duplicate_code;
    ParserResult zeta_parser = {
        .first_code = &zeta_shared_code,
        .last_code = &zeta_duplicate_code,
        .code_count = 2,
    };

    AnalysisSourceInput reversed_inputs[] = {
        {.path = S8("zeta.bbb"), .parser = &zeta_parser},
        {.path = S8("alpha.bbb"), .parser = &alpha_parser},
    };
    AnalysisResult indexed =
        analysis_index_module(arguments->arena, (AnalysisModuleId){.value = 7}, S8("example"), reversed_inputs, BUSTER_ARRAY_LENGTH(reversed_inputs));

    BUSTER_TEST(arguments, indexed.module.source_count == 2);
    BUSTER_STRING_TEST(arguments, indexed.module.sources[0].path, S8("alpha.bbb"));
    BUSTER_STRING_TEST(arguments, indexed.module.sources[1].path, S8("zeta.bbb"));
    BUSTER_TEST(arguments, indexed.module.sources[0].id.value == 0);
    BUSTER_TEST(arguments, indexed.module.sources[1].id.value == 1);
    BUSTER_TEST(arguments, indexed.module.entity_count == 4);
    BUSTER_STRING_TEST(arguments, indexed.module.entities[0].name, S8("duplicate"));
    BUSTER_STRING_TEST(arguments, indexed.module.entities[1].name, S8("Shared"));
    BUSTER_STRING_TEST(arguments, indexed.module.entities[2].name, S8("Shared"));
    BUSTER_STRING_TEST(arguments, indexed.module.entities[3].name, S8("duplicate"));
    BUSTER_TEST(arguments, indexed.module.entities[3].id.module.value == 7);
    BUSTER_TEST(arguments, indexed.module.entities[3].id.index.value == 3);

    // Type and value namespaces may use the same spelling. Only the repeated
    // value declaration is diagnosed.
    BUSTER_TEST(arguments, indexed.diagnostic_count == 1);
    BUSTER_TEST(arguments, indexed.first_diagnostic != 0);
    BUSTER_TEST(arguments, indexed.first_diagnostic->entity.index.value == 3);
    BUSTER_TEST(arguments, indexed.first_diagnostic->previous_entity.index.value == 0);

    AnalysisSourceInput forward_inputs[] = {
        {.path = S8("alpha.bbb"), .parser = &alpha_parser},
        {.path = S8("zeta.bbb"), .parser = &zeta_parser},
    };
    AnalysisResult forward =
        analysis_index_module(arguments->arena, (AnalysisModuleId){.value = 7}, S8("example"), forward_inputs, BUSTER_ARRAY_LENGTH(forward_inputs));
    BUSTER_TEST(arguments, forward.module.entity_count == indexed.module.entity_count);
    for (u32 index = 0; index < indexed.module.entity_count; index += 1)
    {
        BUSTER_STRING_TEST(arguments, forward.module.entities[index].name, indexed.module.entities[index].name);
        BUSTER_TEST(arguments, forward.module.entities[index].id.index.value == indexed.module.entities[index].id.index.value);
        BUSTER_TEST(arguments, forward.module.entities[index].source.value == indexed.module.entities[index].source.value);
    }
    String8 indexed_summary = analysis_serialize_module_interface(arguments->arena, &indexed);
    String8 forward_summary = analysis_serialize_module_interface(arguments->arena, &forward);
    BUSTER_STRING_TEST(arguments, indexed_summary, forward_summary);

    AstImport app_missing_import = {
        .name_space = {.text = S8("missing"), .range = analysis_test_range(10)},
        .path = S8("does/not/exist"),
        .range = analysis_test_range(10),
        .path_range = analysis_test_range(20),
    };
    AstImport app_math_import = {
        .next = &app_missing_import,
        .name_space = {.text = S8("math"), .range = analysis_test_range(0)},
        .path = S8("core/math"),
        .range = analysis_test_range(0),
        .path_range = analysis_test_range(5),
    };
    ParserResult app_parser = {
        .first_import = &app_math_import,
        .last_import = &app_missing_import,
        .import_count = 2,
    };
    AstCode sum_code = {
        .name = S8("sum"),
        .range = analysis_test_range(0),
    };
    ParserResult math_parser = {
        .first_code = &sum_code,
        .last_code = &sum_code,
        .code_count = 1,
    };
    AstImport a_import = {
        .name_space = {.text = S8("b"), .range = analysis_test_range(0)},
        .path = S8("cycle/b"),
        .range = analysis_test_range(0),
    };
    AstImport b_import = {
        .name_space = {.text = S8("a"), .range = analysis_test_range(0)},
        .path = S8("cycle/a"),
        .range = analysis_test_range(0),
    };
    ParserResult a_parser = {
        .first_import = &a_import,
        .last_import = &a_import,
        .import_count = 1,
    };
    ParserResult b_parser = {
        .first_import = &b_import,
        .last_import = &b_import,
        .import_count = 1,
    };
    AnalysisSourceInput app_input = {.path = S8("app.bbb"), .parser = &app_parser};
    AnalysisSourceInput math_input = {.path = S8("math.bbb"), .parser = &math_parser};
    AnalysisSourceInput a_input = {.path = S8("a.bbb"), .parser = &a_parser};
    AnalysisSourceInput b_input = {.path = S8("b.bbb"), .parser = &b_parser};
    AnalysisResult app_module = analysis_index_module(arguments->arena, (AnalysisModuleId){.value = 20}, S8("app"), &app_input, 1);
    AnalysisResult math_module = analysis_index_module(arguments->arena, (AnalysisModuleId){.value = 21}, S8("core/math"), &math_input, 1);
    AnalysisResult a_module = analysis_index_module(arguments->arena, (AnalysisModuleId){.value = 22}, S8("cycle/a"), &a_input, 1);
    AnalysisResult b_module = analysis_index_module(arguments->arena, (AnalysisModuleId){.value = 23}, S8("cycle/b"), &b_input, 1);
    AnalysisResult* import_modules[] = {
        &app_module,
        &math_module,
        &a_module,
        &b_module,
    };
    analysis_resolve_imports(arguments->arena, import_modules, BUSTER_ARRAY_LENGTH(import_modules));
    BUSTER_TEST(arguments, app_module.module.import_count == 2);
    BUSTER_TEST(arguments, app_module.module.imports[0].state == ANALYSIS_IMPORT_RESOLVED);
    BUSTER_TEST(arguments, app_module.module.imports[0].target == &math_module);
    BUSTER_TEST(arguments, app_module.module.imports[1].state == ANALYSIS_IMPORT_MISSING);
    BUSTER_TEST(arguments, app_module.diagnostic_count == 1);
    BUSTER_TEST(arguments, app_module.first_diagnostic != 0 && app_module.first_diagnostic->kind == ANALYSIS_DIAGNOSTIC_MISSING_IMPORTED_MODULE);
    AnalysisEntity* qualified_sum = analysis_find_qualified_entity(&app_module, S8("math"), S8("sum"), ANALYSIS_NAMESPACE_VALUE);
    BUSTER_TEST(arguments, qualified_sum == math_module.module.entities);
    BUSTER_TEST(arguments, b_module.module.imports[0].state == ANALYSIS_IMPORT_CYCLE);
    BUSTER_TEST(arguments, b_module.diagnostic_count == 1);
    BUSTER_TEST(arguments, b_module.first_diagnostic != 0 && b_module.first_diagnostic->kind == ANALYSIS_DIAGNOSTIC_IMPORT_CYCLE);

    AstImport duplicate_second = {
        .name_space = {.text = S8("same"), .range = analysis_test_range(10)},
        .path = S8("two"),
        .range = analysis_test_range(10),
    };
    AstImport duplicate_first = {
        .next = &duplicate_second,
        .name_space = {.text = S8("same"), .range = analysis_test_range(0)},
        .path = S8("one"),
        .range = analysis_test_range(0),
    };
    ParserResult duplicate_import_parser = {
        .first_import = &duplicate_first,
        .last_import = &duplicate_second,
        .import_count = 2,
    };
    AnalysisSourceInput duplicate_import_input = {
        .path = S8("duplicate-import.bbb"),
        .parser = &duplicate_import_parser,
    };
    AnalysisResult duplicate_import_module =
        analysis_index_module(arguments->arena, (AnalysisModuleId){.value = 24}, S8("duplicate-import"), &duplicate_import_input, 1);
    BUSTER_TEST(arguments, duplicate_import_module.diagnostic_count == 1);
    BUSTER_TEST(arguments, duplicate_import_module.first_diagnostic != 0 &&
                               duplicate_import_module.first_diagnostic->kind == ANALYSIS_DIAGNOSTIC_DUPLICATE_IMPORT_NAMESPACE);
    if (duplicate_import_module.first_diagnostic)
    {
        BUSTER_TEST(arguments, duplicate_import_module.first_diagnostic->first_note != 0);
    }

    AstType named_b = {
        .range = analysis_test_range(1),
        .id = AST_TYPE_NAMED,
        .name = S8("B"),
    };
    AstType named_s32 = {
        .range = analysis_test_range(11),
        .id = AST_TYPE_NAMED,
        .name = S8("s32"),
    };
    AstType named_a = {
        .range = analysis_test_range(21),
        .id = AST_TYPE_NAMED,
        .name = S8("A"),
    };
    AstType pointer_a = {
        .range = analysis_test_range(20),
        .id = AST_TYPE_POINTER,
        .element_type = &named_a,
    };
    AstType named_node = {
        .range = analysis_test_range(32),
        .id = AST_TYPE_NAMED,
        .name = S8("Node"),
    };
    AstType pointer_node = {
        .range = analysis_test_range(31),
        .id = AST_TYPE_POINTER,
        .element_type = &named_node,
    };
    AstType missing = {
        .range = analysis_test_range(41),
        .id = AST_TYPE_NAMED,
        .name = S8("Missing"),
    };
    AstType named_d = {
        .range = analysis_test_range(51),
        .id = AST_TYPE_NAMED,
        .name = S8("D"),
    };
    AstType named_c = {
        .range = analysis_test_range(61),
        .id = AST_TYPE_NAMED,
        .name = S8("C"),
    };

    AstTypeDeclaration alias_a = {
        .alias_type = &named_b,
        .name = {.text = S8("A")},
        .range = analysis_test_range(0),
        .kind = AST_TYPE_DECLARATION_ALIAS,
    };
    AstTypeDeclaration alias_b = {
        .alias_type = &named_s32,
        .name = {.text = S8("B")},
        .range = analysis_test_range(10),
        .kind = AST_TYPE_DECLARATION_ALIAS,
    };
    AstTypeDeclaration alias_pointer = {
        .alias_type = &pointer_a,
        .name = {.text = S8("Pointer")},
        .range = analysis_test_range(20),
        .kind = AST_TYPE_DECLARATION_ALIAS,
    };
    AstTypeField node_next = {
        .name = {.text = S8("next")},
        .type = &pointer_node,
        .range = analysis_test_range(31),
    };
    AstTypeDeclaration node = {
        .first_field = &node_next,
        .last_field = &node_next,
        .name = {.text = S8("Node")},
        .range = analysis_test_range(30),
        .kind = AST_TYPE_DECLARATION_STRUCT,
        .field_count = 1,
    };
    AstTypeDeclaration unknown = {
        .alias_type = &missing,
        .name = {.text = S8("Unknown")},
        .range = analysis_test_range(40),
        .kind = AST_TYPE_DECLARATION_ALIAS,
    };
    AstTypeDeclaration alias_c = {
        .alias_type = &named_d,
        .name = {.text = S8("C")},
        .range = analysis_test_range(50),
        .kind = AST_TYPE_DECLARATION_ALIAS,
    };
    AstTypeDeclaration alias_d = {
        .alias_type = &named_c,
        .name = {.text = S8("D")},
        .range = analysis_test_range(60),
        .kind = AST_TYPE_DECLARATION_ALIAS,
    };
    alias_a.next = &alias_b;
    alias_b.next = &alias_pointer;
    alias_pointer.next = &node;
    node.next = &unknown;
    unknown.next = &alias_c;
    alias_c.next = &alias_d;

    AstType named_pointer = {
        .range = analysis_test_range(72),
        .id = AST_TYPE_NAMED,
        .name = S8("Pointer"),
    };
    AstType function_return = {
        .range = analysis_test_range(73),
        .id = AST_TYPE_NAMED,
        .name = S8("A"),
    };
    AstTypeArgument function_argument = {
        .name = S8("value"),
        .type = &named_pointer,
        .range = analysis_test_range(72),
    };
    AstType function_type = {
        .range = analysis_test_range(71),
        .id = AST_TYPE_FUNCTION,
        .function =
            {
                .first_argument = &function_argument,
                .last_argument = &function_argument,
                .return_type = &function_return,
                .calling_convention = AST_CALLING_CONVENTION_C,
                .argument_count = 1,
            },
    };
    AstCode function = {
        .name = S8("consume"),
        .type = &function_type,
        .range = analysis_test_range(70),
    };
    ParserResult resolution_parser = {
        .first_code = &function,
        .last_code = &function,
        .first_type_declaration = &alias_a,
        .last_type_declaration = &alias_d,
        .code_count = 1,
        .type_declaration_count = 7,
    };
    AnalysisSourceInput resolution_input = {
        .path = S8("types.bbb"),
        .parser = &resolution_parser,
    };
    AnalysisResult resolved = analysis_index_module(arguments->arena, (AnalysisModuleId){.value = 9}, S8("types"), &resolution_input, 1);
    analysis_resolve_module_interfaces(arguments->arena, &resolved);

    BUSTER_TEST(arguments, resolved.types.count > 13);
    BUSTER_TEST(arguments, resolved.module.semantics[0].type.value == resolved.types.builtin.s32_type.value);
    BUSTER_TEST(arguments, resolved.module.semantics[1].type.value == resolved.types.builtin.s32_type.value);
    AnalysisType* pointer_type = analysis_type_from_id(&resolved, resolved.module.semantics[2].type);
    BUSTER_TEST(arguments, pointer_type->kind == ANALYSIS_TYPE_POINTER);
    BUSTER_TEST(arguments, pointer_type->as.element_type.value == resolved.types.builtin.s32_type.value);

    AnalysisType* node_type = analysis_type_from_id(&resolved, resolved.module.semantics[3].type);
    BUSTER_TEST(arguments, node_type->kind == ANALYSIS_TYPE_STRUCT);
    BUSTER_TEST(arguments, resolved.module.semantics[3].state == ANALYSIS_RESOLUTION_RESOLVED);
    BUSTER_TEST(arguments, resolved.module.semantics[3].field_count == 1);
    AnalysisType* next_type = analysis_type_from_id(&resolved, resolved.module.semantics[3].fields[0].type);
    BUSTER_TEST(arguments, next_type->kind == ANALYSIS_TYPE_POINTER);
    BUSTER_TEST(arguments, next_type->as.element_type.value == node_type->id.value);

    BUSTER_TEST(arguments, resolved.module.semantics[4].state == ANALYSIS_RESOLUTION_ERROR);
    BUSTER_TEST(arguments, resolved.module.semantics[5].state == ANALYSIS_RESOLUTION_ERROR);
    BUSTER_TEST(arguments, resolved.module.semantics[6].state == ANALYSIS_RESOLUTION_ERROR);
    AnalysisType* code_type = analysis_type_from_id(&resolved, resolved.module.semantics[7].type);
    BUSTER_TEST(arguments, code_type->kind == ANALYSIS_TYPE_FUNCTION);
    BUSTER_TEST(arguments, code_type->as.function.argument_count == 1);
    BUSTER_TEST(arguments, code_type->as.function.argument_types[0].value == resolved.module.semantics[2].type.value);
    BUSTER_TEST(arguments, code_type->as.function.return_type.value == resolved.types.builtin.s32_type.value);

    BUSTER_TEST(arguments, resolved.diagnostic_count == 2);
    BUSTER_TEST(arguments, resolved.first_diagnostic->kind == ANALYSIS_DIAGNOSTIC_UNKNOWN_TYPE);
    BUSTER_STRING_TEST(arguments, resolved.first_diagnostic->subject, S8("Missing"));
    BUSTER_TEST(arguments, resolved.last_diagnostic->kind == ANALYSIS_DIAGNOSTIC_TYPE_ALIAS_CYCLE);
    analysis_compute_layouts(&resolved, (AnalysisLayoutOptions){.pointer_size = 8, .pointer_alignment = 8});
    BUSTER_TEST(arguments, node_type->layout.state == ANALYSIS_LAYOUT_RESOLVED);
    BUSTER_TEST(arguments, node_type->layout.size == 8);
    BUSTER_TEST(arguments, node_type->layout.alignment == 8);
    BUSTER_TEST(arguments, resolved.module.semantics[3].fields[0].offset == 0);
    BUSTER_TEST(arguments, code_type->layout.abi_class == ANALYSIS_ABI_CLASS_POINTER);
    AnalysisFunctionAbi systemv_abi =
        analysis_classify_function_abi(arguments->arena, &resolved, code_type->id, (Target){.cpu_arch = CPU_ARCH_X86_64, .os = OPERATING_SYSTEM_LINUX});
    BUSTER_TEST(arguments, systemv_abi.convention == ANALYSIS_ABI_CONVENTION_SYSTEMV_X86_64);
    BUSTER_TEST(arguments, systemv_abi.argument_count == 1);
    BUSTER_TEST(arguments, systemv_abi.arguments[0].parts[0].register_index == 0);
    BUSTER_TEST(arguments, systemv_abi.result.parts[0].register_index == 0);
    BUSTER_TEST(arguments, systemv_abi.stack_size == 0);
    AnalysisFunctionAbi windows_abi =
        analysis_classify_function_abi(arguments->arena, &resolved, code_type->id, (Target){.cpu_arch = CPU_ARCH_X86_64, .os = OPERATING_SYSTEM_WINDOWS});
    BUSTER_TEST(arguments, windows_abi.convention == ANALYSIS_ABI_CONVENTION_WIN64_X86_64);
    BUSTER_TEST(arguments, windows_abi.stack_size == 32);
    AnalysisFunctionAbi aapcs_abi =
        analysis_classify_function_abi(arguments->arena, &resolved, code_type->id, (Target){.cpu_arch = CPU_ARCH_AARCH64, .os = OPERATING_SYSTEM_LINUX});
    BUSTER_TEST(arguments, aapcs_abi.convention == ANALYSIS_ABI_CONVENTION_AAPCS64);
    BUSTER_TEST(arguments, aapcs_abi.arguments[0].parts[0].register_index == 0);
    AnalysisFunctionAbi windows_aarch64_abi = analysis_classify_function_abi(arguments->arena, &resolved, code_type->id,
                                                                             (Target){
                                                                                 .cpu_arch = CPU_ARCH_AARCH64,
                                                                                 .os = OPERATING_SYSTEM_WINDOWS,
                                                                             });
    BUSTER_TEST(arguments, windows_aarch64_abi.convention == ANALYSIS_ABI_CONVENTION_WINDOWS_AARCH64);
    BUSTER_TEST(arguments, windows_aarch64_abi.arguments[0].parts[0].register_index == 0);
    AnalysisFunctionAbi uefi_x86_64_abi = analysis_classify_function_abi(arguments->arena, &resolved, code_type->id,
                                                                         (Target){
                                                                             .cpu_arch = CPU_ARCH_X86_64,
                                                                             .os = OPERATING_SYSTEM_UEFI,
                                                                         });
    BUSTER_TEST(arguments, uefi_x86_64_abi.convention == ANALYSIS_ABI_CONVENTION_WIN64_X86_64);
    analysis_build_jobs(arguments->arena, &resolved);
    BUSTER_TEST(arguments, resolved.job_count == 16);
    BUSTER_TEST(arguments, resolved.jobs[0].kind == ANALYSIS_JOB_INTERFACE);
    BUSTER_TEST(arguments, resolved.jobs[8].kind == ANALYSIS_JOB_LAYOUT);
    BUSTER_TEST(arguments, resolved.jobs[15].kind == ANALYSIS_JOB_BODY);
    BUSTER_TEST(arguments, resolved.jobs[15].dependency_count == 1);
    BUSTER_TEST(arguments, resolved.jobs[15].dependency_kinds[0] == ANALYSIS_DEPENDENCY_INTERFACE);
    AnalysisScheduleResult schedule = analysis_execute_jobs(arguments->arena, &resolved, 4, 0, 0);
    BUSTER_TEST(arguments, !schedule.has_cycle);
    BUSTER_TEST(arguments, schedule.execution_count == resolved.job_count);
    BUSTER_TEST(arguments, schedule.wave_count == 2);
    for (u32 execution_index = 0; execution_index < schedule.execution_count; execution_index += 1)
    {
        AnalysisJob* scheduled = resolved.jobs + schedule.execution_order[execution_index].value;
        for (u32 dependency_index = 0; dependency_index < scheduled->dependency_count; dependency_index += 1)
        {
            bool dependency_precedes = false;
            for (u32 previous = 0; previous < execution_index; previous += 1)
            {
                dependency_precedes |= schedule.execution_order[previous].value == scheduled->dependencies[dependency_index].value;
            }
            BUSTER_TEST(arguments, dependency_precedes);
        }
    }

    String8 body_source = S8("code add : fn (a: s32, b: s32) s32\n"
                             "{\n"
                             "    data total = a + b;\n"
                             "    if (a < b)\n"
                             "    {\n"
                             "        data a: s32 = b;\n"
                             "        total += a;\n"
                             "    }\n"
                             "    else\n"
                             "    {\n"
                             "        total += 1;\n"
                             "    }\n"
                             "    for (data i = 0 .. 2)\n"
                             "    {\n"
                             "        total += i;\n"
                             "    }\n"
                             "    loop (total < 100)\n"
                             "    {\n"
                             "        total += 1;\n"
                             "        break;\n"
                             "    }\n"
                             "    return total;\n"
                             "}\n"
                             "code call : fn () s32\n"
                             "{\n"
                             "    return add(1, 2);\n"
                             "}\n");
    Arena* expression_arena = arena_create((ArenaCreation){0});
    BUSTER_CHECK(expression_arena);
    TokenizerResult body_tokens = tokenize(arguments->arena, body_source.pointer, body_source.length);
    ParserResult body_parser = parser_parse(arguments->arena, expression_arena, body_source, body_tokens);
    BUSTER_TEST(arguments, body_tokens.error_count == 0);
    BUSTER_TEST(arguments, body_parser.diagnostic_count == 0);
    AnalysisSourceInput body_input = {
        .path = S8("semantic-body.bbb"),
        .parser = &body_parser,
    };
    AnalysisResult body_result = analysis_index_module(arguments->arena, (AnalysisModuleId){.value = 10}, S8("semantic-body"), &body_input, 1);
    analysis_resolve_module_interfaces(arguments->arena, &body_result);
    analysis_analyze_bodies(arguments->arena, &body_result);
    BUSTER_TEST(arguments, body_result.diagnostic_count == 0);
    BUSTER_TEST(arguments, body_result.module.code_count == 2);
    for (u32 index = 0; index < body_result.module.entity_count; index += 1)
    {
        AnalysisEntity* entity = body_result.module.entities + index;
        AnalysisBody* body = body_result.module.bodies + index;
        BUSTER_TEST(arguments, body->analyzed);
        if (string_equal(entity->name, S8("add")))
        {
            BUSTER_TEST(arguments, body->local_count == 5);
            BUSTER_TEST(arguments, body->locals[0].kind == ANALYSIS_LOCAL_ARGUMENT);
            BUSTER_TEST(arguments, body->locals[0].is_mutable);
            BUSTER_TEST(arguments, body->locals[0].is_initialized);
            BUSTER_TEST(arguments, body->locals[2].kind == ANALYSIS_LOCAL_DATA);
            BUSTER_TEST(arguments, body->locals[2].is_mutable);
            BUSTER_TEST(arguments, body->locals[2].is_initialized);
            BUSTER_TEST(arguments, body->locals[3].scope_depth == 1);
            BUSTER_TEST(arguments, body->locals[4].kind == ANALYSIS_LOCAL_FOR);
            BUSTER_TEST(arguments, body->expression_count == 14);
        }
        else
        {
            BUSTER_STRING_TEST(arguments, entity->name, S8("call"));
            BUSTER_TEST(arguments, body->local_count == 0);
            BUSTER_TEST(arguments, body->expression_count == 1);
            BUSTER_TEST(arguments, body->first_expression->type.value == body_result.types.builtin.s32_type.value);
        }
    }

    String8 constant_source = S8("code constants : fn () f64\n"
                                 "{\n"
                                 "    data sum: f64 = 1.25 + 2.75;\n"
                                 "    data hexadecimal: f64 = 0x1.fp+2;\n"
                                 "    data integer: s32 = @cast(3.75);\n"
                                 "    return sum + hexadecimal + @cast(integer);\n"
                                 "}\n");
    TokenizerResult constant_tokens = tokenize(arguments->arena, constant_source.pointer, constant_source.length);
    ParserResult constant_parser = parser_parse(arguments->arena, expression_arena, constant_source, constant_tokens);
    BUSTER_TEST(arguments, constant_tokens.error_count == 0);
    BUSTER_TEST(arguments, constant_parser.diagnostic_count == 0);
    AnalysisSourceInput constant_input = {
        .path = S8("semantic-constants.bbb"),
        .parser = &constant_parser,
    };
    AnalysisResult constant_result = analysis_index_module(arguments->arena, (AnalysisModuleId){.value = 12}, S8("semantic-constants"), &constant_input, 1);
    analysis_resolve_module_interfaces(arguments->arena, &constant_result);
    analysis_analyze_bodies(arguments->arena, &constant_result);
    BUSTER_TEST(arguments, constant_result.diagnostic_count == 0);
    AnalysisTypedExpression* constant_expression = constant_result.module.bodies[0].first_expression;
    BUSTER_TEST(arguments, constant_expression != 0);
    if (constant_expression)
    {
        AnalysisConstant folded = constant_expression->nodes[constant_expression->ast.count - 1].constant;
        BUSTER_TEST(arguments, folded.kind == ANALYSIS_CONSTANT_FLOAT);
        BUSTER_TEST(arguments, folded.floating == 4.0);
        constant_expression = constant_expression->next;
    }
    BUSTER_TEST(arguments, constant_expression != 0);
    if (constant_expression)
    {
        AnalysisConstant hexadecimal = constant_expression->nodes[constant_expression->ast.count - 1].constant;
        BUSTER_TEST(arguments, hexadecimal.kind == ANALYSIS_CONSTANT_FLOAT);
        BUSTER_TEST(arguments, hexadecimal.floating == 7.75);
        constant_expression = constant_expression->next;
    }
    BUSTER_TEST(arguments, constant_expression != 0);
    if (constant_expression)
    {
        AnalysisConstant cast = constant_expression->nodes[constant_expression->ast.count - 1].constant;
        BUSTER_TEST(arguments, cast.kind == ANALYSIS_CONSTANT_INTEGER);
        BUSTER_TEST(arguments, cast.integer == 3 && !cast.is_negative);
    }

    String8 initialization_source = S8("code initialization : fn (condition: bool) s32\n"
                                       "{\n"
                                       "    data complete: s32 = undefined;\n"
                                       "    if (condition) { complete = 1; } else { complete = 2; }\n"
                                       "    data partial: s32 = undefined;\n"
                                       "    if (condition) { partial = 1; }\n"
                                       "    data bad: s32 = partial;\n"
                                       "    data compound: s32 = undefined;\n"
                                       "    compound += 1;\n"
                                       "    data addressed: s32 = undefined;\n"
                                       "    data pointer: &s32 = &addressed;\n"
                                       "    addressed = 3;\n"
                                       "    data through_break: s32 = undefined;\n"
                                       "    loop\n"
                                       "    {\n"
                                       "        if (condition) { through_break = 4; break; }\n"
                                       "        else { through_break = 5; break; }\n"
                                       "    }\n"
                                       "    return complete + addressed + through_break;\n"
                                       "}\n");
    TokenizerResult initialization_tokens = tokenize(arguments->arena, initialization_source.pointer, initialization_source.length);
    ParserResult initialization_parser = parser_parse(arguments->arena, expression_arena, initialization_source, initialization_tokens);
    BUSTER_TEST(arguments, initialization_tokens.error_count == 0);
    BUSTER_TEST(arguments, initialization_parser.diagnostic_count == 0);
    AnalysisSourceInput initialization_input = {
        .path = S8("semantic-initialization.bbb"),
        .parser = &initialization_parser,
    };
    AnalysisResult initialization_result =
        analysis_index_module(arguments->arena, (AnalysisModuleId){.value = 13}, S8("semantic-initialization"), &initialization_input, 1);
    analysis_resolve_module_interfaces(arguments->arena, &initialization_result);
    analysis_analyze_bodies(arguments->arena, &initialization_result);
    BUSTER_TEST(arguments, initialization_result.diagnostic_count == 2);
    for (AnalysisDiagnostic* diagnostic = initialization_result.first_diagnostic; diagnostic; diagnostic = diagnostic->next)
    {
        BUSTER_TEST(arguments, diagnostic->kind == ANALYSIS_DIAGNOSTIC_USE_BEFORE_INITIALIZATION);
        BUSTER_TEST(arguments, diagnostic->first_note != 0);
        if (diagnostic->first_note)
        {
            BUSTER_STRING_TEST(arguments, diagnostic->first_note->message, S8("local is declared uninitialized here"));
        }
    }

    String8 error_source = S8("code broken : fn (value: s32) s32\n"
                              "{\n"
                              "    data value: s32 = 0;\n"
                              "    data result: s32 = missing;\n"
                              "    1 = result;\n"
                              "    break;\n"
                              "    return result;\n"
                              "}\n");
    TokenizerResult error_tokens = tokenize(arguments->arena, error_source.pointer, error_source.length);
    ParserResult error_parser = parser_parse(arguments->arena, expression_arena, error_source, error_tokens);
    BUSTER_TEST(arguments, error_parser.diagnostic_count == 0);
    AnalysisSourceInput error_input = {
        .path = S8("semantic-errors.bbb"),
        .parser = &error_parser,
    };
    AnalysisResult error_result = analysis_index_module(arguments->arena, (AnalysisModuleId){.value = 11}, S8("semantic-errors"), &error_input, 1);
    analysis_resolve_module_interfaces(arguments->arena, &error_result);
    analysis_analyze_bodies(arguments->arena, &error_result);
    BUSTER_TEST(arguments, error_result.diagnostic_count == 5);
    AnalysisDiagnosticKind expected_diagnostics[] = {
        ANALYSIS_DIAGNOSTIC_DUPLICATE_LOCAL,      ANALYSIS_DIAGNOSTIC_UNKNOWN_IDENTIFIER,    ANALYSIS_DIAGNOSTIC_EXPECTED_PLACE,
        ANALYSIS_DIAGNOSTIC_INVALID_CONTROL_FLOW, ANALYSIS_DIAGNOSTIC_UNREACHABLE_STATEMENT,
    };
    AnalysisDiagnostic* diagnostic = error_result.first_diagnostic;
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(expected_diagnostics); index += 1)
    {
        BUSTER_TEST(arguments, diagnostic != 0);
        BUSTER_TEST(arguments, diagnostic->kind == expected_diagnostics[index]);
        diagnostic = diagnostic->next;
    }
    BUSTER_TEST(arguments, diagnostic == 0);

    String8 namespace_math_source = S8("type Vector = struct { x: s32, y: s32, }\n"
                                       "code add : fn (a: s32, b: s32) s32\n"
                                       "{\n"
                                       "    return a + b;\n"
                                       "}\n"
                                       "code identity : fn ($value: $T) $T\n"
                                       "{\n"
                                       "    return value;\n"
                                       "}\n");
    String8 namespace_app_source = S8("import math = \"core/math\";\n"
                                      "code use : fn (value: math.Vector) s32\n"
                                      "{\n"
                                      "    data copy: math.Vector = value;\n"
                                      "    return math.add(copy.x, copy.y) + math.identity(5);\n"
                                      "}\n");
    String8 namespace_app_two_source = S8("import math = \"core/math\";\n"
                                          "code use_again : fn () s32\n"
                                          "{\n"
                                          "    return math.identity(5);\n"
                                          "}\n");
    TokenizerResult namespace_math_tokens = tokenize(arguments->arena, namespace_math_source.pointer, namespace_math_source.length);
    ParserResult namespace_math_parser = parser_parse(arguments->arena, expression_arena, namespace_math_source, namespace_math_tokens);
    TokenizerResult namespace_app_tokens = tokenize(arguments->arena, namespace_app_source.pointer, namespace_app_source.length);
    ParserResult namespace_app_parser = parser_parse(arguments->arena, expression_arena, namespace_app_source, namespace_app_tokens);
    TokenizerResult namespace_app_two_tokens = tokenize(arguments->arena, namespace_app_two_source.pointer, namespace_app_two_source.length);
    ParserResult namespace_app_two_parser = parser_parse(arguments->arena, expression_arena, namespace_app_two_source, namespace_app_two_tokens);
    BUSTER_TEST(arguments, namespace_math_parser.diagnostic_count == 0);
    BUSTER_TEST(arguments, namespace_app_parser.diagnostic_count == 0);
    BUSTER_TEST(arguments, namespace_app_two_parser.diagnostic_count == 0);
    AnalysisSourceInput namespace_math_input = {
        .path = S8("math.bbb"),
        .parser = &namespace_math_parser,
    };
    AnalysisSourceInput namespace_app_input = {
        .path = S8("app.bbb"),
        .parser = &namespace_app_parser,
    };
    AnalysisSourceInput namespace_app_two_input = {
        .path = S8("app-two.bbb"),
        .parser = &namespace_app_two_parser,
    };
    AnalysisResult namespace_math = analysis_index_module(arguments->arena, (AnalysisModuleId){.value = 40}, S8("core/math"), &namespace_math_input, 1);
    AnalysisResult namespace_app = analysis_index_module(arguments->arena, (AnalysisModuleId){.value = 41}, S8("app"), &namespace_app_input, 1);
    AnalysisResult namespace_app_two = analysis_index_module(arguments->arena, (AnalysisModuleId){.value = 42}, S8("app-two"), &namespace_app_two_input, 1);
    AnalysisResult* namespace_modules[] = {
        &namespace_app_two,
        &namespace_math,
        &namespace_app,
    };
    analysis_resolve_program_interfaces(arguments->arena, namespace_modules, BUSTER_ARRAY_LENGTH(namespace_modules));
    analysis_analyze_bodies(arguments->arena, &namespace_math);
    analysis_analyze_bodies(arguments->arena, &namespace_app);
    analysis_analyze_bodies(arguments->arena, &namespace_app_two);
    BUSTER_TEST(arguments, namespace_math.instantiation_count == 1);
    BUSTER_TEST(arguments, namespace_math.first_instantiation && namespace_math.first_instantiation->requester_count == 2);
    BUSTER_TEST(arguments, namespace_math.first_instantiation && namespace_math.first_instantiation->codegen_owner.value == 40);
    analysis_analyze_bodies(arguments->arena, &namespace_math);
    analysis_compute_layouts(&namespace_math, (AnalysisLayoutOptions){.pointer_size = 8, .pointer_alignment = 8});
    analysis_compute_layouts(&namespace_app, (AnalysisLayoutOptions){.pointer_size = 8, .pointer_alignment = 8});
    analysis_compute_layouts(&namespace_app_two, (AnalysisLayoutOptions){.pointer_size = 8, .pointer_alignment = 8});
    BUSTER_TEST(arguments, namespace_math.diagnostic_count == 0);
    BUSTER_TEST(arguments, namespace_app.diagnostic_count == 0);
    BUSTER_TEST(arguments, namespace_app_two.diagnostic_count == 0);
    analysis_build_jobs(arguments->arena, &namespace_math);
    analysis_build_jobs(arguments->arena, &namespace_app);
    analysis_build_jobs(arguments->arena, &namespace_app_two);
    AnalysisResult* scheduled_modules[] = {
        &namespace_app_two,
        &namespace_math,
        &namespace_app,
    };
    AnalysisProgram scheduled_program = {
        .module_results = scheduled_modules,
        .module_count = BUSTER_ARRAY_LENGTH(scheduled_modules),
    };
    AnalysisProgramScheduleResult program_schedule = analysis_execute_program_jobs(arguments->arena, &scheduled_program, 4, 0, 0);
    BUSTER_TEST(arguments, !program_schedule.has_cycle);
    BUSTER_TEST(arguments, program_schedule.execution_count == namespace_math.job_count + namespace_app.job_count + namespace_app_two.job_count);
    BUSTER_TEST(arguments, program_schedule.wave_count >= 2);
    AnalysisEntity* namespace_use = analysis_value_entity_find(&namespace_app, S8("use"));
    BUSTER_TEST(arguments, namespace_use != 0);
    if (namespace_use)
    {
        AnalysisType* namespace_function = analysis_type_from_id(&namespace_app, namespace_app.module.semantics[namespace_use->id.index.value].type);
        BUSTER_TEST(arguments, namespace_function->kind == ANALYSIS_TYPE_FUNCTION);
        BUSTER_TEST(arguments, namespace_function->kind != ANALYSIS_TYPE_FUNCTION || namespace_function->as.function.argument_count == 1);
        if (namespace_function->kind == ANALYSIS_TYPE_FUNCTION && namespace_function->as.function.argument_count == 1)
        {
            AnalysisType* namespace_vector = analysis_type_from_id(&namespace_app, namespace_function->as.function.argument_types[0]);
            BUSTER_TEST(arguments, namespace_vector->kind == ANALYSIS_TYPE_STRUCT);
            BUSTER_TEST(arguments, namespace_vector->as.declaration.module.value == 40);
            BUSTER_TEST(arguments, namespace_vector->layout.state == ANALYSIS_LAYOUT_RESOLVED);
            BUSTER_TEST(arguments, namespace_vector->layout.size == 8);
            BUSTER_TEST(arguments, namespace_vector->layout.alignment == 4);
        }
        AnalysisBody* namespace_body = namespace_app.module.bodies + namespace_use->id.index.value;
        BUSTER_TEST(arguments, namespace_body->analyzed);
        BUSTER_TEST(arguments, namespace_body->dependency_count == 2);
        for (u32 dependency_index = 0; dependency_index < namespace_body->dependency_count; dependency_index += 1)
        {
            BUSTER_TEST(arguments, namespace_body->dependencies[dependency_index].module.value == 40);
        }
    }
    AnalysisInterfaceSummary specialization_summary = analysis_module_interface_summary(arguments->arena, &namespace_math);
    BUSTER_TEST(arguments, analysis_interface_summary_is_valid(specialization_summary));
    BUSTER_TEST(arguments, string_first_sequence(specialization_summary.bytes, S8("specialization ")) != BUSTER_STRING_NO_MATCH);
    BUSTER_TEST(arguments, string_first_sequence(specialization_summary.bytes, S8("requester=app\n")) != BUSTER_STRING_NO_MATCH);
    BUSTER_TEST(arguments, string_first_sequence(specialization_summary.bytes, S8("requester=app-two\n")) != BUSTER_STRING_NO_MATCH);
    AnalysisInterfaceCache specialization_cache = {0};
    BUSTER_TEST(arguments, analysis_interface_cache_store(arguments->arena, &specialization_cache, namespace_math.module.name, specialization_summary));
    AnalysisInterfaceSummary specialization_round_trip = analysis_module_interface_summary(arguments->arena, &namespace_math);
    BUSTER_TEST(arguments, !analysis_interface_cache_store(arguments->arena, &specialization_cache, namespace_math.module.name, specialization_round_trip));
    AnalysisInterfaceCacheEntry* specialization_cache_entry = analysis_interface_cache_find(&specialization_cache, namespace_math.module.name);
    BUSTER_TEST(arguments, specialization_cache_entry != 0);
    BUSTER_TEST(arguments, specialization_cache_entry && specialization_cache_entry->summary.hash == specialization_summary.hash);
    BUSTER_TEST(arguments, specialization_cache_entry && string_equal(specialization_cache_entry->summary.bytes, specialization_summary.bytes));
    AnalysisInterfaceSummary corrupted_summary = specialization_summary;
    corrupted_summary.hash ^= 1;
    BUSTER_TEST(arguments, !analysis_interface_summary_is_valid(corrupted_summary));

    String8 stable_source_left = S8("code identity : fn ($value: $T) $T { return value; }\n"
                                    "code main : fn () s32\n"
                                    "{\n"
                                    "    data first: s32 = identity(5);\n"
                                    "    data second: s32 = identity(6);\n"
                                    "    return first + second;\n"
                                    "}\n");
    String8 stable_source_right = S8("code identity : fn ($value: $T) $T { return value; }\n"
                                     "code main : fn () s32\n"
                                     "{\n"
                                     "    data second: s32 = identity(6);\n"
                                     "    data first: s32 = identity(5);\n"
                                     "    return first + second;\n"
                                     "}\n");
    TokenizerResult stable_left_tokens = tokenize(arguments->arena, stable_source_left.pointer, stable_source_left.length);
    ParserResult stable_left_parser = parser_parse(arguments->arena, expression_arena, stable_source_left, stable_left_tokens);
    TokenizerResult stable_right_tokens = tokenize(arguments->arena, stable_source_right.pointer, stable_source_right.length);
    ParserResult stable_right_parser = parser_parse(arguments->arena, expression_arena, stable_source_right, stable_right_tokens);
    AnalysisSourceInput stable_left_input = {
        .path = S8("stable.bbb"),
        .parser = &stable_left_parser,
    };
    AnalysisSourceInput stable_right_input = {
        .path = S8("stable.bbb"),
        .parser = &stable_right_parser,
    };
    AnalysisResult stable_left = analysis_index_module(arguments->arena, (AnalysisModuleId){.value = 50}, S8("stable"), &stable_left_input, 1);
    AnalysisResult stable_right = analysis_index_module(arguments->arena, (AnalysisModuleId){.value = 99}, S8("stable"), &stable_right_input, 1);
    analysis_resolve_module_interfaces(arguments->arena, &stable_left);
    analysis_resolve_module_interfaces(arguments->arena, &stable_right);
    analysis_analyze_bodies(arguments->arena, &stable_left);
    analysis_analyze_bodies(arguments->arena, &stable_right);
    BUSTER_TEST(arguments, stable_left.instantiation_count == 2);
    BUSTER_TEST(arguments, stable_right.instantiation_count == 2);
    String8 stable_left_five = {0};
    String8 stable_right_five = {0};
    AnalysisInstantiationId stable_left_five_id = ANALYSIS_INSTANTIATION_ID_INVALID;
    AnalysisInstantiationId stable_right_five_id = ANALYSIS_INSTANTIATION_ID_INVALID;
    for (AnalysisInstantiation* instantiation = stable_left.first_instantiation; instantiation; instantiation = instantiation->next)
    {
        if (instantiation->compile_time_argument_count && instantiation->compile_time_arguments[0].constant.integer == 5)
        {
            stable_left_five = instantiation->symbol_name;
            stable_left_five_id = instantiation->id;
        }
    }
    for (AnalysisInstantiation* instantiation = stable_right.first_instantiation; instantiation; instantiation = instantiation->next)
    {
        if (instantiation->compile_time_argument_count && instantiation->compile_time_arguments[0].constant.integer == 5)
        {
            stable_right_five = instantiation->symbol_name;
            stable_right_five_id = instantiation->id;
        }
    }
    BUSTER_STRING_TEST(arguments, stable_left_five, stable_right_five);
    BUSTER_TEST(arguments, stable_left_five_id.value != stable_right_five_id.value);
    AnalysisInterfaceSummary stable_left_summary = analysis_module_interface_summary(arguments->arena, &stable_left);
    AnalysisInterfaceSummary stable_right_summary = analysis_module_interface_summary(arguments->arena, &stable_right);
    BUSTER_STRING_TEST(arguments, stable_left_summary.bytes, stable_right_summary.bytes);
    BUSTER_TEST(arguments, stable_left_summary.hash == stable_right_summary.hash);
    AnalysisInterfaceCache stable_cache = {0};
    BUSTER_TEST(arguments, analysis_interface_cache_store(arguments->arena, &stable_cache, S8("stable"), stable_left_summary));
    BUSTER_TEST(arguments, !analysis_interface_cache_store(arguments->arena, &stable_cache, S8("stable"), stable_right_summary));
    BUSTER_CHECK(arena_destroy(expression_arena, 1));

    Arena* fixture_expression_arena = arena_create((ArenaCreation){0});
    BUSTER_CHECK(fixture_expression_arena);
    String8 rule_source = S8("type Duplicate = struct { value: s32, value: u32, }\n"
                             "type Pair = struct { a: s32, b: s32, }\n"
                             "type Choice = enum { a, a, b = 1 + 2, }\n"
                             "code rules : fn (same: s32, same: s32) s32\n"
                             "{\n"
                             "    data small: u8 = 300;\n"
                             "    data pair: Pair = { .a = 1, .a = 2 };\n"
                             "    data choice: Choice = .a;\n"
                             "    switch (choice)\n"
                             "    {\n"
                             "        .a => {},\n"
                             "        .a => {},\n"
                             "    }\n"
                             "}\n");
    TokenizerResult rule_tokenizer = tokenize(arguments->arena, rule_source.pointer, rule_source.length);
    ParserResult rule_parser = parser_parse(arguments->arena, fixture_expression_arena, rule_source, rule_tokenizer);
    BUSTER_TEST(arguments, rule_parser.diagnostic_count == 0);
    AnalysisSourceInput rule_input = {.path = S8("semantic-rules.bbb"), .parser = &rule_parser};
    AnalysisResult rule_result = analysis_index_module(arguments->arena, (AnalysisModuleId){.value = 12}, S8("semantic-rules"), &rule_input, 1);
    analysis_resolve_module_interfaces(arguments->arena, &rule_result);
    analysis_analyze_bodies(arguments->arena, &rule_result);
    BUSTER_TEST(arguments, analysis_test_has_diagnostic(&rule_result, ANALYSIS_DIAGNOSTIC_DUPLICATE_FIELD));
    BUSTER_TEST(arguments, analysis_test_has_diagnostic(&rule_result, ANALYSIS_DIAGNOSTIC_DUPLICATE_ENUM_MEMBER));
    BUSTER_TEST(arguments, analysis_test_has_diagnostic(&rule_result, ANALYSIS_DIAGNOSTIC_DUPLICATE_LOCAL));
    BUSTER_TEST(arguments, analysis_test_has_diagnostic(&rule_result, ANALYSIS_DIAGNOSTIC_INVALID_CONSTANT));
    BUSTER_TEST(arguments, analysis_test_has_diagnostic(&rule_result, ANALYSIS_DIAGNOSTIC_DUPLICATE_AGGREGATE_FIELD));
    BUSTER_TEST(arguments, analysis_test_has_diagnostic(&rule_result, ANALYSIS_DIAGNOSTIC_MISSING_AGGREGATE_FIELD));
    BUSTER_TEST(arguments, analysis_test_has_diagnostic(&rule_result, ANALYSIS_DIAGNOSTIC_DUPLICATE_SWITCH_CASE));
    BUSTER_TEST(arguments, analysis_test_has_diagnostic(&rule_result, ANALYSIS_DIAGNOSTIC_NONEXHAUSTIVE_SWITCH));
    BUSTER_TEST(arguments, analysis_test_has_diagnostic(&rule_result, ANALYSIS_DIAGNOSTIC_MISSING_RETURN));
    BUSTER_TEST(arguments, rule_result.first_diagnostic != 0);
    if (rule_result.first_diagnostic)
    {
        String8 formatted = analysis_format_diagnostic(arguments->arena, &rule_result, rule_result.first_diagnostic);
        BUSTER_TEST(arguments, string_first_sequence(formatted, S8("semantic-rules.bbb")) != BUSTER_STRING_NO_MATCH);
        BUSTER_TEST(arguments, string_first_sequence(formatted, S8(": error: ")) != BUSTER_STRING_NO_MATCH);
    }

    for (u32 fixture_index = 0; fixture_index < BUSTER_ARRAY_LENGTH(analysis_fixture_tests); fixture_index += 1)
    {
        TemporalArena fixture_temporary = arena_begin_temporal(arguments->arena);
        AnalysisFixtureTest fixture = analysis_fixture_tests[fixture_index];
        FileMapRead source_file = file_map_read(arguments->arena, fixture.path, (FileReadOptions){0});
        String8 source = BYTE_SLICE_TO_STRING(8, source_file.bytes);
        BUSTER_TEST(arguments, source.pointer != 0);
        TokenizerResult tokenizer = tokenize(arguments->arena, source.pointer, source.length);
        ParserResult parser = parser_parse(arguments->arena, fixture_expression_arena, source, tokenizer);
        BUSTER_TEST(arguments, tokenizer.error_count == 0);
        BUSTER_TEST(arguments, parser.diagnostic_count == 0);
        AnalysisSourceInput input = {.path = fixture.path, .parser = &parser};
        AnalysisResult fixture_result = analysis_index_module(arguments->arena, (AnalysisModuleId){.value = 100 + fixture_index}, S8("fixture"), &input, 1);
        analysis_resolve_module_interfaces(arguments->arena, &fixture_result);
        analysis_analyze_bodies(arguments->arena, &fixture_result);
        analysis_compute_layouts(&fixture_result, (AnalysisLayoutOptions){.pointer_size = 8, .pointer_alignment = 8});
        analysis_build_jobs(arguments->arena, &fixture_result);
        BUSTER_TEST(arguments, fixture_result.diagnostic_count == fixture.expected_diagnostic_count);
        if (string_equal(fixture.path, S8("tests/basic_vector.bbb")))
        {
            AnalysisEntity* main_entity = analysis_value_entity_find(&fixture_result, S8("main"));
            BUSTER_TEST(arguments, main_entity != 0);
            if (main_entity)
            {
                AnalysisBody* body = fixture_result.module.bodies + main_entity->id.index.value;
                AnalysisLocal* sum = 0;
                AnalysisLocal* less = 0;
                for (u32 local_index = 0; local_index < body->local_count; local_index += 1)
                {
                    if (string_equal(body->locals[local_index].name, S8("sum")))
                    {
                        sum = body->locals + local_index;
                    }
                    if (string_equal(body->locals[local_index].name, S8("less")))
                    {
                        less = body->locals + local_index;
                    }
                }
                BUSTER_TEST(arguments, sum != 0);
                if (sum)
                {
                    AnalysisType* type = analysis_type_from_id(&fixture_result, sum->type);
                    BUSTER_TEST(arguments, type->kind == ANALYSIS_TYPE_VECTOR);
                    BUSTER_TEST(arguments, type->as.vector.count == 4);
                    BUSTER_TEST(arguments, type->layout.size == 16);
                    BUSTER_TEST(arguments, type->layout.abi_class == ANALYSIS_ABI_CLASS_VECTOR);
                }
                BUSTER_TEST(arguments, less != 0);
                if (less)
                {
                    AnalysisType* mask = analysis_type_from_id(&fixture_result, less->type);
                    AnalysisType* lane = mask->kind == ANALYSIS_TYPE_VECTOR ? analysis_type_from_id(&fixture_result, mask->as.vector.element_type) : 0;
                    BUSTER_TEST(arguments, mask->kind == ANALYSIS_TYPE_VECTOR);
                    BUSTER_TEST(arguments, mask->as.vector.count == 4);
                    BUSTER_TEST(arguments, lane && lane->kind == ANALYSIS_TYPE_INTEGER && !lane->as.integer.is_signed && lane->as.integer.bit_width == 32);
                }
            }
        }
        if (string_equal(fixture.path, S8("tests/basic_compile_time.bbb")))
        {
            AnalysisEntity* constant = analysis_value_entity_find(&fixture_result, S8("this_is_a_constant"));
            BUSTER_TEST(arguments, constant && constant->kind == ANALYSIS_ENTITY_DATA);
            if (constant)
            {
                AnalysisEntitySemantic* semantic = fixture_result.module.semantics + constant->id.index.value;
                BUSTER_TEST(arguments, semantic->constant && semantic->constant->kind == ANALYSIS_CONSTANT_INTEGER);
                BUSTER_TEST(arguments, semantic->constant && semantic->constant->integer == 5);
            }
            AnalysisEntity* generic = analysis_value_entity_find(&fixture_result, S8("identity"));
            BUSTER_TEST(arguments, generic != 0);
            if (generic)
            {
                AnalysisType* generic_function = analysis_type_from_id(&fixture_result, fixture_result.module.semantics[generic->id.index.value].type);
                BUSTER_TEST(arguments, generic_function->kind == ANALYSIS_TYPE_FUNCTION);
                BUSTER_TEST(arguments, generic_function->kind != ANALYSIS_TYPE_FUNCTION ||
                                           analysis_type_from_id(&fixture_result, generic_function->as.function.return_type)->kind ==
                                               ANALYSIS_TYPE_COMPILE_TIME_PARAMETER);
            }
            BUSTER_TEST(arguments, fixture_result.instantiation_count == 3);
            u32 analyzed_instantiation_count = 0;
            for (AnalysisInstantiation* instantiation = fixture_result.first_instantiation; instantiation; instantiation = instantiation->next)
            {
                AnalysisType* concrete = analysis_type_from_id(&fixture_result, instantiation->function_type);
                BUSTER_TEST(arguments, instantiation->analyzed);
                BUSTER_TEST(arguments, instantiation->body.analyzed);
                BUSTER_TEST(arguments, concrete->kind == ANALYSIS_TYPE_FUNCTION);
                BUSTER_TEST(arguments, concrete->kind != ANALYSIS_TYPE_FUNCTION || concrete->as.function.argument_count == 0);
                analyzed_instantiation_count += instantiation->analyzed;
            }
            BUSTER_TEST(arguments, analyzed_instantiation_count == 3);
            u32 specialized_body_job_count = 0;
            u32 ordinary_body_job_count = 0;
            for (u32 job_index = 0; job_index < fixture_result.job_count; job_index += 1)
            {
                AnalysisJob* job = fixture_result.jobs + job_index;
                if (job->kind != ANALYSIS_JOB_BODY)
                {
                    continue;
                }
                if (job->instantiation.value == ANALYSIS_ID_UNDERLYING_INVALID)
                {
                    ordinary_body_job_count += 1;
                }
                else
                {
                    specialized_body_job_count += 1;
                }
            }
            BUSTER_TEST(arguments, ordinary_body_job_count == 1);
            BUSTER_TEST(arguments, specialized_body_job_count == 3);
            AnalysisScheduleResult generic_schedule = analysis_execute_jobs(arguments->arena, &fixture_result, 2, 0, 0);
            BUSTER_TEST(arguments, !generic_schedule.has_cycle);
            BUSTER_TEST(arguments, generic_schedule.execution_count == fixture_result.job_count);
        }
        else if (string_equal(fixture.path, S8("tests/compile_time_argument_error.bbb")))
        {
            BUSTER_TEST(arguments, fixture_result.instantiation_count == 0);
            BUSTER_TEST(arguments, fixture_result.first_diagnostic != 0);
            if (fixture_result.first_diagnostic)
            {
                BUSTER_TEST(arguments, fixture_result.first_diagnostic->kind == ANALYSIS_DIAGNOSTIC_COMPILE_TIME_ARGUMENT_REQUIRED);
                BUSTER_TEST(arguments, fixture_result.first_diagnostic->has_argument_index);
                BUSTER_TEST(arguments, fixture_result.first_diagnostic->argument_index == 0);
            }
        }
        for (u32 entity_index = 0; entity_index < fixture_result.module.entity_count; entity_index += 1)
        {
            AnalysisEntity* fixture_entity = fixture_result.module.entities + entity_index;
            if (fixture_entity->kind == ANALYSIS_ENTITY_CODE && fixture_entity->ast.code->has_body)
            {
                if (!analysis_entity_is_generic(fixture_temporary.arena, &fixture_result, fixture_entity))
                {
                    BUSTER_TEST(arguments, fixture_result.module.bodies[entity_index].analyzed);
                }
            }
        }
        file_map_unmap(source_file);
        arena_set_position(fixture_temporary.arena, fixture_temporary.position);
    }

    AnalysisProgram loaded = analysis_program_load(arguments->arena, fixture_expression_arena,
                                                   (AnalysisProgramOptions){
                                                       .root_path = S8("tests/basic_import.bbb"),
                                                       .root_module_name = S8("app"),
                                                       .module_root = S8("tests/modules"),
                                                       .pointer_size = 8,
                                                       .pointer_alignment = 8,
                                                   });
    BUSTER_TEST(arguments, !loaded.load_failed);
    BUSTER_TEST(arguments, loaded.module_count == 3);
    BUSTER_TEST(arguments, loaded.root != 0);
    BUSTER_TEST(arguments, loaded.parser_diagnostic_count == 0);
    BUSTER_TEST(arguments, loaded.analysis_diagnostic_count == 0);
    if (loaded.root)
    {
        BUSTER_STRING_TEST(arguments, loaded.root->name, S8("app"));
        BUSTER_TEST(arguments, loaded.root->analysis->module.id.value == 0);
        BUSTER_TEST(arguments, loaded.root->analysis->module.import_count == 2);
        String8 first_summary = analysis_serialize_module_interface(arguments->arena, loaded.root->analysis);
        String8 second_summary = analysis_serialize_module_interface(arguments->arena, loaded.root->analysis);
        BUSTER_STRING_TEST(arguments, first_summary, second_summary);
        AnalysisInterfaceSummary hashed = analysis_module_interface_summary(arguments->arena, loaded.root->analysis);
        AnalysisInterfaceCache cache = {0};
        BUSTER_TEST(arguments, analysis_interface_cache_store(arguments->arena, &cache, loaded.root->name, hashed));
        BUSTER_TEST(arguments, !analysis_interface_cache_store(arguments->arena, &cache, loaded.root->name, hashed));
        BUSTER_TEST(arguments, cache.count == 1);
        AnalysisProgramScheduleResult loaded_schedule = analysis_execute_program_jobs(arguments->arena, &loaded, 4, 0, 0);
        BUSTER_TEST(arguments, !loaded_schedule.has_cycle);
        BUSTER_TEST(arguments, loaded_schedule.execution_count != 0);
    }
    analysis_program_unmap_sources(&loaded);
    BUSTER_CHECK(arena_destroy(fixture_expression_arena, 1));

    arena_set_position(temporary.arena, temporary.position);
    return result;
}
#endif
