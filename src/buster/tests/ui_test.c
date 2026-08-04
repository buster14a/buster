#include <buster/tests/ui_test.h>

#include <buster/lib/ui_builder.h>

BUSTER_GLOBAL_LOCAL void ui_test_frame(UI_State* state, Arena* arena, UI_EventList events, f64 frame_time)
{
    ui_state_select(state);
    ui_build_begin(0, 0, frame_time, events);
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
    UI_BoxFlagInfo debug = ui_box_flag_info(63);
    BUSTER_TEST_RAW(arguments, debug.implemented && debug.flag == UI_BoxFlag_Debug, S8("UI debug flag inventory missing"));
    UI_BoxFlagInfo blur = ui_box_flag_info(28);
    UI_BoxFlagInfo bucket = ui_box_flag_info(41);
    UI_BoxFlagInfo rounded = ui_box_flag_info(55);
    BUSTER_TEST_RAW(arguments, blur.renderer_dependency && bucket.renderer_dependency && rounded.renderer_dependency,
                    S8("UI renderer dependency inventory missing"));
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
    BUSTER_TEST(arguments, parent->renderer_dependency_flags == UI_BoxRendererDependency_CornerRadii);
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
    UI_EventList drop_events = ui_test_single_event(arguments->arena, UI_EventKind_FileDrop, WM_KEY_NULL, center, float2_make(0, 0), S8(""));
    ui_test_frame(state, arguments->arena, drop_events, 0.016);
    ui_set_next_fixed_width(160.0f);
    ui_set_next_fixed_height(30.0f);
    drop = ui_box_make(UI_BoxFlag_DropSite, S8("drop_site"));
    UI_Signal drop_signal = ui_signal_from_box(drop);
    ui_build_end();
    BUSTER_TEST(arguments, ui_dropped(drop_signal));

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
    ui_test_text_and_widgets(arguments, &result);
    return result;
}
