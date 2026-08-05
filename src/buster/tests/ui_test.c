#include <buster/tests/ui_test.h>
#if BUSTER_INCLUDE_TESTS

#include <buster/lib/ui_builder.h>
#include <buster/lib/string.h>

BUSTER_GLOBAL_LOCAL void ui_test_frame(UI_State* state, Arena* arena, UI_EventList events, f64 frame_time)
{
    ui_state_select(state);
    // Test callers use seconds; ui_build_begin's public frame-time unit is ms.
    ui_build_begin(0, 0, frame_time * 1000.0, events);
    BUSTER_UNUSED(arena);
}

BUSTER_GLOBAL_LOCAL UI_EventList ui_test_single_event(Arena* arena, UI_EventKind kind, WmKey key, float2 position, float2 delta, String8 string)
{
    UI_EventList result = {0};
    UI_Event event = {
        .kind = kind,
        .key = key,
        .pos = position,
        .delta = delta,
        .string = string,
    };
    ui_event_list_push(arena, &result, &event);
    return result;
}

BUSTER_GLOBAL_LOCAL UI_EventList ui_test_key_event(Arena* arena, UI_EventKind kind, WmKey key, u8 modifiers, float2 position, String8 string)
{
    UI_EventList result = {0};
    UI_Event event = {
        .kind = kind,
        .key = key,
        .modifiers = modifiers,
        .pos = position,
        .string = string,
    };
    ui_event_list_push(arena, &result, &event);
    return result;
}

BUSTER_GLOBAL_LOCAL float2 ui_test_box_center(UI_Box* box)
{
    return float2_make((box->rect.x0 + box->rect.x1) * 0.5f, (box->rect.y0 + box->rect.y1) * 0.5f);
}

BUSTER_GLOBAL_LOCAL UI_EventList ui_test_drop_event(Arena* arena, float2 position, String8 path)
{
    String8 paths[] = {path};
    UI_EventList result = {0};
    UI_Event event = {
        .kind = UI_EventKind_FileDrop,
        .pos = position,
        .paths = {.pointer = paths, .length = BUSTER_ARRAY_LENGTH(paths)},
    };
    ui_event_list_push(arena, &result, &event);
    return result;
}

BUSTER_GLOBAL_LOCAL UI_DrawCommand* ui_test_first_draw_command(UI_State* state, UI_Box* box, UI_DrawCommandKind kind)
{
    for (u64 index = 0; index < state->draw_command_count; index += 1)
    {
        UI_DrawCommand* command = &state->draw_commands[index];
        if (command->box == box && command->kind == kind)
        {
            return command;
        }
    }
    return 0;
}

BUSTER_GLOBAL_LOCAL UI_DrawCommand* ui_test_highlight_command(UI_State* state, UI_Box* box)
{
    for (u64 index = 0; index < state->draw_command_count; index += 1)
    {
        UI_DrawCommand* command = &state->draw_commands[index];
        if (command->box == box && command->kind == UI_DrawCommandKind_Rect && command->rect.y1 == box->rect.y1 &&
            command->rect.y0 == box->rect.y1 - 2.0f && command->rect.x1 > command->rect.x0)
        {
            return command;
        }
    }
    return 0;
}

BUSTER_GLOBAL_LOCAL u64 ui_test_draw_command_index(UI_State* state, UI_Box* box, UI_DrawCommandKind kind)
{
    for (u64 index = 0; index < state->draw_command_count; index += 1)
    {
        if (state->draw_commands[index].box == box && state->draw_commands[index].kind == kind)
        {
            return index;
        }
    }
    return (u64)-1;
}

BUSTER_GLOBAL_LOCAL UI_DrawCommand* ui_test_text_command(UI_State* state, UI_Box* box, String8 text)
{
    for (u64 index = 0; index < state->draw_command_count; index += 1)
    {
        UI_DrawCommand* command = &state->draw_commands[index];
        if (command->box == box && command->kind == UI_DrawCommandKind_Text && string_equal(command->text, text))
        {
            return command;
        }
    }
    return 0;
}

BUSTER_GLOBAL_LOCAL UI_Box* ui_test_build_tooltip_box(UI_BoxFlags flags)
{
    ui_set_next_fixed_x(50.0f);
    ui_set_next_fixed_y(50.0f);
    ui_set_next_fixed_width(30.0f);
    ui_set_next_fixed_height(20.0f);
    return ui_box_make(flags, S8("tooltip text"));
}

BUSTER_GLOBAL_LOCAL UI_Box* ui_test_build_fastpath_box(void)
{
    ui_set_next_fixed_width(100.0f);
    ui_set_next_fixed_height(20.0f);
    UI_Box* result = ui_box_make(UI_BoxFlag_DrawText | UI_BoxFlag_DrawTextFastpathCodepoint | UI_BoxFlag_FocusHot, S8("exit"));
    ui_box_set_fastpath_codepoint(result, (u32)'x');
    return result;
}

BUSTER_GLOBAL_LOCAL UI_Box* ui_test_build_positioned_fastpath_box(String8 label, f32 x)
{
    ui_set_next_fixed_x(x);
    ui_set_next_fixed_y(32.0f);
    ui_set_next_fixed_width(100.0f);
    ui_set_next_fixed_height(24.0f);
    UI_Box* result = ui_box_make(UI_BoxFlag_FloatingX | UI_BoxFlag_FloatingY | UI_BoxFlag_DrawText | UI_BoxFlag_DrawTextFastpathCodepoint | UI_BoxFlag_FocusHot,
                                 label);
    ui_box_set_fastpath_codepoint(result, (u32)'x');
    return result;
}

BUSTER_GLOBAL_LOCAL void ui_test_build_clipped_hit_tree(UI_Box** disjoint_child, UI_Box** empty_child)
{
    ui_set_next_fixed_width(240.0f);
    ui_set_next_fixed_height(160.0f);
    UI_Box* root = ui_box_make(0, S8("clipped_hit_root"));
    ui_push_parent(root);

    ui_set_next_fixed_x(20.0f);
    ui_set_next_fixed_y(20.0f);
    ui_set_next_fixed_width(40.0f);
    ui_set_next_fixed_height(40.0f);
    UI_Box* disjoint_parent = ui_box_make(UI_BoxFlag_FloatingX | UI_BoxFlag_FloatingY | UI_BoxFlag_Clip, S8("disjoint_hit_parent"));
    ui_push_parent(disjoint_parent);
    ui_set_next_fixed_x(100.0f);
    ui_set_next_fixed_y(0.0f);
    ui_set_next_fixed_width(30.0f);
    ui_set_next_fixed_height(20.0f);
    *disjoint_child = ui_box_make(UI_BoxFlag_FloatingX | UI_BoxFlag_FloatingY | UI_BoxFlag_MouseClickable | UI_BoxFlag_KeyboardClickable |
                                      UI_BoxFlag_ClickToFocus | UI_BoxFlag_FocusHot | UI_BoxFlag_FocusActive | UI_BoxFlag_Scroll |
                                      UI_BoxFlag_ViewScrollY | UI_BoxFlag_DropSite,
                                  S8("disjoint_hit_child"));
    BUSTER_UNUSED(ui_pop_parent());

    ui_set_next_fixed_x(20.0f);
    ui_set_next_fixed_y(90.0f);
    ui_set_next_fixed_width(0.0f);
    ui_set_next_fixed_height(0.0f);
    UI_Box* empty_parent = ui_box_make(UI_BoxFlag_FloatingX | UI_BoxFlag_FloatingY | UI_BoxFlag_Clip, S8("empty_hit_parent"));
    ui_push_parent(empty_parent);
    ui_set_next_fixed_x(0.0f);
    ui_set_next_fixed_y(0.0f);
    ui_set_next_fixed_width(30.0f);
    ui_set_next_fixed_height(20.0f);
    *empty_child = ui_box_make(UI_BoxFlag_FloatingX | UI_BoxFlag_FloatingY | UI_BoxFlag_MouseClickable | UI_BoxFlag_KeyboardClickable |
                                   UI_BoxFlag_ClickToFocus | UI_BoxFlag_FocusHot | UI_BoxFlag_FocusActive | UI_BoxFlag_Scroll |
                                   UI_BoxFlag_ViewScrollY | UI_BoxFlag_DropSite,
                               S8("empty_hit_child"));
    BUSTER_UNUSED(ui_pop_parent());
    BUSTER_UNUSED(ui_pop_parent());
}

BUSTER_GLOBAL_LOCAL void ui_test_flag_inventory(UnitTestArguments* arguments, UnitTestResult* result)
{
    BUSTER_UNUSED(arguments);
    UnitTestResult result_local = {0};
#define result result_local
    for (u32 bit = 0; bit < UI_BOX_FLAG_COUNT; bit += 1)
    {
        UI_BoxFlagInfo info = ui_box_flag_info(bit);
        BUSTER_TEST_RAW(arguments, info.implemented && info.flag == ((UI_BoxFlags)1ull << bit), S8("UI flag inventory missing bit"));
    }
    for (u32 bit = 57; bit < 63; bit += 1)
    {
        UI_BoxFlagInfo info = ui_box_flag_info(bit);
        BUSTER_TEST_RAW(arguments, !info.implemented && info.flag == 0, S8("UI unassigned flag bit became classified"));
    }
    UI_BoxFlagInfo debug = ui_box_flag_info(63);
    BUSTER_TEST_RAW(arguments, debug.implemented && debug.flag == UI_BoxFlag_Debug, S8("UI debug flag inventory missing"));
    UI_BoxFlagInfo blur = ui_box_flag_info(28);
    UI_BoxFlagInfo bucket = ui_box_flag_info(41);
    UI_BoxFlagInfo rounded = ui_box_flag_info(55);
    BUSTER_TEST_RAW(arguments, blur.renderer_dependency && bucket.renderer_dependency && rounded.renderer_dependency,
                    S8("UI renderer dependency inventory missing"));
    BUSTER_TEST(arguments, (UI_BoxFlag_All & UI_BoxFlag_AllContiguous) == UI_BoxFlag_AllContiguous);
    BUSTER_TEST(arguments, (UI_BoxFlag_All & UI_BoxFlag_Debug) == UI_BoxFlag_Debug);
    BUSTER_TEST(arguments, (UI_BoxFlag_All & ((UI_BoxFlags)0x3full << 57)) == 0);
#undef result
    result->succeeded_test_count += result_local.succeeded_test_count;
    result->test_count += result_local.test_count;
}

BUSTER_GLOBAL_LOCAL void ui_test_layout_and_view(UnitTestArguments* arguments, UnitTestResult* result)
{
    BUSTER_UNUSED(arguments);
    UnitTestResult result_local = {0};
#define result result_local
    UI_State* state = ui_state_allocate(0, 0);
    ui_test_frame(state, arguments->arena, (UI_EventList){0}, 0.016);

    ui_set_next_fixed_width(100.0f);
    ui_set_next_fixed_height(50.0f);
    UI_Box* parent = ui_box_make(UI_BoxFlag_Clip | UI_BoxFlag_SquishAnchored | UI_BoxFlag_ViewClampY, S8("layout_parent"));
    ui_push_parent(parent);
    ui_set_next_pref_width(ui_pixels(30, 1.0f));
    ui_set_next_pref_height(ui_pixels(10, 1.0f));
    UI_Box* first = ui_box_make(UI_BoxFlag_DrawBackground, S8("layout_first"));
    ui_set_next_pref_width(ui_percentage(0.5f, 0.0f));
    ui_set_next_pref_height(ui_pixels(100, 1.0f));
    UI_Box* second = ui_box_make(UI_BoxFlag_DrawBackground | UI_BoxFlag_AllowOverflowY, S8("layout_second"));
    ui_pop_parent();
    ui_build_end();

    BUSTER_TEST(arguments, float2_element(parent->fixed_size, AXIS2_X) == 100.0f && float2_element(parent->fixed_size, AXIS2_Y) == 50.0f);
    BUSTER_TEST(arguments, first->rect.x0 == parent->rect.x0 && first->rect.x1 - first->rect.x0 == 30.0f);
    BUSTER_TEST(arguments, float2_element(second->fixed_size, AXIS2_X) == 50.0f && float2_element(second->fixed_size, AXIS2_Y) == 100.0f);
    BUSTER_TEST(arguments, second->clip_rect.y1 <= parent->rect.y1);

    ui_test_frame(state, arguments->arena, (UI_EventList){0}, 0.016);
    ui_set_next_fixed_width(20.0f);
    ui_set_next_fixed_height(20.0f);
    parent = ui_box_make(UI_BoxFlag_Clip, S8("fully_clipped_parent"));
    ui_push_parent(parent);
    ui_set_next_fixed_x(30.0f);
    ui_set_next_fixed_y(0.0f);
    ui_set_next_fixed_width(10.0f);
    ui_set_next_fixed_height(10.0f);
    UI_Box* fully_clipped = ui_box_make(UI_BoxFlag_FloatingX | UI_BoxFlag_FloatingY | UI_BoxFlag_DrawBackground, S8("fully_clipped_child"));
    ui_pop_parent();
    ui_build_end();
    BUSTER_TEST(arguments, !fully_clipped->visible && !(fully_clipped->state_flags & UI_BoxState_Visible));

    ui_test_frame(state, arguments->arena, (UI_EventList){0}, 0.016);
    ui_set_next_fixed_width(50.0f);
    ui_set_next_fixed_height(20.0f);
    parent = ui_box_make(UI_BoxFlag_Clip | UI_BoxFlag_RoundChildrenByParent, S8("appearance_parent"));
    ui_box_set_corner_radii(parent, 7.0f);
    ui_push_parent(parent);
    ui_set_next_fixed_width(100.0f);
    ui_set_next_fixed_height(20.0f);
    UI_Box* appearance = ui_box_make(UI_BoxFlag_DrawDropShadow | UI_BoxFlag_DrawBackgroundBlur | UI_BoxFlag_DrawBackground | UI_BoxFlag_DrawBorder |
                                         UI_BoxFlag_DrawSideTop | UI_BoxFlag_DrawSideBottom | UI_BoxFlag_DrawSideLeft | UI_BoxFlag_DrawSideRight |
                                         UI_BoxFlag_DrawText | UI_BoxFlag_DrawTextFastpathCodepoint | UI_BoxFlag_DrawTextWeak | UI_BoxFlag_DrawOverlay |
                                         UI_BoxFlag_DrawBucket | UI_BoxFlag_DrawFadeTop | UI_BoxFlag_DrawFadeBottom | UI_BoxFlag_DrawFadeLeft |
                                         UI_BoxFlag_DrawFadeRight | UI_BoxFlag_AllowOverflowX | UI_BoxFlag_Debug | UI_BoxFlag_HasFuzzyMatchRanges,
                                     S8("abcdefghijklmnop"));
    UI_FuzzyMatchRange appearance_range = {.first = 1, .one_past_last = 3};
    ui_box_set_fuzzy_match_ranges(appearance, &appearance_range, 1);
    ui_pop_parent();
    ui_build_end();
    BUSTER_TEST(arguments, appearance->renderer_dependency_flags ==
                                  (UI_BoxRendererDependency_BackgroundBlur | UI_BoxRendererDependency_BucketSubmission));
    BUSTER_TEST(arguments, parent->renderer_dependency_flags == (UI_BoxRendererDependency_CornerRadii | UI_BoxRendererDependency_TextScissor));
    BUSTER_TEST(arguments, appearance->draw_pass == 1 && (appearance->state_flags & UI_BoxState_RendererDependency));
    BUSTER_TEST(arguments, appearance->text_truncated && (appearance->state_flags & UI_BoxState_TextTruncated));
    BUSTER_TEST(arguments, appearance->state_flags & UI_BoxState_TextFastpath);
    BUSTER_TEST(arguments, appearance->state_flags & UI_BoxState_Overlay);
    BUSTER_TEST(arguments, appearance->state_flags & UI_BoxState_Fade);
    BUSTER_TEST(arguments, appearance->corner_radii[CORNER_00] == 7.0f);

    ui_test_frame(state, arguments->arena, (UI_EventList){0}, 0.016);
    ui_set_next_fixed_width(100.0f);
    ui_set_next_fixed_height(20.0f);
    UI_Box* scroll = ui_box_make(UI_BoxFlag_Clip | UI_BoxFlag_Scroll | UI_BoxFlag_ViewScrollY | UI_BoxFlag_ViewClampY, S8("scroll_parent"));
    ui_push_parent(scroll);
    ui_set_next_pref_width(ui_pixels(90, 1.0f));
    ui_set_next_pref_height(ui_pixels(100, 1.0f));
    ui_box_make(UI_BoxFlag_DrawBackground, S8("scroll_child"));
    ui_pop_parent();
    ui_build_end();
    BUSTER_TEST(arguments, float2_element(scroll->view_bounds, AXIS2_Y) == 100.0f);

    UI_EventList scroll_events = ui_test_single_event(arguments->arena, UI_EventKind_Scroll, WM_KEY_MOUSE_WHEEL_UP, float2_make(10.0f, 10.0f),
                                                       float2_make(0.0f, 1.0f), S8(""));
    ui_test_frame(state, arguments->arena, scroll_events, 0.016);
    ui_set_next_fixed_width(100.0f);
    ui_set_next_fixed_height(20.0f);
    scroll = ui_box_make(UI_BoxFlag_Clip | UI_BoxFlag_Scroll | UI_BoxFlag_ViewScrollY | UI_BoxFlag_ViewClampY, S8("scroll_parent"));
    ui_push_parent(scroll);
    ui_set_next_pref_width(ui_pixels(90, 1.0f));
    ui_set_next_pref_height(ui_pixels(100, 1.0f));
    ui_box_make(UI_BoxFlag_DrawBackground, S8("scroll_child"));
    ui_pop_parent();
    UI_Signal scroll_signal = ui_signal_from_box(scroll);
    ui_build_end();
    BUSTER_TEST(arguments, ui_scrolled(scroll_signal));
    BUSTER_TEST(arguments, float2_element(scroll->view_off, AXIS2_Y) == 32.0f);
    BUSTER_TEST(arguments, float2_element(scroll->view_off, AXIS2_Y) <= float2_element(scroll->view_bounds, AXIS2_Y) - (scroll->rect.y1 - scroll->rect.y0));

    ui_test_frame(state, arguments->arena, (UI_EventList){0}, 0.016);
    ui_set_next_fixed_width(60.0f);
    ui_set_next_fixed_height(40.0f);
    ui_set_next_child_layout_axis(AXIS2_Y);
    UI_Box* cross_axis_parent = ui_box_make(0, S8("cross_axis_parent"));
    ui_push_parent(cross_axis_parent);
    ui_set_next_pref_width(ui_pixels(100.0f, 0.0f));
    ui_set_next_pref_height(ui_pixels(10.0f, 1.0f));
    UI_Box* cross_axis_first = ui_box_make(0, S8("cross_axis_first"));
    ui_set_next_pref_width(ui_pixels(100.0f, 0.0f));
    ui_set_next_pref_height(ui_pixels(10.0f, 1.0f));
    UI_Box* cross_axis_second = ui_box_make(0, S8("cross_axis_second"));
    BUSTER_UNUSED(ui_pop_parent());
    ui_build_end();
    BUSTER_TEST(arguments, float2_element(cross_axis_first->fixed_size, AXIS2_X) == 60.0f &&
                               float2_element(cross_axis_second->fixed_size, AXIS2_X) == 60.0f &&
                               cross_axis_first->rect.x1 - cross_axis_first->rect.x0 <= 60.0f &&
                               cross_axis_second->rect.x1 - cross_axis_second->rect.x0 <= 60.0f);
    ui_state_deinitialize(state);
#undef result
    result->succeeded_test_count += result_local.succeeded_test_count;
    result->test_count += result_local.test_count;
}

BUSTER_GLOBAL_LOCAL void ui_test_input_and_focus(UnitTestArguments* arguments, UnitTestResult* result)
{
    BUSTER_UNUSED(arguments);
    UnitTestResult result_local = {0};
#define result result_local
    UI_State* state = ui_state_allocate(0, 0);
    ui_test_frame(state, arguments->arena, (UI_EventList){0}, 0.016);
    ui_set_next_fixed_width(160.0f);
    ui_set_next_fixed_height(30.0f);
    UI_Box* button = ui_box_make(UI_BoxFlag_DrawText | UI_BoxFlag_Clickable | UI_BoxFlag_ClickToFocus | UI_BoxFlag_FocusHot | UI_BoxFlag_FocusActive |
                                      UI_BoxFlag_DefaultFocusNavY,
                                  S8("focus_button"));
    ui_build_end();
    float2 center = float2_make((button->rect.x0 + button->rect.x1) * 0.5f, (button->rect.y0 + button->rect.y1) * 0.5f);

    UI_EventList press_events = ui_test_single_event(arguments->arena, UI_EventKind_Press, WM_KEY_MOUSE_LEFT, center, float2_make(0, 0), S8(""));
    ui_test_frame(state, arguments->arena, press_events, 0.016);
    ui_set_next_fixed_width(160.0f);
    ui_set_next_fixed_height(30.0f);
    button = ui_box_make(UI_BoxFlag_DrawText | UI_BoxFlag_Clickable | UI_BoxFlag_ClickToFocus | UI_BoxFlag_FocusHot | UI_BoxFlag_FocusActive |
                             UI_BoxFlag_DefaultFocusNavY,
                         S8("focus_button"));
    UI_Signal press = ui_signal_from_box(button);
    ui_build_end();
    BUSTER_TEST(arguments, ui_pressed(press));
    BUSTER_TEST(arguments, ui_key_match(state->focus_active_key, button->key));

    UI_EventList release_events = ui_test_single_event(arguments->arena, UI_EventKind_Release, WM_KEY_MOUSE_LEFT, center, float2_make(0, 0), S8(""));
    ui_test_frame(state, arguments->arena, release_events, 0.016);
    ui_set_next_fixed_width(160.0f);
    ui_set_next_fixed_height(30.0f);
    button = ui_box_make(UI_BoxFlag_DrawText | UI_BoxFlag_Clickable | UI_BoxFlag_ClickToFocus | UI_BoxFlag_FocusHot | UI_BoxFlag_FocusActive |
                             UI_BoxFlag_DefaultFocusNavY,
                         S8("focus_button"));
    UI_Signal release = ui_signal_from_box(button);
    ui_build_end();
    BUSTER_TEST(arguments, ui_clicked(release) && ui_released(release));

    ui_test_frame(state, arguments->arena, (UI_EventList){0}, 0.016);
    ui_set_next_fixed_width(160.0f);
    ui_set_next_fixed_height(30.0f);
    UI_Box* disabled = ui_box_make(UI_BoxFlag_DrawText | UI_BoxFlag_Clickable | UI_BoxFlag_Disabled, S8("disabled_button"));
    ui_build_end();
    center = float2_make((disabled->rect.x0 + disabled->rect.x1) * 0.5f, (disabled->rect.y0 + disabled->rect.y1) * 0.5f);
    press_events = ui_test_single_event(arguments->arena, UI_EventKind_Press, WM_KEY_MOUSE_LEFT, center, float2_make(0, 0), S8(""));
    ui_test_frame(state, arguments->arena, press_events, 0.016);
    ui_set_next_fixed_width(160.0f);
    ui_set_next_fixed_height(30.0f);
    disabled = ui_box_make(UI_BoxFlag_DrawText | UI_BoxFlag_Clickable | UI_BoxFlag_Disabled, S8("disabled_button"));
    UI_Signal disabled_signal = ui_signal_from_box(disabled);
    ui_build_end();
    BUSTER_TEST(arguments, !ui_pressed(disabled_signal));

    ui_test_frame(state, arguments->arena, (UI_EventList){0}, 0.016);
    ui_set_next_fixed_width(160.0f);
    ui_set_next_fixed_height(30.0f);
    UI_Box* drop = ui_box_make(UI_BoxFlag_DropSite, S8("drop_site"));
    ui_build_end();
    center = float2_make((drop->rect.x0 + drop->rect.x1) * 0.5f, (drop->rect.y0 + drop->rect.y1) * 0.5f);
    char8 drop_path_data[] = "/tmp/accepted.txt";
    String8 drop_path_strings[] = {{.pointer = drop_path_data, .length = BUSTER_ARRAY_LENGTH(drop_path_data) - 1}};
    UI_EventList drop_events = {0};
    UI_Event drop_event = {
        .kind = UI_EventKind_FileDrop,
        .pos = center,
        .paths = {.pointer = drop_path_strings, .length = BUSTER_ARRAY_LENGTH(drop_path_strings)},
    };
    ui_event_list_push(arguments->arena, &drop_events, &drop_event);
    ui_test_frame(state, arguments->arena, drop_events, 0.016);
    ui_set_next_fixed_width(160.0f);
    ui_set_next_fixed_height(30.0f);
    drop = ui_box_make(UI_BoxFlag_DropSite, S8("drop_site"));
    UI_Signal drop_signal = ui_signal_from_box(drop);
    drop_path_data[0] = 'X';
    ui_build_end();
    BUSTER_TEST(arguments, ui_dropped(drop_signal) && drop_signal.drop_paths.length == 1 &&
                               string_equal(drop_signal.drop_paths.pointer[0], S8("/tmp/accepted.txt")));

    ui_test_frame(state, arguments->arena, (UI_EventList){0}, 0.016);
    ui_set_next_fixed_width(160.0f);
    ui_set_next_fixed_height(30.0f);
    UI_Box* nav_first = ui_box_make(UI_BoxFlag_KeyboardClickable | UI_BoxFlag_FocusActive | UI_BoxFlag_DefaultFocusNavY, S8("nav_first"));
    ui_set_next_fixed_width(160.0f);
    ui_set_next_fixed_height(30.0f);
    UI_Box* nav_second = ui_box_make(UI_BoxFlag_KeyboardClickable | UI_BoxFlag_FocusActive | UI_BoxFlag_DefaultFocusNavY, S8("nav_second"));
    ui_build_end();
    BUSTER_TEST(arguments, nav_first != 0 && nav_second != 0);
    UI_EventList tab_events = ui_test_single_event(arguments->arena, UI_EventKind_Press, WM_KEY_TAB, center, float2_make(0, 0), S8(""));
    ui_test_frame(state, arguments->arena, tab_events, 0.016);
    ui_set_next_fixed_width(160.0f);
    ui_set_next_fixed_height(30.0f);
    nav_first = ui_box_make(UI_BoxFlag_KeyboardClickable | UI_BoxFlag_FocusActive | UI_BoxFlag_DefaultFocusNavY, S8("nav_first"));
    ui_set_next_fixed_width(160.0f);
    ui_set_next_fixed_height(30.0f);
    nav_second = ui_box_make(UI_BoxFlag_KeyboardClickable | UI_BoxFlag_FocusActive | UI_BoxFlag_DefaultFocusNavY, S8("nav_second"));
    ui_build_end();
    BUSTER_TEST(arguments, nav_second != 0);
    BUSTER_TEST(arguments, ui_key_match(state->focus_active_key, nav_first->key));
    tab_events = ui_test_single_event(arguments->arena, UI_EventKind_Press, WM_KEY_TAB, center, float2_make(0, 0), S8(""));
    ui_test_frame(state, arguments->arena, tab_events, 0.016);
    ui_set_next_fixed_width(160.0f);
    ui_set_next_fixed_height(30.0f);
    nav_first = ui_box_make(UI_BoxFlag_KeyboardClickable | UI_BoxFlag_FocusActive | UI_BoxFlag_DefaultFocusNavY, S8("nav_first"));
    ui_set_next_fixed_width(160.0f);
    ui_set_next_fixed_height(30.0f);
    nav_second = ui_box_make(UI_BoxFlag_KeyboardClickable | UI_BoxFlag_FocusActive | UI_BoxFlag_DefaultFocusNavY, S8("nav_second"));
    ui_build_end();
    BUSTER_TEST(arguments, nav_first != 0);
    BUSTER_TEST(arguments, ui_key_match(state->focus_active_key, nav_second->key));
    ui_state_deinitialize(state);
#undef result
    result->succeeded_test_count += result_local.succeeded_test_count;
    result->test_count += result_local.test_count;
}

BUSTER_GLOBAL_LOCAL void ui_test_clipped_hit_ownership(UnitTestArguments* arguments, UnitTestResult* result)
{
    BUSTER_UNUSED(arguments);
    UnitTestResult result_local = {0};
#define result result_local
    UI_State* state = ui_state_allocate(0, 0);
    UI_Box* disjoint_child = 0;
    UI_Box* empty_child = 0;
    ui_test_frame(state, arguments->arena, (UI_EventList){0}, 0.016);
    ui_test_build_clipped_hit_tree(&disjoint_child, &empty_child);
    ui_build_end();
    float2 disjoint_point = ui_test_box_center(disjoint_child);
    float2 empty_point = ui_test_box_center(empty_child);
    BUSTER_TEST(arguments, !disjoint_child->visible && !empty_child->visible);

    UI_EventList press_events = {0};
    UI_Event disjoint_press = {.kind = UI_EventKind_Press, .key = WM_KEY_MOUSE_LEFT, .pos = disjoint_point};
    UI_Event empty_press = {.kind = UI_EventKind_Press, .key = WM_KEY_MOUSE_LEFT, .pos = empty_point};
    ui_event_list_push(arguments->arena, &press_events, &disjoint_press);
    ui_event_list_push(arguments->arena, &press_events, &empty_press);
    ui_test_frame(state, arguments->arena, press_events, 0.016);
    ui_test_build_clipped_hit_tree(&disjoint_child, &empty_child);
    UI_Signal disjoint_press_signal = ui_signal_from_box(disjoint_child);
    UI_Signal empty_press_signal = ui_signal_from_box(empty_child);
    ui_build_end();
    BUSTER_TEST(arguments, !ui_pressed(disjoint_press_signal) && !ui_pressed(empty_press_signal) && state->events.count == 2);

    UI_EventList hover_events = ui_test_single_event(arguments->arena, UI_EventKind_MouseMove, WM_KEY_NULL, disjoint_point, float2_make(0, 0), S8(""));
    ui_test_frame(state, arguments->arena, hover_events, 0.016);
    ui_test_build_clipped_hit_tree(&disjoint_child, &empty_child);
    disjoint_press_signal = ui_signal_from_box(disjoint_child);
    empty_press_signal = ui_signal_from_box(empty_child);
    ui_build_end();
    BUSTER_TEST(arguments, !ui_mouse_over(disjoint_press_signal) && !ui_mouse_over(empty_press_signal));

    UI_EventList scroll_events = ui_test_single_event(arguments->arena, UI_EventKind_Scroll, WM_KEY_MOUSE_WHEEL_UP, disjoint_point,
                                                       float2_make(0.0f, 1.0f), S8(""));
    ui_test_frame(state, arguments->arena, scroll_events, 0.016);
    ui_test_build_clipped_hit_tree(&disjoint_child, &empty_child);
    UI_Signal disjoint_scroll_signal = ui_signal_from_box(disjoint_child);
    UI_Signal empty_scroll_signal = ui_signal_from_box(empty_child);
    ui_build_end();
    BUSTER_TEST(arguments, !ui_scrolled(disjoint_scroll_signal) && !ui_scrolled(empty_scroll_signal) && state->events.count == 1);

    char8 path_data[] = "/tmp/clipped-drop";
    String8 path = {.pointer = path_data, .length = BUSTER_ARRAY_LENGTH(path_data) - 1};
    String8 paths[] = {path};
    UI_EventList drop_events = {0};
    UI_Event drop_event = {
        .kind = UI_EventKind_FileDrop,
        .pos = empty_point,
        .paths = {.pointer = paths, .length = BUSTER_ARRAY_LENGTH(paths)},
    };
    ui_event_list_push(arguments->arena, &drop_events, &drop_event);
    ui_test_frame(state, arguments->arena, drop_events, 0.016);
    ui_test_build_clipped_hit_tree(&disjoint_child, &empty_child);
    disjoint_press_signal = ui_signal_from_box(disjoint_child);
    UI_Signal empty_drop_signal = ui_signal_from_box(empty_child);
    ui_build_end();
    BUSTER_TEST(arguments, !ui_dropped(disjoint_press_signal) && !ui_dropped(empty_drop_signal) && state->events.count == 1);
    ui_state_deinitialize(state);
#undef result
    result->succeeded_test_count += result_local.succeeded_test_count;
    result->test_count += result_local.test_count;
}

BUSTER_GLOBAL_LOCAL UI_Box* ui_test_build_disabled_tree(UI_BoxFlags descendant_flags)
{
    ui_set_next_fixed_x(12.0f);
    ui_set_next_fixed_y(12.0f);
    ui_set_next_fixed_width(180.0f);
    ui_set_next_fixed_height(100.0f);
    UI_Box* parent = ui_box_make(UI_BoxFlag_FloatingX | UI_BoxFlag_FloatingY | UI_BoxFlag_Clip | UI_BoxFlag_Disabled, S8("disabled_parent"));
    ui_push_parent(parent);
    ui_set_next_fixed_width(160.0f);
    ui_set_next_fixed_height(30.0f);
    UI_Box* descendant = ui_box_make(descendant_flags, S8("disabled_descendant"));
    ui_push_parent(descendant);
    ui_set_next_fixed_width(160.0f);
    ui_set_next_fixed_height(120.0f);
    ui_box_make(UI_BoxFlag_DrawBackground, S8("disabled_content"));
    BUSTER_UNUSED(ui_pop_parent());
    BUSTER_UNUSED(ui_pop_parent());
    return descendant;
}

BUSTER_GLOBAL_LOCAL UI_Box* ui_test_build_overlay_box(String8 string)
{
    ui_set_next_fixed_x(32.0f);
    ui_set_next_fixed_y(32.0f);
    ui_set_next_fixed_width(120.0f);
    ui_set_next_fixed_height(30.0f);
    return ui_box_make(UI_BoxFlag_FloatingX | UI_BoxFlag_FloatingY | UI_BoxFlag_MouseClickable | UI_BoxFlag_ClickToFocus | UI_BoxFlag_FocusHot,
                       string);
}

BUSTER_GLOBAL_LOCAL void ui_test_first_frame_and_overlay_ownership(UnitTestArguments* arguments, UnitTestResult* result)
{
    BUSTER_UNUSED(arguments);
    UnitTestResult result_local = {0};
#define result result_local
    UI_State* state = ui_state_allocate(0, 0);
    float2 point = float2_make(80.0f, 47.0f);
    UI_EventList events = ui_test_single_event(arguments->arena, UI_EventKind_Press, WM_KEY_MOUSE_LEFT, point, float2_make(0, 0), S8(""));
    ui_test_frame(state, arguments->arena, events, 0.016);
    UI_Box* first_underlying = ui_test_build_overlay_box(S8("first_underlying"));
    UI_Box* first_overlay = ui_test_build_overlay_box(S8("first_overlay"));
    UI_Signal first_underlying_signal = ui_signal_from_box(first_underlying);
    UI_Signal first_overlay_signal = ui_signal_from_box(first_overlay);
    ui_build_end();
    BUSTER_TEST(arguments, !ui_pressed(first_underlying_signal) && !ui_pressed(first_overlay_signal) && state->events.count == 1);
    BUSTER_TEST(arguments, !state->pointer_targets_assigned && ui_key_match(state->hot_box_key, ui_key_zero()));
    ui_state_deinitialize(state);

    state = ui_state_allocate(0, 0);
    ui_test_frame(state, arguments->arena, (UI_EventList){0}, 0.016);
    UI_Box* underlying = ui_test_build_overlay_box(S8("stable_underlying"));
    ui_build_end();
    UI_Key underlying_key = underlying->key;

    events = ui_test_single_event(arguments->arena, UI_EventKind_Press, WM_KEY_MOUSE_LEFT, point, float2_make(0, 0), S8(""));
    ui_test_frame(state, arguments->arena, events, 0.016);
    UI_Box* newly_introduced_overlay = ui_test_build_overlay_box(S8("new_overlay"));
    UI_Signal overlay_signal = ui_signal_from_box(newly_introduced_overlay);
    underlying = ui_test_build_overlay_box(S8("stable_underlying"));
    UI_Signal underlying_signal = ui_signal_from_box(underlying);
    ui_build_end();
    BUSTER_TEST(arguments, !ui_pressed(overlay_signal) && ui_pressed(underlying_signal) && state->events.count == 0);
    BUSTER_TEST(arguments, ui_key_match(state->active_box_key[UI_MouseButtonKind_Left], underlying_key));

    events = ui_test_single_event(arguments->arena, UI_EventKind_Release, WM_KEY_MOUSE_LEFT, point, float2_make(0, 0), S8(""));
    ui_test_frame(state, arguments->arena, events, 0.016);
    newly_introduced_overlay = ui_test_build_overlay_box(S8("new_overlay"));
    overlay_signal = ui_signal_from_box(newly_introduced_overlay);
    underlying = ui_test_build_overlay_box(S8("stable_underlying"));
    underlying_signal = ui_signal_from_box(underlying);
    ui_build_end();
    BUSTER_TEST(arguments, !ui_clicked(overlay_signal) && ui_clicked(underlying_signal) && state->events.count == 0);
    ui_state_deinitialize(state);

#undef result
    result->succeeded_test_count += result_local.succeeded_test_count;
    result->test_count += result_local.test_count;
}

BUSTER_GLOBAL_LOCAL UI_Box* ui_test_build_click_owner(String8 label, bool clickable, bool disabled)
{
    ui_set_next_fixed_x(32.0f);
    ui_set_next_fixed_y(32.0f);
    ui_set_next_fixed_width(120.0f);
    ui_set_next_fixed_height(30.0f);
    UI_BoxFlags flags = UI_BoxFlag_FloatingX | UI_BoxFlag_FloatingY;
    if (clickable)
    {
        flags |= UI_BoxFlag_MouseClickable;
    }
    if (disabled)
    {
        flags |= UI_BoxFlag_Disabled;
    }
    return ui_box_make(flags, label);
}

BUSTER_GLOBAL_LOCAL UI_Box* ui_test_build_positioned_click_owner(String8 label, f32 x, f32 y)
{
    ui_set_next_fixed_x(x);
    ui_set_next_fixed_y(y);
    ui_set_next_fixed_width(120.0f);
    ui_set_next_fixed_height(30.0f);
    return ui_box_make(UI_BoxFlag_FloatingX | UI_BoxFlag_FloatingY | UI_BoxFlag_MouseClickable | UI_BoxFlag_ClickToFocus | UI_BoxFlag_FocusHot |
                           UI_BoxFlag_FocusActive,
                       label);
}

BUSTER_GLOBAL_LOCAL UI_Box* ui_test_build_positioned_keyboard_click_owner(String8 label, f32 x)
{
    ui_set_next_fixed_x(x);
    ui_set_next_fixed_y(32.0f);
    ui_set_next_fixed_width(120.0f);
    ui_set_next_fixed_height(30.0f);
    return ui_box_make(UI_BoxFlag_FloatingX | UI_BoxFlag_FloatingY | UI_BoxFlag_MouseClickable | UI_BoxFlag_KeyboardClickable | UI_BoxFlag_ClickToFocus |
                           UI_BoxFlag_FocusHot | UI_BoxFlag_FocusActive | UI_BoxFlag_DefaultFocusNavX | UI_BoxFlag_DefaultFocusNavY,
                       label);
}

BUSTER_GLOBAL_LOCAL void ui_test_active_owner_pruning(UnitTestArguments* arguments, UnitTestResult* result)
{
    BUSTER_UNUSED(arguments);
    UnitTestResult result_local = {0};
#define result result_local
    UI_State* state = ui_state_allocate(0, 0);
    float2 point = float2_make(80.0f, 47.0f);
    String8 owner_label = S8("toggle_owner");
    String8 replacement_label = S8("replacement_owner");

    ui_test_frame(state, arguments->arena, (UI_EventList){0}, 0.016);
    UI_Box* owner = ui_test_build_click_owner(owner_label, true, false);
    ui_build_end();
    UI_Key owner_key = owner->key;

    UI_EventList events = ui_test_single_event(arguments->arena, UI_EventKind_Press, WM_KEY_MOUSE_LEFT, point, float2_make(0, 0), S8(""));
    ui_test_frame(state, arguments->arena, events, 0.016);
    owner = ui_test_build_click_owner(owner_label, true, false);
    UI_Signal press = ui_signal_from_box(owner);
    ui_build_end();
    BUSTER_TEST(arguments, ui_pressed(press) && ui_key_match(state->active_box_key[UI_MouseButtonKind_Left], owner_key));

    events = ui_test_single_event(arguments->arena, UI_EventKind_Release, WM_KEY_MOUSE_LEFT, point, float2_make(0, 0), S8(""));
    ui_test_frame(state, arguments->arena, events, 0.016);
    owner = ui_test_build_click_owner(owner_label, false, false);
    UI_Signal nonclickable_release = ui_signal_from_box(owner);
    UI_Box* replacement = ui_test_build_click_owner(replacement_label, true, false);
    UI_Signal replacement_release = ui_signal_from_box(replacement);
    BUSTER_TEST(arguments, !ui_released(nonclickable_release) && !ui_clicked(replacement_release) && state->events.count == 1);
    ui_build_end();
    BUSTER_TEST(arguments, ui_key_match(state->active_box_key[UI_MouseButtonKind_Left], ui_key_zero()) &&
                               !(owner->state_flags & UI_BoxState_Active) && !(replacement->state_flags & UI_BoxState_Active));

    ui_test_frame(state, arguments->arena, (UI_EventList){0}, 0.016);
    ui_test_build_click_owner(owner_label, true, false);
    ui_build_end();
    events = ui_test_single_event(arguments->arena, UI_EventKind_Press, WM_KEY_MOUSE_LEFT, point, float2_make(0, 0), S8(""));
    ui_test_frame(state, arguments->arena, events, 0.016);
    owner = ui_test_build_click_owner(owner_label, true, false);
    press = ui_signal_from_box(owner);
    ui_build_end();
    BUSTER_TEST(arguments, ui_pressed(press));

    events = ui_test_single_event(arguments->arena, UI_EventKind_Release, WM_KEY_MOUSE_LEFT, point, float2_make(0, 0), S8(""));
    ui_test_frame(state, arguments->arena, events, 0.016);
    replacement = ui_test_build_click_owner(replacement_label, true, false);
    replacement_release = ui_signal_from_box(replacement);
    BUSTER_TEST(arguments, !ui_clicked(replacement_release) && state->events.count == 1);
    ui_build_end();
    BUSTER_TEST(arguments, ui_key_match(state->active_box_key[UI_MouseButtonKind_Left], ui_key_zero()));

    ui_test_frame(state, arguments->arena, (UI_EventList){0}, 0.016);
    ui_test_build_click_owner(owner_label, true, false);
    ui_build_end();
    events = ui_test_single_event(arguments->arena, UI_EventKind_Press, WM_KEY_MOUSE_LEFT, point, float2_make(0, 0), S8(""));
    ui_test_frame(state, arguments->arena, events, 0.016);
    owner = ui_test_build_click_owner(owner_label, true, false);
    press = ui_signal_from_box(owner);
    ui_build_end();
    BUSTER_TEST(arguments, ui_pressed(press));

    events = ui_test_single_event(arguments->arena, UI_EventKind_Release, WM_KEY_MOUSE_LEFT, point, float2_make(0, 0), S8(""));
    ui_test_frame(state, arguments->arena, events, 0.016);
    owner = ui_test_build_click_owner(owner_label, true, true);
    UI_Signal disabled_release = ui_signal_from_box(owner);
    replacement = ui_test_build_click_owner(replacement_label, true, false);
    replacement_release = ui_signal_from_box(replacement);
    BUSTER_TEST(arguments, !ui_released(disabled_release) && !ui_clicked(replacement_release) && state->events.count == 1);
    ui_build_end();
    BUSTER_TEST(arguments, ui_key_match(state->active_box_key[UI_MouseButtonKind_Left], ui_key_zero()) &&
                               !(owner->state_flags & UI_BoxState_Active) && !(replacement->state_flags & UI_BoxState_Active));

    ui_test_frame(state, arguments->arena, (UI_EventList){0}, 0.016);
    BUSTER_UNUSED(ui_test_build_click_owner(owner_label, true, false));
    ui_build_end();
    UI_EventList combined_events = {0};
    UI_Event combined_press = {
        .kind = UI_EventKind_Press,
        .key = WM_KEY_MOUSE_LEFT,
        .pos = point,
    };
    UI_Event combined_release = {
        .kind = UI_EventKind_Release,
        .key = WM_KEY_MOUSE_LEFT,
        .pos = point,
    };
    ui_event_list_push(arguments->arena, &combined_events, &combined_press);
    ui_event_list_push(arguments->arena, &combined_events, &combined_release);
    ui_test_frame(state, arguments->arena, combined_events, 0.016);
    owner = ui_test_build_click_owner(owner_label, true, false);
    UI_Signal combined_signal = ui_signal_from_box(owner);
    ui_build_end();
    BUSTER_TEST(arguments, ui_pressed(combined_signal) && ui_released(combined_signal) && ui_clicked(combined_signal) && state->events.count == 0 &&
                               ui_key_match(state->active_box_key[UI_MouseButtonKind_Left], ui_key_zero()) &&
                               !(owner->state_flags & UI_BoxState_Active));

    ui_test_frame(state, arguments->arena, (UI_EventList){0}, 0.016);
    owner = ui_test_build_click_owner(owner_label, true, false);
    UI_Signal following_signal = ui_signal_from_box(owner);
    ui_build_end();
    BUSTER_TEST(arguments, !ui_dragging(following_signal) && ui_key_match(state->active_box_key[UI_MouseButtonKind_Left], ui_key_zero()));

    ui_state_deinitialize(state);
#undef result
    result->succeeded_test_count += result_local.succeeded_test_count;
    result->test_count += result_local.test_count;
}

BUSTER_GLOBAL_LOCAL void ui_test_chronological_cross_box_ownership(UnitTestArguments* arguments, UnitTestResult* result)
{
    BUSTER_UNUSED(arguments);
    UnitTestResult result_local = {0};
#define result result_local
    float2 a_point = float2_make(92.0f, 47.0f);
    float2 b_point = float2_make(252.0f, 47.0f);
    UI_State* state = ui_state_allocate(0, 0);

    ui_test_frame(state, arguments->arena, (UI_EventList){0}, 0.016);
    UI_Box* a = ui_test_build_positioned_click_owner(S8("chronological_click_a"), 32.0f, 32.0f);
    UI_Box* b = ui_test_build_positioned_click_owner(S8("chronological_click_b"), 192.0f, 32.0f);
    ui_build_end();
    BUSTER_UNUSED(a);
    BUSTER_UNUSED(b);

    UI_EventList events = {0};
    UI_Event press_b = {.kind = UI_EventKind_Press, .key = WM_KEY_MOUSE_LEFT, .pos = b_point};
    UI_Event release_b = {.kind = UI_EventKind_Release, .key = WM_KEY_MOUSE_LEFT, .pos = b_point};
    UI_Event press_a = {.kind = UI_EventKind_Press, .key = WM_KEY_MOUSE_LEFT, .pos = a_point};
    ui_event_list_push(arguments->arena, &events, &press_b);
    ui_event_list_push(arguments->arena, &events, &release_b);
    ui_event_list_push(arguments->arena, &events, &press_a);
    ui_test_frame(state, arguments->arena, events, 0.016);
    a = ui_test_build_positioned_click_owner(S8("chronological_click_a"), 32.0f, 32.0f);
    UI_Signal a_signal = ui_signal_from_box(a);
    b = ui_test_build_positioned_click_owner(S8("chronological_click_b"), 192.0f, 32.0f);
    UI_Signal b_signal = ui_signal_from_box(b);
    ui_build_end();
    BUSTER_TEST(arguments, ui_pressed(a_signal) && !ui_released(a_signal) && ui_pressed(b_signal) && ui_released(b_signal) && ui_clicked(b_signal) &&
                               ui_focus_changed(a_signal) && ui_focus_changed(b_signal) && state->events.count == 0 &&
                               ui_key_match(state->active_box_key[UI_MouseButtonKind_Left], a->key) && ui_key_match(state->focus_active_key, a->key));

    events = ui_test_single_event(arguments->arena, UI_EventKind_Release, WM_KEY_MOUSE_LEFT, a_point, float2_make(0, 0), S8(""));
    ui_test_frame(state, arguments->arena, events, 0.016);
    b = ui_test_build_positioned_click_owner(S8("chronological_click_b"), 192.0f, 32.0f);
    UI_Signal b_release = ui_signal_from_box(b);
    a = ui_test_build_positioned_click_owner(S8("chronological_click_a"), 32.0f, 32.0f);
    UI_Signal a_release = ui_signal_from_box(a);
    ui_build_end();
    BUSTER_TEST(arguments, !ui_released(b_release) && !ui_clicked(b_release) && ui_released(a_release) && ui_clicked(a_release) && state->events.count == 0 &&
                               ui_key_match(state->active_box_key[UI_MouseButtonKind_Left], ui_key_zero()) && ui_key_match(state->focus_active_key, a->key));
    ui_state_deinitialize(state);

    state = ui_state_allocate(0, 0);
    UI_TextEditState editor_a_state = {0};
    UI_TextEditState editor_b_state = {0};
    char8 editor_a_data[32] = "a";
    char8 editor_b_data[32] = "b";
    String8 editor_a_value = {.pointer = editor_a_data, .length = 1};
    String8 editor_b_value = {.pointer = editor_b_data, .length = 1};

    ui_test_frame(state, arguments->arena, (UI_EventList){0}, 0.016);
    ui_set_next_fixed_x(32.0f);
    ui_set_next_fixed_y(32.0f);
    ui_set_next_fixed_width(120.0f);
    ui_set_next_fixed_height(30.0f);
    UI_TextEditResult editor_a = ui_text_edit(&editor_a_state, S8("chronological_editor_a"), &editor_a_value, BUSTER_ARRAY_LENGTH(editor_a_data));
    ui_set_next_fixed_x(192.0f);
    ui_set_next_fixed_y(32.0f);
    ui_set_next_fixed_width(120.0f);
    ui_set_next_fixed_height(30.0f);
    UI_TextEditResult editor_b = ui_text_edit(&editor_b_state, S8("chronological_editor_b"), &editor_b_value, BUSTER_ARRAY_LENGTH(editor_b_data));
    ui_build_end();
    float2 editor_a_point = ui_test_box_center(editor_a.widget.box);
    float2 editor_b_point = ui_test_box_center(editor_b.widget.box);

    events = (UI_EventList){0};
    press_b = (UI_Event){.kind = UI_EventKind_Press, .key = WM_KEY_MOUSE_LEFT, .pos = editor_b_point};
    UI_Event text_b = {.kind = UI_EventKind_Text, .key = WM_KEY_NULL, .pos = editor_b_point, .string = S8("x")};
    press_a = (UI_Event){.kind = UI_EventKind_Press, .key = WM_KEY_MOUSE_LEFT, .pos = editor_a_point};
    UI_Event text_a = {.kind = UI_EventKind_Text, .key = WM_KEY_NULL, .pos = editor_a_point, .string = S8("y")};
    ui_event_list_push(arguments->arena, &events, &press_b);
    ui_event_list_push(arguments->arena, &events, &text_b);
    ui_event_list_push(arguments->arena, &events, &press_a);
    ui_event_list_push(arguments->arena, &events, &text_a);
    ui_test_frame(state, arguments->arena, events, 0.016);
    ui_set_next_fixed_x(32.0f);
    ui_set_next_fixed_y(32.0f);
    ui_set_next_fixed_width(120.0f);
    ui_set_next_fixed_height(30.0f);
    editor_a = ui_text_edit(&editor_a_state, S8("chronological_editor_a"), &editor_a_value, BUSTER_ARRAY_LENGTH(editor_a_data));
    ui_set_next_fixed_x(192.0f);
    ui_set_next_fixed_y(32.0f);
    ui_set_next_fixed_width(120.0f);
    ui_set_next_fixed_height(30.0f);
    editor_b = ui_text_edit(&editor_b_state, S8("chronological_editor_b"), &editor_b_value, BUSTER_ARRAY_LENGTH(editor_b_data));
    ui_build_end();
    BUSTER_TEST(arguments, string_equal(editor_a.value, S8("ay")) && string_equal(editor_b.value, S8("bx")) && editor_a.changed && editor_b.changed &&
                               ui_key_match(state->focus_active_key, editor_a.widget.box->key) && ui_key_match(state->focus_edit_key, editor_a.widget.box->key) &&
                               state->events.count == 0);
    ui_state_deinitialize(state);

#undef result
    result->succeeded_test_count += result_local.succeeded_test_count;
    result->test_count += result_local.test_count;
}

BUSTER_GLOBAL_LOCAL void ui_test_prebuild_event_chronology(UnitTestArguments* arguments, UnitTestResult* result)
{
    BUSTER_UNUSED(arguments);
    UnitTestResult result_local = {0};
#define result result_local

    UI_State* state = ui_state_allocate(0, 0);
    UI_TextEditState editor_a_state = {0};
    UI_TextEditState editor_b_state = {0};
    char8 editor_a_data[32] = "a";
    char8 editor_b_data[32] = "b";
    String8 editor_a_value = {.pointer = editor_a_data, .length = 1};
    String8 editor_b_value = {.pointer = editor_b_data, .length = 1};

    ui_test_frame(state, arguments->arena, (UI_EventList){0}, 0.016);
    UI_TextEditResult editor_a = ui_text_edit(&editor_a_state, S8("prebuild_editor_a"), &editor_a_value, BUSTER_ARRAY_LENGTH(editor_a_data));
    UI_TextEditResult editor_b = ui_text_edit(&editor_b_state, S8("prebuild_editor_b"), &editor_b_value, BUSTER_ARRAY_LENGTH(editor_b_data));
    ui_build_end();
    UI_Key editor_a_key = editor_a.widget.box->key;
    UI_Key editor_b_key = editor_b.widget.box->key;
    state->focus_hot_key = editor_a_key;
    state->focus_active_key = editor_a_key;
    state->focus_edit_key = editor_a_key;

    UI_EventList keyboard_events = {0};
    UI_Event tab = {.kind = UI_EventKind_Press, .key = WM_KEY_TAB};
    UI_Event text = {.kind = UI_EventKind_Text, .key = WM_KEY_NULL, .string = S8("x")};
    ui_event_list_push(arguments->arena, &keyboard_events, &tab);
    ui_event_list_push(arguments->arena, &keyboard_events, &text);
    ui_test_frame(state, arguments->arena, keyboard_events, 0.016);
    // Reverse construction order. The Tab target and the following Text
    // owner must still come from the previous completed tree and event order.
    editor_b = ui_text_edit(&editor_b_state, S8("prebuild_editor_b"), &editor_b_value, BUSTER_ARRAY_LENGTH(editor_b_data));
    editor_a = ui_text_edit(&editor_a_state, S8("prebuild_editor_a"), &editor_a_value, BUSTER_ARRAY_LENGTH(editor_a_data));
    ui_build_end();
    BUSTER_TEST(arguments, string_equal(editor_a.value, S8("a")) && string_equal(editor_b.value, S8("bx")) && editor_b.changed &&
                               ui_key_match(state->focus_active_key, editor_b_key) && ui_key_match(state->focus_edit_key, editor_b_key) && state->events.count == 0);
    BUSTER_UNUSED(editor_a_key);
    ui_state_deinitialize(state);

    state = ui_state_allocate(0, 0);
    float2 a_point = float2_make(82.0f, 44.0f);
    float2 b_point = float2_make(242.0f, 44.0f);
    ui_test_frame(state, arguments->arena, (UI_EventList){0}, 0.016);
    UI_Box* a = ui_test_build_positioned_fastpath_box(S8("prebuild_fastpath_a"), 32.0f);
    UI_Box* b = ui_test_build_positioned_fastpath_box(S8("prebuild_fastpath_b"), 192.0f);
    ui_build_end();
    BUSTER_UNUSED(a);
    BUSTER_UNUSED(b);

    ui_test_frame(state, arguments->arena, ui_test_single_event(arguments->arena, UI_EventKind_MouseMove, WM_KEY_NULL, a_point, float2_make(0, 0), S8("")), 0.016);
    a = ui_test_build_positioned_fastpath_box(S8("prebuild_fastpath_a"), 32.0f);
    b = ui_test_build_positioned_fastpath_box(S8("prebuild_fastpath_b"), 192.0f);
    ui_build_end();
    UI_Key fastpath_a_key = a->key;
    UI_Key fastpath_b_key = b->key;

    UI_EventList pointer_events = {0};
    text = (UI_Event){.kind = UI_EventKind_Text, .key = WM_KEY_NULL, .pos = a_point, .string = S8("x")};
    UI_Event move_b = {.kind = UI_EventKind_MouseMove, .key = WM_KEY_NULL, .pos = b_point};
    ui_event_list_push(arguments->arena, &pointer_events, &text);
    ui_event_list_push(arguments->arena, &pointer_events, &move_b);
    ui_test_frame(state, arguments->arena, pointer_events, 0.016);
    b = ui_test_build_positioned_fastpath_box(S8("prebuild_fastpath_b"), 192.0f);
    BUSTER_TEST(arguments, state->events.first && state->events.first->v.kind == UI_EventKind_Text && state->events.first->v.owner_assigned &&
                               state->events.first->v.owner_key == fastpath_a_key.value);
    UI_Signal b_signal = ui_signal_from_box(b);
    a = ui_test_build_positioned_fastpath_box(S8("prebuild_fastpath_a"), 32.0f);
    UI_Signal a_signal = ui_signal_from_box(a);
    ui_build_end();
    BUSTER_TEST(arguments, ui_clicked(a_signal));
    BUSTER_TEST(arguments, !ui_clicked(b_signal));
    BUSTER_TEST(arguments, ui_key_match(state->focus_hot_key, fastpath_b_key));
    BUSTER_TEST(arguments, ui_key_match(a->key, fastpath_a_key) && ui_key_match(b->key, fastpath_b_key));
    BUSTER_TEST(arguments, float2_element(state->mouse, AXIS2_X) == float2_element(b_point, AXIS2_X) && state->events.count == 1);
    ui_state_deinitialize(state);

#undef result
    result->succeeded_test_count += result_local.succeeded_test_count;
    result->test_count += result_local.test_count;
}

BUSTER_GLOBAL_LOCAL void ui_test_mixed_keyboard_activation_case(UnitTestArguments* arguments, UnitTestResult* output, WmKey activation_key, bool follow_tab)
{
    BUSTER_UNUSED(arguments);
    UnitTestResult result_local = {0};
#define result result_local

    UI_State* state = ui_state_allocate(0, 0);
    ui_test_frame(state, arguments->arena, (UI_EventList){0}, 0.016);
    UI_Box* a = ui_test_build_positioned_keyboard_click_owner(S8("mixed_keyboard_a"), 32.0f);
    UI_Box* b = ui_test_build_positioned_keyboard_click_owner(S8("mixed_keyboard_b"), 192.0f);
    ui_build_end();
    UI_Key a_key = a->key;
    UI_Key b_key = b->key;
    float2 b_point = ui_test_box_center(b);
    state->focus_hot_key = a_key;
    state->focus_active_key = a_key;

    UI_EventList events = {0};
    UI_Event activation = {.kind = UI_EventKind_Press, .key = activation_key};
    ui_event_list_push(arguments->arena, &events, &activation);
    if (follow_tab)
    {
        UI_Event tab = {.kind = UI_EventKind_Press, .key = WM_KEY_TAB};
        ui_event_list_push(arguments->arena, &events, &tab);
    }
    else
    {
        UI_Event mouse_press = {.kind = UI_EventKind_Press, .key = WM_KEY_MOUSE_LEFT, .pos = b_point};
        ui_event_list_push(arguments->arena, &events, &mouse_press);
    }

    ui_test_frame(state, arguments->arena, events, 0.016);
    // Reverse construction order so signal delivery cannot repair an
    // activation by relying on current build order or final focus.
    b = ui_test_build_positioned_keyboard_click_owner(S8("mixed_keyboard_b"), 192.0f);
    UI_Signal b_signal = ui_signal_from_box(b);
    a = ui_test_build_positioned_keyboard_click_owner(S8("mixed_keyboard_a"), 32.0f);
    UI_Signal a_signal = ui_signal_from_box(a);
    u32 clicked_count = (ui_clicked(a_signal) ? 1u : 0u) + (ui_clicked(b_signal) ? 1u : 0u);
    u32 pressed_count = (ui_pressed(a_signal) ? 1u : 0u) + (ui_pressed(b_signal) ? 1u : 0u);
    BUSTER_TEST(arguments, ui_clicked(a_signal) && !ui_clicked(b_signal) && clicked_count == 1);
    BUSTER_TEST(arguments, ui_pressed(a_signal) && pressed_count == (follow_tab ? 1u : 2u));
    BUSTER_TEST(arguments, !follow_tab || !ui_pressed(b_signal));
    BUSTER_TEST(arguments, state->events.count == 0 && ui_key_match(state->focus_active_key, b_key));
    if (follow_tab)
    {
        BUSTER_TEST(arguments, ui_key_match(state->active_box_key[UI_MouseButtonKind_Left], ui_key_zero()));
    }
    else
    {
        BUSTER_TEST(arguments, ui_key_match(state->active_box_key[UI_MouseButtonKind_Left], b_key));
    }
    ui_build_end();
    ui_state_deinitialize(state);

#undef result
    output->succeeded_test_count += result_local.succeeded_test_count;
    output->test_count += result_local.test_count;
}

BUSTER_GLOBAL_LOCAL void ui_test_mixed_keyboard_activation(UnitTestArguments* arguments, UnitTestResult* result)
{
    ui_test_mixed_keyboard_activation_case(arguments, result, WM_KEY_RETURN, true);
    ui_test_mixed_keyboard_activation_case(arguments, result, WM_KEY_SPACE, true);
    ui_test_mixed_keyboard_activation_case(arguments, result, WM_KEY_RETURN, false);
    ui_test_mixed_keyboard_activation_case(arguments, result, WM_KEY_SPACE, false);
}

BUSTER_GLOBAL_LOCAL UI_Box* ui_test_build_drop_site(void)
{
    ui_set_next_fixed_x(16.0f);
    ui_set_next_fixed_y(16.0f);
    ui_set_next_fixed_width(180.0f);
    ui_set_next_fixed_height(40.0f);
    return ui_box_make(UI_BoxFlag_FloatingX | UI_BoxFlag_FloatingY | UI_BoxFlag_DropSite, S8("drop_lifetime_site"));
}

BUSTER_GLOBAL_LOCAL void ui_test_ordered_focus_and_text_ownership(UnitTestArguments* arguments, UnitTestResult* result)
{
    BUSTER_UNUSED(arguments);
    UnitTestResult result_local = {0};
#define result result_local
    UI_State* state = ui_state_allocate(0, 0);
    UI_TextEditState editor_a_state = {0};
    UI_TextEditState editor_b_state = {0};
    char8 editor_a_data[32] = "a";
    char8 editor_b_data[32] = "b";
    String8 editor_a_value = {.pointer = editor_a_data, .length = 1};
    String8 editor_b_value = {.pointer = editor_b_data, .length = 1};

    ui_test_frame(state, arguments->arena, (UI_EventList){0}, 0.016);
    UI_TextEditResult editor_a = {0};
    BUSTER_UNUSED(ui_text_edit(&editor_a_state, S8("editor_a"), &editor_a_value, BUSTER_ARRAY_LENGTH(editor_a_data)));
    UI_TextEditResult editor_b = ui_text_edit(&editor_b_state, S8("editor_b"), &editor_b_value, BUSTER_ARRAY_LENGTH(editor_b_data));
    ui_build_end();
    float2 editor_b_point = ui_test_box_center(editor_b.widget.box);

    UI_EventList events = {0};
    UI_Event press = {
        .kind = UI_EventKind_Press,
        .key = WM_KEY_MOUSE_LEFT,
        .pos = editor_b_point,
    };
    UI_Event text = {
        .kind = UI_EventKind_Text,
        .key = WM_KEY_NULL,
        .pos = editor_b_point,
        .string = S8("x"),
    };
    ui_event_list_push(arguments->arena, &events, &press);
    ui_event_list_push(arguments->arena, &events, &text);
    ui_test_frame(state, arguments->arena, events, 0.016);
    editor_a = ui_text_edit(&editor_a_state, S8("editor_a"), &editor_a_value, BUSTER_ARRAY_LENGTH(editor_a_data));
    editor_b = ui_text_edit(&editor_b_state, S8("editor_b"), &editor_b_value, BUSTER_ARRAY_LENGTH(editor_b_data));
    ui_build_end();
    BUSTER_TEST(arguments, string_equal(editor_a.value, S8("a")) && string_equal(editor_b.value, S8("bx")) && editor_b.changed &&
                               ui_key_match(state->focus_edit_key, editor_b.widget.box->key) && state->events.count == 0);

    UI_EventList release_events = ui_test_single_event(arguments->arena, UI_EventKind_Release, WM_KEY_MOUSE_LEFT, editor_b_point, float2_make(0, 0), S8(""));
    ui_test_frame(state, arguments->arena, release_events, 0.016);
    editor_a = ui_text_edit(&editor_a_state, S8("editor_a"), &editor_a_value, BUSTER_ARRAY_LENGTH(editor_a_data));
    editor_b = ui_text_edit(&editor_b_state, S8("editor_b"), &editor_b_value, BUSTER_ARRAY_LENGTH(editor_b_data));
    ui_build_end();
    BUSTER_TEST(arguments, ui_key_match(state->active_box_key[UI_MouseButtonKind_Left], ui_key_zero()) && editor_a.widget.box && editor_b.widget.box);

    ui_state_deinitialize(state);
#undef result
    result->succeeded_test_count += result_local.succeeded_test_count;
    result->test_count += result_local.test_count;
}

BUSTER_GLOBAL_LOCAL void ui_test_drop_copy_lifetime_and_capacity(UnitTestArguments* arguments, UnitTestResult* result)
{
    BUSTER_UNUSED(arguments);
    UnitTestResult result_local = {0};
#define result result_local
    UI_State* state = ui_state_allocate(0, 0);
    ui_test_frame(state, arguments->arena, (UI_EventList){0}, 0.016);
    UI_Box* drop_site = ui_test_build_drop_site();
    ui_build_end();
    float2 point = ui_test_box_center(drop_site);

    char8 first_path_bytes[] = "/tmp/first.txt";
    char8 second_path_bytes[] = "/tmp/second.txt";
    String8 source_paths[] = {
        {.pointer = first_path_bytes, .length = BUSTER_ARRAY_LENGTH(first_path_bytes) - 1},
        {.pointer = second_path_bytes, .length = BUSTER_ARRAY_LENGTH(second_path_bytes) - 1},
    };
    UI_EventList events = {0};
    UI_Event event = {
        .kind = UI_EventKind_FileDrop,
        .pos = point,
        .paths = {.pointer = source_paths, .length = BUSTER_ARRAY_LENGTH(source_paths)},
    };
    ui_event_list_push(arguments->arena, &events, &event);
    first_path_bytes[0] = 'X';
    second_path_bytes[0] = 'Y';
    ui_test_frame(state, arguments->arena, events, 0.016);
    drop_site = ui_test_build_drop_site();
    UI_Signal drop_signal = ui_signal_from_box(drop_site);
    ui_build_end();
    BUSTER_TEST(arguments, ui_dropped(drop_signal) && drop_signal.drop_paths.length == 2 && string_equal(drop_signal.drop_paths.pointer[0], S8("/tmp/first.txt")) &&
                               string_equal(drop_signal.drop_paths.pointer[1], S8("/tmp/second.txt")));

    events = ui_test_single_event(arguments->arena, UI_EventKind_FileDrop, WM_KEY_NULL, point, float2_make(0, 0), S8(""));
    UI_EventNode* capacity_node = events.first;
    String8 capacity_paths[] = {
        {.pointer = first_path_bytes, .length = BUSTER_ARRAY_LENGTH(first_path_bytes) - 1},
        {.pointer = second_path_bytes, .length = BUSTER_ARRAY_LENGTH(second_path_bytes) - 1},
    };
    capacity_node->v.paths.pointer = capacity_paths;
    capacity_node->v.paths.length = BUSTER_ARRAY_LENGTH(capacity_paths);
    ui_test_frame(state, arguments->arena, events, 0.016);
    drop_site = ui_test_build_drop_site();
    Arena* drop_build_arena = ui_build_arena();
    u64 drop_saved_position = drop_build_arena->position;
    u64 drop_saved_reserved_size = drop_build_arena->reserved_size;
    drop_build_arena->reserved_size = drop_saved_reserved_size - 1;
    drop_build_arena->position = drop_build_arena->reserved_size - sizeof(String8);
    drop_signal = ui_signal_from_box(drop_site);
    BUSTER_TEST(arguments, ui_dropped(drop_signal) && drop_signal.drop_paths.length == 0);
    drop_build_arena->position = drop_saved_position;
    drop_build_arena->reserved_size = drop_saved_reserved_size;
    ui_build_end();

    String8 invalid_path = {.pointer = 0, .length = 4};
    String8 invalid_paths[] = {invalid_path};
    events = (UI_EventList){0};
    event = (UI_Event){.kind = UI_EventKind_FileDrop, .pos = point, .paths = {.pointer = invalid_paths, .length = 1}};
    ui_event_list_push(arguments->arena, &events, &event);
    ui_test_frame(state, arguments->arena, events, 0.016);
    drop_site = ui_test_build_drop_site();
    drop_signal = ui_signal_from_box(drop_site);
    ui_build_end();
    BUSTER_TEST(arguments, ui_dropped(drop_signal) && drop_signal.drop_paths.length == 0);

    String8 overflow_path = {.pointer = (char8*)1, .length = (u64)-1};
    String8 overflow_paths[] = {overflow_path};
    events = (UI_EventList){0};
    event = (UI_Event){.kind = UI_EventKind_FileDrop, .pos = point, .paths = {.pointer = overflow_paths, .length = 1}};
    ui_event_list_push(arguments->arena, &events, &event);
    ui_test_frame(state, arguments->arena, events, 0.016);
    drop_site = ui_test_build_drop_site();
    drop_signal = ui_signal_from_box(drop_site);
    ui_build_end();
    BUSTER_TEST(arguments, ui_dropped(drop_signal) && drop_signal.drop_paths.length == 0);
    ui_state_deinitialize(state);

#undef result
    result->succeeded_test_count += result_local.succeeded_test_count;
    result->test_count += result_local.test_count;
}

BUSTER_GLOBAL_LOCAL void ui_test_disabled_ownership_and_focus_policy(UnitTestArguments* arguments, UnitTestResult* result)
{
    BUSTER_UNUSED(arguments);
    UnitTestResult result_local = {0};
#define result result_local
    UI_BoxFlags descendant_flags = UI_BoxFlag_MouseClickable | UI_BoxFlag_KeyboardClickable | UI_BoxFlag_DropSite | UI_BoxFlag_ClickToFocus |
                                   UI_BoxFlag_Scroll | UI_BoxFlag_ViewScrollY | UI_BoxFlag_ViewClampY | UI_BoxFlag_FocusHot | UI_BoxFlag_FocusActive;
    UI_State* state = ui_state_allocate(0, 0);
    ui_test_frame(state, arguments->arena, (UI_EventList){0}, 0.016);
    UI_Box* descendant = ui_test_build_disabled_tree(descendant_flags);
    ui_build_end();
    BUSTER_TEST(arguments, descendant && (descendant->flags & UI_BoxFlag_Disabled));
    float2 center = ui_test_box_center(descendant);
    UI_Key descendant_key = descendant->key;

    UI_EventList events = ui_test_single_event(arguments->arena, UI_EventKind_Press, WM_KEY_MOUSE_LEFT, center, float2_make(0, 0), S8(""));
    ui_test_frame(state, arguments->arena, events, 0.016);
    descendant = ui_test_build_disabled_tree(descendant_flags);
    UI_Signal press = ui_signal_from_box(descendant);
    ui_build_end();
    BUSTER_TEST(arguments, !ui_pressed(press) && state->events.count == 1 && ui_key_match(state->active_box_key[UI_MouseButtonKind_Left], ui_key_zero()));

    events = ui_test_single_event(arguments->arena, UI_EventKind_Release, WM_KEY_MOUSE_LEFT, center, float2_make(0, 0), S8(""));
    ui_test_frame(state, arguments->arena, events, 0.016);
    descendant = ui_test_build_disabled_tree(descendant_flags);
    UI_Signal release = ui_signal_from_box(descendant);
    ui_build_end();
    BUSTER_TEST(arguments, !ui_released(release) && state->events.count == 1);

    state->focus_active_key = descendant_key;
    events = ui_test_key_event(arguments->arena, UI_EventKind_Press, WM_KEY_RETURN, 0, center, S8(""));
    ui_test_frame(state, arguments->arena, events, 0.016);
    descendant = ui_test_build_disabled_tree(descendant_flags);
    UI_Signal keyboard = ui_signal_from_box(descendant);
    ui_build_end();
    BUSTER_TEST(arguments, !ui_pressed(keyboard) && state->events.count == 1 && ui_key_match(state->focus_active_key, ui_key_zero()));

    state->focus_edit_key = descendant_key;
    events = ui_test_key_event(arguments->arena, UI_EventKind_Text, WM_KEY_NULL, 0, center, S8("x"));
    ui_test_frame(state, arguments->arena, events, 0.016);
    descendant = ui_test_build_disabled_tree(descendant_flags);
    UI_Signal text = ui_signal_from_box(descendant);
    ui_build_end();
    BUSTER_TEST(arguments, !ui_clicked(text) && state->events.count == 1 && ui_key_match(state->focus_edit_key, ui_key_zero()));

    events = ui_test_single_event(arguments->arena, UI_EventKind_Scroll, WM_KEY_MOUSE_WHEEL_UP, center, float2_make(0, 1), S8(""));
    ui_test_frame(state, arguments->arena, events, 0.016);
    descendant = ui_test_build_disabled_tree(descendant_flags);
    UI_Signal scroll = ui_signal_from_box(descendant);
    ui_build_end();
    float2 disabled_scroll_offset = ui_box_scroll_offset(descendant);
    BUSTER_TEST(arguments, !ui_scrolled(scroll) && float2_element(disabled_scroll_offset, AXIS2_Y) == 0.0f && state->events.count == 1);

    events = ui_test_drop_event(arguments->arena, center, S8("/tmp/disabled.txt"));
    ui_test_frame(state, arguments->arena, events, 0.016);
    descendant = ui_test_build_disabled_tree(descendant_flags);
    UI_Signal drop = ui_signal_from_box(descendant);
    ui_build_end();
    BUSTER_TEST(arguments, !ui_dropped(drop) && drop.drop_paths.length == 0 && state->events.count == 1);
    ui_state_deinitialize(state);

    state = ui_state_allocate(0, 0);
    ui_test_frame(state, arguments->arena, (UI_EventList){0}, 0.016);
    ui_set_next_fixed_x(32.0f);
    ui_set_next_fixed_y(32.0f);
    ui_set_next_fixed_width(120.0f);
    ui_set_next_fixed_height(30.0f);
    UI_Box* underlying = ui_box_make(UI_BoxFlag_FloatingX | UI_BoxFlag_FloatingY | UI_BoxFlag_MouseClickable | UI_BoxFlag_FocusHot, S8("underlying"));
    ui_set_next_fixed_x(32.0f);
    ui_set_next_fixed_y(32.0f);
    ui_set_next_fixed_width(120.0f);
    ui_set_next_fixed_height(30.0f);
    BUSTER_UNUSED(ui_box_make(UI_BoxFlag_FloatingX | UI_BoxFlag_FloatingY | UI_BoxFlag_Disabled, S8("disabled_overlay")));
    ui_build_end();
    center = ui_test_box_center(underlying);
    events = ui_test_single_event(arguments->arena, UI_EventKind_Press, WM_KEY_MOUSE_LEFT, center, float2_make(0, 0), S8(""));
    ui_test_frame(state, arguments->arena, events, 0.016);
    ui_set_next_fixed_x(32.0f);
    ui_set_next_fixed_y(32.0f);
    ui_set_next_fixed_width(120.0f);
    ui_set_next_fixed_height(30.0f);
    underlying = ui_box_make(UI_BoxFlag_FloatingX | UI_BoxFlag_FloatingY | UI_BoxFlag_MouseClickable | UI_BoxFlag_FocusHot, S8("underlying"));
    ui_set_next_fixed_x(32.0f);
    ui_set_next_fixed_y(32.0f);
    ui_set_next_fixed_width(120.0f);
    ui_set_next_fixed_height(30.0f);
    UI_Box* overlay = ui_box_make(UI_BoxFlag_FloatingX | UI_BoxFlag_FloatingY | UI_BoxFlag_Disabled, S8("disabled_overlay"));
    UI_Signal underlying_signal = ui_signal_from_box(underlying);
    UI_Signal overlay_signal = ui_signal_from_box(overlay);
    ui_build_end();
    BUSTER_TEST(arguments, !ui_pressed(underlying_signal) && !ui_pressed(overlay_signal) && state->events.count == 1);
    ui_state_deinitialize(state);

    state = ui_state_allocate(0, 0);
    ui_test_frame(state, arguments->arena, (UI_EventList){0}, 0.016);
    ui_set_next_fixed_width(120.0f);
    ui_set_next_fixed_height(30.0f);
    UI_Box* hot_disabled = ui_box_make(UI_BoxFlag_MouseClickable | UI_BoxFlag_FocusHot | UI_BoxFlag_FocusActive | UI_BoxFlag_FocusHotDisabled, S8("hot_disabled"));
    ui_build_end();
    center = ui_test_box_center(hot_disabled);
    events = ui_test_single_event(arguments->arena, UI_EventKind_MouseMove, WM_KEY_NULL, center, float2_make(0, 0), S8(""));
    ui_test_frame(state, arguments->arena, events, 0.016);
    hot_disabled = ui_box_make(UI_BoxFlag_MouseClickable | UI_BoxFlag_FocusHot | UI_BoxFlag_FocusActive | UI_BoxFlag_FocusHotDisabled, S8("hot_disabled"));
    UI_Signal hot_disabled_signal = ui_signal_from_box(hot_disabled);
    ui_build_end();
    BUSTER_TEST(arguments, !ui_hovering(hot_disabled_signal) && ui_key_match(state->focus_hot_key, ui_key_zero()));
    ui_state_deinitialize(state);

    state = ui_state_allocate(0, 0);
    ui_test_frame(state, arguments->arena, (UI_EventList){0}, 0.016);
    ui_set_next_fixed_width(120.0f);
    ui_set_next_fixed_height(30.0f);
    UI_Box* active_disabled = ui_box_make(UI_BoxFlag_MouseClickable | UI_BoxFlag_ClickToFocus | UI_BoxFlag_FocusHot | UI_BoxFlag_FocusActive |
                                              UI_BoxFlag_FocusActiveDisabled,
                                          S8("active_disabled"));
    ui_build_end();
    center = ui_test_box_center(active_disabled);
    events = ui_test_single_event(arguments->arena, UI_EventKind_Press, WM_KEY_MOUSE_LEFT, center, float2_make(0, 0), S8(""));
    ui_test_frame(state, arguments->arena, events, 0.016);
    active_disabled = ui_box_make(UI_BoxFlag_MouseClickable | UI_BoxFlag_ClickToFocus | UI_BoxFlag_FocusHot | UI_BoxFlag_FocusActive |
                                      UI_BoxFlag_FocusActiveDisabled,
                                  S8("active_disabled"));
    UI_Signal active_disabled_signal = ui_signal_from_box(active_disabled);
    ui_build_end();
    BUSTER_TEST(arguments, ui_pressed(active_disabled_signal) && ui_key_match(state->focus_active_key, ui_key_zero()));
    ui_state_deinitialize(state);

    state = ui_state_allocate(0, 0);
    ui_test_frame(state, arguments->arena, (UI_EventList){0}, 0.016);
    ui_set_next_fixed_x(20.0f);
    ui_set_next_fixed_y(20.0f);
    ui_set_next_fixed_width(200.0f);
    ui_set_next_fixed_height(40.0f);
    UI_Box* outer = ui_box_make(UI_BoxFlag_FloatingX | UI_BoxFlag_FloatingY | UI_BoxFlag_Clip | UI_BoxFlag_Scroll | UI_BoxFlag_ViewScrollY, S8("outer_scroll"));
    ui_push_parent(outer);
    ui_set_next_fixed_width(180.0f);
    ui_set_next_fixed_height(30.0f);
    UI_Box* inner = ui_box_make(UI_BoxFlag_Scroll | UI_BoxFlag_ViewScrollY | UI_BoxFlag_ViewClampY | UI_BoxFlag_AllowOverflowY | UI_BoxFlag_Clip,
                                S8("inner_scroll"));
    ui_push_parent(inner);
    ui_set_next_fixed_width(180.0f);
    ui_set_next_fixed_height(100.0f);
    ui_box_make(UI_BoxFlag_DrawBackground, S8("inner_scroll_content"));
    BUSTER_UNUSED(ui_pop_parent());
    BUSTER_UNUSED(ui_pop_parent());
    ui_signal_from_box(outer);
    ui_build_end();
    center = ui_test_box_center(inner);
    events = ui_test_single_event(arguments->arena, UI_EventKind_Scroll, WM_KEY_MOUSE_WHEEL_UP, center, float2_make(0, 1), S8(""));
    ui_test_frame(state, arguments->arena, events, 0.016);
    ui_set_next_fixed_x(20.0f);
    ui_set_next_fixed_y(20.0f);
    ui_set_next_fixed_width(200.0f);
    ui_set_next_fixed_height(40.0f);
    outer = ui_box_make(UI_BoxFlag_FloatingX | UI_BoxFlag_FloatingY | UI_BoxFlag_Clip | UI_BoxFlag_Scroll | UI_BoxFlag_ViewScrollY, S8("outer_scroll"));
    ui_push_parent(outer);
    ui_set_next_fixed_width(180.0f);
    ui_set_next_fixed_height(30.0f);
    inner = ui_box_make(UI_BoxFlag_Scroll | UI_BoxFlag_ViewScrollY | UI_BoxFlag_ViewClampY | UI_BoxFlag_AllowOverflowY | UI_BoxFlag_Clip,
                        S8("inner_scroll"));
    UI_Signal outer_signal = ui_signal_from_box(outer);
    UI_Signal inner_signal = ui_signal_from_box(inner);
    ui_push_parent(inner);
    ui_set_next_fixed_width(180.0f);
    ui_set_next_fixed_height(100.0f);
    ui_box_make(UI_BoxFlag_DrawBackground, S8("inner_scroll_content"));
    BUSTER_UNUSED(ui_pop_parent());
    BUSTER_UNUSED(ui_pop_parent());
    ui_build_end();
    BUSTER_TEST(arguments, !ui_scrolled(outer_signal) && ui_scrolled(inner_signal) && float2_element(outer->view_off, AXIS2_Y) == 0.0f &&
                               float2_element(inner->view_off, AXIS2_Y) > 0.0f);
    ui_state_deinitialize(state);

#undef result
    result->succeeded_test_count += result_local.succeeded_test_count;
    result->test_count += result_local.test_count;
}

BUSTER_GLOBAL_LOCAL void ui_test_stable_widget_identity_and_state(UnitTestArguments* arguments, UnitTestResult* result)
{
    BUSTER_UNUSED(arguments);
    UnitTestResult result_local = {0};
#define result result_local
    BUSTER_TEST(arguments, ui_key_match(ui_key_from_string(ui_key_zero(), S8("first##same")), ui_key_from_string(ui_key_zero(), S8("second##same"))));
    BUSTER_TEST(arguments, ui_key_match(ui_key_from_string(ui_key_zero(), S8("first###same")), ui_key_from_string(ui_key_zero(), S8("second###same"))));

    UI_State* state = ui_state_allocate(0, 0);
    float2 center = {0};
    ui_test_frame(state, arguments->arena, (UI_EventList){0}, 0.016);
    UI_Signal initial_button = ui_button(S8("Stable##button"));
    ui_build_end();
    UI_Key button_key = initial_button.box->key;

    ui_test_frame(state, arguments->arena, (UI_EventList){0}, 0.016);
    ui_spacer(ui_pixels(0, 1.0f), ui_pixels(0, 1.0f));
    UI_Signal reordered_button = ui_button(S8("Stable##button"));
    ui_build_end();
    BUSTER_TEST(arguments, ui_key_match(button_key, reordered_button.box->key));
    center = ui_test_box_center(reordered_button.box);
    UI_EventList events = ui_test_single_event(arguments->arena, UI_EventKind_Press, WM_KEY_MOUSE_LEFT, center, float2_make(0, 0), S8(""));
    ui_test_frame(state, arguments->arena, events, 0.016);
    ui_spacer(ui_pixels(0, 1.0f), ui_pixels(0, 1.0f));
    UI_Signal button_pressed = ui_button(S8("Stable##button"));
    ui_build_end();
    BUSTER_TEST(arguments, ui_pressed(button_pressed) && ui_key_match(button_key, button_pressed.box->key));
    events = ui_test_single_event(arguments->arena, UI_EventKind_Release, WM_KEY_MOUSE_LEFT, center, float2_make(0, 0), S8(""));
    ui_test_frame(state, arguments->arena, events, 0.016);
    ui_spacer(ui_pixels(0, 1.0f), ui_pixels(0, 1.0f));
    UI_Signal button_released = ui_button(S8("Stable##button"));
    ui_build_end();
    BUSTER_TEST(arguments, ui_clicked(button_released) && ui_key_match(button_key, button_released.box->key));
    ui_state_deinitialize(state);

    state = ui_state_allocate(0, 0);
    char8 editor_memory[64] = "abcdef";
    String8 editor_value = {.pointer = editor_memory, .length = 6};
    UI_TextEditState editor_state = {0};
    ui_test_frame(state, arguments->arena, (UI_EventList){0}, 0.016);
    UI_TextEditResult initial_editor = ui_text_edit(&editor_state, S8("Editor##text"), &editor_value, sizeof(editor_memory) - 1);
    ui_build_end();
    UI_Key editor_key = initial_editor.widget.box->key;
    center = ui_test_box_center(initial_editor.widget.box);
    events = ui_test_single_event(arguments->arena, UI_EventKind_Press, WM_KEY_MOUSE_LEFT, center, float2_make(0, 0), S8(""));
    ui_test_frame(state, arguments->arena, events, 0.016);
    ui_text_edit(&editor_state, S8("Editor##text"), &editor_value, sizeof(editor_memory) - 1);
    ui_build_end();
    BUSTER_TEST(arguments, ui_key_match(state->focus_edit_key, editor_key));
    events = ui_test_key_event(arguments->arena, UI_EventKind_Press, WM_KEY_LEFT, (u8)(1u << WM_MODIFIER_SHIFT), center, S8(""));
    ui_test_frame(state, arguments->arena, events, 0.016);
    UI_TextEditResult selected_editor = ui_text_edit(&editor_state, S8("Editor##text"), &editor_value, sizeof(editor_memory) - 1);
    ui_build_end();
    BUSTER_TEST(arguments, selected_editor.cursor == 5 && selected_editor.mark == 6 && editor_state.selecting);
    ui_test_frame(state, arguments->arena, (UI_EventList){0}, 0.016);
    ui_label(S8("unrelated earlier label"));
    UI_TextEditResult inserted_editor = ui_text_edit(&editor_state, S8("Editor##text"), &editor_value, sizeof(editor_memory) - 1);
    ui_build_end();
    BUSTER_TEST(arguments, ui_key_match(inserted_editor.widget.box->key, editor_key) && inserted_editor.cursor == 5 && inserted_editor.mark == 6);
    events = ui_test_key_event(arguments->arena, UI_EventKind_Press, WM_KEY_RIGHT, (u8)(1u << WM_MODIFIER_SHIFT), ui_test_box_center(inserted_editor.widget.box), S8(""));
    ui_test_frame(state, arguments->arena, events, 0.016);
    UI_TextEditResult reordered_editor = ui_text_edit(&editor_state, S8("Editor##text"), &editor_value, sizeof(editor_memory) - 1);
    ui_build_end();
    BUSTER_TEST(arguments, ui_key_match(reordered_editor.widget.box->key, editor_key) && reordered_editor.cursor == 6 && reordered_editor.mark == 6);
    ui_state_deinitialize(state);

    state = ui_state_allocate(0, 0);
    ui_test_frame(state, arguments->arena, (UI_EventList){0}, 0.016);
    UI_Box* initial_region = ui_scroll_region_begin(S8("Region##scroll"));
    ui_set_next_fixed_height(1000.0f);
    ui_box_make(UI_BoxFlag_DrawBackground, S8("RegionContent##scroll_content"));
    ui_scroll_region_end();
    ui_build_end();
    UI_Key region_key = initial_region->key;
    center = ui_test_box_center(initial_region);
    events = ui_test_single_event(arguments->arena, UI_EventKind_Scroll, WM_KEY_MOUSE_WHEEL_UP, center, float2_make(0, 1), S8(""));
    ui_test_frame(state, arguments->arena, events, 0.016);
    UI_Box* scrolled_region = ui_scroll_region_begin(S8("Region##scroll"));
    ui_set_next_fixed_height(1000.0f);
    ui_box_make(UI_BoxFlag_DrawBackground, S8("RegionContent##scroll_content"));
    ui_scroll_region_end();
    ui_build_end();
    f32 scroll_offset = float2_element(scrolled_region->view_off, AXIS2_Y);
    BUSTER_TEST(arguments, scroll_offset > 0.0f);
    ui_test_frame(state, arguments->arena, (UI_EventList){0}, 0.016);
    ui_label(S8("new earlier sibling"));
    UI_Box* stable_region = ui_scroll_region_begin(S8("Region##scroll"));
    ui_set_next_fixed_height(1000.0f);
    ui_box_make(UI_BoxFlag_DrawBackground, S8("RegionContent##scroll_content"));
    ui_scroll_region_end();
    ui_build_end();
    BUSTER_TEST(arguments, ui_key_match(stable_region->key, region_key) && float2_element(stable_region->view_off, AXIS2_Y) == scroll_offset);
    ui_state_deinitialize(state);

    state = ui_state_allocate(0, 0);
    ui_test_frame(state, arguments->arena, (UI_EventList){0}, 0.016);
    UI_Box* visible_id = ui_box_make(UI_BoxFlag_DrawText, S8("Visible##id"));
    UI_Box* literal_id = ui_box_make(UI_BoxFlag_DisableIDString | UI_BoxFlag_DrawText, S8("a##b"));
    UI_Box* triple_id = ui_box_make(UI_BoxFlag_DrawText, S8("Caption###identity"));
    ui_build_end();
    BUSTER_TEST(arguments, string_equal(visible_id->display_string, S8("Visible")) && string_equal(literal_id->display_string, S8("a##b")) &&
                               string_equal(triple_id->display_string, S8("Caption")));
    BUSTER_TEST(arguments, !ui_key_match(literal_id->key, ui_key_from_string(ui_key_zero(), S8("b"))) &&
                               ui_key_match(triple_id->key, ui_key_from_string(ui_key_zero(), S8("identity"))));
    ui_state_deinitialize(state);

#undef result
    result->succeeded_test_count += result_local.succeeded_test_count;
    result->test_count += result_local.test_count;
}

BUSTER_GLOBAL_LOCAL void ui_test_text_edit_atomic_policy(UnitTestArguments* arguments, UnitTestResult* result)
{
    BUSTER_UNUSED(arguments);
    UnitTestResult result_local = {0};
#define result result_local
    UI_State* state = ui_state_allocate(0, 0);
    char8 memory[16] = "cat";
    String8 value = {.pointer = memory, .length = 3};
    UI_TextEditState edit = {0};
    ui_test_frame(state, arguments->arena, (UI_EventList){0}, 0.016);
    UI_TextEditResult initial = ui_text_edit(&edit, S8("Atomic##text"), &value, 3);
    ui_build_end();
    float2 center = ui_test_box_center(initial.widget.box);
    UI_EventList events = ui_test_single_event(arguments->arena, UI_EventKind_Press, WM_KEY_MOUSE_LEFT, center, float2_make(0, 0), S8(""));
    ui_test_frame(state, arguments->arena, events, 0.016);
    ui_text_edit(&edit, S8("Atomic##text"), &value, 3);
    ui_build_end();

    events = ui_test_key_event(arguments->arena, UI_EventKind_Press, WM_KEY_A, (u8)(1u << WM_MODIFIER_CONTROL), center, S8(""));
    ui_test_frame(state, arguments->arena, events, 0.016);
    UI_TextEditResult selected = ui_text_edit(&edit, S8("Atomic##text"), &value, 3);
    ui_build_end();
    BUSTER_TEST(arguments, selected.cursor == value.length && selected.mark == 0 && selected.widget.box && edit.selecting);

    events = ui_test_key_event(arguments->arena, UI_EventKind_Text, WM_KEY_NULL, 0, center, S8("long"));
    ui_test_frame(state, arguments->arena, events, 0.016);
    UI_TextEditResult failed_replacement = ui_text_edit(&edit, S8("Atomic##text"), &value, 3);
    ui_build_end();
    BUSTER_STRING_TEST(arguments, value, S8("cat"));
    BUSTER_TEST(arguments, !failed_replacement.changed && failed_replacement.cursor == 3 && failed_replacement.mark == 0 && edit.selecting);

    events = ui_test_key_event(arguments->arena, UI_EventKind_Text, WM_KEY_NULL, 0, center, S8("dog"));
    ui_test_frame(state, arguments->arena, events, 0.016);
    UI_TextEditResult successful_replacement = ui_text_edit(&edit, S8("Atomic##text"), &value, 3);
    ui_build_end();
    BUSTER_STRING_TEST(arguments, value, S8("dog"));
    BUSTER_TEST(arguments, successful_replacement.changed && successful_replacement.cursor == 3 && successful_replacement.mark == 3 && !edit.selecting);
    ui_state_deinitialize(state);

#undef result
    result->succeeded_test_count += result_local.succeeded_test_count;
    result->test_count += result_local.test_count;
}

BUSTER_GLOBAL_LOCAL void ui_test_draw_command_capacity(UnitTestArguments* arguments, UnitTestResult* result)
{
    BUSTER_UNUSED(arguments);
    UnitTestResult result_local = {0};
#define result result_local
    UI_State* state = ui_state_allocate(0, 0);
    ui_test_frame(state, arguments->arena, (UI_EventList){0}, 0.016);
    UI_Box* first = 0;
    UI_Box* last = 0;
    for (u64 index = 0; index < 5000; index += 1)
    {
        ui_set_next_fixed_x(4.0f);
        ui_set_next_fixed_y(4.0f);
        ui_set_next_fixed_width(1.0f);
        ui_set_next_fixed_height(1.0f);
        UI_Box* box = ui_box_make_format(UI_BoxFlag_FloatingX | UI_BoxFlag_FloatingY | UI_BoxFlag_DrawBackground, S8("draw_command_{u64}"), index);
        first = first ? first : box;
        last = box;
    }
    ui_build_end();
    ui_draw();
    BUSTER_TEST(arguments, state->draw_commands_complete && state->draw_command_count > 4096);
    BUSTER_TEST(arguments, state->draw_command_count <= state->draw_command_capacity && state->draw_commands[5].box == first);
    BUSTER_TEST(arguments, state->draw_commands[state->draw_command_count - 1].box == last);
    ui_state_deinitialize(state);

#undef result
    result->succeeded_test_count += result_local.succeeded_test_count;
    result->test_count += result_local.test_count;
}

BUSTER_GLOBAL_LOCAL void ui_test_utf8_tooltip_and_draw_commands(UnitTestArguments* arguments, UnitTestResult* result)
{
    BUSTER_UNUSED(arguments);
    UnitTestResult result_local = {0};
#define result result_local
    UI_State* state = ui_state_allocate(0, 0);
    ui_test_frame(state, arguments->arena, (UI_EventList){0}, 0.016);
    ui_set_next_font_size(10.0f);
    ui_set_next_text_padding(0.0f);
    ui_set_next_fixed_width(18.0f);
    ui_set_next_fixed_height(20.0f);
    UI_Box* exact_ascii = ui_box_make(UI_BoxFlag_DrawText, S8("abcd"));
    ui_build_end();
    BUSTER_TEST(arguments, exact_ascii->text_truncated && exact_ascii->text_visible_columns == 0 && exact_ascii->text_visible_length == 0);
    ui_draw();
    BUSTER_TEST(arguments, ui_test_text_command(state, exact_ascii, S8("...")) != 0);

    char8 multibyte_bytes[] = {'a', (char8)0xc3, (char8)0xa9, (char8)0xe7, (char8)0x95, (char8)0x8c, 'b', 'c', 'd'};
    char8 multibyte_prefix_bytes[] = {'a', (char8)0xc3, (char8)0xa9};
    String8 multibyte = {.pointer = multibyte_bytes, .length = BUSTER_ARRAY_LENGTH(multibyte_bytes)};
    String8 multibyte_prefix = {.pointer = multibyte_prefix_bytes, .length = BUSTER_ARRAY_LENGTH(multibyte_prefix_bytes)};
    ui_test_frame(state, arguments->arena, (UI_EventList){0}, 0.016);
    ui_set_next_font_size(10.0f);
    ui_set_next_text_padding(0.0f);
    ui_set_next_fixed_width(30.0f);
    ui_set_next_fixed_height(20.0f);
    UI_Box* exact_multibyte = ui_box_make(UI_BoxFlag_DrawText, multibyte);
    ui_build_end();
    BUSTER_TEST(arguments, exact_multibyte->text_truncated && exact_multibyte->text_visible_columns == 2 && exact_multibyte->text_visible_length == 3);
    ui_draw();
    UI_DrawCommand* multibyte_text = ui_test_text_command(state, exact_multibyte, multibyte_prefix);
    BUSTER_TEST(arguments, multibyte_text != 0 && ui_test_text_command(state, exact_multibyte, S8("...")) != 0);

    char8 invalid_bytes[] = {'a', (char8)0xc3, (char8)0x28, 'b'};
    char8 sanitized_bytes[] = {'a', (char8)0xef, (char8)0xbf, (char8)0xbd, (char8)0x28, 'b'};
    String8 invalid = {.pointer = invalid_bytes, .length = BUSTER_ARRAY_LENGTH(invalid_bytes)};
    String8 sanitized = {.pointer = sanitized_bytes, .length = BUSTER_ARRAY_LENGTH(sanitized_bytes)};
    ui_test_frame(state, arguments->arena, (UI_EventList){0}, 0.016);
    ui_set_next_font_size(10.0f);
    ui_set_next_text_padding(0.0f);
    ui_set_next_fixed_width(100.0f);
    ui_set_next_fixed_height(20.0f);
    UI_Box* safe_text = ui_box_make(UI_BoxFlag_DrawText, invalid);
    ui_build_end();
    BUSTER_STRING_TEST(arguments, safe_text->string, sanitized);
    ui_draw();
    BUSTER_TEST(arguments, ui_test_text_command(state, safe_text, sanitized) != 0);

    ui_test_frame(state, arguments->arena, (UI_EventList){0}, 0.016);
    UI_BoxFlags tooltip_flags = UI_BoxFlag_FloatingX | UI_BoxFlag_FloatingY | UI_BoxFlag_DrawText | UI_BoxFlag_MouseClickable | UI_BoxFlag_ClickToFocus |
                                UI_BoxFlag_FocusHot | UI_BoxFlag_FocusActive;
    UI_Box* tooltip_box = ui_test_build_tooltip_box(tooltip_flags);
    ui_build_end();
    float2 center = ui_test_box_center(tooltip_box);
    UI_EventList events = ui_test_single_event(arguments->arena, UI_EventKind_MouseMove, WM_KEY_NULL, center, float2_make(0, 0), S8(""));
    ui_test_frame(state, arguments->arena, events, 0.016);
    tooltip_box = ui_test_build_tooltip_box(tooltip_flags);
    UI_Signal tooltip_hover = ui_signal_from_box(tooltip_box);
    ui_build_end();
    BUSTER_TEST(arguments, tooltip_box->text_truncated);
    BUSTER_TEST(arguments, ui_box_has_tooltip(tooltip_box));
    BUSTER_TEST(arguments, !!(tooltip_box->state_flags & UI_BoxState_Tooltip));
    BUSTER_TEST(arguments, ui_mouse_over(tooltip_hover));
    ui_draw();
    BUSTER_TEST(arguments, ui_test_text_command(state, tooltip_box, S8("tooltip text")) != 0);
    events = ui_test_single_event(arguments->arena, UI_EventKind_Press, WM_KEY_MOUSE_LEFT, center, float2_make(0, 0), S8(""));
    ui_test_frame(state, arguments->arena, events, 0.016);
    tooltip_box = ui_test_build_tooltip_box(tooltip_flags);
    UI_Signal tooltip_press = ui_signal_from_box(tooltip_box);
    ui_build_end();
    events = ui_test_single_event(arguments->arena, UI_EventKind_Release, WM_KEY_MOUSE_LEFT, center, float2_make(0, 0), S8(""));
    ui_test_frame(state, arguments->arena, events, 0.016);
    tooltip_box = ui_test_build_tooltip_box(tooltip_flags);
    UI_Signal tooltip_click = ui_signal_from_box(tooltip_box);
    ui_build_end();
    BUSTER_TEST(arguments, ui_pressed(tooltip_press) && ui_clicked(tooltip_click));

    UI_BoxFlags no_tooltip_flags = tooltip_flags | UI_BoxFlag_DisableTruncatedHover;
    events = ui_test_single_event(arguments->arena, UI_EventKind_MouseMove, WM_KEY_NULL, center, float2_make(0, 0), S8(""));
    ui_test_frame(state, arguments->arena, events, 0.016);
    tooltip_box = ui_test_build_tooltip_box(no_tooltip_flags);
    UI_Signal no_tooltip_hover = ui_signal_from_box(tooltip_box);
    ui_build_end();
    BUSTER_TEST(arguments, !ui_box_has_tooltip(tooltip_box) && ui_mouse_over(no_tooltip_hover));
    events = ui_test_single_event(arguments->arena, UI_EventKind_Press, WM_KEY_MOUSE_LEFT, center, float2_make(0, 0), S8(""));
    ui_test_frame(state, arguments->arena, events, 0.016);
    tooltip_box = ui_test_build_tooltip_box(no_tooltip_flags);
    UI_Signal no_tooltip_press = ui_signal_from_box(tooltip_box);
    ui_build_end();
    events = ui_test_single_event(arguments->arena, UI_EventKind_Release, WM_KEY_MOUSE_LEFT, center, float2_make(0, 0), S8(""));
    ui_test_frame(state, arguments->arena, events, 0.016);
    tooltip_box = ui_test_build_tooltip_box(no_tooltip_flags);
    UI_Signal no_tooltip_click = ui_signal_from_box(tooltip_box);
    ui_build_end();
    BUSTER_TEST(arguments, ui_pressed(no_tooltip_press) && ui_clicked(no_tooltip_click));

    float2 overlapping_tooltip_point = float2_make(60.0f, 60.0f);
    ui_test_frame(state, arguments->arena,
                  ui_test_single_event(arguments->arena, UI_EventKind_MouseMove, WM_KEY_NULL, overlapping_tooltip_point, float2_make(0, 0), S8("")),
                  0.016);
    ui_set_next_fixed_x(50.0f);
    ui_set_next_fixed_y(50.0f);
    ui_set_next_fixed_width(30.0f);
    ui_set_next_fixed_height(20.0f);
    UI_Box* obscured_tooltip = ui_box_make(UI_BoxFlag_FloatingX | UI_BoxFlag_FloatingY | UI_BoxFlag_DrawText, S8("obscured tooltip"));
    ui_set_next_fixed_x(50.0f);
    ui_set_next_fixed_y(50.0f);
    ui_set_next_fixed_width(30.0f);
    ui_set_next_fixed_height(20.0f);
    UI_Box* top_tooltip = ui_box_make(UI_BoxFlag_FloatingX | UI_BoxFlag_FloatingY | UI_BoxFlag_DrawText | UI_BoxFlag_DrawOverlay, S8("top overlay tooltip"));
    ui_build_end();
    BUSTER_TEST(arguments, !obscured_tooltip->tooltip_visible && !(obscured_tooltip->state_flags & UI_BoxState_Tooltip) && top_tooltip->tooltip_visible &&
                               (top_tooltip->state_flags & UI_BoxState_Tooltip));

    ui_test_frame(state, arguments->arena,
                  ui_test_single_event(arguments->arena, UI_EventKind_MouseMove, WM_KEY_NULL, overlapping_tooltip_point, float2_make(0, 0), S8("")),
                  0.016);
    ui_set_next_fixed_x(50.0f);
    ui_set_next_fixed_y(50.0f);
    ui_set_next_fixed_width(30.0f);
    ui_set_next_fixed_height(20.0f);
    UI_Box* occluded_tooltip = ui_box_make(UI_BoxFlag_FloatingX | UI_BoxFlag_FloatingY | UI_BoxFlag_DrawText, S8("occluded tooltip"));
    ui_set_next_fixed_x(50.0f);
    ui_set_next_fixed_y(50.0f);
    ui_set_next_fixed_width(30.0f);
    ui_set_next_fixed_height(20.0f);
    UI_Box* nontruncated_overlay = ui_box_make(UI_BoxFlag_FloatingX | UI_BoxFlag_FloatingY | UI_BoxFlag_DrawOverlay, S8("x"));
    ui_build_end();
    BUSTER_TEST(arguments, occluded_tooltip->text_truncated && !occluded_tooltip->tooltip_visible &&
                               !(occluded_tooltip->state_flags & UI_BoxState_Tooltip) && !nontruncated_overlay->tooltip_visible);

    char8 long_tooltip_bytes[14000];
    for (u64 index = 0; index < 7000; index += 1)
    {
        long_tooltip_bytes[index * 2] = (char8)0xc3;
        long_tooltip_bytes[index * 2 + 1] = (char8)0xa9;
    }
    String8 long_tooltip_text = {.pointer = long_tooltip_bytes, .length = BUSTER_ARRAY_LENGTH(long_tooltip_bytes)};
    ui_test_frame(state, arguments->arena, ui_test_single_event(arguments->arena, UI_EventKind_MouseMove, WM_KEY_NULL, float2_make(8.0f, 8.0f),
                                                                 float2_make(0, 0), S8("")),
                  0.016);
    ui_set_next_fixed_x(4.0f);
    ui_set_next_fixed_y(4.0f);
    ui_set_next_fixed_width(24.0f);
    ui_set_next_fixed_height(20.0f);
    UI_Box* narrow_tooltip_parent = ui_box_make(UI_BoxFlag_FloatingX | UI_BoxFlag_FloatingY | UI_BoxFlag_Clip | UI_BoxFlag_AllowOverflowX,
                                                S8("narrow_tooltip_parent"));
    ui_push_parent(narrow_tooltip_parent);
    ui_set_next_fixed_width(4000.0f);
    ui_set_next_fixed_height(20.0f);
    UI_Box* long_tooltip = ui_box_make(UI_BoxFlag_DrawText | UI_BoxFlag_AllowOverflowX, long_tooltip_text);
    BUSTER_UNUSED(ui_pop_parent());
    ui_build_end();
    BUSTER_TEST(arguments, long_tooltip->text_truncated && long_tooltip->tooltip_visible && long_tooltip->tooltip_text_truncated && long_tooltip->tooltip_line_count > 1);
    ui_draw();
    u64 tooltip_text_command_count = 0;
    bool tooltip_commands_inside = true;
    bool tooltip_has_continuation = false;
    for (u64 index = 0; index < state->draw_command_count; index += 1)
    {
        UI_DrawCommand* command = &state->draw_commands[index];
        bool is_tooltip_text = command->box == long_tooltip && command->kind == UI_DrawCommandKind_Text &&
                               command->clip_rect.x0 == long_tooltip->tooltip_rect.x0 && command->clip_rect.y0 == long_tooltip->tooltip_rect.y0 &&
                               command->clip_rect.x1 == long_tooltip->tooltip_rect.x1 && command->clip_rect.y1 == long_tooltip->tooltip_rect.y1;
        if (is_tooltip_text)
        {
            tooltip_text_command_count += 1;
            tooltip_commands_inside = tooltip_commands_inside && command->rect.x0 >= command->clip_rect.x0 && command->rect.y0 >= command->clip_rect.y0 &&
                                      command->rect.x1 <= command->clip_rect.x1 && command->rect.y1 <= command->clip_rect.y1;
            if (command->text.length >= 3 && command->text.pointer[command->text.length - 3] == '.' &&
                command->text.pointer[command->text.length - 2] == '.' && command->text.pointer[command->text.length - 1] == '.')
            {
                tooltip_has_continuation = true;
            }
        }
    }
    BUSTER_TEST(arguments, long_tooltip->tooltip_rect.x0 >= state->root->rect.x0 && long_tooltip->tooltip_rect.y0 >= state->root->rect.y0 &&
                               long_tooltip->tooltip_rect.x1 <= state->root->rect.x1 && long_tooltip->tooltip_rect.y1 <= state->root->rect.y1);
    BUSTER_TEST(arguments, tooltip_text_command_count == long_tooltip->tooltip_line_count && tooltip_commands_inside && tooltip_has_continuation);

    UI_FuzzyMatchRange copied_range = {.first = 1, .one_past_last = 3};
    ui_test_frame(state, arguments->arena, (UI_EventList){0}, 0.016);
    ui_set_next_font_size(10.0f);
    ui_set_next_text_padding(0.0f);
    ui_set_next_fixed_width(120.0f);
    ui_set_next_fixed_height(20.0f);
    ui_set_next_text_alignment(UI_TextAlign_Center);
    UI_Box* centered = ui_box_make(UI_BoxFlag_DrawText, S8("abcd"));
    ui_box_set_fuzzy_match_ranges(centered, &copied_range, 1);
    copied_range.first = 0;
    copied_range.one_past_last = 1;
    ui_set_next_font_size(10.0f);
    ui_set_next_text_padding(0.0f);
    ui_set_next_fixed_width(120.0f);
    ui_set_next_fixed_height(20.0f);
    ui_set_next_text_alignment(UI_TextAlign_Right);
    UI_Box* right_aligned = ui_box_make(UI_BoxFlag_DrawText, S8("right##fuzzy"));
    UI_FuzzyMatchRange right_range = {.first = 1, .one_past_last = 3};
    ui_box_set_fuzzy_match_ranges(right_aligned, &right_range, 1);
    UI_Box* invalid_ranges = ui_box_make(UI_BoxFlag_DrawText, S8("invalid fuzzy"));
    UI_FuzzyMatchRange invalid_range = {.first = 3, .one_past_last = 1};
    ui_box_set_fuzzy_match_ranges(invalid_ranges, &invalid_range, 1);
    UI_Box* invalid_fuzzy_source = ui_box_make(UI_BoxFlag_DrawText, S8("invalid fuzzy source"));
    ui_box_set_fuzzy_match_ranges(invalid_fuzzy_source, 0, 1);
    ui_box_set_fuzzy_match_ranges(invalid_fuzzy_source, (UI_FuzzyMatchRange*)1, (u64)-1);
    UI_Box* near_capacity_ranges = ui_box_make(UI_BoxFlag_DrawText, S8("near capacity fuzzy"));
    UI_FuzzyMatchRange near_capacity_range = {.first = 0, .one_past_last = 1};
    Arena* fuzzy_build_arena = ui_build_arena();
    u64 fuzzy_saved_position = fuzzy_build_arena->position;
    u64 fuzzy_saved_reserved_size = fuzzy_build_arena->reserved_size;
    fuzzy_build_arena->reserved_size = fuzzy_saved_reserved_size - 1;
    fuzzy_build_arena->position = fuzzy_build_arena->reserved_size - sizeof(UI_FuzzyMatchRange);
    ui_box_set_fuzzy_match_ranges(near_capacity_ranges, &near_capacity_range, 1);
    BUSTER_TEST(arguments, near_capacity_ranges->fuzzy_match_range_count == 0 && !(near_capacity_ranges->flags & UI_BoxFlag_HasFuzzyMatchRanges));
    fuzzy_build_arena->position = fuzzy_saved_position;
    fuzzy_build_arena->reserved_size = fuzzy_saved_reserved_size;
    ui_build_end();
    BUSTER_TEST(arguments, centered->fuzzy_match_range_count == 1 && centered->fuzzy_match_ranges[0].first == 1 && centered->fuzzy_match_ranges[0].one_past_last == 3);
    BUSTER_TEST(arguments, invalid_ranges->fuzzy_match_range_count == 0 && !(invalid_ranges->flags & UI_BoxFlag_HasFuzzyMatchRanges));
    BUSTER_TEST(arguments, invalid_fuzzy_source->fuzzy_match_range_count == 0 && !(invalid_fuzzy_source->flags & UI_BoxFlag_HasFuzzyMatchRanges));
    ui_draw();
    UI_DrawCommand* centered_highlight = ui_test_highlight_command(state, centered);
    UI_DrawCommand* right_highlight = ui_test_highlight_command(state, right_aligned);
    BUSTER_TEST(arguments, centered_highlight && centered_highlight->rect.x0 > centered->rect.x0 + 30.0f && right_highlight &&
                               right_highlight->rect.x0 > right_aligned->rect.x0 + 70.0f);

    ui_test_frame(state, arguments->arena, (UI_EventList){0}, 0.016);
    ui_set_next_child_layout_axis(AXIS2_X);
    ui_set_next_fixed_width(100.0f);
    ui_set_next_fixed_height(20.0f);
    UI_Box* rounded_parent = ui_box_make(UI_BoxFlag_RoundChildrenByParent | UI_BoxFlag_DrawBucket | UI_BoxFlag_DrawBackground, S8("rounded_bucket"));
    ui_box_set_corner_radii(rounded_parent, 7.0f);
    ui_push_parent(rounded_parent);
    ui_set_next_fixed_width(50.0f);
    ui_set_next_fixed_height(20.0f);
    UI_Box* rounded_first = ui_box_make(UI_BoxFlag_DrawBackground, S8("rounded_first"));
    ui_set_next_fixed_width(50.0f);
    ui_set_next_fixed_height(20.0f);
    UI_Box* rounded_last = ui_box_make(UI_BoxFlag_DrawBackground, S8("rounded_last"));
    BUSTER_UNUSED(ui_pop_parent());
    ui_build_end();
    BUSTER_TEST(arguments, rounded_first->corner_radii[CORNER_00] == 7.0f && rounded_first->corner_radii[CORNER_01] == 7.0f &&
                               rounded_last->corner_radii[CORNER_10] == 7.0f && rounded_last->corner_radii[CORNER_11] == 7.0f);
    ui_draw();
    u64 parent_draw_index = ui_test_draw_command_index(state, rounded_parent, UI_DrawCommandKind_Rect);
    u64 first_draw_index = ui_test_draw_command_index(state, rounded_first, UI_DrawCommandKind_Rect);
    u64 last_draw_index = ui_test_draw_command_index(state, rounded_last, UI_DrawCommandKind_Rect);
    BUSTER_TEST(arguments, parent_draw_index < first_draw_index && first_draw_index < last_draw_index);
    UI_DrawCommand* first_draw = first_draw_index != (u64)-1 ? &state->draw_commands[first_draw_index] : 0;
    UI_DrawCommand* last_draw = last_draw_index != (u64)-1 ? &state->draw_commands[last_draw_index] : 0;
    BUSTER_TEST(arguments, first_draw && float4_element(first_draw->corner_radii, 0) == 7.0f && float4_element(first_draw->corner_radii, 1) == 7.0f &&
                               last_draw && float4_element(last_draw->corner_radii, 2) == 7.0f && float4_element(last_draw->corner_radii, 3) == 7.0f);

    ui_test_frame(state, arguments->arena, (UI_EventList){0}, 0.016);
    ui_set_next_fixed_width(40.0f);
    ui_set_next_fixed_height(20.0f);
    UI_Box* clip_parent = ui_box_make(UI_BoxFlag_Clip | UI_BoxFlag_AllowOverflowX | UI_BoxFlag_DrawBackground, S8("clip_parent"));
    ui_push_parent(clip_parent);
    ui_set_next_fixed_width(100.0f);
    ui_set_next_fixed_height(20.0f);
    UI_Box* clipped_text = ui_box_make(UI_BoxFlag_DrawText | UI_BoxFlag_AllowOverflowX, S8("clipped text"));
    BUSTER_UNUSED(ui_pop_parent());
    ui_build_end();
    ui_draw();
    UI_DrawCommand* clipped_text_command = ui_test_first_draw_command(state, clipped_text, UI_DrawCommandKind_Text);
    BUSTER_TEST(arguments, clipped_text_command != 0);
    BUSTER_TEST(arguments, clipped_text_command && clipped_text_command->clip_rect.x1 <= clip_parent->rect.x1);
    BUSTER_TEST(arguments, clipped_text_command && clipped_text_command->text.length <= 2);

    ui_test_frame(state, arguments->arena, (UI_EventList){0}, 0.016);
    UI_Box* fastpath = ui_test_build_fastpath_box();
    ui_build_end();
    ui_draw();
    BUSTER_TEST(arguments, ui_test_text_command(state, fastpath, S8("exit")) != 0 && (fastpath->state_flags & UI_BoxState_TextFastpath));
    center = ui_test_box_center(fastpath);
    events = ui_test_single_event(arguments->arena, UI_EventKind_MouseMove, WM_KEY_NULL, center, float2_make(0, 0), S8(""));
    ui_test_frame(state, arguments->arena, events, 0.016);
    fastpath = ui_test_build_fastpath_box();
    ui_signal_from_box(fastpath);
    ui_build_end();
    events = ui_test_single_event(arguments->arena, UI_EventKind_Text, WM_KEY_NULL, center, float2_make(0, 0), S8("x"));
    ui_test_frame(state, arguments->arena, events, 0.016);
    fastpath = ui_test_build_fastpath_box();
    UI_Signal fastpath_signal = ui_signal_from_box(fastpath);
    ui_build_end();
    BUSTER_TEST(arguments, ui_clicked(fastpath_signal) && state->events.count == 0);
    ui_state_deinitialize(state);

    state = ui_state_allocate(0, 0);
    ui_test_frame(state, arguments->arena, (UI_EventList){0}, 0.016);
    ui_set_next_fixed_x(0.0f);
    ui_set_next_fixed_y(10.0f);
    ui_set_next_fixed_width(20.0f);
    ui_set_next_fixed_height(20.0f);
    UI_Box* animated = ui_box_make(UI_BoxFlag_FloatingX | UI_BoxFlag_FloatingY | UI_BoxFlag_AnimatePosX, S8("animated"));
    ui_build_end();
    f32 initial_x = animated->rect.x0;
    ui_test_frame(state, arguments->arena, (UI_EventList){0}, 0.016);
    ui_set_next_fixed_x(100.0f);
    ui_set_next_fixed_y(10.0f);
    ui_set_next_fixed_width(20.0f);
    ui_set_next_fixed_height(20.0f);
    animated = ui_box_make(UI_BoxFlag_FloatingX | UI_BoxFlag_FloatingY | UI_BoxFlag_AnimatePosX, S8("animated"));
    ui_build_end();
    BUSTER_TEST(arguments, animated->rect.x0 > initial_x && animated->rect.x0 < 100.0f && float2_element(animated->position_delta, AXIS2_X) < 100.0f);
    ui_state_deinitialize(state);

#undef result
    result->succeeded_test_count += result_local.succeeded_test_count;
    result->test_count += result_local.test_count;
}

BUSTER_GLOBAL_LOCAL void ui_test_build_focus_grid(UI_Box** top_left, UI_Box** top_right, UI_Box** bottom_left, UI_Box** bottom_right)
{
    ui_set_next_fixed_width(200.0f);
    ui_set_next_fixed_height(70.0f);
    UI_Box* scope = ui_box_make(UI_BoxFlag_DefaultFocusNavX | UI_BoxFlag_DefaultFocusNavY | UI_BoxFlag_FocusNavSkip, S8("grid_scope"));
    ui_push_parent(scope);
    ui_set_next_fixed_width(200.0f);
    ui_set_next_fixed_height(30.0f);
    ui_set_next_child_layout_axis(AXIS2_X);
    UI_Box* top_row = ui_box_make(0, S8("grid_top_row"));
    ui_push_parent(top_row);
    ui_set_next_fixed_width(50.0f);
    ui_set_next_fixed_height(30.0f);
    *top_left = ui_box_make(UI_BoxFlag_KeyboardClickable | UI_BoxFlag_FocusActive | UI_BoxFlag_DefaultFocusNavX | UI_BoxFlag_DefaultFocusNavY, S8("grid_top_left"));
    ui_set_next_fixed_width(50.0f);
    ui_set_next_fixed_height(30.0f);
    *top_right = ui_box_make(UI_BoxFlag_KeyboardClickable | UI_BoxFlag_FocusActive | UI_BoxFlag_DefaultFocusNavX | UI_BoxFlag_DefaultFocusNavY, S8("grid_top_right"));
    BUSTER_UNUSED(ui_pop_parent());
    ui_set_next_fixed_width(200.0f);
    ui_set_next_fixed_height(30.0f);
    ui_set_next_child_layout_axis(AXIS2_X);
    UI_Box* bottom_row = ui_box_make(0, S8("grid_bottom_row"));
    ui_push_parent(bottom_row);
    ui_set_next_fixed_width(50.0f);
    ui_set_next_fixed_height(30.0f);
    *bottom_left = ui_box_make(UI_BoxFlag_KeyboardClickable | UI_BoxFlag_FocusActive | UI_BoxFlag_DefaultFocusNavX | UI_BoxFlag_DefaultFocusNavY, S8("grid_bottom_left"));
    ui_set_next_fixed_width(50.0f);
    ui_set_next_fixed_height(30.0f);
    *bottom_right = ui_box_make(UI_BoxFlag_KeyboardClickable | UI_BoxFlag_FocusActive | UI_BoxFlag_DefaultFocusNavX | UI_BoxFlag_DefaultFocusNavY, S8("grid_bottom_right"));
    BUSTER_UNUSED(ui_pop_parent());
    BUSTER_UNUSED(ui_pop_parent());
}

BUSTER_GLOBAL_LOCAL void ui_test_build_nested_focus_scope(UI_Box** outer_button, UI_Box** inner_first, UI_Box** inner_second)
{
    ui_set_next_fixed_width(200.0f);
    ui_set_next_fixed_height(100.0f);
    UI_Box* outer_scope = ui_box_make(UI_BoxFlag_DefaultFocusNavY | UI_BoxFlag_FocusNavSkip, S8("outer_focus_scope"));
    ui_push_parent(outer_scope);
    ui_set_next_fixed_width(200.0f);
    ui_set_next_fixed_height(30.0f);
    *outer_button = ui_box_make(UI_BoxFlag_KeyboardClickable | UI_BoxFlag_FocusActive | UI_BoxFlag_DefaultFocusNavY, S8("outer_focus_button"));
    ui_set_next_fixed_width(200.0f);
    ui_set_next_fixed_height(60.0f);
    UI_Box* inner_scope = ui_box_make(UI_BoxFlag_DefaultFocusNavY | UI_BoxFlag_FocusNavSkip, S8("inner_focus_scope"));
    ui_push_parent(inner_scope);
    ui_set_next_fixed_width(200.0f);
    ui_set_next_fixed_height(30.0f);
    *inner_first = ui_box_make(UI_BoxFlag_KeyboardClickable | UI_BoxFlag_FocusActive | UI_BoxFlag_DefaultFocusNavY, S8("inner_focus_first"));
    ui_set_next_fixed_width(200.0f);
    ui_set_next_fixed_height(30.0f);
    *inner_second = ui_box_make(UI_BoxFlag_KeyboardClickable | UI_BoxFlag_FocusActive | UI_BoxFlag_DefaultFocusNavY, S8("inner_focus_second"));
    BUSTER_UNUSED(ui_pop_parent());
    BUSTER_UNUSED(ui_pop_parent());
}

BUSTER_GLOBAL_LOCAL void ui_test_scoped_directional_focus(UnitTestArguments* arguments, UnitTestResult* result)
{
    BUSTER_UNUSED(arguments);
    UnitTestResult result_local = {0};
#define result result_local
    UI_State* state = ui_state_allocate(0, 0);
    UI_Box *top_left = 0, *top_right = 0, *bottom_left = 0, *bottom_right = 0;
    ui_test_frame(state, arguments->arena, (UI_EventList){0}, 0.016);
    ui_test_build_focus_grid(&top_left, &top_right, &bottom_left, &bottom_right);
    ui_build_end();
    state->focus_active_key = top_left->key;
    UI_EventList events = ui_test_single_event(arguments->arena, UI_EventKind_Press, WM_KEY_RIGHT, float2_make(0, 0), float2_make(0, 0), S8(""));
    ui_test_frame(state, arguments->arena, events, 0.016);
    ui_test_build_focus_grid(&top_left, &top_right, &bottom_left, &bottom_right);
    ui_build_end();
    BUSTER_TEST(arguments, ui_key_match(state->focus_active_key, top_right->key));
    events = ui_test_single_event(arguments->arena, UI_EventKind_Press, WM_KEY_DOWN, float2_make(0, 0), float2_make(0, 0), S8(""));
    ui_test_frame(state, arguments->arena, events, 0.016);
    ui_test_build_focus_grid(&top_left, &top_right, &bottom_left, &bottom_right);
    ui_build_end();
    BUSTER_TEST(arguments, ui_key_match(state->focus_active_key, bottom_right->key));
    events = ui_test_single_event(arguments->arena, UI_EventKind_Press, WM_KEY_LEFT, float2_make(0, 0), float2_make(0, 0), S8(""));
    ui_test_frame(state, arguments->arena, events, 0.016);
    ui_test_build_focus_grid(&top_left, &top_right, &bottom_left, &bottom_right);
    ui_build_end();
    BUSTER_TEST(arguments, ui_key_match(state->focus_active_key, bottom_left->key));
    ui_state_deinitialize(state);

    state = ui_state_allocate(0, 0);
    UI_Box *outer_button = 0, *inner_first = 0, *inner_second = 0;
    ui_test_frame(state, arguments->arena, (UI_EventList){0}, 0.016);
    ui_test_build_nested_focus_scope(&outer_button, &inner_first, &inner_second);
    ui_build_end();
    state->focus_active_key = inner_first->key;
    events = ui_test_single_event(arguments->arena, UI_EventKind_Press, WM_KEY_TAB, float2_make(0, 0), float2_make(0, 0), S8(""));
    ui_test_frame(state, arguments->arena, events, 0.016);
    ui_test_build_nested_focus_scope(&outer_button, &inner_first, &inner_second);
    ui_build_end();
    BUSTER_TEST(arguments, ui_key_match(state->focus_active_key, inner_second->key) && !ui_key_match(state->focus_active_key, outer_button->key));
    events = ui_test_single_event(arguments->arena, UI_EventKind_Press, WM_KEY_TAB, float2_make(0, 0), float2_make(0, 0), S8(""));
    ui_test_frame(state, arguments->arena, events, 0.016);
    ui_test_build_nested_focus_scope(&outer_button, &inner_first, &inner_second);
    ui_build_end();
    BUSTER_TEST(arguments, ui_key_match(state->focus_active_key, inner_first->key));
    ui_state_deinitialize(state);

#undef result
    result->succeeded_test_count += result_local.succeeded_test_count;
    result->test_count += result_local.test_count;
}

BUSTER_GLOBAL_LOCAL void ui_test_keyboard_focus_changed_signal(UnitTestArguments* arguments, UnitTestResult* result)
{
    BUSTER_UNUSED(arguments);
    UnitTestResult result_local = {0};
#define result result_local
    UI_State* state = ui_state_allocate(0, 0);
    char8 first_memory[32] = "first";
    char8 second_memory[32] = "second";
    String8 first_value = {.pointer = first_memory, .length = 5};
    String8 second_value = {.pointer = second_memory, .length = 6};
    UI_TextEditState first_edit = {0};
    UI_TextEditState second_edit = {0};

    ui_test_frame(state, arguments->arena, (UI_EventList){0}, 0.016);
    ui_set_next_fixed_width(220.0f);
    ui_set_next_fixed_height(60.0f);
    UI_Box* scope = ui_box_make(UI_BoxFlag_DefaultFocusNavY | UI_BoxFlag_FocusNavSkip, S8("keyboard_focus_scope"));
    ui_push_parent(scope);
    UI_TextEditResult first_result = ui_text_edit(&first_edit, S8("Focus##first"), &first_value, BUSTER_ARRAY_LENGTH(first_memory) - 1);
    UI_TextEditResult second_result = ui_text_edit(&second_edit, S8("Focus##second"), &second_value, BUSTER_ARRAY_LENGTH(second_memory) - 1);
    BUSTER_UNUSED(ui_pop_parent());
    BUSTER_UNUSED(second_result);
    ui_build_end();
    state->focus_hot_key = first_result.widget.box->key;
    state->focus_active_key = first_result.widget.box->key;
    state->focus_edit_key = first_result.widget.box->key;

    UI_EventList tab_events = ui_test_single_event(arguments->arena, UI_EventKind_Press, WM_KEY_TAB, float2_make(0, 0), float2_make(0, 0), S8(""));
    ui_test_frame(state, arguments->arena, tab_events, 0.016);
    ui_set_next_fixed_width(220.0f);
    ui_set_next_fixed_height(60.0f);
    scope = ui_box_make(UI_BoxFlag_DefaultFocusNavY | UI_BoxFlag_FocusNavSkip, S8("keyboard_focus_scope"));
    ui_push_parent(scope);
    first_result = ui_text_edit(&first_edit, S8("Focus##first"), &first_value, BUSTER_ARRAY_LENGTH(first_memory) - 1);
    second_result = ui_text_edit(&second_edit, S8("Focus##second"), &second_value, BUSTER_ARRAY_LENGTH(second_memory) - 1);
    BUSTER_UNUSED(ui_pop_parent());
    BUSTER_TEST(arguments, !ui_focus_changed(first_result.widget.signal) && !ui_focus_changed(second_result.widget.signal));
    ui_build_end();
    BUSTER_TEST(arguments, ui_key_match(state->focus_active_key, second_result.widget.box->key) &&
                               ui_key_match(state->focus_edit_key, second_result.widget.box->key));

    ui_test_frame(state, arguments->arena, (UI_EventList){0}, 0.016);
    ui_set_next_fixed_width(220.0f);
    ui_set_next_fixed_height(60.0f);
    scope = ui_box_make(UI_BoxFlag_DefaultFocusNavY | UI_BoxFlag_FocusNavSkip, S8("keyboard_focus_scope"));
    ui_push_parent(scope);
    first_result = ui_text_edit(&first_edit, S8("Focus##first"), &first_value, BUSTER_ARRAY_LENGTH(first_memory) - 1);
    second_result = ui_text_edit(&second_edit, S8("Focus##second"), &second_value, BUSTER_ARRAY_LENGTH(second_memory) - 1);
    BUSTER_UNUSED(ui_pop_parent());
    BUSTER_UNUSED(first_result);
    ui_build_end();
    BUSTER_TEST(arguments, ui_focus_changed(second_result.widget.signal) && second_result.active &&
                               ui_key_match(state->focus_active_key, second_result.widget.box->key) &&
                               ui_key_match(state->focus_edit_key, second_result.widget.box->key));
    ui_state_deinitialize(state);
#undef result
    result->succeeded_test_count += result_local.succeeded_test_count;
    result->test_count += result_local.test_count;
}

BUSTER_GLOBAL_LOCAL void ui_test_text_and_widgets(UnitTestArguments* arguments, UnitTestResult* result)
{
    BUSTER_UNUSED(arguments);
    UnitTestResult result_local = {0};
#define result result_local
    UI_State* state = ui_state_allocate(0, 0);
    char8 buffer_memory[64] = "cat";
    String8 value = {.pointer = buffer_memory, .length = 3};
    UI_TextEditState edit = {0};

    ui_test_frame(state, arguments->arena, (UI_EventList){0}, 0.016);
    UI_TextEditResult initial_edit = ui_text_edit(&edit, S8("text_edit"), &value, sizeof(buffer_memory) - 1);
    UI_Box* text_box = initial_edit.widget.box;
    ui_build_end();
    float2 center = float2_make((text_box->rect.x0 + text_box->rect.x1) * 0.5f, (text_box->rect.y0 + text_box->rect.y1) * 0.5f);

    UI_EventList press_events = ui_test_single_event(arguments->arena, UI_EventKind_Press, WM_KEY_MOUSE_LEFT, center, float2_make(0, 0), S8(""));
    ui_test_frame(state, arguments->arena, press_events, 0.016);
    ui_text_edit(&edit, S8("text_edit"), &value, sizeof(buffer_memory) - 1);
    ui_build_end();

    UI_EventList text_events = ui_test_single_event(arguments->arena, UI_EventKind_Text, WM_KEY_NULL, center, float2_make(0, 0), S8("s"));
    ui_test_frame(state, arguments->arena, text_events, 0.016);
    UI_TextEditResult edit_result = ui_text_edit(&edit, S8("text_edit"), &value, sizeof(buffer_memory) - 1);
    ui_build_end();
    BUSTER_STRING_TEST(arguments, value, S8("cats"));
    BUSTER_TEST(arguments, edit_result.changed && edit_result.active);

    ui_test_frame(state, arguments->arena, (UI_EventList){0}, 0.016);
    UI_Signal initial_button = ui_button(S8("event_button"));
    ui_build_end();
    UI_Key button_key = initial_button.box->key;
    center = float2_make((initial_button.box->rect.x0 + initial_button.box->rect.x1) * 0.5f,
                         (initial_button.box->rect.y0 + initial_button.box->rect.y1) * 0.5f);
    UI_EventList button_press_events = ui_test_single_event(arguments->arena, UI_EventKind_Press, WM_KEY_MOUSE_LEFT, center, float2_make(0, 0), S8(""));
    ui_test_frame(state, arguments->arena, button_press_events, 0.016);
    UI_Signal button_pressed = ui_button(S8("event_button"));
    ui_build_end();
    BUSTER_TEST(arguments, ui_pressed(button_pressed) && ui_key_match(button_key, button_pressed.box->key));
    UI_EventList button_release_events = ui_test_single_event(arguments->arena, UI_EventKind_Release, WM_KEY_MOUSE_LEFT, center, float2_make(0, 0), S8(""));
    ui_test_frame(state, arguments->arena, button_release_events, 0.016);
    UI_Signal button_clicked = ui_button(S8("event_button"));
    ui_build_end();
    BUSTER_TEST(arguments, ui_clicked(button_clicked) && ui_key_match(button_key, button_clicked.box->key));

    ui_test_frame(state, arguments->arena, (UI_EventList){0}, 0.016);
    UI_WidgetResult initial_checkbox = ui_checkbox(S8("event_checkbox"), false);
    ui_build_end();
    center = float2_make((initial_checkbox.box->rect.x0 + initial_checkbox.box->rect.x1) * 0.5f,
                         (initial_checkbox.box->rect.y0 + initial_checkbox.box->rect.y1) * 0.5f);
    UI_EventList checkbox_press_events = ui_test_single_event(arguments->arena, UI_EventKind_Press, WM_KEY_MOUSE_LEFT, center, float2_make(0, 0), S8(""));
    ui_test_frame(state, arguments->arena, checkbox_press_events, 0.016);
    UI_WidgetResult checkbox_pressed = ui_checkbox(S8("event_checkbox"), false);
    ui_build_end();
    BUSTER_TEST(arguments, ui_pressed(checkbox_pressed.signal));
    UI_EventList checkbox_release_events = ui_test_single_event(arguments->arena, UI_EventKind_Release, WM_KEY_MOUSE_LEFT, center, float2_make(0, 0), S8(""));
    ui_test_frame(state, arguments->arena, checkbox_release_events, 0.016);
    UI_WidgetResult checkbox_clicked = ui_checkbox(S8("event_checkbox"), false);
    ui_build_end();
    BUSTER_TEST(arguments, checkbox_clicked.changed && checkbox_clicked.value);

    ui_test_frame(state, arguments->arena, (UI_EventList){0}, 0.016);
    UI_WidgetResult initial_slider = ui_slider(S8("event_slider"), 0.5f, 0.0f, 1.0f);
    ui_build_end();
    f32 slider_width = initial_slider.box->rect.x1 - initial_slider.box->rect.x0;
    center = float2_make(initial_slider.box->rect.x0 + slider_width * 0.8f,
                         (initial_slider.box->rect.y0 + initial_slider.box->rect.y1) * 0.5f);
    UI_EventList slider_press_events = ui_test_single_event(arguments->arena, UI_EventKind_Press, WM_KEY_MOUSE_LEFT, center, float2_make(0, 0), S8(""));
    ui_test_frame(state, arguments->arena, slider_press_events, 0.016);
    UI_WidgetResult slider_pressed = ui_slider(S8("event_slider"), 0.5f, 0.0f, 1.0f);
    ui_build_end();
    BUSTER_TEST(arguments, slider_pressed.changed && slider_pressed.value_f32 > 0.7f);
    UI_EventList slider_release_events = ui_test_single_event(arguments->arena, UI_EventKind_Release, WM_KEY_MOUSE_LEFT, center, float2_make(0, 0), S8(""));
    ui_test_frame(state, arguments->arena, slider_release_events, 0.016);
    UI_WidgetResult slider_released = ui_slider(S8("event_slider"), 0.5f, 0.0f, 1.0f);
    ui_build_end();
    BUSTER_TEST(arguments, ui_released(slider_released.signal));

    ui_test_frame(state, arguments->arena, (UI_EventList){0}, 0.016);
    UI_Box* initial_scroll_region = ui_scroll_region_begin(S8("event_scroll_region"));
    ui_set_next_fixed_height(1000.0f);
    ui_box_make(UI_BoxFlag_DrawBackground, S8("event_scroll_content"));
    ui_scroll_region_end();
    ui_build_end();
    UI_Box* initial_scroll_content = initial_scroll_region->first;
    BUSTER_TEST(arguments, initial_scroll_content != 0);
    BUSTER_TEST(arguments, initial_scroll_content && (initial_scroll_content->flags & UI_BoxFlag_FixedHeight));
    BUSTER_TEST(arguments, initial_scroll_content && float2_element(initial_scroll_content->fixed_size, AXIS2_Y) == 1000.0f);
    f32 initial_scroll_content_height = float2_element(initial_scroll_region->view_bounds, AXIS2_Y);
    f32 initial_scroll_region_height = initial_scroll_region->rect.y1 - initial_scroll_region->rect.y0;
    BUSTER_TEST(arguments, initial_scroll_content_height > initial_scroll_region_height);
    center = float2_make((initial_scroll_region->rect.x0 + initial_scroll_region->rect.x1) * 0.5f,
                         (initial_scroll_region->rect.y0 + initial_scroll_region->rect.y1) * 0.5f);
    UI_EventList widget_scroll_events = ui_test_single_event(arguments->arena, UI_EventKind_Scroll, WM_KEY_MOUSE_WHEEL_UP, center,
                                                              float2_make(0, 1), S8(""));
    ui_test_frame(state, arguments->arena, widget_scroll_events, 0.016);
    UI_Box* scrolled_region = ui_scroll_region_begin(S8("event_scroll_region"));
    ui_set_next_fixed_height(1000.0f);
    ui_box_make(UI_BoxFlag_DrawBackground, S8("event_scroll_content"));
    ui_scroll_region_end();
    ui_build_end();
    float2 scrolled_offset = ui_box_scroll_offset(scrolled_region);
    BUSTER_TEST(arguments, float2_element(scrolled_offset, AXIS2_Y) > 0.0f);

    ui_test_frame(state, arguments->arena, (UI_EventList){0}, 0.016);
    UI_Box* initial_menu = ui_menu_begin(S8("event_menu"));
    UI_WidgetResult initial_menu_item = ui_menu_item(S8("event_menu_item"), true);
    ui_menu_end();
    ui_build_end();
    BUSTER_UNUSED(initial_menu);
    center = float2_make((initial_menu_item.box->rect.x0 + initial_menu_item.box->rect.x1) * 0.5f,
                         (initial_menu_item.box->rect.y0 + initial_menu_item.box->rect.y1) * 0.5f);
    UI_EventList menu_press_events = ui_test_single_event(arguments->arena, UI_EventKind_Press, WM_KEY_MOUSE_LEFT, center, float2_make(0, 0), S8(""));
    ui_test_frame(state, arguments->arena, menu_press_events, 0.016);
    ui_menu_begin(S8("event_menu"));
    UI_WidgetResult menu_pressed = ui_menu_item(S8("event_menu_item"), true);
    ui_menu_end();
    ui_build_end();
    UI_EventList menu_release_events = ui_test_single_event(arguments->arena, UI_EventKind_Release, WM_KEY_MOUSE_LEFT, center, float2_make(0, 0), S8(""));
    ui_test_frame(state, arguments->arena, menu_release_events, 0.016);
    ui_menu_begin(S8("event_menu"));
    UI_WidgetResult menu_clicked = ui_menu_item(S8("event_menu_item"), true);
    ui_menu_end();
    ui_build_end();
    BUSTER_TEST(arguments, ui_pressed(menu_pressed.signal) && menu_clicked.changed && menu_clicked.value);

    ui_test_frame(state, arguments->arena, (UI_EventList){0}, 0.016);
    UI_Box* initial_popup = ui_popup_begin(S8("event_popup"));
    UI_WidgetResult initial_popup_item = ui_popup_item(S8("event_popup_item"), true);
    ui_popup_end();
    ui_build_end();
    BUSTER_UNUSED(initial_popup);
    center = float2_make((initial_popup_item.box->rect.x0 + initial_popup_item.box->rect.x1) * 0.5f,
                         (initial_popup_item.box->rect.y0 + initial_popup_item.box->rect.y1) * 0.5f);
    UI_EventList popup_press_events = ui_test_single_event(arguments->arena, UI_EventKind_Press, WM_KEY_MOUSE_LEFT, center, float2_make(0, 0), S8(""));
    ui_test_frame(state, arguments->arena, popup_press_events, 0.016);
    ui_popup_begin(S8("event_popup"));
    UI_WidgetResult popup_pressed = ui_popup_item(S8("event_popup_item"), true);
    ui_popup_end();
    ui_build_end();
    UI_EventList popup_release_events = ui_test_single_event(arguments->arena, UI_EventKind_Release, WM_KEY_MOUSE_LEFT, center, float2_make(0, 0), S8(""));
    ui_test_frame(state, arguments->arena, popup_release_events, 0.016);
    ui_popup_begin(S8("event_popup"));
    UI_WidgetResult popup_clicked = ui_popup_item(S8("event_popup_item"), true);
    ui_popup_end();
    ui_build_end();
    BUSTER_TEST(arguments, ui_pressed(popup_pressed.signal) && popup_clicked.changed && popup_clicked.value);

    ui_test_frame(state, arguments->arena, (UI_EventList){0}, 0.016);
    UI_Box* label = ui_label(S8("label"));
    UI_Signal button = ui_button(S8("button"));
    UI_WidgetResult checkbox = ui_checkbox(S8("check"), false);
    UI_WidgetResult radio = ui_radio(S8("choice"), false);
    UI_WidgetResult slider = ui_slider(S8("range"), 0.5f, 0.0f, 1.0f);
    UI_Box* separator = ui_separator(AXIS2_X);
    UI_Box* spacer = ui_spacer(ui_pixels(4, 1.0f), ui_pixels(4, 1.0f));
    UI_Signal row = ui_list_row(S8("row"), true);
    UI_Signal tree = ui_tree_row(S8("tree"), 2, false, true);
    UI_WidgetResult tab = ui_tab(S8("tab"), true);
    UI_Box* scroll_region = ui_scroll_region_begin(S8("scroll_region"));
    UI_Box* scroll_label = ui_label(S8("inside_scroll"));
    ui_scroll_region_end();
    UI_Box* menu = ui_menu_begin(S8("menu"));
    UI_WidgetResult menu_item = ui_menu_item(S8("menu_item"), true);
    ui_menu_end();
    UI_Box* popup = ui_popup_begin(S8("popup"));
    UI_WidgetResult popup_item = ui_popup_item(S8("popup_item"), true);
    ui_popup_end();
    ui_build_end();
    BUSTER_TEST(arguments, label && button.box && checkbox.box && radio.box && slider.box && separator && spacer && row.box && tree.box && tab.box &&
                               scroll_region && scroll_label && menu && menu_item.box && popup && popup_item.box);
    BUSTER_TEST(arguments, !checkbox.value && !radio.value && slider.value_f32 == 0.5f && edit.key.value != 0);
    ui_state_deinitialize(state);
#undef result
    result->succeeded_test_count += result_local.succeeded_test_count;
    result->test_count += result_local.test_count;
}

UnitTestResult ui_tests(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    ui_test_flag_inventory(arguments, &result);
    ui_test_layout_and_view(arguments, &result);
    ui_test_input_and_focus(arguments, &result);
    ui_test_clipped_hit_ownership(arguments, &result);
    ui_test_first_frame_and_overlay_ownership(arguments, &result);
    ui_test_active_owner_pruning(arguments, &result);
    ui_test_chronological_cross_box_ownership(arguments, &result);
    ui_test_prebuild_event_chronology(arguments, &result);
    ui_test_mixed_keyboard_activation(arguments, &result);
    ui_test_ordered_focus_and_text_ownership(arguments, &result);
    ui_test_drop_copy_lifetime_and_capacity(arguments, &result);
    ui_test_disabled_ownership_and_focus_policy(arguments, &result);
    ui_test_stable_widget_identity_and_state(arguments, &result);
    ui_test_text_edit_atomic_policy(arguments, &result);
    ui_test_draw_command_capacity(arguments, &result);
    ui_test_utf8_tooltip_and_draw_commands(arguments, &result);
    ui_test_scoped_directional_focus(arguments, &result);
    ui_test_keyboard_focus_changed_signal(arguments, &result);
    ui_test_text_and_widgets(arguments, &result);
    return result;
}
#endif
