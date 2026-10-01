#pragma once

// Private headless frame/event helpers shared by the retained UI suite and
// bounded component consumers. Frame time is converted from seconds to ms.
#include <buster/lib/ui_core.h>

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

