#pragma once

#include <buster/base.h>
#include <buster/entry_point.h>
#include <buster/ui_core.h>
#include <buster/rendering.h>
#include <buster/window.h>
#include <buster/font_provider.h>
#include <buster/time.h>
#include <buster/ui_builder.h>
#include <buster/arguments.h>
#include <buster/arena.h>

#if BUSTER_UNITY_BUILD
#include <buster/arena.cpp>
#include <buster/integer.cpp>
#include <buster/os.cpp>
#include <buster/string.cpp>
#include <buster/assertion.cpp>
#include <buster/arguments.cpp>
#if BUSTER_INCLUDE_TESTS
#include <buster/test.cpp>
#endif
#include <buster/memory.cpp>
#include <buster/entry_point.cpp>
#include <buster/target.cpp>
#if defined(__x86_64__)
#include <buster/x86_64.cpp>
#endif
#include <buster/ui_core.cpp>
#include <buster/ui_builder.cpp>
#include <buster/window.cpp>
#include <buster/rendering.cpp>
#include <buster/file.cpp>
#include <buster/font_provider.cpp>
#include <buster/time.cpp>
#include <buster/float.cpp>
#endif

STRUCT(IdePanel)
{
    IdePanel* first;
    IdePanel* last;
    IdePanel* previous;
    IdePanel* next;
    IdePanel* parent;
    f32 parent_percentage;
    Axis2 split_axis;
};

STRUCT(IdeWindow)
{
    OsWindowHandle* os;
    RenderingWindowHandle* render;
    IdeWindow* previous;
    IdeWindow* next;
    IdePanel* root_panel;
    UI_State* ui;
};

STRUCT(IdeProgram)
{
    ProgramState state;
    IdeWindow* first_window;
    IdeWindow* last_window;
    OsWindowingHandle* windowing;
    RenderingHandle* rendering;
    OsWindowingEventList event_list;
    bool test;
    u8 reserved[7];
    TimeDataType last_frame_timestamp;
};

BUSTER_GLOBAL_LOCAL IdeProgram state = {};

BUSTER_F_IMPL ProgramState* program_state = &state.state;

#if BUSTER_FUZZING
BUSTER_F_IMPL s32 buster_fuzz(const u8* pointer, size_t size)
{
    BUSTER_UNUSED(pointer);
    BUSTER_UNUSED(size);
    return 0;
}
#else
BUSTER_F_IMPL ProcessResult process_arguments()
{
    ProcessResult result = ProcessResult::Success;

    let argv = program_state->input.argv;
    let envp = program_state->input.envp;

    let arg_it = string_os_list_iterator_initialize(argv);

    string_os_list_iterator_next(&arg_it);

    u64 i = 1;

    for (let arg = string_os_list_iterator_next(&arg_it); arg.pointer; arg = string_os_list_iterator_next(&arg_it), i += 1)
    {
        if (!string_os_equal(arg, SOs("test")))
        {
            let r = buster_argument_process(argv, envp, i, arg);
            if (r != ProcessResult::Success)
            {
                string8_print(S8("Failed to process argument {SOs}\n"), arg);
                result = r;
                break;
            }
        }
        else
        {
            state.test = true;
        }
    }

    return result;
}

BUSTER_GLOBAL_LOCAL void ui_top_bar()
{
    ui_push(pref_height, ui_em(1, 1));
    {
        ui_push(child_layout_axis, Axis2::X);
        let top_bar = ui_widget_make((UI_WidgetFlags) {
                }, S8("top_bar"));
        ui_push(parent, top_bar);
        {
            if (ui_button(S8("Button s")).clicked_left)
            {
                string8_print(S8("Button pressed\n"));
            }
            ui_button(S8("Button 2"));
            ui_button(S8("Button 3"));
        }
        ui_pop(parent);
        ui_pop(child_layout_axis);
    }
    ui_pop(pref_height);
}

STRUCT(UI_Node)
{
    String8 name;
    String8 type;
    String8 value;
    String8 name_space;
    String8 function;
};

BUSTER_GLOBAL_LOCAL void ui_node(UI_Node node)
{
    let node_widget = ui_widget_make_format((UI_WidgetFlags) {
        .draw_background = 1,
        .draw_text = 1,
    }, S8("{S8} : {S8} = {S8}##{S8}{S8}"), node.name, node.type, node.value, node.function, node.name_space);
    BUSTER_UNUSED(node_widget);
}

BUSTER_GLOBAL_LOCAL void app_update()
{
    let frame_end = timestamp_take();
    state.event_list = os_windowing_poll_events(state.state.arena, state.windowing);
    let frame_ms = (f64)timestamp_ns_between(state.last_frame_timestamp, frame_end) / (1000 * 1000);
    state.last_frame_timestamp = frame_end;

    for (OsWindowingEvent* os_event = state.event_list.first; os_event; os_event = os_event->next)
    {
        switch (os_event->kind)
        {
            case OsWindowingEventKind::OS_WINDOWING_EVENT_WINDOW_CLOSE:
            {
                for (IdeWindow* window = state.first_window; window; window = window->next)
                {
                    if (window->os == os_event->window)
                    {
                        if (window->previous)
                        {
                            window->previous->next = window->next;
                        }

                        if (window->next)
                        {
                            window->next->previous = window->previous;
                        }

                        if (state.first_window == window)
                        {
                            state.first_window = window->next;
                        }

                        if (state.last_window == window)
                        {
                            state.last_window = window->previous;
                        }

                        ui_state_deinitialize(window->ui);
                        window->ui = 0;
                        rendering_window_deinitialize(state.rendering, window->render);
                        window->render = 0;

                        break;
                    }
                }
            } break;
            break; case OsWindowingEventKind::Count: BUSTER_UNREACHABLE();
        }
    }

    let window = state.first_window;
    while (window)
    {
        let next = window->next;

        let render_window = window->render;
        rendering_window_frame_begin(state.rendering, render_window);

        ui_state_select(window->ui);

        ui_build_begin(state.windowing, window->os, frame_ms, &state.event_list);

        ui_push(font_size, 24);

        ui_top_bar();
        ui_push(child_layout_axis, Axis2::X);
        let workspace_widget = ui_widget_make_format((UI_WidgetFlags) {}, S8("workspace{u64}"), window->os);
        ui_push(parent, workspace_widget);
        {
            // Node visualizer
            ui_push(child_layout_axis, Axis2::Y);
            let node_visualizer_widget = ui_widget_make_format((UI_WidgetFlags) {
                .draw_background = 1,
            }, S8("node_visualizer{u64}"), window->os);

            ui_push(parent, node_visualizer_widget);
            {
                ui_node((UI_Node) {
                    .name = S8("a"),
                    .type = S8("s32"),
                    .value = S8("1"),
                    .name_space = S8("foo"),
                    .function = S8("main"),
                });
                ui_node((UI_Node) {
                    .name = S8("b"),
                    .type = S8("s32"),
                    .value = S8("2"),
                    .name_space = S8("foo"),
                    .function = S8("main"),
                });
            }
            ui_pop(parent);
            ui_pop(child_layout_axis);

            // Side-panel stub
            ui_button(S8("Options"));
        }
        ui_pop(parent);
        ui_pop(child_layout_axis);

        ui_build_end();

        ui_draw();

        ui_pop(font_size);

        rendering_window_frame_end(state.rendering, render_window);

        window = next;
    }
}

BUSTER_GLOBAL_LOCAL void window_refresh_callback(OsWindowHandle* window, void* context)
{
    BUSTER_UNUSED(window);
    BUSTER_UNUSED(context);
    app_update();
}

BUSTER_F_IMPL void async_user_tick()
{
}

BUSTER_GLOBAL_LOCAL ProcessResult run_app()
{
    ProcessResult result = ProcessResult::Success;

#if BUSTER_INCLUDE_TESTS
    if (state.test)
    {
        let arena = arena_create((ArenaCreation){});
        UnitTestArguments arguments = { arena, &default_show };
        let batch_test_result = library_tests(&arguments);
        result = batch_test_report(&arguments, batch_test_result) ? ProcessResult::Success : ProcessResult::Failed;
        arena_destroy(arena, 1);
    }
#endif

    if (result == ProcessResult::Success)
    {
        let windowing = state.windowing = os_windowing_initialize();
        if (windowing)
        {
            let arena = program_state->arena;
            let r = state.rendering = rendering_initialize(arena);
            if (r)
            {
                state.first_window = state.last_window = arena_allocate(arena, IdeWindow, 1);
                let os_window = os_window_create(windowing, (OsWindowCreate) {
                        .name = SOs("Ide"),
                        .size = {
                        .width = 1600,
                        .height= 900,
                        },
                        .refresh_callback = &window_refresh_callback,
                        });
                state.first_window->os = os_window;

                if (os_window)
                {
                    let render_window = state.first_window->render = rendering_window_initialize(arena, windowing, r, os_window);

                    if (render_window)
                    {
                        state.first_window->ui = ui_state_allocate(r, render_window);
                        state.first_window->root_panel = arena_allocate(state.state.arena, IdePanel, 1);
                        state.first_window->root_panel->parent_percentage = 1.0f;
                        state.first_window->root_panel->split_axis = Axis2::X;

                        rendering_window_rect_texture_update_begin(state.first_window->render);

                        let font_path = font_file_get_path(arena, FontIndex::FONT_INDEX_MONO);
                        u32 monospace_font_height = 24;

                        let white_texture = white_texture_create(state.state.arena, state.rendering);
                        let font = rendering_font_create(state.state.arena, state.rendering, (FontTextureAtlasCreate) {
                                .font_path = font_path,
                                .text_height = monospace_font_height,
                                });

                        rendering_window_queue_rect_texture_update(state.rendering, state.first_window->render, RectTextureSlot::RECT_TEXTURE_SLOT_WHITE, white_texture);
                        rendering_queue_font_update(state.rendering, state.first_window->render, RenderFontType::RENDER_FONT_TYPE_MONOSPACE, font);

                        rendering_window_rect_texture_update_end(state.rendering, state.first_window->render);

                        state.last_frame_timestamp = timestamp_take();

                        bool test = state.test && !flag_get(program_state->input.flags, ProgramFlag::Test_Persist);
                        let loop_times = test ? (u64)3 : UINT64_MAX;
                        for (u64 i = 0; i < loop_times && state.first_window; i += 1)
                        {
                            app_update();
                        }

                        if (test)
                        {
                            for (let window = state.first_window; window; window = window->next)
                            {
                                ui_state_deinitialize(window->ui);
                                window->ui = 0;
                                rendering_window_deinitialize(state.rendering, window->render);
                                window->render = 0;
                            }
                        }

                        // TODO: OS deinitialization
                    }
                    else
                    {
                        string8_print(S8("Failed to create render window\n"));
                        result = ProcessResult::Failed;
                    }
                }
                else
                {
                    string8_print(S8("Failed to create window\n"));
                    result = ProcessResult::Failed;
                }

                rendering_deinitialize(r);
            }
            else
            {
                string8_print(S8("Failed to initialize rendering\n"));
                result = ProcessResult::Failed;
            }

            os_windowing_deinitialize(windowing);
        }
        else
        {
            string8_print(S8("Failed to initialize windowing\n"));
            result = ProcessResult::Failed;
        }
    }

    return result;
}

BUSTER_F_IMPL ProcessResult entry_point()
{
    return run_app();
}
#endif
