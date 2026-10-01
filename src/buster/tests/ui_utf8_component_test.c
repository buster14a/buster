// Headless UI scalar regressions. test_ui_utf8 compiles the actual ui_core
// module, exercises text-event activation and draw-command underline matching,
// and supplies only the unused native rendering boundary. No compiler or
// desktop window/backend dependency belongs to this component runner.

#include <buster/lib/system_headers.h>
#include <buster/lib/os.h>
#include <buster/lib/ui_core.h>
#include <buster/lib/string.h>
#include <stdio.h>

BUSTER_V_IMPL OsState os_state;
BUSTER_GLOBAL_LOCAL ProgramState ui_utf8_program;
BUSTER_V_IMPL ProgramState* program_state = &ui_utf8_program;
BUSTER_GLOBAL_LOCAL u32 ui_utf8_renderer_calls;
BUSTER_GLOBAL_LOCAL u32 ui_utf8_assertions;
BUSTER_GLOBAL_LOCAL u32 ui_utf8_failures;

#if BUSTER_UNITY_BUILD
#include <buster/lib/arena.c>
#include <buster/lib/integer.c>
#include <buster/lib/string.c>
#include <buster/lib/os.c>
#include <buster/lib/file.c>
#include <buster/lib/hash.c>
#include <buster/lib/time.c>
#include <buster/lib/float.c>
#include <buster/lib/ui_core.c>
#endif

RenderingWindowSize rendering_window_get_size(RenderingWindowHandle* window)
{
    BUSTER_UNUSED(window);
    ui_utf8_renderer_calls += 1;
    return (RenderingWindowSize){0};
}

void rendering_window_render_rect(RenderingWindowHandle* window, RectDraw draw)
{
    BUSTER_UNUSED(window);
    BUSTER_UNUSED(draw);
    ui_utf8_renderer_calls += 1;
}

void rendering_window_render_text(RenderingHandle* rendering, RenderingWindowHandle* window, String8 string, float4 color,
                                RenderFontType font_type, f32 x_offset, f32 y_offset)
{
    BUSTER_UNUSED(rendering);
    BUSTER_UNUSED(window);
    BUSTER_UNUSED(string);
    BUSTER_UNUSED(color);
    BUSTER_UNUSED(font_type);
    BUSTER_UNUSED(x_offset);
    BUSTER_UNUSED(y_offset);
    ui_utf8_renderer_calls += 1;
}

bool rendering_window_render_background_blur_rounded(RenderingWindowHandle* window, F32Interval2 rect, u32 radius, float4 corner_radii)
{
    BUSTER_UNUSED(window);
    BUSTER_UNUSED(rect);
    BUSTER_UNUSED(radius);
    BUSTER_UNUSED(corner_radii);
    ui_utf8_renderer_calls += 1;
    return false;
}

BUSTER_GLOBAL_LOCAL bool ui_utf8_check(bool condition, u32 codepoint, String8 operation)
{
    ui_utf8_assertions += 1;
    if (!condition)
    {
        ui_utf8_failures += 1;
        fprintf(stderr, "ui_utf8_component_tests: U+%04X %.*s failed\n", (unsigned)codepoint, (int)operation.length, operation.pointer);
    }
    return condition;
}

BUSTER_GLOBAL_LOCAL UI_Box* ui_utf8_box_build(String8 label, u32 codepoint)
{
    ui_set_next_font_size(10.0f);
    ui_set_next_text_padding(0.0f);
    ui_set_next_fixed_width(100.0f);
    ui_set_next_fixed_height(20.0f);
    UI_Box* result = ui_box_make(UI_BoxFlag_DrawText | UI_BoxFlag_DrawTextFastpathCodepoint | UI_BoxFlag_FocusHot, S8("utf8_fastpath"));
    ui_box_set_display_string(result, label);
    ui_box_set_fastpath_codepoint(result, codepoint);
    return result;
}

BUSTER_GLOBAL_LOCAL UI_EventList ui_utf8_event(Arena* arena, UI_EventKind kind, float2 position, String8 text)
{
    UI_Event event = {.kind = kind, .pos = position, .string = text};
    UI_EventList result = {0};
    ui_event_list_push(arena, &result, &event);
    return result;
}

BUSTER_GLOBAL_LOCAL void ui_utf8_consumer_case(Arena* arena, String8 text, u32 codepoint, String8 sanitized, bool expect_activation)
{
    UI_State* state = ui_state_allocate(0, 0);
    if (ui_utf8_check(state != 0, codepoint, S8("state allocation")))
    {
        // Two columns occupy three bytes before the scalar being underlined.
        char8 label_bytes[8] = {'a', (char8)0xc3, (char8)0xa1};
        for (u64 index = 0; index < text.length; index += 1)
        {
            label_bytes[3 + index] = text.pointer[index];
        }
        label_bytes[3 + text.length] = 'z';
        String8 label = {.pointer = label_bytes, .length = 4 + text.length};
        ui_state_select(state);
        ui_build_begin(0, 0, 16.0, (UI_EventList){0});
        UI_Box* box = ui_utf8_box_build(label, codepoint);
        ui_build_end();
        ui_utf8_check(box->string.length == 4 + sanitized.length &&
                      string_equal(string_slice(box->string, 3, box->string.length - 1), sanitized),
                      codepoint, S8("display sanitization policy"));
        ui_draw();
        UI_DrawCommand* underline = 0;
        for (u64 index = 0; index < state->draw_command_count; index += 1)
        {
            UI_DrawCommand* command = &state->draw_commands[index];
            if (command->box == box && command->kind == UI_DrawCommandKind_Rect)
            {
                underline = command;
            }
        }
        if (ui_utf8_check(underline != 0, codepoint, S8("underline matching")))
        {
            ui_utf8_check(underline->rect.x0 == box->rect.x0 + 12.0f && underline->rect.x1 == box->rect.x0 + 18.0f &&
                          underline->rect.y0 == box->rect.y1 - 2.0f && underline->rect.y1 == box->rect.y1,
                          codepoint, S8("underline scalar column"));
        }

        float2 center = float2_make((box->rect.x0 + box->rect.x1) * 0.5f, (box->rect.y0 + box->rect.y1) * 0.5f);
        ui_build_begin(0, 0, 16.0, ui_utf8_event(arena, UI_EventKind_MouseMove, center, S8("")));
        box = ui_utf8_box_build(label, codepoint);
        BUSTER_UNUSED(ui_signal_from_box(box));
        ui_build_end();
        ui_utf8_check(ui_key_match(state->focus_hot_key, box->key), codepoint, S8("hot focus routing"));

        ui_build_begin(0, 0, 16.0, ui_utf8_event(arena, UI_EventKind_Text, center, text));
        box = ui_utf8_box_build(label, codepoint);
        if (ui_utf8_check(state->events.first != 0, codepoint, S8("text event retention")))
        {
            ui_utf8_check(string_equal(state->events.first->v.string, text), codepoint, S8("raw text event policy"));
        }
        UI_Signal signal = ui_signal_from_box(box);
        ui_build_end();
        ui_utf8_check(ui_clicked(signal) == expect_activation && !!(signal.f & UI_SignalFlag_KeyboardPressed) == expect_activation &&
                      state->events.count == (expect_activation ? 0u : 1u),
                      codepoint, S8("text event scalar activation"));

        ui_build_begin(0, 0, 16.0, ui_utf8_event(arena, UI_EventKind_Text, center, S8("q")));
        box = ui_utf8_box_build(label, codepoint);
        signal = ui_signal_from_box(box);
        ui_build_end();
        ui_utf8_check(!ui_clicked(signal) && state->events.count == 1, codepoint, S8("different scalar rejection"));
        ui_state_deinitialize(state);
    }
}

BUSTER_GLOBAL_LOCAL void ui_utf8_disabled_case(Arena* arena)
{
    UI_State* state = ui_state_allocate(0, 0);
    if (ui_utf8_check(state != 0, 0, S8("state allocation")))
    {
        ui_state_select(state);
        ui_build_begin(0, 0, 16.0, (UI_EventList){0});
        UI_Box* box = ui_utf8_box_build(S8("a\0z"), 0);
        ui_build_end();
        ui_draw();
        bool has_underline = false;
        for (u64 index = 0; index < state->draw_command_count; index += 1)
        {
            UI_DrawCommand* command = &state->draw_commands[index];
            has_underline = has_underline || (command->box == box && command->kind == UI_DrawCommandKind_Rect);
        }
        ui_utf8_check(!has_underline, 0, S8("zero fastpath has no underline"));
        float2 center = float2_make((box->rect.x0 + box->rect.x1) * 0.5f, (box->rect.y0 + box->rect.y1) * 0.5f);
        ui_build_begin(0, 0, 16.0, ui_utf8_event(arena, UI_EventKind_MouseMove, center, S8("")));
        box = ui_utf8_box_build(S8("a\0z"), 0);
        BUSTER_UNUSED(ui_signal_from_box(box));
        ui_build_end();
        ui_build_begin(0, 0, 16.0, ui_utf8_event(arena, UI_EventKind_Text, center, S8("\0")));
        box = ui_utf8_box_build(S8("a\0z"), 0);
        UI_Signal signal = ui_signal_from_box(box);
        ui_build_end();
        ui_utf8_check(!ui_clicked(signal) && state->events.count == 1, 0, S8("zero fastpath has no activation"));
        ui_state_deinitialize(state);
    }
}

int main(void)
{
    os_state.page_size = os_get_page_size();
    os_state.allocation_granularity = os_state.page_size;
    os_state.large_page_size = BUSTER_MB(2);
    ThreadContext* context = thread_context_allocate();
    thread_context_select(context);
    Arena* arena = arena_create((ArenaCreation){0});
    program_state->arena = arena;
    struct
    {
        String8 text;
        u32 codepoint;
    } cases[] = {
        {S8("\x01"), 0x0001u},
        {S8("x"), 0x0078u},
        {S8("\x7f"), 0x007fu},
        {S8("\xc2\x80"), 0x0080u},
        {S8("\xc3\xa9"), 0x00e9u},
        {S8("\xdf\xbf"), 0x07ffu},
        {S8("\xe0\xa0\x80"), 0x0800u},
        {S8("\xe2\x82\xac"), 0x20acu},
        {S8("\xed\x9f\xbf"), 0xd7ffu},
        {S8("\xee\x80\x80"), 0xe000u},
        {S8("\xef\xbf\xbd"), 0xfffdu},
        {S8("\xef\xbf\xbf"), 0xffffu},
        {S8("\xf0\x90\x80\x80"), 0x10000u},
        {S8("\xf0\x9f\x98\x80"), 0x1f600u},
        {S8("\xf4\x8f\xbf\xbf"), 0x10ffffu},
    };
    for (u64 index = 0; index < BUSTER_ARRAY_LENGTH(cases); index += 1)
    {
        ui_utf8_consumer_case(arena, cases[index].text, cases[index].codepoint, cases[index].text, true);
    }
    ui_utf8_consumer_case(arena, S8("\x80"), 0xfffdu, S8("\xef\xbf\xbd"), true);
    ui_utf8_consumer_case(arena, S8("\xe2"), 0xfffdu, S8("\xef\xbf\xbd"), true);
    ui_utf8_consumer_case(arena, S8("\xc0\xaf"), 0xfffdu, S8("\xef\xbf\xbd\xef\xbf\xbd"), false);
    ui_utf8_consumer_case(arena, S8("\xe2\x82"), 0xfffdu, S8("\xef\xbf\xbd\xef\xbf\xbd"), false);
    ui_utf8_consumer_case(arena, S8("\xed\xa0\x80"), 0xfffdu, S8("\xef\xbf\xbd\xef\xbf\xbd\xef\xbf\xbd"), false);
    ui_utf8_consumer_case(arena, S8("\xf4\x90\x80\x80"), 0xfffdu, S8("\xef\xbf\xbd\xef\xbf\xbd\xef\xbf\xbd\xef\xbf\xbd"), false);
    ui_utf8_disabled_case(arena);
    ui_utf8_check(ui_utf8_renderer_calls == 0, 0, S8("headless rendering boundary"));
    printf("ui_utf8_component_tests: %u/%u assertions passed\n", (unsigned)(ui_utf8_assertions - ui_utf8_failures), (unsigned)ui_utf8_assertions);
    thread_context_release(context);
    arena_destroy(arena, 1);
    program_state->arena = 0;
    return ui_utf8_failures != 0;
}
