#define BUSTER_USE_GRAPHICS 1

#include <buster/lib/base.h>
#include <buster/lib/entry_point.h>
#include <buster/lib/ui_core.h>
#include <buster/lib/rendering.h>
#include <buster/lib/window.h>
#include <buster/lib/font_provider.h>
#include <buster/lib/time.h>
#include <buster/lib/ui_builder.h>
#include <buster/lib/arena.h>
#include <buster/lib/compiler/frontend/buster/parser.h>
#include <buster/lib/compiler/frontend/buster/analysis.h>
#include <buster/lib/compiler/frontend/c/c.h>
#include <buster/lib/compiler/assembly/assembly.h>
#include <buster/lib/compiler/assembly/x86_64_metadata.h>
#include <buster/lib/compiler/ir/ir.h>
#include <buster/lib/compiler/ir/interpreter.h>
#include <buster/lib/compiler/debug/debug.h>
#include <buster/lib/compiler/codegen/codegen.h>
#include <buster/lib/compiler/object/object.h>
#include <buster/lib/compiler/link/link.h>
#include <buster/lib/compiler/driver/driver.h>
#include <buster/lib/ide_document.h>
#include <buster/lib/integer.h>
#include <buster/lib/string.h>
#if BUSTER_INCLUDE_TESTS
#include <buster/tests/test.h>
#endif

#if BUSTER_UNITY_BUILD
#if BUSTER_INCLUDE_TESTS
#include <buster/tests/test.c>
#endif
#include <buster/lib/arena.c>
#include <buster/lib/integer.c>
#include <buster/lib/os.c>
#include <buster/lib/string.c>
#include <buster/lib/entry_point.c>
#include <buster/lib/target.c>
#include <buster/lib/simd.c>
#include <buster/lib/file.c>
#include <buster/lib/truetype.c>
#include <buster/lib/font_provider.c>
#include <buster/lib/window.c>
#include <buster/lib/rendering.c>
#include <buster/lib/ui_core.c>
#include <buster/lib/ui_builder.c>
#include <buster/lib/time.c>
#include <buster/lib/float.c>
#include <buster/lib/compiler/frontend/buster/parser.c>
#include <buster/lib/compiler/frontend/buster/analysis.c>
#include <buster/lib/compiler/frontend/c/c.c>
#include <buster/lib/compiler/assembly/assembly.c>
#include <buster/lib/compiler/assembly/x86_64_metadata.c>
#include <buster/lib/compiler/ir/ir.c>
#include <buster/lib/compiler/ir/interpreter.c>
#include <buster/lib/compiler/debug/debug.c>
#include <buster/lib/compiler/codegen/codegen.c>
#include <buster/lib/compiler/dwarf/dwarf.c>
#include <buster/lib/compiler/codeview/codeview.c>
#include <buster/lib/compiler/pdb/pdb.c>
#include <buster/lib/compiler/object/object.c>
#include <buster/lib/compiler/link/link.c>
#include <buster/lib/compiler/driver/driver.c>
#include <buster/lib/hash.c>
#include <buster/lib/ide_document.c>
#endif

typedef struct IdePanel IdePanel;
struct IdePanel
{
    IdePanel* first;
    IdePanel* last;
    IdePanel* previous;
    IdePanel* next;
    IdePanel* parent;
    f32 parent_percentage;
    Axis2 split_axis;
};

typedef struct IdeWindow IdeWindow;
struct IdeWindow
{
    WmWindowHandle* wm;
    RenderingWindowHandle* render;
    IdeWindow* previous;
    IdeWindow* next;
    IdePanel* root_panel;
    UI_State* ui;
    f32 dpi;
    f32 font_size;
    u32 font_height;
    u8 reserved[4];
};

typedef enum IdePendingAction
{
    IDE_PENDING_ACTION_NONE,
    IDE_PENDING_ACTION_OPEN,
    IDE_PENDING_ACTION_SAVE,
    IDE_PENDING_ACTION_RELOAD,
    IDE_PENDING_ACTION_RELOAD_DISCARD,
    IDE_PENDING_ACTION_ENTITY,
    IDE_PENDING_ACTION_FILTER,
} IdePendingAction;

typedef struct IdeProgram IdeProgram;
struct IdeProgram
{
    ProgramState state;
    IdeWindow* first_window;
    IdeWindow* last_window;
    WmHandle* windowing;
    RenderingHandle* rendering;
    String8 compile_source_path;
    String8 compile_output_path;
    String8 compile_linker;
    String8 compile_module_root;
    SliceString8 cc_arguments;
    SliceString8 fuzz_arguments;
    bool test;
    bool bench;
    bool compile;
    bool compile_debug_info;
    bool cc;
    bool fuzz;
    bool test_app;
    bool document_model_ready;
    bool editor_dirty;
    bool editor_overflow;
    bool metadata_show_path;
    bool metadata_show_type;
    bool metadata_show_location;
    bool metadata_show_diagnostics;
    bool pending_editor_commit;
    bool pending_editor_view_commit;
    bool pending_editor_view_restore;
    bool pending_entity_reveal;
    bool reload_discard_confirmation_pending;
    bool pending_filter_state;
    u8 reserved[4];
    TimeDataType last_frame_timestamp;
    IdeDocumentModel document_model;
    Arena* document_arena;
    Arena* document_staging_arena;
    UI_TextEditState path_edit;
    UI_TextEditState query_edit;
    UI_TextEditState editor_edit;
    UI_Box* editor_scroll_box;
    String8 path_bar;
    String8 query;
    String8 editor_value;
    String8 status_message;
    String8 error_path;
    String8 editor_path;
    String8 pending_open_path;
    String8 pending_edit_path;
    String8 pending_entity_path;
    String8 reload_discard_identity;
    char8 path_bar_storage[4096];
    char8 query_storage[1024];
    char8 editor_storage[1024 * 1024];
    char8 status_storage[4096];
    char8 error_path_storage[4096];
    char8 editor_path_storage[4096];
    char8 pending_open_storage[4096];
    char8 pending_edit_storage[4096];
    char8 pending_entity_storage[4096];
    char8 reload_discard_identity_storage[4096];
    u64 editor_revision;
    u32 selected_entity_index;
    ParserSourceRange pending_entity_range;
    u32 pending_reveal_line;
    u32 last_drop_path_count;
    u32 last_drop_ignored_count;
    IdeDocumentErrorKind document_error;
    IdePendingAction pending_action;
    IdeDocumentWorkspaceFilterState pending_filter;
    float2 pending_editor_scroll;
    u64 reload_discard_revision;
    String8 startup_open_path;
    bool startup_open_path_too_long;
};

BUSTER_GLOBAL_LOCAL IdeProgram ide_state = {0};

BUSTER_V_IMPL ProgramState* program_state = &ide_state.state;

#define IDE_BASE_DPI (96.0f)
#define IDE_BASE_FONT_SIZE (24.0f)
#define IDE_OPEN_PATH_CAPACITY (4096u)
#define IDE_QUERY_CAPACITY (1024u)
#define IDE_EDITOR_CAPACITY (1024u * 1024u)
#define IDE_STATUS_CAPACITY (4096u)

BUSTER_GLOBAL_LOCAL f32 ide_font_size_from_dpi(f32 dpi)
{
    if (dpi <= 0.0f)
    {
        dpi = IDE_BASE_DPI;
    }

    return BUSTER_CLAMP(6.0f, IDE_BASE_FONT_SIZE * (dpi / IDE_BASE_DPI), 72.0f);
}

BUSTER_GLOBAL_LOCAL void ide_window_queue_font_update(IdeWindow* window, f32 dpi)
{
    f32 font_size = ide_font_size_from_dpi(dpi);
    u32 font_height = (u32)(font_size + 0.5f);
    if (font_height == 0)
    {
        font_height = 1;
    }

    String8 font_path = font_file_get_path(FONT_INDEX_MONO);
    FontTextureAtlas font = rendering_font_create(ide_state.state.arena, ide_state.rendering,
                                                  (FontTextureAtlasCreate){
                                                      .font_path = font_path,
                                                      .text_height = font_height,
                                                  });
    rendering_queue_font_update(ide_state.rendering, window->render, RENDER_FONT_TYPE_MONOSPACE, font);

    window->dpi = dpi;
    window->font_size = font_size;
    window->font_height = font_height;
}

BUSTER_GLOBAL_LOCAL void ide_window_deinitialize(IdeWindow* window, RenderingHandle* rendering)
{
    if (!window)
    {
        return;
    }

    if (window->ui)
    {
        ui_state_deinitialize(window->ui);
        window->ui = 0;
    }

    if (rendering && window->render)
    {
        rendering_window_deinitialize(rendering, window->render);
        window->render = 0;
    }

    window->previous = 0;
    window->next = 0;
    window->wm = 0;
    window->root_panel = 0;
}

BUSTER_GLOBAL_LOCAL void ide_window_update_font_for_dpi(IdeWindow* window)
{
    if (window && window->wm && window->render)
    {
        f32 dpi = wm_window_get_dpi(ide_state.windowing, window->wm);
        f32 font_size = ide_font_size_from_dpi(dpi);
        u32 font_height = (u32)(font_size + 0.5f);
        if (dpi != window->dpi || font_height != window->font_height)
        {
            rendering_window_rect_texture_update_begin(window->render);
            ide_window_queue_font_update(window, dpi);
            rendering_window_rect_texture_update_end(ide_state.rendering, window->render);
        }
    }
}

BUSTER_GLOBAL_LOCAL bool ide_app_copy_storage(char8* storage, u64 capacity, String8 value, String8* result)
{
    if (!storage || !result || value.length > capacity || (value.length && !value.pointer))
    {
        return false;
    }
    if (value.length)
    {
        memmove(storage, value.pointer, value.length);
    }
    storage[value.length] = 0;
    *result = (String8){.pointer = storage, .length = value.length};
    return true;
}

BUSTER_GLOBAL_LOCAL void ide_app_set_status(String8 message)
{
    if (!ide_app_copy_storage(ide_state.status_storage, IDE_STATUS_CAPACITY - 1, message, &ide_state.status_message))
    {
        ide_state.status_message = S8("status message was too long");
    }
    ide_state.document_error = IDE_DOCUMENT_ERROR_NONE;
    ide_state.error_path = (String8){0};
}

BUSTER_GLOBAL_LOCAL void ide_app_set_error(IdeDocumentErrorKind error, String8 path)
{
    ide_state.document_error = error;
    ide_state.error_path = (String8){0};
    ide_app_copy_storage(ide_state.error_path_storage, IDE_OPEN_PATH_CAPACITY - 1, path, &ide_state.error_path);
    ide_state.status_message = (String8){0};
}

BUSTER_GLOBAL_LOCAL void ide_app_clear_reload_discard_confirmation(void)
{
    ide_state.reload_discard_confirmation_pending = false;
    ide_state.reload_discard_identity = (String8){0};
    ide_state.reload_discard_revision = 0;
}

BUSTER_GLOBAL_LOCAL bool ide_app_reload_discard_confirmation_matches(IdeDocument* document)
{
    return ide_state.reload_discard_confirmation_pending && document &&
           ide_state.reload_discard_revision == document->revision &&
           string_equal(ide_state.reload_discard_identity, document->identity);
}

BUSTER_GLOBAL_LOCAL u32 ide_app_pending_action_priority(IdePendingAction action)
{
    switch (action)
    {
        case IDE_PENDING_ACTION_OPEN:
            return 50;
        case IDE_PENDING_ACTION_SAVE:
            return 40;
        case IDE_PENDING_ACTION_RELOAD:
        case IDE_PENDING_ACTION_RELOAD_DISCARD:
            return 30;
        case IDE_PENDING_ACTION_ENTITY:
            return 20;
        case IDE_PENDING_ACTION_FILTER:
            return 10;
        case IDE_PENDING_ACTION_NONE:
            break;
    }
    return 0;
}

BUSTER_GLOBAL_LOCAL bool ide_app_queue_action(IdePendingAction action)
{
    if (action == IDE_PENDING_ACTION_NONE)
    {
        return false;
    }
    if (ide_state.pending_action != IDE_PENDING_ACTION_NONE &&
        ide_app_pending_action_priority(action) <= ide_app_pending_action_priority(ide_state.pending_action))
    {
        return false;
    }
    if (ide_state.pending_action == IDE_PENDING_ACTION_ENTITY && action != IDE_PENDING_ACTION_ENTITY)
    {
        ide_state.pending_entity_path = (String8){0};
        ide_state.pending_entity_reveal = false;
    }
    if (action != IDE_PENDING_ACTION_RELOAD_DISCARD)
    {
        ide_app_clear_reload_discard_confirmation();
    }
    ide_state.pending_action = action;
    return true;
}

BUSTER_GLOBAL_LOCAL bool ide_app_queue_filter(void)
{
    if (!ide_state.document_model_ready)
    {
        return false;
    }
    IdeDocumentWorkspaceFilterState filter = ide_state.pending_filter_state ? ide_state.pending_filter : ide_state.document_model.workspace.filter;
    filter.query = ide_state.query;
    ide_state.pending_filter = filter;
    ide_state.pending_filter_state = true;
    return ide_app_queue_action(IDE_PENDING_ACTION_FILTER);
}

BUSTER_GLOBAL_LOCAL bool ide_app_path_equals(IdeDocument* document, String8 path)
{
    return document && path.length && (string_equal(document->path, path) || string_equal(document->identity, path));
}

BUSTER_GLOBAL_LOCAL bool ide_app_editor_buffer_dirty(void)
{
    return ide_state.editor_dirty || ide_state.pending_editor_commit;
}

BUSTER_GLOBAL_LOCAL bool ide_app_has_unsaved_changes(void)
{
    IdeDocumentWorkspaceStatus status = ide_state.document_model_ready ? ide_document_model_status(&ide_state.document_model)
                                                                         : (IdeDocumentWorkspaceStatus){0};
    return status.dirty_document_count != 0 || ide_app_editor_buffer_dirty();
}

BUSTER_GLOBAL_LOCAL bool ide_app_request_open(String8 path)
{
    // Copy and validate the bounded payload before changing the pending-action
    // slot. A rejected high-priority request must not erase an already valid
    // lower-priority action.
    char8 stable_path_storage[IDE_OPEN_PATH_CAPACITY];
    String8 stable_path = {0};
    if (!ide_app_copy_storage(stable_path_storage, IDE_OPEN_PATH_CAPACITY - 1, path, &stable_path))
    {
        ide_app_set_error(IDE_DOCUMENT_ERROR_INVALID_ARGUMENT, path);
        return false;
    }
    if (!ide_app_queue_action(IDE_PENDING_ACTION_OPEN))
    {
        return false;
    }
    BUSTER_UNUSED(ide_app_copy_storage(ide_state.pending_open_storage, IDE_OPEN_PATH_CAPACITY - 1, stable_path, &ide_state.pending_open_path));
    BUSTER_UNUSED(ide_app_copy_storage(ide_state.path_bar_storage, IDE_OPEN_PATH_CAPACITY - 1, stable_path, &ide_state.path_bar));
    return true;
}

BUSTER_GLOBAL_LOCAL bool ide_app_request_entity_activation(IdeDocumentEntitySnapshot* entity)
{
    if (!entity)
    {
        return false;
    }
    char8 stable_path_storage[IDE_OPEN_PATH_CAPACITY];
    String8 stable_path = {0};
    if (!ide_app_copy_storage(stable_path_storage, IDE_OPEN_PATH_CAPACITY - 1, entity->source_path, &stable_path))
    {
        ide_app_set_error(IDE_DOCUMENT_ERROR_INVALID_ARGUMENT, entity->source_path);
        return false;
    }
    ParserSourceRange range = entity->range;
    if (!ide_app_queue_action(IDE_PENDING_ACTION_ENTITY))
    {
        return false;
    }
    BUSTER_UNUSED(ide_app_copy_storage(ide_state.pending_entity_storage, IDE_OPEN_PATH_CAPACITY - 1, stable_path,
                                       &ide_state.pending_entity_path));
    ide_state.pending_entity_range = range;
    return true;
}

BUSTER_GLOBAL_LOCAL bool ide_app_request_reload_discard(void)
{
    IdeDocument* document = ide_state.document_model_ready ? ide_document_model_active_document(&ide_state.document_model) : 0;
    if (document && document->dirty && !ide_app_reload_discard_confirmation_matches(document))
    {
        if (!ide_app_copy_storage(ide_state.reload_discard_identity_storage, IDE_OPEN_PATH_CAPACITY - 1, document->identity,
                                  &ide_state.reload_discard_identity))
        {
            ide_app_set_error(IDE_DOCUMENT_ERROR_INVALID_ARGUMENT, document->identity);
            return false;
        }
        ide_state.reload_discard_revision = document->revision;
        ide_state.reload_discard_confirmation_pending = true;
        ide_app_set_status(S8("click Confirm reload discard to discard dirty edits"));
        return false;
    }
    ide_app_clear_reload_discard_confirmation();
    return ide_app_queue_action(IDE_PENDING_ACTION_RELOAD_DISCARD);
}

BUSTER_GLOBAL_LOCAL void ide_app_stage_drop(SliceString8 paths)
{
    Arena* conflicts[] = {ide_state.state.arena};
    TemporalArena scratch = scratch_begin(conflicts, BUSTER_ARRAY_LENGTH(conflicts));
    IdeDocumentDropSelection selection = ide_document_choose_drop_path(scratch.arena, paths);
    ide_state.last_drop_path_count = selection.path_count;
    ide_state.last_drop_ignored_count = selection.ignored_count;
    if (!selection.accepted)
    {
        ide_app_set_error(IDE_DOCUMENT_ERROR_OPEN_PATH_NOT_SUPPORTED, (String8){0});
        scratch_end(scratch);
        return;
    }
    if (!ide_app_request_open(selection.path))
    {
        scratch_end(scratch);
        return;
    }
    if (selection.ignored_count)
    {
        ide_app_set_status(string_format(scratch.arena, S8("opened first .bbb path; ignored {u32} of {u32} dropped paths"),
                                         selection.ignored_count, selection.path_count));
    }
    scratch_end(scratch);
}

BUSTER_GLOBAL_LOCAL void ide_app_sync_editor(void)
{
    if (!ide_state.document_model_ready)
    {
        return;
    }
    IdeDocument* document = ide_document_model_active_document(&ide_state.document_model);
    if (!document)
    {
        ide_state.editor_value.length = 0;
        ide_state.editor_path.length = 0;
        ide_state.editor_revision = 0;
        ide_state.editor_dirty = false;
        ide_state.editor_overflow = false;
        ide_state.pending_editor_view_commit = false;
        ide_state.pending_editor_view_restore = false;
        return;
    }
    bool same_document = string_equal(document->identity, ide_state.editor_path);
    if (same_document && ide_state.editor_dirty && document->revision != ide_state.editor_revision)
    {
        ide_app_set_status(S8("active source changed while editor text is dirty; save or reload explicitly"));
        return;
    }
    if (!same_document || (!ide_state.editor_dirty && document->revision != ide_state.editor_revision))
    {
        ide_app_copy_storage(ide_state.editor_path_storage, IDE_OPEN_PATH_CAPACITY - 1, document->identity, &ide_state.editor_path);
        ide_state.editor_revision = document->revision;
        ide_state.editor_edit = (UI_TextEditState){0};
        ide_state.editor_overflow = document->source.length > IDE_EDITOR_CAPACITY - 1;
        if (ide_state.editor_overflow)
        {
            ide_state.editor_value.length = 0;
            ide_state.editor_dirty = false;
            ide_state.pending_editor_view_commit = false;
            ide_state.pending_editor_view_restore = false;
            return;
        }
        ide_app_copy_storage(ide_state.editor_storage, IDE_EDITOR_CAPACITY - 1, document->source, &ide_state.editor_value);
        ide_state.editor_edit.cursor = BUSTER_MIN(document->view.cursor_offset, ide_state.editor_value.length);
        u64 selection_start = BUSTER_MIN(document->view.selection_start, ide_state.editor_value.length);
        u64 selection_end = BUSTER_MIN(document->view.selection_end, ide_state.editor_value.length);
        if (ide_state.editor_edit.cursor == selection_start)
        {
            ide_state.editor_edit.mark = selection_end;
        }
        else if (ide_state.editor_edit.cursor == selection_end)
        {
            ide_state.editor_edit.mark = selection_start;
        }
        else
        {
            ide_state.editor_edit.mark = selection_end;
        }
        ide_state.pending_editor_scroll = float2_make(document->view.scroll_x, document->view.scroll_y);
        ide_state.pending_editor_view_commit = false;
        ide_state.pending_editor_view_restore = true;
        ide_state.editor_dirty = false;
    }
}

BUSTER_GLOBAL_LOCAL String8 ide_app_status_text(Arena* arena)
{
    if (ide_state.document_error != IDE_DOCUMENT_ERROR_NONE)
    {
        String8 name = ide_document_error_kind_name(ide_state.document_error);
        return ide_state.error_path.length ? string_format(arena, S8("error: {S8}: {S8}"), name, ide_state.error_path)
                                           : string_format(arena, S8("error: {S8}"), name);
    }
    return ide_state.status_message;
}

#if BUSTER_FUZZ_AVAILABLE
s32 buster_fuzz_test_input(const u8* pointer, size_t size)
{
    if (size > BUSTER_KB(64))
    {
        return 0;
    }
    if (!pointer && size)
    {
        return 0;
    }

    object_fuzz_test_input(pointer, size);

    Arena* result_arena = arena_create((ArenaCreation){
        .reserved_size = BUSTER_MB(64),
    });
    Arena* expression_arena = arena_create((ArenaCreation){
        .reserved_size = BUSTER_MB(64),
    });
    if (!result_arena || !expression_arena)
    {
        if (result_arena)
        {
            arena_destroy(result_arena, 1);
        }
        if (expression_arena)
        {
            arena_destroy(expression_arena, 1);
        }
        return 0;
    }

    String8 source = {
        .pointer = pointer ? (char8*)pointer : S8("").pointer,
        .length = size,
    };
    AnalysisProgram buster_analysis = analysis_program_load_memory(result_arena, expression_arena, source);
    IrProgram buster_ir = ir_generate_program(result_arena, &buster_analysis);
    for (u32 module_index = 0; module_index < buster_analysis.module_count; module_index += 1)
    {
        AnalysisResult* module_analysis = buster_analysis.module_results[module_index];
        if (module_analysis)
        {
            IrValidationResult validation = ir_validate_module(module_analysis, &buster_ir.modules[module_index]);
            BUSTER_CHECK(validation.error == IR_VALIDATION_NONE);
        }
    }
    analysis_program_unmap_sources(&buster_analysis);
    arena_reset_to_start(result_arena);
    arena_reset_to_start(expression_arena);

    CPreprocessResult preprocess = c_preprocess(result_arena, source,
                                                (CPreprocessOptions){
                                                    .source_path = S8("fuzz.c"),
                                                    .target = target_native,
                                                    .data_layout = target_data_layout(target_native),
                                                    .expansion_limit = BUSTER_KB(4),
                                                    .include_depth_limit = 8,
                                                    .disable_external_includes = true,
                                                });
    CParserResult syntax = c_parse_ast(result_arena, preprocess);
    CIRLowerResult c_ir = c_analyze(result_arena, S8("fuzz.c"), preprocess, syntax, target_native);
    if (c_ir.program)
    {
        for (u32 module_index = 0; module_index < c_ir.program->module_count; module_index += 1)
        {
            IrValidationResult validation = ir_validate_canonical_module(c_ir.program, &c_ir.program->modules[module_index]);
            BUSTER_CHECK(validation.error == IR_VALIDATION_NONE);
        }
    }

    arena_destroy(expression_arena, 1);
    arena_destroy(result_arena, 1);
    return 0;
}
#endif

ProcessResult process_arguments(void)
{
    ProcessResult result = PROCESS_RESULT_SUCCESS;

    SliceString8 arguments = program_state->input.arguments;
    // SliceString8 environment = program_state->input.environment;

    // StringOsListIterator arg_it = string_os_list_iterator_initialize(argv);
    //
    // string_os_list_iterator_next(&arg_it);

    for (u64 i = 1; i < arguments.length; i += 1)
    {
        String8 arg = arguments.pointer[i];
        if (string_equal(arg, S8("test")))
        {
            ide_state.test = true;
        }
        else if (string_equal(arg, S8("test_app")))
        {
            ide_state.test = true;
            ide_state.test_app = true;
        }
        else if (string_equal(arg, S8("bench")))
        {
            ide_state.bench = true;
        }
        else if (string_equal(arg, S8("--open")) || string_starts_with_sequence(arg, S8("--open=")))
        {
            String8 open_path = {0};
            if (string_equal(arg, S8("--open")))
            {
                if (i + 1 >= arguments.length)
                {
                    if (!ide_state.test && !ide_state.bench && !ide_state.compile && !ide_state.cc && !ide_state.fuzz)
                    {
                        string_print(S8("expected a path after --open\n"));
                        result = PROCESS_RESULT_FAILED;
                        break;
                    }
                }
                else
                {
                    i += 1;
                    open_path = arguments.pointer[i];
                }
            }
            else
            {
                open_path = (String8){
                    .pointer = arg.pointer + S8("--open=").length,
                    .length = arg.length - S8("--open=").length,
                };
            }
            if (open_path.length && !ide_state.test && !ide_state.bench && !ide_state.compile && !ide_state.cc && !ide_state.fuzz)
            {
                ide_state.startup_open_path = open_path;
            }
        }
        else if (string_equal(arg, S8("compile")))
        {
            if (ide_state.compile || i + 1 >= arguments.length)
            {
                string_print(S8("usage: ide compile <source.bbb> "
                                "[-o <output>] [--module-root=<path>] "
                                "[--linker=<path>]\n"));
                result = PROCESS_RESULT_FAILED;
                break;
            }
            ide_state.compile = true;
            ide_state.compile_debug_info = true;
            i += 1;
            ide_state.compile_source_path = arguments.pointer[i];
        }
        else if (string_equal(arg, S8("cc")))
        {
            ide_state.cc = true;
            ide_state.cc_arguments = (SliceString8){
                .pointer = arguments.pointer + i + 1,
                .length = arguments.length - i - 1,
            };
            break;
        }
        else if (string_equal(arg, S8("--fuzz")))
        {
#if BUSTER_FUZZ_AVAILABLE
            ide_state.fuzz = true;
            ide_state.fuzz_arguments = (SliceString8){
                .pointer = arguments.pointer + i + 1,
                .length = arguments.length - i - 1,
            };
            break;
#else
            string_print(S8("fuzzing is not available in this build\n"));
            result = PROCESS_RESULT_FAILED;
            break;
#endif
        }
        else if ((string_equal(arg, S8("-g")) || string_equal(arg, S8("-g0"))) && ide_state.compile)
        {
            ide_state.compile_debug_info = !string_equal(arg, S8("-g0"));
        }
        else if (ide_state.compile && string_starts_with_sequence(arg, S8("-g")))
        {
            string_print(S8("unsupported debug option: {S8}\n"), arg);
            result = PROCESS_RESULT_FAILED;
            break;
        }
        else if (string_equal(arg, S8("-o")) && ide_state.compile)
        {
            if (i + 1 >= arguments.length)
            {
                string_print(S8("expected an output path after -o\n"));
                result = PROCESS_RESULT_FAILED;
                break;
            }
            i += 1;
            ide_state.compile_output_path = arguments.pointer[i];
        }
        else if (ide_state.compile && string_starts_with_sequence(arg, S8("--module-root=")))
        {
            ide_state.compile_module_root = (String8){
                .pointer = arg.pointer + S8("--module-root=").length,
                .length = arg.length - S8("--module-root=").length,
            };
            if (!ide_state.compile_module_root.length)
            {
                string_print(S8("expected a module root after --module-root=\n"));
                result = PROCESS_RESULT_FAILED;
                break;
            }
        }
        else if (ide_state.compile && string_starts_with_sequence(arg, S8("--linker=")))
        {
            ide_state.compile_linker = (String8){
                .pointer = arg.pointer + S8("--linker=").length,
                .length = arg.length - S8("--linker=").length,
            };
            if (!ide_state.compile_linker.length)
            {
                string_print(S8("expected a linker path after --linker=\n"));
                result = PROCESS_RESULT_FAILED;
                break;
            }
        }
        else
        {
            ProcessResult r = buster_argument_process(i);
            if (r != PROCESS_RESULT_SUCCESS)
            {
                string_print(S8("Failed to process argument {S8}\n"), arg);
                result = r;
                break;
            }
        }
    }

    if (ide_state.test || ide_state.bench || ide_state.compile || ide_state.cc || ide_state.fuzz)
    {
        ide_state.startup_open_path = (String8){0};
    }

    return result;
}

BUSTER_GLOBAL_LOCAL String8 ide_ui_import_state_name(IdeDocumentImportState state)
{
    switch (state)
    {
        case IDE_DOCUMENT_IMPORT_RESOLVED:
            return S8("resolved");
        case IDE_DOCUMENT_IMPORT_MISSING:
            return S8("missing");
        case IDE_DOCUMENT_IMPORT_AMBIGUOUS:
            return S8("ambiguous");
        case IDE_DOCUMENT_IMPORT_CYCLE:
            return S8("cycle");
        case IDE_DOCUMENT_IMPORT_STATE_COUNT:
            break;
    }
    return S8("unknown");
}

BUSTER_GLOBAL_LOCAL String8 ide_ui_entity_kind_name(IdeDocumentEntityKind kind)
{
    switch (kind)
    {
        case IDE_DOCUMENT_ENTITY_TYPE:
            return S8("type");
        case IDE_DOCUMENT_ENTITY_CODE:
            return S8("function");
        case IDE_DOCUMENT_ENTITY_DATA:
            return S8("data");
        case IDE_DOCUMENT_ENTITY_KIND_COUNT:
            break;
    }
    return S8("entity");
}

BUSTER_GLOBAL_LOCAL void ide_ui_top_bar(void)
{
    ui_push(pref_height, ui_em(2.0f, 1.0f));
    ui_push(child_layout_axis, AXIS2_X);
    UI_Box* top_bar = ui_box_make(UI_BoxFlag_DrawBackground | UI_BoxFlag_DrawBorder, S8("top_bar"));
    ui_push(parent, top_bar);
    {
        ui_set_next_pref_width(ui_pixels(720, 1.0f));
        UI_Box* path_slot = ui_box_make(0, S8("path_slot"));
        ui_push(parent, path_slot);
        {
            UI_TextEditResult path_result = ui_text_edit(&ide_state.path_edit, S8("open path##open_path"), &ide_state.path_bar,
                                                         IDE_OPEN_PATH_CAPACITY - 1);
            BUSTER_UNUSED(path_result);
        }
        BUSTER_UNUSED(ui_pop(parent));
        UI_Signal open_signal = ui_button(S8("Open##open_button"));
        if (ui_clicked(open_signal))
        {
            ide_app_request_open(ide_state.path_bar);
        }
        UI_Signal save_signal = ui_button(S8("Save##save_button"));
        if (ui_clicked(save_signal))
        {
            ide_app_queue_action(IDE_PENDING_ACTION_SAVE);
        }
        UI_Signal reload_signal = ui_button(S8("Reload##reload_button"));
        if (ui_clicked(reload_signal))
        {
            ide_app_queue_action(IDE_PENDING_ACTION_RELOAD);
        }
        IdeDocument* reload_document = ide_state.document_model_ready ? ide_document_model_active_document(&ide_state.document_model) : 0;
        String8 discard_reload_label = ide_app_reload_discard_confirmation_matches(reload_document)
                                           ? S8("Confirm reload discard##reload_discard_button")
                                           : S8("Reload discard edits##reload_discard_button");
        UI_Signal discard_reload_signal = ui_button(discard_reload_label);
        if (ui_clicked(discard_reload_signal))
        {
            ide_app_request_reload_discard();
        }
        ui_set_next_pref_width(ui_pixels(480, 1.0f));
        UI_Box* query_slot = ui_box_make(0, S8("query_slot"));
        ui_push(parent, query_slot);
        {
            UI_TextEditResult query_result = ui_text_edit(&ide_state.query_edit, S8("filter##workspace_filter"), &ide_state.query,
                                                          IDE_QUERY_CAPACITY - 1);
            if (query_result.changed)
            {
                ide_app_queue_filter();
            }
        }
        BUSTER_UNUSED(ui_pop(parent));
        ui_set_next_pref_width(ui_pixels(560, 1.0f));
        ui_label(ide_app_status_text(ui_build_arena()));
    }
    BUSTER_UNUSED(ui_pop(parent));
    BUSTER_UNUSED(ui_pop(child_layout_axis));
    BUSTER_UNUSED(ui_pop(pref_height));
}

BUSTER_GLOBAL_LOCAL void ide_ui_document_list(void)
{
    ui_label(S8("Workspace modules / documents"));
    IdeDocumentModel* model = &ide_state.document_model;
    if (!ide_state.document_model_ready || !model->workspace.root_path.length)
    {
        ui_label(S8("No workspace selected. Open a .bbb file to begin."));
        return;
    }
    ui_label(string_format(ui_build_arena(), S8("root: {S8}"), model->workspace.root_path));
    ui_scroll_region_begin(S8("workspace_documents_scroll"));
    u32 shown = 0;
    for (u32 index = 0; index < model->workspace.document_count; index += 1)
    {
        IdeDocument* document = ide_document_model_document_at(model, index);
        if (!document || !ide_document_model_document_matches_filter(model, index))
        {
            continue;
        }
        String8 diagnostic_status = ide_state.metadata_show_diagnostics
                                         ? (document->diagnostic_count ? S8("diagnostics") : S8("ok"))
                                         : (String8){0};
        String8 row = string_format(ui_build_arena(), S8("{S8}  {S8}  {S8}##document-{u32}"), document->module_name,
                                    document->dirty ? S8("dirty") : S8("clean"),
                                    diagnostic_status, index);
        UI_Signal signal = ui_tree_row(row, 0, document->is_open && document == ide_document_model_active_document(model), true);
        if (ui_focused(signal))
        {
            // Focus navigation through the document list keeps Enter/Open
            // deterministic and exposes the selected path in the editable bar.
            ide_app_copy_storage(ide_state.path_bar_storage, IDE_OPEN_PATH_CAPACITY - 1, document->path, &ide_state.path_bar);
        }
        if (ui_clicked(signal))
        {
            ide_app_request_open(document->path);
        }
        shown += 1;
    }
    if (!shown)
    {
        ui_label(S8("No documents match the filter."));
    }
    ui_scroll_region_end();
}

BUSTER_GLOBAL_LOCAL void ide_ui_entity_row(IdeDocumentEntitySnapshot* entity, u32 index)
{
    Arena* arena = ui_build_arena();
    String8 location = ide_state.metadata_show_location
                           ? string_format(arena, S8("line {u32}, col {u32}"), entity->range.line + 1, entity->range.column + 1)
                           : (String8){0};
    String8 path = ide_state.metadata_show_path ? entity->source_path : (String8){0};
    String8 type = ide_state.metadata_show_type ? entity->type_text : (String8){0};
    String8 row = string_format(arena, S8("{S8}  [{S8}]  {S8}  {S8}  {S8}##entity-{u32}"), entity->name,
                               ide_ui_entity_kind_name(entity->kind), type, path, location, index);
    UI_Signal signal = ui_list_row(row, index == ide_state.selected_entity_index);
    if (ui_focused(signal))
    {
        ide_state.selected_entity_index = index;
    }
    if (ui_clicked(signal))
    {
        ide_app_request_entity_activation(entity);
    }
}

BUSTER_GLOBAL_LOCAL void ide_ui_entity_list(void)
{
    ui_label(S8("Declarations / entities"));
    IdeDocumentModel* model = &ide_state.document_model;
    ui_scroll_region_begin(S8("entity_result_scroll"));
    u32 shown = 0;
    if (ide_state.document_model_ready)
    {
        for (u32 index = 0; index < model->workspace.entity_count; index += 1)
        {
            if (ide_document_model_entity_matches_filter(model, index))
            {
                ide_ui_entity_row(model->workspace.entities + index, index);
                shown += 1;
            }
        }
    }
    if (!shown)
    {
        ui_label(ide_state.document_model_ready && model->workspace.entity_count ? S8("No entities match the filter.")
                                                                                     : S8("No entity snapshot is available yet."));
    }
    ui_scroll_region_end();
}

BUSTER_GLOBAL_LOCAL void ide_ui_source_editor(void)
{
    ui_label(S8("Source"));
    IdeDocument* document = ide_state.document_model_ready ? ide_document_model_active_document(&ide_state.document_model) : 0;
    if (!document)
    {
        ui_label(S8("Empty state: choose a path or drop a .bbb file."));
        ide_state.editor_scroll_box = 0;
        return;
    }
    ui_label(string_format(ui_build_arena(), S8("{S8}{S8}"), document->path, document->dirty ? S8("  [dirty]") : S8("")));
    ide_state.editor_scroll_box = ui_scroll_region_begin(S8("source_editor_scroll"));
    if (ide_state.pending_editor_view_restore)
    {
        float2 current_scroll = ui_box_scroll_offset(ide_state.editor_scroll_box);
        ui_box_scroll_by(ide_state.editor_scroll_box,
                         float2_make(float2_element(ide_state.pending_editor_scroll, AXIS2_X) - float2_element(current_scroll, AXIS2_X),
                                     float2_element(ide_state.pending_editor_scroll, AXIS2_Y) - float2_element(current_scroll, AXIS2_Y)));
        ide_state.pending_editor_view_restore = false;
    }
    if (ide_state.pending_entity_reveal)
    {
        f32 line_height = ide_state.first_window ? ide_state.first_window->font_size : IDE_BASE_FONT_SIZE;
        float2 scroll = ui_box_scroll_offset(ide_state.editor_scroll_box);
        f32 target_y = (f32)ide_state.pending_reveal_line * line_height;
        ui_box_scroll_by(ide_state.editor_scroll_box,
                         float2_make(0.0f, target_y - float2_element(scroll, AXIS2_Y)));
        ide_state.pending_entity_reveal = false;
    }
    if (ide_state.editor_overflow)
    {
        ui_label(S8("Source exceeds the bounded editor buffer; save/reload from the path bar."));
    }
    else
    {
        UI_TextEditResult editor_result = ui_text_edit(&ide_state.editor_edit, S8("source##source_editor"), &ide_state.editor_value,
                                                       IDE_EDITOR_CAPACITY - 1);
        if (editor_result.changed)
        {
            ide_state.editor_dirty = true;
            ide_state.pending_editor_commit = true;
            ide_app_copy_storage(ide_state.pending_edit_storage, IDE_OPEN_PATH_CAPACITY - 1, document->path, &ide_state.pending_edit_path);
        }
    }
    ui_scroll_region_end();
}

BUSTER_GLOBAL_LOCAL void ide_ui_inspector(void)
{
    IdeDocumentModel* model = &ide_state.document_model;
    ui_label(S8("Metadata"));
    UI_WidgetResult path_toggle = ui_checkbox(S8("Source path##metadata_path"), ide_state.metadata_show_path);
    if (path_toggle.changed)
    {
        ide_state.metadata_show_path = path_toggle.value;
    }
    UI_WidgetResult type_toggle = ui_checkbox(S8("Type text##metadata_type"), ide_state.metadata_show_type);
    if (type_toggle.changed)
    {
        ide_state.metadata_show_type = type_toggle.value;
    }
    UI_WidgetResult location_toggle = ui_checkbox(S8("Line / column##metadata_location"), ide_state.metadata_show_location);
    if (location_toggle.changed)
    {
        ide_state.metadata_show_location = location_toggle.value;
    }
    UI_WidgetResult diagnostic_toggle = ui_checkbox(S8("Diagnostic counts##metadata_diagnostics"), ide_state.metadata_show_diagnostics);
    if (diagnostic_toggle.changed)
    {
        ide_state.metadata_show_diagnostics = diagnostic_toggle.value;
    }
    ui_separator(AXIS2_X);
    ui_label(S8("Result filters"));
    IdeDocumentWorkspaceFilterState filter = model->workspace.filter;
    UI_WidgetResult open_only_toggle = ui_checkbox(S8("Open documents only##filter_open_only"), filter.show_open_only);
    UI_WidgetResult dirty_only_toggle = ui_checkbox(S8("Dirty documents only##filter_dirty_only"), filter.show_dirty_only);
    UI_WidgetResult diagnostics_only_toggle = ui_checkbox(S8("Diagnostics only##filter_diagnostics_only"), filter.show_diagnostics_only);
    UI_WidgetResult case_sensitive_toggle = ui_checkbox(S8("Case-sensitive##filter_case_sensitive"), filter.case_sensitive);
    if (open_only_toggle.changed || dirty_only_toggle.changed || diagnostics_only_toggle.changed || case_sensitive_toggle.changed)
    {
        filter.show_open_only = open_only_toggle.value;
        filter.show_dirty_only = dirty_only_toggle.value;
        filter.show_diagnostics_only = diagnostics_only_toggle.value;
        filter.case_sensitive = case_sensitive_toggle.value;
        filter.query = ide_state.query;
        ide_state.pending_filter = filter;
        ide_state.pending_filter_state = true;
        ide_app_queue_action(IDE_PENDING_ACTION_FILTER);
    }
    IdeDocumentWorkspaceStatus status = ide_document_model_status(model);
    ui_label(string_format(ui_build_arena(), S8("documents {u32}  open {u32}  dirty {u32}"), status.document_count,
                           status.open_document_count, status.dirty_document_count));
    ui_label(string_format(ui_build_arena(), S8("entities {u32}  imports {u32}"), status.entity_count, status.import_count));
    ui_label(string_format(ui_build_arena(), S8("diagnostics: {u32} errors, {u32} warnings"), status.error_count, status.warning_count));
    if (ide_state.last_drop_path_count)
    {
        ui_label(string_format(ui_build_arena(), S8("last drop: {u32} paths, ignored {u32}"), ide_state.last_drop_path_count,
                               ide_state.last_drop_ignored_count));
    }
    ui_separator(AXIS2_X);
    ui_label(S8("Imports"));
    ui_scroll_region_begin(S8("imports_scroll"));
    if (ide_state.document_model_ready)
    {
        for (u32 index = 0; index < model->workspace.import_count; index += 1)
        {
            IdeDocumentImport* import = ide_document_model_import_at(model, index);
            ui_list_row(string_format(ui_build_arena(), S8("{S8} -> {S8} [{S8}]##import-{u32}"), import->name_space,
                                      import->requested_path, ide_ui_import_state_name(import->state), index), false);
        }
    }
    if (!status.import_count)
    {
        ui_label(S8("No imports."));
    }
    ui_scroll_region_end();
    ui_separator(AXIS2_X);
    ui_label(S8("Diagnostics"));
    ui_scroll_region_begin(S8("diagnostics_scroll"));
    IdeDocument* active = ide_state.document_model_ready ? ide_document_model_active_document(model) : 0;
    if (active && active->diagnostic_count)
    {
        for (u32 index = 0; index < active->diagnostic_count; index += 1)
        {
            IdeDocumentDiagnostic* diagnostic = active->diagnostics + index;
            ui_list_row(string_format(ui_build_arena(), S8("{S8}:{u32}:{u32} {S8}##diagnostic-{u32}"), diagnostic->file_path,
                                      diagnostic->range.line + 1, diagnostic->range.column + 1, diagnostic->message, index), false);
        }
    }
    else
    {
        ui_label(S8("No diagnostics for the active document."));
    }
    ui_scroll_region_end();
}

BUSTER_GLOBAL_LOCAL void ide_ui_workspace(void)
{
    ui_push(child_layout_axis, AXIS2_X);
    UI_Box* workspace_widget = ui_box_make(UI_BoxFlag_DrawBackground | UI_BoxFlag_DrawBorder | UI_BoxFlag_DropSite, S8("workspace"));
    ui_push(parent, workspace_widget);
    {
        ui_push(pref_width, ui_pixels(340, 1.0f));
        UI_Box* sidebar = ui_box_make(UI_BoxFlag_DrawBackground, S8("workspace_sidebar"));
        ui_push(parent, sidebar);
        ide_ui_document_list();
        BUSTER_UNUSED(ui_pop(parent));
        BUSTER_UNUSED(ui_pop(pref_width));

        ui_push(pref_width, ui_percentage(0.55f, 1.0f));
        UI_Box* center = ui_box_make(UI_BoxFlag_DrawBackground, S8("workspace_center"));
        ui_push(parent, center);
        ui_push(child_layout_axis, AXIS2_Y);
        ui_push(pref_height, ui_percentage(0.36f, 1.0f));
        UI_Box* entities = ui_box_make(UI_BoxFlag_DrawBackground, S8("entity_panel"));
        ui_push(parent, entities);
        ide_ui_entity_list();
        BUSTER_UNUSED(ui_pop(parent));
        BUSTER_UNUSED(ui_pop(pref_height));
        ui_push(pref_height, ui_percentage(0.64f, 1.0f));
        UI_Box* source = ui_box_make(UI_BoxFlag_DrawBackground, S8("source_panel"));
        ui_push(parent, source);
        ide_ui_source_editor();
        BUSTER_UNUSED(ui_pop(parent));
        BUSTER_UNUSED(ui_pop(pref_height));
        BUSTER_UNUSED(ui_pop(child_layout_axis));
        BUSTER_UNUSED(ui_pop(parent));
        BUSTER_UNUSED(ui_pop(pref_width));

        ui_push(pref_width, ui_pixels(420, 1.0f));
        UI_Box* inspector = ui_box_make(UI_BoxFlag_DrawBackground, S8("workspace_inspector"));
        ui_push(parent, inspector);
        ide_ui_inspector();
        BUSTER_UNUSED(ui_pop(parent));
        BUSTER_UNUSED(ui_pop(pref_width));
    }
    BUSTER_UNUSED(ui_pop(parent));
    BUSTER_UNUSED(ui_pop(child_layout_axis));
}

BUSTER_GLOBAL_LOCAL bool ide_app_apply_editor_commit(void)
{
    IdeDocument* document = ide_document_model_active_document(&ide_state.document_model);
    String8 path = ide_state.pending_edit_path;
    if (!document || !ide_app_path_equals(document, path))
    {
        ide_app_set_status(S8("source edit was not applied because the active document changed"));
        return false;
    }

    String8 source = ide_state.editor_value;
    IdeDocumentErrorKind error = ide_document_model_set_text(&ide_state.document_model, path, source);
    if (error != IDE_DOCUMENT_ERROR_NONE)
    {
        ide_app_set_error(error, path);
        return false;
    }

    // set_text commits a new arena. Reacquire before recording the editor
    // cursor/selection/view state, and use the stable application-owned path.
    document = ide_document_model_active_document(&ide_state.document_model);
    IdeDocumentViewState view = document ? document->view : (IdeDocumentViewState){0};
    view.cursor_offset = BUSTER_MIN(ide_state.editor_edit.cursor, source.length);
    view.selection_start = BUSTER_MIN(ide_state.editor_edit.cursor, source.length);
    view.selection_end = BUSTER_MIN(ide_state.editor_edit.mark, source.length);
    if (view.selection_start > view.selection_end)
    {
        u64 swap = view.selection_start;
        view.selection_start = view.selection_end;
        view.selection_end = swap;
    }
    view.scroll_x = float2_element(ide_state.pending_editor_scroll, AXIS2_X);
    view.scroll_y = float2_element(ide_state.pending_editor_scroll, AXIS2_Y);
    error = ide_document_model_set_view(&ide_state.document_model, path, view);
    if (error != IDE_DOCUMENT_ERROR_NONE)
    {
        ide_app_set_error(error, path);
        return false;
    }
    document = ide_document_model_active_document(&ide_state.document_model);
    if (document)
    {
        ide_state.editor_revision = document->revision;
        ide_state.editor_dirty = false;
        ide_app_copy_storage(ide_state.editor_path_storage, IDE_OPEN_PATH_CAPACITY - 1, document->identity, &ide_state.editor_path);
    }
    ide_state.pending_editor_commit = false;
    ide_state.pending_editor_view_commit = false;
    ide_app_set_status(S8("source edited; save to write the file"));
    return true;
}

BUSTER_GLOBAL_LOCAL bool ide_app_apply_editor_view_commit(void)
{
    ide_state.pending_editor_view_commit = false;
    IdeDocument* document = ide_document_model_active_document(&ide_state.document_model);
    String8 path = ide_state.editor_path;
    if (!document || !ide_app_path_equals(document, path))
    {
        ide_app_set_status(S8("source view was not applied because the active document changed"));
        return false;
    }

    IdeDocumentViewState view = document->view;
    view.scroll_x = float2_element(ide_state.pending_editor_scroll, AXIS2_X);
    view.scroll_y = float2_element(ide_state.pending_editor_scroll, AXIS2_Y);
    IdeDocumentErrorKind error = ide_document_model_set_view(&ide_state.document_model, path, view);
    if (error != IDE_DOCUMENT_ERROR_NONE)
    {
        ide_app_set_error(error, path);
        return false;
    }
    return true;
}

BUSTER_GLOBAL_LOCAL u32 ide_app_find_entity_source_range(String8 path, ParserSourceRange range)
{
    IdeDocumentModel* model = &ide_state.document_model;
    for (u32 index = 0; index < model->workspace.entity_count; index += 1)
    {
        IdeDocumentEntitySnapshot* entity = model->workspace.entities + index;
        if (string_equal(entity->source_path, path) && entity->range.offset == range.offset && entity->range.length == range.length &&
            entity->range.line == range.line && entity->range.column == range.column)
        {
            return index;
        }
    }
    return IDE_DOCUMENT_INDEX_INVALID;
}

BUSTER_GLOBAL_LOCAL bool ide_app_apply_entity_activation(void)
{
    String8 path = ide_state.pending_entity_path;
    if (!path.length)
    {
        ide_app_set_error(IDE_DOCUMENT_ERROR_INVALID_ARGUMENT, path);
        ide_state.pending_entity_reveal = false;
        return false;
    }

    // The model owns the complete staged operation. The queued path/range are
    // stable application storage, so this call cannot retain a snapshot pointer
    // across its open/active/view commits.
    IdeDocumentErrorKind error = ide_document_model_activate_source_range(&ide_state.document_model, path, ide_state.pending_entity_range);
    if (error != IDE_DOCUMENT_ERROR_NONE)
    {
        ide_app_set_error(error, path);
        ide_state.pending_entity_reveal = false;
        return false;
    }
    u32 entity_index = ide_app_find_entity_source_range(path, ide_state.pending_entity_range);
    ide_state.selected_entity_index = entity_index;
    ide_state.pending_reveal_line = ide_state.pending_entity_range.line;
    ide_state.pending_entity_reveal = true;
    ide_state.editor_dirty = false;
    ide_state.editor_revision = 0;
    ide_app_set_status(S8("entity source selected"));
    return true;
}

BUSTER_GLOBAL_LOCAL void ide_app_apply_pending(void)
{
    if (!ide_state.document_model_ready)
    {
        return;
    }

    // Source text is committed first and exclusively for this frame. Any
    // button/drop/shortcut action remains queued for the next frame, so one UI
    // event cannot perform a chain of open/save/reload/filter mutations.
    if (ide_state.pending_editor_commit)
    {
        bool applied = ide_app_apply_editor_commit();
        if (!applied)
        {
            ide_state.pending_action = IDE_PENDING_ACTION_NONE;
        }
        else if (ide_state.pending_action == IDE_PENDING_ACTION_RELOAD_DISCARD)
        {
            // The editor commit may have made a previously clean document
            // dirty after the discard click was queued. Re-check the bound
            // confirmation against the newly committed revision before the
            // destructive action can run.
            IdeDocument* document = ide_document_model_active_document(&ide_state.document_model);
            if (document && document->dirty && !ide_app_reload_discard_confirmation_matches(document))
            {
                ide_state.pending_action = IDE_PENDING_ACTION_NONE;
                ide_app_request_reload_discard();
            }
        }
        return;
    }
    if (ide_state.pending_editor_view_commit)
    {
        if (!ide_app_apply_editor_view_commit())
        {
            ide_state.pending_action = IDE_PENDING_ACTION_NONE;
        }
        return;
    }

    IdePendingAction action = ide_state.pending_action;
    ide_state.pending_action = IDE_PENDING_ACTION_NONE;
    switch (action)
    {
        case IDE_PENDING_ACTION_OPEN:
        {
            String8 path = ide_state.pending_open_path;
            IdeDocument* current = ide_document_model_active_document(&ide_state.document_model);
            if (ide_app_editor_buffer_dirty() || (current && current->dirty && !ide_app_path_equals(current, path)))
            {
                ide_app_set_status(S8("save the dirty editor/document before opening another path"));
            }
            else
            {
                IdeDocumentErrorKind error = ide_document_model_open_path(&ide_state.document_model, path);
                if (error != IDE_DOCUMENT_ERROR_NONE)
                {
                    ide_app_set_error(error, path);
                }
                else
                {
                    IdeDocument* document = ide_document_model_active_document(&ide_state.document_model);
                    if (document)
                    {
                        ide_app_copy_storage(ide_state.path_bar_storage, IDE_OPEN_PATH_CAPACITY - 1, document->path, &ide_state.path_bar);
                    }
                    ide_state.editor_dirty = false;
                    ide_state.editor_revision = 0;
                    ide_app_set_status(S8("document opened"));
                }
            }
        }
        break;
        case IDE_PENDING_ACTION_SAVE:
        {
            IdeDocument* document = ide_document_model_active_document(&ide_state.document_model);
            if (!document)
            {
                ide_app_set_error(IDE_DOCUMENT_ERROR_ACTIVE_DOCUMENT_REQUIRED, (String8){0});
            }
            else if (ide_app_editor_buffer_dirty())
            {
                if (!ide_app_path_equals(document, ide_state.editor_path))
                {
                    ide_app_set_status(S8("the dirty editor belongs to another document; save or resolve it before switching"));
                }
                else if (!ide_state.pending_editor_commit)
                {
                    if (!ide_app_copy_storage(ide_state.pending_edit_storage, IDE_OPEN_PATH_CAPACITY - 1, document->path,
                                              &ide_state.pending_edit_path))
                    {
                        ide_app_set_error(IDE_DOCUMENT_ERROR_INVALID_ARGUMENT, document->path);
                    }
                    else
                    {
                        ide_state.pending_editor_commit = true;
                        ide_state.pending_action = IDE_PENDING_ACTION_SAVE;
                    }
                }
            }
            else
            {
                String8 path = document->path;
                IdeDocumentErrorKind error = ide_document_model_save(&ide_state.document_model, path);
                if (error != IDE_DOCUMENT_ERROR_NONE)
                {
                    ide_app_set_error(error, path);
                }
                else
                {
                    ide_state.editor_dirty = false;
                    ide_state.editor_revision = 0;
                    ide_app_set_status(S8("document saved"));
                }
            }
        }
        break;
        case IDE_PENDING_ACTION_RELOAD:
        {
            IdeDocument* document = ide_document_model_active_document(&ide_state.document_model);
            if (!document)
            {
                ide_app_set_error(IDE_DOCUMENT_ERROR_ACTIVE_DOCUMENT_REQUIRED, (String8){0});
            }
            else if (ide_app_editor_buffer_dirty())
            {
                ide_app_set_status(S8("save the dirty editor before reloading"));
            }
            else
            {
                String8 path = document->path;
                IdeDocumentErrorKind error = ide_document_model_reload(&ide_state.document_model, path, IDE_DOCUMENT_RELOAD_REJECT_DIRTY);
                if (error != IDE_DOCUMENT_ERROR_NONE)
                {
                    ide_app_set_error(error, path);
                }
                else
                {
                    ide_state.editor_dirty = false;
                    ide_state.editor_revision = 0;
                    ide_app_set_status(S8("document reloaded"));
                }
            }
        }
        break;
        case IDE_PENDING_ACTION_RELOAD_DISCARD:
        {
            ide_app_clear_reload_discard_confirmation();
            IdeDocument* document = ide_document_model_active_document(&ide_state.document_model);
            if (!document)
            {
                ide_app_set_error(IDE_DOCUMENT_ERROR_ACTIVE_DOCUMENT_REQUIRED, (String8){0});
            }
            else if (ide_app_editor_buffer_dirty())
            {
                ide_app_set_status(S8("save the dirty editor before discarding and reloading"));
            }
            else
            {
                String8 path = document->path;
                IdeDocumentErrorKind error = ide_document_model_reload(&ide_state.document_model, path, IDE_DOCUMENT_RELOAD_DISCARD_DIRTY);
                if (error != IDE_DOCUMENT_ERROR_NONE)
                {
                    ide_app_set_error(error, path);
                }
                else
                {
                    ide_state.editor_dirty = false;
                    ide_state.editor_revision = 0;
                    ide_app_set_status(S8("dirty edits discarded and document reloaded"));
                }
            }
        }
        break;
        case IDE_PENDING_ACTION_ENTITY:
            ide_app_apply_entity_activation();
            ide_state.pending_entity_path = (String8){0};
            break;
        case IDE_PENDING_ACTION_FILTER:
        {
            IdeDocumentWorkspaceFilterState filter = ide_state.pending_filter_state ? ide_state.pending_filter : ide_state.document_model.workspace.filter;
            ide_state.pending_filter_state = false;
            filter.query = ide_state.query;
            IdeDocumentErrorKind error = ide_document_model_set_filter_state(&ide_state.document_model, filter);
            if (error != IDE_DOCUMENT_ERROR_NONE)
            {
                ide_app_set_error(error, (String8){0});
            }
        }
        break;
        case IDE_PENDING_ACTION_NONE:
            break;
    }

    // A lower-priority query edit is never executed in the same frame as a
    // higher-priority action, but it is not lost: synchronize it on the next
    // frame after the committed action.
    if (action != IDE_PENDING_ACTION_FILTER &&
        (ide_state.pending_filter_state || !string_equal(ide_state.document_model.workspace.filter.query, ide_state.query)))
    {
        ide_app_queue_action(IDE_PENDING_ACTION_FILTER);
    }
}

#if BUSTER_INCLUDE_TESTS
BUSTER_GLOBAL_LOCAL bool ide_app_test_model_begin(void)
{
    if (ide_state.document_model_ready || ide_state.document_arena || ide_state.document_staging_arena)
    {
        return false;
    }
    ide_state.document_arena = arena_create((ArenaCreation){.reserved_size = BUSTER_MB(32)});
    ide_state.document_staging_arena = arena_create((ArenaCreation){.reserved_size = BUSTER_MB(32)});
    if (!ide_state.document_arena || !ide_state.document_staging_arena)
    {
        if (ide_state.document_arena)
        {
            arena_destroy(ide_state.document_arena, 1);
            ide_state.document_arena = 0;
        }
        if (ide_state.document_staging_arena)
        {
            arena_destroy(ide_state.document_staging_arena, 1);
            ide_state.document_staging_arena = 0;
        }
        return false;
    }
    IdeDocumentErrorKind error = ide_document_model_initialize(&ide_state.document_model, ide_state.document_arena,
                                                                ide_state.document_staging_arena, (IdeDocumentModelOptions){0});
    if (error != IDE_DOCUMENT_ERROR_NONE)
    {
        ide_document_model_deinitialize(&ide_state.document_model);
        arena_destroy(ide_state.document_arena, 1);
        arena_destroy(ide_state.document_staging_arena, 1);
        ide_state.document_arena = 0;
        ide_state.document_staging_arena = 0;
        return false;
    }
    ide_state.document_model_ready = true;
    ide_state.pending_action = IDE_PENDING_ACTION_NONE;
    ide_state.pending_editor_commit = false;
    ide_state.pending_editor_view_commit = false;
    ide_state.pending_editor_view_restore = false;
    ide_state.editor_dirty = false;
    ide_state.editor_revision = 0;
    ide_state.editor_value = (String8){0};
    ide_state.editor_path = (String8){0};
    ide_state.pending_edit_path = (String8){0};
    ide_state.pending_editor_scroll = float2_make(0.0f, 0.0f);
    ide_state.pending_entity_reveal = false;
    ide_app_clear_reload_discard_confirmation();
    ide_state.pending_filter_state = false;
    ide_state.pending_open_path = (String8){0};
    ide_state.pending_edit_path = (String8){0};
    ide_state.pending_entity_path = (String8){0};
    ide_state.path_bar = (String8){0};
    ide_state.query = (String8){0};
    ide_state.status_message = (String8){0};
    ide_state.error_path = (String8){0};
    ide_state.document_error = IDE_DOCUMENT_ERROR_NONE;
    return true;
}

BUSTER_GLOBAL_LOCAL void ide_app_test_model_end(void)
{
    if (ide_state.document_model_ready)
    {
        ide_document_model_deinitialize(&ide_state.document_model);
        ide_state.document_model_ready = false;
    }
    if (ide_state.document_arena)
    {
        arena_destroy(ide_state.document_arena, 1);
        ide_state.document_arena = 0;
    }
    if (ide_state.document_staging_arena)
    {
        arena_destroy(ide_state.document_staging_arena, 1);
        ide_state.document_staging_arena = 0;
    }
    ide_state.pending_action = IDE_PENDING_ACTION_NONE;
    ide_state.pending_editor_commit = false;
    ide_state.pending_editor_view_commit = false;
    ide_state.pending_editor_view_restore = false;
    ide_state.editor_dirty = false;
    ide_state.editor_revision = 0;
    ide_state.editor_value = (String8){0};
    ide_state.editor_path = (String8){0};
    ide_state.pending_edit_path = (String8){0};
    ide_state.pending_editor_scroll = float2_make(0.0f, 0.0f);
    ide_state.pending_entity_reveal = false;
    ide_app_clear_reload_discard_confirmation();
    ide_state.pending_filter_state = false;
    ide_state.pending_open_path = (String8){0};
    ide_state.pending_edit_path = (String8){0};
    ide_state.pending_entity_path = (String8){0};
    ide_state.path_bar = (String8){0};
    ide_state.query = (String8){0};
    ide_state.status_message = (String8){0};
    ide_state.error_path = (String8){0};
    ide_state.document_error = IDE_DOCUMENT_ERROR_NONE;
}

IdeDocumentErrorKind ide_app_test_open_path(String8 path)
{
    IdeDocumentErrorKind result = IDE_DOCUMENT_ERROR_INVALID_ARGUMENT;
    if (ide_app_test_model_begin())
    {
        if (!ide_app_copy_storage(ide_state.path_bar_storage, IDE_OPEN_PATH_CAPACITY - 1, path, &ide_state.path_bar) ||
            !ide_app_request_open(ide_state.path_bar))
        {
            result = ide_state.document_error != IDE_DOCUMENT_ERROR_NONE ? ide_state.document_error : IDE_DOCUMENT_ERROR_INVALID_ARGUMENT;
        }
        else
        {
            ide_app_apply_pending();
            result = ide_state.document_error;
            IdeDocument* document = ide_document_model_active_document(&ide_state.document_model);
            if (result == IDE_DOCUMENT_ERROR_NONE && !document)
            {
                result = IDE_DOCUMENT_ERROR_DOCUMENT_NOT_FOUND;
            }
            if (result == IDE_DOCUMENT_ERROR_NONE && document && !ide_app_path_equals(document, path))
            {
                result = IDE_DOCUMENT_ERROR_DOCUMENT_NOT_FOUND;
            }
        }
        ide_app_test_model_end();
    }
    return result;
}

bool ide_app_test_bad_open_preserves_empty(String8 path)
{
    bool result = false;
    if (ide_app_test_model_begin())
    {
        bool queued = ide_app_copy_storage(ide_state.path_bar_storage, IDE_OPEN_PATH_CAPACITY - 1, path, &ide_state.path_bar) &&
                      ide_app_request_open(ide_state.path_bar);
        if (queued)
        {
            ide_app_apply_pending();
        }
        result = queued && ide_state.document_error == IDE_DOCUMENT_ERROR_PATH_NOT_FOUND &&
                 ide_state.document_model.workspace.root_path.length == 0 &&
                 ide_document_model_active_document(&ide_state.document_model) == 0;
        ide_app_test_model_end();
    }
    return result;
}

bool ide_app_test_reload_discard(String8 path, String8 expected_source, String8 edited_source)
{
    bool result = false;
    if (ide_app_test_model_begin())
    {
        bool opened = ide_app_copy_storage(ide_state.path_bar_storage, IDE_OPEN_PATH_CAPACITY - 1, path, &ide_state.path_bar) &&
                      ide_app_request_open(ide_state.path_bar);
        if (opened)
        {
            ide_app_apply_pending();
            IdeDocument* document = ide_document_model_active_document(&ide_state.document_model);
            opened = ide_state.document_error == IDE_DOCUMENT_ERROR_NONE && document && ide_app_path_equals(document, path);
        }
        if (opened && ide_document_model_set_text(&ide_state.document_model, path, edited_source) == IDE_DOCUMENT_ERROR_NONE)
        {
            bool first_click = !ide_app_request_reload_discard() && ide_state.reload_discard_confirmation_pending &&
                               ide_state.pending_action == IDE_PENDING_ACTION_NONE;
            // A revision change after the first confirmation must invalidate
            // that confirmation. The next click is another confirmation, not
            // an implicit discard of the newer edit.
            String8 second_edit = S8("code dirty from the application bridge again : fn () s32 { return 10; }\n");
            bool edited_again = first_click && ide_document_model_set_text(&ide_state.document_model, path, second_edit) == IDE_DOCUMENT_ERROR_NONE;
            bool reconfirmed = edited_again && !ide_app_request_reload_discard() && ide_state.reload_discard_confirmation_pending &&
                               ide_state.pending_action == IDE_PENDING_ACTION_NONE;
            bool second_click = reconfirmed && ide_app_request_reload_discard() && ide_state.pending_action == IDE_PENDING_ACTION_RELOAD_DISCARD;
            if (first_click && edited_again && reconfirmed && second_click)
            {
                ide_app_apply_pending();
                IdeDocument* reloaded = ide_document_model_active_document(&ide_state.document_model);
                result = ide_state.document_error == IDE_DOCUMENT_ERROR_NONE && reloaded && !reloaded->dirty &&
                         string_equal(reloaded->source, expected_source);
            }
        }
        ide_app_test_model_end();
    }
    return result;
}

bool ide_app_test_scroll_round_trip(String8 first_path, String8 second_path)
{
    bool result = false;
    if (ide_app_test_model_begin())
    {
        bool opened_first = ide_app_request_open(first_path);
        if (opened_first)
        {
            ide_app_apply_pending();
            IdeDocument* first = ide_document_model_active_document(&ide_state.document_model);
            opened_first = ide_state.document_error == IDE_DOCUMENT_ERROR_NONE && first && ide_app_path_equals(first, first_path);
            if (opened_first)
            {
                BUSTER_UNUSED(ide_app_copy_storage(ide_state.editor_path_storage, IDE_OPEN_PATH_CAPACITY - 1, first->identity,
                                                   &ide_state.editor_path));
                ide_state.editor_revision = first->revision;
                ide_state.pending_editor_scroll = float2_make(23.0f, 47.0f);
                ide_state.pending_editor_view_commit = true;
                ide_app_apply_pending();
                first = ide_document_model_active_document(&ide_state.document_model);
                opened_first = first && !first->dirty && first->view.scroll_x == 23.0f && first->view.scroll_y == 47.0f;
            }
        }
        bool opened_second = opened_first && ide_app_request_open(second_path);
        if (opened_second)
        {
            ide_app_apply_pending();
            IdeDocument* second = ide_document_model_active_document(&ide_state.document_model);
            opened_second = ide_state.document_error == IDE_DOCUMENT_ERROR_NONE && second && ide_app_path_equals(second, second_path);
        }
        bool reopened_first = opened_second && ide_app_request_open(first_path);
        if (reopened_first)
        {
            ide_app_apply_pending();
            IdeDocument* first_again = ide_document_model_active_document(&ide_state.document_model);
            reopened_first = first_again && first_again->view.scroll_x == 23.0f && first_again->view.scroll_y == 47.0f;
        }
        result = opened_first && opened_second && reopened_first;
        ide_app_test_model_end();
    }
    return result;
}

bool ide_app_test_filter_state(String8 path, String8 query)
{
    bool result = false;
    if (ide_app_test_model_begin())
    {
        bool opened = ide_app_request_open(path);
        if (opened)
        {
            ide_app_apply_pending();
            opened = ide_state.document_error == IDE_DOCUMENT_ERROR_NONE && ide_document_model_active_document(&ide_state.document_model) != 0;
        }
        if (opened && ide_app_copy_storage(ide_state.query_storage, IDE_QUERY_CAPACITY - 1, query, &ide_state.query))
        {
            ide_state.pending_filter = (IdeDocumentWorkspaceFilterState){
                .query = ide_state.query,
                .show_open_only = true,
                .show_dirty_only = true,
                .show_diagnostics_only = true,
                .case_sensitive = true,
            };
            ide_state.pending_filter_state = true;
            ide_app_queue_action(IDE_PENDING_ACTION_FILTER);
            ide_app_apply_pending();
            IdeDocumentWorkspaceFilterState filter = ide_state.document_model.workspace.filter;
            result = ide_state.document_error == IDE_DOCUMENT_ERROR_NONE && string_equal(filter.query, query) && filter.show_open_only &&
                     filter.show_dirty_only && filter.show_diagnostics_only && filter.case_sensitive;
        }
        ide_app_test_model_end();
    }
    return result;
}

bool ide_app_test_oversized_request_preserves_action(String8 preserved_path)
{
    bool result = false;
    if (ide_app_test_model_begin())
    {
        char8 oversized_storage[IDE_OPEN_PATH_CAPACITY + 1];
        for (u64 index = 0; index < IDE_OPEN_PATH_CAPACITY; index += 1)
        {
            oversized_storage[index] = 'x';
        }
        oversized_storage[IDE_OPEN_PATH_CAPACITY] = 0;
        String8 oversized = {.pointer = oversized_storage, .length = IDE_OPEN_PATH_CAPACITY};
        BUSTER_UNUSED(ide_app_copy_storage(ide_state.pending_open_storage, IDE_OPEN_PATH_CAPACITY - 1, preserved_path,
                                           &ide_state.pending_open_path));
        ide_state.pending_action = IDE_PENDING_ACTION_SAVE;
        bool open_rejected = !ide_app_request_open(oversized);
        bool open_preserved = ide_state.pending_action == IDE_PENDING_ACTION_SAVE && string_equal(ide_state.pending_open_path, preserved_path);
        IdeDocumentEntitySnapshot entity = {.source_path = oversized};
        bool entity_rejected = !ide_app_request_entity_activation(&entity);
        bool entity_preserved = ide_state.pending_action == IDE_PENDING_ACTION_SAVE && string_equal(ide_state.pending_open_path, preserved_path);
        result = open_rejected && open_preserved && entity_rejected && entity_preserved;
        ide_app_test_model_end();
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool ide_app_test_disk_matches(String8 path, String8 expected)
{
    TemporalArena scratch = scratch_begin(0, 0);
    ByteSlice bytes = file_read(scratch.arena, path, (FileReadOptions){0});
    bool result = bytes.length == expected.length &&
                  (!bytes.length || (bytes.pointer && expected.pointer && memcmp(bytes.pointer, expected.pointer, bytes.length) == 0));
    scratch_end(scratch);
    return result;
}

bool ide_app_test_failed_editor_commit_preserves_buffer(String8 path, String8 edited_source)
{
    bool result = false;
    if (ide_app_test_model_begin())
    {
        bool opened = ide_app_request_open(path);
        if (opened)
        {
            ide_app_apply_pending();
            IdeDocument* document = ide_document_model_active_document(&ide_state.document_model);
            opened = ide_state.document_error == IDE_DOCUMENT_ERROR_NONE && document && ide_app_path_equals(document, path);
            if (opened)
            {
                ide_app_sync_editor();
            }
        }

        IdeDocument* document = ide_document_model_active_document(&ide_state.document_model);
        String8 original_source = document ? document->source : (String8){0};
        bool buffer_ready = opened && document && ide_app_copy_storage(ide_state.editor_storage, IDE_EDITOR_CAPACITY - 1, edited_source,
                                                                         &ide_state.editor_value) &&
                            ide_app_copy_storage(ide_state.pending_edit_storage, IDE_OPEN_PATH_CAPACITY - 1, document->path,
                                                 &ide_state.pending_edit_path);
        if (buffer_ready)
        {
            ide_state.editor_dirty = true;
            ide_state.pending_editor_commit = true;
            ide_state.document_model.max_diagnostics = 0;
            ide_state.document_error = IDE_DOCUMENT_ERROR_NONE;
            ide_app_apply_pending();

            document = ide_document_model_active_document(&ide_state.document_model);
            bool commit_failed_without_loss = ide_state.document_error == IDE_DOCUMENT_ERROR_DIAGNOSTIC_LIMIT &&
                                               ide_state.editor_dirty && ide_state.pending_editor_commit && document &&
                                               string_equal(document->source, original_source) && string_equal(ide_state.editor_value, edited_source);
            bool close_blocked_after_commit_failure = ide_app_has_unsaved_changes();

            ide_state.document_error = IDE_DOCUMENT_ERROR_NONE;
            ide_state.pending_action = IDE_PENDING_ACTION_SAVE;
            ide_app_apply_pending();
            document = ide_document_model_active_document(&ide_state.document_model);
            bool save_did_not_write_stale_source = ide_state.document_error == IDE_DOCUMENT_ERROR_DIAGNOSTIC_LIMIT &&
                                                   ide_state.editor_dirty && ide_state.pending_editor_commit && document &&
                                                   !document->dirty && string_equal(document->source, original_source) &&
                                                   string_equal(ide_state.editor_value, edited_source) && ide_app_test_disk_matches(path, original_source);
            bool close_blocked_after_save_attempt = ide_app_has_unsaved_changes();

            ide_state.document_model.max_diagnostics = IDE_DOCUMENT_DEFAULT_MAX_DIAGNOSTICS;
            ide_state.document_error = IDE_DOCUMENT_ERROR_NONE;
            bool retry_queued = ide_app_queue_action(IDE_PENDING_ACTION_SAVE);
            if (retry_queued)
            {
                ide_app_apply_pending();
                document = ide_document_model_active_document(&ide_state.document_model);
                bool buffer_committed = ide_state.document_error == IDE_DOCUMENT_ERROR_NONE && document &&
                                        string_equal(document->source, edited_source) && document->dirty && !ide_state.editor_dirty &&
                                        !ide_state.pending_editor_commit && ide_state.pending_action == IDE_PENDING_ACTION_SAVE;
                ide_app_apply_pending();
                document = ide_document_model_active_document(&ide_state.document_model);
                bool buffer_saved = ide_state.document_error == IDE_DOCUMENT_ERROR_NONE && document && !document->dirty &&
                                    !ide_state.editor_dirty && !ide_state.pending_editor_commit && ide_app_test_disk_matches(path, edited_source) &&
                                    !ide_app_has_unsaved_changes();
                result = commit_failed_without_loss && close_blocked_after_commit_failure && save_did_not_write_stale_source &&
                         close_blocked_after_save_attempt && buffer_committed && buffer_saved;
            }
        }
        ide_app_test_model_end();
    }
    return result;
}

bool ide_app_test_drop_preserves_first(SliceString8 first, SliceString8 second, String8 expected_path)
{
    ide_state.pending_action = IDE_PENDING_ACTION_NONE;
    ide_state.pending_open_path = (String8){0};
    ide_state.document_error = IDE_DOCUMENT_ERROR_NONE;
    ide_app_stage_drop(first);
    bool first_accepted = ide_state.pending_action == IDE_PENDING_ACTION_OPEN && string_equal(ide_state.pending_open_path, expected_path);
    ide_app_stage_drop(second);
    bool result = first_accepted && ide_state.pending_action == IDE_PENDING_ACTION_OPEN && string_equal(ide_state.pending_open_path, expected_path);
    ide_state.pending_action = IDE_PENDING_ACTION_NONE;
    ide_state.pending_open_path = (String8){0};
    ide_state.path_bar = (String8){0};
    ide_state.status_message = (String8){0};
    ide_state.document_error = IDE_DOCUMENT_ERROR_NONE;
    return result;
}

bool ide_app_test_copy_storage_self(void)
{
    char8 storage[64] = {0};
    String8 initial = {0};
    String8 result = {0};
    bool first = ide_app_copy_storage(storage, BUSTER_ARRAY_LENGTH(storage) - 1, S8("self-copy"), &initial);
    bool second = ide_app_copy_storage(storage, BUSTER_ARRAY_LENGTH(storage) - 1, initial, &result);
    return first && second && result.pointer == storage && string_equal(result, S8("self-copy"));
}
#endif

BUSTER_GLOBAL_LOCAL u8 ide_app_shortcut_letter(WmKey key)
{
    switch (key)
    {
        case WM_KEY_O:
            return (u8)'o';
        case WM_KEY_S:
            return (u8)'s';
        case WM_KEY_R:
            return (u8)'r';
        default:
            break;
    }
    return 0;
}

BUSTER_GLOBAL_LOCAL void ide_app_process_keyboard_shortcuts(WmEventList events)
{
    u8 control_mask = (u8)(1u << WM_MODIFIER_CONTROL);
    u8 shift_mask = (u8)(1u << WM_MODIFIER_SHIFT);
    u8 alt_mask = (u8)(1u << WM_MODIFIER_ALT);
    for (WmEvent* event = events.first; event; event = event->next)
    {
        if (event->kind != WM_EVENT_KEY_PRESS || (event->modifiers & (shift_mask | alt_mask)) || !(event->modifiers & control_mask))
        {
            continue;
        }
        IdeDocumentShortcutAction shortcut = ide_document_shortcut_action(ide_app_shortcut_letter(event->key), event->modifiers,
                                                                          WM_MODIFIER_CONTROL, (u8)(shift_mask | alt_mask));
        switch (shortcut)
        {
            case IDE_DOCUMENT_SHORTCUT_OPEN:
                ide_app_request_open(ide_state.path_bar);
                break;
            case IDE_DOCUMENT_SHORTCUT_SAVE:
                ide_app_queue_action(IDE_PENDING_ACTION_SAVE);
                break;
            case IDE_DOCUMENT_SHORTCUT_RELOAD:
                ide_app_queue_action(IDE_PENDING_ACTION_RELOAD);
                break;
            case IDE_DOCUMENT_SHORTCUT_NONE:
            case IDE_DOCUMENT_SHORTCUT_ACTION_COUNT:
                break;
        }
    }
}

BUSTER_GLOBAL_LOCAL u64 frame_depth = 0;

bool frame(void)
{
    frame_depth += 1;

    TimeDataType frame_end = timestamp_take();

    WmEventList event_list = {0};
    if (frame_depth == 1)
    {
        event_list = wm_poll_events(ide_state.state.arena, ide_state.windowing);
    }

    f64 frame_ms = (f64)timestamp_ns_between(ide_state.last_frame_timestamp, frame_end) / (1000 * 1000);
    ide_state.last_frame_timestamp = frame_end;
    ide_app_sync_editor();
    ide_app_process_keyboard_shortcuts(event_list);

    for (WmEvent* event = event_list.first; event; event = event->next)
    {
        switch (event->kind)
        {
            break;
        case WM_EVENT_WINDOW_CLOSE:
        {
            if (ide_app_has_unsaved_changes())
            {
                ide_app_set_status(S8("save all dirty documents and editor buffers before closing the IDE"));
                break;
            }
            for (IdeWindow* window = ide_state.first_window; window; window = window->next)
            {
                if (window->wm == event->window)
                {
                    if (window->previous)
                    {
                        window->previous->next = window->next;
                    }

                    if (window->next)
                    {
                        window->next->previous = window->previous;
                    }

                    if (ide_state.first_window == window)
                    {
                        ide_state.first_window = window->next;
                    }

                    if (ide_state.last_window == window)
                    {
                        ide_state.last_window = window->previous;
                    }

                    ide_window_deinitialize(window, ide_state.rendering);

                    break;
                }
            }
        }
        break;
        case WM_EVENT_TEXT_INPUT:
        {
            string_print(S8("User wrote \"{S8}\"\n"), event->text);
        }
        break;
        case WM_EVENT_FILE_DROP:
        {
            if (!event->window || (ide_state.first_window && event->window == ide_state.first_window->wm))
            {
                ide_app_stage_drop(event->paths);
            }
        }
        break;
        default:
        {
        }
        break;
        case WM_EVENT_COUNT:
            BUSTER_UNREACHABLE();
        }
    }

#if BUSTER_ANDROID
    {
        // While backgrounded/locked the native window (and its Vulkan surface)
        // is gone: skip rendering instead of crashing. On resume, rebuild the
        // surface/swapchain for the new native window before drawing again.
        static bool was_paused = false;
        if (!wm_window_is_visible(ide_state.windowing))
        {
            was_paused = true;
            frame_depth -= 1;
            return false;
        }
        if (was_paused)
        {
            was_paused = false;
            for (IdeWindow* w = ide_state.first_window; w; w = w->next)
            {
                if (w->render)
                {
                    rendering_window_surface_recreate(ide_state.rendering, ide_state.windowing, w->render, w->wm);
                }
            }
        }
    }
#endif

    IdeWindow* window = ide_state.first_window;
    while (window)
    {
        IdeWindow* next = window->next;

        RenderingWindowHandle* render_window = window->render;
        rendering_window_frame_begin(ide_state.rendering, render_window);
        ide_window_update_font_for_dpi(window);

        ui_state_select(window->ui);

        TemporalArena ui_events_scratch = scratch_begin(0, 0);
        UI_EventList ui_events = ui_event_list_from_wm_events(ui_events_scratch.arena, window->wm, event_list);
        ui_build_begin(ide_state.windowing, window->wm, frame_ms, ui_events);

        ui_push(font_size, window->font_size);

        ide_ui_top_bar();
        ide_ui_workspace();

        ui_build_end();

        if (ide_state.editor_scroll_box)
        {
            float2 current_scroll = ui_box_scroll_offset(ide_state.editor_scroll_box);
            f32 current_x = float2_element(current_scroll, AXIS2_X);
            f32 current_y = float2_element(current_scroll, AXIS2_Y);
            f32 previous_x = float2_element(ide_state.pending_editor_scroll, AXIS2_X);
            f32 previous_y = float2_element(ide_state.pending_editor_scroll, AXIS2_Y);
            if (current_x != previous_x || current_y != previous_y)
            {
                ide_state.pending_editor_view_commit = true;
            }
            ide_state.pending_editor_scroll = current_scroll;
        }

        ui_draw();

        BUSTER_UNUSED(ui_pop(font_size));

        rendering_window_frame_end(ide_state.rendering, render_window);
        scratch_end(ui_events_scratch);

        window = next;
    }

    ide_app_apply_pending();

    frame_depth -= 1;

    bool result = !ide_state.first_window;
    return result;
}

void async_user_tick(void)
{
}

BUSTER_GLOBAL_LOCAL ProcessResult run_graphical_app(void)
{
    ProcessResult result = PROCESS_RESULT_SUCCESS;

    ide_state.document_arena = arena_create((ArenaCreation){.reserved_size = BUSTER_MB(128)});
    ide_state.document_staging_arena = arena_create((ArenaCreation){.reserved_size = BUSTER_MB(128)});
    if (!ide_state.document_arena || !ide_state.document_staging_arena)
    {
        string_print(S8("Failed to allocate IDE document model arenas\n"));
        result = PROCESS_RESULT_FAILED;
    }
    else
    {
        IdeDocumentErrorKind error = ide_document_model_initialize(&ide_state.document_model, ide_state.document_arena,
                                                                    ide_state.document_staging_arena, (IdeDocumentModelOptions){0});
        if (error != IDE_DOCUMENT_ERROR_NONE)
        {
            string_print(S8("Failed to initialize IDE document model: {S8}\n"), ide_document_error_kind_name(error));
            result = PROCESS_RESULT_FAILED;
        }
        else
        {
            ide_state.document_model_ready = true;
            ide_app_copy_storage(ide_state.path_bar_storage, IDE_OPEN_PATH_CAPACITY - 1, (String8){0}, &ide_state.path_bar);
            ide_app_copy_storage(ide_state.query_storage, IDE_QUERY_CAPACITY - 1, (String8){0}, &ide_state.query);
            ide_state.metadata_show_path = true;
            ide_state.metadata_show_type = true;
            ide_state.metadata_show_location = true;
            ide_state.metadata_show_diagnostics = true;

            String8 startup_path = ide_state.startup_open_path;
            ide_state.startup_open_path = (String8){0};
            if (ide_state.startup_open_path_too_long || startup_path.length > IDE_OPEN_PATH_CAPACITY - 1)
            {
                ide_state.startup_open_path_too_long = true;
                ide_state.document_error = IDE_DOCUMENT_ERROR_INVALID_ARGUMENT;
                ide_state.error_path = (String8){0};
                ide_state.status_message = (String8){0};
            }
            else if (startup_path.length)
            {
                ide_app_copy_storage(ide_state.path_bar_storage, IDE_OPEN_PATH_CAPACITY - 1, startup_path, &ide_state.path_bar);
                error = ide_document_model_open_path(&ide_state.document_model, ide_state.path_bar);
                if (error != IDE_DOCUMENT_ERROR_NONE)
                {
                    ide_app_set_error(error, ide_state.path_bar);
                }
                else
                {
                    IdeDocument* document = ide_document_model_active_document(&ide_state.document_model);
                    if (document)
                    {
                        ide_app_copy_storage(ide_state.path_bar_storage, IDE_OPEN_PATH_CAPACITY - 1, document->path, &ide_state.path_bar);
                    }
                    ide_app_set_status(S8("document opened"));
                }
            }
        }
    }

    WmHandle* windowing = 0;
    if (result == PROCESS_RESULT_SUCCESS)
    {
        windowing = ide_state.windowing = wm_initialize();
    }
    if (windowing)
    {
        Arena* arena = program_state->arena;
        RenderingHandle* r = ide_state.rendering = rendering_initialize(arena);
        if (r)
        {
            ide_state.first_window = ide_state.last_window = arena_allocate(arena, IdeWindow, 1);
            ide_state.first_window->previous = 0;
            ide_state.first_window->next = 0;
            WmWindowHandle* wm_window = wm_window_create(windowing, (WmWindowCreate){
                                                                        .name = S8("Ide"),
                                                                        .size =
                                                                            {
                                                                                .width = 1600,
                                                                                .height = 900,
                                                                            },
                                                                    });
            ide_state.first_window->wm = wm_window;

            if (wm_window)
            {
                RenderingWindowHandle* render_window = ide_state.first_window->render = rendering_window_initialize(arena, windowing, r, wm_window);

                if (render_window)
                {
                    ide_state.first_window->ui = ui_state_allocate(r, render_window);
                    ide_state.first_window->root_panel = arena_allocate(ide_state.state.arena, IdePanel, 1);
                    ide_state.first_window->root_panel->parent_percentage = 1.0f;
                    ide_state.first_window->root_panel->split_axis = AXIS2_X;

                    rendering_window_rect_texture_update_begin(ide_state.first_window->render);

                    f32 dpi = wm_window_get_dpi(windowing, wm_window);
                    TextureIndex white_texture = white_texture_create(ide_state.state.arena, ide_state.rendering);

                    rendering_window_queue_rect_texture_update(ide_state.rendering, ide_state.first_window->render, RECT_TEXTURE_SLOT_WHITE, white_texture);
                    ide_window_queue_font_update(ide_state.first_window, dpi);

                    rendering_window_rect_texture_update_end(ide_state.rendering, ide_state.first_window->render);

                    ide_state.last_frame_timestamp = timestamp_take();

                    bool test = ide_state.test && !program_flag_get(PROGRAM_FLAG_TEST_PERSIST);
                    u64 loop_times = test ? (u64)3 : UINT64_MAX;
                    for (u64 i = 0; i < loop_times && ide_state.first_window; i += 1)
                    {
                        bool quit = update();
                        if (quit)
                        {
                            break;
                        }
                    }

                    if (test)
                    {
#if BUSTER_IOS
                        // The iOS worker thread calls exit() right after this
                        // returns, so the OS reclaims all GPU/window resources.
                        // Skip the explicit teardown: in a headless simulator the
                        // last presented drawable's command buffer never completes
                        // (nothing drives a subsequent vsync), so
                        // rendering_window_deinitialize's waitUntilCompleted would
                        // block forever and the BUSTER_IOS_RESULT marker would
                        // never be printed.
#else
                        for (IdeWindow* window = ide_state.first_window; window;)
                        {
                            IdeWindow* next = window->next;
                            ide_window_deinitialize(window, ide_state.rendering);
                            window = next;
                        }
                        ide_state.first_window = 0;
                        ide_state.last_window = 0;
#endif
                    }

                    // Process-wide thread/TLS, mutex, arena, and socket state
                    // is released by buster_entry_point after this returns.
                }
                else
                {
                    string_print(S8("Failed to create render window\n"));
                    result = PROCESS_RESULT_FAILED;
                }
            }
            else
            {
                string_print(S8("Failed to create window\n"));
                result = PROCESS_RESULT_FAILED;
            }

            rendering_deinitialize(r);
        }
        else
        {
            string_print(S8("Failed to initialize rendering\n"));
            result = PROCESS_RESULT_FAILED;
        }

        wm_deinitialize(windowing);
    }
    else
    {
        if (result == PROCESS_RESULT_SUCCESS)
        {
            string_print(S8("Failed to initialize windowing\n"));
            result = PROCESS_RESULT_FAILED;
        }
    }

    if (ide_state.document_model_ready)
    {
        ide_document_model_deinitialize(&ide_state.document_model);
        ide_state.document_model_ready = false;
    }
    if (ide_state.document_arena)
    {
        arena_destroy(ide_state.document_arena, 1);
        ide_state.document_arena = 0;
    }
    if (ide_state.document_staging_arena)
    {
        arena_destroy(ide_state.document_staging_arena, 1);
        ide_state.document_staging_arena = 0;
    }

    return result;
}

// Deliberately independent of the windowing/rendering path `test` drives via
// run_graphical_app(): bench must run headless on a plain CI runner with no
// display server, and BUSTER_INCLUDE_TESTS off must not disable it either.
BUSTER_GLOBAL_LOCAL void print_benchmark_result(String8 label, ParserBenchResult result)
{
    string_print(
        S8("{S8} parse_all_tests iterations={u64} files={u64} min_ns={u64} median_ns={u64}\n"), label, result.iterations, result.file_count,
        result.min_ns, result.median_ns);

#if BUSTER_INSTRUMENT
    string_print(S8("{S8}_PHASE tokenize min_ns={u64} median_ns={u64}\n"), label, result.tokenize_min_ns, result.tokenize_median_ns);
    string_print(S8("{S8}_PHASE parse min_ns={u64} median_ns={u64}\n"), label, result.parse_min_ns, result.parse_median_ns);
    for (u64 i = 0; i < result.file_count; i += 1)
    {
        ParserBenchFileResult file_result = result.files[i];
        string_print(S8("{S8}_FILE path={S8} min_ns={u64} median_ns={u64}\n"), label, file_result.path, file_result.min_ns, file_result.median_ns);
    }
#endif
}

BUSTER_GLOBAL_LOCAL ProcessResult run_benchmarks(void)
{
    Arena* arena = arena_create((ArenaCreation){0});

    ParserBenchResult io_result = parser_bench_run(arena, 200, PARSER_BENCH_MODE_IO);
    ParserBenchResult parse_result = parser_bench_run(arena, 200, PARSER_BENCH_MODE_PARSE);
    if (!io_result.source_load_succeeded || !parse_result.source_load_succeeded)
    {
        string_print(S8("parser benchmark source load failed\n"));
        arena_destroy(arena, 1);
        return PROCESS_RESULT_FAILED;
    }

    print_benchmark_result(S8("BENCH_IO"), io_result);
    print_benchmark_result(S8("BENCH_PARSE"), parse_result);

    arena_destroy(arena, 1);
    return PROCESS_RESULT_SUCCESS;
}

BUSTER_GLOBAL_LOCAL ProcessResult run_app(void)
{
#if BUSTER_INCLUDE_TESTS
    if (ide_state.test)
    {
        // Driver tests intentionally retain several native and cross-target
        // artifacts until their module finishes. Reserve enough virtual
        // address space for platforms whose native artifacts are larger.
        Arena* arena = arena_create((ArenaCreation){
            .reserved_size = BUSTER_MB(256),
        });
        UnitTestArguments arguments = {arena, &default_show};

        u64 position = arena->position;
        BatchTestResult batch_test_result = library_tests(&arguments);
        arena->position = position;

        if (program_flag_get(PROGRAM_FLAG_CI))
        {
            ProcessResult app_test_result = run_graphical_app();
            consume_external_tests(&batch_test_result, app_test_result);
        }

        position = arena->position;
        ProcessResult result = batch_test_report(&arguments, batch_test_result) ? PROCESS_RESULT_SUCCESS : PROCESS_RESULT_FAILED;
        arena->position = position;

        arena_destroy(arena, 1);
        return result;
    }
#endif

    return run_graphical_app();
}

BUSTER_GLOBAL_LOCAL ProcessResult run_compiler(void)
{
    Arena* arena = arena_create((ArenaCreation){
        .reserved_size = BUSTER_GB(1),
    });
    if (!arena)
    {
        return PROCESS_RESULT_FAILED;
    }
    String8 output_path = ide_state.compile_output_path;
    if (!output_path.length)
    {
#if BUSTER_WINDOWS
        output_path = S8("a.exe");
#else
        output_path = S8("a.out");
#endif
    }
    CompilerDriverResult compile = compiler_driver_compile(arena, (CompilerDriverOptions){
                                                                      .source_path = ide_state.compile_source_path,
                                                                      .output_path = output_path,
                                                                      .module_root = ide_state.compile_module_root,
                                                                      .target = target_native,
                                                                      .debug_info = ide_state.compile_debug_info,
                                                                  });
    ProcessResult result = PROCESS_RESULT_SUCCESS;
    if (compile.error != COMPILER_DRIVER_ERROR_NONE)
    {
        string_print(S8("compile failed: {S8}\n"), compile.diagnostic);
        result = PROCESS_RESULT_FAILED;
    }
    else
    {
        string_print(S8("compiled {S8} -> {S8}\n"), ide_state.compile_source_path, output_path);
    }
    arena_destroy(arena, 1);
    return result;
}

BUSTER_GLOBAL_LOCAL ProcessResult run_c_compiler(void)
{
    Arena* arena = arena_create((ArenaCreation){
        .reserved_size = COMPILER_DRIVER_C_TRANSLATION_UNIT_RESERVED_SIZE,
    });
    if (!arena)
    {
        return PROCESS_RESULT_FAILED;
    }
    CompilerDriverInvocation invocation = compiler_driver_parse_arguments(arena, ide_state.cc_arguments);
    CompilerDriverResult compile = compiler_driver_execute_invocation(arena, invocation);
    ProcessResult result = PROCESS_RESULT_SUCCESS;
    if (compile.warning.length)
    {
        string_print(S8("{S8}"), compile.warning);
    }
    if (compile.error != COMPILER_DRIVER_ERROR_NONE)
    {
        string_print(S8("cc: error: {S8}\n"), compile.diagnostic);
        result = PROCESS_RESULT_FAILED;
    }
    else if (!invocation.output_path.length)
    {
        string_print(S8("{S8}"), compile.output);
    }
    if (compile.error == COMPILER_DRIVER_ERROR_NONE && invocation.verbose)
    {
        string_print(S8("TARGET cpu={S8} features={S8}\n"), cpu_model_to_string_os(invocation.target.cpu_model),
                     target_cpu_features_to_string(arena, invocation.target));
    }
    if (compile.error == COMPILER_DRIVER_ERROR_NONE && invocation.verbose && compile.codegen_statistics.function_count)
    {
        string_print(S8("CODEGEN cpu={S8} vector_bits={u32} functions={u32} instructions={u64} values={u64} stack_value_bytes={u64} stack_frame_bytes={u64} "
                        "max_stack_frame_bytes={u32} code_bytes={u64} forwarded_wide_vector_loads={u64} native_vector_operations={u64} "
                        "split_vector_operations={u64} vzeroupper={u64}\n"),
                     cpu_model_to_string_os(invocation.target.cpu_model), target_vector_register_size(invocation.target) * 8,
                     compile.codegen_statistics.function_count, compile.codegen_statistics.instruction_count, compile.codegen_statistics.value_count,
                     compile.codegen_statistics.stack_value_bytes, compile.codegen_statistics.stack_frame_bytes,
                     compile.codegen_statistics.maximum_stack_frame_bytes, compile.codegen_statistics.code_bytes,
                     compile.codegen_statistics.forwarded_wide_vector_load_count, compile.codegen_statistics.native_vector_operation_count,
                     compile.codegen_statistics.split_vector_operation_count, compile.codegen_statistics.vzeroupper_count);
    }
    arena_destroy(arena, 1);
    return result;
}

#if BUSTER_FUZZ_AVAILABLE
// The release below can run from an atexit handler, which fires after
// buster_entry_point has destroyed the program arena. The path therefore lives
// in static storage rather than in that arena, whose pages are unmapped by then.
BUSTER_GLOBAL_LOCAL char8 ide_fuzz_output_corpus_storage[512];
BUSTER_GLOBAL_LOCAL String8 ide_fuzz_output_corpus = {0};

BUSTER_GLOBAL_LOCAL String8 ide_fuzz_scratch_root(void)
{
#if BUSTER_WINDOWS
    // The repo's own build tree is the one scratch location every platform
    // agrees on; Windows has no fixed /tmp.
    return S8("build/");
#else
    return S8("/tmp/");
#endif
}

BUSTER_GLOBAL_LOCAL void ide_fuzz_output_corpus_set(String8 path)
{
    if (!path.length || path.length >= BUSTER_ARRAY_LENGTH(ide_fuzz_output_corpus_storage))
    {
        return;
    }
    memcpy(ide_fuzz_output_corpus_storage, path.pointer, path.length);
    ide_fuzz_output_corpus_storage[path.length] = 0;
    ide_fuzz_output_corpus = string_from_pointer_length(ide_fuzz_output_corpus_storage, path.length);
}

BUSTER_GLOBAL_LOCAL void ide_fuzz_output_corpus_release(void)
{
    // os_directory_delete needs a scratch arena. When FuzzerDriver returns
    // rather than exiting, buster_entry_point releases the thread context
    // before atexit handlers run and there is nothing left to delete with; the
    // explicit release after buster_fuzz_run covers that path instead.
    if (ide_fuzz_output_corpus.length && thread_context_selected())
    {
        os_directory_delete(ide_fuzz_output_corpus);
        ide_fuzz_output_corpus = (String8){0};
    }
}
#endif

ProcessResult entry_point(void)
{
#if BUSTER_FUZZ_AVAILABLE
    if (ide_state.fuzz)
    {
        return buster_fuzz_run(ide_state.fuzz_arguments);
    }
#endif
    if (ide_state.cc)
    {
        return run_c_compiler();
    }
    if (ide_state.compile)
    {
        return run_compiler();
    }
    if (ide_state.bench)
    {
        return run_benchmarks();
    }
    if (ide_state.test_app)
    {
        return run_graphical_app();
    }

    ProcessResult result = run_app();
#if BUSTER_FUZZ_AVAILABLE
    if (ide_state.test)
    {
        // libFuzzer writes every newly discovered unit into the *first*
        // positional directory, so `tests/fuzz` must not be that directory:
        // the bounded session would otherwise leave hundreds of untracked
        // files in the working tree on every `test_all` run. Give it a
        // throwaway output corpus and pass the checked-in seeds read-only.
        ide_fuzz_output_corpus_set(string_format_z(program_state->arena, S8("{S8}buster-fuzz-corpus-{u64}"), ide_fuzz_scratch_root(),
                                                   os_get_current_process_id()));
        os_make_directory(ide_fuzz_output_corpus);
        // FuzzerDriver ends a timed session with exit(0) and never returns, so
        // the corpus needs an exit handler. It does return on flag and corpus
        // errors, and the runtime is torn down before atexit handlers run in
        // that case, so the explicit release below is the one that fires there.
        atexit(&ide_fuzz_output_corpus_release);
        // Crash artifacts default to `./`, which would drop them in the repo
        // root. This is a filename prefix rather than a directory, so a clean
        // run creates nothing and a crash leaves one plainly named file behind
        // instead of being swept up with the corpus.
        String8 artifact_prefix = string_format_z(program_state->arena, S8("-artifact_prefix={S8}buster-fuzz-{u64}-"), ide_fuzz_scratch_root(),
                                                  os_get_current_process_id());
        String8 fuzz_arguments[] = {
            S8("-max_len=65536"),
            S8("-max_total_time=2"),
            artifact_prefix,
            ide_fuzz_output_corpus,
            S8("tests/fuzz"),
        };
        ProcessResult fuzz_result = buster_fuzz_run((SliceString8)BUSTER_ARRAY_TO_SLICE(fuzz_arguments));
        ide_fuzz_output_corpus_release();
        if (result == PROCESS_RESULT_SUCCESS)
        {
            result = fuzz_result;
        }
    }
#endif
    return result;
}
