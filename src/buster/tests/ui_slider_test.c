// Slider component regressions owned by ui_slider_tests. The fixture helpers
// below cover horizontal-arrow ownership, completed-click coordinates, text-edit
// selection state and checkbox activation parity;
// ui_slider_component_test.c runs this module against the real UI front doors.
#include <buster/tests/ui_slider_test.h>
#if BUSTER_INCLUDE_TESTS
#include <buster/lib/ui_builder.h>
#include <buster/tests/ui_test_internal.h>

BUSTER_GLOBAL_LOCAL UI_WidgetResult ui_test_positioned_slider(String8 label, f32 x, f32 value, UI_BoxFlags flags)
{
    ui_set_next_fixed_x(x);
    ui_set_next_fixed_y(20.0f);
    ui_set_next_fixed_width(100.0f);
    ui_set_next_fixed_height(30.0f);
    ui_set_next_flags(flags | UI_BoxFlag_Floating);
    return ui_slider(label, value, 0.0f, 1.0f);
}

BUSTER_GLOBAL_LOCAL UI_Signal ui_test_slider_neighbor(void)
{
    ui_set_next_fixed_x(160.0f);
    ui_set_next_fixed_y(80.0f);
    ui_set_next_fixed_width(100.0f);
    ui_set_next_fixed_height(30.0f);
    ui_set_next_flags(UI_BoxFlag_Floating);
    return ui_button(S8("slider_neighbor"));
}

BUSTER_GLOBAL_LOCAL void ui_test_slider_pair(f32 value, UI_BoxFlags flags, bool reverse, UI_WidgetResult* slider, UI_Signal* neighbor)
{
    if (reverse)
    {
        *neighbor = ui_test_slider_neighbor();
    }
    *slider = ui_test_positioned_slider(S8("owned_slider"), 20.0f, value, flags);
    if (!reverse)
    {
        *neighbor = ui_test_slider_neighbor();
    }
}

BUSTER_GLOBAL_LOCAL void ui_test_slider_keyboard_ownership(UnitTestArguments* arguments, UnitTestResult* result)
{
    UnitTestResult result_local = {0};
#define result result_local
    for (u32 click_focus = 0; click_focus < 2; click_focus += 1)
    {
        UI_State* state = ui_state_allocate(0, 0);
        UI_WidgetResult slider;
        UI_Signal neighbor;
        ui_test_frame(state, arguments->arena, (UI_EventList){0}, 0.016);
        ui_test_slider_pair(0.5f, 0, false, &slider, &neighbor);
        ui_build_end();
        UI_Key slider_key = slider.box->key;
        UI_Key neighbor_key = neighbor.box->key;
        UI_EventList events = {0};
        if (click_focus)
        {
            UI_Event press = {.kind = UI_EventKind_Press, .key = WM_KEY_MOUSE_LEFT, .pos = ui_test_box_center(slider.box)};
            UI_Event release = press;
            release.kind = UI_EventKind_Release;
            ui_event_list_push(arguments->arena, &events, &press);
            ui_event_list_push(arguments->arena, &events, &release);
        }
        else
        {
            events = ui_test_key_event(arguments->arena, UI_EventKind_Press, WM_KEY_TAB, 0, float2_make(0, 0), S8(""));
        }
        ui_test_frame(state, arguments->arena, events, 0.016);
        ui_test_slider_pair(0.5f, 0, false, &slider, &neighbor);
        ui_build_end();
        BUSTER_TEST(arguments, ui_key_match(state->focus_active_key, slider_key) && !slider.changed && state->events.count == 0);

        WmKey keys[] = {WM_KEY_RIGHT, WM_KEY_LEFT, WM_KEY_RIGHT, WM_KEY_RIGHT, WM_KEY_LEFT, WM_KEY_LEFT};
        f32 values[] = {0.5f, 0.55f, 0.99f, 1.0f, 0.01f, 0.0f};
        f32 expected[] = {0.55f, 0.5f, 1.0f, 1.0f, 0.0f, 0.0f};
        for (u64 index = 0; index < BUSTER_ARRAY_LENGTH(keys); index += 1)
        {
            events = ui_test_key_event(arguments->arena, UI_EventKind_Press, keys[index], 0, float2_make(0, 0), S8(""));
            ui_test_frame(state, arguments->arena, events, 0.016);
            ui_test_slider_pair(values[index], 0, !!(index & 1u), &slider, &neighbor);
            ui_build_end();
            BUSTER_TEST(arguments, slider.value_f32 > expected[index] - 0.000001f && slider.value_f32 < expected[index] + 0.000001f &&
                                       slider.changed == (values[index] != expected[index]) && state->events.count == 0 &&
                                       ui_key_match(state->focus_active_key, slider_key));
        }

        // Right belongs to the slider even though the following Tab leaves it
        // before signals are queried in the opposite widget build order.
        events = (UI_EventList){0};
        UI_Event right = {.kind = UI_EventKind_Press, .key = WM_KEY_RIGHT};
        UI_Event tab = {.kind = UI_EventKind_Press, .key = WM_KEY_TAB};
        ui_event_list_push(arguments->arena, &events, &right);
        ui_event_list_push(arguments->arena, &events, &tab);
        ui_test_frame(state, arguments->arena, events, 0.016);
        ui_test_slider_pair(0.5f, 0, true, &slider, &neighbor);
        ui_build_end();
        BUSTER_TEST(arguments, slider.value_f32 == 0.55f && slider.changed && state->events.count == 0 &&
                                   ui_key_match(state->focus_active_key, neighbor_key));
        events = ui_test_key_event(arguments->arena, UI_EventKind_Press, WM_KEY_LEFT, 0, float2_make(0, 0), S8(""));
        ui_test_frame(state, arguments->arena, events, 0.016);
        ui_test_slider_pair(0.55f, 0, false, &slider, &neighbor);
        ui_build_end();
        BUSTER_TEST(arguments, slider.value_f32 == 0.55f && !slider.changed && state->events.count == 0 &&
                                   ui_key_match(state->focus_active_key, slider_key));
        events = ui_test_key_event(arguments->arena, UI_EventKind_Press, WM_KEY_DOWN, 0, float2_make(0, 0), S8(""));
        ui_test_frame(state, arguments->arena, events, 0.016);
        ui_test_slider_pair(0.55f, 0, false, &slider, &neighbor);
        ui_build_end();
        BUSTER_TEST(arguments, !slider.changed && state->events.count == 0 && ui_key_match(state->focus_active_key, neighbor_key));
        ui_state_deinitialize(state);
    }

    UI_BoxFlags disabled_flags[] = {UI_BoxFlag_Disabled, UI_BoxFlag_FocusActiveDisabled};
    for (u64 index = 0; index < BUSTER_ARRAY_LENGTH(disabled_flags); index += 1)
    {
        UI_State* state = ui_state_allocate(0, 0);
        UI_WidgetResult slider;
        UI_Signal neighbor;
        ui_test_frame(state, arguments->arena, (UI_EventList){0}, 0.016);
        ui_test_slider_pair(0.5f, 0, false, &slider, &neighbor);
        ui_build_end();
        UI_EventList events = ui_test_key_event(arguments->arena, UI_EventKind_Press, WM_KEY_TAB, 0, float2_make(0, 0), S8(""));
        ui_test_frame(state, arguments->arena, events, 0.016);
        ui_test_slider_pair(0.5f, 0, false, &slider, &neighbor);
        ui_build_end();
        events = ui_test_key_event(arguments->arena, UI_EventKind_Press, WM_KEY_RIGHT, 0, float2_make(0, 0), S8(""));
        ui_test_frame(state, arguments->arena, events, 0.016);
        ui_test_slider_pair(0.5f, disabled_flags[index], false, &slider, &neighbor);
        ui_build_end();
        BUSTER_TEST(arguments, slider.value_f32 == 0.5f && !slider.changed && state->events.count == 1 &&
                                   ui_key_match(state->focus_active_key, ui_key_zero()));
        events = ui_test_key_event(arguments->arena, UI_EventKind_Press, WM_KEY_TAB, 0, float2_make(0, 0), S8(""));
        ui_test_frame(state, arguments->arena, events, 0.016);
        ui_test_slider_pair(0.5f, disabled_flags[index], false, &slider, &neighbor);
        ui_build_end();
        BUSTER_TEST(arguments, !slider.changed && ui_key_match(state->focus_active_key, neighbor.box->key));
        ui_state_deinitialize(state);
    }
#undef result
    result->succeeded_test_count += result_local.succeeded_test_count;
    result->test_count += result_local.test_count;
}

BUSTER_GLOBAL_LOCAL void ui_test_slider_release_chronology(UnitTestArguments* arguments, UnitTestResult* result)
{
    UnitTestResult result_local = {0};
#define result result_local
    for (u32 move_case = 0; move_case < 3; move_case += 1)
    {
        UI_State* state = ui_state_allocate(0, 0);
        UI_WidgetResult slider;
        UI_Signal neighbor;
        ui_test_frame(state, arguments->arena, (UI_EventList){0}, 0.016);
        ui_test_slider_pair(0.5f, 0, false, &slider, &neighbor);
        ui_build_end();
        UI_EventList events = ui_test_single_event(arguments->arena, UI_EventKind_Press, WM_KEY_MOUSE_LEFT, float2_make(70, 35), float2_make(0, 0), S8(""));
        ui_test_frame(state, arguments->arena, events, 0.016);
        ui_test_slider_pair(0.5f, 0, false, &slider, &neighbor);
        ui_build_end();
        BUSTER_TEST(arguments, ui_pressed(slider.signal) && ui_dragging(slider.signal) && slider.value_f32 == 0.5f);

        events = (UI_EventList){0};
        UI_Event move = {.kind = UI_EventKind_MouseMove, .pos = float2_make(90, 35)};
        UI_Event release = {.kind = UI_EventKind_Release, .key = WM_KEY_MOUSE_LEFT, .pos = float2_make(45, 35)};
        if (move_case == 2)
        {
            ui_event_list_push(arguments->arena, &events, &move);
        }
        ui_event_list_push(arguments->arena, &events, &release);
        if (move_case)
        {
            move.pos = float2_make(107.5f, 35);
            ui_event_list_push(arguments->arena, &events, &move);
        }
        ui_test_frame(state, arguments->arena, events, 0.016);
        ui_test_slider_pair(0.5f, 0, !!move_case, &slider, &neighbor);
        ui_build_end();
        BUSTER_TEST(arguments, slider.value_f32 == 0.25f && slider.changed && ui_clicked(slider.signal) && ui_released(slider.signal) &&
                                   !ui_dragging(slider.signal) && float2_element(slider.signal.left_click_position, AXIS2_X) == 45.0f &&
                                   ui_key_match(state->active_box_key[UI_MouseButtonKind_Left], ui_key_zero()));
        BUSTER_TEST(arguments, float2_element(state->mouse, AXIS2_X) == (move_case ? 107.5f : 45.0f));
        ui_state_deinitialize(state);
    }

    UI_State* state = ui_state_allocate(0, 0);
    UI_WidgetResult slider;
    UI_Signal neighbor;
    ui_test_frame(state, arguments->arena, (UI_EventList){0}, 0.016);
    ui_test_slider_pair(0.5f, 0, false, &slider, &neighbor);
    ui_build_end();
    UI_EventList events = ui_test_single_event(arguments->arena, UI_EventKind_Press, WM_KEY_MOUSE_LEFT, float2_make(70, 35), float2_make(0, 0), S8(""));
    ui_test_frame(state, arguments->arena, events, 0.016);
    ui_test_slider_pair(0.5f, 0, false, &slider, &neighbor);
    ui_build_end();
    events = ui_test_single_event(arguments->arena, UI_EventKind_MouseMove, WM_KEY_NULL, float2_make(170, 35), float2_make(0, 0), S8(""));
    ui_test_frame(state, arguments->arena, events, 0.016);
    ui_test_slider_pair(0.5f, 0, true, &slider, &neighbor);
    ui_build_end();
    BUSTER_TEST(arguments, slider.changed && slider.value_f32 == 1.0f && ui_dragging(slider.signal));
    events = (UI_EventList){0};
    UI_Event release = {.kind = UI_EventKind_Release, .key = WM_KEY_MOUSE_LEFT, .pos = float2_make(170, 35)};
    UI_Event move = {.kind = UI_EventKind_MouseMove, .pos = float2_make(45, 35)};
    ui_event_list_push(arguments->arena, &events, &release);
    ui_event_list_push(arguments->arena, &events, &move);
    ui_test_frame(state, arguments->arena, events, 0.016);
    ui_test_slider_pair(1.0f, 0, false, &slider, &neighbor);
    ui_build_end();
    BUSTER_TEST(arguments, slider.value_f32 == 1.0f && !slider.changed && ui_released(slider.signal) && !ui_clicked(slider.signal) &&
                               !ui_dragging(slider.signal) && ui_key_match(state->active_box_key[UI_MouseButtonKind_Left], ui_key_zero()));

    // A release followed by a new press restores live capture; its later move
    // must take precedence over the earlier completed release coordinate.
    events = (UI_EventList){0};
    UI_Event press = {.kind = UI_EventKind_Press, .key = WM_KEY_MOUSE_LEFT, .pos = float2_make(70, 35)};
    release.pos = float2_make(45, 35);
    ui_event_list_push(arguments->arena, &events, &press);
    ui_event_list_push(arguments->arena, &events, &release);
    press.pos = float2_make(80, 35);
    ui_event_list_push(arguments->arena, &events, &press);
    move.pos = float2_make(90, 35);
    ui_event_list_push(arguments->arena, &events, &move);
    ui_test_frame(state, arguments->arena, events, 0.016);
    ui_test_slider_pair(1.0f, 0, true, &slider, &neighbor);
    ui_build_end();
    BUSTER_TEST(arguments, slider.changed && slider.value_f32 == 0.7f && ui_released(slider.signal) && ui_dragging(slider.signal) &&
                               float2_element(slider.signal.left_click_position, AXIS2_X) == 45.0f);

    // A later outside release has no click of its own and must not replace
    // the coordinate attached to an earlier completed click in this batch.
    events = (UI_EventList){0};
    release.pos = float2_make(45, 35);
    ui_event_list_push(arguments->arena, &events, &release);
    ui_event_list_push(arguments->arena, &events, &press);
    release.pos = float2_make(170, 35);
    ui_event_list_push(arguments->arena, &events, &release);
    move.pos = float2_make(107.5f, 35);
    ui_event_list_push(arguments->arena, &events, &move);
    ui_test_frame(state, arguments->arena, events, 0.016);
    ui_test_slider_pair(0.7f, 0, false, &slider, &neighbor);
    ui_build_end();
    BUSTER_TEST(arguments, slider.changed && slider.value_f32 == 0.25f && ui_clicked(slider.signal) && !ui_dragging(slider.signal) &&
                               float2_element(slider.signal.left_click_position, AXIS2_X) == 45.0f &&
                               ui_key_match(state->active_box_key[UI_MouseButtonKind_Left], ui_key_zero()));
    ui_state_deinitialize(state);

    for (u32 reverse = 0; reverse < 2; reverse += 1)
    {
        state = ui_state_allocate(0, 0);
        ui_test_frame(state, arguments->arena, (UI_EventList){0}, 0.016);
        BUSTER_UNUSED(ui_test_positioned_slider(S8("slider_a"), 20.0f, 0.5f, 0));
        BUSTER_UNUSED(ui_test_positioned_slider(S8("slider_b"), 160.0f, 0.5f, 0));
        ui_build_end();
        events = (UI_EventList){0};
        press.pos = float2_make(70, 35);
        release.pos = float2_make(45, 35);
        ui_event_list_push(arguments->arena, &events, &press);
        ui_event_list_push(arguments->arena, &events, &release);
        press.pos = float2_make(210, 35);
        release.pos = float2_make(235, 35);
        ui_event_list_push(arguments->arena, &events, &press);
        ui_event_list_push(arguments->arena, &events, &release);
        ui_event_list_push(arguments->arena, &events, &move);
        ui_test_frame(state, arguments->arena, events, 0.016);
        UI_WidgetResult b;
        if (reverse)
        {
            b = ui_test_positioned_slider(S8("slider_b"), 160.0f, 0.5f, 0);
        }
        UI_WidgetResult a = ui_test_positioned_slider(S8("slider_a"), 20.0f, 0.5f, 0);
        if (!reverse)
        {
            b = ui_test_positioned_slider(S8("slider_b"), 160.0f, 0.5f, 0);
        }
        ui_build_end();
        BUSTER_TEST(arguments, a.changed && a.value_f32 == 0.25f && b.changed && b.value_f32 == 0.75f &&
                                   ui_clicked(a.signal) && ui_clicked(b.signal) && !ui_dragging(a.signal) && !ui_dragging(b.signal) &&
                                   float2_element(a.signal.left_click_position, AXIS2_X) == 45.0f &&
                                   float2_element(b.signal.left_click_position, AXIS2_X) == 235.0f &&
                                   float2_element(state->mouse, AXIS2_X) == 107.5f &&
                                   ui_key_match(state->active_box_key[UI_MouseButtonKind_Left], ui_key_zero()));
        ui_state_deinitialize(state);
    }
#undef result
    result->succeeded_test_count += result_local.succeeded_test_count;
    result->test_count += result_local.test_count;
}

BUSTER_GLOBAL_LOCAL UI_TextEditResult ui_test_slider_editor(UI_TextEditState* edit, String8* value, u64 capacity)
{
    ui_set_next_fixed_x(160.0f);
    ui_set_next_fixed_y(20.0f);
    ui_set_next_fixed_width(120.0f);
    ui_set_next_fixed_height(30.0f);
    ui_set_next_flags(UI_BoxFlag_Floating);
    return ui_text_edit(edit, S8("slider_editor"), value, capacity);
}

BUSTER_GLOBAL_LOCAL void ui_test_slider_preserves_editor_arrows(UnitTestArguments* arguments, UnitTestResult* result)
{
    UnitTestResult result_local = {0};
#define result result_local
    UI_State* state = ui_state_allocate(0, 0);
    UI_TextEditState edit = {0};
    char8 memory[16] = "abc";
    String8 value = {.pointer = memory, .length = 3};
    ui_test_frame(state, arguments->arena, (UI_EventList){0}, 0.016);
    BUSTER_UNUSED(ui_test_positioned_slider(S8("owned_slider"), 20.0f, 0.5f, 0));
    BUSTER_UNUSED(ui_test_slider_editor(&edit, &value, BUSTER_ARRAY_LENGTH(memory)));
    ui_build_end();

    UI_EventList events = {0};
    UI_Event tab = {.kind = UI_EventKind_Press, .key = WM_KEY_TAB};
    ui_event_list_push(arguments->arena, &events, &tab);
    ui_event_list_push(arguments->arena, &events, &tab);
    ui_test_frame(state, arguments->arena, events, 0.016);
    UI_WidgetResult slider = ui_test_positioned_slider(S8("owned_slider"), 20.0f, 0.5f, 0);
    UI_TextEditResult editor = ui_test_slider_editor(&edit, &value, BUSTER_ARRAY_LENGTH(memory));
    ui_build_end();
    BUSTER_TEST(arguments, !slider.changed && editor.active && editor.cursor == value.length &&
                               ui_key_match(state->focus_edit_key, editor.widget.box->key));
    WmKey keys[] = {WM_KEY_LEFT, WM_KEY_RIGHT};
    for (u64 index = 0; index < BUSTER_ARRAY_LENGTH(keys); index += 1)
    {
        events = ui_test_key_event(arguments->arena, UI_EventKind_Press, keys[index], 0, float2_make(0, 0), S8(""));
        ui_test_frame(state, arguments->arena, events, 0.016);
        slider = ui_test_positioned_slider(S8("owned_slider"), 20.0f, 0.5f, 0);
        editor = ui_test_slider_editor(&edit, &value, BUSTER_ARRAY_LENGTH(memory));
        ui_build_end();
        BUSTER_TEST(arguments, !slider.changed && slider.value_f32 == 0.5f && editor.cursor == 2 + index &&
                                   ui_key_match(state->focus_active_key, editor.widget.box->key) &&
                                   ui_key_match(state->focus_edit_key, editor.widget.box->key) && state->events.count == 0);
    }
    ui_state_deinitialize(state);
#undef result
    result->succeeded_test_count += result_local.succeeded_test_count;
    result->test_count += result_local.test_count;
}

BUSTER_GLOBAL_LOCAL void ui_test_text_edit_destructive_shift_case(UnitTestArguments* arguments, UnitTestResult* output, WmKey key, bool shift, bool split,
                                                                  const char8* initial, u64 initial_cursor, u64 initial_mark, const char8* expected,
                                                                  u64 expected_cursor)
{
    UnitTestResult result_local = {0};
#define result result_local
    UI_State* state = ui_state_allocate(0, 0);
    char8 memory[16] = {0};
    u64 initial_length = strlen(initial);
    memcpy(memory, initial, initial_length);
    String8 value = {.pointer = memory, .length = initial_length};
    UI_TextEditState edit = {0};
    ui_test_frame(state, arguments->arena, (UI_EventList){0}, 0.016);
    UI_TextEditResult built = ui_text_edit(&edit, S8("Destructive##text"), &value, BUSTER_ARRAY_LENGTH(memory));
    ui_build_end();
    float2 center = ui_test_box_center(built.widget.box);
    UI_EventList events = ui_test_single_event(arguments->arena, UI_EventKind_Press, WM_KEY_MOUSE_LEFT, center, float2_make(0, 0), S8(""));
    ui_test_frame(state, arguments->arena, events, 0.016);
    ui_text_edit(&edit, S8("Destructive##text"), &value, BUSTER_ARRAY_LENGTH(memory));
    ui_build_end();
    edit.cursor = initial_cursor;
    edit.mark = initial_mark;

    u8 modifiers = shift ? (u8)(1u << WM_MODIFIER_SHIFT) : 0;
    UI_Event press = {.kind = UI_EventKind_Press, .key = key, .modifiers = modifiers, .pos = center};
    UI_Event text = {.kind = UI_EventKind_Text, .string = S8("X"), .pos = center};
    events = (UI_EventList){0};
    ui_event_list_push(arguments->arena, &events, &press);
    if (!split)
    {
        ui_event_list_push(arguments->arena, &events, &text);
    }
    ui_test_frame(state, arguments->arena, events, 0.016);
    UI_TextEditResult deleted = ui_text_edit(&edit, S8("Destructive##text"), &value, BUSTER_ARRAY_LENGTH(memory));
    ui_build_end();
    if (split)
    {
        BUSTER_TEST(arguments, deleted.mark == deleted.cursor && deleted.mark <= deleted.value.length && !edit.selecting);
        events = (UI_EventList){0};
        ui_event_list_push(arguments->arena, &events, &text);
        ui_test_frame(state, arguments->arena, events, 0.016);
        deleted = ui_text_edit(&edit, S8("Destructive##text"), &value, BUSTER_ARRAY_LENGTH(memory));
        ui_build_end();
    }
    BUSTER_STRING_TEST(arguments, value, string_from_pointer_length(expected, strlen(expected)));
    BUSTER_TEST(arguments, deleted.changed && deleted.cursor == expected_cursor && deleted.mark == expected_cursor && !edit.selecting &&
                               deleted.mark <= value.length && state->events.count == 0);
    ui_state_deinitialize(state);
#undef result
    output->succeeded_test_count += result_local.succeeded_test_count;
    output->test_count += result_local.test_count;
}

BUSTER_GLOBAL_LOCAL void ui_test_text_edit_destructive_shift(UnitTestArguments* arguments, UnitTestResult* result)
{
    for (u32 split = 0; split < 2; split += 1)
    {
        for (u32 shift = 0; shift < 2; shift += 1)
        {
            // Unselected deletion at the end, interior and start, plus both selection directions and multibyte text.
            ui_test_text_edit_destructive_shift_case(arguments, result, WM_KEY_BACKSPACE, shift, split, "abc", 3, 3, "abX", 3);
            ui_test_text_edit_destructive_shift_case(arguments, result, WM_KEY_BACKSPACE, shift, split, "abc", 1, 1, "Xbc", 1);
            ui_test_text_edit_destructive_shift_case(arguments, result, WM_KEY_DELETE, shift, split, "abc", 0, 0, "Xbc", 1);
            ui_test_text_edit_destructive_shift_case(arguments, result, WM_KEY_DELETE, shift, split, "abc", 2, 2, "abX", 3);
            ui_test_text_edit_destructive_shift_case(arguments, result, WM_KEY_DELETE, shift, split, "abc", 0, 3, "X", 1);
            ui_test_text_edit_destructive_shift_case(arguments, result, WM_KEY_DELETE, shift, split, "abc", 3, 0, "X", 1);
            ui_test_text_edit_destructive_shift_case(arguments, result, WM_KEY_BACKSPACE, shift, split, "abc", 0, 3, "X", 1);
            ui_test_text_edit_destructive_shift_case(arguments, result, WM_KEY_BACKSPACE, shift, split, "abc", 3, 0, "X", 1);
            ui_test_text_edit_destructive_shift_case(arguments, result, WM_KEY_BACKSPACE, shift, split, "a\xc3\xa9", 3, 3, "aX", 2);
            ui_test_text_edit_destructive_shift_case(arguments, result, WM_KEY_DELETE, shift, split, "\xc3\xa9z", 0, 0, "Xz", 1);
        }
    }
}

BUSTER_GLOBAL_LOCAL void ui_test_slider_keyboard_activation_ignores_pointer(UnitTestArguments* arguments, UnitTestResult* result)
{
    UnitTestResult result_local = {0};
#define result result_local
    WmKey activation_keys[] = {WM_KEY_RETURN, WM_KEY_SPACE};
    f32 mouse_x[] = {0.0f, 70.0f, 170.0f};
    for (u64 key_index = 0; key_index < BUSTER_ARRAY_LENGTH(activation_keys); key_index += 1)
    {
        for (u64 mouse_index = 0; mouse_index < BUSTER_ARRAY_LENGTH(mouse_x); mouse_index += 1)
        {
            for (u32 reverse = 0; reverse < 2; reverse += 1)
            {
                UI_State* state = ui_state_allocate(0, 0);
                UI_WidgetResult slider;
                UI_Signal neighbor;
                ui_test_frame(state, arguments->arena, (UI_EventList){0}, 0.016);
                ui_test_slider_pair(0.5f, 0, false, &slider, &neighbor);
                ui_build_end();
                UI_Key slider_key = slider.box->key;
                UI_EventList events = ui_test_key_event(arguments->arena, UI_EventKind_Press, WM_KEY_TAB, 0, float2_make(0, 0), S8(""));
                ui_test_frame(state, arguments->arena, events, 0.016);
                ui_test_slider_pair(0.5f, 0, false, &slider, &neighbor);
                ui_build_end();
                BUSTER_TEST(arguments, ui_key_match(state->focus_active_key, slider_key));

                // The pointer only moves; no button is pressed or released.
                events = (UI_EventList){0};
                UI_Event move = {.kind = UI_EventKind_MouseMove, .pos = float2_make(mouse_x[mouse_index], 35)};
                UI_Event activation = {.kind = UI_EventKind_Press, .key = activation_keys[key_index]};
                ui_event_list_push(arguments->arena, &events, &move);
                ui_event_list_push(arguments->arena, &events, &activation);
                ui_test_frame(state, arguments->arena, events, 0.016);
                ui_test_slider_pair(0.5f, 0, !!reverse, &slider, &neighbor);
                ui_build_end();
                BUSTER_TEST(arguments, ui_clicked(slider.signal));
                BUSTER_TEST(arguments, slider.value_f32 == 0.5f && !slider.changed);
                BUSTER_TEST(arguments, float2_element(state->mouse, AXIS2_X) == mouse_x[mouse_index]);
                ui_state_deinitialize(state);
            }
        }
    }
#undef result
    result->succeeded_test_count += result_local.succeeded_test_count;
    result->test_count += result_local.test_count;
}

BUSTER_GLOBAL_LOCAL UI_WidgetResult ui_test_positioned_checkbox(bool checked)
{
    ui_set_next_fixed_x(20.0f);
    ui_set_next_fixed_y(20.0f);
    ui_set_next_fixed_width(100.0f);
    ui_set_next_fixed_height(30.0f);
    ui_set_next_flags(UI_BoxFlag_Floating);
    return ui_checkbox(S8("parity_checkbox"), checked);
}

// Appends one accepted activation: a Return press/release pair, or a left
// click inside the checkbox.
BUSTER_GLOBAL_LOCAL void ui_test_checkbox_push_activation(UnitTestArguments* arguments, UI_EventList* events, bool mouse)
{
    if (mouse)
    {
        UI_Event press = {.kind = UI_EventKind_Press, .key = WM_KEY_MOUSE_LEFT, .pos = float2_make(70, 35)};
        UI_Event release = press;
        release.kind = UI_EventKind_Release;
        ui_event_list_push(arguments->arena, events, &press);
        ui_event_list_push(arguments->arena, events, &release);
    }
    else
    {
        UI_Event press = {.kind = UI_EventKind_Press, .key = WM_KEY_RETURN};
        UI_Event release = press;
        release.kind = UI_EventKind_Release;
        ui_event_list_push(arguments->arena, events, &press);
        ui_event_list_push(arguments->arena, events, &release);
    }
}

// mode 0: Return pairs, 1: mouse clicks, 2: alternating Return and mouse.
BUSTER_GLOBAL_LOCAL void ui_test_checkbox_parity_case(UnitTestArguments* arguments, UnitTestResult* output, u32 mode, u32 count, bool initial, bool split)
{
    UnitTestResult result_local = {0};
#define result result_local
    UI_State* state = ui_state_allocate(0, 0);
    ui_test_frame(state, arguments->arena, (UI_EventList){0}, 0.016);
    UI_WidgetResult checkbox = ui_test_positioned_checkbox(initial);
    ui_build_end();
    UI_Key key = checkbox.box->key;
    UI_EventList events = ui_test_key_event(arguments->arena, UI_EventKind_Press, WM_KEY_TAB, 0, float2_make(0, 0), S8(""));
    ui_test_frame(state, arguments->arena, events, 0.016);
    checkbox = ui_test_positioned_checkbox(initial);
    ui_build_end();
    BUSTER_TEST(arguments, ui_key_match(state->focus_active_key, key) && !checkbox.changed);

    bool value = initial;
    u32 frame_count = split ? count : (count ? 1u : 0u);
    u32 next = 0;
    for (u32 frame = 0; frame < frame_count; frame += 1)
    {
        events = (UI_EventList){0};
        u32 in_frame = split ? 1u : count;
        for (u32 index = 0; index < in_frame; index += 1)
        {
            ui_test_checkbox_push_activation(arguments, &events, mode == 1 || (mode == 2 && (next & 1u)));
            next += 1;
        }
        ui_test_frame(state, arguments->arena, events, 0.016);
        checkbox = ui_test_positioned_checkbox(value);
        ui_build_end();
        bool expected = value ^ !!(in_frame & 1u);
        BUSTER_TEST(arguments, checkbox.value == expected && checkbox.changed == !!(in_frame & 1u));
        value = checkbox.value;
    }
    BUSTER_TEST(arguments, value == (initial ^ !!(count & 1u)) && ui_key_match(state->focus_active_key, key));
    ui_state_deinitialize(state);
#undef result
    output->succeeded_test_count += result_local.succeeded_test_count;
    output->test_count += result_local.test_count;
}

BUSTER_GLOBAL_LOCAL void ui_test_checkbox_activation_parity(UnitTestArguments* arguments, UnitTestResult* result)
{
    for (u32 mode = 0; mode < 3; mode += 1)
    {
        for (u32 count = 0; count <= 4; count += 1)
        {
            for (u32 initial = 0; initial < 2; initial += 1)
            {
                ui_test_checkbox_parity_case(arguments, result, mode, count, !!initial, false);
                ui_test_checkbox_parity_case(arguments, result, mode, count, !!initial, true);
            }
        }
    }
}

UnitTestResult ui_slider_tests(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    ui_test_slider_keyboard_ownership(arguments, &result);
    ui_test_slider_release_chronology(arguments, &result);
    ui_test_slider_keyboard_activation_ignores_pointer(arguments, &result);
    ui_test_slider_preserves_editor_arrows(arguments, &result);
    ui_test_text_edit_destructive_shift(arguments, &result);
    ui_test_checkbox_activation_parity(arguments, &result);
    return result;
}
#endif
