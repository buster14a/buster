#include <buster/tests/ide_document_test.h>
#if BUSTER_INCLUDE_TESTS

#include <buster/lib/file.h>
#include <buster/lib/hash.h>
#include <buster/lib/string.h>
#include <buster/lib/system_headers.h>
#include <buster/lib/window.h>

BUSTER_F_DECL IdeDocumentErrorKind ide_app_test_open_path(String8 path);
BUSTER_F_DECL bool ide_app_test_bad_open_preserves_empty(String8 path);
BUSTER_F_DECL bool ide_app_test_reload_discard(String8 path, String8 expected_source, String8 edited_source);
BUSTER_F_DECL bool ide_app_test_scroll_round_trip(String8 first_path, String8 second_path);
BUSTER_F_DECL bool ide_app_test_filter_state(String8 path, String8 query);
BUSTER_F_DECL bool ide_app_test_oversized_request_preserves_action(String8 preserved_path);
BUSTER_F_DECL bool ide_app_test_failed_editor_commit_preserves_buffer(String8 path, String8 edited_source);
BUSTER_F_DECL bool ide_app_test_drop_preserves_first(SliceString8 first, SliceString8 second, String8 expected_path);
BUSTER_F_DECL bool ide_app_test_copy_storage_self(void);

#if !BUSTER_ANDROID && !BUSTER_IOS
BUSTER_GLOBAL_LOCAL String8 ide_document_test_temporary_root(Arena* arena, String8 name)
{
    String8 temporary = buster_test_temporary_path(arena, name, S8(""));
#if defined(_WIN32)
    String8 result = os_path_absolute(arena, temporary, true);
    if (!result.length)
    {
        return temporary;
    }
    for (u64 index = 0; index < result.length; index += 1)
    {
        if (result.pointer[index] == '\\')
        {
            result.pointer[index] = '/';
        }
    }
    return result;
#elif defined(__linux__) || defined(__APPLE__)
    if (!temporary.length)
    {
        return temporary;
    }

    // realpath() rejects a not-yet-created leaf. Canonicalize the complete
    // existing temporary parent instead so the nested buster-tests directory
    // remains owned by the harness and macOS /tmp -> /private/tmp is reflected
    // in every test path before the workspace is created.
    u64 leaf_start = temporary.length;
    while (leaf_start && temporary.pointer[leaf_start - 1] != '/' && temporary.pointer[leaf_start - 1] != '\\')
    {
        leaf_start -= 1;
    }
    if (!leaf_start)
    {
        return temporary;
    }
    String8 parent_path = string_slice(temporary, 0, leaf_start - 1);
    // os_path_absolute() passes POSIX input directly to realpath(), which
    // requires a NUL-terminated path even though the rest of the code uses
    // length-delimited String8 values. Copy the nested parent before asking
    // realpath() to canonicalize it.
    String8 parent_input = string_duplicate_arena(arena, parent_path, true);
    String8 parent = os_path_absolute(arena, parent_input, true);
    if (!parent.length)
    {
        return temporary;
    }
    String8 leaf = string_slice(temporary, leaf_start, temporary.length);
    bool parent_separator = parent.length && (parent.pointer[parent.length - 1] == '/' || parent.pointer[parent.length - 1] == '\\');
    return string_format_z(arena, parent_separator ? S8("{S8}{S8}") : S8("{S8}/{S8}"), parent, leaf);
#else
    return temporary;
#endif
}

BUSTER_GLOBAL_LOCAL bool ide_document_test_analysis_stats_equal(IdeDocumentAnalysisOperationStats left,
                                                                 IdeDocumentAnalysisOperationStats right)
{
    return left.parsed_count == right.parsed_count && left.indexed_count == right.indexed_count &&
           left.analyzed_count == right.analyzed_count && left.invalidated_count == right.invalidated_count &&
           left.allocated_snapshot_count == right.allocated_snapshot_count && left.full_fallback == right.full_fallback;
}

BUSTER_GLOBAL_LOCAL bool ide_document_test_range_equal(ParserSourceRange left, ParserSourceRange right)
{
    return left.offset == right.offset && left.length == right.length && left.line == right.line && left.column == right.column;
}

BUSTER_GLOBAL_LOCAL u32 ide_document_test_index(IdeDocumentModel* model, String8 path)
{
    IdeDocument* document = ide_document_model_find(model, path);
    if (!document)
    {
        return IDE_DOCUMENT_INDEX_INVALID;
    }
    for (u32 index = 0; index < ide_document_model_document_count(model); index += 1)
    {
        if (ide_document_model_document_at(model, index) == document)
        {
            return index;
        }
    }
    return IDE_DOCUMENT_INDEX_INVALID;
}

BUSTER_GLOBAL_LOCAL bool ide_document_test_analysis_outcome_equal(IdeDocumentModel* left, IdeDocumentModel* right)
{
    if (left->workspace.document_count != right->workspace.document_count ||
        left->workspace.import_count != right->workspace.import_count || left->workspace.entity_count != right->workspace.entity_count)
    {
        return false;
    }
    for (u32 index = 0; index < left->workspace.document_count; index += 1)
    {
        IdeDocument* left_document = left->workspace.documents + index;
        IdeDocument* right_document = right->workspace.documents + index;
        if (!string_equal(left_document->path, right_document->path) ||
            !string_equal(left_document->module_name, right_document->module_name) ||
            !string_equal(left_document->source, right_document->source) ||
            left_document->diagnostic_count != right_document->diagnostic_count)
        {
            return false;
        }
        for (u32 diagnostic_index = 0; diagnostic_index < left_document->diagnostic_count; diagnostic_index += 1)
        {
            IdeDocumentDiagnostic* left_diagnostic = left_document->diagnostics + diagnostic_index;
            IdeDocumentDiagnostic* right_diagnostic = right_document->diagnostics + diagnostic_index;
            if (!string_equal(left_diagnostic->file_path, right_diagnostic->file_path) ||
                !string_equal(left_diagnostic->message, right_diagnostic->message) ||
                !ide_document_test_range_equal(left_diagnostic->range, right_diagnostic->range) ||
                left_diagnostic->identity != right_diagnostic->identity || left_diagnostic->severity != right_diagnostic->severity ||
                left_diagnostic->source != right_diagnostic->source)
            {
                return false;
            }
        }
    }
    for (u32 index = 0; index < left->workspace.import_count; index += 1)
    {
        IdeDocumentImport* left_import = left->workspace.imports + index;
        IdeDocumentImport* right_import = right->workspace.imports + index;
        if (!string_equal(left_import->source_path, right_import->source_path) ||
            !string_equal(left_import->name_space, right_import->name_space) ||
            !string_equal(left_import->requested_path, right_import->requested_path) ||
            !string_equal(left_import->target_path, right_import->target_path) ||
            !ide_document_test_range_equal(left_import->range, right_import->range) ||
            !ide_document_test_range_equal(left_import->path_range, right_import->path_range) || left_import->state != right_import->state)
        {
            return false;
        }
    }
    for (u32 index = 0; index < left->workspace.entity_count; index += 1)
    {
        IdeDocumentEntitySnapshot* left_entity = left->workspace.entities + index;
        IdeDocumentEntitySnapshot* right_entity = right->workspace.entities + index;
        if (!string_equal(left_entity->module_name, right_entity->module_name) ||
            !string_equal(left_entity->source_path, right_entity->source_path) || !string_equal(left_entity->name, right_entity->name) ||
            !string_equal(left_entity->type_text, right_entity->type_text) ||
            !ide_document_test_range_equal(left_entity->range, right_entity->range) || left_entity->kind != right_entity->kind)
        {
            return false;
        }
    }
    return true;
}

BUSTER_GLOBAL_LOCAL UnitTestResult ide_document_incremental_analysis_tests(UnitTestArguments* arguments, Arena* test_arena, Arena* arena,
                                                                           Arena* staging_arena)
{
    UnitTestResult result = {0};
    BUSTER_UNUSED(arguments);
    u8 all_work = (u8)(IDE_DOCUMENT_ANALYSIS_WORK_PARSED | IDE_DOCUMENT_ANALYSIS_WORK_INDEXED |
                       IDE_DOCUMENT_ANALYSIS_WORK_ANALYZED | IDE_DOCUMENT_ANALYSIS_WORK_INVALIDATED);
    u8 dependent_work =
        (u8)(IDE_DOCUMENT_ANALYSIS_WORK_INDEXED | IDE_DOCUMENT_ANALYSIS_WORK_ANALYZED | IDE_DOCUMENT_ANALYSIS_WORK_INVALIDATED);
    String8 root = ide_document_test_temporary_root(test_arena, S8("buster-ide-document-incremental"));
    String8 app_path = string_format_z(test_arena, S8("{S8}/app.bbb"), root);
    String8 leaf_path = string_format_z(test_arena, S8("{S8}/leaf.bbb"), root);
    String8 mid_path = string_format_z(test_arena, S8("{S8}/mid.bbb"), root);
    String8 other_path = string_format_z(test_arena, S8("{S8}/other.bbb"), root);
    String8 app_source = S8("import dep = \"./mid.bbb\";\ncode main : fn () s32 { return dep.mid(); }\n");
    String8 leaf_source = S8("data $value: s32 = 1;\ncode leaf : fn () s32 { return 1; }\n");
    String8 leaf_body_source = S8("data $value: s32 = 1;\ncode leaf : fn () s32 { return 2; }\n");
    String8 leaf_public_source = S8("data $value: u64 = 1;\ncode leaf : fn () u64 { return 2; }\n");
    String8 mid_source = S8("import dep = \"leaf\";\ncode mid : fn () s32 { return dep.leaf(); }\n");
    String8 mid_retargeted_source = S8("import dep = \"other\";\ncode mid : fn () s32 { return dep.leaf(); }\n");
    String8 other_source = S8("code leaf : fn () s32 { return 7; }\n");
    String8 other_body_source = S8("code leaf : fn () s32 { return 8; }\n");
    String8 diagnostic_source = S8("data $value: u64 = 1;\ncode leaf : fn () u64 { return missing; }\n");

    os_directory_delete(root);
    os_make_directory(root);
    BUSTER_TEST(arguments, file_write(app_path, BUSTER_SLICE_TO_BYTE_SLICE(app_source)));
    BUSTER_TEST(arguments, file_write(leaf_path, BUSTER_SLICE_TO_BYTE_SLICE(leaf_source)));
    BUSTER_TEST(arguments, file_write(mid_path, BUSTER_SLICE_TO_BYTE_SLICE(mid_source)));
    BUSTER_TEST(arguments, file_write(other_path, BUSTER_SLICE_TO_BYTE_SLICE(other_source)));

    IdeDocumentModel model = {0};
    IdeDocumentErrorKind error = ide_document_model_initialize(&model, arena, staging_arena,
                                                               (IdeDocumentModelOptions){.workspace_root = root, .open_path = leaf_path});
    BUSTER_TEST(arguments, error == IDE_DOCUMENT_ERROR_NONE);
    if (model.initialized)
    {
        BUSTER_TEST(arguments, model.workspace.document_count == 4 && model.workspace.import_count == 2 &&
                                  !model.workspace.analysis_contains_generics && !model.workspace.analysis_has_cycles);
        u32 app_index = ide_document_test_index(&model, app_path);
        u32 leaf_index = ide_document_test_index(&model, leaf_path);
        u32 mid_index = ide_document_test_index(&model, mid_path);
        u32 other_index = ide_document_test_index(&model, other_path);
        BUSTER_TEST(arguments, app_index != IDE_DOCUMENT_INDEX_INVALID && leaf_index != IDE_DOCUMENT_INDEX_INVALID &&
                                  mid_index != IDE_DOCUMENT_INDEX_INVALID && other_index != IDE_DOCUMENT_INDEX_INVALID);
        IdeDocumentImport* app_import = 0;
        for (u32 import_index = 0; import_index < model.workspace.import_count; import_index += 1)
        {
            IdeDocumentImport* candidate = ide_document_model_import_at(&model, import_index);
            if (candidate && string_equal(candidate->source_path, app_path))
            {
                app_import = candidate;
                break;
            }
        }
        BUSTER_TEST(arguments, app_import && app_import->state == IDE_DOCUMENT_IMPORT_RESOLVED &&
                                  string_equal(app_import->target_path, mid_path));
        BUSTER_TEST(arguments, app_index == IDE_DOCUMENT_INDEX_INVALID || model.workspace.documents[app_index].diagnostic_count == 0);

        IdeDocumentAnalysisOperationStats equal_stats = ide_document_model_test_last_analysis_stats(&model);
        u8 equal_work_flags[4] = {0};
        for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(equal_work_flags); index += 1)
        {
            equal_work_flags[index] = ide_document_model_test_document_work_flags(&model, index);
        }
        u64 equal_generation = ide_document_model_analysis_generation(&model);
        IdeDocument* equal_leaf = ide_document_model_find(&model, leaf_path);
        IdeDocument* equal_documents = model.workspace.documents;
        IdeDocumentImport* equal_imports = model.workspace.imports;
        IdeDocumentEntitySnapshot* equal_entities = model.workspace.entities;
        char8* equal_source_pointer = equal_leaf ? equal_leaf->source.pointer : 0;
        BUSTER_TEST(arguments, ide_document_model_set_text(&model, leaf_path, leaf_source) == IDE_DOCUMENT_ERROR_NONE);
        BUSTER_TEST(arguments, ide_document_model_analysis_generation(&model) == equal_generation &&
                                  model.workspace.documents == equal_documents && model.workspace.imports == equal_imports &&
                                  model.workspace.entities == equal_entities && ide_document_model_find(&model, leaf_path) == equal_leaf &&
                                  equal_leaf && equal_leaf->source.pointer == equal_source_pointer);
        BUSTER_TEST(arguments,
                    ide_document_test_analysis_stats_equal(ide_document_model_test_last_analysis_stats(&model), equal_stats));
        for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(equal_work_flags); index += 1)
        {
            BUSTER_TEST(arguments, ide_document_model_test_document_work_flags(&model, index) == equal_work_flags[index]);
        }

        char8* other_source_before_body_edit = ide_document_model_find(&model, other_path)->source.pointer;
        BUSTER_TEST(arguments, ide_document_model_set_text(&model, leaf_path, leaf_body_source) == IDE_DOCUMENT_ERROR_NONE);
        IdeDocumentAnalysisOperationStats body_stats = ide_document_model_test_last_analysis_stats(&model);
        BUSTER_TEST(arguments, ide_document_model_analysis_generation(&model) == equal_generation + 1);
        BUSTER_TEST(arguments, body_stats.parsed_count == 1 && body_stats.indexed_count == 1 && body_stats.analyzed_count == 1 &&
                                  body_stats.invalidated_count == 1 && body_stats.allocated_snapshot_count == 1 && !body_stats.full_fallback);
        BUSTER_TEST(arguments, ide_document_model_test_document_work_flags(&model, leaf_index) == all_work);
        BUSTER_TEST(arguments, ide_document_model_test_document_work_flags(&model, app_index) == 0 &&
                                  ide_document_model_test_document_work_flags(&model, mid_index) == 0 &&
                                  ide_document_model_test_document_work_flags(&model, other_index) == 0);
        BUSTER_TEST(arguments, ide_document_model_find(&model, other_path)->source.pointer == other_source_before_body_edit);

        BUSTER_TEST(arguments, ide_document_model_open(&model, other_path) == IDE_DOCUMENT_ERROR_NONE);
        BUSTER_TEST(arguments, ide_document_model_set_text(&model, other_path, other_body_source) == IDE_DOCUMENT_ERROR_NONE);
        IdeDocumentAnalysisOperationStats other_body_stats = ide_document_model_test_last_analysis_stats(&model);
        BUSTER_TEST(arguments, other_body_stats.parsed_count == 1 && other_body_stats.indexed_count == 1 &&
                                  other_body_stats.analyzed_count == 1 && other_body_stats.invalidated_count == 1 &&
                                  other_body_stats.allocated_snapshot_count == 1 && !other_body_stats.full_fallback);

        u64 public_generation = ide_document_model_analysis_generation(&model);
        char8* other_source_before_public_edit = ide_document_model_find(&model, other_path)->source.pointer;
        BUSTER_TEST(arguments, ide_document_model_set_text(&model, leaf_path, leaf_public_source) == IDE_DOCUMENT_ERROR_NONE);
        IdeDocumentAnalysisOperationStats public_stats = ide_document_model_test_last_analysis_stats(&model);
        BUSTER_TEST(arguments, ide_document_model_analysis_generation(&model) == public_generation + 1);
        BUSTER_TEST(arguments, public_stats.parsed_count == 1 && public_stats.indexed_count == 3 && public_stats.analyzed_count == 3 &&
                                  public_stats.invalidated_count == 3 && public_stats.allocated_snapshot_count == 3 && !public_stats.full_fallback);
        BUSTER_TEST(arguments, ide_document_model_test_document_work_flags(&model, leaf_index) == all_work &&
                                  ide_document_model_test_document_work_flags(&model, mid_index) == dependent_work &&
                                  ide_document_model_test_document_work_flags(&model, app_index) == dependent_work &&
                                  ide_document_model_test_document_work_flags(&model, other_index) == 0);
        BUSTER_TEST(arguments, ide_document_model_find(&model, other_path)->source.pointer == other_source_before_public_edit);

        BUSTER_TEST(arguments, ide_document_model_open(&model, mid_path) == IDE_DOCUMENT_ERROR_NONE);
        u64 retarget_generation = ide_document_model_analysis_generation(&model);
        char8* leaf_source_before_retarget = ide_document_model_find(&model, leaf_path)->source.pointer;
        char8* other_source_before_retarget = ide_document_model_find(&model, other_path)->source.pointer;
        BUSTER_TEST(arguments, ide_document_model_set_text(&model, mid_path, mid_retargeted_source) == IDE_DOCUMENT_ERROR_NONE);
        IdeDocumentAnalysisOperationStats retarget_stats = ide_document_model_test_last_analysis_stats(&model);
        BUSTER_TEST(arguments, ide_document_model_analysis_generation(&model) == retarget_generation + 1);
        BUSTER_TEST(arguments, retarget_stats.parsed_count == 1 && retarget_stats.indexed_count == 2 && retarget_stats.analyzed_count == 2 &&
                                  retarget_stats.invalidated_count == 2 && retarget_stats.allocated_snapshot_count == 2 &&
                                  !retarget_stats.full_fallback);
        BUSTER_TEST(arguments, ide_document_model_test_document_work_flags(&model, mid_index) == all_work &&
                                  ide_document_model_test_document_work_flags(&model, app_index) == dependent_work &&
                                  ide_document_model_test_document_work_flags(&model, leaf_index) == 0 &&
                                  ide_document_model_test_document_work_flags(&model, other_index) == 0);
        BUSTER_TEST(arguments, ide_document_model_find(&model, leaf_path)->source.pointer == leaf_source_before_retarget &&
                                  ide_document_model_find(&model, other_path)->source.pointer == other_source_before_retarget);

        u64 rollback_generation = ide_document_model_analysis_generation(&model);
        IdeDocumentAnalysisOperationStats rollback_stats = ide_document_model_test_last_analysis_stats(&model);
        u8 rollback_work_flags[4] = {0};
        for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(rollback_work_flags); index += 1)
        {
            rollback_work_flags[index] = ide_document_model_test_document_work_flags(&model, index);
        }
        IdeDocument* rollback_documents = model.workspace.documents;
        IdeDocumentImport* rollback_imports = model.workspace.imports;
        IdeDocumentEntitySnapshot* rollback_entities = model.workspace.entities;
        String8 rollback_root = model.workspace.root_path;
        IdeDocument* rollback_leaf = ide_document_model_find(&model, leaf_path);
        String8 rollback_source = rollback_leaf ? rollback_leaf->source : (String8){0};
        model.max_diagnostics = 0;
        BUSTER_TEST(arguments,
                    ide_document_model_set_text(&model, leaf_path, diagnostic_source) == IDE_DOCUMENT_ERROR_DIAGNOSTIC_LIMIT);
        BUSTER_TEST(arguments, ide_document_model_analysis_generation(&model) == rollback_generation &&
                                  model.workspace.documents == rollback_documents && model.workspace.imports == rollback_imports &&
                                  model.workspace.entities == rollback_entities && model.workspace.root_path.pointer == rollback_root.pointer &&
                                  ide_document_model_find(&model, leaf_path) == rollback_leaf && rollback_leaf &&
                                  rollback_leaf->source.pointer == rollback_source.pointer && string_equal(rollback_leaf->source, rollback_source));
        BUSTER_TEST(arguments,
                    ide_document_test_analysis_stats_equal(ide_document_model_test_last_analysis_stats(&model), rollback_stats));
        for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(rollback_work_flags); index += 1)
        {
            BUSTER_TEST(arguments, ide_document_model_test_document_work_flags(&model, index) == rollback_work_flags[index]);
        }
        model.max_diagnostics = IDE_DOCUMENT_DEFAULT_MAX_DIAGNOSTICS;

        // Materialize the incrementally edited workspace and compare its public
        // analysis snapshots with a fresh full rebuild of the same sources.
        BUSTER_TEST(arguments, file_write(leaf_path, BUSTER_SLICE_TO_BYTE_SLICE(leaf_public_source)));
        BUSTER_TEST(arguments, file_write(mid_path, BUSTER_SLICE_TO_BYTE_SLICE(mid_retargeted_source)));
        BUSTER_TEST(arguments, file_write(other_path, BUSTER_SLICE_TO_BYTE_SLICE(other_body_source)));
        Arena* clean_arena = arena_create((ArenaCreation){.reserved_size = BUSTER_MB(32)});
        Arena* clean_staging_arena = arena_create((ArenaCreation){.reserved_size = BUSTER_MB(32)});
        BUSTER_TEST(arguments, clean_arena != 0 && clean_staging_arena != 0);
        if (clean_arena && clean_staging_arena)
        {
            IdeDocumentModel clean_model = {0};
            BUSTER_TEST(arguments,
                        ide_document_model_initialize(&clean_model, clean_arena, clean_staging_arena,
                                                      (IdeDocumentModelOptions){.workspace_root = root}) == IDE_DOCUMENT_ERROR_NONE);
            if (clean_model.initialized)
            {
                BUSTER_TEST(arguments, ide_document_test_analysis_outcome_equal(&model, &clean_model));
                ide_document_model_deinitialize(&clean_model);
            }
        }
        if (clean_arena)
        {
            BUSTER_TEST(arguments, arena_destroy(clean_arena, 1));
        }
        if (clean_staging_arena)
        {
            BUSTER_TEST(arguments, arena_destroy(clean_staging_arena, 1));
        }
        ide_document_model_deinitialize(&model);
    }
    BUSTER_TEST(arguments, os_directory_delete(root));

    String8 generic_root = ide_document_test_temporary_root(test_arena, S8("buster-ide-document-incremental-generic"));
    String8 generic_path = string_format_z(test_arena, S8("{S8}/generic.bbb"), generic_root);
    String8 plain_path = string_format_z(test_arena, S8("{S8}/plain.bbb"), generic_root);
    String8 generic_source = S8("code identity : fn ($value: $T) $T { return value; }\n");
    String8 plain_source = S8("code plain : fn () s32 { return 1; }\n");
    String8 plain_edited_source = S8("code plain : fn () s32 { return 2; }\n");
    os_directory_delete(generic_root);
    os_make_directory(generic_root);
    BUSTER_TEST(arguments, file_write(generic_path, BUSTER_SLICE_TO_BYTE_SLICE(generic_source)));
    BUSTER_TEST(arguments, file_write(plain_path, BUSTER_SLICE_TO_BYTE_SLICE(plain_source)));

    error = ide_document_model_initialize(&model, arena, staging_arena,
                                          (IdeDocumentModelOptions){.workspace_root = generic_root, .open_path = plain_path});
    BUSTER_TEST(arguments, error == IDE_DOCUMENT_ERROR_NONE);
    if (model.initialized)
    {
        BUSTER_TEST(arguments, model.workspace.document_count == 2 && model.workspace.analysis_contains_generics);
        u32 generic_index = ide_document_test_index(&model, generic_path);
        u32 plain_index = ide_document_test_index(&model, plain_path);
        BUSTER_TEST(arguments, generic_index != IDE_DOCUMENT_INDEX_INVALID && plain_index != IDE_DOCUMENT_INDEX_INVALID);
        u64 generation = ide_document_model_analysis_generation(&model);
        char8* generic_source_pointer = ide_document_model_find(&model, generic_path)->source.pointer;
        BUSTER_TEST(arguments, ide_document_model_set_text(&model, plain_path, plain_edited_source) == IDE_DOCUMENT_ERROR_NONE);
        IdeDocumentAnalysisOperationStats stats = ide_document_model_test_last_analysis_stats(&model);
        BUSTER_TEST(arguments, ide_document_model_analysis_generation(&model) == generation + 1);
        BUSTER_TEST(arguments, stats.parsed_count == 1 && stats.indexed_count == 2 && stats.analyzed_count == 2 &&
                                  stats.invalidated_count == 2 && stats.allocated_snapshot_count == 2 && stats.full_fallback);
        BUSTER_TEST(arguments, ide_document_model_test_document_work_flags(&model, plain_index) == all_work &&
                                  ide_document_model_test_document_work_flags(&model, generic_index) == dependent_work);
        BUSTER_TEST(arguments, ide_document_model_find(&model, generic_path)->source.pointer == generic_source_pointer);
        ide_document_model_deinitialize(&model);
    }
    BUSTER_TEST(arguments, os_directory_delete(generic_root));

    String8 cyclic_root = ide_document_test_temporary_root(test_arena, S8("buster-ide-document-incremental-cycle"));
    String8 cycle_a_path = string_format_z(test_arena, S8("{S8}/a.bbb"), cyclic_root);
    String8 cycle_b_path = string_format_z(test_arena, S8("{S8}/b.bbb"), cyclic_root);
    String8 cycle_a_source = S8("import b = \"b\";\ncode a : fn () s32 { return b.b(); }\n");
    String8 cycle_b_source = S8("import a = \"a\";\ncode b : fn () s32 { return 1; }\n");
    String8 cycle_b_edited_source = S8("import a = \"a\";\ncode b : fn () s32 { return 2; }\n");
    os_directory_delete(cyclic_root);
    os_make_directory(cyclic_root);
    BUSTER_TEST(arguments, file_write(cycle_a_path, BUSTER_SLICE_TO_BYTE_SLICE(cycle_a_source)));
    BUSTER_TEST(arguments, file_write(cycle_b_path, BUSTER_SLICE_TO_BYTE_SLICE(cycle_b_source)));

    error = ide_document_model_initialize(&model, arena, staging_arena,
                                          (IdeDocumentModelOptions){.workspace_root = cyclic_root, .open_path = cycle_b_path});
    BUSTER_TEST(arguments, error == IDE_DOCUMENT_ERROR_NONE);
    if (model.initialized)
    {
        BUSTER_TEST(arguments, model.workspace.document_count == 2 && model.workspace.analysis_has_cycles);
        u32 cycle_a_index = ide_document_test_index(&model, cycle_a_path);
        u32 cycle_b_index = ide_document_test_index(&model, cycle_b_path);
        BUSTER_TEST(arguments, cycle_a_index != IDE_DOCUMENT_INDEX_INVALID && cycle_b_index != IDE_DOCUMENT_INDEX_INVALID);
        u64 generation = ide_document_model_analysis_generation(&model);
        IdeDocument* cycle_a = ide_document_model_find(&model, cycle_a_path);
        char8* cycle_a_source_pointer = cycle_a ? cycle_a->source.pointer : 0;
        BUSTER_TEST(arguments, ide_document_model_set_text(&model, cycle_b_path, cycle_b_edited_source) == IDE_DOCUMENT_ERROR_NONE);
        IdeDocumentAnalysisOperationStats stats = ide_document_model_test_last_analysis_stats(&model);
        BUSTER_TEST(arguments, ide_document_model_analysis_generation(&model) == generation + 1);
        BUSTER_TEST(arguments, stats.parsed_count == 1 && stats.indexed_count == 2 && stats.analyzed_count == 2 &&
                                  stats.invalidated_count == 2 && stats.allocated_snapshot_count == 2 && stats.full_fallback);
        BUSTER_TEST(arguments, ide_document_model_test_document_work_flags(&model, cycle_b_index) == all_work &&
                                  ide_document_model_test_document_work_flags(&model, cycle_a_index) == dependent_work);
        cycle_a = ide_document_model_find(&model, cycle_a_path);
        BUSTER_TEST(arguments, cycle_a && cycle_a->source.pointer == cycle_a_source_pointer);
        ide_document_model_deinitialize(&model);
    }
    BUSTER_TEST(arguments, os_directory_delete(cyclic_root));

    String8 recovered_root = ide_document_test_temporary_root(test_arena, S8("buster-ide-document-recovered-import"));
    String8 recovered_invalid_path = string_format_z(test_arena, S8("{S8}/a.bbb"), recovered_root);
    String8 recovered_valid_path = string_format_z(test_arena, S8("{S8}/b.bbb"), recovered_root);
    String8 recovered_invalid_source = S8("import b = \"b\";\n@\n");
    String8 recovered_valid_source = S8("import a = \"a\";\ncode b : fn () s32 { return 1; }\n");
    String8 recovered_valid_edited_source = S8("import a = \"a\";\ncode b : fn () s32 { return 2; }\n");
    os_directory_delete(recovered_root);
    os_make_directory(recovered_root);
    BUSTER_TEST(arguments, file_write(recovered_invalid_path, BUSTER_SLICE_TO_BYTE_SLICE(recovered_invalid_source)));
    BUSTER_TEST(arguments, file_write(recovered_valid_path, BUSTER_SLICE_TO_BYTE_SLICE(recovered_valid_source)));

    error = ide_document_model_initialize(&model, arena, staging_arena,
                                          (IdeDocumentModelOptions){.workspace_root = recovered_root, .open_path = recovered_valid_path});
    BUSTER_TEST(arguments, error == IDE_DOCUMENT_ERROR_NONE);
    if (model.initialized)
    {
        BUSTER_TEST(arguments, model.workspace.document_count == 2 && model.workspace.import_count == 2 &&
                                  !model.workspace.analysis_has_cycles);
        IdeDocumentImport* recovered_invalid_import = 0;
        IdeDocumentImport* recovered_valid_import = 0;
        for (u32 import_index = 0; import_index < model.workspace.import_count; import_index += 1)
        {
            IdeDocumentImport* import = ide_document_model_import_at(&model, import_index);
            if (import && string_equal(import->source_path, recovered_invalid_path))
            {
                recovered_invalid_import = import;
            }
            else if (import && string_equal(import->source_path, recovered_valid_path))
            {
                recovered_valid_import = import;
            }
        }
        BUSTER_TEST(arguments, recovered_invalid_import && recovered_invalid_import->state == IDE_DOCUMENT_IMPORT_RESOLVED);
        BUSTER_TEST(arguments, recovered_valid_import && recovered_valid_import->state == IDE_DOCUMENT_IMPORT_RESOLVED &&
                                  string_equal(recovered_valid_import->target_path, recovered_invalid_path));
        IdeDocument* recovered_invalid = ide_document_model_find(&model, recovered_invalid_path);
        IdeDocument* recovered_valid = ide_document_model_find(&model, recovered_valid_path);
        BUSTER_TEST(arguments, recovered_invalid && recovered_invalid->diagnostic_count != 0);
        BUSTER_TEST(arguments, recovered_valid && recovered_valid->diagnostic_count == 0);

        u64 generation = ide_document_model_analysis_generation(&model);
        BUSTER_TEST(arguments,
                    ide_document_model_set_text(&model, recovered_valid_path, recovered_valid_edited_source) == IDE_DOCUMENT_ERROR_NONE);
        IdeDocumentAnalysisOperationStats stats = ide_document_model_test_last_analysis_stats(&model);
        BUSTER_TEST(arguments, ide_document_model_analysis_generation(&model) == generation + 1 && !model.workspace.analysis_has_cycles);
        BUSTER_TEST(arguments, stats.parsed_count == 1 && stats.indexed_count == 1 && stats.analyzed_count == 1 &&
                                  stats.invalidated_count == 1 && stats.allocated_snapshot_count == 1 && !stats.full_fallback);
        ide_document_model_deinitialize(&model);
    }
    BUSTER_TEST(arguments, os_directory_delete(recovered_root));
    return result;
}
#endif

UnitTestResult ide_document_tests(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
#if !BUSTER_ANDROID && !BUSTER_IOS
    Arena* arena = arena_create((ArenaCreation){.reserved_size = BUSTER_MB(32)});
    Arena* staging_arena = arena_create((ArenaCreation){.reserved_size = BUSTER_MB(32)});
    BUSTER_TEST(arguments, arena != 0 && staging_arena != 0);
    if (!arena || !staging_arena)
    {
        if (arena)
        {
            arena_destroy(arena, 1);
        }
        if (staging_arena)
        {
            arena_destroy(staging_arena, 1);
        }
        return result;
    }

    Arena* test_arena = arguments->arena;
    String8 containment_root = ide_document_test_temporary_root(test_arena, S8("buster-ide-document-containment"));
    u64 containment_leaf_start = containment_root.length;
    while (containment_leaf_start && containment_root.pointer[containment_leaf_start - 1] != '/' && containment_root.pointer[containment_leaf_start - 1] != '\\')
    {
        containment_leaf_start -= 1;
    }
    if (containment_leaf_start)
    {
        u64 parent_leaf_start = containment_leaf_start - 1;
        while (parent_leaf_start && containment_root.pointer[parent_leaf_start - 1] != '/' && containment_root.pointer[parent_leaf_start - 1] != '\\')
        {
            parent_leaf_start -= 1;
        }
        String8 containment_parent_leaf = string_slice(containment_root, parent_leaf_start, containment_leaf_start - 1);
        BUSTER_TEST(arguments, string_starts_with_sequence(containment_parent_leaf, S8("buster-tests-")));
    }
    else
    {
        BUSTER_TEST(arguments, false);
    }
    String8 root = ide_document_test_temporary_root(test_arena, S8("buster-ide-document-model"));
    String8 subdirectory = string_format_z(test_arena, S8("{S8}/sub"), root);
    String8 a_path = string_format_z(test_arena, S8("{S8}/a.bbb"), root);
    String8 b_path = string_format_z(test_arena, S8("{S8}/sub/b.bbb"), root);
    String8 z_path = string_format_z(test_arena, S8("{S8}/z.bbb"), root);
    String8 unsupported_open_path = string_format_z(test_arena, S8("{S8}/notes.txt"), root);
    String8 a_source = S8("import b = \"sub/b.bbb\";\ncode main : fn () s32 { return 0; }\n");
    String8 b_source = S8("import a = \"a\";\ncode helper : fn () s32 { return 0; }\n");
    String8 z_source = S8("code zed : fn () s32 { return 0; }\n");

    os_directory_delete(root);
    os_make_directory(root);
    os_make_directory(subdirectory);
    BUSTER_TEST(arguments, file_write(a_path, BUSTER_SLICE_TO_BYTE_SLICE(a_source)));
    BUSTER_TEST(arguments, file_write(b_path, BUSTER_SLICE_TO_BYTE_SLICE(b_source)));
    BUSTER_TEST(arguments, file_write(z_path, BUSTER_SLICE_TO_BYTE_SLICE(z_source)));
    BUSTER_TEST(arguments, file_write(unsupported_open_path, BUSTER_SLICE_TO_BYTE_SLICE(S8("not a buster source\n"))));

    IdeDocumentModel unsupported_open_model = {0};
    IdeDocumentErrorKind unsupported_open_error = ide_document_model_initialize(
        &unsupported_open_model, arena, staging_arena,
        (IdeDocumentModelOptions){
            .workspace_root = root,
            .open_path = unsupported_open_path,
        });
    BUSTER_TEST(arguments, unsupported_open_error == IDE_DOCUMENT_ERROR_OPEN_PATH_NOT_SUPPORTED);
    BUSTER_TEST(arguments, !unsupported_open_model.initialized);
    if (unsupported_open_model.initialized)
    {
        ide_document_model_deinitialize(&unsupported_open_model);
    }

    IdeDocumentModel model = {0};
    IdeDocumentErrorKind error = ide_document_model_initialize(
        &model, arena, staging_arena,
        (IdeDocumentModelOptions){
            .workspace_root = root,
        });
    BUSTER_TEST(arguments, error == IDE_DOCUMENT_ERROR_NONE);
    BUSTER_TEST(arguments, ide_document_model_document_count(&model) == 3);
    BUSTER_TEST(arguments, ide_document_model_open_document_count(&model) == 1);
    BUSTER_TEST(arguments, ide_document_model_active_document(&model) != 0);
    IdeDocument* pointer_before_reinitialize = ide_document_model_active_document(&model);
    Arena* expression_arena_before_reinitialize = model.expression_arena;
    BUSTER_TEST(arguments, ide_document_model_initialize(&model, arena, staging_arena, (IdeDocumentModelOptions){.workspace_root = root}) ==
                              IDE_DOCUMENT_ERROR_ALREADY_INITIALIZED);
    BUSTER_TEST(arguments, model.initialized && model.expression_arena == expression_arena_before_reinitialize &&
                              ide_document_model_active_document(&model) == pointer_before_reinitialize);
    if (model.initialized)
    {
        IdeDocument* first = ide_document_model_document_at(&model, 0);
        IdeDocument* second = ide_document_model_document_at(&model, 1);
        IdeDocument* third = ide_document_model_document_at(&model, 2);
        BUSTER_TEST(arguments, first && string_ends_with_sequence(first->path, S8("/a.bbb")));
        BUSTER_TEST(arguments, second && string_ends_with_sequence(second->path, S8("/sub/b.bbb")));
        BUSTER_TEST(arguments, third && string_ends_with_sequence(third->path, S8("/z.bbb")));
        BUSTER_TEST(arguments, first && first->is_open);
        BUSTER_TEST(arguments, ide_document_model_find(&model, S8("sub/b.bbb")) == second);

        String8 alias = string_format_z(test_arena, S8("{S8}/./a.bbb"), root);
        BUSTER_TEST(arguments, ide_document_model_find(&model, alias) == first);
        BUSTER_TEST(arguments, ide_document_model_find(&model, a_path) == first);

        bool has_cycle = false;
        for (u32 import_index = 0; import_index < model.workspace.import_count; import_index += 1)
        {
            has_cycle |= ide_document_model_import_at(&model, import_index)->state == IDE_DOCUMENT_IMPORT_CYCLE;
        }
        BUSTER_TEST(arguments, model.workspace.import_count == 2);
        BUSTER_TEST(arguments, has_cycle);

        String8 missing_leaf_import_source = S8("import missing = \"missing-leaf\";\ncode main : fn () s32 { return 0; }\n");
        String8 missing_parent_import_source =
            S8("import missing = \"not-created/missing-leaf\";\ncode main : fn () s32 { return 0; }\n");
        String8 missing_leaf_target = string_format_z(test_arena, S8("{S8}/missing-leaf.bbb"), root);
        String8 missing_parent_directory = string_format_z(test_arena, S8("{S8}/not-created"), root);
        os_file_delete(missing_leaf_target);
        os_directory_delete(missing_parent_directory);
        BUSTER_TEST(arguments, ide_document_model_set_text(&model, a_path, missing_leaf_import_source) == IDE_DOCUMENT_ERROR_NONE);
        IdeDocumentImport* missing_leaf_import = ide_document_model_import_at(&model, 0);
        BUSTER_TEST(arguments, missing_leaf_import && missing_leaf_import->state == IDE_DOCUMENT_IMPORT_MISSING &&
                                  missing_leaf_import->target_path.length != 0);
        if (missing_leaf_import)
        {
            BUSTER_STRING_TEST(arguments, missing_leaf_import->target_path, missing_leaf_target);
        }
        BUSTER_TEST(arguments, ide_document_model_set_text(&model, a_path, missing_parent_import_source) == IDE_DOCUMENT_ERROR_NONE);
        IdeDocumentImport* missing_parent_import = ide_document_model_import_at(&model, 0);
        BUSTER_TEST(arguments, missing_parent_import && missing_parent_import->state == IDE_DOCUMENT_IMPORT_MISSING &&
                                  missing_parent_import->target_path.length == 0);
        BUSTER_TEST(arguments, ide_document_model_set_text(&model, a_path, a_source) == IDE_DOCUMENT_ERROR_NONE);

        BUSTER_TEST(arguments, ide_document_model_open(&model, z_path) == IDE_DOCUMENT_ERROR_NONE);
        BUSTER_TEST(arguments, ide_document_model_open_document_count(&model) == 2);
        BUSTER_TEST(arguments, ide_document_model_active_document(&model) == ide_document_model_find(&model, z_path));
        BUSTER_TEST(arguments, ide_document_model_open(&model, b_path) == IDE_DOCUMENT_ERROR_NONE);
        BUSTER_TEST(arguments, ide_document_model_open_document_count(&model) == 3);
        BUSTER_TEST(arguments, ide_document_model_close(&model, b_path) == IDE_DOCUMENT_ERROR_NONE);
        BUSTER_TEST(arguments, ide_document_model_active_document(&model) == ide_document_model_find(&model, z_path));
        BUSTER_TEST(arguments, ide_document_model_close(&model, z_path) == IDE_DOCUMENT_ERROR_NONE);
        BUSTER_TEST(arguments, ide_document_model_active_document(&model) == ide_document_model_find(&model, a_path));
        BUSTER_TEST(arguments, ide_document_model_close(&model, a_path) == IDE_DOCUMENT_ERROR_NONE);
        BUSTER_TEST(arguments, ide_document_model_active_document(&model) == 0);
        BUSTER_TEST(arguments, ide_document_model_open(&model, alias) == IDE_DOCUMENT_ERROR_NONE);

        IdeDocument* pointer_before_mutation = ide_document_model_find(&model, a_path);
        IdeDocumentImport* import_pointer_before_mutation = ide_document_model_import_at(&model, 0);
        BUSTER_TEST(arguments, ide_document_model_open(&model, b_path) == IDE_DOCUMENT_ERROR_NONE);
        IdeDocument* pointer_after_open = ide_document_model_find(&model, a_path);
        IdeDocumentImport* import_pointer_after_open = ide_document_model_import_at(&model, 0);
        BUSTER_TEST(arguments, pointer_after_open != 0 && import_pointer_after_open != 0);
        BUSTER_UNUSED(pointer_before_mutation);
        BUSTER_UNUSED(import_pointer_before_mutation);

        String8 b_without_import = S8("code helper : fn () s32 { return 0; }\n");
        String8 a_retargeted = S8("import z = \"z\";\ncode main : fn () s32 { return 0; }\n");
        String8 a_without_import = S8("code main : fn () s32 { return 0; }\n");
        String8 a_with_unknown = S8("import b = \"sub/b\";\ncode main : fn () s32 { return b.missing(); }\n");
        String8 b_with_missing = S8("code helper : fn () s32 { return 0; }\ncode missing : fn () s32 { return 0; }\n");
        BUSTER_TEST(arguments, ide_document_model_set_text(&model, b_path, b_without_import) == IDE_DOCUMENT_ERROR_NONE);
        BUSTER_TEST(arguments, model.workspace.import_count == 1);
        BUSTER_TEST(arguments, ide_document_model_set_text(&model, a_path, a_retargeted) == IDE_DOCUMENT_ERROR_NONE);
        BUSTER_TEST(arguments, model.workspace.import_count == 1);
        BUSTER_TEST(arguments, ide_document_model_import_at(&model, 0)->state == IDE_DOCUMENT_IMPORT_RESOLVED);
        BUSTER_TEST(arguments, ide_document_model_set_text(&model, a_path, a_without_import) == IDE_DOCUMENT_ERROR_NONE);
        BUSTER_TEST(arguments, model.workspace.import_count == 0);
        BUSTER_TEST(arguments, ide_document_model_set_text(&model, a_path, a_with_unknown) == IDE_DOCUMENT_ERROR_NONE);
        BUSTER_TEST(arguments, model.workspace.import_count == 1);
        bool has_dependent_analysis_diagnostic = false;
        IdeDocument* dependent_document = ide_document_model_find(&model, a_path);
        if (dependent_document)
        {
            for (u32 diagnostic_index = 0; diagnostic_index < dependent_document->diagnostic_count; diagnostic_index += 1)
            {
                has_dependent_analysis_diagnostic |=
                    dependent_document->diagnostics[diagnostic_index].source == IDE_DOCUMENT_DIAGNOSTIC_SOURCE_ANALYSIS;
            }
        }
        BUSTER_TEST(arguments, has_dependent_analysis_diagnostic);
        BUSTER_TEST(arguments, ide_document_model_set_text(&model, b_path, b_with_missing) == IDE_DOCUMENT_ERROR_NONE);
        dependent_document = ide_document_model_find(&model, a_path);
        BUSTER_TEST(arguments, dependent_document && dependent_document->diagnostic_count == 0);
        BUSTER_TEST(arguments, ide_document_model_set_text(&model, a_path, a_source) == IDE_DOCUMENT_ERROR_NONE);
        BUSTER_TEST(arguments, ide_document_model_set_text(&model, b_path, b_source) == IDE_DOCUMENT_ERROR_NONE);
        bool has_dirty_cycle = false;
        for (u32 import_index = 0; import_index < model.workspace.import_count; import_index += 1)
        {
            has_dirty_cycle |= ide_document_model_import_at(&model, import_index)->state == IDE_DOCUMENT_IMPORT_CYCLE;
        }
        BUSTER_TEST(arguments, model.workspace.import_count == 2 && has_dirty_cycle);
        u32 preserved_import_count = model.workspace.import_count;
        u32 preserved_entity_count = model.workspace.entity_count;
        BUSTER_TEST(arguments, ide_document_model_set_text(&model, a_path, a_source) == IDE_DOCUMENT_ERROR_NONE);
        BUSTER_TEST(arguments, model.workspace.import_count == preserved_import_count && model.workspace.entity_count == preserved_entity_count);

        IdeDocument* restored = ide_document_model_find(&model, a_path);
        u64 restored_revision = restored ? restored->revision : 0;
        String8 edited_source = S8("code edited : fn () s32 { return 1; }\n");
        BUSTER_TEST(arguments, ide_document_model_set_text(&model, a_path, edited_source) == IDE_DOCUMENT_ERROR_NONE);
        IdeDocument* edited = ide_document_model_find(&model, a_path);
        BUSTER_TEST(arguments, edited && edited->dirty && edited->revision != edited->saved_revision);
        BUSTER_TEST(arguments, ide_document_model_set_text(&model, a_path, a_source) == IDE_DOCUMENT_ERROR_NONE);
        edited = ide_document_model_find(&model, a_path);
        BUSTER_TEST(arguments, edited && !edited->dirty && edited->revision > restored_revision);
        String8 save_source = S8("code saved : fn () s32 { return 3; }\n");
        String8 save_edit_source = S8("code saved edit : fn () s32 { return 4; }\n");
        BUSTER_TEST(arguments, ide_document_model_set_text(&model, a_path, save_source) == IDE_DOCUMENT_ERROR_NONE);
#if defined(__linux__) || defined(__APPLE__)
        mode_t save_mode = 0600;
        BUSTER_TEST(arguments, chmod((const char*)a_path.pointer, save_mode) == 0);
#endif
        BUSTER_TEST(arguments, ide_document_model_save(&model, a_path) == IDE_DOCUMENT_ERROR_NONE);
#if defined(__linux__) || defined(__APPLE__)
        struct stat saved_mode_stats = {0};
        BUSTER_TEST(arguments, stat((const char*)a_path.pointer, &saved_mode_stats) == 0);
        BUSTER_TEST(arguments, (saved_mode_stats.st_mode & 07777) == save_mode);
#endif
        edited = ide_document_model_find(&model, a_path);
        u64 saved_revision = edited ? edited->saved_revision : 0;
        BUSTER_TEST(arguments, edited && !edited->dirty && edited->saved_hash == buster_hash_64((u8*)save_source.pointer, save_source.length));
        BUSTER_TEST(arguments, ide_document_model_set_text(&model, a_path, save_edit_source) == IDE_DOCUMENT_ERROR_NONE);
        edited = ide_document_model_find(&model, a_path);
        IdeDocument* pointer_before_save_conflict = edited;
        IdeDocumentImport* import_before_save_conflict = ide_document_model_import_at(&model, 0);
        bool external_modified_before_save_conflict = edited ? edited->external_modified : false;
        u64 external_hash_before_save_conflict = edited ? edited->external_hash : 0;
        u64 revision_before_save_conflict = edited ? edited->revision : 0;
        String8 save_conflict_disk_source = S8("code save conflict : fn () s32 { return 5; }\n");
        BUSTER_TEST(arguments, file_write(a_path, BUSTER_SLICE_TO_BYTE_SLICE(save_conflict_disk_source)));
        BUSTER_TEST(arguments, ide_document_model_save(&model, a_path) == IDE_DOCUMENT_ERROR_EXTERNAL_MODIFICATION_CONFLICT);
        BUSTER_TEST(arguments, ide_document_model_find(&model, a_path) == pointer_before_save_conflict);
        BUSTER_TEST(arguments, ide_document_model_import_at(&model, 0) == import_before_save_conflict);
        edited = ide_document_model_find(&model, a_path);
        BUSTER_TEST(arguments, edited && edited->dirty && edited->revision == revision_before_save_conflict);
        if (edited)
        {
            BUSTER_STRING_TEST(arguments, edited->source, save_edit_source);
            BUSTER_STRING_TEST(arguments, edited->saved_source, save_source);
        }
        BUSTER_TEST(arguments, edited && edited->external_modified == external_modified_before_save_conflict &&
                                  edited->external_hash == external_hash_before_save_conflict);
        ByteSlice conflict_disk_bytes = file_read(test_arena, a_path, (FileReadOptions){0});
        BUSTER_STRING_TEST(arguments, BYTE_SLICE_TO_STRING(8, conflict_disk_bytes), save_conflict_disk_source);
        BUSTER_TEST(arguments, file_write(a_path, BUSTER_SLICE_TO_BYTE_SLICE(save_source)));
        BUSTER_TEST(arguments, ide_document_model_save(&model, a_path) == IDE_DOCUMENT_ERROR_NONE);
        edited = ide_document_model_find(&model, a_path);
        BUSTER_TEST(arguments, edited && !edited->dirty && edited->revision > saved_revision);
        if (edited)
        {
            BUSTER_STRING_TEST(arguments, edited->source, save_edit_source);
            BUSTER_STRING_TEST(arguments, edited->saved_source, save_edit_source);
        }
        BUSTER_TEST(arguments, edited && edited->saved_hash == buster_hash_64((u8*)save_edit_source.pointer, save_edit_source.length));

        String8 failed_save_source = S8("code failed save : fn () s32 { return 6; }\n");
        BUSTER_TEST(arguments, ide_document_model_set_text(&model, a_path, failed_save_source) == IDE_DOCUMENT_ERROR_NONE);
        edited = ide_document_model_find(&model, a_path);
        IdeDocument* pointer_before_replace_failure = edited;
        ide_document_model_test_set_save_replace_failure(true);
        BUSTER_TEST(arguments, ide_document_model_save(&model, a_path) == IDE_DOCUMENT_ERROR_FILE_WRITE);
        ide_document_model_test_set_save_replace_failure(false);
        BUSTER_TEST(arguments, ide_document_model_find(&model, a_path) == pointer_before_replace_failure);
        edited = ide_document_model_find(&model, a_path);
        BUSTER_TEST(arguments, edited && edited->dirty);
        if (edited)
        {
            BUSTER_STRING_TEST(arguments, edited->source, failed_save_source);
            BUSTER_STRING_TEST(arguments, edited->saved_source, save_edit_source);
        }
        ByteSlice failed_save_disk_bytes = file_read(test_arena, a_path, (FileReadOptions){0});
        BUSTER_STRING_TEST(arguments, BYTE_SLICE_TO_STRING(8, failed_save_disk_bytes), save_edit_source);
        BUSTER_TEST(arguments, ide_document_model_save(&model, a_path) == IDE_DOCUMENT_ERROR_NONE);
        edited = ide_document_model_find(&model, a_path);
        u64 failed_save_revision = edited ? edited->saved_revision : 0;
        BUSTER_TEST(arguments, edited && !edited->dirty);
        BUSTER_TEST(arguments, ide_document_model_set_text(&model, a_path, save_edit_source) == IDE_DOCUMENT_ERROR_NONE);
        BUSTER_TEST(arguments, ide_document_model_set_text(&model, a_path, failed_save_source) == IDE_DOCUMENT_ERROR_NONE);
        edited = ide_document_model_find(&model, a_path);
        BUSTER_TEST(arguments, edited && !edited->dirty && edited->revision > failed_save_revision);
        BUSTER_TEST(arguments, edited && edited->saved_hash == buster_hash_64((u8*)failed_save_source.pointer, failed_save_source.length));
        BUSTER_TEST(arguments, ide_document_model_set_text(&model, a_path, edited_source) == IDE_DOCUMENT_ERROR_NONE);
        edited = ide_document_model_find(&model, a_path);
        String8 preserved_source = edited ? string_duplicate_arena(test_arena, edited->source, true) : (String8){0};
        String8 disk_source = S8("code external : fn () s32 { return 2; }\n");
        BUSTER_TEST(arguments, file_write(a_path, BUSTER_SLICE_TO_BYTE_SLICE(disk_source)));
        BUSTER_TEST(arguments, ide_document_model_poll_external(&model, a_path) == IDE_DOCUMENT_ERROR_NONE);
        edited = ide_document_model_find(&model, a_path);
        BUSTER_TEST(arguments, edited && edited->external_modified);
        BUSTER_TEST(arguments, edited && edited->external_hash == buster_hash_64((u8*)disk_source.pointer, disk_source.length));
        IdeDocumentErrorKind refresh_error = ide_document_model_refresh_workspace(&model);
        BUSTER_TEST(arguments, refresh_error == IDE_DOCUMENT_ERROR_NONE);
        edited = ide_document_model_find(&model, a_path);
        BUSTER_TEST(arguments, edited != 0);
        if (edited)
        {
            BUSTER_TEST(arguments, edited->dirty && edited->external_modified);
            BUSTER_TEST(arguments, edited->external_hash == buster_hash_64((u8*)disk_source.pointer, disk_source.length));
        }
        IdeDocumentErrorKind reject_error = ide_document_model_reload(&model, a_path, IDE_DOCUMENT_RELOAD_REJECT_DIRTY);
        BUSTER_TEST(arguments, reject_error == IDE_DOCUMENT_ERROR_DIRTY_RELOAD_CONFLICT);
        edited = ide_document_model_find(&model, a_path);
        BUSTER_TEST(arguments, edited != 0);
        if (edited)
        {
            BUSTER_STRING_TEST(arguments, edited->source, preserved_source);
            BUSTER_TEST(arguments, edited->dirty);
        }
        BUSTER_TEST(arguments, ide_document_model_reload(&model, a_path, IDE_DOCUMENT_RELOAD_DISCARD_DIRTY) == IDE_DOCUMENT_ERROR_NONE);
        edited = ide_document_model_find(&model, a_path);
        BUSTER_TEST(arguments, edited != 0);
        if (edited)
        {
            BUSTER_STRING_TEST(arguments, edited->source, disk_source);
            BUSTER_TEST(arguments, !edited->dirty && !edited->external_modified);
        }

        String8 diagnostic_error_source = S8("code broken : fn () s32 { return unknown; }\n");
        BUSTER_TEST(arguments, ide_document_model_set_text(&model, a_path, a_source) == IDE_DOCUMENT_ERROR_NONE);
        edited = ide_document_model_find(&model, a_path);
        String8 source_before_failed_rebuild = edited ? string_duplicate_arena(test_arena, edited->source, true) : (String8){0};
        BUSTER_TEST(arguments, ide_document_model_set_text(&model, b_path, diagnostic_error_source) == IDE_DOCUMENT_ERROR_NONE);
        IdeDocument* b_with_diagnostic = ide_document_model_find(&model, b_path);
        BUSTER_TEST(arguments, b_with_diagnostic && b_with_diagnostic->diagnostic_count != 0);
        u32 imports_before_failed_rebuild = model.workspace.import_count;
        IdeDocument* pointer_before_failed_rebuild = ide_document_model_find(&model, a_path);
        IdeDocumentImport* import_before_failed_rebuild = ide_document_model_import_at(&model, 0);
        model.max_diagnostics = 1;
        BUSTER_TEST(arguments, ide_document_model_set_text(&model, a_path, diagnostic_error_source) == IDE_DOCUMENT_ERROR_DIAGNOSTIC_LIMIT);
        BUSTER_TEST(arguments, ide_document_model_find(&model, a_path) == pointer_before_failed_rebuild);
        BUSTER_TEST(arguments, ide_document_model_import_at(&model, 0) == import_before_failed_rebuild);
        edited = ide_document_model_find(&model, a_path);
        if (edited)
        {
            BUSTER_STRING_TEST(arguments, edited->source, source_before_failed_rebuild);
        }
        BUSTER_TEST(arguments, model.workspace.import_count == imports_before_failed_rebuild);
        model.max_diagnostics = IDE_DOCUMENT_DEFAULT_MAX_DIAGNOSTICS;
        BUSTER_TEST(arguments, ide_document_model_set_text(&model, b_path, b_source) == IDE_DOCUMENT_ERROR_NONE);
        edited = ide_document_model_find(&model, a_path);

        String8 failed_source = edited ? string_duplicate_arena(test_arena, edited->source, true) : (String8){0};
#if defined(__linux__) || defined(__APPLE__)
        String8 root_alias = string_format_z(test_arena, S8("{S8}-alias"), root);
        String8 alias_a_path = string_format_z(test_arena, S8("{S8}/a.bbb"), root_alias);
        BUSTER_TEST(arguments, os_file_delete(root_alias));
        bool root_alias_created = symlink((const char*)root.pointer, (const char*)root_alias.pointer) == 0;
        BUSTER_TEST(arguments, root_alias_created);
#endif
        BUSTER_TEST(arguments, os_file_delete(a_path));
        BUSTER_TEST(arguments, ide_document_model_poll_external(&model, a_path) == IDE_DOCUMENT_ERROR_NONE);
        edited = ide_document_model_find(&model, a_path);
        BUSTER_TEST(arguments, edited && !edited->external_exists && edited->external_modified);
        BUSTER_TEST(arguments, ide_document_model_reload(&model, a_path, IDE_DOCUMENT_RELOAD_DISCARD_DIRTY) == IDE_DOCUMENT_ERROR_PATH_NOT_FOUND);
        edited = ide_document_model_find(&model, a_path);
        if (edited)
        {
            BUSTER_STRING_TEST(arguments, edited->source, failed_source);
        }
        BUSTER_TEST(arguments, edited && !edited->external_exists);
#if defined(__linux__) || defined(__APPLE__)
        if (root_alias_created)
        {
            BUSTER_TEST(arguments, ide_document_model_poll_external(&model, alias_a_path) == IDE_DOCUMENT_ERROR_NONE);
            edited = ide_document_model_find(&model, alias_a_path);
            BUSTER_TEST(arguments, edited && !edited->external_exists && edited->external_modified);
            BUSTER_TEST(arguments, ide_document_model_reload(&model, alias_a_path, IDE_DOCUMENT_RELOAD_DISCARD_DIRTY) ==
                                      IDE_DOCUMENT_ERROR_PATH_NOT_FOUND);
            edited = ide_document_model_find(&model, alias_a_path);
            if (edited)
            {
                BUSTER_STRING_TEST(arguments, edited->source, failed_source);
            }
            BUSTER_TEST(arguments, edited && !edited->external_exists);
        }
#endif
        BUSTER_TEST(arguments, ide_document_model_close(&model, a_path) == IDE_DOCUMENT_ERROR_NONE);
        BUSTER_TEST(arguments, ide_document_model_refresh_workspace(&model) == IDE_DOCUMENT_ERROR_NONE);
        edited = ide_document_model_find(&model, a_path);
        BUSTER_TEST(arguments, edited && !edited->is_open && edited->dirty);
        if (edited)
        {
            BUSTER_STRING_TEST(arguments, edited->source, failed_source);
        }
        BUSTER_TEST(arguments, ide_document_model_open(&model, a_path) == IDE_DOCUMENT_ERROR_NONE);
        edited = ide_document_model_find(&model, a_path);
        BUSTER_TEST(arguments, edited && edited->is_open && edited->dirty);
        if (edited)
        {
            BUSTER_STRING_TEST(arguments, edited->source, failed_source);
        }
        String8 recreated_source = S8("code recreated : fn () s32 { return 8; }\n");
        BUSTER_TEST(arguments, file_write(a_path, BUSTER_SLICE_TO_BYTE_SLICE(recreated_source)));
        BUSTER_TEST(arguments, ide_document_model_poll_external(&model, a_path) == IDE_DOCUMENT_ERROR_NONE);
        edited = ide_document_model_find(&model, a_path);
        BUSTER_TEST(arguments, edited && edited->external_exists && edited->external_modified);
        BUSTER_TEST(arguments, edited && edited->external_hash == buster_hash_64((u8*)recreated_source.pointer, recreated_source.length));
        BUSTER_TEST(arguments, ide_document_model_reload(&model, a_path, IDE_DOCUMENT_RELOAD_DISCARD_DIRTY) == IDE_DOCUMENT_ERROR_NONE);
        edited = ide_document_model_find(&model, a_path);
        if (edited)
        {
            BUSTER_STRING_TEST(arguments, edited->source, recreated_source);
        }
        BUSTER_TEST(arguments, edited && !edited->dirty && !edited->external_modified);
#if defined(__linux__) || defined(__APPLE__)
        if (root_alias_created)
        {
            BUSTER_TEST(arguments, os_file_delete(root_alias));
        }
#endif

        IdeDocumentDiagnosticInput diagnostics[] = {
            {
                .file_path = z_path,
                .range = {.offset = 40, .length = 2, .line = 2, .column = 4},
                .message = S8("zed warning"),
                .severity = IDE_DOCUMENT_DIAGNOSTIC_WARNING,
                .source = IDE_DOCUMENT_DIAGNOSTIC_SOURCE_USER,
            },
            {
                .file_path = a_path,
                .range = {.offset = 12, .length = 1, .line = 1, .column = 2},
                .message = S8("second"),
                .severity = IDE_DOCUMENT_DIAGNOSTIC_ERROR,
                .source = IDE_DOCUMENT_DIAGNOSTIC_SOURCE_ANALYSIS,
            },
            {
                .file_path = a_path,
                .range = {.offset = 4, .length = 1, .line = 1, .column = 1},
                .message = S8("first"),
                .severity = IDE_DOCUMENT_DIAGNOSTIC_ERROR,
                .source = IDE_DOCUMENT_DIAGNOSTIC_SOURCE_PARSER,
            },
        };
        BUSTER_TEST(arguments, ide_document_model_replace_diagnostics(&model, diagnostics, BUSTER_ARRAY_LENGTH(diagnostics)) ==
                                  IDE_DOCUMENT_ERROR_NONE);
        IdeDocument* diagnostic_document = ide_document_model_find(&model, a_path);
        BUSTER_TEST(arguments, diagnostic_document && diagnostic_document->diagnostic_count == 2);
        u64 first_diagnostic_identity = 0;
        u64 second_diagnostic_identity = 0;
        if (diagnostic_document && diagnostic_document->diagnostic_count == 2)
        {
            BUSTER_STRING_TEST(arguments, diagnostic_document->diagnostics[0].message, S8("first"));
            BUSTER_STRING_TEST(arguments, diagnostic_document->diagnostics[1].message, S8("second"));
            first_diagnostic_identity = diagnostic_document->diagnostics[0].identity;
            second_diagnostic_identity = diagnostic_document->diagnostics[1].identity;
        }
        BUSTER_TEST(arguments, first_diagnostic_identity != 0 && second_diagnostic_identity != 0);
        BUSTER_TEST(arguments, ide_document_model_replace_diagnostics(&model, diagnostics, BUSTER_ARRAY_LENGTH(diagnostics)) ==
                                  IDE_DOCUMENT_ERROR_NONE);
        diagnostic_document = ide_document_model_find(&model, a_path);
        BUSTER_TEST(arguments, diagnostic_document && diagnostic_document->diagnostic_count == 2);
        if (diagnostic_document && diagnostic_document->diagnostic_count == 2)
        {
            BUSTER_TEST(arguments, diagnostic_document->diagnostics[0].identity == first_diagnostic_identity &&
                                      diagnostic_document->diagnostics[1].identity == second_diagnostic_identity);
        }
        BUSTER_TEST(arguments, ide_document_model_replace_diagnostics(&model, diagnostics + 1, 1) == IDE_DOCUMENT_ERROR_NONE);
        diagnostic_document = ide_document_model_find(&model, a_path);
        BUSTER_TEST(arguments, diagnostic_document && diagnostic_document->diagnostic_count == 1);
        IdeDocument* z_document = ide_document_model_find(&model, z_path);
        BUSTER_TEST(arguments, z_document && z_document->diagnostic_count == 0);

        IdeDocumentViewState view = {
            .cursor_offset = 3,
            .selection_start = 1,
            .selection_end = 3,
            .scroll_x = 4.0f,
            .scroll_y = 5.0f,
            .zoom = 1.25f,
        };
        BUSTER_TEST(arguments, ide_document_model_set_view(&model, a_path, view) == IDE_DOCUMENT_ERROR_NONE);
        diagnostic_document = ide_document_model_find(&model, a_path);
        BUSTER_TEST(arguments, diagnostic_document && diagnostic_document->view.cursor_offset == 3 && diagnostic_document->view.zoom == 1.25f);

        u64 long_view_length = diagnostic_document ? diagnostic_document->source.length : 0;
        BUSTER_TEST(arguments, ide_document_model_set_view(&model, a_path,
                                                           (IdeDocumentViewState){
                                                               .cursor_offset = long_view_length,
                                                               .selection_start = long_view_length ? long_view_length - 1 : 0,
                                                               .selection_end = long_view_length,
                                                           }) == IDE_DOCUMENT_ERROR_NONE);
        String8 set_text_short_source = S8("code s : fn () s32 { return 0; }\n");
        BUSTER_TEST(arguments, set_text_short_source.length < long_view_length);
        BUSTER_TEST(arguments, ide_document_model_set_text(&model, a_path, set_text_short_source) == IDE_DOCUMENT_ERROR_NONE);
        diagnostic_document = ide_document_model_find(&model, a_path);
        BUSTER_TEST(arguments, diagnostic_document && diagnostic_document->view.cursor_offset <= diagnostic_document->source.length &&
                                  diagnostic_document->view.selection_start <= diagnostic_document->view.selection_end &&
                                  diagnostic_document->view.selection_end <= diagnostic_document->source.length);

        u64 reload_view_length = diagnostic_document ? diagnostic_document->source.length : 0;
        BUSTER_TEST(arguments, ide_document_model_set_view(&model, a_path,
                                                           (IdeDocumentViewState){
                                                               .cursor_offset = reload_view_length,
                                                               .selection_start = reload_view_length ? reload_view_length - 1 : 0,
                                                               .selection_end = reload_view_length,
                                                           }) == IDE_DOCUMENT_ERROR_NONE);
        String8 reload_short_source = S8("code r : fn () s32 { return 0; }");
        BUSTER_TEST(arguments, reload_short_source.length < reload_view_length);
        BUSTER_TEST(arguments, file_write(a_path, BUSTER_SLICE_TO_BYTE_SLICE(reload_short_source)));
        BUSTER_TEST(arguments, ide_document_model_reload(&model, a_path, IDE_DOCUMENT_RELOAD_DISCARD_DIRTY) == IDE_DOCUMENT_ERROR_NONE);
        diagnostic_document = ide_document_model_find(&model, a_path);
        BUSTER_TEST(arguments, diagnostic_document && diagnostic_document->view.cursor_offset <= diagnostic_document->source.length &&
                                  diagnostic_document->view.selection_start <= diagnostic_document->view.selection_end &&
                                  diagnostic_document->view.selection_end <= diagnostic_document->source.length);

        u64 refresh_view_length = diagnostic_document ? diagnostic_document->source.length : 0;
        BUSTER_TEST(arguments, ide_document_model_set_view(&model, a_path,
                                                           (IdeDocumentViewState){
                                                               .cursor_offset = refresh_view_length,
                                                               .selection_start = refresh_view_length ? refresh_view_length - 1 : 0,
                                                               .selection_end = refresh_view_length,
                                                           }) == IDE_DOCUMENT_ERROR_NONE);
        String8 refresh_short_source = S8("code q : fn () s32 {return 0;}");
        BUSTER_TEST(arguments, refresh_short_source.length < refresh_view_length);
        BUSTER_TEST(arguments, file_write(a_path, BUSTER_SLICE_TO_BYTE_SLICE(refresh_short_source)));
        BUSTER_TEST(arguments, ide_document_model_refresh_workspace(&model) == IDE_DOCUMENT_ERROR_NONE);
        diagnostic_document = ide_document_model_find(&model, a_path);
        BUSTER_TEST(arguments, diagnostic_document && diagnostic_document->view.cursor_offset <= diagnostic_document->source.length &&
                                  diagnostic_document->view.selection_start <= diagnostic_document->view.selection_end &&
                                  diagnostic_document->view.selection_end <= diagnostic_document->source.length);

        IdeDocumentCompileMetadata compile = {
            .status = IDE_DOCUMENT_COMPILE_SUCCEEDED,
            .compiled_revision = diagnostic_document ? diagnostic_document->revision : 0,
            .artifact_hash = 123,
            .artifact_path = S8("build/a.o"),
            .command_line = S8("ide compile a.bbb"),
            .message = S8("ok"),
        };
        BUSTER_TEST(arguments, ide_document_model_set_compile_metadata(&model, a_path, compile) == IDE_DOCUMENT_ERROR_NONE);
        IdeDocumentSearchState search = {
            .query = S8("return"),
            .replacement = S8("yield"),
            .match_count = 2,
            .case_sensitive = true,
            .whole_word = true,
        };
        BUSTER_TEST(arguments, ide_document_model_set_search_state(&model, a_path, search) == IDE_DOCUMENT_ERROR_NONE);
        BUSTER_TEST(arguments, ide_document_model_set_filter_state(&model, (IdeDocumentWorkspaceFilterState){
                                                                         .query = S8("bbb"),
                                                                         .show_dirty_only = true,
                                                                     }) == IDE_DOCUMENT_ERROR_NONE);
        diagnostic_document = ide_document_model_find(&model, a_path);
        BUSTER_TEST(arguments, diagnostic_document != 0);
        if (diagnostic_document)
        {
            BUSTER_STRING_TEST(arguments, diagnostic_document->compile.artifact_path, S8("build/a.o"));
            BUSTER_STRING_TEST(arguments, diagnostic_document->search.query, S8("return"));
        }
        BUSTER_STRING_TEST(arguments, model.workspace.filter.query, S8("bbb"));
        BUSTER_TEST(arguments, model.workspace.filter.show_dirty_only);

        String8 reload_source = diagnostic_document ? diagnostic_document->source : (String8){0};
        BUSTER_TEST(arguments, ide_document_model_refresh_workspace(&model) == IDE_DOCUMENT_ERROR_NONE);
        diagnostic_document = ide_document_model_find(&model, a_path);
        if (diagnostic_document)
        {
            BUSTER_STRING_TEST(arguments, diagnostic_document->source, reload_source);
            BUSTER_TEST(arguments, diagnostic_document->is_open);
        }

#if defined(__linux__) || defined(__APPLE__)
        String8 sibling_prefix_file = string_format_z(test_arena, S8("{S8}\\outside.bbb"), root);
        String8 in_root_backslash_file = string_format_z(test_arena, S8("{S8}/inside\\name.bbb"), root);
        String8 backslash_source = S8("code backslash : fn () s32 { return 7; }\n");
        BUSTER_TEST(arguments, !ide_document_path_is_within(root, sibling_prefix_file));
        BUSTER_TEST(arguments, file_write(sibling_prefix_file, BUSTER_SLICE_TO_BYTE_SLICE(S8("outside sibling"))));
        BUSTER_TEST(arguments, ide_document_model_open(&model, sibling_prefix_file) == IDE_DOCUMENT_ERROR_PATH_OUTSIDE_ROOT);
        BUSTER_TEST(arguments, ide_document_model_save(&model, sibling_prefix_file) == IDE_DOCUMENT_ERROR_PATH_OUTSIDE_ROOT);
        BUSTER_TEST(arguments, file_write(in_root_backslash_file, BUSTER_SLICE_TO_BYTE_SLICE(backslash_source)));
        BUSTER_TEST(arguments, ide_document_path_is_within(root, in_root_backslash_file));
        BUSTER_TEST(arguments, ide_document_model_refresh_workspace(&model) == IDE_DOCUMENT_ERROR_NONE);
        BUSTER_TEST(arguments, ide_document_model_document_count(&model) == 4);
        IdeDocument* in_root_backslash_document = ide_document_model_find(&model, in_root_backslash_file);
        BUSTER_TEST(arguments, in_root_backslash_document != 0);
        if (in_root_backslash_document)
        {
            BUSTER_STRING_TEST(arguments, in_root_backslash_document->source, backslash_source);
        }
        BUSTER_TEST(arguments, ide_document_model_open(&model, in_root_backslash_file) == IDE_DOCUMENT_ERROR_NONE);
        BUSTER_TEST(arguments, ide_document_model_close(&model, in_root_backslash_file) == IDE_DOCUMENT_ERROR_NONE);
        BUSTER_TEST(arguments, os_file_delete(in_root_backslash_file));
        BUSTER_TEST(arguments, os_file_delete(sibling_prefix_file));
        BUSTER_TEST(arguments, ide_document_model_refresh_workspace(&model) == IDE_DOCUMENT_ERROR_NONE);
        BUSTER_TEST(arguments, ide_document_model_document_count(&model) == 3);

        String8 outside_directory = ide_document_test_temporary_root(test_arena, S8("buster-ide-document-outside"));
        String8 outside_file = string_format_z(test_arena, S8("{S8}/outside.bbb"), outside_directory);
        String8 link_directory = string_format_z(test_arena, S8("{S8}/escape"), root);
        String8 safe_directory = string_format_z(test_arena, S8("{S8}/safe"), root);
        String8 nested_link_directory = string_format_z(test_arena, S8("{S8}/safe/link"), root);
        String8 escaped_file = string_format_z(test_arena, S8("{S8}/outside.bbb"), link_directory);
        String8 nested_escaped_file = string_format_z(test_arena, S8("{S8}/safe/link/outside.bbb"), root);
        String8 outside_source = S8("code outside : fn () s32 { return 9; }\n");
        os_directory_delete(outside_directory);
        os_file_delete(outside_file);
        os_file_delete(link_directory);
        os_file_delete(nested_link_directory);
        os_directory_delete(safe_directory);
        os_make_directory(outside_directory);
        os_make_directory(safe_directory);
        BUSTER_TEST(arguments, file_write(outside_file, BUSTER_SLICE_TO_BYTE_SLICE(outside_source)));
        BUSTER_TEST(arguments, symlink((const char*)outside_directory.pointer, (const char*)link_directory.pointer) == 0);
        BUSTER_TEST(arguments, symlink((const char*)outside_directory.pointer, (const char*)nested_link_directory.pointer) == 0);
        String8 missing_reparse_file = string_format_z(test_arena, S8("{S8}/missing.bbb"), link_directory);
        String8 reparse_import_source = S8("import missing = \"escape/missing-leaf\";\ncode main : fn () s32 { return 0; }\n");
        BUSTER_TEST(arguments, ide_document_model_set_text(&model, a_path, reparse_import_source) == IDE_DOCUMENT_ERROR_NONE);
        IdeDocumentImport* reparse_import = ide_document_model_import_at(&model, 0);
        BUSTER_TEST(arguments, reparse_import && reparse_import->state == IDE_DOCUMENT_IMPORT_MISSING &&
                                  reparse_import->target_path.length == 0);
        BUSTER_TEST(arguments, ide_document_model_set_text(&model, a_path, a_source) == IDE_DOCUMENT_ERROR_NONE);
        BUSTER_TEST(arguments, ide_document_model_open(&model, escaped_file) == IDE_DOCUMENT_ERROR_PATH_OUTSIDE_ROOT);
        BUSTER_TEST(arguments, ide_document_model_open(&model, missing_reparse_file) == IDE_DOCUMENT_ERROR_PATH_OUTSIDE_ROOT);
        BUSTER_TEST(arguments, ide_document_model_save(&model, escaped_file) == IDE_DOCUMENT_ERROR_PATH_OUTSIDE_ROOT);
        BUSTER_TEST(arguments, ide_document_model_open(&model, nested_escaped_file) == IDE_DOCUMENT_ERROR_PATH_OUTSIDE_ROOT);
        BUSTER_TEST(arguments, ide_document_model_save(&model, nested_escaped_file) == IDE_DOCUMENT_ERROR_PATH_OUTSIDE_ROOT);
        BUSTER_TEST(arguments, ide_document_model_poll_external(&model, nested_escaped_file) == IDE_DOCUMENT_ERROR_PATH_OUTSIDE_ROOT);
        BUSTER_TEST(arguments, ide_document_model_reload(&model, nested_escaped_file, IDE_DOCUMENT_RELOAD_DISCARD_DIRTY) ==
                                  IDE_DOCUMENT_ERROR_PATH_OUTSIDE_ROOT);
        BUSTER_TEST(arguments, ide_document_model_refresh_workspace(&model) == IDE_DOCUMENT_ERROR_NONE);
        ByteSlice outside_bytes = file_read(test_arena, outside_file, (FileReadOptions){0});
        BUSTER_STRING_TEST(arguments, BYTE_SLICE_TO_STRING(8, outside_bytes), outside_source);
        BUSTER_TEST(arguments, ide_document_model_document_count(&model) == 3);
        BUSTER_TEST(arguments, os_file_delete(link_directory));
        BUSTER_TEST(arguments, os_file_delete(nested_link_directory));
        BUSTER_TEST(arguments, os_directory_delete(safe_directory));
        BUSTER_TEST(arguments, os_file_delete(outside_file));
        BUSTER_TEST(arguments, os_directory_delete(outside_directory));
#elif defined(_WIN32)
        String8 outside_directory = ide_document_test_temporary_root(test_arena, S8("buster-ide-document-outside"));
        String8 outside_file = string_format_z(test_arena, S8("{S8}/outside.bbb"), outside_directory);
        String8 link_directory = string_format_z(test_arena, S8("{S8}/escape"), root);
        String8 safe_directory = string_format_z(test_arena, S8("{S8}/safe"), root);
        String8 nested_link_directory = string_format_z(test_arena, S8("{S8}/safe/link"), root);
        String8 escaped_file = string_format_z(test_arena, S8("{S8}/outside.bbb"), link_directory);
        String8 nested_escaped_file = string_format_z(test_arena, S8("{S8}/safe/link/outside.bbb"), root);
        String8 outside_source = S8("code outside : fn () s32 { return 9; }\n");
        BUSTER_TEST(arguments, !ide_document_path_is_within(root, outside_directory));
        BUSTER_TEST(arguments, !ide_document_path_is_within(root, outside_file));
        os_directory_delete(outside_directory);
        os_file_delete(outside_file);
        os_directory_delete(safe_directory);
        os_make_directory(outside_directory);
        os_make_directory(safe_directory);
        BUSTER_TEST(arguments, file_write(outside_file, BUSTER_SLICE_TO_BYTE_SLICE(outside_source)));
        String16 outside_w = string16_from_string8(test_arena, outside_directory, true);
        String16 link_w = string16_from_string8(test_arena, link_directory, true);
        String16 nested_link_w = string16_from_string8(test_arena, nested_link_directory, true);
        bool created_reparse = CreateSymbolicLinkW(link_w.pointer, outside_w.pointer, SYMBOLIC_LINK_FLAG_DIRECTORY) != 0;
        bool created_nested_reparse = CreateSymbolicLinkW(nested_link_w.pointer, outside_w.pointer, SYMBOLIC_LINK_FLAG_DIRECTORY) != 0;
        String8 missing_reparse_file = string_format_z(test_arena, S8("{S8}/missing.bbb"), link_directory);
        if (created_reparse)
        {
            String8 reparse_import_source = S8("import missing = \"escape/missing-leaf\";\ncode main : fn () s32 { return 0; }\n");
            BUSTER_TEST(arguments, ide_document_model_set_text(&model, a_path, reparse_import_source) == IDE_DOCUMENT_ERROR_NONE);
            IdeDocumentImport* reparse_import = ide_document_model_import_at(&model, 0);
            BUSTER_TEST(arguments, reparse_import && reparse_import->state == IDE_DOCUMENT_IMPORT_MISSING &&
                                      reparse_import->target_path.length == 0);
            BUSTER_TEST(arguments, ide_document_model_set_text(&model, a_path, a_source) == IDE_DOCUMENT_ERROR_NONE);
            BUSTER_TEST(arguments, ide_document_model_open(&model, escaped_file) == IDE_DOCUMENT_ERROR_PATH_OUTSIDE_ROOT);
            BUSTER_TEST(arguments, ide_document_model_open(&model, missing_reparse_file) == IDE_DOCUMENT_ERROR_PATH_OUTSIDE_ROOT);
            BUSTER_TEST(arguments, ide_document_model_save(&model, escaped_file) == IDE_DOCUMENT_ERROR_PATH_OUTSIDE_ROOT);
        }
        if (created_nested_reparse)
        {
            BUSTER_TEST(arguments, ide_document_model_open(&model, nested_escaped_file) == IDE_DOCUMENT_ERROR_PATH_OUTSIDE_ROOT);
            BUSTER_TEST(arguments, ide_document_model_save(&model, nested_escaped_file) == IDE_DOCUMENT_ERROR_PATH_OUTSIDE_ROOT);
            BUSTER_TEST(arguments, ide_document_model_poll_external(&model, nested_escaped_file) == IDE_DOCUMENT_ERROR_PATH_OUTSIDE_ROOT);
            BUSTER_TEST(arguments, ide_document_model_reload(&model, nested_escaped_file, IDE_DOCUMENT_RELOAD_DISCARD_DIRTY) ==
                                      IDE_DOCUMENT_ERROR_PATH_OUTSIDE_ROOT);
            BUSTER_TEST(arguments, ide_document_model_refresh_workspace(&model) == IDE_DOCUMENT_ERROR_NONE);
            ByteSlice outside_bytes = file_read(test_arena, outside_file, (FileReadOptions){0});
            BUSTER_STRING_TEST(arguments, BYTE_SLICE_TO_STRING(8, outside_bytes), outside_source);
            BUSTER_TEST(arguments, ide_document_model_document_count(&model) == 3);
        }
        if (created_reparse)
        {
            BUSTER_TEST(arguments, RemoveDirectoryW(link_w.pointer) != 0);
        }
        if (created_nested_reparse)
        {
            BUSTER_TEST(arguments, RemoveDirectoryW(nested_link_w.pointer) != 0);
        }
        BUSTER_TEST(arguments, RemoveDirectoryW(string16_from_string8(test_arena, safe_directory, true).pointer) != 0);
        BUSTER_TEST(arguments, os_file_delete(outside_file));
        BUSTER_TEST(arguments, os_directory_delete(outside_directory));
#endif
    }

    ide_document_model_deinitialize(&model);
    UnitTestResult incremental_analysis_result = ide_document_incremental_analysis_tests(arguments, test_arena, arena, staging_arena);
    result.succeeded_test_count += incremental_analysis_result.succeeded_test_count;
    result.test_count += incremental_analysis_result.test_count;
    {
        String8 second_root = ide_document_test_temporary_root(test_arena, S8("buster-ide-document-second-root"));
        String8 second_path = string_format_z(test_arena, S8("{S8}/second.bbb"), second_root);
        String8 second_source = S8("code Helper : fn () s32 { return 8; }\n");
        String8 missing_path = string_format_z(test_arena, S8("{S8}/missing.bbb"), second_root);
        u64 second_name_start = second_root.length;
        while (second_name_start && second_root.pointer[second_name_start - 1] != '/' && second_root.pointer[second_name_start - 1] != '\\')
        {
            second_name_start -= 1;
        }
        String8 second_name = string_slice(second_root, second_name_start, second_root.length);
        String8 relative_second_path = string_format_z(test_arena, S8("../{S8}/second.bbb"), second_name);
        os_directory_delete(second_root);
        os_make_directory(second_root);
        BUSTER_TEST(arguments, file_write(second_path, BUSTER_SLICE_TO_BYTE_SLICE(second_source)));

        BUSTER_TEST(arguments, ide_app_test_open_path(second_path) == IDE_DOCUMENT_ERROR_NONE);
        BUSTER_TEST(arguments, ide_app_test_bad_open_preserves_empty(missing_path));
        BUSTER_TEST(arguments, ide_app_test_reload_discard(second_path, second_source,
                                                           S8("code dirty from the application bridge : fn () s32 { return 9; }\n")));
        BUSTER_TEST(arguments, ide_app_test_failed_editor_commit_preserves_buffer(
                                  second_path, S8("code broken from the editor buffer : fn () s32 { return unknown; }\n")));
        BUSTER_TEST(arguments, file_write(second_path, BUSTER_SLICE_TO_BYTE_SLICE(second_source)));
        BUSTER_TEST(arguments, ide_app_test_scroll_round_trip(a_path, b_path));
        BUSTER_TEST(arguments, ide_app_test_filter_state(second_path, S8("Helper")));
        BUSTER_TEST(arguments, ide_app_test_oversized_request_preserves_action(second_path));
        BUSTER_TEST(arguments, ide_app_test_copy_storage_self());

        // The graphical bridge starts with no selected root or active document.
        IdeDocumentModel empty_model = {0};
        BUSTER_TEST(arguments, ide_document_model_initialize(&empty_model, arena, staging_arena, (IdeDocumentModelOptions){0}) ==
                                  IDE_DOCUMENT_ERROR_NONE);
        BUSTER_TEST(arguments, empty_model.workspace.root_path.length == 0 && empty_model.workspace.documents == 0 &&
                                  ide_document_model_document_count(&empty_model) == 0 &&
                                  ide_document_model_active_document(&empty_model) == 0);
        IdeDocumentWorkspaceStatus empty_status = ide_document_model_status(&empty_model);
        BUSTER_TEST(arguments, empty_status.document_count == 0 && empty_status.open_document_count == 0 &&
                                  ide_document_model_find(&empty_model, missing_path) == 0 &&
                                  ide_document_model_document_at(&empty_model, 0) == 0);
        BUSTER_TEST(arguments, ide_document_model_open_path(&empty_model, missing_path) == IDE_DOCUMENT_ERROR_PATH_NOT_FOUND);
        BUSTER_TEST(arguments, empty_model.workspace.root_path.length == 0 && ide_document_model_active_document(&empty_model) == 0);
        BUSTER_TEST(arguments, ide_document_model_open_path(&empty_model, second_path) == IDE_DOCUMENT_ERROR_NONE);
        BUSTER_TEST(arguments, empty_model.workspace.root_path.length != 0 && ide_document_model_active_document(&empty_model) != 0);
        ide_document_model_deinitialize(&empty_model);

        // A successful open of a file in another directory stages a complete
        // candidate before swapping it into the committed model.
        BUSTER_TEST(arguments, ide_document_model_initialize(&model, arena, staging_arena, (IdeDocumentModelOptions){0}) ==
                                  IDE_DOCUMENT_ERROR_NONE);
        BUSTER_TEST(arguments, ide_document_model_open_path(&model, a_path) == IDE_DOCUMENT_ERROR_NONE);
        IdeDocument* first_root_active = ide_document_model_active_document(&model);
        String8 first_root_name = string_duplicate_arena(test_arena, model.workspace.root_path, true);
        BUSTER_TEST(arguments, first_root_active && string_ends_with_sequence(first_root_active->path, S8("/a.bbb")));

        BUSTER_TEST(arguments, ide_document_model_open(&model, b_path) == IDE_DOCUMENT_ERROR_NONE);
        String8 dirty_non_active_source = S8("code dirty non-active : fn () s32 { return 10; }\n");
        BUSTER_TEST(arguments, ide_document_model_set_text(&model, b_path, dirty_non_active_source) == IDE_DOCUMENT_ERROR_NONE);
        BUSTER_TEST(arguments, ide_document_model_set_active(&model, a_path) == IDE_DOCUMENT_ERROR_NONE);
        IdeDocument* clean_active_before_dirty_replacement = ide_document_model_active_document(&model);
        IdeDocument* dirty_non_active_before_replacement = ide_document_model_find(&model, b_path);
        IdeDocument* documents_before_dirty_replacement = model.workspace.documents;
        IdeDocumentImport* imports_before_dirty_replacement = model.workspace.imports;
        IdeDocumentEntitySnapshot* entities_before_dirty_replacement = model.workspace.entities;
        String8 root_before_dirty_replacement = model.workspace.root_path;
        String8 clean_source_before_dirty_replacement = clean_active_before_dirty_replacement ? clean_active_before_dirty_replacement->source : (String8){0};
        String8 dirty_source_before_dirty_replacement = dirty_non_active_before_replacement ? dirty_non_active_before_replacement->source : (String8){0};
#if defined(_WIN32)
        String8 missing_same_root_path = string_format_z(test_arena, S8("{S8}\\not-created\\missing.bbb"), model.workspace.root_path);
        String8 missing_final_path = string_format_z(test_arena, S8("{S8}\\missing-final.bbb"), subdirectory);
#else
        String8 missing_same_root_path = string_format_z(test_arena, S8("{S8}/not-created/missing.bbb"), model.workspace.root_path);
        String8 missing_final_path = string_format_z(test_arena, S8("{S8}/missing-final.bbb"), subdirectory);
#endif
        BUSTER_TEST(arguments, ide_document_path_is_within(model.workspace.root_path, missing_same_root_path));
        BUSTER_TEST(arguments, ide_document_model_open_path(&model, missing_same_root_path) == IDE_DOCUMENT_ERROR_PATH_NOT_FOUND);
        BUSTER_TEST(arguments, ide_document_path_is_within(model.workspace.root_path, missing_final_path));
        BUSTER_TEST(arguments, ide_document_model_open_path(&model, missing_final_path) == IDE_DOCUMENT_ERROR_PATH_NOT_FOUND);
        BUSTER_TEST(arguments, model.workspace.documents == documents_before_dirty_replacement &&
                                  ide_document_model_active_document(&model) == clean_active_before_dirty_replacement &&
                                  ide_document_model_find(&model, b_path) == dirty_non_active_before_replacement);
        IdeDocumentErrorKind dirty_replacement_error = ide_document_model_open_path(&model, relative_second_path);
        BUSTER_TEST(arguments, dirty_replacement_error == IDE_DOCUMENT_ERROR_DIRTY_WORKSPACE_REPLACEMENT);
        BUSTER_TEST(arguments, model.workspace.documents == documents_before_dirty_replacement && model.workspace.imports == imports_before_dirty_replacement &&
                                  model.workspace.entities == entities_before_dirty_replacement && model.workspace.root_path.pointer == root_before_dirty_replacement.pointer &&
                                  ide_document_model_active_document(&model) == clean_active_before_dirty_replacement &&
                                  ide_document_model_find(&model, b_path) == dirty_non_active_before_replacement);
        BUSTER_TEST(arguments, ide_document_model_active_document(&model) &&
                                  string_equal(ide_document_model_active_document(&model)->source, clean_source_before_dirty_replacement));
        dirty_non_active_before_replacement = ide_document_model_find(&model, b_path);
        BUSTER_TEST(arguments, dirty_non_active_before_replacement &&
                                  string_equal(dirty_non_active_before_replacement->source, dirty_source_before_dirty_replacement) &&
                                  dirty_non_active_before_replacement->dirty);
        BUSTER_TEST(arguments, ide_document_model_set_text(&model, b_path, b_source) == IDE_DOCUMENT_ERROR_NONE);
        BUSTER_TEST(arguments, ide_document_model_set_active(&model, a_path) == IDE_DOCUMENT_ERROR_NONE);

        // The candidate used to be a scratch pointer after root comparison.
        // Clobbering that scratch must not affect a relative open that selects
        // the sibling workspace root.
        ide_document_model_test_set_open_path_scratch_clobber(true);
        IdeDocumentErrorKind relative_open_error = ide_document_model_open_path(&model, relative_second_path);
        ide_document_model_test_set_open_path_scratch_clobber(false);
        BUSTER_TEST(arguments, relative_open_error == IDE_DOCUMENT_ERROR_NONE);
        BUSTER_TEST(arguments, string_equal(model.workspace.root_path, second_root) &&
                                  ide_document_model_active_document(&model) &&
                                  string_ends_with_sequence(ide_document_model_active_document(&model)->path, S8("/second.bbb")));
        BUSTER_TEST(arguments, ide_document_model_open_path(&model, a_path) == IDE_DOCUMENT_ERROR_NONE);

        // Entity activation copies the target path/range before opening the
        // closed document. The open-path scratch clobber makes same-scratch
        // reuse corruptions deterministic rather than allocator-dependent.
        u32 helper_entity_index = IDE_DOCUMENT_INDEX_INVALID;
        String8 helper_entity_path = {0};
        ParserSourceRange helper_entity_range = {0};
        for (u32 entity_index = 0; entity_index < model.workspace.entity_count; entity_index += 1)
        {
            IdeDocumentEntitySnapshot* entity = model.workspace.entities + entity_index;
            if (string_equal(entity->name, S8("helper")) && string_ends_with_sequence(entity->source_path, S8("/sub/b.bbb")))
            {
                helper_entity_index = entity_index;
                helper_entity_path = string_duplicate_arena(test_arena, entity->source_path, true);
                helper_entity_range = entity->range;
                break;
            }
        }
        BUSTER_TEST(arguments, helper_entity_index != IDE_DOCUMENT_INDEX_INVALID && helper_entity_path.length != 0);
        BUSTER_TEST(arguments, ide_document_model_open(&model, helper_entity_path) == IDE_DOCUMENT_ERROR_NONE);
        BUSTER_TEST(arguments, ide_document_model_set_view(&model, helper_entity_path,
                                                           (IdeDocumentViewState){.scroll_x = 37.0f, .scroll_y = 91.0f}) ==
                                  IDE_DOCUMENT_ERROR_NONE);
        BUSTER_TEST(arguments, ide_document_model_set_active(&model, a_path) == IDE_DOCUMENT_ERROR_NONE);
        BUSTER_TEST(arguments, ide_document_model_close(&model, helper_entity_path) == IDE_DOCUMENT_ERROR_NONE);
        BUSTER_TEST(arguments, ide_document_model_find(&model, helper_entity_path) &&
                                  !ide_document_model_find(&model, helper_entity_path)->is_open);

        // A closed, clean indexed document must validate against the exact
        // source staged from disk. A stale in-memory length used to let this
        // activation commit a shortened file and only then reject the view.
        IdeDocument* stale_active_before = ide_document_model_active_document(&model);
        IdeDocument* stale_documents_before = model.workspace.documents;
        IdeDocumentImport* stale_imports_before = model.workspace.imports;
        IdeDocumentEntitySnapshot* stale_entities_before = model.workspace.entities;
        String8 stale_root_before = model.workspace.root_path;
        IdeDocument* stale_target_before = ide_document_model_find(&model, helper_entity_path);
        String8 stale_source_before = stale_target_before ? string_duplicate_arena(test_arena, stale_target_before->source, true) : (String8){0};
        String8 stale_short_source = S8("code helper : fn () s32 { return 0; }\n");
        BUSTER_TEST(arguments, stale_target_before && !stale_target_before->is_open && stale_source_before.length > stale_short_source.length);
        BUSTER_TEST(arguments, file_write(b_path, BUSTER_SLICE_TO_BYTE_SLICE(stale_short_source)));
        IdeDocumentErrorKind stale_activation_error = ide_document_model_activate_source_range(
            &model, helper_entity_path, (ParserSourceRange){.offset = (u32)stale_source_before.length, .length = 0});
        BUSTER_TEST(arguments, stale_activation_error == IDE_DOCUMENT_ERROR_INVALID_SELECTION);
        BUSTER_TEST(arguments, model.workspace.documents == stale_documents_before && model.workspace.imports == stale_imports_before &&
                                  model.workspace.entities == stale_entities_before && model.workspace.root_path.pointer == stale_root_before.pointer &&
                                  ide_document_model_active_document(&model) == stale_active_before &&
                                  ide_document_model_find(&model, helper_entity_path) == stale_target_before);
        stale_target_before = ide_document_model_find(&model, helper_entity_path);
        BUSTER_TEST(arguments, stale_target_before && !stale_target_before->is_open);
        if (stale_target_before)
        {
            BUSTER_STRING_TEST(arguments, stale_target_before->source, stale_source_before);
        }
        BUSTER_TEST(arguments, file_write(b_path, BUSTER_SLICE_TO_BYTE_SLICE(b_source)));

        // Invalid source ranges are rejected before a closed target can be
        // opened or the active document can be changed. Failed activation
        // therefore preserves committed arena pointers exactly.
        IdeDocument* invalid_range_active_before = ide_document_model_active_document(&model);
        IdeDocument* invalid_range_documents_before = model.workspace.documents;
        IdeDocument* invalid_range_target_before = ide_document_model_find(&model, helper_entity_path);
        u32 invalid_range_offset = 1;
        if (invalid_range_target_before && invalid_range_target_before->source.length < UINT32_MAX)
        {
            invalid_range_offset = (u32)(invalid_range_target_before->source.length + 1);
        }
        IdeDocumentErrorKind invalid_range_error = ide_document_model_activate_source_range(
            &model, helper_entity_path, (ParserSourceRange){.offset = invalid_range_offset, .length = 0});
        BUSTER_TEST(arguments, invalid_range_error == IDE_DOCUMENT_ERROR_INVALID_SELECTION);
        BUSTER_TEST(arguments, model.workspace.documents == invalid_range_documents_before &&
                                  ide_document_model_active_document(&model) == invalid_range_active_before &&
                                  ide_document_model_find(&model, helper_entity_path) == invalid_range_target_before);

        ide_document_model_test_set_open_path_scratch_clobber(true);
        IdeDocumentErrorKind helper_activation_error = ide_document_model_activate_entity(&model, helper_entity_index);
        ide_document_model_test_set_open_path_scratch_clobber(false);
        BUSTER_TEST(arguments, helper_activation_error == IDE_DOCUMENT_ERROR_NONE);
        IdeDocument* helper_active = ide_document_model_active_document(&model);
        BUSTER_TEST(arguments, helper_active && string_equal(helper_active->path, helper_entity_path));
        BUSTER_TEST(arguments, helper_active && helper_active->view.cursor_offset == helper_entity_range.offset &&
                                  helper_active->view.selection_start == helper_entity_range.offset &&
                                  helper_active->view.selection_end == (u64)helper_entity_range.offset + helper_entity_range.length &&
                                  helper_active->view.scroll_x == 37.0f && helper_active->view.scroll_y == 91.0f);
        BUSTER_TEST(arguments, ide_document_model_set_filter_state(&model, (IdeDocumentWorkspaceFilterState){0}) == IDE_DOCUMENT_ERROR_NONE);
        helper_active = ide_document_model_active_document(&model);
        BUSTER_TEST(arguments, helper_active && string_equal(helper_active->path, helper_entity_path) &&
                                  helper_active->view.selection_end == (u64)helper_entity_range.offset + helper_entity_range.length);
        BUSTER_TEST(arguments, ide_document_model_open_path(&model, a_path) == IDE_DOCUMENT_ERROR_NONE);

        BUSTER_TEST(arguments, ide_document_model_open_path(&model, S8("sub/b.bbb")) == IDE_DOCUMENT_ERROR_NONE);
        BUSTER_TEST(arguments, ide_document_model_active_document(&model) &&
                                  string_ends_with_sequence(ide_document_model_active_document(&model)->path, S8("/sub/b.bbb")));
        BUSTER_TEST(arguments, ide_document_model_open_path(&model, a_path) == IDE_DOCUMENT_ERROR_NONE);
        first_root_active = ide_document_model_active_document(&model);
        BUSTER_TEST(arguments, ide_document_model_open_path(&model, second_path) == IDE_DOCUMENT_ERROR_NONE);
        IdeDocument* second_root_active = ide_document_model_active_document(&model);
        BUSTER_TEST(arguments, second_root_active && string_ends_with_sequence(second_root_active->path, S8("/second.bbb")));
        BUSTER_TEST(arguments, !string_equal(model.workspace.root_path, first_root_name));
        BUSTER_TEST(arguments, second_root_active != first_root_active);

        // Failed opens leave both the active document and its arena-backed
        // source/diagnostic/entity snapshots untouched.
        IdeDocument* active_before_failed_open = second_root_active;
        String8 source_before_failed_open = second_root_active ? second_root_active->source : (String8){0};
        IdeDocumentEntitySnapshot* entity_before_failed_open = model.workspace.entity_count ? model.workspace.entities : 0;
        BUSTER_TEST(arguments, ide_document_model_open_path(&model, missing_path) == IDE_DOCUMENT_ERROR_PATH_NOT_FOUND);
        second_root_active = ide_document_model_active_document(&model);
        BUSTER_TEST(arguments, second_root_active == active_before_failed_open);
        BUSTER_TEST(arguments, second_root_active && string_equal(second_root_active->source, source_before_failed_open));
        BUSTER_TEST(arguments, (!entity_before_failed_open && !model.workspace.entity_count) ||
                                  (entity_before_failed_open && model.workspace.entities == entity_before_failed_open));

        String8 drop_txt = string_format_z(test_arena, S8("{S8}/notes.txt"), second_root);
        String8 drop_first_bbb = string_format_z(test_arena, S8("{S8}/first.BBB"), second_root);
        String8 drop_paths_storage[] = {drop_txt, drop_first_bbb, second_path};
        IdeDocumentDropSelection drop = ide_document_choose_drop_path(
            test_arena, (SliceString8){.pointer = drop_paths_storage, .length = BUSTER_ARRAY_LENGTH(drop_paths_storage)});
        BUSTER_TEST(arguments, drop.accepted && string_equal(drop.path, drop_first_bbb) && drop.path_count == 3 && drop.ignored_count == 2);
        String8 no_bbb_paths_storage[] = {drop_txt, S8("README")};
        IdeDocumentDropSelection rejected_drop = ide_document_choose_drop_path(
            test_arena, (SliceString8){.pointer = no_bbb_paths_storage, .length = BUSTER_ARRAY_LENGTH(no_bbb_paths_storage)});
        BUSTER_TEST(arguments, !rejected_drop.accepted && rejected_drop.path_count == 2 && rejected_drop.ignored_count == 2);
        String8 bridge_drop_paths[] = {drop_txt, drop_first_bbb, second_path};
        String8 later_bridge_drop_paths[] = {second_path};
        BUSTER_TEST(arguments, ide_app_test_drop_preserves_first(
                                  (SliceString8){.pointer = bridge_drop_paths, .length = BUSTER_ARRAY_LENGTH(bridge_drop_paths)},
                                  (SliceString8){.pointer = later_bridge_drop_paths, .length = BUSTER_ARRAY_LENGTH(later_bridge_drop_paths)}, drop_first_bbb));
        BUSTER_TEST(arguments, ide_document_path_is_bbb(S8("source.BBB")) && !ide_document_path_is_bbb(S8("source.c")));
        u8 control_modifier = (u8)(1u << WM_MODIFIER_CONTROL);
        u8 shift_modifier = (u8)(1u << WM_MODIFIER_SHIFT);
        u8 alt_modifier = (u8)(1u << WM_MODIFIER_ALT);
        u8 shortcut_disallowed_modifiers = (u8)(shift_modifier | alt_modifier);
        BUSTER_TEST(arguments, ide_document_shortcut_action((u8)'o', control_modifier, WM_MODIFIER_CONTROL,
                                                            shortcut_disallowed_modifiers) == IDE_DOCUMENT_SHORTCUT_OPEN);
        BUSTER_TEST(arguments, ide_document_shortcut_action((u8)'s', control_modifier, WM_MODIFIER_CONTROL,
                                                            shortcut_disallowed_modifiers) == IDE_DOCUMENT_SHORTCUT_SAVE);
        BUSTER_TEST(arguments, ide_document_shortcut_action((u8)'r', control_modifier, WM_MODIFIER_CONTROL,
                                                            shortcut_disallowed_modifiers) == IDE_DOCUMENT_SHORTCUT_RELOAD);
        BUSTER_TEST(arguments, ide_document_shortcut_action((u8)'o', 0, WM_MODIFIER_CONTROL, shortcut_disallowed_modifiers) ==
                                  IDE_DOCUMENT_SHORTCUT_NONE);
        BUSTER_TEST(arguments, ide_document_shortcut_action((u8)'s', (u8)(control_modifier | shift_modifier), WM_MODIFIER_CONTROL,
                                                            shortcut_disallowed_modifiers) == IDE_DOCUMENT_SHORTCUT_NONE);
        BUSTER_TEST(arguments, ide_document_shortcut_action((u8)'r', (u8)(control_modifier | alt_modifier), WM_MODIFIER_CONTROL,
                                                            shortcut_disallowed_modifiers) == IDE_DOCUMENT_SHORTCUT_NONE);

        BUSTER_TEST(arguments, ide_document_string_contains(S8("HelperFunction"), S8("helper"), false));
        BUSTER_TEST(arguments, !ide_document_string_contains(S8("HelperFunction"), S8("helper"), true));
        BUSTER_TEST(arguments, ide_document_model_set_filter_state(&model, (IdeDocumentWorkspaceFilterState){
                                                                         .query = S8("SECOND"),
                                                                         .case_sensitive = false,
                                                                     }) == IDE_DOCUMENT_ERROR_NONE);
        BUSTER_TEST(arguments, ide_document_model_document_matches_filter(&model, 0));
        IdeDocument* filter_active_before_unsupported = ide_document_model_active_document(&model);
        String8 filter_query_before_unsupported = model.workspace.filter.query;
        BUSTER_TEST(arguments, ide_document_model_set_filter_state(&model, (IdeDocumentWorkspaceFilterState){
                                                                         .query = S8("ignored"),
                                                                         .whole_word = true,
                                                                     }) == IDE_DOCUMENT_ERROR_UNSUPPORTED_FILTER);
        BUSTER_TEST(arguments, ide_document_model_active_document(&model) == filter_active_before_unsupported &&
                                  model.workspace.filter.query.pointer == filter_query_before_unsupported.pointer);
        BUSTER_TEST(arguments, ide_document_model_set_filter_state(&model, (IdeDocumentWorkspaceFilterState){
                                                                         .query = S8("ignored"),
                                                                         .regular_expression = true,
                                                                     }) == IDE_DOCUMENT_ERROR_UNSUPPORTED_FILTER);
        BUSTER_TEST(arguments, ide_document_model_set_filter_state(&model, (IdeDocumentWorkspaceFilterState){
                                                                         .query = S8("HELPER"),
                                                                         .case_sensitive = false,
                                                                     }) == IDE_DOCUMENT_ERROR_NONE);
        bool found_helper = false;
        for (u32 entity_index = 0; entity_index < model.workspace.entity_count; entity_index += 1)
        {
            found_helper |= ide_document_model_entity_matches_filter(&model, entity_index);
        }
        BUSTER_TEST(arguments, found_helper);

        BUSTER_TEST(arguments, ide_document_model_set_filter_state(&model, (IdeDocumentWorkspaceFilterState){.show_open_only = true}) ==
                                  IDE_DOCUMENT_ERROR_NONE);
        bool found_open_helper = false;
        for (u32 entity_index = 0; entity_index < model.workspace.entity_count; entity_index += 1)
        {
            found_open_helper |= ide_document_model_entity_matches_filter(&model, entity_index);
        }
        BUSTER_TEST(arguments, found_open_helper);
        BUSTER_TEST(arguments, ide_document_model_close(&model, second_path) == IDE_DOCUMENT_ERROR_NONE);
        bool found_closed_helper = false;
        for (u32 entity_index = 0; entity_index < model.workspace.entity_count; entity_index += 1)
        {
            found_closed_helper |= ide_document_model_entity_matches_filter(&model, entity_index);
        }
        BUSTER_TEST(arguments, !found_closed_helper);
        BUSTER_TEST(arguments, ide_document_model_open(&model, second_path) == IDE_DOCUMENT_ERROR_NONE);
        String8 dirty_entity_source = S8("code Helper : fn () s32 { return 11; }\n");
        BUSTER_TEST(arguments, ide_document_model_set_text(&model, second_path, dirty_entity_source) == IDE_DOCUMENT_ERROR_NONE);
        BUSTER_TEST(arguments, ide_document_model_set_filter_state(&model, (IdeDocumentWorkspaceFilterState){.show_dirty_only = true}) ==
                                  IDE_DOCUMENT_ERROR_NONE);
        bool found_dirty_helper = false;
        for (u32 entity_index = 0; entity_index < model.workspace.entity_count; entity_index += 1)
        {
            found_dirty_helper |= ide_document_model_entity_matches_filter(&model, entity_index);
        }
        BUSTER_TEST(arguments, found_dirty_helper);
        BUSTER_TEST(arguments, ide_document_model_set_text(&model, second_path, second_source) == IDE_DOCUMENT_ERROR_NONE);

        bool found_entity_snapshot = false;
        for (u32 entity_index = 0; entity_index < model.workspace.entity_count; entity_index += 1)
        {
            IdeDocumentEntitySnapshot* entity = model.workspace.entities + entity_index;
            if (string_equal(entity->name, S8("Helper")))
            {
                found_entity_snapshot = entity->source_path.length != 0 && entity->type_text.length != 0;
                BUSTER_TEST(arguments, entity->range.length != 0 || entity->range.offset == 0);
            }
        }
        BUSTER_TEST(arguments, found_entity_snapshot);

        IdeDocumentWorkspaceStatus status_before_diagnostic = ide_document_model_status(&model);
        BUSTER_TEST(arguments, status_before_diagnostic.document_count == 1 && status_before_diagnostic.open_document_count == 1 &&
                                  status_before_diagnostic.entity_count == model.workspace.entity_count &&
                                  status_before_diagnostic.diagnostic_document_count == 0);
        IdeDocumentDiagnosticInput bridge_diagnostic = {
            .file_path = second_path,
            .range = {.offset = 5, .length = 6, .line = 0, .column = 5},
            .message = S8("bridge diagnostic"),
            .severity = IDE_DOCUMENT_DIAGNOSTIC_ERROR,
            .source = IDE_DOCUMENT_DIAGNOSTIC_SOURCE_USER,
        };
        BUSTER_TEST(arguments, ide_document_model_replace_diagnostics(&model, &bridge_diagnostic, 1) == IDE_DOCUMENT_ERROR_NONE);
        IdeDocumentWorkspaceStatus status_after_diagnostic = ide_document_model_status(&model);
        BUSTER_TEST(arguments, status_after_diagnostic.diagnostic_document_count == 1 && status_after_diagnostic.error_count == 1);
        BUSTER_TEST(arguments, ide_document_model_set_filter_state(&model, (IdeDocumentWorkspaceFilterState){.show_diagnostics_only = true}) ==
                                  IDE_DOCUMENT_ERROR_NONE);
        bool found_diagnostic_entity = false;
        for (u32 entity_index = 0; entity_index < model.workspace.entity_count; entity_index += 1)
        {
            found_diagnostic_entity |= ide_document_model_entity_matches_filter(&model, entity_index);
        }
        BUSTER_TEST(arguments, found_diagnostic_entity);
        BUSTER_TEST(arguments, ide_document_model_set_filter_state(&model, (IdeDocumentWorkspaceFilterState){
                                                                         .query = S8("BRIDGE DIAGNOSTIC"),
                                                                         .case_sensitive = false,
                                                                     }) == IDE_DOCUMENT_ERROR_NONE);
        BUSTER_TEST(arguments, ide_document_model_document_matches_filter(&model, 0));

        BUSTER_TEST(arguments, ide_document_model_set_active(&model, second_path) == IDE_DOCUMENT_ERROR_NONE);
        BUSTER_TEST(arguments, ide_document_model_active_document(&model) != 0 &&
                                  string_ends_with_sequence(ide_document_model_active_document(&model)->path, S8("second.bbb")));
        ide_document_model_deinitialize(&model);
        BUSTER_TEST(arguments, os_file_delete(second_path));
        BUSTER_TEST(arguments, os_directory_delete(second_root));
    }
    error = ide_document_model_initialize(
        &model, arena, staging_arena,
        (IdeDocumentModelOptions){
            .workspace_root = root,
            .open_path = z_path,
        });
    BUSTER_TEST(arguments, error == IDE_DOCUMENT_ERROR_NONE);
    BUSTER_TEST(arguments, ide_document_model_open_document_count(&model) == 1);
    BUSTER_TEST(arguments, ide_document_model_active_document(&model) == ide_document_model_find(&model, z_path));
    ide_document_model_deinitialize(&model);
    BUSTER_TEST(arguments, os_directory_delete(root));
    BUSTER_TEST(arguments, arena_destroy(arena, 1));
    BUSTER_TEST(arguments, arena_destroy(staging_arena, 1));
#else
    BUSTER_UNUSED(arguments);
#endif
    return result;
}
#endif
