// This UI is heavily inspired by the ideas of Casey Muratori and Ryan Fleury on GUI programming.
// https://www.youtube.com/watch?v=Z1qyvQsjK5Y
// https://www.rfleury.com/p/ui-part-1-the-interaction-medium

#include <buster/ui_core.h>
#include <buster/string.h>
#include <buster/float.h>

BUSTER_V_IMPL UI_State* ui_state;

void ui_autopop(UI_State* state)
{
    if (state->stack_autopops.parent && state->stacks.parent_length > 0)
    {
        state->stacks.parent_length -= 1;
        state->stack_autopops.parent = 0;
    }
    if (state->stack_autopops.pref_width && state->stacks.pref_width_length > 0)
    {
        state->stacks.pref_width_length -= 1;
        state->stack_autopops.pref_width = 0;
    }
    if (state->stack_autopops.pref_height && state->stacks.pref_height_length > 0)
    {
        state->stacks.pref_height_length -= 1;
        state->stack_autopops.pref_height = 0;
    }
    if (state->stack_autopops.child_layout_axis && state->stacks.child_layout_axis_length > 0)
    {
        state->stacks.child_layout_axis_length -= 1;
        state->stack_autopops.child_layout_axis = 0;
    }
    if (state->stack_autopops.text_color && state->stacks.text_color_length > 0)
    {
        state->stacks.text_color_length -= 1;
        state->stack_autopops.text_color = 0;
    }
    if (state->stack_autopops.background_color && state->stacks.background_color_length > 0)
    {
        state->stacks.background_color_length -= 1;
        state->stack_autopops.background_color = 0;
    }
    if (state->stack_autopops.font_size && state->stacks.font_size_length > 0)
    {
        state->stacks.font_size_length -= 1;
        state->stack_autopops.font_size = 0;
    }
}

void ui_state_select(UI_State* state)
{
    ui_state = state;
}

UI_State* ui_state_get(void)
{
    return ui_state;
}

Arena* ui_build_arena(void)
{
    Arena* arena = ui_state->build_arenas[ui_state->build_count % BUSTER_ARRAY_LENGTH(ui_state->build_arenas)];
    return arena;
}

UI_Key ui_key_null(void)
{
    UI_Key key = {0};
    return key;
}

UI_State* ui_state_allocate(RenderingHandle* rendering, RenderingWindowHandle* window)
{
    Arena* arena = arena_create((ArenaCreation){
        .reserved_size = BUSTER_GB(8),
        .granularity = BUSTER_MB(2),
        .initial_size = BUSTER_MB(2),
    });
    UI_State* state = arena_allocate(arena, UI_State, 1);
    state->rendering = rendering;
    state->rendering_window = window;
    state->arena = arena;

    u64 widget_table_length = 4096;
    state->widget_table.length = widget_table_length;
    state->widget_table.pointer = arena_allocate(arena, UI_WidgetSlot, widget_table_length);

    for (u64 i = 0; i < BUSTER_ARRAY_LENGTH(state->build_arenas); i += 1)
    {
        state->build_arenas[i] = arena_create((ArenaCreation){
            .reserved_size = BUSTER_GB(8),
            .granularity = BUSTER_MB(2),
            .initial_size = BUSTER_MB(2),
        });
    }

    state->stack_nulls = (UI_StateStackNulls){
        .parent = 0,
        .child_layout_axis = AXIS2_COUNT,
        .pref_width = {0},
        .pref_height = {0},
    };

    return state;
}

void ui_state_deinitialize(UI_State* state)
{
    if (state)
    {
        for (u64 i = 0; i < BUSTER_ARRAY_LENGTH(state->build_arenas); i += 1)
        {
            if (state->build_arenas[i])
            {
                arena_destroy(state->build_arenas[i], 1);
                state->build_arenas[i] = 0;
            }
        }

        if (ui_state == state)
        {
            ui_state = 0;
        }

        Arena* arena = state->arena;
        if (arena)
        {
            arena_destroy(arena, 1);
        }
    }
}

u64 ui_widget_index_from_key(UI_Key key)
{
    u64 length = ui_state->widget_table.length;
    BUSTER_CHECK(BUSTER_IS_POWER_OF_TWO(length));
    return key.value & (length - 1);
}

String8 ui_text_from_key_string(String8 string)
{
    String8 result = string;
    String8 text_end_delimiter = S8("##");
    u64 index = string_first_sequence(string, text_end_delimiter);
    if (index < string.length)
    {
        result.length = index;
    }
    return result;
}

String8 ui_hash_from_key_string(String8 string)
{
    String8 result = string;
    String8 hash_start_delimiter = S8("###");
    u64 index = string_first_sequence(string, hash_start_delimiter);
    if (index < string.length)
    {
        result = string_slice(string, index, string.length);
    }
    return result;
}

UI_Key ui_key_from_string(UI_Key seed, String8 string)
{
    UI_Key key = ui_key_null();

    if (string.length)
    {
        key = seed;

        for (u64 i = 0; i < string.length; i += 1)
        {
            key.value = ((key.value << 5) + key.value) + string.pointer[i];
        }
    }

    return key;
}

BUSTER_GLOBAL_LOCAL UI_Key ui_key_from_string_format(UI_Key seed, String8 format, ...)
{
    TemporalArena scratch = scratch_begin(0, 0);
    va_list args;
    va_start(args, format);
    String8 string = string_format_va(scratch.arena, format, args);
    va_end(args);
    UI_Key result = ui_key_from_string(seed, string);
    return result;
}

u8 ui_key_equal(UI_Key a, UI_Key b)
{
    return a.value == b.value;
}

UI_Widget* ui_widget_from_key(UI_Key key)
{
    UI_Widget* result = 0;

    if (!ui_key_equal(key, ui_key_null()))
    {
        u64 index = ui_widget_index_from_key(key);
        for (UI_Widget* widget = ui_state->widget_table.pointer[index].first; widget; widget = widget->hash_next)
        {
            if (ui_key_equal(widget->key, key))
            {
                result = widget;
                break;
            }
        }
    }

    return result;
}

UI_Widget* ui_widget_make_from_key(UI_WidgetFlags flags, UI_Key key)
{
    UI_Widget* widget = ui_widget_from_key(key);

    if (widget)
    {
        if (widget->last_build_touched == ui_state->build_count)
        {
            key = ui_key_null();
            widget = 0;
        }
    }

    u8 first_frame = 0;
    if (!widget)
    {
        u64 index = ui_widget_index_from_key(key);
        first_frame = 1;
        BUSTER_UNUSED(first_frame);

        widget = arena_allocate(ui_state->arena, UI_Widget, 1);

        UI_WidgetSlot* table_widget_slot = &ui_state->widget_table.pointer[index];
        if (!table_widget_slot->last)
        {
            table_widget_slot->first = widget;
            table_widget_slot->last = widget;
        }
        else
        {
            table_widget_slot->last->hash_next = widget;
            widget->hash_previous = table_widget_slot->last;
            table_widget_slot->last = widget;
        }
    }

    UI_Widget* parent = ui_top(parent);

    if (parent)
    {
        if (!parent->last)
        {
            parent->last = widget;
            parent->first = widget;
        }
        else
        {
            UI_Widget* previous_last = parent->last;
            previous_last->next = widget;
            widget->previous = previous_last;
            parent->last = widget;
        }

        parent->child_count += 1;
        widget->parent = parent;
    }
    else
    {
        ui_state->root = widget;
    }

    widget->key = key;

    for (u64 i = 0; i < BUSTER_ARRAY_LENGTH(widget->background_colors); i += 1)
    {
        widget->background_colors[i] = ui_top(background_color);
    }
    widget->text_color = ui_top(text_color);
    widget->flags = flags;
    widget->first = 0;
    widget->last = 0;
    widget->last_build_touched = ui_state->build_count;
    widget->pref_size[AXIS2_X] = ui_top(pref_width);
    widget->pref_size[AXIS2_Y] = ui_top(pref_height);
    widget->child_layout_axis = ui_top(child_layout_axis);

    ui_autopop(ui_state);

    return widget;
}

UI_Widget* ui_widget_make(UI_WidgetFlags flags, String8 string)
{
    UI_Key seed = ui_key_null();
    String8 hash_string = ui_hash_from_key_string(string);
    UI_Key key = ui_key_from_string(seed, hash_string);
    UI_Widget* widget = ui_widget_make_from_key(flags, key);

    if (flags.draw_text)
    {
        widget->text = ui_text_from_key_string(string);
    }

    return widget;
}

UI_Widget* ui_widget_make_format(UI_WidgetFlags flags, String8 format, ...)
{
    Arena* arena = ui_build_arena();
    va_list args;
    va_start(args, format);
    String8 string = string_format_va(arena, format, args);
    va_end(args);

    UI_Widget* result = ui_widget_make(flags, string);
    return result;
}

UI_Signal ui_signal_from_widget(UI_Widget* widget)
{
    F32Interval2 rect = widget->rect;
    UI_MousePosition mouse_position = ui_state->mouse_position;
    UI_Signal signal = {
        .clicked_left =
            (u32)((widget->flags.mouse_clickable & (ui_state->mouse_button_events[(u64)WINDOWING_EVENT_MOUSE_LEFT].action == WINDOWING_EVENT_MOUSE_RELEASE)) != 0) &
            (u32)(((mouse_position.x >= (f64)rect.x0) & (mouse_position.x <= (f64)rect.x1)) != 0) &
            (u32)(((mouse_position.y >= (f64)rect.y0) & (mouse_position.y <= (f64)rect.y1)) != 0),
    };
    return signal;
}

void ui_stack_reset(UI_State* state)
{
    state->stacks.parent_length = 0;
    state->stacks.pref_width_length = 0;
    state->stacks.pref_height_length = 0;
    state->stacks.child_layout_axis_length = 0;
    state->stacks.text_color_length = 0;
    state->stacks.background_color_length = 0;
    state->stacks.font_size_length = 0;
}

UI_Size ui_pixels(u32 width, f32 strictness)
{
    return (UI_Size) {
        .kind = UI_SIZE_PIXEL_COUNT,
        .strictness = strictness,
        .value = (f32)width,
    };
}

UI_Size ui_percentage(f32 percentage, f32 strictness)
{
    return (UI_Size) {
        .kind = UI_SIZE_PERCENTAGE,
        .strictness = strictness,
        .value = percentage,
    };
}

UI_Size ui_em(f32 value, f32 strictness)
{
    f32 font_size = ui_top(font_size);
    BUSTER_CHECK(font_size != 0);
    return (UI_Size) {
        .kind = UI_SIZE_PIXEL_COUNT,
        .strictness = strictness,
        .value = value * font_size,
    };
}

u8 ui_build_begin(OsWindowingHandle* windowing, OsWindowHandle* window, f64 frame_time, OsWindowingEventList* event_queue)
{
    ui_state->build_count += 1;
    Arena* build_arena = ui_build_arena();
    arena_reset_to_start(build_arena);
    ui_state->frame_time = frame_time;
    ui_state->window = window;

    ui_stack_reset(ui_state);

    u8 open = 1;
    u32 event_index = 0;
    u64 event_count = event_queue->count;

    for (OsWindowingEvent* event = event_queue->first; open & (event_index < event_count); event_count += 1, event = event->next)
    {
        // switch (event->type)
        // {
        // case WINDOWING_EVENT_TYPE_MOUSE_BUTTON:
        //     {
        //         let button = event_queue->mouse_buttons[event_index];
        //         let previous_button_event = ui_state->mouse_button_events[button.button];
        //         switch (button.event.action)
        //         {
        //             case WINDOWING_EVENT_MOUSE_RELAX:
        //                 BUSTER_UNREACHABLE();
        //                 break;
        //             case WINDOWING_EVENT_MOUSE_RELEASE:
        //                 BUSTER_CHECK(previous_button_event.action == WINDOWING_EVENT_MOUSE_PRESS);
        //                 break;
        //             case WINDOWING_EVENT_MOUSE_PRESS:
        //                 BUSTER_CHECK(previous_button_event.action == WINDOWING_EVENT_MOUSE_RELAX || mouse_button_count);
        //                 break;
        //             case WINDOWING_EVENT_MOUSE_REPEAT:
        //                 BUSTER_UNREACHABLE();
        //                 break;
        //         }
        //
        //         ui_state->mouse_button_events[button.button] = button.event;
        //         mouse_button_count += 1;
        //     } break;
        // case WINDOWING_EVENT_TYPE_WINDOW_FOCUS:
        //     break;
        // case WINDOWING_EVENT_TYPE_CURSOR_POSITION:
        //     {
        //         let mouse_position = event_queue->cursor_positions[event_index];
        //         ui_state->mouse_position = (UI_MousePosition) {
        //             .x = mouse_position.x,
        //             .y = mouse_position.y,
        //         };
        //     } break;
        // case WINDOWING_EVENT_TYPE_CURSOR_ENTER:
        //     // TODO
        //     break;
        // case WINDOWING_EVENT_TYPE_WINDOW_POSITION:
        //     break;
        // case WINDOWING_EVENT_TYPE_WINDOW_CLOSE:
        //     open = 0;
        //     break;
        // }
    }

    if (open)
    {
        for (u64 i = 0; i < ui_state->widget_table.length; i += 1)
        {
            UI_WidgetSlot* widget_table_element = &ui_state->widget_table.pointer[i];
            for (UI_Widget* widget = widget_table_element->first, *next = 0; widget; widget = next)
            {
                next = widget->hash_next;

                if (ui_key_equal(widget->key, ui_key_null()) || widget->last_build_touched + 1 < ui_state->build_count)
                {
                    if (widget->hash_previous)
                    {
                        widget->hash_previous->hash_next = widget->hash_next;
                    }

                    if (widget->hash_next)
                    {
                        widget->hash_next->hash_previous = widget->hash_previous;
                    }

                    if (widget_table_element->first == widget)
                    {
                        widget_table_element->first = widget->hash_next;
                    }

                    if (widget_table_element->last == widget)
                    {
                        widget_table_element->last = widget->hash_previous;
                    }
                }
            }
        }

        OsWindowSize framebuffer_size = os_window_get_framebuffer_size(windowing, window);
        ui_push_next_only(pref_width, ui_pixels(framebuffer_size.width, 1.0f));
        ui_push_next_only(pref_height, ui_pixels(framebuffer_size.height, 1.0f));
        ui_push_next_only(child_layout_axis, AXIS2_Y);

        UI_Widget* root = ui_widget_make_format((UI_WidgetFlags) {0}, S8("window_root_{u64:x}"), (u64)window);
        BUSTER_CHECK(!ui_state->stack_autopops.child_layout_axis);

        ui_push(parent, root);
        ui_push(font_size, 12);
        ui_push(text_color, float4_make(1.0f, 1.0f, 1.0f, 1.0f));
        ui_push(background_color, float4_make(0.1f, 0.1f, 0.1f, 1.0f));
        ui_push(pref_width, ui_percentage(1.0f, 0.0f));
        ui_push(pref_height, ui_percentage(1.0f, 0.0f));
    }

    return open;
}

void ui_compute_independent_sizes(UI_Widget* widget)
{
    for (Axis2 axis = 0; axis < AXIS2_COUNT; axis += 1) 
    {
        UI_Size pref_size = widget->pref_size[axis];
        switch (pref_size.kind)
        {
            default: break;
            case UI_SIZE_KIND_COUNT: BUSTER_UNREACHABLE(); break;
            case UI_SIZE_PIXEL_COUNT: float2_element(widget->computed_size, axis) = floor_f32(widget->pref_size[axis].value); break;
        }
    }

    for (UI_Widget* child_widget = widget->first; child_widget; child_widget = child_widget->next)
    {
        ui_compute_independent_sizes(child_widget);
    }
}

void ui_compute_upward_dependent_sizes(UI_Widget* widget)
{
    for (Axis2 axis = 0; axis < AXIS2_COUNT; axis += 1)
    {
        UI_Size pref_size = widget->pref_size[axis];
        switch (pref_size.kind)
        {
            default: break;
            case UI_SIZE_KIND_COUNT: BUSTER_UNREACHABLE(); break;
            case UI_SIZE_PERCENTAGE:
            {
                for (UI_Widget* ancestor = widget->parent; ancestor; ancestor = ancestor->parent)
                {
                    if (ancestor->pref_size[axis].kind != UI_SIZE_BY_CHILDREN)
                    {
                        float2_element(widget->computed_size, axis) = floor_f32(float2_element(ancestor->computed_size, axis) * widget->pref_size[axis].value);
                        break;
                    }
                }
            } break;
        }
    }

    for (UI_Widget* child_widget = widget->first; child_widget; child_widget = child_widget->next)
    {
        ui_compute_upward_dependent_sizes(child_widget);
    }
}

void ui_compute_downward_dependent_sizes(UI_Widget* widget)
{
    for (UI_Widget* child_widget = widget->first; child_widget; child_widget = child_widget->next)
    {
        ui_compute_downward_dependent_sizes(child_widget);
    }

    for (Axis2 axis = 0; axis < AXIS2_COUNT; axis += 1)
    {
        UI_Size pref_size = widget->pref_size[axis];
        switch (pref_size.kind)
        {
            default: break;
            case UI_SIZE_KIND_COUNT: BUSTER_UNREACHABLE(); break;
            case UI_SIZE_BY_CHILDREN:
            {
                // TODO: implement
                BUSTER_TRAP();
            } break;
        }
    }
}

void ui_resolve_conflicts(UI_Widget* widget)
{
    for (Axis2 axis = 0; axis < AXIS2_COUNT; axis += 1)
    {
        f32 available_space = float2_element(widget->computed_size, axis);
        f32 taken_space = 0;
        f32 total_fixup_budget = 0;

        if (!(widget->flags.v & ((u64)UI_WIDGET_FLAG_OVERFLOW_X << axis)))
        {
            for (UI_Widget* child_widget = widget->first; child_widget; child_widget = child_widget->next)
            {
                if (!(child_widget->flags.v & ((u64)UI_WIDGET_FLAG_FLOATING_X << axis)))
                {
                    if (axis == (u64)widget->child_layout_axis)
                    {
                        taken_space += float2_element(child_widget->computed_size, axis);
                    }
                    else
                    {
                        taken_space = BUSTER_MAX(taken_space, float2_element(child_widget->computed_size, axis));
                    }
                    f32 fixup_budget_this_child = float2_element(child_widget->computed_size, axis) * (1 - child_widget->pref_size[axis].strictness);
                    total_fixup_budget += fixup_budget_this_child;
                }
            }

            f32 conflict = taken_space - available_space;

            if (conflict > 0 && total_fixup_budget > 0)
            {
                for (UI_Widget* child_widget = widget->first; child_widget; child_widget = child_widget->next)
                {
                    if (!(child_widget->flags.v & ((u64)UI_WIDGET_FLAG_FLOATING_X << axis)))
                    {
                        f32 fixup_budget_this_child = float2_element(child_widget->computed_size, axis) * (1 - child_widget->pref_size[axis].strictness);
                        f32 fixup_size_this_child = 0;

                        if (axis == (u64)widget->child_layout_axis)
                        {
                            fixup_size_this_child = fixup_budget_this_child * (conflict / total_fixup_budget);
                        }
                        else
                        {
                            fixup_size_this_child = float2_element(child_widget->computed_size, axis) - available_space;
                        }

                        fixup_size_this_child = BUSTER_CLAMP(0, fixup_size_this_child, fixup_budget_this_child);
                        float2_element(child_widget->computed_size, axis) = floor_f32(float2_element(child_widget->computed_size, axis) - fixup_size_this_child);
                    }
                }
            }
        }

        if (axis == (u64)widget->child_layout_axis)
        {
            f32 p = 0;

            for (UI_Widget* child_widget = widget->first; child_widget; child_widget = child_widget->next)
            {
                if (!(child_widget->flags.v & ((u64)UI_WIDGET_FLAG_FLOATING_X << axis)))
                {
                    float2_element(child_widget->computed_relative_position, axis) = p;
                    p += float2_element(child_widget->computed_size, axis);
                }
            }
        }
        else
        {
            for (UI_Widget* child_widget = widget->first; child_widget; child_widget = child_widget->next)
            {
                if (!(child_widget->flags.v & ((u64)UI_WIDGET_FLAG_FLOATING_X << axis)))
                {
                    float2_element(child_widget->computed_relative_position, axis) = 0;
                }
            }
        }

        for (UI_Widget* child_widget = widget->first; child_widget; child_widget = child_widget->next)
        {
            F32Interval2 last_relative_rect = child_widget->relative_rect;
            float2_element(child_widget->relative_rect.p0, axis) = float2_element(child_widget->computed_relative_position, axis);
            float2_element(child_widget->relative_rect.p1, axis) = float2_element(child_widget->relative_rect.p0, axis) + float2_element(child_widget->computed_size, axis);

            float2 last_corner_01 = float2_make(last_relative_rect.x0, last_relative_rect.y1);
            float2 last_corner_10 = float2_make(last_relative_rect.x1, last_relative_rect.y0);
            float2 this_corner_01 = float2_make(child_widget->relative_rect.x0, child_widget->relative_rect.y1);
            float2 this_corner_10 = float2_make(child_widget->relative_rect.x1, child_widget->relative_rect.y0);

            float2_element(child_widget->relative_corner_delta[CORNER_00], axis) = float2_element(child_widget->relative_rect.p0, axis) - float2_element(last_relative_rect.p0, axis);
            float2_element(child_widget->relative_corner_delta[CORNER_01], axis) = float2_element(this_corner_01, axis) - float2_element(last_corner_01, axis);
            float2_element(child_widget->relative_corner_delta[CORNER_10], axis) = float2_element(this_corner_10, axis) - float2_element(last_corner_10, axis);
            float2_element(child_widget->relative_corner_delta[CORNER_11], axis) = float2_element(child_widget->relative_rect.p1, axis) - float2_element(last_relative_rect.p1, axis);

            float2_element(child_widget->rect.p0, axis) = float2_element(widget->rect.p0, axis) + float2_element(child_widget->relative_rect.p0, axis) - float2_element(widget->view_offset, axis);
            float2_element(child_widget->rect.p1, axis) = float2_element(child_widget->rect.p0, axis) + float2_element(child_widget->computed_size, axis);

            if (!(child_widget->flags.v & ((u64)UI_WIDGET_FLAG_FLOATING_X << axis)))
            {
                float2_element(child_widget->rect.p0, axis) = floor_f32(float2_element(child_widget->rect.p0, axis));
                float2_element(child_widget->rect.p1, axis) = floor_f32(float2_element(child_widget->rect.p1, axis));
            }
        }

        for (UI_Widget* child_widget = widget->first; child_widget; child_widget = child_widget->next)
        {
            ui_resolve_conflicts(child_widget);
        }
    }
}

void ui_build_end(void)
{
    // Clear release button presses
    for (u32 i = 0; i < BUSTER_ARRAY_LENGTH(ui_state->mouse_button_events); i += 1)
    {
        WindowingEventMouseButtonEvent* event = &ui_state->mouse_button_events[i];
        if (event->action == WINDOWING_EVENT_MOUSE_RELEASE)
        {
            event->action = WINDOWING_EVENT_MOUSE_RELAX;
        }
    }

    BUSTER_UNUSED(ui_pop(parent));

    ui_compute_independent_sizes(ui_state->root);
    ui_compute_upward_dependent_sizes(ui_state->root);
    ui_compute_downward_dependent_sizes(ui_state->root);
    ui_resolve_conflicts(ui_state->root);
}

typedef struct WidgetIterator WidgetIterator;
struct WidgetIterator
{
    UI_Widget* next;
    u32 push_count;
    u32 pop_count;
};

#define ui_widget_recurse_depth_first_preorder(widget) ui_widget_recurse_depth_first((widget), BUSTER_OFFSET_OF(UI_Widget, next), BUSTER_OFFSET_OF(UI_Widget, first))
#define ui_widget_recurse_depth_first_postorder(widget) ui_widget_recurse_depth_first((widget), BUSTER_OFFSET_OF(UI_Widget, previous), BUSTER_OFFSET_OF(UI_Widget, last))

WidgetIterator ui_widget_recurse_depth_first(UI_Widget* widget, u64 sibling_offset, u64 child_offset)
{
    WidgetIterator it = {0};
    UI_Widget* child = *(UI_Widget**)((u8*)widget + child_offset);
    if (child)
    {
        it.next = child;
        it.push_count += 1;
    }
    else
    {
        for (UI_Widget* w = widget; w; w = w->parent)
        {
            UI_Widget* sibling = *(UI_Widget**)((u8*)w + sibling_offset);
            if (sibling)
            {
                it.next = sibling;
                break;
            }

            it.pop_count += 1;
        }
    }

    return it;
}

void ui_draw(void)
{
    UI_Widget* root = ui_state->root;

    UI_Widget* widget = root;
    RenderingWindowHandle* window = ui_state->rendering_window;
    RenderingHandle* rendering = ui_state->rendering;

    while (widget)
    {
        if (widget->flags.draw_background)
        {
            rendering_window_render_rect(window, (RectDraw) {
                .colors = { widget->background_colors[0], widget->background_colors[1], widget->background_colors[2], widget->background_colors[3] },
                .vertex = widget->rect,
            });
        }

        if (widget->flags.draw_text)
        {
            rendering_window_render_text(rendering, window, widget->text, widget->text_color, RENDER_FONT_TYPE_MONOSPACE, widget->rect.x0, widget->rect.y0);
        }

        widget = ui_widget_recurse_depth_first_postorder(widget).next;
    }
}
