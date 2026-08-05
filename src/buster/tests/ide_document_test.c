#include <buster/tests/ide_document_test.h>
#if BUSTER_INCLUDE_TESTS

#include <buster/lib/file.h>
#include <buster/lib/hash.h>
#include <buster/lib/string.h>
#include <buster/lib/system_headers.h>

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
    String8 root = buster_test_temporary_path(test_arena, S8("buster-ide-document-model"), S8(""));
#if defined(_WIN32)
    root = os_path_absolute(test_arena, root, true);
#endif
    String8 subdirectory = string_format_z(test_arena, S8("{S8}/sub"), root);
    String8 a_path = string_format_z(test_arena, S8("{S8}/a.bbb"), root);
    String8 b_path = string_format_z(test_arena, S8("{S8}/sub/b.bbb"), root);
    String8 z_path = string_format_z(test_arena, S8("{S8}/z.bbb"), root);
    String8 unsupported_open_path = string_format_z(test_arena, S8("{S8}/notes.txt"), root);
    String8 a_source = S8("import b = \"sub/b\";\ncode main : fn () s32 { return 0; }\n");
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

        String8 outside_directory = buster_test_temporary_path(test_arena, S8("buster-ide-document-outside"), S8(""));
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
        BUSTER_TEST(arguments, ide_document_model_open(&model, escaped_file) == IDE_DOCUMENT_ERROR_PATH_OUTSIDE_ROOT);
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
        String8 outside_directory = buster_test_temporary_path(test_arena, S8("buster-ide-document-outside"), S8(""));
        outside_directory = os_path_absolute(test_arena, outside_directory, true);
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
        if (created_reparse)
        {
            BUSTER_TEST(arguments, ide_document_model_open(&model, escaped_file) == IDE_DOCUMENT_ERROR_PATH_OUTSIDE_ROOT);
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
