#include <buster/lib/ide_document.h>

#include <buster/lib/compiler/frontend/buster/analysis.h>
#include <buster/lib/hash.h>
#include <buster/lib/string.h>
#include <buster/lib/system_headers.h>
#if defined(__linux__) || defined(__APPLE__)
#include <stdio.h>
#endif

#if !BUSTER_INCLUDE_TESTS
enum
{
    IDE_DOCUMENT_ANALYSIS_WORK_PARSED = 1 << 0,
    IDE_DOCUMENT_ANALYSIS_WORK_INDEXED = 1 << 1,
    IDE_DOCUMENT_ANALYSIS_WORK_ANALYZED = 1 << 2,
    IDE_DOCUMENT_ANALYSIS_WORK_INVALIDATED = 1 << 3,
};
#endif

typedef enum IdePathKind
{
    IDE_PATH_MISSING,
    IDE_PATH_REGULAR,
    IDE_PATH_DIRECTORY,
    IDE_PATH_SYMLINK,
    IDE_PATH_OTHER,
} IdePathKind;

typedef enum IdePathComponentStatus
{
    IDE_PATH_COMPONENT_SAFE,
    IDE_PATH_COMPONENT_MISSING,
    IDE_PATH_COMPONENT_UNSAFE,
} IdePathComponentStatus;

typedef struct IdeLoadedFile IdeLoadedFile;
struct IdeLoadedFile
{
    String8 source;
    FileStats stats;
    u64 hash;
};

typedef struct IdeSyntaxRevision IdeSyntaxRevision;
typedef struct IdeModuleSnapshot IdeModuleSnapshot;

struct IdeDocumentStorageOwner
{
    Arena* arena;
    IdeDocumentStorageOwner** dependencies;
    IdeDocumentStorageOwner* staging_next;
    IdeDocumentStorageOwner* release_next;
    u32 dependency_count;
    u32 reference_count;
};

typedef struct IdeSyntaxBatch IdeSyntaxBatch;
struct IdeSyntaxBatch
{
    IdeDocumentStorageOwner owner;
};

struct IdeSyntaxRevision
{
    IdeDocumentStorageOwner* owner;
    String8 source;
    String8 interface_bytes;
    TokenizerResult tokenizer;
    ParserResult parser;
    u64 interface_hash;
    u32 tokenizer_error_count;
    bool has_syntax;
    bool analysis_eligible;
    bool has_generic_syntax;
    u8 reserved[1];
};

typedef struct IdeAnalysisBatch IdeAnalysisBatch;
struct IdeAnalysisBatch
{
    IdeDocumentStorageOwner owner;
    IdeModuleSnapshot* snapshots;
    u32 snapshot_count;
};

struct IdeModuleSnapshot
{
    IdeDocumentStorageOwner* owner;
    IdeSyntaxRevision* syntax;
    AnalysisResult* analysis;
    String8 interface_bytes;
    IdeDocumentImport* imports;
    IdeDocumentEntitySnapshot* entities;
    IdeDocumentDiagnostic* diagnostics;
    u64 interface_hash;
    u32 import_count;
    u32 entity_count;
    u32 diagnostic_count;
    bool has_generic_entities;
    u8 reserved[3];
};

struct IdeDocumentRevisionState
{
    IdeSyntaxRevision* current;
    IdeSyntaxRevision* saved;
    IdeModuleSnapshot* module;
};

struct IdeDocumentGraph
{
    u32* import_offsets;
    u32* binding_targets;
    AnalysisImportResolutionState* binding_states;
    u32* forward_offsets;
    u32* forward_edges;
    u32* reverse_offsets;
    u32* reverse_edges;
    u32* dependency_order;
    u32 edge_count;
    bool has_cycle;
    u8 reserved[3];
};

typedef struct IdeScanResult IdeScanResult;
struct IdeScanResult
{
    String8* paths;
    u32 path_count;
};

typedef struct IdeImportTraversalFrame IdeImportTraversalFrame;
struct IdeImportTraversalFrame
{
    u32 document_index;
    u32 next_import_index;
};

BUSTER_GLOBAL_LOCAL u64 ide_save_temp_serial;
#if BUSTER_INCLUDE_TESTS
BUSTER_GLOBAL_LOCAL bool ide_test_force_save_replace_failure;
BUSTER_GLOBAL_LOCAL bool ide_test_clobber_open_path_scratch;
#endif

BUSTER_GLOBAL_LOCAL String8 ide_string_copy(Arena* arena, String8 string);

BUSTER_GLOBAL_LOCAL void ide_owner_retain(IdeDocumentStorageOwner* owner)
{
    if (owner)
    {
        BUSTER_CHECK(owner->reference_count < UINT32_MAX);
        owner->reference_count += 1;
    }
}

BUSTER_GLOBAL_LOCAL void ide_owner_release(IdeDocumentStorageOwner* owner)
{
    if (!owner)
    {
        return;
    }
    BUSTER_CHECK(owner->reference_count);
    owner->reference_count -= 1;
    IdeDocumentStorageOwner* pending = 0;
    if (!owner->reference_count)
    {
        owner->release_next = pending;
        pending = owner;
    }
    while (pending)
    {
        IdeDocumentStorageOwner* current = pending;
        pending = current->release_next;
        for (u32 index = 0; index < current->dependency_count; index += 1)
        {
            IdeDocumentStorageOwner* dependency = current->dependencies[index];
            BUSTER_CHECK(dependency && dependency->reference_count);
            dependency->reference_count -= 1;
            if (!dependency->reference_count)
            {
                dependency->release_next = pending;
                pending = dependency;
            }
        }
        Arena* arena = current->arena;
        BUSTER_CHECK(arena);
        BUSTER_CHECK(arena_destroy(arena, 1));
    }
}

BUSTER_GLOBAL_LOCAL void ide_model_stage_owner(IdeDocumentModel* model, IdeDocumentStorageOwner* owner)
{
    BUSTER_CHECK(model && owner && owner->reference_count == 1);
    owner->staging_next = model->staged_owners;
    model->staged_owners = owner;
}

BUSTER_GLOBAL_LOCAL void ide_model_unstage_owner(IdeDocumentModel* model, IdeDocumentStorageOwner* owner)
{
    IdeDocumentStorageOwner** link = &model->staged_owners;
    while (*link && *link != owner)
    {
        link = &(*link)->staging_next;
    }
    BUSTER_CHECK(*link == owner);
    *link = owner->staging_next;
    owner->staging_next = 0;
    ide_owner_release(owner);
}

BUSTER_GLOBAL_LOCAL void ide_model_release_staged_owners(IdeDocumentModel* model)
{
    IdeDocumentStorageOwner* owner = model->staged_owners;
    model->staged_owners = 0;
    while (owner)
    {
        IdeDocumentStorageOwner* next = owner->staging_next;
        owner->staging_next = 0;
        ide_owner_release(owner);
        owner = next;
    }
}

BUSTER_GLOBAL_LOCAL void ide_owner_dependencies_set(IdeDocumentStorageOwner* owner, IdeDocumentStorageOwner** dependencies, u32 dependency_count)
{
    BUSTER_CHECK(owner && !owner->dependencies && !owner->dependency_count);
    if (!dependency_count)
    {
        return;
    }
    owner->dependencies = arena_allocate(owner->arena, IdeDocumentStorageOwner*, dependency_count);
    for (u32 index = 0; index < dependency_count; index += 1)
    {
        IdeDocumentStorageOwner* dependency = dependencies[index];
        if (!dependency || dependency == owner)
        {
            continue;
        }
        bool duplicate = false;
        for (u32 previous = 0; previous < owner->dependency_count; previous += 1)
        {
            duplicate |= owner->dependencies[previous] == dependency;
        }
        if (!duplicate)
        {
            owner->dependencies[owner->dependency_count] = dependency;
            owner->dependency_count += 1;
            ide_owner_retain(dependency);
        }
    }
}

BUSTER_GLOBAL_LOCAL Arena* ide_storage_arena_create(void)
{
    // Owner arenas cannot grow their reservation. Preserve enough address-space
    // capacity for large sources, but commit only one native page up front.
    u64 page_size = os_get_page_size();
    return arena_create((ArenaCreation){
        .reserved_size = BUSTER_MB(128),
        .granularity = page_size,
        .initial_size = page_size,
    });
}

BUSTER_GLOBAL_LOCAL IdeSyntaxBatch* ide_syntax_batch_create(IdeDocumentModel* model)
{
    Arena* arena = ide_storage_arena_create();
    if (!arena)
    {
        return 0;
    }
    IdeSyntaxBatch* batch = arena_allocate(arena, IdeSyntaxBatch, 1);
    *batch = (IdeSyntaxBatch){.owner = {.arena = arena, .reference_count = 1}};
    ide_model_stage_owner(model, &batch->owner);
    return batch;
}

BUSTER_GLOBAL_LOCAL IdeAnalysisBatch* ide_analysis_batch_create(IdeDocumentModel* model, u32 snapshot_count)
{
    Arena* arena = ide_storage_arena_create();
    if (!arena)
    {
        return 0;
    }
    IdeAnalysisBatch* batch = arena_allocate(arena, IdeAnalysisBatch, 1);
    *batch = (IdeAnalysisBatch){
        .owner = {.arena = arena, .reference_count = 1},
        .snapshot_count = snapshot_count,
    };
    if (snapshot_count)
    {
        batch->snapshots = arena_allocate(arena, IdeModuleSnapshot, snapshot_count);
        memset(batch->snapshots, 0, sizeof(*batch->snapshots) * snapshot_count);
    }
    ide_model_stage_owner(model, &batch->owner);
    return batch;
}

BUSTER_GLOBAL_LOCAL String8 ide_string_copy(Arena* arena, String8 string)
{
    if (string.length == UINT64_MAX)
    {
        return (String8){0};
    }

    char8* pointer = (char8*)arena_allocate_bytes(arena, string.length + 1, BUSTER_ALIGN_OF(char8));
    if (string.length)
    {
        memcpy(pointer, string.pointer, string.length);
    }
    pointer[string.length] = 0;
    return (String8){.pointer = pointer, .length = string.length};
}

BUSTER_GLOBAL_LOCAL bool ide_source_range_is_valid(String8 source, ParserSourceRange range)
{
    return range.offset <= source.length && range.length <= source.length - range.offset;
}

BUSTER_GLOBAL_LOCAL bool ide_fingerprint_size_add(u64* size, u64 length)
{
    u64 overhead = sizeof(u64) + 1;
    if (*size > UINT64_MAX - overhead || length > UINT64_MAX - overhead - *size)
    {
        return false;
    }
    *size += sizeof(u64) + 1 + length;
    return true;
}

BUSTER_GLOBAL_LOCAL void ide_fingerprint_part_write(char8** output, u8 tag, String8 value)
{
    *(*output)++ = (char8)tag;
    memcpy(*output, &value.length, sizeof(value.length));
    *output += sizeof(value.length);
    if (value.length)
    {
        memcpy(*output, value.pointer, value.length);
        *output += value.length;
    }
}

BUSTER_GLOBAL_LOCAL String8 ide_syntax_fingerprint(Arena* arena, String8 module_name, IdeSyntaxRevision* revision)
{
    BUSTER_CHECK(arena && revision);
    ParserResult* parser = &revision->parser;
    bool eligible = revision->analysis_eligible;
    u64 size = 0;
    bool valid = ide_fingerprint_size_add(&size, module_name.length);
    char8 eligibility_byte = eligible ? 1 : 0;
    valid &= ide_fingerprint_size_add(&size, 1);
    if (!eligible)
    {
        valid &= ide_fingerprint_size_add(&size, revision->source.length);
    }
    else
    {
        for (AstImport* import = parser->first_import; import; import = import->next)
        {
            valid &= ide_source_range_is_valid(revision->source, import->range);
            valid &= ide_fingerprint_size_add(&size, import->range.length);
        }
        for (AstTypeDeclaration* type = parser->first_type_declaration; type; type = type->next)
        {
            valid &= ide_source_range_is_valid(revision->source, type->range);
            valid &= ide_fingerprint_size_add(&size, type->range.length);
        }
        for (AstDataDeclaration* data = parser->first_data_declaration; data; data = data->next)
        {
            valid &= ide_source_range_is_valid(revision->source, data->range);
            valid &= ide_fingerprint_size_add(&size, data->range.length);
        }
        for (AstCode* code = parser->first_code; code; code = code->next)
        {
            u64 header_end = code->has_body ? code->body.range.offset : (u64)code->range.offset + code->range.length;
            valid &= ide_source_range_is_valid(revision->source, code->range);
            valid &= header_end >= code->range.offset && header_end <= (u64)code->range.offset + code->range.length;
            valid &= ide_fingerprint_size_add(&size, header_end >= code->range.offset ? header_end - code->range.offset : 0);
        }
    }
    if (!valid)
    {
        size = 0;
        BUSTER_CHECK(ide_fingerprint_size_add(&size, module_name.length));
        BUSTER_CHECK(ide_fingerprint_size_add(&size, 1));
        BUSTER_CHECK(ide_fingerprint_size_add(&size, revision->source.length));
        eligible = false;
        eligibility_byte = 0;
    }

    String8 result = {.pointer = arena_allocate(arena, char8, size), .length = size};
    char8* output = result.pointer;
    ide_fingerprint_part_write(&output, 'M', module_name);
    ide_fingerprint_part_write(&output, 'E', (String8){.pointer = &eligibility_byte, .length = 1});
    if (!eligible)
    {
        ide_fingerprint_part_write(&output, 'S', revision->source);
    }
    else
    {
        for (AstImport* import = parser->first_import; import; import = import->next)
        {
            ide_fingerprint_part_write(&output, 'I', string_slice(revision->source, import->range.offset, import->range.offset + import->range.length));
        }
        AstTypeDeclaration* type = parser->first_type_declaration;
        AstDataDeclaration* data = parser->first_data_declaration;
        AstCode* code = parser->first_code;
        while (type || data || code)
        {
            u32 type_offset = type ? type->range.offset : UINT32_MAX;
            u32 code_offset = code ? code->range.offset : UINT32_MAX;
            u32 data_offset = data ? data->range.offset : UINT32_MAX;
            if (type && type_offset <= code_offset && type_offset <= data_offset)
            {
                ide_fingerprint_part_write(&output, 'T', string_slice(revision->source, type->range.offset, type->range.offset + type->range.length));
                type = type->next;
            }
            else if (code && code_offset <= data_offset)
            {
                u64 header_end = code->has_body ? code->body.range.offset : (u64)code->range.offset + code->range.length;
                ide_fingerprint_part_write(&output, 'C', string_slice(revision->source, code->range.offset, header_end));
                code = code->next;
            }
            else
            {
                BUSTER_CHECK(data);
                ide_fingerprint_part_write(&output, 'D', string_slice(revision->source, data->range.offset, data->range.offset + data->range.length));
                data = data->next;
            }
        }
    }
    BUSTER_CHECK((u64)(output - result.pointer) == result.length);
    return result;
}

BUSTER_GLOBAL_LOCAL bool ide_ast_type_has_compile_time(AstType* root)
{
    if (!root)
    {
        return false;
    }
    typedef struct IdeAstTypeWork IdeAstTypeWork;
    struct IdeAstTypeWork
    {
        IdeAstTypeWork* previous;
        AstType* type;
    };
    TemporalArena scratch = scratch_begin(0, 0);
    IdeAstTypeWork* top = arena_allocate(scratch.arena, IdeAstTypeWork, 1);
    *top = (IdeAstTypeWork){.type = root};
    bool result = false;
    while (top && !result)
    {
        AstType* type = top->type;
        top = top->previous;
        if (type->is_compile_time)
        {
            result = true;
            break;
        }
        AstType* child = 0;
        if (type->id == AST_TYPE_POINTER || type->id == AST_TYPE_SLICE || type->id == AST_TYPE_INFERRED_ARRAY)
        {
            child = type->element_type;
        }
        else if (type->id == AST_TYPE_ARRAY)
        {
            child = type->array.element_type;
        }
        else if (type->id == AST_TYPE_VECTOR)
        {
            child = type->vector.element_type;
        }
        if (child)
        {
            IdeAstTypeWork* work = arena_allocate(scratch.arena, IdeAstTypeWork, 1);
            *work = (IdeAstTypeWork){.previous = top, .type = child};
            top = work;
        }
        if (type->id == AST_TYPE_FUNCTION)
        {
            if (type->function.return_type)
            {
                IdeAstTypeWork* work = arena_allocate(scratch.arena, IdeAstTypeWork, 1);
                *work = (IdeAstTypeWork){.previous = top, .type = type->function.return_type};
                top = work;
            }
            for (AstTypeArgument* argument = type->function.first_argument; argument; argument = argument->next)
            {
                if (argument->is_compile_time)
                {
                    result = true;
                    break;
                }
                if (argument->type)
                {
                    IdeAstTypeWork* work = arena_allocate(scratch.arena, IdeAstTypeWork, 1);
                    *work = (IdeAstTypeWork){.previous = top, .type = argument->type};
                    top = work;
                }
            }
        }
    }
    scratch_end(scratch);
    return result;
}

BUSTER_GLOBAL_LOCAL bool ide_parser_has_generic_syntax(ParserResult* parser)
{
    if (!parser)
    {
        return false;
    }
    for (AstCode* code = parser->first_code; code; code = code->next)
    {
        if (ide_ast_type_has_compile_time(code->type))
        {
            return true;
        }
    }
    return false;
}

BUSTER_GLOBAL_LOCAL IdeSyntaxRevision* ide_source_revision_create(IdeSyntaxBatch* batch, String8 source)
{
    if (!batch)
    {
        return 0;
    }
    IdeSyntaxRevision* revision = arena_allocate(batch->owner.arena, IdeSyntaxRevision, 1);
    *revision = (IdeSyntaxRevision){
        .owner = &batch->owner,
        .source = ide_string_copy(batch->owner.arena, source),
    };
    return revision;
}

BUSTER_GLOBAL_LOCAL IdeSyntaxRevision* ide_syntax_revision_create(IdeSyntaxBatch* batch, Arena* expression_arena, String8 module_name, String8 source)
{
    IdeSyntaxRevision* revision = ide_source_revision_create(batch, source);
    if (!revision)
    {
        return 0;
    }
    revision->has_syntax = true;
    revision->tokenizer = tokenize(batch->owner.arena, revision->source.pointer, revision->source.length);
    revision->parser = parser_parse(batch->owner.arena, expression_arena, revision->source, revision->tokenizer);
    revision->tokenizer_error_count = revision->tokenizer.error_count;
    revision->analysis_eligible = revision->tokenizer.error_count == 0 && revision->parser.diagnostic_count == 0;
    revision->has_generic_syntax = revision->analysis_eligible && ide_parser_has_generic_syntax(&revision->parser);
    revision->interface_bytes = ide_syntax_fingerprint(batch->owner.arena, module_name, revision);
    revision->interface_hash = buster_hash_64((u8*)revision->interface_bytes.pointer, revision->interface_bytes.length);
    return revision;
}

BUSTER_GLOBAL_LOCAL u8 ide_identity_byte(u8 byte)
{
#if defined(_WIN32)
    if (byte >= (u8)'A' && byte <= (u8)'Z')
    {
        byte = (u8)(byte + ((u8)'a' - (u8)'A'));
    }
#else
    BUSTER_UNUSED(byte);
#endif
    return byte;
}

BUSTER_GLOBAL_LOCAL s32 ide_string_compare(String8 left, String8 right, bool identity)
{
    u64 common_length = BUSTER_MIN(left.length, right.length);
    for (u64 index = 0; index < common_length; index += 1)
    {
        u8 left_byte = (u8)left.pointer[index];
        u8 right_byte = (u8)right.pointer[index];
        if (identity)
        {
            left_byte = ide_identity_byte(left_byte);
            right_byte = ide_identity_byte(right_byte);
        }
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

BUSTER_GLOBAL_LOCAL bool ide_identity_equal(String8 left, String8 right)
{
    return ide_string_compare(left, right, true) == 0;
}

bool ide_document_path_is_bbb(String8 path)
{
    if (path.length < 4 || !path.pointer)
    {
        return false;
    }
    String8 suffix = string_slice(path, path.length - 4, path.length);
    for (u64 index = 0; index < suffix.length; index += 1)
    {
        u8 byte = (u8)suffix.pointer[index];
        if (byte >= (u8)'A' && byte <= (u8)'Z')
        {
            byte = (u8)(byte + ((u8)'a' - (u8)'A'));
        }
        if (byte != (u8)S8(".bbb").pointer[index])
        {
            return false;
        }
    }
    return true;
}

IdeDocumentShortcutAction ide_document_shortcut_action(u8 key, u8 modifiers, u8 control_modifier, u8 disallowed_modifiers)
{
    if (control_modifier >= 8 || !(modifiers & (u8)(1u << control_modifier)) || (modifiers & disallowed_modifiers))
    {
        return IDE_DOCUMENT_SHORTCUT_NONE;
    }

    switch (key)
    {
        case 'o':
        case 'O':
            return IDE_DOCUMENT_SHORTCUT_OPEN;
        case 's':
        case 'S':
            return IDE_DOCUMENT_SHORTCUT_SAVE;
        case 'r':
        case 'R':
            return IDE_DOCUMENT_SHORTCUT_RELOAD;
    }
    return IDE_DOCUMENT_SHORTCUT_NONE;
}

BUSTER_GLOBAL_LOCAL u8 ide_query_fold_byte(u8 byte)
{
    if (byte >= (u8)'A' && byte <= (u8)'Z')
    {
        byte = (u8)(byte + ((u8)'a' - (u8)'A'));
    }
    return byte;
}

bool ide_document_string_contains(String8 value, String8 query, bool case_sensitive)
{
    if (!query.length)
    {
        return true;
    }
    if (!value.pointer || !query.pointer || query.length > value.length)
    {
        return false;
    }
    for (u64 start = 0; start <= value.length - query.length; start += 1)
    {
        bool matches = true;
        for (u64 index = 0; index < query.length; index += 1)
        {
            u8 left = (u8)value.pointer[start + index];
            u8 right = (u8)query.pointer[index];
            if (!case_sensitive)
            {
                left = ide_query_fold_byte(left);
                right = ide_query_fold_byte(right);
            }
            if (left != right)
            {
                matches = false;
                break;
            }
        }
        if (matches)
        {
            return true;
        }
    }
    return false;
}

IdeDocumentDropSelection ide_document_choose_drop_path(Arena* arena, SliceString8 paths)
{
    IdeDocumentDropSelection result = {.path_count = paths.length > UINT32_MAX ? UINT32_MAX : (u32)paths.length};
    u64 selected_index = UINT64_MAX;
    for (u64 index = 0; index < paths.length; index += 1)
    {
        if (selected_index == UINT64_MAX && ide_document_path_is_bbb(paths.pointer[index]))
        {
            selected_index = index;
        }
    }
    if (selected_index != UINT64_MAX)
    {
        result.accepted = true;
        result.ignored_count = paths.length > UINT32_MAX ? UINT32_MAX - 1 : (u32)(paths.length - 1);
        result.path = ide_string_copy(arena, paths.pointer[selected_index]);
    }
    else
    {
        result.ignored_count = paths.length > UINT32_MAX ? UINT32_MAX : (u32)paths.length;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool ide_path_is_absolute(String8 path)
{
    bool result = path.length && path.pointer[0] == '/';
#if defined(_WIN32)
    result |= path.length && path.pointer[0] == '\\';
    result |= path.length >= 2 && path.pointer[1] == ':';
#endif
    return result;
}

BUSTER_GLOBAL_LOCAL IdePathKind ide_path_kind(String8 path)
{
    if (!path.length || !path.pointer)
    {
        return IDE_PATH_MISSING;
    }

#if defined(_WIN32)
    TemporalArena scratch = scratch_begin(0, 0);
    String16 path_w = string16_from_string8(scratch.arena, path, true);
    DWORD attributes = GetFileAttributesW(path_w.pointer);
    scratch_end(scratch);
    if (attributes == INVALID_FILE_ATTRIBUTES)
    {
        return IDE_PATH_MISSING;
    }
    if (attributes & FILE_ATTRIBUTE_REPARSE_POINT)
    {
        return IDE_PATH_SYMLINK;
    }
    return (attributes & FILE_ATTRIBUTE_DIRECTORY) ? IDE_PATH_DIRECTORY : IDE_PATH_REGULAR;
#else
    struct stat stats = {0};
    if (lstat((const char*)path.pointer, &stats) != 0)
    {
        return IDE_PATH_MISSING;
    }
    if (S_ISLNK(stats.st_mode))
    {
        return IDE_PATH_SYMLINK;
    }
    if (S_ISDIR(stats.st_mode))
    {
        return IDE_PATH_DIRECTORY;
    }
    if (S_ISREG(stats.st_mode))
    {
        return IDE_PATH_REGULAR;
    }
    return IDE_PATH_OTHER;
#endif
}

BUSTER_GLOBAL_LOCAL bool ide_path_separator(char8 byte);
BUSTER_GLOBAL_LOCAL String8 ide_path_parent(Arena* arena, String8 path);

String8 ide_document_path_canonical(Arena* arena, String8 path)
{
    if (!arena || !path.length || !path.pointer)
    {
        return (String8){0};
    }

    String8 path_z = ide_string_copy(arena, path);
    String8 result = os_path_absolute(arena, path_z, true);
#if defined(__linux__) || defined(__APPLE__)
    if (!result.length)
    {
        String8 parent = ide_path_parent(arena, path_z);
        u64 basename_start = 0;
        if (parent.length)
        {
            basename_start = parent.length;
            while (basename_start < path_z.length && ide_path_separator(path_z.pointer[basename_start]))
            {
                basename_start += 1;
            }
        }
        else
        {
            parent = ide_string_copy(arena, S8("."));
        }

        if (basename_start < path_z.length)
        {
            String8 basename = string_slice(path_z, basename_start, path_z.length);
            if (!string_equal(basename, S8(".")) && !string_equal(basename, S8("..")))
            {
                String8 canonical_parent = os_path_absolute(arena, parent, true);
                if (canonical_parent.length)
                {
                    bool parent_separator = ide_path_separator(canonical_parent.pointer[canonical_parent.length - 1]);
                    result = string_format_z(arena, parent_separator ? S8("{S8}{S8}") : S8("{S8}/{S8}"), canonical_parent, basename);
                }
            }
        }
    }
#elif defined(_WIN32)
    for (u64 index = 0; index < result.length; index += 1)
    {
        if (result.pointer[index] == '\\')
        {
            result.pointer[index] = '/';
        }
    }
#endif
    return result;
}

String8 ide_document_path_identity(Arena* arena, String8 canonical_path)
{
    String8 result = ide_string_copy(arena, canonical_path);
    for (u64 index = 0; index < result.length; index += 1)
    {
        result.pointer[index] = (char8)ide_identity_byte((u8)result.pointer[index]);
#if defined(_WIN32)
        if (result.pointer[index] == '\\')
        {
            result.pointer[index] = '/';
        }
#endif
    }
    return result;
}

bool ide_document_path_is_within(String8 root, String8 path)
{
    if (!root.length || !path.length || path.length < root.length)
    {
        return false;
    }

    for (u64 index = 0; index < root.length; index += 1)
    {
        u8 root_byte = ide_identity_byte((u8)root.pointer[index]);
        u8 path_byte = ide_identity_byte((u8)path.pointer[index]);
#if defined(_WIN32)
        if (root_byte == '\\')
        {
            root_byte = '/';
        }
        if (path_byte == '\\')
        {
            path_byte = '/';
        }
#endif
        if (root_byte != path_byte)
        {
            return false;
        }
    }

    return path.length == root.length || ide_path_separator(root.pointer[root.length - 1]) ||
           (path.length > root.length && ide_path_separator(path.pointer[root.length]));
}

BUSTER_GLOBAL_LOCAL bool ide_path_separator(char8 byte)
{
#if defined(_WIN32)
    return byte == '/' || byte == '\\';
#else
    return byte == '/';
#endif
}

BUSTER_GLOBAL_LOCAL IdePathComponentStatus ide_path_component_inspect(Arena* arena, String8 path, IdePathKind* kind_out)
{
    if (kind_out)
    {
        *kind_out = IDE_PATH_OTHER;
    }
    if (!arena || !path.length || !path.pointer)
    {
        return IDE_PATH_COMPONENT_UNSAFE;
    }

#if defined(_WIN32)
    String16 path_w = string16_from_string8(arena, path, true);
    if (!path_w.pointer)
    {
        return IDE_PATH_COMPONENT_UNSAFE;
    }
    SetLastError(ERROR_SUCCESS);
    DWORD attributes = GetFileAttributesW(path_w.pointer);
    if (attributes == INVALID_FILE_ATTRIBUTES)
    {
        DWORD error = GetLastError();
        if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND)
        {
            if (kind_out)
            {
                *kind_out = IDE_PATH_MISSING;
            }
            return IDE_PATH_COMPONENT_MISSING;
        }
        return IDE_PATH_COMPONENT_UNSAFE;
    }
    if (attributes & FILE_ATTRIBUTE_REPARSE_POINT)
    {
        if (kind_out)
        {
            *kind_out = IDE_PATH_SYMLINK;
        }
        return IDE_PATH_COMPONENT_UNSAFE;
    }
    if (kind_out)
    {
        *kind_out = (attributes & FILE_ATTRIBUTE_DIRECTORY) ? IDE_PATH_DIRECTORY : IDE_PATH_REGULAR;
    }
    return IDE_PATH_COMPONENT_SAFE;
#else
    struct stat stats = {0};
    if (lstat((const char*)path.pointer, &stats) != 0)
    {
        if (errno == ENOENT || errno == ENOTDIR)
        {
            if (kind_out)
            {
                *kind_out = IDE_PATH_MISSING;
            }
            return IDE_PATH_COMPONENT_MISSING;
        }
        return IDE_PATH_COMPONENT_UNSAFE;
    }
    if (S_ISLNK(stats.st_mode))
    {
        if (kind_out)
        {
            *kind_out = IDE_PATH_SYMLINK;
        }
        return IDE_PATH_COMPONENT_UNSAFE;
    }
    if (kind_out)
    {
        if (S_ISDIR(stats.st_mode))
        {
            *kind_out = IDE_PATH_DIRECTORY;
        }
        else if (S_ISREG(stats.st_mode))
        {
            *kind_out = IDE_PATH_REGULAR;
        }
        else
        {
            *kind_out = IDE_PATH_OTHER;
        }
    }
    return IDE_PATH_COMPONENT_SAFE;
#endif
}

BUSTER_GLOBAL_LOCAL IdePathComponentStatus ide_path_component_status(Arena* arena, String8 root, String8 path, bool* missing_final_out)
{
    if (missing_final_out)
    {
        *missing_final_out = false;
    }
    if (!arena || !ide_document_path_is_within(root, path))
    {
        return IDE_PATH_COMPONENT_UNSAFE;
    }

    Arena* conflicts[] = {arena};
    TemporalArena scratch = scratch_begin(conflicts, BUSTER_ARRAY_LENGTH(conflicts));
    char8* prefix = (char8*)arena_allocate_bytes(scratch.arena, path.length + 1, BUSTER_ALIGN_OF(char8));
    if (!prefix)
    {
        scratch_end(scratch);
        return IDE_PATH_COMPONENT_UNSAFE;
    }
    memcpy(prefix, path.pointer, path.length);
    prefix[path.length] = 0;
    u64 cursor = root.length;
    while (cursor < path.length)
    {
        while (cursor < path.length && ide_path_separator(path.pointer[cursor]))
        {
            cursor += 1;
        }
        u64 component_start = cursor;
        while (cursor < path.length && !ide_path_separator(path.pointer[cursor]))
        {
            cursor += 1;
        }
        if (component_start == cursor)
        {
            continue;
        }
        String8 component = string_slice(path, component_start, cursor);
        if (string_equal(component, S8(".")))
        {
            continue;
        }
        if (string_equal(component, S8("..")))
        {
            scratch_end(scratch);
            return IDE_PATH_COMPONENT_UNSAFE;
        }
        char8 separator = prefix[cursor];
        prefix[cursor] = 0;
        IdePathKind kind = IDE_PATH_OTHER;
        IdePathComponentStatus status = ide_path_component_inspect(scratch.arena, (String8){.pointer = prefix, .length = cursor}, &kind);
        prefix[cursor] = separator;
        if (status != IDE_PATH_COMPONENT_SAFE)
        {
            if (status == IDE_PATH_COMPONENT_MISSING && missing_final_out)
            {
                *missing_final_out = cursor == path.length;
            }
            scratch_end(scratch);
            return status;
        }
        if (cursor < path.length && kind != IDE_PATH_DIRECTORY)
        {
            scratch_end(scratch);
            return IDE_PATH_COMPONENT_UNSAFE;
        }
    }
    scratch_end(scratch);
    return IDE_PATH_COMPONENT_SAFE;
}

BUSTER_GLOBAL_LOCAL bool ide_path_has_parent_component(String8 path)
{
    u64 cursor = 0;
    while (cursor < path.length)
    {
        while (cursor < path.length && ide_path_separator(path.pointer[cursor]))
        {
            cursor += 1;
        }
        u64 component_start = cursor;
        while (cursor < path.length && !ide_path_separator(path.pointer[cursor]))
        {
            cursor += 1;
        }
        if (string_equal(string_slice(path, component_start, cursor), S8("..")))
        {
            return true;
        }
    }
    return false;
}

BUSTER_GLOBAL_LOCAL String8 ide_path_parent(Arena* arena, String8 path)
{
    if (!path.length)
    {
        return (String8){0};
    }

    u64 slash = BUSTER_STRING_NO_MATCH;
    for (u64 index = path.length; index; index -= 1)
    {
        char8 byte = path.pointer[index - 1];
        if (ide_path_separator(byte))
        {
            slash = index - 1;
            break;
        }
    }
    if (slash == BUSTER_STRING_NO_MATCH)
    {
        return (String8){0};
    }
    if (slash == 0)
    {
        return ide_string_copy(arena, string_slice(path, 0, 1));
    }
#if defined(_WIN32)
    if (slash == 2 && path.length >= 3 && path.pointer[1] == ':')
    {
        return ide_string_copy(arena, string_slice(path, 0, 3));
    }
#endif
    return ide_string_copy(arena, string_slice(path, 0, slash));
}

BUSTER_GLOBAL_LOCAL String8 ide_path_join(Arena* arena, String8 root, String8 part, bool add_bbb)
{
    bool root_separator = root.length && ide_path_separator(root.pointer[root.length - 1]);
    bool part_has_extension = ide_document_path_is_bbb(part);
    String8 result = string_format_z(arena, root_separator ? S8("{S8}{S8}{S8}") : S8("{S8}/{S8}{S8}"), root, part,
                                     add_bbb && !part_has_extension ? S8(".bbb") : S8(""));
    return result;
}

BUSTER_GLOBAL_LOCAL String8 ide_path_resolve_for_root(Arena* arena, String8 root, String8 path)
{
    return ide_path_is_absolute(path) ? ide_string_copy(arena, path) : ide_path_join(arena, root, path, false);
}

BUSTER_GLOBAL_LOCAL bool ide_file_read(Arena* arena, String8 root, String8 path, IdeLoadedFile* result, IdeDocumentErrorKind* error_out)
{
    *result = (IdeLoadedFile){0};
    if (!ide_document_path_is_within(root, path))
    {
        *error_out = IDE_DOCUMENT_ERROR_PATH_OUTSIDE_ROOT;
        return false;
    }
    IdePathComponentStatus component_status = ide_path_component_status(arena, root, path, 0);
    if (component_status == IDE_PATH_COMPONENT_UNSAFE)
    {
        *error_out = IDE_DOCUMENT_ERROR_PATH_OUTSIDE_ROOT;
        return false;
    }
    if (component_status == IDE_PATH_COMPONENT_MISSING)
    {
        *error_out = IDE_DOCUMENT_ERROR_PATH_NOT_FOUND;
        return false;
    }
    IdePathKind kind = ide_path_kind(path);
    if (kind == IDE_PATH_MISSING)
    {
        *error_out = IDE_DOCUMENT_ERROR_PATH_NOT_FOUND;
        return false;
    }
    if (kind != IDE_PATH_REGULAR)
    {
        *error_out = IDE_DOCUMENT_ERROR_NOT_REGULAR_FILE;
        return false;
    }

    OsFileDescriptor* file = os_file_open(path, (OpenFlags){.read = 1}, (OpenPermissions){.read = 1});
    if (!file)
    {
        *error_out = IDE_DOCUMENT_ERROR_FILE_READ;
        return false;
    }

    u64 size = os_file_get_size(file);
    u8* bytes = (u8*)arena_allocate_bytes(arena, size + 1, BUSTER_ALIGN_OF(u8));
    u64 read_size = os_file_read(file, (ByteSlice){.pointer = bytes, .length = size}, size);
    FileStats stats = os_file_get_stats(file, (FileStatsOptions){.size = 1, .modified_time = 1});
    bool close_result = os_file_close(file);
    if (read_size != size || !close_result)
    {
        *error_out = IDE_DOCUMENT_ERROR_FILE_READ;
        return false;
    }

    bytes[size] = 0;
    result->source = (String8){.pointer = (char8*)bytes, .length = size};
    result->stats = stats;
    result->hash = buster_hash_64(bytes, size);
    *error_out = IDE_DOCUMENT_ERROR_NONE;
    return true;
}

BUSTER_GLOBAL_LOCAL bool ide_loaded_file_matches(String8 source, u64 hash, String8 expected_source, u64 expected_hash)
{
    return hash == expected_hash && string_equal(source, expected_source);
}

BUSTER_GLOBAL_LOCAL void ide_document_clamp_view(IdeDocument* document)
{
    u64 source_length = document->source.length;
    document->view.cursor_offset = BUSTER_MIN(document->view.cursor_offset, source_length);
    document->view.selection_start = BUSTER_MIN(document->view.selection_start, source_length);
    document->view.selection_end = BUSTER_MIN(document->view.selection_end, source_length);
    if (document->view.selection_start > document->view.selection_end)
    {
        document->view.selection_start = document->view.selection_end;
    }
}

BUSTER_GLOBAL_LOCAL String8 ide_save_temp_path(Arena* arena, String8 path, u64 serial)
{
    String8 parent = ide_path_parent(arena, path);
    if (!parent.length)
    {
        return (String8){0};
    }
    String8 name = string_format_z(arena, S8(".buster-save-{u64}-{u64}.tmp"), os_get_current_process_id(), serial);
    return ide_path_join(arena, parent, name, false);
}

BUSTER_GLOBAL_LOCAL bool ide_atomic_write_file(Arena* arena, String8 path, String8 source)
{
#if defined(__linux__) || defined(__APPLE__)
    struct stat destination_stats = {0};
    if (lstat((const char*)path.pointer, &destination_stats) != 0 || !S_ISREG(destination_stats.st_mode))
    {
        return false;
    }
    mode_t destination_mode = destination_stats.st_mode & 07777;
    for (u32 attempt = 0; attempt < 64; attempt += 1)
    {
        String8 temporary_path = ide_save_temp_path(arena, path, ide_save_temp_serial++);
        if (!temporary_path.length)
        {
            return false;
        }
        int file_descriptor = open((const char*)temporary_path.pointer, O_WRONLY | O_CREAT | O_EXCL, 0600);
        if (file_descriptor < 0)
        {
            if (errno == EEXIST)
            {
                continue;
            }
            return false;
        }

        bool write_success = true;
        u64 offset = 0;
        while (write_success && offset < source.length)
        {
            u64 chunk_length = BUSTER_MIN(source.length - offset, (u64)0x40000000);
            ssize_t write_count;
            do
            {
                write_count = write(file_descriptor, source.pointer + offset, (size_t)chunk_length);
            } while (write_count < 0 && errno == EINTR);
            if (write_count <= 0)
            {
                write_success = false;
            }
            else
            {
                offset += (u64)write_count;
            }
        }

        if (write_success)
        {
            write_success = fchmod(file_descriptor, destination_mode) == 0;
        }
        int sync_result = 0;
        if (write_success)
        {
            do
            {
                sync_result = fsync(file_descriptor);
            } while (sync_result < 0 && errno == EINTR);
            write_success = sync_result == 0;
        }
        bool close_success = close(file_descriptor) == 0;
        if (!write_success || !close_success)
        {
            os_file_delete(temporary_path);
            return false;
        }
#if BUSTER_INCLUDE_TESTS
        if (ide_test_force_save_replace_failure)
        {
            os_file_delete(temporary_path);
            return false;
        }
#endif
        int rename_result;
        do
        {
            rename_result = rename((const char*)temporary_path.pointer, (const char*)path.pointer);
        } while (rename_result != 0 && errno == EINTR);
        if (rename_result != 0)
        {
            os_file_delete(temporary_path);
            return false;
        }
        return true;
    }
#elif defined(_WIN32)
    String16 destination_w = string16_from_string8(arena, path, true);
    for (u32 attempt = 0; attempt < 64; attempt += 1)
    {
        String8 temporary_path = ide_save_temp_path(arena, path, ide_save_temp_serial++);
        if (!temporary_path.length)
        {
            return false;
        }
        String16 temporary_w = string16_from_string8(arena, temporary_path, true);
        HANDLE file = CreateFileW(temporary_w.pointer, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, 0, CREATE_NEW,
                                  FILE_ATTRIBUTE_NORMAL, 0);
        if (file == INVALID_HANDLE_VALUE)
        {
            DWORD error = GetLastError();
            if (error == ERROR_FILE_EXISTS || error == ERROR_ALREADY_EXISTS)
            {
                continue;
            }
            return false;
        }

        bool write_success = true;
        u64 offset = 0;
        while (write_success && offset < source.length)
        {
            DWORD chunk_length = (DWORD)BUSTER_MIN(source.length - offset, (u64)0x40000000);
            DWORD write_count = 0;
            write_success = WriteFile(file, source.pointer + offset, chunk_length, &write_count, 0) != 0 && write_count == chunk_length;
            offset += write_count;
        }
        if (write_success)
        {
            write_success = FlushFileBuffers(file) != 0;
        }
        bool close_success = CloseHandle(file) != 0;
        if (!write_success || !close_success)
        {
            os_file_delete(temporary_path);
            return false;
        }
#if BUSTER_INCLUDE_TESTS
        if (ide_test_force_save_replace_failure)
        {
            os_file_delete(temporary_path);
            return false;
        }
#endif
        if (!ReplaceFileW(destination_w.pointer, temporary_w.pointer, 0, REPLACEFILE_WRITE_THROUGH, 0, 0))
        {
            os_file_delete(temporary_path);
            return false;
        }
        return true;
    }
#else
    BUSTER_UNUSED(arena);
    BUSTER_UNUSED(path);
    BUSTER_UNUSED(source);
#endif
    return false;
}

BUSTER_GLOBAL_LOCAL bool ide_scan_append(Arena* arena, String8 root, String8* directories, u32* directory_count, String8* paths, u32* path_count,
                                         u32 capacity, u32* entry_count, String8 entry)
{
    if (*entry_count >= capacity)
    {
        return false;
    }
    *entry_count += 1;

    if (ide_path_component_status(arena, root, entry, 0) != IDE_PATH_COMPONENT_SAFE)
    {
        return true;
    }
    IdePathKind kind = ide_path_kind(entry);
    if (kind == IDE_PATH_SYMLINK || kind == IDE_PATH_OTHER || kind == IDE_PATH_MISSING)
    {
        return true;
    }
    if (kind == IDE_PATH_DIRECTORY)
    {
        if (*directory_count >= capacity)
        {
            return false;
        }
        directories[*directory_count] = ide_string_copy(arena, entry);
        *directory_count += 1;
    }
    else if (kind == IDE_PATH_REGULAR && ide_document_path_is_bbb(entry))
    {
        if (*path_count >= capacity)
        {
            return false;
        }
        String8 canonical = ide_document_path_canonical(arena, entry);
        if (!canonical.length || !ide_document_path_is_within(root, canonical) ||
            ide_path_component_status(arena, root, canonical, 0) != IDE_PATH_COMPONENT_SAFE)
        {
            return false;
        }
        paths[*path_count] = canonical;
        *path_count += 1;
    }
    return true;
}

BUSTER_GLOBAL_LOCAL IdeDocumentErrorKind ide_scan_workspace(Arena* arena, String8 root, u32 capacity, IdeScanResult* result)
{
    if (!capacity || !root.length || !root.pointer)
    {
        result->paths = 0;
        result->path_count = 0;
        return IDE_DOCUMENT_ERROR_TRAVERSAL_LIMIT;
    }
    result->paths = arena_allocate(arena, String8, capacity);
    result->path_count = 0;
    String8* directories = arena_allocate(arena, String8, capacity);
    if (!result->paths || !directories)
    {
        return IDE_DOCUMENT_ERROR_FILE_READ;
    }
    u32 directory_count = 1;
    u32 directory_index = 0;
    u32 entry_count = 0;
    directories[0] = ide_string_copy(arena, root);
    if (!directories[0].length || !directories[0].pointer)
    {
        return IDE_DOCUMENT_ERROR_FILE_READ;
    }

    while (directory_index < directory_count)
    {
        String8 directory_path = directories[directory_index];
        directory_index += 1;
#if defined(_WIN32)
        String8 pattern = string_format_z(arena, S8("{S8}\\*"), directory_path);
        String16 pattern_w = string16_from_string8(arena, pattern, true);
        WIN32_FIND_DATAW find_data = {0};
        HANDLE find = FindFirstFileW(pattern_w.pointer, &find_data);
        if (find == INVALID_HANDLE_VALUE)
        {
            DWORD error = GetLastError();
            if (error != ERROR_FILE_NOT_FOUND)
            {
                return IDE_DOCUMENT_ERROR_FILE_READ;
            }
            continue;
        }
        bool more = true;
        while (more)
        {
            u64 name_length = 0;
            while (find_data.cFileName[name_length])
            {
                name_length += 1;
            }
            String8 name = string8_from_string16(arena, (String16){.pointer = (char16*)find_data.cFileName, .length = name_length}, false);
            if (!string_equal(name, S8(".")) && !string_equal(name, S8("..")))
            {
                String8 entry = string_format_z(arena, S8("{S8}\\{S8}"), directory_path, name);
                if (!ide_scan_append(arena, root, directories, &directory_count, result->paths, &result->path_count, capacity, &entry_count, entry))
                {
                    FindClose(find);
                    return IDE_DOCUMENT_ERROR_TRAVERSAL_LIMIT;
                }
            }
            more = FindNextFileW(find, &find_data) != 0;
        }
        DWORD enumeration_error = GetLastError();
        FindClose(find);
        if (enumeration_error != ERROR_NO_MORE_FILES)
        {
            return IDE_DOCUMENT_ERROR_FILE_READ;
        }
#else
        if (!directory_path.length || !directory_path.pointer)
        {
            return IDE_DOCUMENT_ERROR_FILE_READ;
        }
        DIR* directory = opendir((const char*)directory_path.pointer);
        if (!directory)
        {
            return IDE_DOCUMENT_ERROR_FILE_READ;
        }
        for (;;)
        {
            errno = 0;
            struct dirent* entry = readdir(directory);
            if (!entry)
            {
                if (errno != 0)
                {
                    closedir(directory);
                    return IDE_DOCUMENT_ERROR_FILE_READ;
                }
                break;
            }
            String8 name = string_from_pointer((const char8*)entry->d_name);
            if (string_equal(name, S8(".")) || string_equal(name, S8("..")))
            {
                continue;
            }
            String8 entry_path = string_format_z(arena, S8("{S8}/{S8}"), directory_path, name);
            if (!ide_scan_append(arena, root, directories, &directory_count, result->paths, &result->path_count, capacity, &entry_count, entry_path))
            {
                closedir(directory);
                return IDE_DOCUMENT_ERROR_TRAVERSAL_LIMIT;
            }
        }
        closedir(directory);
#endif
    }

    for (u32 index = 1; index < result->path_count; index += 1)
    {
        String8 value = result->paths[index];
        u32 insertion = index;
        while (insertion && ide_string_compare(result->paths[insertion - 1], value, true) > 0)
        {
            result->paths[insertion] = result->paths[insertion - 1];
            insertion -= 1;
        }
        result->paths[insertion] = value;
    }

    u32 unique_count = 0;
    for (u32 index = 0; index < result->path_count; index += 1)
    {
        if (!unique_count || !ide_identity_equal(result->paths[unique_count - 1], result->paths[index]))
        {
            result->paths[unique_count] = result->paths[index];
            unique_count += 1;
        }
    }
    result->path_count = unique_count;
    return IDE_DOCUMENT_ERROR_NONE;
}

BUSTER_GLOBAL_LOCAL u32 ide_workspace_find_identity(IdeDocumentWorkspace* workspace, String8 identity)
{
    for (u32 index = 0; index < workspace->document_count; index += 1)
    {
        if (ide_identity_equal(workspace->documents[index].identity, identity))
        {
            return index;
        }
    }
    return IDE_DOCUMENT_INDEX_INVALID;
}

BUSTER_GLOBAL_LOCAL u32 ide_workspace_find_exact_path(IdeDocumentWorkspace* workspace, String8 path)
{
    for (u32 index = 0; index < workspace->document_count; index += 1)
    {
        if (string_equal(workspace->documents[index].path, path) || string_equal(workspace->documents[index].identity, path))
        {
            return index;
        }
    }
    return IDE_DOCUMENT_INDEX_INVALID;
}

BUSTER_GLOBAL_LOCAL u32 ide_workspace_find_path(Arena* arena, IdeDocumentWorkspace* workspace, String8 path)
{
    String8 candidate = ide_path_resolve_for_root(arena, workspace->root_path, path);
    String8 canonical = ide_document_path_canonical(arena, candidate);
    if (canonical.length)
    {
        String8 identity = ide_document_path_identity(arena, canonical);
        u32 index = ide_workspace_find_identity(workspace, identity);
        if (index != IDE_DOCUMENT_INDEX_INVALID)
        {
            return index;
        }
    }
    return ide_workspace_find_exact_path(workspace, canonical.length ? canonical : candidate);
}

BUSTER_GLOBAL_LOCAL IdeDocumentErrorKind ide_model_path_error(Arena* arena, String8 root, String8 path)
{
    String8 candidate = ide_path_resolve_for_root(arena, root, path);
    String8 canonical = ide_document_path_canonical(arena, candidate);
    if (canonical.length)
    {
        if (!ide_document_path_is_within(root, canonical) ||
            ide_path_component_status(arena, root, canonical, 0) == IDE_PATH_COMPONENT_UNSAFE)
        {
            return IDE_DOCUMENT_ERROR_PATH_OUTSIDE_ROOT;
        }
    }
    else if (!ide_document_path_is_within(root, candidate))
    {
        return IDE_DOCUMENT_ERROR_PATH_OUTSIDE_ROOT;
    }

    if (ide_document_path_is_within(root, candidate) &&
        ide_path_component_status(arena, root, candidate, 0) == IDE_PATH_COMPONENT_UNSAFE)
    {
        return IDE_DOCUMENT_ERROR_PATH_OUTSIDE_ROOT;
    }
    return IDE_DOCUMENT_ERROR_NONE;
}

BUSTER_GLOBAL_LOCAL String8 ide_module_name_from_path(Arena* arena, String8 root, String8 path)
{
    u64 start = root.length;
    if (start < path.length && (path.pointer[start] == '/' || path.pointer[start] == '\\'))
    {
        start += 1;
    }
    String8 relative = string_slice(path, start, path.length);
    if (ide_document_path_is_bbb(relative))
    {
        relative = string_slice(relative, 0, relative.length - 4);
    }
    return ide_string_copy(arena, relative);
}

typedef enum IdeWorkspaceCopyMode
{
    IDE_WORKSPACE_COPY_PRESERVE_DERIVED,
    IDE_WORKSPACE_COPY_REBUILD_DERIVED,
} IdeWorkspaceCopyMode;

BUSTER_GLOBAL_LOCAL bool ide_document_copy_to_arena(Arena* arena, IdeDocument* destination, const IdeDocument* source,
                                                   IdeWorkspaceCopyMode mode, bool copy_sources)
{
    *destination = *source;
    destination->path = ide_string_copy(arena, source->path);
    destination->identity = ide_string_copy(arena, source->identity);
    destination->module_name = ide_string_copy(arena, source->module_name);
    if (copy_sources)
    {
        destination->source = ide_string_copy(arena, source->source);
        destination->saved_source = ide_string_copy(arena, source->saved_source);
    }
    destination->search.query = ide_string_copy(arena, source->search.query);
    destination->search.replacement = ide_string_copy(arena, source->search.replacement);
    destination->compile.artifact_path = ide_string_copy(arena, source->compile.artifact_path);
    destination->compile.command_line = ide_string_copy(arena, source->compile.command_line);
    destination->compile.message = ide_string_copy(arena, source->compile.message);
    if (mode == IDE_WORKSPACE_COPY_REBUILD_DERIVED)
    {
        destination->diagnostics = 0;
        destination->diagnostic_count = 0;
    }
    else if (source->diagnostic_count)
    {
        destination->diagnostics = arena_allocate(arena, IdeDocumentDiagnostic, source->diagnostic_count);
        for (u32 index = 0; index < source->diagnostic_count; index += 1)
        {
            destination->diagnostics[index] = source->diagnostics[index];
            destination->diagnostics[index].file_path = ide_string_copy(arena, source->diagnostics[index].file_path);
            destination->diagnostics[index].message = ide_string_copy(arena, source->diagnostics[index].message);
        }
    }
    return true;
}

BUSTER_GLOBAL_LOCAL IdeDocumentGraph* ide_graph_copy(Arena* arena, const IdeDocumentWorkspace* source)
{
    if (!source->graph)
    {
        return 0;
    }
    IdeDocumentGraph* graph = arena_allocate(arena, IdeDocumentGraph, 1);
    *graph = *source->graph;
    u32 document_count = source->document_count;
    u64 offset_count = (u64)document_count + 1;
    u32 import_count = source->import_count;
    if (document_count)
    {
        graph->import_offsets = arena_allocate(arena, u32, offset_count);
        graph->forward_offsets = arena_allocate(arena, u32, offset_count);
        graph->reverse_offsets = arena_allocate(arena, u32, offset_count);
        graph->dependency_order = arena_allocate(arena, u32, document_count);
        memcpy(graph->import_offsets, source->graph->import_offsets, sizeof(u32) * offset_count);
        memcpy(graph->forward_offsets, source->graph->forward_offsets, sizeof(u32) * offset_count);
        memcpy(graph->reverse_offsets, source->graph->reverse_offsets, sizeof(u32) * offset_count);
        memcpy(graph->dependency_order, source->graph->dependency_order, sizeof(u32) * document_count);
    }
    if (import_count)
    {
        graph->binding_targets = arena_allocate(arena, u32, import_count);
        graph->binding_states = arena_allocate(arena, AnalysisImportResolutionState, import_count);
        memcpy(graph->binding_targets, source->graph->binding_targets, sizeof(u32) * import_count);
        memcpy(graph->binding_states, source->graph->binding_states, sizeof(AnalysisImportResolutionState) * import_count);
    }
    if (graph->edge_count)
    {
        graph->forward_edges = arena_allocate(arena, u32, graph->edge_count);
        graph->reverse_edges = arena_allocate(arena, u32, graph->edge_count);
        memcpy(graph->forward_edges, source->graph->forward_edges, sizeof(u32) * graph->edge_count);
        memcpy(graph->reverse_edges, source->graph->reverse_edges, sizeof(u32) * graph->edge_count);
    }
    return graph;
}

BUSTER_GLOBAL_LOCAL bool ide_workspace_copy_to_arena_mode(Arena* arena, IdeDocumentWorkspace* destination,
                                                          const IdeDocumentWorkspace* source, IdeWorkspaceCopyMode mode)
{
    *destination = *source;
    destination->root_path = ide_string_copy(arena, source->root_path);
    destination->filter.query = ide_string_copy(arena, source->filter.query);
    if (source->document_count)
    {
        destination->documents = arena_allocate(arena, IdeDocument, source->document_count);
        destination->revision_states = arena_allocate(arena, IdeDocumentRevisionState, source->document_count);
        if (source->revision_states)
        {
            memcpy(destination->revision_states, source->revision_states, sizeof(*destination->revision_states) * source->document_count);
        }
        else
        {
            memset(destination->revision_states, 0, sizeof(*destination->revision_states) * source->document_count);
        }
        for (u32 index = 0; index < source->document_count; index += 1)
        {
            bool copy_sources = !source->revision_states || !source->revision_states[index].current;
            ide_document_copy_to_arena(arena, destination->documents + index, source->documents + index, mode, copy_sources);
            if (!copy_sources)
            {
                destination->documents[index].source = destination->revision_states[index].current->source;
                destination->documents[index].saved_source = destination->revision_states[index].saved
                                                                 ? destination->revision_states[index].saved->source
                                                                 : destination->revision_states[index].current->source;
            }
        }
    }
    destination->graph = ide_graph_copy(arena, source);
#if BUSTER_INCLUDE_TESTS
    if (source->document_count && source->last_analysis_work_flags)
    {
        destination->last_analysis_work_flags = arena_allocate(arena, u8, source->document_count);
        memcpy(destination->last_analysis_work_flags, source->last_analysis_work_flags, source->document_count);
    }
#endif
    if (mode == IDE_WORKSPACE_COPY_REBUILD_DERIVED)
    {
        destination->imports = 0;
        destination->import_count = 0;
        destination->entities = 0;
        destination->entity_count = 0;
    }
    else if (source->import_count)
    {
        destination->imports = arena_allocate(arena, IdeDocumentImport, source->import_count);
        for (u32 index = 0; index < source->import_count; index += 1)
        {
            IdeDocumentImport* destination_import = destination->imports + index;
            IdeDocumentImport* source_import = source->imports + index;
            *destination_import = *source_import;
            destination_import->source_path = ide_string_copy(arena, source_import->source_path);
            destination_import->name_space = ide_string_copy(arena, source_import->name_space);
            destination_import->requested_path = ide_string_copy(arena, source_import->requested_path);
            destination_import->target_path = ide_string_copy(arena, source_import->target_path);
        }
    }
    if (mode == IDE_WORKSPACE_COPY_PRESERVE_DERIVED && source->entity_count)
    {
        destination->entities = arena_allocate(arena, IdeDocumentEntitySnapshot, source->entity_count);
        for (u32 index = 0; index < source->entity_count; index += 1)
        {
            IdeDocumentEntitySnapshot* destination_entity = destination->entities + index;
            IdeDocumentEntitySnapshot* source_entity = source->entities + index;
            *destination_entity = *source_entity;
            destination_entity->module_name = ide_string_copy(arena, source_entity->module_name);
            destination_entity->source_path = ide_string_copy(arena, source_entity->source_path);
            destination_entity->name = ide_string_copy(arena, source_entity->name);
            destination_entity->type_text = ide_string_copy(arena, source_entity->type_text);
        }
    }
    return true;
}

BUSTER_GLOBAL_LOCAL bool ide_workspace_copy_to_arena(Arena* arena, IdeDocumentWorkspace* destination, const IdeDocumentWorkspace* source)
{
    return ide_workspace_copy_to_arena_mode(arena, destination, source, IDE_WORKSPACE_COPY_PRESERVE_DERIVED);
}

BUSTER_GLOBAL_LOCAL void ide_workspace_initialize_empty(IdeDocumentWorkspace* workspace)
{
    *workspace = (IdeDocumentWorkspace){0};
    workspace->active_document_index = IDE_DOCUMENT_INDEX_INVALID;
    workspace->next_open_order = 1;
}

BUSTER_GLOBAL_LOCAL u64 ide_diagnostic_identity(String8 path, ParserSourceRange range, String8 message, IdeDocumentDiagnosticSeverity severity,
                                                IdeDocumentDiagnosticSource source)
{
    u64 values[] = {
        buster_hash_64((u8*)path.pointer, path.length),
        buster_hash_64((u8*)message.pointer, message.length),
        range.offset,
        range.length,
        range.line,
        range.column,
        (u64)(u32)severity,
        (u64)(u32)source,
    };
    return buster_hash_64((u8*)values, sizeof(values));
}

BUSTER_GLOBAL_LOCAL s32 ide_diagnostic_compare(IdeDocumentDiagnostic left, IdeDocumentDiagnostic right)
{
    s32 result = ide_string_compare(left.file_path, right.file_path, true);
    if (result == 0 && left.range.offset != right.range.offset)
    {
        result = left.range.offset < right.range.offset ? -1 : 1;
    }
    if (result == 0 && left.range.line != right.range.line)
    {
        result = left.range.line < right.range.line ? -1 : 1;
    }
    if (result == 0 && left.range.column != right.range.column)
    {
        result = left.range.column < right.range.column ? -1 : 1;
    }
    if (result == 0 && left.range.length != right.range.length)
    {
        result = left.range.length < right.range.length ? -1 : 1;
    }
    if (result == 0 && left.severity != right.severity)
    {
        result = left.severity < right.severity ? -1 : 1;
    }
    if (result == 0 && left.source != right.source)
    {
        result = left.source < right.source ? -1 : 1;
    }
    if (result == 0 && left.identity != right.identity)
    {
        result = left.identity < right.identity ? -1 : 1;
    }
    if (result == 0)
    {
        result = ide_string_compare(left.message, right.message, false);
    }
    return result;
}

BUSTER_GLOBAL_LOCAL void ide_diagnostics_sort(IdeDocument* document)
{
    for (u32 index = 1; index < document->diagnostic_count; index += 1)
    {
        IdeDocumentDiagnostic value = document->diagnostics[index];
        u32 insertion = index;
        while (insertion && ide_diagnostic_compare(document->diagnostics[insertion - 1], value) > 0)
        {
            document->diagnostics[insertion] = document->diagnostics[insertion - 1];
            insertion -= 1;
        }
        document->diagnostics[insertion] = value;
    }
}

BUSTER_GLOBAL_LOCAL IdeDocumentImportState ide_import_state_from_analysis(AnalysisImportResolutionState state)
{
    switch (state)
    {
        case ANALYSIS_IMPORT_CYCLE:
            return IDE_DOCUMENT_IMPORT_CYCLE;
        case ANALYSIS_IMPORT_AMBIGUOUS:
            return IDE_DOCUMENT_IMPORT_AMBIGUOUS;
        case ANALYSIS_IMPORT_MISSING:
            return IDE_DOCUMENT_IMPORT_MISSING;
        case ANALYSIS_IMPORT_RESOLVED:
            return IDE_DOCUMENT_IMPORT_RESOLVED;
        case ANALYSIS_IMPORT_UNRESOLVED:
        case ANALYSIS_IMPORT_COUNT:
            break;
    }
    return IDE_DOCUMENT_IMPORT_MISSING;
}

BUSTER_GLOBAL_LOCAL String8 ide_import_target_path(Arena* arena, String8 root, String8 requested)
{
    if (!requested.length)
    {
        return (String8){0};
    }
    String8 candidate = ide_path_is_absolute(requested) ? ide_string_copy(arena, requested) : ide_path_join(arena, root, requested, true);
    String8 canonical = ide_document_path_canonical(arena, candidate);
    bool missing_final = false;
    IdePathComponentStatus component_status = ide_path_component_status(arena, root, candidate, &missing_final);
    bool candidate_is_safe_or_missing_leaf = component_status == IDE_PATH_COMPONENT_SAFE ||
                                              (component_status == IDE_PATH_COMPONENT_MISSING && missing_final);
    if (canonical.length)
    {
        if (ide_document_path_is_within(root, canonical) &&
            candidate_is_safe_or_missing_leaf)
        {
            return canonical;
        }
        return (String8){0};
    }
    if (ide_document_path_is_within(root, candidate) &&
        candidate_is_safe_or_missing_leaf)
    {
        return candidate;
    }
    return (String8){0};
}

BUSTER_GLOBAL_LOCAL void ide_documents_sort(IdeDocument* documents, u32 count)
{
    for (u32 index = 1; index < count; index += 1)
    {
        IdeDocument document = documents[index];
        u32 insertion = index;
        while (insertion && ide_string_compare(documents[insertion - 1].identity, document.identity, true) > 0)
        {
            documents[insertion] = documents[insertion - 1];
            insertion -= 1;
        }
        documents[insertion] = document;
    }
}

BUSTER_GLOBAL_LOCAL bool ide_graph_module_has_semantic_imports(const IdeDocumentWorkspace* workspace, u32 module_index)
{
    IdeSyntaxRevision* revision = workspace->revision_states[module_index].current;
    return revision && revision->analysis_eligible;
}

BUSTER_GLOBAL_LOCAL IdeDocumentErrorKind ide_graph_build(Arena* arena, IdeDocumentWorkspace* workspace)
{
    u32 document_count = workspace->document_count;
    u64 offset_count = (u64)document_count + 1;
    if (document_count && !workspace->revision_states)
    {
        return IDE_DOCUMENT_ERROR_FILE_READ;
    }
    u64 import_count64 = 0;
    for (u32 index = 0; index < document_count; index += 1)
    {
        IdeSyntaxRevision* revision = workspace->revision_states[index].current;
        import_count64 += revision && revision->has_syntax ? revision->parser.import_count : 0;
    }
    if (import_count64 > UINT32_MAX)
    {
        return IDE_DOCUMENT_ERROR_TRAVERSAL_LIMIT;
    }
    u32 import_count = (u32)import_count64;
    IdeDocumentGraph* graph = arena_allocate(arena, IdeDocumentGraph, 1);
    *graph = (IdeDocumentGraph){0};
    workspace->graph = graph;
    workspace->imports = import_count ? arena_allocate(arena, IdeDocumentImport, import_count) : 0;
    workspace->import_count = import_count;
    if (document_count)
    {
        graph->import_offsets = arena_allocate(arena, u32, offset_count);
    }
    if (import_count)
    {
        graph->binding_targets = arena_allocate(arena, u32, import_count);
        graph->binding_states = arena_allocate(arena, AnalysisImportResolutionState, import_count);
    }

    u32 flat_import = 0;
    for (u32 source_index = 0; source_index < document_count; source_index += 1)
    {
        graph->import_offsets[source_index] = flat_import;
        IdeSyntaxRevision* revision = workspace->revision_states[source_index].current;
        ParserResult* parser = revision && revision->has_syntax ? &revision->parser : 0;
        for (AstImport* ast_import = parser ? parser->first_import : 0; ast_import; ast_import = ast_import->next)
        {
            u32 target_index = IDE_DOCUMENT_INDEX_INVALID;
            u32 match_count = 0;
            String8 normalized = ast_import->path;
            if (ide_document_path_is_bbb(normalized))
            {
                normalized = string_slice(normalized, 0, normalized.length - 4);
            }
            for (u32 candidate = 0; candidate < document_count; candidate += 1)
            {
                if (string_equal(workspace->documents[candidate].module_name, normalized))
                {
                    target_index = candidate;
                    match_count += 1;
                }
            }
            String8 target_path = {0};
            if (!match_count)
            {
                target_path = ide_import_target_path(arena, workspace->root_path, ast_import->path);
                if (target_path.length)
                {
                    target_index = ide_workspace_find_exact_path(workspace, target_path);
                    match_count = target_index != IDE_DOCUMENT_INDEX_INVALID;
                }
            }
            AnalysisImportResolutionState state = ANALYSIS_IMPORT_MISSING;
            if (match_count == 1)
            {
                state = ANALYSIS_IMPORT_RESOLVED;
                target_path = workspace->documents[target_index].path;
            }
            else if (match_count > 1)
            {
                state = ANALYSIS_IMPORT_AMBIGUOUS;
                target_index = IDE_DOCUMENT_INDEX_INVALID;
                target_path = (String8){0};
            }
            workspace->imports[flat_import] = (IdeDocumentImport){
                .source_path = workspace->documents[source_index].path,
                .name_space = ide_string_copy(arena, ast_import->name_space.text),
                .requested_path = ide_string_copy(arena, ast_import->path),
                .target_path = target_path,
                .range = ast_import->range,
                .path_range = ast_import->path_range,
                .state = ide_import_state_from_analysis(state),
            };
            graph->binding_targets[flat_import] = target_index;
            graph->binding_states[flat_import] = state;
            flat_import += 1;
        }
    }
    if (document_count)
    {
        graph->import_offsets[document_count] = flat_import;
    }
    BUSTER_CHECK(flat_import == import_count);

    u8* colors = document_count ? arena_allocate(arena, u8, document_count) : 0;
    IdeImportTraversalFrame* frames = document_count ? arena_allocate(arena, IdeImportTraversalFrame, document_count) : 0;
    if (document_count)
    {
        memset(colors, 0, document_count);
    }
    for (u32 root = 0; root < document_count; root += 1)
    {
        if (colors[root])
        {
            continue;
        }
        u32 frame_count = 1;
        frames[0] = (IdeImportTraversalFrame){.document_index = root, .next_import_index = graph->import_offsets[root]};
        colors[root] = 1;
        while (frame_count)
        {
            IdeImportTraversalFrame* frame = frames + frame_count - 1;
            u32 import_end = ide_graph_module_has_semantic_imports(workspace, frame->document_index)
                                 ? graph->import_offsets[frame->document_index + 1]
                                 : graph->import_offsets[frame->document_index];
            if (frame->next_import_index >= import_end)
            {
                colors[frame->document_index] = 2;
                frame_count -= 1;
                continue;
            }
            u32 import_index = frame->next_import_index;
            frame->next_import_index += 1;
            if (graph->binding_states[import_index] != ANALYSIS_IMPORT_RESOLVED)
            {
                continue;
            }
            u32 target = graph->binding_targets[import_index];
            if (target == IDE_DOCUMENT_INDEX_INVALID)
            {
                continue;
            }
            if (colors[target] == 1)
            {
                graph->binding_states[import_index] = ANALYSIS_IMPORT_CYCLE;
                workspace->imports[import_index].state = IDE_DOCUMENT_IMPORT_CYCLE;
                graph->has_cycle = true;
            }
            else if (!colors[target])
            {
                BUSTER_CHECK(frame_count < document_count);
                frames[frame_count] = (IdeImportTraversalFrame){
                    .document_index = target,
                    .next_import_index = graph->import_offsets[target],
                };
                frame_count += 1;
                colors[target] = 1;
            }
        }
    }

    u32 edge_count = 0;
    for (u32 source_index = 0; source_index < document_count; source_index += 1)
    {
        if (!ide_graph_module_has_semantic_imports(workspace, source_index))
        {
            continue;
        }
        for (u32 import_index = graph->import_offsets[source_index]; import_index < graph->import_offsets[source_index + 1]; import_index += 1)
        {
            edge_count += graph->binding_targets[import_index] != IDE_DOCUMENT_INDEX_INVALID &&
                          (graph->binding_states[import_index] == ANALYSIS_IMPORT_RESOLVED ||
                           graph->binding_states[import_index] == ANALYSIS_IMPORT_CYCLE);
        }
    }
    graph->edge_count = edge_count;
    if (document_count)
    {
        graph->forward_offsets = arena_allocate(arena, u32, offset_count);
        graph->reverse_offsets = arena_allocate(arena, u32, offset_count);
        memset(graph->reverse_offsets, 0, sizeof(u32) * offset_count);
        graph->dependency_order = arena_allocate(arena, u32, document_count);
    }
    if (edge_count)
    {
        graph->forward_edges = arena_allocate(arena, u32, edge_count);
        graph->reverse_edges = arena_allocate(arena, u32, edge_count);
    }
    u32 edge_index = 0;
    for (u32 source_index = 0; source_index < document_count; source_index += 1)
    {
        graph->forward_offsets[source_index] = edge_index;
        if (!ide_graph_module_has_semantic_imports(workspace, source_index))
        {
            continue;
        }
        for (u32 import_index = graph->import_offsets[source_index]; import_index < graph->import_offsets[source_index + 1]; import_index += 1)
        {
            u32 target = graph->binding_targets[import_index];
            AnalysisImportResolutionState state = graph->binding_states[import_index];
            if (target != IDE_DOCUMENT_INDEX_INVALID && (state == ANALYSIS_IMPORT_RESOLVED || state == ANALYSIS_IMPORT_CYCLE))
            {
                graph->forward_edges[edge_index] = target;
                graph->reverse_offsets[target + 1] += 1;
                edge_index += 1;
            }
        }
    }
    if (document_count)
    {
        graph->forward_offsets[document_count] = edge_index;
    }
    BUSTER_CHECK(edge_index == edge_count);
    for (u32 index = 0; index < document_count; index += 1)
    {
        graph->reverse_offsets[index + 1] += graph->reverse_offsets[index];
    }
    u32* reverse_cursor = document_count ? arena_allocate(arena, u32, document_count) : 0;
    if (document_count)
    {
        memcpy(reverse_cursor, graph->reverse_offsets, sizeof(u32) * document_count);
    }
    for (u32 source_index = 0; source_index < document_count; source_index += 1)
    {
        for (u32 edge = graph->forward_offsets[source_index]; edge < graph->forward_offsets[source_index + 1]; edge += 1)
        {
            u32 target = graph->forward_edges[edge];
            graph->reverse_edges[reverse_cursor[target]] = source_index;
            reverse_cursor[target] += 1;
        }
    }

    if (document_count)
    {
        memset(colors, 0, document_count);
    }
    u32 order_count = 0;
    for (u32 root = 0; root < document_count; root += 1)
    {
        if (colors[root])
        {
            continue;
        }
        u32 frame_count = 1;
        frames[0] = (IdeImportTraversalFrame){.document_index = root, .next_import_index = graph->import_offsets[root]};
        colors[root] = 1;
        while (frame_count)
        {
            IdeImportTraversalFrame* frame = frames + frame_count - 1;
            u32 import_end = ide_graph_module_has_semantic_imports(workspace, frame->document_index)
                                 ? graph->import_offsets[frame->document_index + 1]
                                 : graph->import_offsets[frame->document_index];
            if (frame->next_import_index >= import_end)
            {
                colors[frame->document_index] = 2;
                graph->dependency_order[order_count] = frame->document_index;
                order_count += 1;
                frame_count -= 1;
                continue;
            }
            u32 import_index = frame->next_import_index;
            frame->next_import_index += 1;
            if (graph->binding_states[import_index] != ANALYSIS_IMPORT_RESOLVED)
            {
                continue;
            }
            u32 target = graph->binding_targets[import_index];
            if (target == IDE_DOCUMENT_INDEX_INVALID)
            {
                continue;
            }
            if (!colors[target])
            {
                BUSTER_CHECK(frame_count < document_count);
                frames[frame_count] = (IdeImportTraversalFrame){
                    .document_index = target,
                    .next_import_index = graph->import_offsets[target],
                };
                frame_count += 1;
                colors[target] = 1;
            }
        }
    }
    BUSTER_CHECK(order_count == document_count);
    workspace->analysis_has_cycles = graph->has_cycle;
    return IDE_DOCUMENT_ERROR_NONE;
}

BUSTER_GLOBAL_LOCAL AstType* ide_ast_type_for_entity(AnalysisEntity* entity)
{
    if (!entity)
    {
        return 0;
    }
    switch (entity->kind)
    {
        case ANALYSIS_ENTITY_CODE:
            return entity->ast.code ? entity->ast.code->type : 0;
        case ANALYSIS_ENTITY_DATA:
            return entity->ast.data ? entity->ast.data->type : 0;
        case ANALYSIS_ENTITY_TYPE:
        case ANALYSIS_ENTITY_COUNT:
            break;
    }
    return 0;
}

BUSTER_GLOBAL_LOCAL String8 ide_source_range_text(Arena* arena, String8 source, ParserSourceRange range)
{
    if (!source.pointer || range.offset > source.length || range.length > source.length - range.offset)
    {
        return (String8){0};
    }
    return ide_string_copy(arena, string_slice(source, range.offset, range.offset + range.length));
}

BUSTER_GLOBAL_LOCAL String8 ide_analysis_type_fallback(Arena* arena, AnalysisResult* analysis, AnalysisTypeId id)
{
    if (!analysis || id.value == ANALYSIS_ID_UNDERLYING_INVALID)
    {
        return (String8){0};
    }
    AnalysisType* type = analysis_type_from_id(analysis, id);
    if (!type)
    {
        return (String8){0};
    }
    if (type->name.length)
    {
        return ide_string_copy(arena, type->name);
    }
    switch (type->kind)
    {
        case ANALYSIS_TYPE_VOID:
            return ide_string_copy(arena, S8("void"));
        case ANALYSIS_TYPE_BOOL:
            return ide_string_copy(arena, S8("bool"));
        case ANALYSIS_TYPE_INTEGER:
            return string_format_z(arena, type->as.integer.is_signed ? S8("s{u32}") : S8("u{u32}"), type->as.integer.bit_width);
        case ANALYSIS_TYPE_FLOAT:
            return string_format_z(arena, S8("f{u32}"), type->as.float_bit_width);
        case ANALYSIS_TYPE_POINTER:
            return ide_string_copy(arena, S8("pointer"));
        case ANALYSIS_TYPE_SLICE:
            return ide_string_copy(arena, S8("slice"));
        case ANALYSIS_TYPE_INFERRED_ARRAY:
            return ide_string_copy(arena, S8("inferred array"));
        case ANALYSIS_TYPE_ARRAY:
            return ide_string_copy(arena, S8("array"));
        case ANALYSIS_TYPE_VECTOR:
            return ide_string_copy(arena, S8("vector"));
        case ANALYSIS_TYPE_FUNCTION:
            return ide_string_copy(arena, S8("fn"));
        case ANALYSIS_TYPE_RANGE:
            return ide_string_copy(arena, S8("range"));
        case ANALYSIS_TYPE_STRUCT:
            return ide_string_copy(arena, S8("struct"));
        case ANALYSIS_TYPE_UNION:
            return ide_string_copy(arena, S8("union"));
        case ANALYSIS_TYPE_ENUM:
            return ide_string_copy(arena, S8("enum"));
        case ANALYSIS_TYPE_POISON:
        case ANALYSIS_TYPE_VA_LIST:
        case ANALYSIS_TYPE_COMPILE_TIME_PARAMETER:
        case ANALYSIS_TYPE_COUNT:
            break;
    }
    return (String8){0};
}

BUSTER_GLOBAL_LOCAL String8 ide_entity_type_text(Arena* arena, IdeDocument* document, AnalysisResult* analysis, AnalysisEntity* entity,
                                                 u32 entity_index)
{
    AstType* ast_type = ide_ast_type_for_entity(entity);
    String8 result = ast_type ? ide_source_range_text(arena, document->source, ast_type->range) : (String8){0};
    if (result.length)
    {
        return result;
    }
    if (analysis && analysis->module.semantics && entity_index < analysis->module.entity_count)
    {
        result = ide_analysis_type_fallback(arena, analysis, analysis->module.semantics[entity_index].type);
    }
    if (!result.length && entity && entity->kind == ANALYSIS_ENTITY_TYPE)
    {
        result = ide_string_copy(arena, S8("type"));
    }
    return result;
}

BUSTER_GLOBAL_LOCAL void ide_analysis_stats_begin(Arena* arena, IdeDocumentWorkspace* workspace)
{
#if BUSTER_INCLUDE_TESTS
    workspace->last_analysis_stats = (IdeDocumentAnalysisOperationStats){0};
    workspace->last_analysis_work_flags = workspace->document_count ? arena_allocate(arena, u8, workspace->document_count) : 0;
    if (workspace->document_count)
    {
        memset(workspace->last_analysis_work_flags, 0, workspace->document_count);
    }
#else
    BUSTER_UNUSED(arena);
    BUSTER_UNUSED(workspace);
#endif
}

BUSTER_GLOBAL_LOCAL void ide_analysis_stats_mark(IdeDocumentWorkspace* workspace, u32 index, u8 flag)
{
#if BUSTER_INCLUDE_TESTS
    BUSTER_CHECK(index < workspace->document_count && workspace->last_analysis_work_flags);
    u8 old = workspace->last_analysis_work_flags[index];
    workspace->last_analysis_work_flags[index] |= flag;
    if (!(old & flag))
    {
        workspace->last_analysis_stats.parsed_count += !!(flag & IDE_DOCUMENT_ANALYSIS_WORK_PARSED);
        workspace->last_analysis_stats.indexed_count += !!(flag & IDE_DOCUMENT_ANALYSIS_WORK_INDEXED);
        workspace->last_analysis_stats.analyzed_count += !!(flag & IDE_DOCUMENT_ANALYSIS_WORK_ANALYZED);
        workspace->last_analysis_stats.invalidated_count += !!(flag & IDE_DOCUMENT_ANALYSIS_WORK_INVALIDATED);
    }
#else
    BUSTER_UNUSED(workspace);
    BUSTER_UNUSED(index);
    BUSTER_UNUSED(flag);
#endif
}

BUSTER_GLOBAL_LOCAL void ide_analysis_stats_set_snapshot_count(IdeDocumentWorkspace* workspace, u32 snapshot_count)
{
#if BUSTER_INCLUDE_TESTS
    workspace->last_analysis_stats.allocated_snapshot_count = snapshot_count;
#else
    BUSTER_UNUSED(workspace);
    BUSTER_UNUSED(snapshot_count);
#endif
}

BUSTER_GLOBAL_LOCAL bool ide_analysis_result_has_generic(AnalysisResult* analysis)
{
    if (!analysis)
    {
        return false;
    }
    TemporalArena scratch = scratch_begin(0, 0);
    bool result = analysis->instantiation_count != 0;
    for (u32 index = 0; index < analysis->module.entity_count && !result; index += 1)
    {
        result = analysis_entity_is_generic(scratch.arena, analysis, analysis->module.entities + index);
    }
    scratch_end(scratch);
    return result;
}

BUSTER_GLOBAL_LOCAL void ide_snapshot_imports_build(Arena* arena, IdeDocumentWorkspace* workspace, u32 module_index, IdeModuleSnapshot* snapshot)
{
    u32 first = workspace->graph->import_offsets[module_index];
    u32 last = workspace->graph->import_offsets[module_index + 1];
    snapshot->import_count = last - first;
    snapshot->imports = snapshot->import_count ? arena_allocate(arena, IdeDocumentImport, snapshot->import_count) : 0;
    for (u32 index = 0; index < snapshot->import_count; index += 1)
    {
        IdeDocumentImport* source = workspace->imports + first + index;
        IdeDocumentImport* destination = snapshot->imports + index;
        *destination = *source;
        destination->source_path = ide_string_copy(arena, source->source_path);
        destination->name_space = ide_string_copy(arena, source->name_space);
        destination->requested_path = ide_string_copy(arena, source->requested_path);
        destination->target_path = ide_string_copy(arena, source->target_path);
    }
}

BUSTER_GLOBAL_LOCAL IdeDocumentErrorKind ide_snapshot_build(Arena* arena, IdeDocumentWorkspace* workspace, u32 module_index,
                                                             AnalysisResult* analysis, IdeModuleSnapshot* snapshot)
{
    IdeDocument* document = workspace->documents + module_index;
    IdeSyntaxRevision* revision = workspace->revision_states[module_index].current;
    IdeDocumentStorageOwner* owner = snapshot->owner;
    *snapshot = (IdeModuleSnapshot){
        .owner = owner,
        .syntax = revision,
        .analysis = analysis,
        .interface_bytes = ide_string_copy(arena, revision->interface_bytes),
        .interface_hash = revision->interface_hash,
        .has_generic_entities = ide_analysis_result_has_generic(analysis),
    };
    ide_snapshot_imports_build(arena, workspace, module_index, snapshot);

    snapshot->entity_count = analysis ? analysis->module.entity_count : 0;
    snapshot->entities = snapshot->entity_count ? arena_allocate(arena, IdeDocumentEntitySnapshot, snapshot->entity_count) : 0;
    for (u32 index = 0; index < snapshot->entity_count; index += 1)
    {
        AnalysisEntity* entity = analysis->module.entities + index;
        snapshot->entities[index] = (IdeDocumentEntitySnapshot){
            .module_name = ide_string_copy(arena, document->module_name),
            .source_path = ide_string_copy(arena, document->path),
            .name = ide_string_copy(arena, entity->name),
            .type_text = ide_entity_type_text(arena, document, analysis, entity, index),
            .range = entity->range,
            .kind = entity->kind == ANALYSIS_ENTITY_TYPE   ? IDE_DOCUMENT_ENTITY_TYPE
                    : entity->kind == ANALYSIS_ENTITY_CODE ? IDE_DOCUMENT_ENTITY_CODE
                                                           : IDE_DOCUMENT_ENTITY_DATA,
        };
    }

    u64 diagnostic_count64 = (u64)revision->parser.diagnostic_count + (revision->tokenizer_error_count != 0);
    diagnostic_count64 += analysis ? analysis->diagnostic_count : 0;
    if (diagnostic_count64 > UINT32_MAX)
    {
        return IDE_DOCUMENT_ERROR_TRAVERSAL_LIMIT;
    }
    snapshot->diagnostic_count = (u32)diagnostic_count64;
    snapshot->diagnostics = snapshot->diagnostic_count ? arena_allocate(arena, IdeDocumentDiagnostic, snapshot->diagnostic_count) : 0;
    u32 diagnostic_index = 0;
    if (revision->tokenizer_error_count)
    {
        snapshot->diagnostics[diagnostic_index] = (IdeDocumentDiagnostic){
            .file_path = ide_string_copy(arena, document->path),
            .message = ide_string_copy(arena, S8("tokenization failed")),
            .severity = IDE_DOCUMENT_DIAGNOSTIC_ERROR,
            .source = IDE_DOCUMENT_DIAGNOSTIC_SOURCE_PARSER,
        };
        diagnostic_index += 1;
    }
    for (ParserDiagnostic* diagnostic = revision->parser.first_diagnostic; diagnostic; diagnostic = diagnostic->next)
    {
        snapshot->diagnostics[diagnostic_index] = (IdeDocumentDiagnostic){
            .file_path = ide_string_copy(arena, document->path),
            .range = diagnostic->range,
            .message = ide_string_copy(arena, diagnostic->message),
            .severity = IDE_DOCUMENT_DIAGNOSTIC_ERROR,
            .source = IDE_DOCUMENT_DIAGNOSTIC_SOURCE_PARSER,
        };
        diagnostic_index += 1;
    }
    for (AnalysisDiagnostic* diagnostic = analysis ? analysis->first_diagnostic : 0; diagnostic; diagnostic = diagnostic->next)
    {
        snapshot->diagnostics[diagnostic_index] = (IdeDocumentDiagnostic){
            .file_path = ide_string_copy(arena, document->path),
            .range = diagnostic->range,
            .message = ide_string_copy(arena, diagnostic->message),
            .severity = IDE_DOCUMENT_DIAGNOSTIC_ERROR,
            .source = IDE_DOCUMENT_DIAGNOSTIC_SOURCE_ANALYSIS,
        };
        diagnostic_index += 1;
    }
    BUSTER_CHECK(diagnostic_index == snapshot->diagnostic_count);
    for (u32 index = 0; index < snapshot->diagnostic_count; index += 1)
    {
        IdeDocumentDiagnostic* diagnostic = snapshot->diagnostics + index;
        diagnostic->identity = ide_diagnostic_identity(diagnostic->file_path, diagnostic->range, diagnostic->message,
                                                       diagnostic->severity, diagnostic->source);
    }
    IdeDocument snapshot_document = {.diagnostics = snapshot->diagnostics, .diagnostic_count = snapshot->diagnostic_count};
    ide_diagnostics_sort(&snapshot_document);
    return IDE_DOCUMENT_ERROR_NONE;
}

BUSTER_GLOBAL_LOCAL IdeDocumentErrorKind ide_workspace_flatten_snapshots(Arena* arena, IdeDocumentWorkspace* workspace, u32 max_diagnostics)
{
    u64 entity_count64 = 0;
    u64 diagnostic_count64 = 0;
    for (u32 index = 0; index < workspace->document_count; index += 1)
    {
        IdeModuleSnapshot* snapshot = workspace->revision_states[index].module;
        if (!snapshot)
        {
            return IDE_DOCUMENT_ERROR_FILE_READ;
        }
        entity_count64 += snapshot->entity_count;
        diagnostic_count64 += snapshot->diagnostic_count;
    }
    if (diagnostic_count64 > max_diagnostics)
    {
        return IDE_DOCUMENT_ERROR_DIAGNOSTIC_LIMIT;
    }
    if (entity_count64 > UINT32_MAX)
    {
        return IDE_DOCUMENT_ERROR_TRAVERSAL_LIMIT;
    }
    workspace->entity_count = (u32)entity_count64;
    workspace->entities = workspace->entity_count ? arena_allocate(arena, IdeDocumentEntitySnapshot, workspace->entity_count) : 0;
    u32 flat_entity = 0;
    for (u32 module_index = 0; module_index < workspace->document_count; module_index += 1)
    {
        IdeDocument* document = workspace->documents + module_index;
        IdeModuleSnapshot* snapshot = workspace->revision_states[module_index].module;
        for (u32 index = 0; index < snapshot->entity_count; index += 1)
        {
            IdeDocumentEntitySnapshot* source = snapshot->entities + index;
            IdeDocumentEntitySnapshot* destination = workspace->entities + flat_entity;
            *destination = *source;
            destination->module_name = ide_string_copy(arena, source->module_name);
            destination->source_path = ide_string_copy(arena, source->source_path);
            destination->name = ide_string_copy(arena, source->name);
            destination->type_text = ide_string_copy(arena, source->type_text);
            flat_entity += 1;
        }
        document->diagnostic_count = snapshot->diagnostic_count;
        document->diagnostics = document->diagnostic_count ? arena_allocate(arena, IdeDocumentDiagnostic, document->diagnostic_count) : 0;
        for (u32 index = 0; index < document->diagnostic_count; index += 1)
        {
            document->diagnostics[index] = snapshot->diagnostics[index];
            document->diagnostics[index].file_path = ide_string_copy(arena, snapshot->diagnostics[index].file_path);
            document->diagnostics[index].message = ide_string_copy(arena, snapshot->diagnostics[index].message);
        }
    }
    BUSTER_CHECK(flat_entity == workspace->entity_count);
    return IDE_DOCUMENT_ERROR_NONE;
}

BUSTER_GLOBAL_LOCAL void ide_analysis_bind_graph_imports(Arena* result_arena, Arena* temporary_arena,
                                                          const IdeDocumentWorkspace* workspace, AnalysisResult** analyses, u32 module_index,
                                                          AnalysisResult** visible_modules, u32 visible_module_count)
{
    AnalysisResult* analysis = analyses[module_index];
    u32 binding_count = analysis->module.import_count;
    AnalysisImportBinding* bindings = binding_count ? arena_allocate(temporary_arena, AnalysisImportBinding, binding_count) : 0;
    u32 first_import = workspace->graph->import_offsets[module_index];
    BUSTER_CHECK(!workspace->revision_states[module_index].current->analysis_eligible ||
                 binding_count == workspace->graph->import_offsets[module_index + 1] - first_import);
    for (u32 binding_index = 0; binding_index < binding_count; binding_index += 1)
    {
        u32 flat_import = first_import + binding_index;
        u32 target = workspace->graph->binding_targets[flat_import];
        bindings[binding_index] = (AnalysisImportBinding){
            .target = target == IDE_DOCUMENT_INDEX_INVALID ? 0 : analyses[target],
            .state = workspace->graph->binding_states[flat_import],
        };
    }
    analysis_bind_module_imports(result_arena, analysis, visible_modules, visible_module_count, bindings, binding_count);
}

BUSTER_GLOBAL_LOCAL IdeDocumentErrorKind ide_analysis_full_from_cached(IdeDocumentModel* model, IdeDocumentWorkspace* workspace, bool full_fallback)
{
    u32 document_count = workspace->document_count;
    IdeAnalysisBatch* batch = ide_analysis_batch_create(model, document_count);
    if (!batch)
    {
        return IDE_DOCUMENT_ERROR_FILE_READ;
    }
    ide_analysis_stats_set_snapshot_count(workspace, batch->snapshot_count);
    Arena* arena = batch->owner.arena;
    AnalysisResult** analyses = document_count ? arena_allocate(model->staging_arena, AnalysisResult*, document_count) : 0;
    IdeDocumentStorageOwner** dependencies = document_count ? arena_allocate(model->staging_arena, IdeDocumentStorageOwner*, document_count) : 0;
    for (u32 index = 0; index < document_count; index += 1)
    {
        IdeSyntaxRevision* revision = workspace->revision_states[index].current;
        AnalysisSourceInput input = {
            .path = workspace->documents[index].path,
            .parser = revision->analysis_eligible ? &revision->parser : 0,
        };
        analyses[index] = arena_allocate(arena, AnalysisResult, 1);
        *analyses[index] = analysis_index_module(arena, (AnalysisModuleId){.value = index}, workspace->documents[index].module_name, &input, 1);
        dependencies[index] = revision->owner;
        ide_analysis_stats_mark(workspace, index, IDE_DOCUMENT_ANALYSIS_WORK_INDEXED);
        ide_analysis_stats_mark(workspace, index, IDE_DOCUMENT_ANALYSIS_WORK_INVALIDATED);
    }
    ide_owner_dependencies_set(&batch->owner, dependencies, document_count);
    for (u32 index = 0; index < document_count; index += 1)
    {
        ide_analysis_bind_graph_imports(arena, model->staging_arena, workspace, analyses, index, analyses, document_count);
    }
    for (u32 order_index = 0; order_index < document_count; order_index += 1)
    {
        analysis_resolve_module_interfaces(arena, analyses[workspace->graph->dependency_order[order_index]]);
    }
    bool contains_generics = false;
    for (u32 index = 0; index < document_count; index += 1)
    {
        contains_generics |= ide_analysis_result_has_generic(analyses[index]);
    }
    for (u32 index = 0; index < document_count; index += 1)
    {
        if (workspace->revision_states[index].current->analysis_eligible)
        {
            analysis_analyze_bodies(arena, analyses[index]);
        }
        ide_analysis_stats_mark(workspace, index, IDE_DOCUMENT_ANALYSIS_WORK_ANALYZED);
        IdeModuleSnapshot* snapshot = batch->snapshots + index;
        snapshot->owner = &batch->owner;
        IdeDocumentErrorKind error = ide_snapshot_build(arena, workspace, index, analyses[index], snapshot);
        if (error != IDE_DOCUMENT_ERROR_NONE)
        {
            return error;
        }
        workspace->revision_states[index].module = snapshot;
        contains_generics |= snapshot->has_generic_entities;
    }
    workspace->analysis_contains_generics = contains_generics;
#if BUSTER_INCLUDE_TESTS
    workspace->last_analysis_stats.full_fallback = full_fallback;
#else
    BUSTER_UNUSED(full_fallback);
#endif
    return IDE_DOCUMENT_ERROR_NONE;
}

BUSTER_GLOBAL_LOCAL IdeDocumentErrorKind ide_rebuild_workspace_analysis(IdeDocumentModel* model, IdeDocumentWorkspace* workspace,
                                                                         u32 max_diagnostics)
{
    u32 document_count = workspace->document_count;
    IdeDocumentRevisionState* previous_states = workspace->revision_states;
    IdeDocumentRevisionState* revision_states = document_count ? arena_allocate(model->staging_arena, IdeDocumentRevisionState, document_count) : 0;
    if (document_count)
    {
        memset(revision_states, 0, sizeof(*revision_states) * document_count);
    }
    workspace->revision_states = revision_states;
    ide_analysis_stats_begin(model->staging_arena, workspace);
    IdeSyntaxBatch* syntax_batch = 0;
    for (u32 index = 0; index < document_count; index += 1)
    {
        IdeDocument* document = workspace->documents + index;
        String8 source = document->source;
        String8 saved_source = document->saved_source;
        IdeSyntaxRevision* current = previous_states ? previous_states[index].current : 0;
        if (!current || !current->has_syntax || !string_equal(current->source, source))
        {
            if (!syntax_batch)
            {
                syntax_batch = ide_syntax_batch_create(model);
            }
            current = ide_syntax_revision_create(syntax_batch, model->expression_arena, document->module_name, source);
            ide_analysis_stats_mark(workspace, index, IDE_DOCUMENT_ANALYSIS_WORK_PARSED);
        }
        if (!current)
        {
            return IDE_DOCUMENT_ERROR_FILE_READ;
        }
        IdeSyntaxRevision* saved = string_equal(source, saved_source) ? current : (previous_states ? previous_states[index].saved : 0);
        if (!saved || !string_equal(saved->source, saved_source))
        {
            if (!syntax_batch)
            {
                syntax_batch = ide_syntax_batch_create(model);
            }
            saved = ide_source_revision_create(syntax_batch, saved_source);
        }
        workspace->revision_states[index] = (IdeDocumentRevisionState){.current = current, .saved = saved};
        document->source = current->source;
        document->saved_source = saved->source;
    }
    IdeDocumentErrorKind error = ide_graph_build(model->staging_arena, workspace);
    if (error == IDE_DOCUMENT_ERROR_NONE)
    {
        error = ide_analysis_full_from_cached(model, workspace, false);
    }
    if (error == IDE_DOCUMENT_ERROR_NONE)
    {
        error = ide_workspace_flatten_snapshots(model->staging_arena, workspace, max_diagnostics);
    }
    if (error == IDE_DOCUMENT_ERROR_NONE)
    {
        workspace->analysis_generation += 1;
    }
    return error;
}

BUSTER_GLOBAL_LOCAL IdeDocumentErrorKind ide_build_workspace(IdeDocumentModel* model, Arena* arena, String8 root_input, String8 open_input,
                                                              const IdeDocumentWorkspace* old_workspace, bool preserve_old,
                                                              u32 max_discovered_files, u32 max_traversal_entries, u32 max_diagnostics,
                                                              IdeDocumentWorkspace* result)
{
    ide_workspace_initialize_empty(result);

    String8 canonical_root = {0};
    String8 open_candidate = {0};
    String8 canonical_open = {0};
    if (open_input.length && !root_input.length)
    {
        // A standalone .bbb path is the workspace selector. This keeps CLI,
        // path-bar, and drop opens on the same single-root analysis flow.
        canonical_open = ide_document_path_canonical(arena, open_input);
        if (!canonical_open.length)
        {
            return IDE_DOCUMENT_ERROR_PATH_NOT_FOUND;
        }
        open_candidate = canonical_open;
        canonical_root = ide_path_parent(arena, canonical_open);
    }
    else
    {
        String8 root_candidate = root_input.length ? root_input : S8(".");
        canonical_root = ide_document_path_canonical(arena, root_candidate);
        open_candidate = open_input.length ? ide_path_resolve_for_root(arena, canonical_root, open_input) : (String8){0};
        canonical_open = open_input.length ? ide_document_path_canonical(arena, open_candidate) : (String8){0};
        if (open_input.length && !canonical_open.length)
        {
            return IDE_DOCUMENT_ERROR_PATH_NOT_FOUND;
        }
    }
    if (!canonical_root.length)
    {
        return IDE_DOCUMENT_ERROR_ROOT_NOT_FOUND;
    }

    IdePathKind root_kind = ide_path_kind(canonical_root);
    if (root_kind == IDE_PATH_REGULAR && ide_document_path_is_bbb(canonical_root))
    {
        if (!canonical_open.length)
        {
            canonical_open = canonical_root;
        }
        canonical_root = ide_path_parent(arena, canonical_root);
        root_kind = ide_path_kind(canonical_root);
    }
    if (root_kind == IDE_PATH_MISSING)
    {
        return IDE_DOCUMENT_ERROR_ROOT_NOT_FOUND;
    }
    if (root_kind != IDE_PATH_DIRECTORY)
    {
        return IDE_DOCUMENT_ERROR_ROOT_NOT_DIRECTORY;
    }
    if (open_input.length)
    {
        String8 security_open = open_candidate;
        if (security_open.length && ide_document_path_is_within(canonical_root, security_open) &&
            ide_path_component_status(arena, canonical_root, security_open, 0) == IDE_PATH_COMPONENT_UNSAFE)
        {
            return IDE_DOCUMENT_ERROR_PATH_OUTSIDE_ROOT;
        }
    }
    if (canonical_open.length)
    {
        if (!ide_document_path_is_bbb(canonical_open))
        {
            return IDE_DOCUMENT_ERROR_OPEN_PATH_NOT_SUPPORTED;
        }
        IdePathKind open_kind = ide_path_kind(canonical_open);
        if (open_kind == IDE_PATH_MISSING)
        {
            return IDE_DOCUMENT_ERROR_PATH_NOT_FOUND;
        }
        if (open_kind != IDE_PATH_REGULAR)
        {
            return IDE_DOCUMENT_ERROR_NOT_REGULAR_FILE;
        }
        if (!ide_document_path_is_within(canonical_root, canonical_open))
        {
            return IDE_DOCUMENT_ERROR_PATH_OUTSIDE_ROOT;
        }
    }

    IdeScanResult scan = {0};
    IdeDocumentErrorKind scan_error = ide_scan_workspace(arena, canonical_root, max_traversal_entries, &scan);
    if (scan_error != IDE_DOCUMENT_ERROR_NONE)
    {
        return scan_error;
    }

    u32 orphan_count = 0;
    if (preserve_old)
    {
        for (u32 old_index = 0; old_index < old_workspace->document_count; old_index += 1)
        {
            IdeDocument* old_document = old_workspace->documents + old_index;
            bool found = false;
            for (u32 path_index = 0; path_index < scan.path_count; path_index += 1)
            {
                String8 identity = ide_document_path_identity(arena, scan.paths[path_index]);
                found |= ide_identity_equal(old_document->identity, identity);
            }
            orphan_count += !found && (old_document->is_open || old_document->dirty);
        }
    }
    if ((u64)scan.path_count + orphan_count > max_discovered_files)
    {
        return IDE_DOCUMENT_ERROR_TRAVERSAL_LIMIT;
    }

    u32 document_count = scan.path_count + orphan_count;
    result->root_path = ide_string_copy(arena, canonical_root);
    result->document_count = document_count;
    result->documents = document_count ? arena_allocate(arena, IdeDocument, document_count) : 0;
    if (document_count && !result->documents)
    {
        return IDE_DOCUMENT_ERROR_FILE_READ;
    }
    if (document_count)
    {
        memset(result->documents, 0, sizeof(*result->documents) * document_count);
    }

    u32 document_index = 0;
    for (u32 path_index = 0; path_index < scan.path_count; path_index += 1)
    {
        String8 path = scan.paths[path_index];
        IdeDocument* document = result->documents + document_index;
        *document = (IdeDocument){0};
        document->path = ide_string_copy(arena, path);
        document->identity = ide_document_path_identity(arena, path);
        document->module_name = ide_module_name_from_path(arena, canonical_root, path);

        IdeLoadedFile loaded = {0};
        IdeDocumentErrorKind load_error = IDE_DOCUMENT_ERROR_NONE;
        if (!ide_file_read(arena, canonical_root, path, &loaded, &load_error))
        {
            return load_error;
        }
        document->source = loaded.source;
        document->saved_source = ide_string_copy(arena, loaded.source);
        document->external_stats = loaded.stats;
        document->external_hash = loaded.hash;
        document->saved_hash = loaded.hash;
        document->external_exists = true;
        document->revision = 1;
        document->saved_revision = 1;

        if (preserve_old)
        {
            u32 old_index = ide_workspace_find_identity((IdeDocumentWorkspace*)old_workspace, document->identity);
            if (old_index != IDE_DOCUMENT_INDEX_INVALID)
            {
                IdeDocument old_copy = old_workspace->documents[old_index];
                document->view = old_copy.view;
                document->search.query = ide_string_copy(arena, old_copy.search.query);
                document->search.replacement = ide_string_copy(arena, old_copy.search.replacement);
                document->search.match_count = old_copy.search.match_count;
                document->search.case_sensitive = old_copy.search.case_sensitive;
                document->search.whole_word = old_copy.search.whole_word;
                document->search.regular_expression = old_copy.search.regular_expression;
                document->compile.status = old_copy.compile.status;
                document->compile.compiled_revision = old_copy.compile.compiled_revision;
                document->compile.artifact_hash = old_copy.compile.artifact_hash;
                document->compile.artifact_path = ide_string_copy(arena, old_copy.compile.artifact_path);
                document->compile.command_line = ide_string_copy(arena, old_copy.compile.command_line);
                document->compile.message = ide_string_copy(arena, old_copy.compile.message);
                document->is_open = old_copy.is_open;
                document->open_order = old_copy.open_order;
                document->revision = old_copy.revision;
                document->saved_revision = old_copy.saved_revision;
                if (old_copy.dirty)
                {
                    document->source = ide_string_copy(arena, old_copy.source);
                    document->saved_source = ide_string_copy(arena, old_copy.saved_source);
                    document->saved_hash = old_copy.saved_hash;
                    document->external_stats = loaded.stats;
                    document->external_hash = loaded.hash;
                    document->external_exists = true;
                    document->external_modified = !string_equal(loaded.source, document->saved_source);
                    document->dirty = !string_equal(document->source, document->saved_source);
                }
                else
                {
                    bool source_changed = !string_equal(old_copy.source, loaded.source);
                    document->revision = old_copy.revision + source_changed;
                    document->saved_revision = document->revision;
                    document->saved_source = ide_string_copy(arena, loaded.source);
                    document->saved_hash = loaded.hash;
                    document->dirty = false;
                    document->external_modified = false;
                    if (source_changed)
                    {
                        document->compile.status = IDE_DOCUMENT_COMPILE_STALE;
                    }
                }
            }
        }
        document_index += 1;
    }

    if (preserve_old)
    {
        for (u32 old_index = 0; old_index < old_workspace->document_count; old_index += 1)
        {
            IdeDocument* old_document = old_workspace->documents + old_index;
            if (!old_document->is_open && !old_document->dirty)
            {
                continue;
            }
            if (ide_workspace_find_identity(result, old_document->identity) != IDE_DOCUMENT_INDEX_INVALID)
            {
                continue;
            }
            IdeDocument* orphan = result->documents + document_index;
            ide_document_copy_to_arena(arena, orphan, old_document, IDE_WORKSPACE_COPY_PRESERVE_DERIVED, true);
            orphan->external_exists = false;
            orphan->external_modified = true;
            document_index += 1;
        }
    }
    for (u32 index = 0; index < document_count; index += 1)
    {
        ide_document_clamp_view(result->documents + index);
    }
    BUSTER_CHECK(document_index == document_count);
    ide_documents_sort(result->documents, document_count);

    if (preserve_old)
    {
        result->analysis_generation = old_workspace->analysis_generation;
        result->revision_states = document_count ? arena_allocate(arena, IdeDocumentRevisionState, document_count) : 0;
        if (document_count)
        {
            memset(result->revision_states, 0, sizeof(*result->revision_states) * document_count);
        }
        for (u32 index = 0; index < document_count; index += 1)
        {
            u32 old_index = ide_workspace_find_identity((IdeDocumentWorkspace*)old_workspace, result->documents[index].identity);
            if (old_index != IDE_DOCUMENT_INDEX_INVALID && old_workspace->revision_states)
            {
                result->revision_states[index] = old_workspace->revision_states[old_index];
            }
        }
    }

    IdeDocumentErrorKind analysis_error = ide_rebuild_workspace_analysis(model, result, max_diagnostics);
    if (analysis_error != IDE_DOCUMENT_ERROR_NONE)
    {
        return analysis_error;
    }

    result->filter = preserve_old ? old_workspace->filter : (IdeDocumentWorkspaceFilterState){0};
    if (preserve_old)
    {
        result->filter.query = ide_string_copy(arena, old_workspace->filter.query);
        result->next_open_order = old_workspace->next_open_order;
    }
    if (!result->next_open_order)
    {
        result->next_open_order = 1;
    }
    result->open_document_count = 0;
    for (u32 index = 0; index < document_count; index += 1)
    {
        result->open_document_count += result->documents[index].is_open;
    }

    if (preserve_old)
    {
        String8 active_identity = {0};
        if (old_workspace->active_document_index != IDE_DOCUMENT_INDEX_INVALID && old_workspace->active_document_index < old_workspace->document_count)
        {
            active_identity = old_workspace->documents[old_workspace->active_document_index].identity;
        }
        result->active_document_index = ide_workspace_find_identity(result, active_identity);
        if (result->active_document_index != IDE_DOCUMENT_INDEX_INVALID && !result->documents[result->active_document_index].is_open)
        {
            result->active_document_index = IDE_DOCUMENT_INDEX_INVALID;
        }
        if (result->active_document_index == IDE_DOCUMENT_INDEX_INVALID)
        {
            u64 best_order = UINT64_MAX;
            for (u32 index = 0; index < document_count; index += 1)
            {
                if (result->documents[index].is_open && result->documents[index].open_order < best_order)
                {
                    best_order = result->documents[index].open_order;
                    result->active_document_index = index;
                }
            }
        }
    }
    else
    {
        u32 startup_index = IDE_DOCUMENT_INDEX_INVALID;
        if (canonical_open.length)
        {
            String8 startup_identity = ide_document_path_identity(arena, canonical_open);
            u32 startup_match_count = 0;
            for (u32 index = 0; index < document_count; index += 1)
            {
                if (ide_identity_equal(result->documents[index].identity, startup_identity))
                {
                    startup_index = index;
                    startup_match_count += 1;
                }
            }
            if (startup_match_count != 1)
            {
                return IDE_DOCUMENT_ERROR_OPEN_PATH_NOT_SUPPORTED;
            }
        }
        else if (document_count)
        {
            startup_index = 0;
        }
        if (startup_index != IDE_DOCUMENT_INDEX_INVALID)
        {
            result->documents[startup_index].is_open = true;
            result->documents[startup_index].open_order = result->next_open_order;
            result->next_open_order += 1;
            result->open_document_count = 1;
            result->active_document_index = startup_index;
        }
    }
    return IDE_DOCUMENT_ERROR_NONE;
}

BUSTER_GLOBAL_LOCAL void ide_model_reset_staging(IdeDocumentModel* model)
{
    ide_model_release_staged_owners(model);
    arena_reset_to_start(model->staging_arena);
}

BUSTER_GLOBAL_LOCAL void ide_workspace_retain_owners(const IdeDocumentWorkspace* workspace)
{
    if (!workspace || !workspace->revision_states)
    {
        return;
    }
    for (u32 index = 0; index < workspace->document_count; index += 1)
    {
        const IdeDocumentRevisionState* state = workspace->revision_states + index;
        ide_owner_retain(state->current ? state->current->owner : 0);
        ide_owner_retain(state->saved ? state->saved->owner : 0);
        ide_owner_retain(state->module ? state->module->owner : 0);
    }
}

BUSTER_GLOBAL_LOCAL void ide_workspace_release_owners(const IdeDocumentWorkspace* workspace)
{
    if (!workspace || !workspace->revision_states)
    {
        return;
    }
    for (u32 index = 0; index < workspace->document_count; index += 1)
    {
        const IdeDocumentRevisionState* state = workspace->revision_states + index;
        ide_owner_release(state->current ? state->current->owner : 0);
        ide_owner_release(state->saved ? state->saved->owner : 0);
        ide_owner_release(state->module ? state->module->owner : 0);
    }
}

BUSTER_GLOBAL_LOCAL bool ide_workspace_has_dirty_documents(const IdeDocumentWorkspace* workspace)
{
    if (!workspace)
    {
        return false;
    }
    for (u32 index = 0; index < workspace->document_count; index += 1)
    {
        if (workspace->documents[index].dirty)
        {
            return true;
        }
    }
    return false;
}

BUSTER_GLOBAL_LOCAL void ide_model_commit(IdeDocumentModel* model, IdeDocumentWorkspace workspace)
{
    Arena* old_active = model->active_arena;
    IdeDocumentWorkspace old_workspace = model->workspace;
    ide_workspace_retain_owners(&workspace);
    model->workspace = workspace;
    model->active_arena = model->staging_arena;
    model->staging_arena = old_active;
    ide_workspace_release_owners(&old_workspace);
    ide_model_release_staged_owners(model);
    arena_reset_to_start(model->staging_arena);
}

BUSTER_GLOBAL_LOCAL u32 ide_active_previous_or_first(IdeDocumentWorkspace* workspace, u64 closed_order)
{
    u32 result = IDE_DOCUMENT_INDEX_INVALID;
    u64 previous_order = 0;
    u64 first_order = UINT64_MAX;
    u32 first_index = IDE_DOCUMENT_INDEX_INVALID;
    for (u32 index = 0; index < workspace->document_count; index += 1)
    {
        IdeDocument* document = workspace->documents + index;
        if (!document->is_open)
        {
            continue;
        }
        if (document->open_order < closed_order && document->open_order > previous_order)
        {
            previous_order = document->open_order;
            result = index;
        }
        if (document->open_order < first_order)
        {
            first_order = document->open_order;
            first_index = index;
        }
    }
    return result != IDE_DOCUMENT_INDEX_INVALID ? result : first_index;
}

BUSTER_GLOBAL_LOCAL void ide_document_compile_mark_stale(IdeDocument* document)
{
    document->compile.status = IDE_DOCUMENT_COMPILE_STALE;
}

String8 ide_document_error_kind_name(IdeDocumentErrorKind kind)
{
    String8 names[] = {
        S8("none"),
        S8("invalid argument"),
        S8("workspace root was not found"),
        S8("workspace root is not a directory"),
        S8("path was not found"),
        S8("path is outside the workspace root"),
        S8("path is not a regular file"),
        S8("file read failed"),
        S8("file write failed"),
        S8("external modification conflicts with the saved document"),
        S8("explicit open path is not a discovered .bbb document"),
        S8("document model is already initialized"),
        S8("workspace traversal limit exceeded"),
        S8("document was not found"),
        S8("document is not open"),
        S8("an active document is required"),
        S8("reload would discard dirty edits"),
        S8("selection is outside the source"),
        S8("diagnostic limit exceeded"),
        S8("workspace replacement would discard dirty documents"),
        S8("requested filter mode is unsupported"),
    };
    return kind < IDE_DOCUMENT_ERROR_COUNT ? names[kind] : S8("unknown document error");
}

IdeDocumentErrorKind ide_document_model_initialize(IdeDocumentModel* model, Arena* arena, Arena* staging_arena, IdeDocumentModelOptions options)
{
    if (!model)
    {
        return IDE_DOCUMENT_ERROR_INVALID_ARGUMENT;
    }
    if (model->initialized)
    {
        return IDE_DOCUMENT_ERROR_ALREADY_INITIALIZED;
    }
    if (!arena || !staging_arena || arena == staging_arena)
    {
        return IDE_DOCUMENT_ERROR_INVALID_ARGUMENT;
    }
    if (options.expression_arena && (options.expression_arena == arena || options.expression_arena == staging_arena))
    {
        return IDE_DOCUMENT_ERROR_INVALID_ARGUMENT;
    }
    // Model construction is the required serial boundary before later edits
    // may tokenize from worker-backed IDE paths.
    tokenizer_prewarm();
    memset(model, 0, sizeof(*model));
    model->active_arena = arena;
    model->staging_arena = staging_arena;
    if (options.expression_arena)
    {
        model->expression_arena = options.expression_arena;
    }
    else
    {
        model->expression_arena = arena_create((ArenaCreation){.reserved_size = BUSTER_MB(64)});
        if (!model->expression_arena)
        {
            return IDE_DOCUMENT_ERROR_INVALID_ARGUMENT;
        }
        model->owns_expression_arena = true;
    }
    model->max_discovered_files = options.max_discovered_files ? options.max_discovered_files : IDE_DOCUMENT_DEFAULT_MAX_DISCOVERED_FILES;
    model->max_traversal_entries = options.max_traversal_entries ? options.max_traversal_entries : IDE_DOCUMENT_DEFAULT_MAX_TRAVERSAL_ENTRIES;
    model->max_diagnostics = options.max_diagnostics ? options.max_diagnostics : IDE_DOCUMENT_DEFAULT_MAX_DIAGNOSTICS;
    ide_workspace_initialize_empty(&model->workspace);
    arena_reset_to_start(model->staging_arena);
    if (!options.workspace_root.length && !options.open_path.length)
    {
        model->initialized = true;
        return IDE_DOCUMENT_ERROR_NONE;
    }
    IdeDocumentWorkspace workspace = {0};
    IdeDocumentErrorKind error = ide_build_workspace(model, model->staging_arena, options.workspace_root, options.open_path, 0, false,
                                                      model->max_discovered_files, model->max_traversal_entries, model->max_diagnostics, &workspace);
    if (error != IDE_DOCUMENT_ERROR_NONE)
    {
        ide_model_reset_staging(model);
        if (model->owns_expression_arena)
        {
            arena_destroy(model->expression_arena, 1);
            model->expression_arena = 0;
            model->owns_expression_arena = false;
        }
        return error;
    }
    model->initialized = true;
    ide_model_commit(model, workspace);
    return IDE_DOCUMENT_ERROR_NONE;
}

void ide_document_model_deinitialize(IdeDocumentModel* model)
{
    if (!model)
    {
        return;
    }
    Arena* expression_arena = model->expression_arena;
    bool owns_expression_arena = model->owns_expression_arena;
    ide_workspace_release_owners(&model->workspace);
    ide_model_release_staged_owners(model);
    if (model->active_arena)
    {
        arena_reset_to_start(model->active_arena);
    }
    if (model->staging_arena)
    {
        arena_reset_to_start(model->staging_arena);
    }
    if (owns_expression_arena && expression_arena)
    {
        arena_destroy(expression_arena, 1);
    }
    memset(model, 0, sizeof(*model));
}

typedef struct IdeSessionRecord IdeSessionRecord;
struct IdeSessionRecord
{
    String8 path;
    String8 query;
    String8 replacement;
    u64 order;
    IdeDocumentViewState view;
    u32 match_count;
    u32 flags;
};

typedef struct IdeSessionSnapshot IdeSessionSnapshot;
struct IdeSessionSnapshot
{
    IdeSessionRecord* records;
    u32 count;
    String8 active;
    String8 filter;
    u32 filter_flags;
};

enum { IDE_SESSION_HEADER = 32, IDE_SESSION_RECORD = 64, IDE_SESSION_VERSION = 1 };
BUSTER_GLOBAL_LOCAL const u8 ide_session_magic[8] = {'B', 'U', 'S', 'T', 'E', 'R', 'U', 'I'};

BUSTER_GLOBAL_LOCAL void ide_session_put(u8** cursor, const void* data, u64 size)
{
    memcpy(*cursor, data, size);
    *cursor += size;
}
BUSTER_GLOBAL_LOCAL void ide_session_put_u32(u8** cursor, u32 value)
{
    u8 data[4] = {(u8)value, (u8)(value >> 8), (u8)(value >> 16), (u8)(value >> 24)};
    ide_session_put(cursor, data, sizeof(data));
}
BUSTER_GLOBAL_LOCAL void ide_session_put_u64(u8** cursor, u64 value)
{
    u8 data[8] = {0};
    u32 index;
    for (index = 0; index < 8; index += 1) data[index] = (u8)(value >> (index * 8));
    ide_session_put(cursor, data, sizeof(data));
}
BUSTER_GLOBAL_LOCAL bool ide_session_take(const u8* bytes, u64 length, u64* cursor, void* data, u64 size)
{
    if (*cursor > length || size > length - *cursor) return false;
    memcpy(data, bytes + *cursor, size);
    *cursor += size;
    return true;
}
BUSTER_GLOBAL_LOCAL bool ide_session_take_u32(const u8* bytes, u64 length, u64* cursor, u32* result)
{
    u8 data[4];
    if (!ide_session_take(bytes, length, cursor, data, sizeof(data))) return false;
    *result = (u32)data[0] | ((u32)data[1] << 8) | ((u32)data[2] << 16) | ((u32)data[3] << 24);
    return true;
}
BUSTER_GLOBAL_LOCAL bool ide_session_take_u64(const u8* bytes, u64 length, u64* cursor, u64* result)
{
    u8 data[8];
    if (!ide_session_take(bytes, length, cursor, data, sizeof(data))) return false;
    *result = 0;
    u32 index;
    for (index = 0; index < 8; index += 1) *result |= (u64)data[index] << (index * 8);
    return true;
}
BUSTER_GLOBAL_LOCAL bool ide_session_string(Arena* arena, const u8* bytes, u64 length, u64* cursor, u32 size, String8* result)
{
    if (size > IDE_DOCUMENT_SESSION_MAX_STRING_LENGTH || *cursor > length || size > length - *cursor) return false;
    *result = ide_string_copy(arena, (String8){.pointer = (char8*)(bytes + *cursor), .length = size});
    *cursor += size;
    return size == 0 || result->pointer != 0;
}
BUSTER_GLOBAL_LOCAL bool ide_session_relative(String8 value)
{
    if (!value.length || value.length > IDE_DOCUMENT_SESSION_MAX_STRING_LENGTH || ide_path_is_absolute(value)) return false;
    u64 start = 0;
    u64 index;
    for (index = 0; index <= value.length; index += 1)
    {
        if (index == value.length || ide_path_separator(value.pointer[index]))
        {
            String8 part = string_slice(value, start, index);
            if (!part.length || string_equal(part, S8(".")) || string_equal(part, S8(".."))) return false;
            start = index + 1;
        }
        else if (!value.pointer[index]) return false;
    }
    return true;
}
BUSTER_GLOBAL_LOCAL String8 ide_session_rel(Arena* arena, String8 root, String8 path)
{
    if (!ide_document_path_is_within(root, path)) return (String8){0};
    u64 start = root.length;
    while (start < path.length && ide_path_separator(path.pointer[start])) start += 1;
    if (path.length - start > IDE_DOCUMENT_SESSION_MAX_STRING_LENGTH) return (String8){0};
    String8 result = ide_string_copy(arena, string_slice(path, start, path.length));
    return ide_session_relative(result) ? result : (String8){0};
}
BUSTER_GLOBAL_LOCAL String8 ide_session_path(Arena* arena, String8 root)
{
    return ide_path_join(arena, root, S8(".buster-session"), false);
}
BUSTER_GLOBAL_LOCAL bool ide_session_float(u32 bits)
{
    return (bits & 0x7f800000u) != 0x7f800000u;
}
BUSTER_GLOBAL_LOCAL bool ide_session_parse(Arena* arena, ByteSlice bytes, IdeSessionSnapshot* result)
{
    *result = (IdeSessionSnapshot){0};
    if (!bytes.pointer || bytes.length < IDE_SESSION_HEADER || bytes.length > IDE_DOCUMENT_SESSION_MAX_BYTES) return false;
    u64 cursor = 0;
    u8 magic[8]; u32 version, header, count, active_size, filter_size, filter_flags;
    if (!ide_session_take(bytes.pointer, bytes.length, &cursor, magic, sizeof(magic)) || memcmp(magic, ide_session_magic, 8) != 0 ||
        !ide_session_take_u32(bytes.pointer, bytes.length, &cursor, &version) || !ide_session_take_u32(bytes.pointer, bytes.length, &cursor, &header) ||
        !ide_session_take_u32(bytes.pointer, bytes.length, &cursor, &count) || !ide_session_take_u32(bytes.pointer, bytes.length, &cursor, &active_size) ||
        !ide_session_take_u32(bytes.pointer, bytes.length, &cursor, &filter_size) || !ide_session_take_u32(bytes.pointer, bytes.length, &cursor, &filter_flags) ||
        version != IDE_SESSION_VERSION || header != IDE_SESSION_HEADER || count > IDE_DOCUMENT_SESSION_MAX_DOCUMENTS ||
        active_size > IDE_DOCUMENT_SESSION_MAX_STRING_LENGTH || filter_size > IDE_DOCUMENT_SESSION_MAX_STRING_LENGTH || (filter_flags & ~15u)) return false;
    result->records = count ? arena_allocate(arena, IdeSessionRecord, count) : 0;
    if (count && !result->records) return false;
    u32 index;
    for (index = 0; index < count; index += 1)
    {
        IdeSessionRecord* record = result->records + index; u32 path_size, query_size, replacement_size, flags, match_count, bits; u64 order; u32 previous;
        if (!ide_session_take_u32(bytes.pointer, bytes.length, &cursor, &path_size) || !ide_session_take_u32(bytes.pointer, bytes.length, &cursor, &query_size) ||
            !ide_session_take_u32(bytes.pointer, bytes.length, &cursor, &replacement_size) || !ide_session_take_u32(bytes.pointer, bytes.length, &cursor, &flags) ||
            !ide_session_take_u32(bytes.pointer, bytes.length, &cursor, &match_count) || !ide_session_take_u64(bytes.pointer, bytes.length, &cursor, &order) ||
            !ide_session_take_u64(bytes.pointer, bytes.length, &cursor, &record->view.cursor_offset) || !ide_session_take_u64(bytes.pointer, bytes.length, &cursor, &record->view.selection_start) ||
            !ide_session_take_u64(bytes.pointer, bytes.length, &cursor, &record->view.selection_end) || !ide_session_take_u32(bytes.pointer, bytes.length, &cursor, &bits) || !ide_session_float(bits)) return false;
        memcpy(&record->view.scroll_x, &bits, sizeof(bits));
        if (!ide_session_take_u32(bytes.pointer, bytes.length, &cursor, &bits) || !ide_session_float(bits)) return false;
        memcpy(&record->view.scroll_y, &bits, sizeof(bits));
        if (!ide_session_take_u32(bytes.pointer, bytes.length, &cursor, &bits) || !ide_session_float(bits)) return false;
        memcpy(&record->view.zoom, &bits, sizeof(bits));
        if (order >= UINT64_MAX - 1 || (flags & ~7u) || !ide_session_string(arena, bytes.pointer, bytes.length, &cursor, path_size, &record->path) || !ide_session_relative(record->path) ||
            !ide_session_string(arena, bytes.pointer, bytes.length, &cursor, query_size, &record->query) || !ide_session_string(arena, bytes.pointer, bytes.length, &cursor, replacement_size, &record->replacement)) return false;
        record->order = order; record->flags = flags; record->match_count = match_count;
        for (previous = 0; previous < index; previous += 1) { if (record->order == result->records[previous].order || ide_identity_equal(record->path, result->records[previous].path)) return false; }
    }
    if (!ide_session_string(arena, bytes.pointer, bytes.length, &cursor, active_size, &result->active) || (result->active.length && !ide_session_relative(result->active)) ||
        !ide_session_string(arena, bytes.pointer, bytes.length, &cursor, filter_size, &result->filter) || cursor != bytes.length) return false;
    result->count = count; result->filter_flags = filter_flags; return true;
}

BUSTER_GLOBAL_LOCAL String8 ide_session_resolve(Arena* arena, String8 root, String8 relative)
{
    if (!ide_session_relative(relative)) return (String8){0};
    String8 candidate = ide_path_join(arena, root, relative, false);
    String8 canonical = ide_document_path_canonical(arena, candidate);
    if (!canonical.length || !ide_document_path_is_within(root, canonical)) return (String8){0};
    return canonical;
}
BUSTER_GLOBAL_LOCAL bool ide_session_validate_path(Arena* arena, String8 root, String8 relative)
{
    String8 candidate = ide_path_join(arena, root, relative, false);
    IdePathComponentStatus status = ide_path_component_status(arena, root, candidate, 0);
    if (status == IDE_PATH_COMPONENT_UNSAFE) return false;
    if (status == IDE_PATH_COMPONENT_MISSING) return true;
    String8 canonical = ide_document_path_canonical(arena, candidate);
    return canonical.length && ide_document_path_is_within(root, canonical);
}
BUSTER_GLOBAL_LOCAL bool ide_session_size(u64* total, u64 amount)
{
    if (amount > IDE_DOCUMENT_SESSION_MAX_BYTES - *total) return false;
    *total += amount;
    return true;
}
BUSTER_GLOBAL_LOCAL ByteSlice ide_session_read(Arena* arena, String8 path)
{
#if defined(__linux__) || defined(__APPLE__)
    int descriptor = open((const char*)path.pointer, O_RDONLY | O_NOFOLLOW);
    if (descriptor < 0) return (ByteSlice){0};
    struct stat stats = {0};
    bool regular = fstat(descriptor, &stats) == 0 && S_ISREG(stats.st_mode) && stats.st_size >= 0;
    u64 size = regular ? (u64)stats.st_size : 0;
    u8* data = regular && size <= IDE_DOCUMENT_SESSION_MAX_BYTES ? arena_allocate(arena, u8, BUSTER_MAX(size, 1u)) : 0;
    u64 offset = 0;
    while (data && offset < size)
    {
        ssize_t count = read(descriptor, data + offset, (size_t)BUSTER_MIN(size - offset, (u64)(SIZE_MAX >> 1)));
        if (count <= 0) { data = 0; break; }
        offset += (u64)count;
    }
    bool closed = close(descriptor) == 0;
    return data && offset == size && closed ? (ByteSlice){data, size} : (ByteSlice){0};
#elif defined(_WIN32)
    String16 path_w = string16_from_string8(arena, path, true);
    HANDLE file = CreateFileW(path_w.pointer, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, 0, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, 0);
    if (file == INVALID_HANDLE_VALUE) return (ByteSlice){0};
    BY_HANDLE_FILE_INFORMATION information = {0};
    LARGE_INTEGER file_size = {0};
    bool regular = GetFileInformationByHandle(file, &information) && !(information.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) &&
                   GetFileSizeEx(file, &file_size) && file_size.QuadPart >= 0;
    u64 size = regular ? (u64)file_size.QuadPart : 0;
    u8* data = regular && size <= IDE_DOCUMENT_SESSION_MAX_BYTES ? arena_allocate(arena, u8, BUSTER_MAX(size, 1u)) : 0;
    u64 offset = 0;
    while (data && offset < size)
    {
        DWORD count = 0;
        if (!ReadFile(file, data + offset, (DWORD)BUSTER_MIN(size - offset, (u64)UINT32_MAX), &count, 0) || !count) { data = 0; break; }
        offset += count;
    }
    bool closed = CloseHandle(file) != 0;
    return data && offset == size && closed ? (ByteSlice){data, size} : (ByteSlice){0};
#else
    BUSTER_UNUSED(arena); BUSTER_UNUSED(path); return (ByteSlice){0};
#endif
}

BUSTER_GLOBAL_LOCAL bool ide_session_create(Arena* arena, String8 path)
{
#if defined(__linux__) || defined(__APPLE__)
    BUSTER_UNUSED(arena);
    int descriptor = open((const char*)path.pointer, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0600);
    return descriptor >= 0 && close(descriptor) == 0;
#elif defined(_WIN32)
    String16 path_w = string16_from_string8(arena, path, true);
    HANDLE file = CreateFileW(path_w.pointer, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, 0, CREATE_NEW,
                              FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, 0);
    return file != INVALID_HANDLE_VALUE && CloseHandle(file) != 0;
#else
    BUSTER_UNUSED(arena); BUSTER_UNUSED(path); return false;
#endif
}

bool ide_document_model_session_save(IdeDocumentModel* model)
{
    if (!model || !model->initialized || !model->workspace.root_path.length) return false;
    Arena* conflicts[] = {model->active_arena, model->staging_arena};
    TemporalArena scratch = scratch_begin(conflicts, BUSTER_ARRAY_LENGTH(conflicts));
    u32 count = 0;
    u32 index;
    for (index = 0; index < model->workspace.document_count; index += 1) count += model->workspace.documents[index].is_open;
    if (count > IDE_DOCUMENT_SESSION_MAX_DOCUMENTS) { scratch_end(scratch); return false; }
    IdeSessionRecord* records = count ? arena_allocate(scratch.arena, IdeSessionRecord, count) : 0;
    bool success = !count || records;
    u32 record_count = 0;
    for (index = 0; success && index < model->workspace.document_count; index += 1)
    {
        IdeDocument* document = model->workspace.documents + index;
        if (!document->is_open) continue;
        IdeSessionRecord* record = records + record_count++;
        *record = (IdeSessionRecord){.path = ide_session_rel(scratch.arena, model->workspace.root_path, document->path), .order = document->open_order,
                                     .view = document->view, .match_count = document->search.match_count,
                                     .flags = (document->search.case_sensitive ? 1u : 0u) | (document->search.whole_word ? 2u : 0u) |
                                              (document->search.regular_expression ? 4u : 0u)};
        if (document->search.query.length <= IDE_DOCUMENT_SESSION_MAX_STRING_LENGTH && document->search.replacement.length <= IDE_DOCUMENT_SESSION_MAX_STRING_LENGTH)
        {
            record->query = ide_string_copy(scratch.arena, document->search.query);
            record->replacement = ide_string_copy(scratch.arena, document->search.replacement);
        }
        u32 view_bits[3] = {0};
        memcpy(view_bits, &record->view.scroll_x, sizeof(view_bits));
        success = record->path.length && record->order < UINT64_MAX - 1 && ide_session_relative(record->path) && record->query.length == document->search.query.length &&
                  record->replacement.length == document->search.replacement.length && ide_session_float(view_bits[0]) && ide_session_float(view_bits[1]) && ide_session_float(view_bits[2]);
        u32 previous;
        for (previous = 0; success && previous + 1 < record_count; previous += 1)
        {
            success = record->order != records[previous].order && !ide_identity_equal(record->path, records[previous].path);
        }
    }
    String8 active = {0};
    if (success && model->workspace.active_document_index != IDE_DOCUMENT_INDEX_INVALID && model->workspace.active_document_index < model->workspace.document_count &&
        model->workspace.documents[model->workspace.active_document_index].is_open)
    {
        active = ide_session_rel(scratch.arena, model->workspace.root_path, model->workspace.documents[model->workspace.active_document_index].path);
        success = active.length != 0;
    }
    bool filter_ok = model->workspace.filter.query.length <= IDE_DOCUMENT_SESSION_MAX_STRING_LENGTH;
    String8 filter = filter_ok ? ide_string_copy(scratch.arena, model->workspace.filter.query) : (String8){0};
    u32 filter_flags = (model->workspace.filter.show_open_only ? 1u : 0u) | (model->workspace.filter.show_dirty_only ? 2u : 0u) |
                       (model->workspace.filter.show_diagnostics_only ? 4u : 0u) | (model->workspace.filter.case_sensitive ? 8u : 0u);
    u64 total = IDE_SESSION_HEADER;
    success = success && filter_ok && active.length <= IDE_DOCUMENT_SESSION_MAX_STRING_LENGTH && filter.length <= IDE_DOCUMENT_SESSION_MAX_STRING_LENGTH &&
              ide_session_size(&total, active.length) && ide_session_size(&total, filter.length);
    for (index = 0; success && index < record_count; index += 1)
    {
        success = ide_session_size(&total, IDE_SESSION_RECORD) && ide_session_size(&total, records[index].path.length) &&
                  ide_session_size(&total, records[index].query.length) && ide_session_size(&total, records[index].replacement.length);
    }
    if (success)
    {
        u8* output = arena_allocate(scratch.arena, u8, total);
        success = output != 0;
        if (success)
        {
            u8* cursor = output;
            ide_session_put(&cursor, ide_session_magic, sizeof(ide_session_magic));
            ide_session_put_u32(&cursor, IDE_SESSION_VERSION); ide_session_put_u32(&cursor, IDE_SESSION_HEADER); ide_session_put_u32(&cursor, record_count);
            ide_session_put_u32(&cursor, (u32)active.length); ide_session_put_u32(&cursor, (u32)filter.length); ide_session_put_u32(&cursor, filter_flags);
            for (index = 0; index < record_count; index += 1)
            {
                IdeSessionRecord* record = records + index; u32 bits = 0;
                ide_session_put_u32(&cursor, (u32)record->path.length); ide_session_put_u32(&cursor, (u32)record->query.length);
                ide_session_put_u32(&cursor, (u32)record->replacement.length); ide_session_put_u32(&cursor, record->flags); ide_session_put_u32(&cursor, record->match_count);
                ide_session_put_u64(&cursor, record->order); ide_session_put_u64(&cursor, record->view.cursor_offset); ide_session_put_u64(&cursor, record->view.selection_start);
                ide_session_put_u64(&cursor, record->view.selection_end); memcpy(&bits, &record->view.scroll_x, sizeof(bits)); ide_session_put_u32(&cursor, bits);
                memcpy(&bits, &record->view.scroll_y, sizeof(bits)); ide_session_put_u32(&cursor, bits); memcpy(&bits, &record->view.zoom, sizeof(bits)); ide_session_put_u32(&cursor, bits);
                ide_session_put(&cursor, record->path.pointer, record->path.length); ide_session_put(&cursor, record->query.pointer, record->query.length);
                ide_session_put(&cursor, record->replacement.pointer, record->replacement.length);
            }
            ide_session_put(&cursor, active.pointer, active.length); ide_session_put(&cursor, filter.pointer, filter.length);
            String8 path = ide_session_path(scratch.arena, model->workspace.root_path);
            String8 directory = ide_path_parent(scratch.arena, path);
            bool missing = false, created = false;
            if (ide_path_component_status(scratch.arena, model->workspace.root_path, directory, &missing) == IDE_PATH_COMPONENT_MISSING && missing) os_make_directory(directory);
            if (ide_path_component_status(scratch.arena, model->workspace.root_path, directory, 0) == IDE_PATH_COMPONENT_SAFE && ide_path_kind(path) == IDE_PATH_MISSING)
            {
                created = ide_session_create(scratch.arena, path);
                success = created;
            }
            success = success && ide_path_kind(path) == IDE_PATH_REGULAR && ide_atomic_write_file(scratch.arena, path, (String8){.pointer = (char8*)output, .length = total});
            if (created && !success) os_file_delete(path);
        }
    }
    scratch_end(scratch);
    return success;
}

bool ide_document_model_session_load(IdeDocumentModel* model)
{
    if (!model || !model->initialized || !model->workspace.root_path.length) return false;
    Arena* conflicts[] = {model->active_arena, model->staging_arena};
    TemporalArena scratch = scratch_begin(conflicts, BUSTER_ARRAY_LENGTH(conflicts));
    String8 path = ide_session_path(scratch.arena, model->workspace.root_path);
    String8 directory = ide_path_parent(scratch.arena, path);
    bool missing = false;
    IdePathComponentStatus directory_status = ide_path_component_status(scratch.arena, model->workspace.root_path, directory, &missing);
    IdePathComponentStatus file_status = directory_status == IDE_PATH_COMPONENT_SAFE ? ide_path_component_status(scratch.arena, model->workspace.root_path, path, 0) : IDE_PATH_COMPONENT_UNSAFE;
    ByteSlice bytes = {0};
    if (directory_status == IDE_PATH_COMPONENT_SAFE && ide_path_kind(directory) == IDE_PATH_DIRECTORY && file_status == IDE_PATH_COMPONENT_SAFE &&
        ide_path_kind(path) == IDE_PATH_REGULAR)
    {
        bytes = ide_session_read(scratch.arena, path);
    }
    IdeSessionSnapshot snapshot = {0};
    bool success = ide_session_parse(scratch.arena, bytes, &snapshot);
    u32 index;
    for (index = 0; success && index < snapshot.count; index += 1)
    {
        success = ide_session_validate_path(scratch.arena, model->workspace.root_path, snapshot.records[index].path);
    }
    if (success && snapshot.active.length)
    {
        success = ide_session_validate_path(scratch.arena, model->workspace.root_path, snapshot.active);
    }
    if (!success) { scratch_end(scratch); return false; }
    ide_model_reset_staging(model);
    IdeDocumentWorkspace workspace = {0};
    success = ide_workspace_copy_to_arena(model->staging_arena, &workspace, &model->workspace);
    u8* restored = workspace.document_count ? arena_allocate(model->staging_arena, u8, workspace.document_count) : 0;
    success = success && (!workspace.document_count || restored);
    if (restored) memset(restored, 0, workspace.document_count);
    for (index = 0; success && index < workspace.document_count; index += 1) { workspace.documents[index].is_open = false; workspace.documents[index].open_order = 0; }
    u64 maximum = 0;
    for (index = 0; success && index < snapshot.count; index += 1)
    {
        IdeSessionRecord* record = snapshot.records + index;
        String8 canonical = ide_session_resolve(model->staging_arena, workspace.root_path, record->path);
        u32 document_index = canonical.length && ide_path_kind(canonical) == IDE_PATH_REGULAR
                                 ? ide_workspace_find_path(model->staging_arena, &workspace, canonical)
                                 : IDE_DOCUMENT_INDEX_INVALID;
        if (document_index == IDE_DOCUMENT_INDEX_INVALID) continue;
        if (restored[document_index]) { success = false; break; }
        IdeDocument* document = workspace.documents + document_index; restored[document_index] = 1; document->is_open = true; document->open_order = record->order;
        document->view = record->view; document->search.query = ide_string_copy(model->staging_arena, record->query); document->search.replacement = ide_string_copy(model->staging_arena, record->replacement);
        document->search.match_count = record->match_count; document->search.case_sensitive = (record->flags & 1u) != 0; document->search.whole_word = (record->flags & 2u) != 0; document->search.regular_expression = (record->flags & 4u) != 0;
        ide_document_clamp_view(document); maximum = BUSTER_MAX(maximum, record->order);
    }
    workspace.filter = (IdeDocumentWorkspaceFilterState){.query = ide_string_copy(model->staging_arena, snapshot.filter), .show_open_only = (snapshot.filter_flags & 1u) != 0,
                                                           .show_dirty_only = (snapshot.filter_flags & 2u) != 0, .show_diagnostics_only = (snapshot.filter_flags & 4u) != 0,
                                                           .case_sensitive = (snapshot.filter_flags & 8u) != 0};
    workspace.open_document_count = 0; workspace.active_document_index = IDE_DOCUMENT_INDEX_INVALID;
    for (index = 0; index < workspace.document_count; index += 1) workspace.open_document_count += workspace.documents[index].is_open;
    if (snapshot.active.length)
    {
        String8 canonical = ide_session_resolve(model->staging_arena, workspace.root_path, snapshot.active);
        index = canonical.length ? ide_workspace_find_path(model->staging_arena, &workspace, canonical) : IDE_DOCUMENT_INDEX_INVALID;
        if (index != IDE_DOCUMENT_INDEX_INVALID && workspace.documents[index].is_open) workspace.active_document_index = index;
    }
    if (workspace.active_document_index == IDE_DOCUMENT_INDEX_INVALID)
    {
        for (index = 0; index < workspace.document_count; index += 1)
            if (workspace.documents[index].is_open && (workspace.active_document_index == IDE_DOCUMENT_INDEX_INVALID || workspace.documents[index].open_order < workspace.documents[workspace.active_document_index].open_order)) workspace.active_document_index = index;
    }
    workspace.next_open_order = maximum < UINT64_MAX - 1 ? maximum + 1 : 1;
    if (success) ide_model_commit(model, workspace); else ide_model_reset_staging(model);
    scratch_end(scratch);
    return success;
}

IdeDocumentErrorKind ide_document_model_refresh_workspace(IdeDocumentModel* model)
{
    if (!model || !model->initialized)
    {
        return IDE_DOCUMENT_ERROR_INVALID_ARGUMENT;
    }
    ide_model_reset_staging(model);
    IdeDocumentWorkspace workspace = {0};
    IdeDocumentErrorKind error = ide_build_workspace(model, model->staging_arena, model->workspace.root_path, (String8){0},
                                                      &model->workspace, true,
                                                      model->max_discovered_files, model->max_traversal_entries, model->max_diagnostics, &workspace);
    if (error != IDE_DOCUMENT_ERROR_NONE)
    {
        ide_model_reset_staging(model);
        return error;
    }
    ide_model_commit(model, workspace);
    return IDE_DOCUMENT_ERROR_NONE;
}

BUSTER_GLOBAL_LOCAL IdeDocumentErrorKind ide_stage_open_path(IdeDocumentModel* model, String8 path, IdeDocumentWorkspace* result,
                                                             u32* result_index)
{
    if (!model || !model->initialized || !path.length || !path.pointer)
    {
        return IDE_DOCUMENT_ERROR_INVALID_ARGUMENT;
    }
    if (path.length > BUSTER_MAX_PATH_LENGTH)
    {
        return IDE_DOCUMENT_ERROR_INVALID_ARGUMENT;
    }
    if (!ide_document_path_is_bbb(path))
    {
        return IDE_DOCUMENT_ERROR_OPEN_PATH_NOT_SUPPORTED;
    }

    TemporalArena scratch = scratch_begin(0, 0);
    String8 candidate = model->workspace.root_path.length && !ide_path_is_absolute(path)
                            ? ide_path_resolve_for_root(scratch.arena, model->workspace.root_path, path)
                            : ide_string_copy(scratch.arena, path);
    String8 canonical = ide_document_path_canonical(scratch.arena, candidate);
    if (!canonical.length)
    {
        scratch_end(scratch);
        return IDE_DOCUMENT_ERROR_PATH_NOT_FOUND;
    }
    if (candidate.length > BUSTER_MAX_PATH_LENGTH || canonical.length > BUSTER_MAX_PATH_LENGTH)
    {
        scratch_end(scratch);
        return IDE_DOCUMENT_ERROR_INVALID_ARGUMENT;
    }

    // Path classification is deliberately completed before the dirty-workspace
    // replacement guard. A missing target must remain PATH_NOT_FOUND even when
    // another, non-active document is dirty.
    char8 candidate_storage[BUSTER_MAX_PATH_LENGTH + 1];
    char8 canonical_storage[BUSTER_MAX_PATH_LENGTH + 1];
    memcpy(candidate_storage, candidate.pointer, candidate.length);
    candidate_storage[candidate.length] = 0;
    memcpy(canonical_storage, canonical.pointer, canonical.length);
    canonical_storage[canonical.length] = 0;
    String8 stable_candidate = {.pointer = candidate_storage, .length = candidate.length};
    String8 stable_canonical = {.pointer = canonical_storage, .length = canonical.length};
    bool lexical_inside_root = model->workspace.root_path.length && ide_document_path_is_within(model->workspace.root_path, stable_candidate);
    bool canonical_inside_root = model->workspace.root_path.length && ide_document_path_is_within(model->workspace.root_path, stable_canonical);
    IdePathComponentStatus candidate_status = lexical_inside_root
                                                  ? ide_path_component_status(scratch.arena, model->workspace.root_path, stable_candidate, 0)
                                                  : IDE_PATH_COMPONENT_UNSAFE;
    IdePathComponentStatus canonical_status = canonical_inside_root
                                                   ? ide_path_component_status(scratch.arena, model->workspace.root_path, stable_canonical, 0)
                                                   : IDE_PATH_COMPONENT_UNSAFE;
    if (lexical_inside_root &&
        ((canonical_inside_root && canonical_status == IDE_PATH_COMPONENT_UNSAFE) ||
         (!canonical_inside_root && !ide_path_has_parent_component(stable_candidate) &&
          candidate_status == IDE_PATH_COMPONENT_UNSAFE)))
    {
        scratch_end(scratch);
        return IDE_DOCUMENT_ERROR_PATH_OUTSIDE_ROOT;
    }
    if (canonical_inside_root && canonical_status == IDE_PATH_COMPONENT_MISSING)
    {
        scratch_end(scratch);
        return IDE_DOCUMENT_ERROR_PATH_NOT_FOUND;
    }
    IdePathKind target_kind = ide_path_kind(stable_canonical);
    if (target_kind == IDE_PATH_MISSING)
    {
        scratch_end(scratch);
        return IDE_DOCUMENT_ERROR_PATH_NOT_FOUND;
    }
    if (target_kind != IDE_PATH_REGULAR)
    {
        scratch_end(scratch);
        return IDE_DOCUMENT_ERROR_NOT_REGULAR_FILE;
    }
    bool same_root = model->workspace.root_path.length && ide_document_path_is_within(model->workspace.root_path, stable_canonical) &&
                     canonical_status == IDE_PATH_COMPONENT_SAFE;

    if (!same_root && ide_workspace_has_dirty_documents(&model->workspace))
    {
        scratch_end(scratch);
        return IDE_DOCUMENT_ERROR_DIRTY_WORKSPACE_REPLACEMENT;
    }

    ide_model_reset_staging(model);
    // Keep the caller's path stable across scratch_end as well. A routed drop
    // normally supplies application-owned storage, but the model API must not
    // assume that every caller's relative String8 outlives this scratch.
    String8 build_path = ide_string_copy(model->staging_arena, path);
    if (!same_root && !ide_path_is_absolute(path))
    {
        build_path = ide_string_copy(model->staging_arena, stable_candidate);
    }
#if BUSTER_INCLUDE_TESTS
    if (ide_test_clobber_open_path_scratch && candidate.pointer && candidate.length)
    {
        // The test deliberately destroys the scratch candidate after root
        // comparison. build_path must already own the value needed below.
        memset(candidate.pointer, (int)'!', candidate.length);
    }
#endif
    scratch_end(scratch);
    IdeDocumentWorkspace workspace = {0};
    String8 root_input = same_root ? model->workspace.root_path : (String8){0};
    const IdeDocumentWorkspace* old_workspace = same_root ? &model->workspace : 0;
    IdeDocumentErrorKind error = ide_build_workspace(model, model->staging_arena, root_input, build_path, old_workspace, same_root,
                                                     model->max_discovered_files, model->max_traversal_entries, model->max_diagnostics, &workspace);
    if (error != IDE_DOCUMENT_ERROR_NONE)
    {
        ide_model_reset_staging(model);
        return error;
    }

    u32 index = ide_workspace_find_path(model->staging_arena, &workspace, build_path);
    if (index == IDE_DOCUMENT_INDEX_INVALID)
    {
        ide_model_reset_staging(model);
        return IDE_DOCUMENT_ERROR_DOCUMENT_NOT_FOUND;
    }
    IdeDocument* document = workspace.documents + index;
    if (!document->is_open)
    {
        document->is_open = true;
        document->open_order = workspace.next_open_order;
        workspace.next_open_order += 1;
        workspace.open_document_count += 1;
    }
    workspace.active_document_index = index;
    *result = workspace;
    if (result_index)
    {
        *result_index = index;
    }
    return IDE_DOCUMENT_ERROR_NONE;
}

IdeDocumentErrorKind ide_document_model_open_path(IdeDocumentModel* model, String8 path)
{
    IdeDocumentWorkspace workspace = {0};
    IdeDocumentErrorKind error = ide_stage_open_path(model, path, &workspace, 0);
    if (error != IDE_DOCUMENT_ERROR_NONE)
    {
        return error;
    }
    ide_model_commit(model, workspace);
    return IDE_DOCUMENT_ERROR_NONE;
}

IdeDocumentErrorKind ide_document_model_open(IdeDocumentModel* model, String8 path)
{
    if (!model || !model->initialized || !path.length)
    {
        return IDE_DOCUMENT_ERROR_INVALID_ARGUMENT;
    }
    if (!model->workspace.root_path.length)
    {
        return ide_document_model_open_path(model, path);
    }
    ide_model_reset_staging(model);
    IdeDocumentErrorKind path_error = ide_model_path_error(model->staging_arena, model->workspace.root_path, path);
    if (path_error != IDE_DOCUMENT_ERROR_NONE)
    {
        ide_model_reset_staging(model);
        return path_error;
    }
    u32 existing_index = ide_workspace_find_path(model->staging_arena, &model->workspace, path);
    bool needs_analysis_rebuild = existing_index != IDE_DOCUMENT_INDEX_INVALID &&
                                  !model->workspace.documents[existing_index].external_exists;
    ide_model_reset_staging(model);
    IdeDocumentWorkspace workspace = {0};
    ide_workspace_copy_to_arena_mode(model->staging_arena, &workspace, &model->workspace,
                                     needs_analysis_rebuild ? IDE_WORKSPACE_COPY_REBUILD_DERIVED : IDE_WORKSPACE_COPY_PRESERVE_DERIVED);
    u32 index = ide_workspace_find_path(model->staging_arena, &workspace, path);
    if (index == IDE_DOCUMENT_INDEX_INVALID)
    {
        ide_model_reset_staging(model);
        return IDE_DOCUMENT_ERROR_DOCUMENT_NOT_FOUND;
    }
    IdeDocument* document = workspace.documents + index;
    if (!document->external_exists && !document->dirty)
    {
        IdeLoadedFile loaded = {0};
        IdeDocumentErrorKind error = IDE_DOCUMENT_ERROR_NONE;
        if (!ide_file_read(model->staging_arena, workspace.root_path, document->path, &loaded, &error))
        {
            ide_model_reset_staging(model);
            return error;
        }
        document->source = loaded.source;
        document->saved_source = ide_string_copy(model->staging_arena, loaded.source);
        document->external_stats = loaded.stats;
        document->external_hash = loaded.hash;
        document->saved_hash = loaded.hash;
        document->external_exists = true;
        document->external_modified = false;
        document->revision += 1;
        document->saved_revision = document->revision;
        document->dirty = false;
        ide_document_clamp_view(document);
    }
    if (!document->is_open)
    {
        document->is_open = true;
        document->open_order = workspace.next_open_order;
        workspace.next_open_order += 1;
        workspace.open_document_count += 1;
    }
    workspace.active_document_index = index;
    if (needs_analysis_rebuild)
    {
        IdeDocumentErrorKind error = ide_rebuild_workspace_analysis(model, &workspace, model->max_diagnostics);
        if (error != IDE_DOCUMENT_ERROR_NONE)
        {
            ide_model_reset_staging(model);
            return error;
        }
    }
    ide_model_commit(model, workspace);
    return IDE_DOCUMENT_ERROR_NONE;
}

IdeDocumentErrorKind ide_document_model_close(IdeDocumentModel* model, String8 path)
{
    if (!model || !model->initialized || !path.length)
    {
        return IDE_DOCUMENT_ERROR_INVALID_ARGUMENT;
    }
    ide_model_reset_staging(model);
    IdeDocumentErrorKind path_error = ide_model_path_error(model->staging_arena, model->workspace.root_path, path);
    if (path_error != IDE_DOCUMENT_ERROR_NONE)
    {
        ide_model_reset_staging(model);
        return path_error;
    }
    IdeDocumentWorkspace workspace = {0};
    ide_workspace_copy_to_arena(model->staging_arena, &workspace, &model->workspace);
    u32 index = ide_workspace_find_path(model->staging_arena, &workspace, path);
    if (index == IDE_DOCUMENT_INDEX_INVALID)
    {
        ide_model_reset_staging(model);
        return IDE_DOCUMENT_ERROR_DOCUMENT_NOT_FOUND;
    }
    IdeDocument* document = workspace.documents + index;
    if (!document->is_open)
    {
        ide_model_reset_staging(model);
        return IDE_DOCUMENT_ERROR_DOCUMENT_NOT_OPEN;
    }
    u64 closed_order = document->open_order;
    bool was_active = workspace.active_document_index == index;
    document->is_open = false;
    workspace.open_document_count -= 1;
    if (was_active)
    {
        workspace.active_document_index = ide_active_previous_or_first(&workspace, closed_order);
    }
    ide_model_commit(model, workspace);
    return IDE_DOCUMENT_ERROR_NONE;
}

IdeDocumentErrorKind ide_document_model_set_active(IdeDocumentModel* model, String8 path)
{
    if (!model || !model->initialized || !path.length)
    {
        return IDE_DOCUMENT_ERROR_INVALID_ARGUMENT;
    }
    ide_model_reset_staging(model);
    IdeDocumentErrorKind path_error = ide_model_path_error(model->staging_arena, model->workspace.root_path, path);
    if (path_error != IDE_DOCUMENT_ERROR_NONE)
    {
        ide_model_reset_staging(model);
        return path_error;
    }
    IdeDocumentWorkspace workspace = {0};
    ide_workspace_copy_to_arena(model->staging_arena, &workspace, &model->workspace);
    u32 index = ide_workspace_find_path(model->staging_arena, &workspace, path);
    if (index == IDE_DOCUMENT_INDEX_INVALID)
    {
        ide_model_reset_staging(model);
        return IDE_DOCUMENT_ERROR_DOCUMENT_NOT_FOUND;
    }
    if (!workspace.documents[index].is_open)
    {
        ide_model_reset_staging(model);
        return IDE_DOCUMENT_ERROR_DOCUMENT_NOT_OPEN;
    }
    workspace.active_document_index = index;
    ide_model_commit(model, workspace);
    return IDE_DOCUMENT_ERROR_NONE;
}

IdeDocumentErrorKind ide_document_model_activate_source_range(IdeDocumentModel* model, String8 path, ParserSourceRange range)
{
    if (!model || !model->initialized || !path.length || !path.pointer)
    {
        return IDE_DOCUMENT_ERROR_INVALID_ARGUMENT;
    }
    if (path.length > BUSTER_MAX_PATH_LENGTH)
    {
        return IDE_DOCUMENT_ERROR_INVALID_ARGUMENT;
    }

    char8 source_path_storage[BUSTER_MAX_PATH_LENGTH + 1];
    memcpy(source_path_storage, path.pointer, path.length);
    source_path_storage[path.length] = 0;
    String8 source_path = {.pointer = source_path_storage, .length = path.length};
    // Stage the complete target before changing the committed model. This
    // refreshes clean files from disk and makes the range check use exactly
    // the source that would become active, rather than an older indexed
    // snapshot. No public mutation helper is called until every validation
    // below has succeeded.
    IdeDocumentWorkspace workspace = {0};
    u32 index = IDE_DOCUMENT_INDEX_INVALID;
    IdeDocumentErrorKind error = ide_stage_open_path(model, source_path, &workspace, &index);
    if (error != IDE_DOCUMENT_ERROR_NONE)
    {
        return error;
    }
    if (index == IDE_DOCUMENT_INDEX_INVALID || index >= workspace.document_count)
    {
        ide_model_reset_staging(model);
        return IDE_DOCUMENT_ERROR_DOCUMENT_NOT_FOUND;
    }
    IdeDocument* document = workspace.documents + index;
    if (range.offset > document->source.length || range.length > document->source.length - range.offset)
    {
        ide_model_reset_staging(model);
        return IDE_DOCUMENT_ERROR_INVALID_SELECTION;
    }
    document->view.cursor_offset = range.offset;
    document->view.selection_start = range.offset;
    document->view.selection_end = (u64)range.offset + range.length;
    ide_model_commit(model, workspace);
    return IDE_DOCUMENT_ERROR_NONE;
}

IdeDocumentErrorKind ide_document_model_activate_entity(IdeDocumentModel* model, u32 index)
{
    if (!model || !model->initialized)
    {
        return IDE_DOCUMENT_ERROR_INVALID_ARGUMENT;
    }
    if (index >= model->workspace.entity_count || !model->workspace.entities)
    {
        return IDE_DOCUMENT_ERROR_DOCUMENT_NOT_FOUND;
    }

    // Copy the snapshot values before the shared operation can acquire scratch
    // or commit a new arena. The shared operation copies the path again into a
    // stable local buffer before calling any other public model helper.
    String8 snapshot_path = model->workspace.entities[index].source_path;
    ParserSourceRange snapshot_range = model->workspace.entities[index].range;
    if (snapshot_path.length > BUSTER_MAX_PATH_LENGTH ||
        (snapshot_path.length && !snapshot_path.pointer))
    {
        return IDE_DOCUMENT_ERROR_INVALID_ARGUMENT;
    }
    char8 source_path_storage[BUSTER_MAX_PATH_LENGTH + 1];
    if (snapshot_path.length)
    {
        memcpy(source_path_storage, snapshot_path.pointer, snapshot_path.length);
    }
    source_path_storage[snapshot_path.length] = 0;
    String8 source_path = {.pointer = source_path_storage, .length = snapshot_path.length};
    return ide_document_model_activate_source_range(model, source_path, snapshot_range);
}

BUSTER_GLOBAL_LOCAL bool ide_module_fingerprint_equal(IdeSyntaxRevision* revision, IdeModuleSnapshot* snapshot)
{
    return revision && snapshot && revision->interface_hash == snapshot->interface_hash &&
           revision->interface_bytes.length == snapshot->interface_bytes.length &&
           string_equal(revision->interface_bytes, snapshot->interface_bytes);
}

BUSTER_GLOBAL_LOCAL bool ide_module_bindings_equal(const IdeDocumentWorkspace* left, const IdeDocumentWorkspace* right, u32 module_index)
{
    if (!left->graph || !right->graph || module_index >= left->document_count || module_index >= right->document_count)
    {
        return false;
    }
    u32 left_first = left->graph->import_offsets[module_index];
    u32 left_last = left->graph->import_offsets[module_index + 1];
    u32 right_first = right->graph->import_offsets[module_index];
    u32 right_last = right->graph->import_offsets[module_index + 1];
    if (left_last - left_first != right_last - right_first)
    {
        return false;
    }
    for (u32 offset = 0; offset < left_last - left_first; offset += 1)
    {
        u32 left_index = left_first + offset;
        u32 right_index = right_first + offset;
        if (left->graph->binding_targets[left_index] != right->graph->binding_targets[right_index] ||
            left->graph->binding_states[left_index] != right->graph->binding_states[right_index])
        {
            return false;
        }
    }
    return true;
}

BUSTER_GLOBAL_LOCAL void ide_graph_visible_set(IdeDocumentGraph* graph, u32 document_count, u32 root, bool* visible, u32* stack)
{
    memset(visible, 0, sizeof(bool) * document_count);
    u32 stack_count = 1;
    stack[0] = root;
    visible[root] = true;
    while (stack_count)
    {
        u32 module = stack[stack_count - 1];
        stack_count -= 1;
        for (u32 edge = graph->forward_offsets[module]; edge < graph->forward_offsets[module + 1]; edge += 1)
        {
            u32 target = graph->forward_edges[edge];
            if (!visible[target])
            {
                visible[target] = true;
                stack[stack_count] = target;
                stack_count += 1;
            }
        }
    }
}

BUSTER_GLOBAL_LOCAL void ide_reverse_cone_add(IdeDocumentGraph* graph, u32 document_count, u32 module, bool* affected, u32* queue, u32* queue_count)
{
    if (!graph)
    {
        return;
    }
    for (u32 edge = graph->reverse_offsets[module]; edge < graph->reverse_offsets[module + 1]; edge += 1)
    {
        u32 dependent = graph->reverse_edges[edge];
        if (!affected[dependent])
        {
            affected[dependent] = true;
            BUSTER_CHECK(*queue_count < document_count);
            queue[*queue_count] = dependent;
            *queue_count += 1;
        }
    }
}

BUSTER_GLOBAL_LOCAL bool ide_workspace_has_generic_syntax(const IdeDocumentWorkspace* workspace)
{
    for (u32 index = 0; index < workspace->document_count; index += 1)
    {
        IdeSyntaxRevision* revision = workspace->revision_states[index].current;
        if (revision && revision->has_generic_syntax)
        {
            return true;
        }
    }
    return false;
}

BUSTER_GLOBAL_LOCAL IdeDocumentErrorKind ide_analysis_incremental(IdeDocumentModel* model, IdeDocumentWorkspace* workspace,
                                                                  const IdeDocumentWorkspace* old_workspace, const bool* affected,
                                                                  bool* introduced_generic)
{
    u32 document_count = workspace->document_count;
    *introduced_generic = false;
    u32 affected_count = 0;
    for (u32 index = 0; index < document_count; index += 1)
    {
        affected_count += affected[index];
    }
    BUSTER_CHECK(affected_count);
    IdeAnalysisBatch* batch = ide_analysis_batch_create(model, affected_count);
    if (!batch)
    {
        return IDE_DOCUMENT_ERROR_FILE_READ;
    }
    ide_analysis_stats_set_snapshot_count(workspace, batch->snapshot_count);
    Arena* arena = batch->owner.arena;
    AnalysisResult** analyses = document_count ? arena_allocate(model->staging_arena, AnalysisResult*, document_count) : 0;
    bool* visible = document_count ? arena_allocate(model->staging_arena, bool, document_count) : 0;
    bool* referenced_old = document_count ? arena_allocate(model->staging_arena, bool, document_count) : 0;
    u32* stack = document_count ? arena_allocate(model->staging_arena, u32, document_count) : 0;
    // A document contributes either its affected syntax owner or its retained
    // module owner, never both, so the exact upper bound is document_count.
    IdeDocumentStorageOwner** dependencies = document_count ? arena_allocate(model->staging_arena, IdeDocumentStorageOwner*, document_count) : 0;
    u32 dependency_count = 0;
    if (document_count)
    {
        memset(referenced_old, 0, sizeof(bool) * document_count);
    }
    for (u32 index = 0; index < document_count; index += 1)
    {
        if (!affected[index])
        {
            analyses[index] = old_workspace->revision_states[index].module->analysis;
            continue;
        }
        IdeSyntaxRevision* revision = workspace->revision_states[index].current;
        AnalysisSourceInput input = {
            .path = workspace->documents[index].path,
            .parser = revision->analysis_eligible ? &revision->parser : 0,
        };
        analyses[index] = arena_allocate(arena, AnalysisResult, 1);
        *analyses[index] = analysis_index_module(arena, (AnalysisModuleId){.value = index}, workspace->documents[index].module_name, &input, 1);
        BUSTER_CHECK(dependency_count < document_count);
        dependencies[dependency_count] = revision->owner;
        dependency_count += 1;
        ide_analysis_stats_mark(workspace, index, IDE_DOCUMENT_ANALYSIS_WORK_INDEXED);

        ide_graph_visible_set(workspace->graph, document_count, index, visible, stack);
        for (u32 visible_index = 0; visible_index < document_count; visible_index += 1)
        {
            referenced_old[visible_index] |= visible[visible_index] && !affected[visible_index];
        }
    }
    for (u32 index = 0; index < document_count; index += 1)
    {
        if (referenced_old[index])
        {
            BUSTER_CHECK(dependency_count < document_count);
            dependencies[dependency_count] = old_workspace->revision_states[index].module->owner;
            dependency_count += 1;
        }
    }
    BUSTER_CHECK(dependency_count <= document_count);
    ide_owner_dependencies_set(&batch->owner, dependencies, dependency_count);

    for (u32 index = 0; index < document_count; index += 1)
    {
        if (!affected[index])
        {
            continue;
        }
        ide_graph_visible_set(workspace->graph, document_count, index, visible, stack);
        u32 visible_count = 0;
        for (u32 candidate = 0; candidate < document_count; candidate += 1)
        {
            visible_count += visible[candidate];
        }
        AnalysisResult** visible_modules = visible_count ? arena_allocate(model->staging_arena, AnalysisResult*, visible_count) : 0;
        u32 visible_index = 0;
        for (u32 candidate = 0; candidate < document_count; candidate += 1)
        {
            if (visible[candidate])
            {
                visible_modules[visible_index] = analyses[candidate];
                visible_index += 1;
            }
        }
        BUSTER_CHECK(visible_index == visible_count);

        ide_analysis_bind_graph_imports(arena, model->staging_arena, workspace, analyses, index, visible_modules, visible_count);
    }

    u32 snapshot_cursor = 0;
    for (u32 order_index = 0; order_index < document_count; order_index += 1)
    {
        u32 index = workspace->graph->dependency_order[order_index];
        if (affected[index])
        {
            analysis_resolve_module_interfaces(arena, analyses[index]);
        }
    }
    for (u32 index = 0; index < document_count; index += 1)
    {
        if (affected[index] && ide_analysis_result_has_generic(analyses[index]))
        {
            *introduced_generic = true;
            ide_model_unstage_owner(model, &batch->owner);
            return IDE_DOCUMENT_ERROR_NONE;
        }
    }
    for (u32 order_index = 0; order_index < document_count; order_index += 1)
    {
        u32 index = workspace->graph->dependency_order[order_index];
        if (!affected[index])
        {
            continue;
        }
        if (workspace->revision_states[index].current->analysis_eligible)
        {
            analysis_analyze_bodies(arena, analyses[index]);
        }
        ide_analysis_stats_mark(workspace, index, IDE_DOCUMENT_ANALYSIS_WORK_ANALYZED);
        BUSTER_CHECK(snapshot_cursor < batch->snapshot_count);
        IdeModuleSnapshot* snapshot = batch->snapshots + snapshot_cursor;
        snapshot_cursor += 1;
        snapshot->owner = &batch->owner;
        IdeDocumentErrorKind error = ide_snapshot_build(arena, workspace, index, analyses[index], snapshot);
        if (error != IDE_DOCUMENT_ERROR_NONE)
        {
            return error;
        }
        workspace->revision_states[index].module = snapshot;
    }
    BUSTER_CHECK(snapshot_cursor == batch->snapshot_count);
    workspace->analysis_contains_generics = false;
    return IDE_DOCUMENT_ERROR_NONE;
}

IdeDocumentErrorKind ide_document_model_set_text(IdeDocumentModel* model, String8 path, String8 source)
{
    if (!model || !model->initialized || (!source.pointer && source.length))
    {
        return IDE_DOCUMENT_ERROR_INVALID_ARGUMENT;
    }
    TemporalArena scratch = scratch_begin(0, 0);
    IdeDocumentErrorKind path_error = ide_model_path_error(scratch.arena, model->workspace.root_path, path);
    if (path_error != IDE_DOCUMENT_ERROR_NONE)
    {
        scratch_end(scratch);
        return path_error;
    }
    u32 existing_index = ide_workspace_find_path(scratch.arena, &model->workspace, path);
    if (existing_index == IDE_DOCUMENT_INDEX_INVALID)
    {
        scratch_end(scratch);
        return IDE_DOCUMENT_ERROR_DOCUMENT_NOT_FOUND;
    }
    IdeDocument* existing = model->workspace.documents + existing_index;
    if (!existing->is_open)
    {
        scratch_end(scratch);
        return IDE_DOCUMENT_ERROR_DOCUMENT_NOT_OPEN;
    }
    if (string_equal(existing->source, source))
    {
        scratch_end(scratch);
        return IDE_DOCUMENT_ERROR_NONE;
    }
    scratch_end(scratch);

    ide_model_reset_staging(model);
    IdeDocumentWorkspace workspace = {0};
    ide_workspace_copy_to_arena(model->staging_arena, &workspace, &model->workspace);
    u32 index = ide_workspace_find_path(model->staging_arena, &workspace, path);
    if (index == IDE_DOCUMENT_INDEX_INVALID || index >= workspace.document_count)
    {
        ide_model_reset_staging(model);
        return IDE_DOCUMENT_ERROR_DOCUMENT_NOT_FOUND;
    }
    IdeDocument* document = workspace.documents + index;
    IdeSyntaxBatch* syntax_batch = ide_syntax_batch_create(model);
    if (!syntax_batch)
    {
        ide_model_reset_staging(model);
        return IDE_DOCUMENT_ERROR_FILE_READ;
    }
    IdeSyntaxRevision* revision = ide_syntax_revision_create(syntax_batch, model->expression_arena, document->module_name, source);
    if (!revision)
    {
        ide_model_reset_staging(model);
        return IDE_DOCUMENT_ERROR_FILE_READ;
    }
    workspace.revision_states[index].current = revision;
    document->source = revision->source;
    document->revision += 1;
    document->dirty = !string_equal(document->source, document->saved_source);
    ide_document_clamp_view(document);
    ide_analysis_stats_begin(model->staging_arena, &workspace);
    ide_analysis_stats_mark(&workspace, index, IDE_DOCUMENT_ANALYSIS_WORK_PARSED);

    IdeDocumentErrorKind error = ide_graph_build(model->staging_arena, &workspace);
    if (error != IDE_DOCUMENT_ERROR_NONE)
    {
        ide_model_reset_staging(model);
        return error;
    }
    bool* affected = arena_allocate(model->staging_arena, bool, workspace.document_count);
    u32* queue = arena_allocate(model->staging_arena, u32, workspace.document_count);
    memset(affected, 0, sizeof(bool) * workspace.document_count);
    affected[index] = true;
    bool interface_equal = ide_module_fingerprint_equal(revision, model->workspace.revision_states[index].module);
    bool bindings_equal = ide_module_bindings_equal(&model->workspace, &workspace, index);
    u32 queue_count = 1;
    u32 queue_cursor = 0;
    queue[0] = index;
    if (!interface_equal || !bindings_equal)
    {
        while (queue_cursor < queue_count)
        {
            u32 module = queue[queue_cursor];
            queue_cursor += 1;
            ide_reverse_cone_add(model->workspace.graph, workspace.document_count, module, affected, queue, &queue_count);
            ide_reverse_cone_add(workspace.graph, workspace.document_count, module, affected, queue, &queue_count);
        }
    }

    bool full_fallback = model->workspace.analysis_contains_generics || model->workspace.analysis_has_cycles ||
                         workspace.analysis_has_cycles || ide_workspace_has_generic_syntax(&workspace);
    if (full_fallback)
    {
        ide_analysis_stats_begin(model->staging_arena, &workspace);
        ide_analysis_stats_mark(&workspace, index, IDE_DOCUMENT_ANALYSIS_WORK_PARSED);
        for (u32 module = 0; module < workspace.document_count; module += 1)
        {
            ide_document_compile_mark_stale(workspace.documents + module);
        }
        error = ide_analysis_full_from_cached(model, &workspace, true);
    }
    else
    {
        for (u32 module = 0; module < workspace.document_count; module += 1)
        {
            if (affected[module])
            {
                ide_analysis_stats_mark(&workspace, module, IDE_DOCUMENT_ANALYSIS_WORK_INVALIDATED);
                ide_document_compile_mark_stale(workspace.documents + module);
            }
        }
        bool introduced_generic = false;
        error = ide_analysis_incremental(model, &workspace, &model->workspace, affected, &introduced_generic);
        if (error == IDE_DOCUMENT_ERROR_NONE && introduced_generic)
        {
            ide_analysis_stats_begin(model->staging_arena, &workspace);
            ide_analysis_stats_mark(&workspace, index, IDE_DOCUMENT_ANALYSIS_WORK_PARSED);
            for (u32 module = 0; module < workspace.document_count; module += 1)
            {
                ide_document_compile_mark_stale(workspace.documents + module);
            }
            error = ide_analysis_full_from_cached(model, &workspace, true);
        }
    }
    if (error == IDE_DOCUMENT_ERROR_NONE)
    {
        error = ide_workspace_flatten_snapshots(model->staging_arena, &workspace, model->max_diagnostics);
    }
    if (error != IDE_DOCUMENT_ERROR_NONE)
    {
        ide_model_reset_staging(model);
        return error;
    }
    workspace.analysis_generation = model->workspace.analysis_generation + 1;
    ide_model_commit(model, workspace);
    return IDE_DOCUMENT_ERROR_NONE;
}

IdeDocumentErrorKind ide_document_model_reload(IdeDocumentModel* model, String8 path, IdeDocumentReloadMode mode)
{
    if (!model || !model->initialized || !path.length || mode >= IDE_DOCUMENT_RELOAD_MODE_COUNT)
    {
        return IDE_DOCUMENT_ERROR_INVALID_ARGUMENT;
    }
    ide_model_reset_staging(model);
    IdeDocumentErrorKind path_error = path.length ? ide_model_path_error(model->staging_arena, model->workspace.root_path, path) : IDE_DOCUMENT_ERROR_NONE;
    if (path_error != IDE_DOCUMENT_ERROR_NONE)
    {
        ide_model_reset_staging(model);
        return path_error;
    }
    IdeDocumentWorkspace workspace = {0};
    ide_workspace_copy_to_arena_mode(model->staging_arena, &workspace, &model->workspace, IDE_WORKSPACE_COPY_REBUILD_DERIVED);
    u32 index = ide_workspace_find_path(model->staging_arena, &workspace, path);
    if (index == IDE_DOCUMENT_INDEX_INVALID)
    {
        ide_model_reset_staging(model);
        return IDE_DOCUMENT_ERROR_DOCUMENT_NOT_FOUND;
    }
    IdeDocument* document = workspace.documents + index;
    IdeLoadedFile loaded = {0};
    IdeDocumentErrorKind error = IDE_DOCUMENT_ERROR_NONE;
    if (!ide_file_read(model->staging_arena, workspace.root_path, document->path, &loaded, &error))
    {
        ide_model_reset_staging(model);
        return error;
    }
    if (document->dirty && mode == IDE_DOCUMENT_RELOAD_REJECT_DIRTY)
    {
        ide_model_reset_staging(model);
        return IDE_DOCUMENT_ERROR_DIRTY_RELOAD_CONFLICT;
    }
    document->source = loaded.source;
    document->external_stats = loaded.stats;
    document->external_hash = loaded.hash;
    document->saved_source = ide_string_copy(model->staging_arena, loaded.source);
    document->saved_hash = loaded.hash;
    document->external_exists = true;
    document->external_modified = false;
    document->revision += 1;
    document->saved_revision = document->revision;
    document->dirty = false;
    ide_document_clamp_view(document);
    ide_document_compile_mark_stale(document);
    error = ide_rebuild_workspace_analysis(model, &workspace, model->max_diagnostics);
    if (error != IDE_DOCUMENT_ERROR_NONE)
    {
        ide_model_reset_staging(model);
        return error;
    }
    ide_model_commit(model, workspace);
    return IDE_DOCUMENT_ERROR_NONE;
}

IdeDocumentErrorKind ide_document_model_poll_external(IdeDocumentModel* model, String8 path)
{
    if (!model || !model->initialized)
    {
        return IDE_DOCUMENT_ERROR_INVALID_ARGUMENT;
    }
    ide_model_reset_staging(model);
    IdeDocumentErrorKind path_error = path.length ? ide_model_path_error(model->staging_arena, model->workspace.root_path, path) : IDE_DOCUMENT_ERROR_NONE;
    if (path_error != IDE_DOCUMENT_ERROR_NONE)
    {
        ide_model_reset_staging(model);
        return path_error;
    }
    IdeDocumentWorkspace workspace = {0};
    ide_workspace_copy_to_arena(model->staging_arena, &workspace, &model->workspace);
    u32 first = 0;
    u32 last = workspace.document_count;
    if (path.length)
    {
        first = ide_workspace_find_path(model->staging_arena, &workspace, path);
        if (first == IDE_DOCUMENT_INDEX_INVALID)
        {
            ide_model_reset_staging(model);
            return IDE_DOCUMENT_ERROR_DOCUMENT_NOT_FOUND;
        }
        last = first + 1;
    }
    for (u32 index = first; index < last; index += 1)
    {
        IdeDocument* document = workspace.documents + index;
        IdeLoadedFile loaded = {0};
        IdeDocumentErrorKind error = IDE_DOCUMENT_ERROR_NONE;
        if (!ide_file_read(model->staging_arena, workspace.root_path, document->path, &loaded, &error))
        {
            if (error == IDE_DOCUMENT_ERROR_PATH_NOT_FOUND)
            {
                document->external_exists = false;
                document->external_modified = true;
                continue;
            }
            ide_model_reset_staging(model);
            return error;
        }
        document->external_exists = true;
        document->external_stats = loaded.stats;
        document->external_hash = loaded.hash;
        document->external_modified = !string_equal(loaded.source, document->saved_source);
    }
    ide_model_commit(model, workspace);
    return IDE_DOCUMENT_ERROR_NONE;
}

IdeDocumentErrorKind ide_document_model_save(IdeDocumentModel* model, String8 path)
{
    if (!model || !model->initialized || !path.length)
    {
        return IDE_DOCUMENT_ERROR_INVALID_ARGUMENT;
    }
    TemporalArena scratch = scratch_begin(0, 0);
    IdeDocumentErrorKind path_error = ide_model_path_error(scratch.arena, model->workspace.root_path, path);
    if (path_error != IDE_DOCUMENT_ERROR_NONE)
    {
        scratch_end(scratch);
        return path_error;
    }
    u32 index = ide_workspace_find_path(scratch.arena, &model->workspace, path);
    if (index == IDE_DOCUMENT_INDEX_INVALID)
    {
        scratch_end(scratch);
        return IDE_DOCUMENT_ERROR_DOCUMENT_NOT_FOUND;
    }
    IdeDocument* current = model->workspace.documents + index;
    if (!current->is_open)
    {
        scratch_end(scratch);
        return IDE_DOCUMENT_ERROR_DOCUMENT_NOT_OPEN;
    }
    if (!ide_document_path_is_within(model->workspace.root_path, current->path) ||
        ide_path_component_status(scratch.arena, model->workspace.root_path, current->path, 0) == IDE_PATH_COMPONENT_UNSAFE)
    {
        scratch_end(scratch);
        return IDE_DOCUMENT_ERROR_PATH_OUTSIDE_ROOT;
    }

    IdeLoadedFile current_disk = {0};
    IdeDocumentErrorKind disk_error = IDE_DOCUMENT_ERROR_NONE;
    if (!ide_file_read(scratch.arena, model->workspace.root_path, current->path, &current_disk, &disk_error))
    {
        scratch_end(scratch);
        return disk_error == IDE_DOCUMENT_ERROR_PATH_NOT_FOUND ? IDE_DOCUMENT_ERROR_EXTERNAL_MODIFICATION_CONFLICT : disk_error;
    }
    if (!ide_loaded_file_matches(current_disk.source, current_disk.hash, current->saved_source, current->saved_hash))
    {
        scratch_end(scratch);
        return IDE_DOCUMENT_ERROR_EXTERNAL_MODIFICATION_CONFLICT;
    }

    bool write_result = ide_atomic_write_file(scratch.arena, current->path, current->source);
    scratch_end(scratch);
    if (!write_result)
    {
        return IDE_DOCUMENT_ERROR_FILE_WRITE;
    }

    ide_model_reset_staging(model);
    IdeDocumentWorkspace workspace = {0};
    ide_workspace_copy_to_arena(model->staging_arena, &workspace, &model->workspace);
    IdeDocument* document = workspace.documents + index;
    IdeLoadedFile loaded = {0};
    IdeDocumentErrorKind error = IDE_DOCUMENT_ERROR_NONE;
    if (!ide_file_read(model->staging_arena, workspace.root_path, document->path, &loaded, &error))
    {
        ide_model_reset_staging(model);
        return error;
    }
    u64 source_hash = buster_hash_64((u8*)document->source.pointer, document->source.length);
    if (!ide_loaded_file_matches(loaded.source, loaded.hash, document->source, source_hash))
    {
        ide_model_reset_staging(model);
        return IDE_DOCUMENT_ERROR_EXTERNAL_MODIFICATION_CONFLICT;
    }
    document->external_stats = loaded.stats;
    document->external_hash = loaded.hash;
    document->external_exists = true;
    document->external_modified = false;
    BUSTER_CHECK(workspace.revision_states && workspace.revision_states[index].current);
    workspace.revision_states[index].saved = workspace.revision_states[index].current;
    document->saved_source = workspace.revision_states[index].saved->source;
    document->saved_hash = source_hash;
    document->saved_revision = document->revision;
    document->dirty = false;
    ide_model_commit(model, workspace);
    return IDE_DOCUMENT_ERROR_NONE;
}

#if BUSTER_INCLUDE_TESTS
void ide_document_model_test_set_save_replace_failure(bool enabled)
{
    ide_test_force_save_replace_failure = enabled;
}

void ide_document_model_test_set_open_path_scratch_clobber(bool enabled)
{
    ide_test_clobber_open_path_scratch = enabled;
}
#endif

IdeDocumentErrorKind ide_document_model_set_view(IdeDocumentModel* model, String8 path, IdeDocumentViewState view)
{
    if (!model || !model->initialized || !path.length || view.selection_start > view.selection_end)
    {
        return view.selection_start > view.selection_end ? IDE_DOCUMENT_ERROR_INVALID_SELECTION : IDE_DOCUMENT_ERROR_INVALID_ARGUMENT;
    }
    ide_model_reset_staging(model);
    IdeDocumentWorkspace workspace = {0};
    ide_workspace_copy_to_arena(model->staging_arena, &workspace, &model->workspace);
    u32 index = ide_workspace_find_path(model->staging_arena, &workspace, path);
    if (index == IDE_DOCUMENT_INDEX_INVALID)
    {
        ide_model_reset_staging(model);
        return IDE_DOCUMENT_ERROR_DOCUMENT_NOT_FOUND;
    }
    IdeDocument* document = workspace.documents + index;
    if (!document->is_open)
    {
        ide_model_reset_staging(model);
        return IDE_DOCUMENT_ERROR_DOCUMENT_NOT_OPEN;
    }
    if (view.cursor_offset > document->source.length || view.selection_end > document->source.length)
    {
        ide_model_reset_staging(model);
        return IDE_DOCUMENT_ERROR_INVALID_SELECTION;
    }
    document->view = view;
    ide_model_commit(model, workspace);
    return IDE_DOCUMENT_ERROR_NONE;
}

IdeDocumentErrorKind ide_document_model_set_compile_metadata(IdeDocumentModel* model, String8 path, IdeDocumentCompileMetadata metadata)
{
    if (!model || !model->initialized || !path.length || metadata.status >= IDE_DOCUMENT_COMPILE_STATUS_COUNT)
    {
        return IDE_DOCUMENT_ERROR_INVALID_ARGUMENT;
    }
    ide_model_reset_staging(model);
    IdeDocumentWorkspace workspace = {0};
    ide_workspace_copy_to_arena(model->staging_arena, &workspace, &model->workspace);
    u32 index = ide_workspace_find_path(model->staging_arena, &workspace, path);
    if (index == IDE_DOCUMENT_INDEX_INVALID)
    {
        ide_model_reset_staging(model);
        return IDE_DOCUMENT_ERROR_DOCUMENT_NOT_FOUND;
    }
    IdeDocumentCompileMetadata* destination = &workspace.documents[index].compile;
    destination->status = metadata.status;
    destination->compiled_revision = metadata.compiled_revision;
    destination->artifact_hash = metadata.artifact_hash;
    destination->artifact_path = ide_string_copy(model->staging_arena, metadata.artifact_path);
    destination->command_line = ide_string_copy(model->staging_arena, metadata.command_line);
    destination->message = ide_string_copy(model->staging_arena, metadata.message);
    ide_model_commit(model, workspace);
    return IDE_DOCUMENT_ERROR_NONE;
}

IdeDocumentErrorKind ide_document_model_set_search_state(IdeDocumentModel* model, String8 path, IdeDocumentSearchState search)
{
    if (!model || !model->initialized || !path.length)
    {
        return IDE_DOCUMENT_ERROR_INVALID_ARGUMENT;
    }
    ide_model_reset_staging(model);
    IdeDocumentWorkspace workspace = {0};
    ide_workspace_copy_to_arena(model->staging_arena, &workspace, &model->workspace);
    u32 index = ide_workspace_find_path(model->staging_arena, &workspace, path);
    if (index == IDE_DOCUMENT_INDEX_INVALID)
    {
        ide_model_reset_staging(model);
        return IDE_DOCUMENT_ERROR_DOCUMENT_NOT_FOUND;
    }
    IdeDocumentSearchState* destination = &workspace.documents[index].search;
    *destination = search;
    destination->query = ide_string_copy(model->staging_arena, search.query);
    destination->replacement = ide_string_copy(model->staging_arena, search.replacement);
    ide_model_commit(model, workspace);
    return IDE_DOCUMENT_ERROR_NONE;
}

IdeDocumentErrorKind ide_document_model_set_filter_state(IdeDocumentModel* model, IdeDocumentWorkspaceFilterState filter)
{
    if (!model || !model->initialized)
    {
        return IDE_DOCUMENT_ERROR_INVALID_ARGUMENT;
    }
    if (filter.whole_word || filter.regular_expression)
    {
        return IDE_DOCUMENT_ERROR_UNSUPPORTED_FILTER;
    }
    ide_model_reset_staging(model);
    IdeDocumentWorkspace workspace = {0};
    ide_workspace_copy_to_arena(model->staging_arena, &workspace, &model->workspace);
    workspace.filter = filter;
    workspace.filter.query = ide_string_copy(model->staging_arena, filter.query);
    ide_model_commit(model, workspace);
    return IDE_DOCUMENT_ERROR_NONE;
}

IdeDocumentErrorKind ide_document_model_replace_diagnostics(IdeDocumentModel* model, const IdeDocumentDiagnosticInput* inputs, u32 input_count)
{
    if (!model || !model->initialized || (input_count && !inputs))
    {
        return IDE_DOCUMENT_ERROR_INVALID_ARGUMENT;
    }
    if (input_count > model->max_diagnostics)
    {
        return IDE_DOCUMENT_ERROR_DIAGNOSTIC_LIMIT;
    }
    ide_model_reset_staging(model);
    IdeDocumentWorkspace workspace = {0};
    ide_workspace_copy_to_arena(model->staging_arena, &workspace, &model->workspace);
    u32* counts = workspace.document_count ? arena_allocate(model->staging_arena, u32, workspace.document_count) : 0;
    if (workspace.document_count)
    {
        if (!counts)
        {
            ide_model_reset_staging(model);
            return IDE_DOCUMENT_ERROR_FILE_READ;
        }
        memset(counts, 0, sizeof(*counts) * workspace.document_count);
    }
    for (u32 input_index = 0; input_index < input_count; input_index += 1)
    {
        const IdeDocumentDiagnosticInput* input = inputs + input_index;
        if (input->severity >= IDE_DOCUMENT_DIAGNOSTIC_SEVERITY_COUNT || input->source >= IDE_DOCUMENT_DIAGNOSTIC_SOURCE_COUNT)
        {
            ide_model_reset_staging(model);
            return IDE_DOCUMENT_ERROR_INVALID_ARGUMENT;
        }
        u32 document_index = workspace.active_document_index;
        if (input->file_path.length)
        {
            document_index = ide_workspace_find_path(model->staging_arena, &workspace, input->file_path);
        }
        if (document_index == IDE_DOCUMENT_INDEX_INVALID)
        {
            ide_model_reset_staging(model);
            return input->file_path.length ? IDE_DOCUMENT_ERROR_DOCUMENT_NOT_FOUND : IDE_DOCUMENT_ERROR_ACTIVE_DOCUMENT_REQUIRED;
        }
        if (!counts)
        {
            ide_model_reset_staging(model);
            return IDE_DOCUMENT_ERROR_FILE_READ;
        }
        if (document_index >= workspace.document_count)
        {
            ide_model_reset_staging(model);
            return IDE_DOCUMENT_ERROR_DOCUMENT_NOT_FOUND;
        }
        counts[document_index] += 1;
    }
    for (u32 index = 0; index < workspace.document_count; index += 1)
    {
        workspace.documents[index].diagnostics = counts[index] ? arena_allocate(model->staging_arena, IdeDocumentDiagnostic, counts[index]) : 0;
        workspace.documents[index].diagnostic_count = counts[index];
        counts[index] = 0;
    }
    for (u32 input_index = 0; input_index < input_count; input_index += 1)
    {
        const IdeDocumentDiagnosticInput* input = inputs + input_index;
        u32 document_index = workspace.active_document_index;
        if (input->file_path.length)
        {
            document_index = ide_workspace_find_path(model->staging_arena, &workspace, input->file_path);
        }
        IdeDocument* document = workspace.documents + document_index;
        IdeDocumentDiagnostic* diagnostic = document->diagnostics + counts[document_index];
        *diagnostic = (IdeDocumentDiagnostic){
            .file_path = document->path,
            .range = input->range,
            .message = ide_string_copy(model->staging_arena, input->message),
            .identity = input->identity,
            .severity = input->severity,
            .source = input->source,
        };
        if (!diagnostic->identity)
        {
            diagnostic->identity = ide_diagnostic_identity(diagnostic->file_path, diagnostic->range, diagnostic->message, diagnostic->severity,
                                                           diagnostic->source);
        }
        counts[document_index] += 1;
    }
    for (u32 index = 0; index < workspace.document_count; index += 1)
    {
        ide_diagnostics_sort(workspace.documents + index);
    }
    ide_model_commit(model, workspace);
    return IDE_DOCUMENT_ERROR_NONE;
}

bool ide_document_model_document_matches_filter(IdeDocumentModel* model, u32 index)
{
    if (!model || !model->initialized || index >= model->workspace.document_count)
    {
        return false;
    }
    IdeDocument* document = model->workspace.documents + index;
    IdeDocumentWorkspaceFilterState filter = model->workspace.filter;
    if (filter.show_open_only && !document->is_open)
    {
        return false;
    }
    if (filter.show_dirty_only && !document->dirty)
    {
        return false;
    }
    if (filter.show_diagnostics_only && !document->diagnostic_count)
    {
        return false;
    }
    if (!filter.query.length)
    {
        return true;
    }
    if (ide_document_string_contains(document->path, filter.query, filter.case_sensitive) ||
        ide_document_string_contains(document->module_name, filter.query, filter.case_sensitive))
    {
        return true;
    }
    for (u32 diagnostic_index = 0; diagnostic_index < document->diagnostic_count; diagnostic_index += 1)
    {
        if (ide_document_string_contains(document->diagnostics[diagnostic_index].message, filter.query, filter.case_sensitive))
        {
            return true;
        }
    }
    return false;
}

bool ide_document_model_entity_matches_filter(IdeDocumentModel* model, u32 index)
{
    if (!model || !model->initialized || index >= model->workspace.entity_count)
    {
        return false;
    }
    IdeDocumentEntitySnapshot* entity = model->workspace.entities + index;
    IdeDocumentWorkspaceFilterState filter = model->workspace.filter;
    IdeDocument* document = ide_document_model_find(model, entity->source_path);
    if (filter.show_open_only && (!document || !document->is_open))
    {
        return false;
    }
    if (filter.show_dirty_only && (!document || !document->dirty))
    {
        return false;
    }
    if (filter.show_diagnostics_only && (!document || !document->diagnostic_count))
    {
        return false;
    }
    if (!filter.query.length)
    {
        return true;
    }
    return ide_document_string_contains(entity->module_name, filter.query, filter.case_sensitive) ||
           ide_document_string_contains(entity->source_path, filter.query, filter.case_sensitive) ||
           ide_document_string_contains(entity->name, filter.query, filter.case_sensitive) ||
           ide_document_string_contains(entity->type_text, filter.query, filter.case_sensitive);
}

IdeDocumentWorkspaceStatus ide_document_model_status(IdeDocumentModel* model)
{
    IdeDocumentWorkspaceStatus result = {0};
    if (!model || !model->initialized)
    {
        return result;
    }
    result.document_count = model->workspace.document_count;
    result.open_document_count = model->workspace.open_document_count;
    result.import_count = model->workspace.import_count;
    result.entity_count = model->workspace.entity_count;
    if (model->workspace.document_count == 0)
    {
        return result;
    }
    BUSTER_CHECK(model->workspace.documents != 0);
    for (u32 index = 0; index < model->workspace.document_count; index += 1)
    {
        IdeDocument* document = model->workspace.documents + index;
        result.dirty_document_count += document->dirty;
        result.diagnostic_document_count += document->diagnostic_count != 0;
        for (u32 diagnostic_index = 0; diagnostic_index < document->diagnostic_count; diagnostic_index += 1)
        {
            IdeDocumentDiagnosticSeverity severity = document->diagnostics[diagnostic_index].severity;
            if (severity == IDE_DOCUMENT_DIAGNOSTIC_INFO)
            {
                result.info_count += 1;
            }
            else if (severity == IDE_DOCUMENT_DIAGNOSTIC_WARNING)
            {
                result.warning_count += 1;
            }
            else if (severity == IDE_DOCUMENT_DIAGNOSTIC_ERROR)
            {
                result.error_count += 1;
            }
        }
    }
    return result;
}

u64 ide_document_model_analysis_generation(const IdeDocumentModel* model)
{
    return model && model->initialized ? model->workspace.analysis_generation : 0;
}

#if BUSTER_INCLUDE_TESTS
IdeDocumentAnalysisOperationStats ide_document_model_test_last_analysis_stats(const IdeDocumentModel* model)
{
    return model && model->initialized ? model->workspace.last_analysis_stats : (IdeDocumentAnalysisOperationStats){0};
}

u8 ide_document_model_test_document_work_flags(const IdeDocumentModel* model, u32 index)
{
    if (!model || !model->initialized || !model->workspace.last_analysis_work_flags || index >= model->workspace.document_count)
    {
        return 0;
    }
    return model->workspace.last_analysis_work_flags[index];
}
#endif

IdeDocument* ide_document_model_find(IdeDocumentModel* model, String8 path)
{
    if (!model || !model->initialized || !path.length)
    {
        return 0;
    }
    if (model->workspace.document_count == 0)
    {
        return 0;
    }
    BUSTER_CHECK(model->workspace.documents != 0);
    TemporalArena scratch = scratch_begin(0, 0);
    u32 index = ide_workspace_find_path(scratch.arena, &model->workspace, path);
    scratch_end(scratch);
    return index == IDE_DOCUMENT_INDEX_INVALID ? 0 : model->workspace.documents + index;
}

IdeDocument* ide_document_model_document_at(IdeDocumentModel* model, u32 index)
{
    if (!model || !model->initialized)
    {
        return 0;
    }
    if (model->workspace.document_count == 0)
    {
        return 0;
    }
    BUSTER_CHECK(model->workspace.documents != 0);
    if (index >= model->workspace.document_count)
    {
        return 0;
    }
    return model->workspace.documents + index;
}

IdeDocument* ide_document_model_active_document(IdeDocumentModel* model)
{
    if (!model || !model->initialized)
    {
        return 0;
    }
    if (model->workspace.document_count == 0)
    {
        return 0;
    }
    BUSTER_CHECK(model->workspace.documents != 0);
    if (model->workspace.active_document_index == IDE_DOCUMENT_INDEX_INVALID)
    {
        return 0;
    }
    return ide_document_model_document_at(model, model->workspace.active_document_index);
}

IdeDocumentImport* ide_document_model_import_at(IdeDocumentModel* model, u32 index)
{
    if (!model || !model->initialized || index >= model->workspace.import_count)
    {
        return 0;
    }
    return model->workspace.imports + index;
}

u32 ide_document_model_document_count(IdeDocumentModel* model)
{
    return model && model->initialized ? model->workspace.document_count : 0;
}

u32 ide_document_model_open_document_count(IdeDocumentModel* model)
{
    return model && model->initialized ? model->workspace.open_document_count : 0;
}
