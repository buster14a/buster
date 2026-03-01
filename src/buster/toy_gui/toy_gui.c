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

#if BUSTER_UNITY_BUILD
#include <buster/string_os.c>
#include <buster/arena.c>
#include <buster/integer.c>
#include <buster/os.c>
#include <buster/string8.c>
#include <buster/string.c>
#include <buster/assertion.c>
#include <buster/arguments.c>
#if BUSTER_INCLUDE_TESTS
#include <buster/test.c>
#endif
#include <buster/memory.c>
#include <buster/entry_point.c>
#include <buster/target.c>
#if defined(__x86_64__)
#include <buster/x86_64.c>
#endif
#include <buster/ui_core.c>
#include <buster/ui_builder.c>
#include <buster/window.c>
#include <buster/rendering.c>
#include <buster/file.c>
#include <buster/font_provider.c>
#include <buster/time.c>
#endif

STRUCT(ToyPanel)
{
    ToyPanel* first;
    ToyPanel* last;
    ToyPanel* previous;
    ToyPanel* next;
    ToyPanel* parent;
    f32 parent_percentage;
    Axis2 split_axis;
};

STRUCT(ToyWindow)
{
    OsWindowHandle* os;
    RenderingWindowHandle* render;
    ToyWindow* previous;
    ToyWindow* next;
    ToyPanel* root_panel;
    UI_State* ui;
};

STRUCT(ToyProgram)
{
    ProgramState state;
    ToyWindow* first_window;
    ToyWindow* last_window;
    OsWindowingHandle* windowing;
    RenderingHandle* rendering;
    OsWindowingEventList event_list;
    bool test;
    u8 reserved[7];
    TimeDataType last_frame_timestamp;
};

BUSTER_GLOBAL_LOCAL ToyProgram state = {};

BUSTER_IMPL ProgramState* program_state = &state.state;

#if BUSTER_FUZZING
BUSTER_IMPL s32 buster_fuzz(const u8* pointer, size_t size)
{
    BUSTER_UNUSED(pointer);
    BUSTER_UNUSED(size);
    return 0;
}
#else
BUSTER_IMPL ProcessResult process_arguments()
{
    ProcessResult result = PROCESS_RESULT_SUCCESS;

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
            if (r != PROCESS_RESULT_SUCCESS)
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
        ui_push(child_layout_axis, AXIS2_X);
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
    String8 namespace;
    String8 function;
};

BUSTER_GLOBAL_LOCAL void ui_node(UI_Node node)
{
    let node_widget = ui_widget_make_format((UI_WidgetFlags) {
        .draw_background = 1,
        .draw_text = 1,
    }, S8("{S8} : {S8} = {S8}##{S8}{S8}"), node.name, node.type, node.value, node.function, node.namespace);
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
            case OS_WINDOWING_EVENT_WINDOW_CLOSE:
            {
                for (ToyWindow* window = state.first_window; window; window = window->next)
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

                        rendering_window_deinitialize(state.rendering, window->render);

                        break;
                    }
                }
            } break;
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
        ui_push(child_layout_axis, AXIS2_X);
        let workspace_widget = ui_widget_make_format((UI_WidgetFlags) {}, S8("workspace{u64}"), window->os);
        ui_push(parent, workspace_widget);
        {
            // Node visualizer
            ui_push(child_layout_axis, AXIS2_Y);
            let node_visualizer_widget = ui_widget_make_format((UI_WidgetFlags) {
                .draw_background = 1,
            }, S8("node_visualizer{u64}"), window->os);

            ui_push(parent, node_visualizer_widget);
            {
                ui_node((UI_Node) {
                    .name = S8("a"),
                    .type = S8("s32"),
                    .value = S8("1"),
                    .namespace = S8("foo"),
                    .function = S8("main"),
                });
                ui_node((UI_Node) {
                    .name = S8("b"),
                    .type = S8("s32"),
                    .value = S8("2"),
                    .namespace = S8("foo"),
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

BUSTER_GLOBAL_LOCAL ProcessResult run_app()
{
    ProcessResult result = PROCESS_RESULT_SUCCESS;

    let windowing = state.windowing = os_windowing_initialize();
    if (windowing)
    {
        let arena = program_state->arena;
        let r = state.rendering = rendering_initialize(arena);
        if (r)
        {
            state.first_window = state.last_window = arena_allocate(arena, ToyWindow, 1);
            let os_window = os_window_create(windowing, (OsWindowCreate) {
                    .name = SOs("Toy"),
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
                    state.first_window->root_panel = arena_allocate(state.state.arena, ToyPanel, 1);
                    state.first_window->root_panel->parent_percentage = 1.0f;
                    state.first_window->root_panel->split_axis = AXIS2_X;

                    rendering_window_rect_texture_update_begin(state.first_window->render);

                    let font_path = font_file_get_path(arena, FONT_INDEX_MONO);
                    u32 monospace_font_height = 24;

                    let white_texture = white_texture_create(state.state.arena, state.rendering);
                    let font = rendering_font_create(state.state.arena, state.rendering, (FontTextureAtlasCreate) {
                            .font_path = font_path,
                            .text_height = monospace_font_height,
                            });

                    rendering_window_queue_rect_texture_update(state.rendering, state.first_window->render, RECT_TEXTURE_SLOT_WHITE, white_texture);
                    rendering_queue_font_update(state.rendering, state.first_window->render, RENDER_FONT_TYPE_MONOSPACE, font);

                    rendering_window_rect_texture_update_end(state.rendering, state.first_window->render);

                    state.last_frame_timestamp = timestamp_take();

                    bool test = state.test && !PROGRAM_FLAG_GET(PROGRAM_FLAG_TEST_PERSIST);
                    let loop_times = test ? (u64)3 : UINT64_MAX;
                    for (u64 i = 0; i < loop_times && state.first_window; i += 1)
                    {
                        app_update();
                    }

                    if (test)
                    {
                        for (let window = state.first_window; window; window = window->next)
                        {
                            rendering_window_deinitialize(state.rendering, window->render);
                        }
                    }

                    // TODO: OS deinitialization
                }
                else
                {
                    string8_print(S8("Failed to create render window\n"));
                    result = PROCESS_RESULT_FAILED;
                }
            }
            else
            {
                string8_print(S8("Failed to create window\n"));
                result = PROCESS_RESULT_FAILED;
            }

            rendering_deinitialize(r);
        }
        else
        {
            string8_print(S8("Failed to initialize rendering\n"));
            result = PROCESS_RESULT_FAILED;
        }

        os_windowing_deinitialize(windowing);
    }
    else
    {
        string8_print(S8("Failed to initialize windowing\n"));
        result = PROCESS_RESULT_FAILED;
    }

    return result;
}

BUSTER_IMPL ProcessResult thread_entry_point()
{
    return run_app();
}
#endif
