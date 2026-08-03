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
#include <buster/lib/compiler/ir/ir.h>
#include <buster/lib/compiler/ir/interpreter.h>
#include <buster/lib/compiler/debug/debug.h>
#include <buster/lib/compiler/codegen/codegen.h>
#include <buster/lib/compiler/object/object.h>
#include <buster/lib/compiler/link/link.h>
#include <buster/lib/compiler/driver/driver.h>
#include <buster/lib/integer.h>
#include <buster/lib/string.h>
#if BUSTER_INCLUDE_TESTS
#include <buster/tests/test.h>
#endif

#if BUSTER_UNITY_BUILD
#include <buster/lib/arena.c>
#include <buster/lib/integer.c>
#include <buster/lib/os.c>
#include <buster/lib/string.c>
#if BUSTER_INCLUDE_TESTS
#include <buster/tests/test.c>
#endif
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
    u8 reserved;
    TimeDataType last_frame_timestamp;
};

BUSTER_GLOBAL_LOCAL IdeProgram ide_state = {0};

BUSTER_V_IMPL ProgramState* program_state = &ide_state.state;

#define IDE_BASE_DPI (96.0f)
#define IDE_BASE_FONT_SIZE (24.0f)

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

#if BUSTER_FUZZ_AVAILABLE
s32 buster_fuzz_test_input(const u8* pointer, size_t size)
{
    if (size > BUSTER_KB(4))
    {
        return 0;
    }
    if (!pointer && size)
    {
        return 0;
    }

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

    return result;
}

BUSTER_GLOBAL_LOCAL void ui_top_bar(void)
{
    ui_push(pref_height, ui_em(1, 1));
    {
        ui_push(child_layout_axis, AXIS2_X);
        UI_Box* top_bar = ui_box_make((UI_BoxFlags){0}, S8("top_bar"));
        ui_push(parent, top_bar);
        {
            if (ui_button(S8("Button 123")).clicked_left)
            {
                string_print(S8("Button pressed\n"));
            }
            ui_button(S8("Button 2"));
            ui_button(S8("Button 3"));
        }
        BUSTER_UNUSED(ui_pop(parent));
        BUSTER_UNUSED(ui_pop(child_layout_axis));
    }
    BUSTER_UNUSED(ui_pop(pref_height));
}

typedef struct UI_Node UI_Node;
struct UI_Node
{
    String8 name;
    String8 type;
    String8 value;
    String8 name_space;
    String8 function;
};

BUSTER_GLOBAL_LOCAL void ui_node(UI_Node node)
{
    UI_BoxFlags flags = UI_BoxFlag_DrawBackground | UI_BoxFlag_DrawText;
    UI_Box* node_widget = ui_box_make_format(flags, S8("{S8} : {S8} = {S8}##{S8}{S8}"), node.name, node.type, node.value, node.function, node.name_space);
    BUSTER_UNUSED(node_widget);
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

    for (WmEvent* event = event_list.first; event; event = event->next)
    {
        switch (event->kind)
        {
            break;
        case WM_EVENT_WINDOW_CLOSE:
        {
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

        ui_top_bar();
        ui_push(child_layout_axis, AXIS2_X);
        UI_Box* workspace_widget = ui_box_make_format((UI_BoxFlags){0}, S8("workspace{u64}"), window->wm);
        ui_push(parent, workspace_widget);
        {
            // Node visualizer
            ui_push(child_layout_axis, AXIS2_Y);
            UI_Box* node_visualizer_widget = ui_box_make_format(UI_BoxFlag_DrawBackground, S8("node_visualizer{u64}"), window->wm);

            ui_push(parent, node_visualizer_widget);
            {
                ui_node((UI_Node){
                    .name = S8("a"),
                    .type = S8("s32"),
                    .value = S8("1"),
                    .name_space = S8("foo"),
                    .function = S8("main"),
                });
                ui_node((UI_Node){
                    .name = S8("b"),
                    .type = S8("s32"),
                    .value = S8("2"),
                    .name_space = S8("foo"),
                    .function = S8("main"),
                });
            }
            BUSTER_UNUSED(ui_pop(parent));
            BUSTER_UNUSED(ui_pop(child_layout_axis));

            // Side-panel stub
            ui_button(S8("Options"));
        }
        BUSTER_UNUSED(ui_pop(parent));
        BUSTER_UNUSED(ui_pop(child_layout_axis));

        ui_build_end();

        ui_draw();

        BUSTER_UNUSED(ui_pop(font_size));

        rendering_window_frame_end(ide_state.rendering, render_window);
        scratch_end(ui_events_scratch);

        window = next;
    }

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

    WmHandle* windowing = ide_state.windowing = wm_initialize();
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
        string_print(S8("Failed to initialize windowing\n"));
        result = PROCESS_RESULT_FAILED;
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
        // A unity translation unit retains preprocessing, binding,
        // typed IR, and object data through the driver call. This is
        // virtual address space; pages are committed on demand.
        .reserved_size = BUSTER_GB(4),
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
            S8("-max_len=4096"),
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
