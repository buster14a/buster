#include <buster/lib/ui_builder.h>
#include <buster/lib/string.h>

BUSTER_GLOBAL_LOCAL String8 ui_widget_display_part(String8 label)
{
    u64 index = string_first_sequence(label, S8("##"));
    if (index < label.length)
    {
        label.length = index;
    }
    return label;
}

BUSTER_GLOBAL_LOCAL String8 ui_widget_identity_part(String8 label)
{
    u64 triple_index = string_first_sequence(label, S8("###"));
    if (triple_index < label.length)
    {
        return string_slice(label, triple_index + 3, label.length);
    }
    u64 double_index = string_first_sequence(label, S8("##"));
    if (double_index < label.length)
    {
        return string_slice(label, double_index + 2, label.length);
    }
    return label;
}

BUSTER_GLOBAL_LOCAL String8 ui_widget_id(Arena* arena, String8 role, String8 label)
{
    UI_Box* parent = ui_top_parent();
    u64 parent_key = parent ? parent->key.value : 0;
    String8 display = ui_widget_display_part(label);
    String8 identity = ui_widget_identity_part(label);
    // A repeated visible label in one parent/role has no stable identity. Its
    // caller must provide a ##id or ###id suffix; the UI core deliberately
    // resolves the resulting duplicate without falling back to build order.
    return string_format(arena, S8("{S8}###{S8}:{u64:x}:{S8}"), display, role, parent_key, identity);
}

BUSTER_GLOBAL_LOCAL UI_BoxFlags ui_interactive_widget_flags(void)
{
    return UI_BoxFlag_MouseClickable | UI_BoxFlag_KeyboardClickable | UI_BoxFlag_ClickToFocus | UI_BoxFlag_FocusHot |
           UI_BoxFlag_FocusActive | UI_BoxFlag_DefaultFocusNavX | UI_BoxFlag_DefaultFocusNavY;
}

BUSTER_GLOBAL_LOCAL UI_Box* ui_widget_box(String8 role, String8 label, UI_BoxFlags flags)
{
    Arena* arena = ui_build_arena();
    String8 id = ui_widget_id(arena, role, label);
    return ui_box_make(flags, id);
}

BUSTER_GLOBAL_LOCAL UI_WidgetResult ui_widget_result(UI_Box* box, UI_Signal signal)
{
    UI_WidgetResult result = {
        .box = box,
        .signal = signal,
        .changed = ui_clicked(signal),
        .value = false,
        .value_f32 = 0.0f,
    };
    return result;
}

UI_Box* ui_label(String8 string)
{
    ui_set_next_pref_width(ui_text_dim(8.0f, 1.0f));
    ui_set_next_pref_height(ui_text_dim(2.0f, 1.0f));
    return ui_widget_box(S8("label"), string, UI_BoxFlag_DrawText);
}

UI_Box* ui_spacer(UI_Size width, UI_Size height)
{
    ui_set_next_pref_width(width);
    ui_set_next_pref_height(height);
    return ui_widget_box(S8("spacer"), S8(""), 0);
}

UI_Box* ui_separator(Axis2 axis)
{
    if (axis == AXIS2_X)
    {
        ui_set_next_pref_width(ui_percentage(1.0f, 0.0f));
        ui_set_next_pref_height(ui_pixels(1, 1.0f));
        return ui_widget_box(S8("separator_x"), S8(""), UI_BoxFlag_DrawSideBottom);
    }
    ui_set_next_pref_width(ui_pixels(1, 1.0f));
    ui_set_next_pref_height(ui_percentage(1.0f, 0.0f));
    return ui_widget_box(S8("separator_y"), S8(""), UI_BoxFlag_DrawSideRight);
}

UI_Signal ui_button(String8 string)
{
    ui_set_next_pref_width(ui_text_dim(8.0f, 1.0f));
    ui_set_next_pref_height(ui_text_dim(4.0f, 1.0f));
    UI_BoxFlags flags = UI_BoxFlag_DrawText | UI_BoxFlag_DrawBackground | UI_BoxFlag_DrawBorder | UI_BoxFlag_DrawHotEffects |
                        UI_BoxFlag_DrawActiveEffects | ui_interactive_widget_flags();
    UI_Box* box = ui_widget_box(S8("button"), string, flags);
    return ui_signal_from_box(box);
}

UI_WidgetResult ui_checkbox(String8 string, bool checked)
{
    ui_set_next_pref_width(ui_text_dim(8.0f, 1.0f));
    ui_set_next_pref_height(ui_text_dim(4.0f, 1.0f));
    UI_BoxFlags flags = UI_BoxFlag_DrawText | UI_BoxFlag_DrawBackground | UI_BoxFlag_DrawBorder | UI_BoxFlag_DrawHotEffects |
                        UI_BoxFlag_DrawActiveEffects | ui_interactive_widget_flags();
    UI_Box* box = ui_widget_box(S8("checkbox"), string, flags);
    Arena* arena = ui_build_arena();
    String8 display = string_format(arena, S8("[{char8}] {S8}"), checked ? 'x' : ' ', ui_widget_display_part(string));
    ui_box_set_display_string(box, display);
    UI_Signal signal = ui_signal_from_box(box);
    UI_WidgetResult result = ui_widget_result(box, signal);
    result.value = checked ^ ui_clicked(signal);
    result.changed = ui_clicked(signal);
    return result;
}

UI_WidgetResult ui_radio(String8 string, bool selected)
{
    ui_set_next_pref_width(ui_text_dim(8.0f, 1.0f));
    ui_set_next_pref_height(ui_text_dim(4.0f, 1.0f));
    UI_BoxFlags flags = UI_BoxFlag_DrawText | UI_BoxFlag_DrawBackground | UI_BoxFlag_DrawBorder | UI_BoxFlag_DrawHotEffects |
                        UI_BoxFlag_DrawActiveEffects | ui_interactive_widget_flags();
    UI_Box* box = ui_widget_box(S8("radio"), string, flags);
    Arena* arena = ui_build_arena();
    String8 display = string_format(arena, S8("({char8}) {S8}"), selected ? '*' : ' ', ui_widget_display_part(string));
    ui_box_set_display_string(box, display);
    UI_Signal signal = ui_signal_from_box(box);
    UI_WidgetResult result = ui_widget_result(box, signal);
    result.value = selected || ui_clicked(signal);
    result.changed = !selected && ui_clicked(signal);
    return result;
}

UI_WidgetResult ui_slider(String8 string, f32 value, f32 minimum, f32 maximum)
{
    ui_set_next_pref_width(ui_percentage(1.0f, 0.0f));
    ui_set_next_pref_height(ui_text_dim(4.0f, 1.0f));
    ui_set_next_child_layout_axis(AXIS2_X);
    UI_BoxFlags flags = UI_BoxFlag_DrawText | UI_BoxFlag_DrawBackground | UI_BoxFlag_DrawBorder | UI_BoxFlag_DrawHotEffects |
                        UI_BoxFlag_DrawActiveEffects | UI_BoxFlag_Clip | ui_interactive_widget_flags();
    UI_Box* box = ui_widget_box(S8("slider"), string, flags);
    UI_Signal signal = ui_signal_from_box(box);
    UI_WidgetResult result = ui_widget_result(box, signal);
    result.value_f32 = BUSTER_CLAMP(minimum, value, maximum);
    f32 span = maximum - minimum;
    if (span > 0.0f && (ui_dragging(signal) || ui_clicked(signal)))
    {
        f32 width = box->rect.x1 - box->rect.x0;
        if (width > 0.0f)
        {
            f32 percentage = BUSTER_CLAMP(0.0f, (float2_element(ui_state_get()->mouse, AXIS2_X) - box->rect.x0) / width, 1.0f);
            result.value_f32 = minimum + span * percentage;
            result.changed = result.value_f32 != value;
        }
    }

    UI_EventIterator iterator = {0};
    if (ui_state_get())
    {
        iterator.list = ui_state_get()->events;
        iterator.current = iterator.list.first;
    }
    UI_Event* event;
    while ((event = ui_next_event(&iterator)))
    {
        bool event_owned = event->owner_assigned && event->owner_key == box->key.value;
        if (event->kind == UI_EventKind_Press && !(box->flags & (UI_BoxFlag_Disabled | UI_BoxFlag_FocusActiveDisabled)) &&
            ((!ui_key_match(ui_state_get()->focus_active_key, ui_key_zero()) && ui_key_match(ui_state_get()->focus_active_key, box->key)) || event_owned) &&
            (!event->owner_assigned || event_owned) &&
            (event->key == WM_KEY_LEFT || event->key == WM_KEY_RIGHT))
        {
            f32 step = span * 0.05f;
            result.value_f32 = BUSTER_CLAMP(minimum, result.value_f32 + (event->key == WM_KEY_RIGHT ? step : -step), maximum);
            result.changed = result.value_f32 != value;
            ui_eat_event(event);
        }
    }

    f32 fill_percentage = span > 0.0f ? BUSTER_CLAMP(0.0f, (result.value_f32 - minimum) / span, 1.0f) : 0.0f;
    ui_push_parent(box);
    ui_set_next_pref_width(ui_percentage(fill_percentage, 0.0f));
    ui_set_next_pref_height(ui_percentage(1.0f, 0.0f));
    UI_Box* fill = ui_widget_box(S8("slider_fill"), string, UI_BoxFlag_DrawBackground);
    fill->background_color = float4_make(0.30f, 0.52f, 0.85f, 0.45f);
    BUSTER_UNUSED(ui_pop_parent());
    return result;
}

BUSTER_GLOBAL_LOCAL u64 ui_text_previous_codepoint(String8 value, u64 position)
{
    while (position > 0 && (((u8)value.pointer[position - 1]) & 0xc0u) == 0x80u)
    {
        position -= 1;
    }
    return position ? position - 1 : 0;
}

BUSTER_GLOBAL_LOCAL void ui_text_delete_range(String8* value, u64 first, u64 one_past_last)
{
    if (value && value->pointer && first < one_past_last && one_past_last <= value->length)
    {
        memmove(value->pointer + first, value->pointer + one_past_last, value->length - one_past_last);
        value->length -= one_past_last - first;
    }
}

BUSTER_GLOBAL_LOCAL bool ui_text_replace_range(String8* value, u64 capacity, u64 first, u64 one_past_last, String8 replacement)
{
    if (!value || !value->pointer || (replacement.length != 0 && !replacement.pointer) || value->length > capacity || first > one_past_last || one_past_last > value->length)
    {
        return false;
    }
    u64 removed_length = one_past_last - first;
    u64 retained_length = value->length - removed_length;
    if (replacement.length > capacity || retained_length > capacity - replacement.length)
    {
        return false;
    }
    memmove(value->pointer + first + replacement.length, value->pointer + one_past_last, value->length - one_past_last);
    if (replacement.length != 0)
    {
        memcpy(value->pointer + first, replacement.pointer, replacement.length);
    }
    value->length = retained_length + replacement.length;
    return true;
}

UI_TextEditResult ui_text_edit(UI_TextEditState* state, String8 label, String8* value, u64 capacity)
{
    UI_TextEditResult result = {0};
    if (!state || !value)
    {
        return result;
    }

    ui_set_next_pref_width(ui_percentage(1.0f, 0.0f));
    ui_set_next_pref_height(ui_text_dim(4.0f, 1.0f));
    UI_BoxFlags flags = UI_BoxFlag_DrawText | UI_BoxFlag_DrawBackground | UI_BoxFlag_DrawBorder | UI_BoxFlag_DrawHotEffects |
                        UI_BoxFlag_DefaultFocusEdit | UI_BoxFlag_FocusHot | UI_BoxFlag_FocusActive | UI_BoxFlag_ClickToFocus | UI_BoxFlag_MouseClickable |
                        UI_BoxFlag_DefaultFocusNavX | UI_BoxFlag_DefaultFocusNavY;
    UI_Box* box = ui_widget_box(S8("text_edit"), label, flags);
    if (!ui_key_match(state->key, box->key))
    {
        state->key = box->key;
        state->cursor = value->length;
        state->mark = state->cursor;
        state->selecting = false;
    }
    state->cursor = BUSTER_MIN(state->cursor, value->length);
    state->mark = BUSTER_MIN(state->mark, value->length);
    ui_box_set_display_string(box, *value);
    UI_Signal signal = ui_signal_from_box(box);
    bool active = !(box->flags & (UI_BoxFlag_Disabled | UI_BoxFlag_FocusActiveDisabled)) && !ui_key_match(ui_state_get()->focus_edit_key, ui_key_zero()) &&
                  ui_key_match(ui_state_get()->focus_edit_key, box->key);
    if (ui_focus_changed(signal))
    {
        state->cursor = value->length;
        state->mark = state->cursor;
    }

    UI_EventIterator iterator = {0};
    iterator.list = ui_state_get()->events;
    iterator.current = iterator.list.first;
    UI_Event* event;
    while ((event = ui_next_event(&iterator)))
    {
        if (event->owner_assigned && event->owner_key != box->key.value)
        {
            continue;
        }
        bool event_owned = event->owner_assigned && event->owner_key == box->key.value;
        if ((!active && !event_owned) || (box->flags & UI_BoxFlag_Disabled))
        {
            continue;
        }
        if (event->kind == UI_EventKind_Text)
        {
            u64 first = BUSTER_MIN(state->cursor, state->mark);
            u64 last = BUSTER_MAX(state->cursor, state->mark);
            if (ui_text_replace_range(value, capacity, first, last, event->string))
            {
                state->cursor = first + event->string.length;
                state->mark = state->cursor;
                state->selecting = false;
                result.changed = (last != first) || event->string.length != 0;
            }
            ui_eat_event(event);
        }
        else if (event->kind == UI_EventKind_Press)
        {
            bool shift = !!(event->modifiers & (1u << WM_MODIFIER_SHIFT));
            bool control = !!(event->modifiers & (1u << WM_MODIFIER_CONTROL));
            u64 old_cursor = state->cursor;
            if (event->key == WM_KEY_HOME)
            {
                state->cursor = 0;
            }
            else if (event->key == WM_KEY_END)
            {
                state->cursor = value->length;
            }
            else if (event->key == WM_KEY_LEFT)
            {
                state->cursor = ui_text_previous_codepoint(*value, state->cursor);
            }
            else if (event->key == WM_KEY_RIGHT)
            {
                if (state->cursor < value->length)
                {
                    state->cursor += 1;
                    while (state->cursor < value->length && (((u8)value->pointer[state->cursor]) & 0xc0u) == 0x80u)
                    {
                        state->cursor += 1;
                    }
                }
            }
            else if (event->key == WM_KEY_BACKSPACE)
            {
                u64 first = BUSTER_MIN(state->cursor, state->mark);
                u64 last = BUSTER_MAX(state->cursor, state->mark);
                if (first == last && state->cursor > 0)
                {
                    first = ui_text_previous_codepoint(*value, state->cursor);
                }
                if (first != last)
                {
                    ui_text_delete_range(value, first, last);
                    state->cursor = first;
                    result.changed = true;
                }
            }
            else if (event->key == WM_KEY_DELETE)
            {
                u64 first = BUSTER_MIN(state->cursor, state->mark);
                u64 last = BUSTER_MAX(state->cursor, state->mark);
                if (first == last && last < value->length)
                {
                    last += 1;
                    while (last < value->length && (((u8)value->pointer[last]) & 0xc0u) == 0x80u)
                    {
                        last += 1;
                    }
                }
                if (first != last)
                {
                    ui_text_delete_range(value, first, last);
                    state->cursor = first;
                    result.changed = true;
                }
            }
            else if (control && event->key == WM_KEY_A)
            {
                state->mark = 0;
                state->cursor = value->length;
                state->selecting = true;
                ui_eat_event(event);
                continue;
            }
            else
            {
                continue;
            }
            if (shift)
            {
                state->mark = state->mark == old_cursor ? old_cursor : state->mark;
                state->selecting = true;
            }
            else
            {
                state->mark = state->cursor;
                state->selecting = false;
            }
            ui_eat_event(event);
        }
    }

    ui_box_set_display_string(box, *value);
    result.widget = ui_widget_result(box, signal);
    result.widget.changed = result.changed;
    result.value = *value;
    result.cursor = state->cursor;
    result.mark = state->mark;
    result.active = active;
    return result;
}

UI_Box* ui_scroll_region_begin(String8 string)
{
    ui_set_next_pref_width(ui_percentage(1.0f, 0.0f));
    ui_set_next_pref_height(ui_percentage(1.0f, 0.0f));
    UI_BoxFlags flags = UI_BoxFlag_DrawBackground | UI_BoxFlag_DrawBorder | UI_BoxFlag_Scroll | UI_BoxFlag_ViewScrollX | UI_BoxFlag_ViewScrollY |
                        UI_BoxFlag_ViewClampX | UI_BoxFlag_ViewClampY | UI_BoxFlag_AllowOverflowX | UI_BoxFlag_AllowOverflowY | UI_BoxFlag_Clip |
                        UI_BoxFlag_FocusHot;
    UI_Box* box = ui_widget_box(S8("scroll_region"), string, flags);
    ui_signal_from_box(box);
    ui_push_parent(box);
    return box;
}

void ui_scroll_region_end(void)
{
    if (ui_top_parent())
    {
        BUSTER_UNUSED(ui_pop_parent());
    }
}

UI_Signal ui_list_row(String8 string, bool selected)
{
    ui_set_next_pref_width(ui_percentage(1.0f, 0.0f));
    ui_set_next_pref_height(ui_text_dim(4.0f, 1.0f));
    UI_BoxFlags flags = UI_BoxFlag_DrawText | UI_BoxFlag_DrawBackground | UI_BoxFlag_DrawHotEffects | UI_BoxFlag_ClickToFocus | UI_BoxFlag_MouseClickable |
                        UI_BoxFlag_KeyboardClickable | UI_BoxFlag_FocusHot | UI_BoxFlag_FocusActive | UI_BoxFlag_DefaultFocusNavY;
    UI_Box* box = ui_widget_box(S8("list_row"), string, flags);
    if (selected)
    {
        box->background_color = float4_make(0.20f, 0.28f, 0.42f, 1.0f);
    }
    return ui_signal_from_box(box);
}

UI_Signal ui_tree_row(String8 string, u32 depth, bool selected, bool expanded)
{
    ui_set_next_text_padding(4.0f + (f32)depth * 16.0f);
    UI_Signal result = ui_list_row(string, selected);
    Arena* arena = ui_build_arena();
    ui_box_set_display_string(result.box, string_format(arena, S8("{char8} {S8}"), expanded ? '-' : '+', ui_widget_display_part(string)));
    return result;
}

UI_WidgetResult ui_tab(String8 string, bool selected)
{
    UI_BoxFlags flags = UI_BoxFlag_DrawText | UI_BoxFlag_DrawBackground | UI_BoxFlag_DrawBorder | UI_BoxFlag_ClickToFocus | UI_BoxFlag_MouseClickable |
                        UI_BoxFlag_KeyboardClickable | UI_BoxFlag_FocusHot | UI_BoxFlag_FocusActive | UI_BoxFlag_DefaultFocusNavX;
    ui_set_next_pref_width(ui_text_dim(8.0f, 1.0f));
    ui_set_next_pref_height(ui_text_dim(4.0f, 1.0f));
    UI_Box* box = ui_widget_box(S8("tab"), string, flags);
    if (selected)
    {
        box->background_color = float4_make(0.22f, 0.30f, 0.44f, 1.0f);
    }
    UI_Signal signal = ui_signal_from_box(box);
    UI_WidgetResult result = ui_widget_result(box, signal);
    result.value = selected || ui_clicked(signal);
    result.changed = !selected && ui_clicked(signal);
    return result;
}

UI_Box* ui_menu_begin(String8 string)
{
    ui_set_next_pref_width(ui_text_dim(16.0f, 1.0f));
    ui_set_next_pref_height(ui_children_sum(1.0f));
    UI_Box* box = ui_widget_box(S8("menu"), string, UI_BoxFlag_DrawBackground | UI_BoxFlag_DrawBorder | UI_BoxFlag_Clip);
    ui_push_parent(box);
    return box;
}

void ui_menu_end(void)
{
    if (ui_top_parent())
    {
        BUSTER_UNUSED(ui_pop_parent());
    }
}

UI_WidgetResult ui_menu_item(String8 string, bool enabled)
{
    ui_set_next_pref_width(ui_percentage(1.0f, 0.0f));
    ui_set_next_pref_height(ui_text_dim(4.0f, 1.0f));
    UI_BoxFlags flags = UI_BoxFlag_DrawText | UI_BoxFlag_DrawHotEffects | UI_BoxFlag_MouseClickable | UI_BoxFlag_KeyboardClickable |
                        UI_BoxFlag_ClickToFocus | UI_BoxFlag_FocusHot | UI_BoxFlag_FocusActive | UI_BoxFlag_DefaultFocusNavY;
    if (!enabled)
    {
        flags |= UI_BoxFlag_Disabled;
    }
    UI_Box* box = ui_widget_box(S8("menu_item"), string, flags);
    UI_Signal signal = ui_signal_from_box(box);
    UI_WidgetResult result = ui_widget_result(box, signal);
    result.value = ui_clicked(signal);
    return result;
}

UI_Box* ui_popup_begin(String8 string)
{
    ui_set_next_pref_width(ui_text_dim(16.0f, 1.0f));
    ui_set_next_pref_height(ui_children_sum(1.0f));
    UI_BoxFlags flags = UI_BoxFlag_DrawBackground | UI_BoxFlag_DrawBorder | UI_BoxFlag_Clip | UI_BoxFlag_FloatingX | UI_BoxFlag_FloatingY;
    UI_Box* box = ui_widget_box(S8("popup"), string, flags);
    ui_push_parent(box);
    return box;
}

void ui_popup_end(void)
{
    if (ui_top_parent())
    {
        BUSTER_UNUSED(ui_pop_parent());
    }
}

UI_WidgetResult ui_popup_item(String8 string, bool enabled)
{
    return ui_menu_item(string, enabled);
}
