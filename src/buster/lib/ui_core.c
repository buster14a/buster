// Raddebugger-inspired immediate-mode UI core, adapted to Buster's renderer/window layer.

#include <buster/lib/ui_core.h>
#include <buster/lib/string.h>
#include <buster/lib/float.h>
#include <buster/lib/os.h>

BUSTER_V_IMPL UI_State* ui_state;

BUSTER_GLOBAL_LOCAL void ui_prune_focus_keys(void);
BUSTER_GLOBAL_LOCAL bool ui_box_is_current(UI_Box* box);
BUSTER_GLOBAL_LOCAL void ui_prune_active_keys(void);
BUSTER_GLOBAL_LOCAL void ui_route_event_owners(void);
BUSTER_GLOBAL_LOCAL UI_MouseButtonKind ui_mouse_button_kind_from_key(WmKey key, bool* is_mouse);
BUSTER_GLOBAL_LOCAL bool ui_box_contains_point(UI_Box* box, float2 point);
BUSTER_GLOBAL_LOCAL bool ui_box_draws_content(UI_Box* box);

typedef enum UI_FocusDirection
{
    UI_FocusDirection_LinearForward,
    UI_FocusDirection_LinearBackward,
    UI_FocusDirection_Left,
    UI_FocusDirection_Right,
    UI_FocusDirection_Up,
    UI_FocusDirection_Down,
} UI_FocusDirection;

BUSTER_GLOBAL_LOCAL UI_Box* ui_focus_navigation_candidate(UI_Key current_key, UI_BoxFlags axis_flag, UI_FocusDirection direction,
                                                           u64 build_index, UI_Box* root);

BUSTER_GLOBAL_LOCAL F32Interval2 ui_rect_make(f32 x0, f32 y0, f32 x1, f32 y1)
{
    return (F32Interval2){.x0 = x0, .y0 = y0, .x1 = x1, .y1 = y1};
}

BUSTER_GLOBAL_LOCAL float2 ui_rect_dim(F32Interval2 rect)
{
    return float2_make(rect.x1 - rect.x0, rect.y1 - rect.y0);
}

BUSTER_GLOBAL_LOCAL bool ui_rect_contains(F32Interval2 rect, float2 p)
{
    f32 px = float2_element(p, AXIS2_X);
    f32 py = float2_element(p, AXIS2_Y);
    bool result = (px >= rect.x0 && px <= rect.x1 && py >= rect.y0 && py <= rect.y1);
    return result;
}

BUSTER_GLOBAL_LOCAL F32Interval2 ui_rect_intersect(F32Interval2 a, F32Interval2 b)
{
    F32Interval2 result = ui_rect_make(BUSTER_MAX(a.x0, b.x0), BUSTER_MAX(a.y0, b.y0), BUSTER_MIN(a.x1, b.x1), BUSTER_MIN(a.y1, b.y1));
    return result;
}

BUSTER_GLOBAL_LOCAL bool ui_rect_has_area(F32Interval2 rect)
{
    return rect.x1 > rect.x0 && rect.y1 > rect.y0;
}

BUSTER_GLOBAL_LOCAL u32 ui_background_blur_radius_clamp(u32 radius)
{
    return radius > UI_BACKGROUND_BLUR_RADIUS_MAX ? UI_BACKGROUND_BLUR_RADIUS_MAX : radius;
}

BUSTER_GLOBAL_LOCAL F32Interval2 ui_box_text_clip_rect(UI_Box* box)
{
    F32Interval2 result = box ? box->clip_rect : (F32Interval2){0};
    if (box && (box->flags & UI_BoxFlag_Clip))
    {
        result = ui_rect_intersect(result, box->rect);
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool ui_box_focusable(UI_Box* box, bool active);

BUSTER_GLOBAL_LOCAL u64 ui_utf8_sequence_length(String8 string, u64 position)
{
    if (!string.pointer || position >= string.length)
    {
        return 0;
    }

    u8 first = (u8)string.pointer[position];
    u64 result = 0;
    if (first <= 0x7fu)
    {
        result = 1;
    }
    else if (first >= 0xc2u && first <= 0xdfu)
    {
        result = 2;
    }
    else if (first >= 0xe0u && first <= 0xefu)
    {
        result = 3;
    }
    else if (first >= 0xf0u && first <= 0xf4u)
    {
        result = 4;
    }
    else
    {
        return 0;
    }

    if (result > string.length - position)
    {
        return 0;
    }
    for (u64 index = 1; index < result; index += 1)
    {
        u8 continuation = (u8)string.pointer[position + index];
        if (continuation < 0x80u || continuation > 0xbfu)
        {
            return 0;
        }
    }

    u8 second = result > 1 ? (u8)string.pointer[position + 1] : 0;
    if ((first == 0xe0u && second < 0xa0u) || (first == 0xedu && second > 0x9fu) || (first == 0xf0u && second < 0x90u) ||
        (first == 0xf4u && second > 0x8fu))
    {
        return 0;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL u32 ui_utf8_codepoint_at(String8 string, u64 position)
{
    u64 length = ui_utf8_sequence_length(string, position);
    if (length == 0)
    {
        return 0xfffdu;
    }
    u8 first = (u8)string.pointer[position];
    u32 result = first;
    if (length >= 2)
    {
        result = (result & 0x1fu) << 6 | ((u8)string.pointer[position + 1] & 0x3fu);
    }
    if (length >= 3)
    {
        result = (result & 0x0fu) << 6 | ((u8)string.pointer[position + 2] & 0x3fu);
    }
    if (length == 4)
    {
        result = (result & 0x07u) << 6 | ((u8)string.pointer[position + 3] & 0x3fu);
    }
    return result;
}

BUSTER_GLOBAL_LOCAL u64 ui_utf8_codepoint_count(String8 string)
{
    u64 result = 0;
    u64 position = 0;
    while (position < string.length)
    {
        u64 sequence_length = ui_utf8_sequence_length(string, position);
        position += sequence_length ? sequence_length : 1;
        result += 1;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL u64 ui_utf8_byte_offset_for_columns(String8 string, u64 columns)
{
    u64 result = 0;
    u64 column = 0;
    while (result < string.length && column < columns)
    {
        u64 sequence_length = ui_utf8_sequence_length(string, result);
        result += sequence_length ? sequence_length : 1;
        column += 1;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL u64 ui_utf8_columns_for_byte_offset(String8 string, u64 byte_offset)
{
    u64 result = 0;
    u64 position = 0;
    byte_offset = BUSTER_MIN(byte_offset, string.length);
    while (position < byte_offset)
    {
        u64 sequence_length = ui_utf8_sequence_length(string, position);
        sequence_length = sequence_length ? sequence_length : 1;
        if (sequence_length > byte_offset - position)
        {
            break;
        }
        position += sequence_length;
        result += 1;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL String8 ui_utf8_sanitize(Arena* arena, String8 string)
{
    if (string.length == 0)
    {
        return string;
    }
    if (!string.pointer)
    {
        return (String8){0};
    }

    bool valid = true;
    u64 extra_length = 0;
    u64 position = 0;
    while (position < string.length)
    {
        u64 sequence_length = ui_utf8_sequence_length(string, position);
        if (sequence_length == 0)
        {
            valid = false;
            if (extra_length > (u64)-1 - 2)
            {
                return (String8){0};
            }
            extra_length += 2;
            position += 1;
        }
        else
        {
            position += sequence_length;
        }
    }
    if (valid)
    {
        if (!arena || string.length == 0)
        {
            return string;
        }
        return string_duplicate_arena(arena, string, false);
    }
    if (!arena || extra_length > (u64)-1 - string.length)
    {
        return (String8){0};
    }

    u64 output_length = string.length + extra_length;
    char8* output = arena_allocate(arena, char8, output_length);
    u64 source_position = 0;
    u64 output_position = 0;
    while (source_position < string.length)
    {
        u64 sequence_length = ui_utf8_sequence_length(string, source_position);
        if (sequence_length == 0)
        {
            output[output_position++] = (char8)0xef;
            output[output_position++] = (char8)0xbf;
            output[output_position++] = (char8)0xbd;
            source_position += 1;
        }
        else
        {
            memcpy(output + output_position, string.pointer + source_position, sequence_length);
            output_position += sequence_length;
            source_position += sequence_length;
        }
    }
    return (String8){.pointer = output, .length = output_length};
}

BUSTER_GLOBAL_LOCAL void ui_stack_reset(UI_State* state)
{
    memset(&state->stacks, 0, sizeof(state->stacks));
}

#define UI_STACK_TOP_IMPL(name, type)                                                                                                                          \
    type ui_top_##name(void)                                                                                                                                   \
    {                                                                                                                                                          \
        type result = ui_state->stack_nulls.name;                                                                                                              \
        if (ui_state->stacks.name##_length != 0)                                                                                                               \
        {                                                                                                                                                      \
            result = ui_state->stacks.name[ui_state->stacks.name##_length - 1];                                                                                \
        }                                                                                                                                                      \
        return result;                                                                                                                                         \
    }

#define UI_STACK_PUSH_IMPL(name, type)                                                                                                                         \
    type ui_push_##name(type v)                                                                                                                                \
    {                                                                                                                                                          \
        type old = ui_top_##name();                                                                                                                            \
        BUSTER_CHECK(ui_state->stacks.name##_length < UI_STACK_CAPACITY);                                                                                      \
        ui_state->stacks.name[ui_state->stacks.name##_length++] = v;                                                                                           \
        ui_state->stacks.name##_auto_pop = 0;                                                                                                                  \
        return old;                                                                                                                                            \
    }

#define UI_STACK_POP_IMPL(name, type)                                                                                                                          \
    type ui_pop_##name(void)                                                                                                                                   \
    {                                                                                                                                                          \
        BUSTER_CHECK(ui_state->stacks.name##_length != 0);                                                                                                     \
        type result = ui_state->stacks.name[--ui_state->stacks.name##_length];                                                                                 \
        ui_state->stacks.name##_auto_pop = 0;                                                                                                                  \
        return result;                                                                                                                                         \
    }

#define UI_STACK_SET_NEXT_IMPL(name, type)                                                                                                                     \
    type ui_set_next_##name(type v)                                                                                                                            \
    {                                                                                                                                                          \
        type old = ui_top_##name();                                                                                                                            \
        BUSTER_CHECK(ui_state->stacks.name##_length < UI_STACK_CAPACITY);                                                                                      \
        ui_state->stacks.name[ui_state->stacks.name##_length++] = v;                                                                                           \
        ui_state->stacks.name##_auto_pop = 1;                                                                                                                  \
        return old;                                                                                                                                            \
    }

#define UI_STACK_IMPL(name, type)                                                                                                                              \
    UI_STACK_TOP_IMPL(name, type)                                                                                                                              \
    UI_STACK_PUSH_IMPL(name, type)                                                                                                                             \
    UI_STACK_POP_IMPL(name, type)                                                                                                                              \
    UI_STACK_SET_NEXT_IMPL(name, type)

UI_STACK_IMPL(parent, UI_Box*)
UI_STACK_IMPL(child_layout_axis, Axis2)
UI_STACK_IMPL(fixed_x, f32)
UI_STACK_IMPL(fixed_y, f32)
UI_STACK_IMPL(fixed_width, f32)
UI_STACK_IMPL(fixed_height, f32)
UI_STACK_IMPL(pref_width, UI_Size)
UI_STACK_IMPL(pref_height, UI_Size)
UI_STACK_IMPL(min_width, f32)
UI_STACK_IMPL(min_height, f32)
UI_STACK_IMPL(flags, UI_BoxFlags)
UI_STACK_IMPL(background_color, float4)
UI_STACK_IMPL(text_color, float4)
UI_STACK_IMPL(border_color, float4)
UI_STACK_IMPL(font_size, f32)
UI_STACK_IMPL(text_padding, f32)
UI_STACK_IMPL(text_alignment, UI_TextAlign)

BUSTER_GLOBAL_LOCAL void ui_stack_auto_pop_all(UI_State* state)
{
#define UI_STACK_AUTO_POP(name)                                                                                                                                \
    do                                                                                                                                                         \
    {                                                                                                                                                          \
        if (state->stacks.name##_auto_pop && state->stacks.name##_length != 0)                                                                                 \
        {                                                                                                                                                      \
            state->stacks.name##_length -= 1;                                                                                                                  \
            state->stacks.name##_auto_pop = 0;                                                                                                                 \
        }                                                                                                                                                      \
    } while (0)
    UI_STACK_AUTO_POP(parent);
    UI_STACK_AUTO_POP(child_layout_axis);
    UI_STACK_AUTO_POP(fixed_x);
    UI_STACK_AUTO_POP(fixed_y);
    UI_STACK_AUTO_POP(fixed_width);
    UI_STACK_AUTO_POP(fixed_height);
    UI_STACK_AUTO_POP(pref_width);
    UI_STACK_AUTO_POP(pref_height);
    UI_STACK_AUTO_POP(min_width);
    UI_STACK_AUTO_POP(min_height);
    UI_STACK_AUTO_POP(flags);
    UI_STACK_AUTO_POP(background_color);
    UI_STACK_AUTO_POP(text_color);
    UI_STACK_AUTO_POP(border_color);
    UI_STACK_AUTO_POP(font_size);
    UI_STACK_AUTO_POP(text_padding);
    UI_STACK_AUTO_POP(text_alignment);
#undef UI_STACK_AUTO_POP
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
    Arena* result = ui_state->build_arenas[ui_state->build_index % BUSTER_ARRAY_LENGTH(ui_state->build_arenas)];
    return result;
}

BUSTER_GLOBAL_LOCAL bool ui_arena_try_advance(Arena* arena, u64* position, u64 size, u64 alignment)
{
    if (!arena || !position || !BUSTER_IS_POWER_OF_TWO(alignment) || *position > arena->reserved_size)
    {
        return false;
    }

    u64 alignment_mask = alignment - 1;
    if (*position > (u64)-1 - alignment_mask)
    {
        return false;
    }

    u64 aligned_position = (*position + alignment_mask) & ~alignment_mask;
    if (aligned_position > arena->reserved_size || size > arena->reserved_size - aligned_position)
    {
        return false;
    }

    *position = aligned_position + size;
    return true;
}

BUSTER_GLOBAL_LOCAL bool ui_string8_source_is_valid(String8 string)
{
    return string.length == 0 || string.pointer != 0;
}

BUSTER_GLOBAL_LOCAL String8 ui_copy_string8_checked(Arena* arena, String8 source)
{
    String8 result = {0};
    if (arena && ui_string8_source_is_valid(source) && source.length != 0)
    {
        u64 position = arena->position;
        if (ui_arena_try_advance(arena, &position, source.length, BUSTER_ALIGN_OF(char8)))
        {
            result.pointer = arena_allocate(arena, char8, source.length);
            result.length = source.length;
            memcpy(result.pointer, source.pointer, source.length);
        }
    }

    return result;
}

BUSTER_GLOBAL_LOCAL SliceString8 ui_copy_string8_slice_checked(Arena* arena, SliceString8 sources)
{
    SliceString8 result = {0};
    if (arena && sources.pointer && sources.length != 0 && sources.length <= (u64)-1 / sizeof(*sources.pointer))
    {
        u64 position = arena->position;
        u64 descriptor_bytes = sources.length * sizeof(*sources.pointer);
        if (ui_arena_try_advance(arena, &position, descriptor_bytes, BUSTER_ALIGN_OF(String8)))
        {
            u64 aggregate_length = 0;
            for (u64 index = 0; index < sources.length; index += 1)
            {
                String8 source = sources.pointer[index];
                if (!ui_string8_source_is_valid(source) || source.length > (u64)-1 - aggregate_length ||
                    !ui_arena_try_advance(arena, &position, source.length, BUSTER_ALIGN_OF(char8)))
                {
                    return result;
                }
                aggregate_length += source.length;
            }

            result.pointer = arena_allocate(arena, String8, sources.length);
            result.length = sources.length;
            for (u64 index = 0; index < sources.length; index += 1)
            {
                result.pointer[index] = ui_copy_string8_checked(arena, sources.pointer[index]);
                if (sources.pointer[index].length != 0 && result.pointer[index].pointer == 0)
                {
                    BUSTER_CHECK(false);
                }
            }
        }
    }

    return result;
}

UI_Box* ui_root_from_state(UI_State* state)
{
    return state ? state->root : 0;
}

UI_EventNode* ui_event_list_push(Arena* arena, UI_EventList* list, UI_Event* event)
{
    UI_EventNode* node = arena_allocate(arena, UI_EventNode, 1);
    memset(node, 0, sizeof(*node));
    node->v = *event;
    if (node->v.string.length != 0)
    {
        node->v.string = ui_copy_string8_checked(arena, node->v.string);
    }
    if (node->v.paths.length != 0)
    {
        node->v.paths = ui_copy_string8_slice_checked(arena, node->v.paths);
    }

    if (!list->last)
    {
        list->first = list->last = node;
    }
    else
    {
        node->prev = list->last;
        list->last->next = node;
        list->last = node;
    }
    list->count += 1;
    return node;
}

void ui_eat_event_node(UI_EventList* list, UI_EventNode* node)
{
    if (node)
    {
        if (node->prev)
        {
            node->prev->next = node->next;
        }
        if (node->next)
        {
            node->next->prev = node->prev;
        }
        if (list->first == node)
        {
            list->first = node->next;
        }
        if (list->last == node)
        {
            list->last = node->prev;
        }
        if (list->count != 0)
        {
            list->count -= 1;
        }
    }
}

UI_EventIterator ui_event_iterator_initialize(UI_State* state)
{
    UI_EventIterator iterator = {
        .list = state->events,
        .current = state->events.first,
    };
    return iterator;
}

UI_Event* ui_next_event(UI_EventIterator* iterator)
{
    UI_Event* result = 0;
    UI_EventNode* current = iterator->current;
    if (current)
    {
        result = &current->v;
        iterator->current = current->next;
    }
    return result;
}

void ui_eat_event(UI_Event* event)
{
    if (event)
    {
        UI_EventNode* event_node = BUSTER_FIELD_PARENT_POINTER(UI_EventNode, v, event);
        ui_eat_event_node(&ui_state->events, event_node);
    }
}

UI_Key ui_key_zero(void)
{
    UI_Key result = {0};
    return result;
}

UI_Key ui_key_make(u64 value)
{
    UI_Key result = {value};
    return result;
}

BUSTER_GLOBAL_LOCAL String8 ui_hash_part_from_key_string(String8 string)
{
    String8 result = string;
    u64 triple_index = string_first_sequence(string, S8("###"));
    if (triple_index < string.length)
    {
        result = string_slice(string, triple_index + 3, string.length);
    }
    else
    {
        u64 double_index = string_first_sequence(string, S8("##"));
        if (double_index < string.length)
        {
            result = string_slice(string, double_index + 2, string.length);
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL String8 ui_display_part_from_key_string(String8 string)
{
    String8 result = string;
    u64 index = string_first_sequence(string, S8("##"));
    if (index < string.length)
    {
        result.length = index;
    }
    return result;
}

UI_Key ui_key_from_string(UI_Key seed, String8 string)
{
    UI_Key result = ui_key_zero();
    String8 hash_part = ui_hash_part_from_key_string(string);
    if (hash_part.length != 0)
    {
        u64 hash = seed.value ? seed.value : 5381;
        for (u64 i = 0; i < hash_part.length; i += 1)
        {
            hash = ((hash << 5) + hash) + (u8)hash_part.pointer[i];
        }
        result.value = hash;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL UI_Key ui_key_from_literal_string(UI_Key seed, String8 string)
{
    UI_Key result = ui_key_zero();
    if (string.pointer && string.length != 0)
    {
        u64 hash = seed.value ? seed.value : 5381;
        for (u64 index = 0; index < string.length; index += 1)
        {
            hash = ((hash << 5) + hash) + (u8)string.pointer[index];
        }
        result.value = hash;
    }
    return result;
}

bool ui_key_match(UI_Key a, UI_Key b)
{
    return a.value == b.value;
}

UI_BoxFlagInfo ui_box_flag_info(u32 bit_index)
{
    UI_BoxFlagInfo result = {0};
#define UI_BOX_FLAG_INFO(bit, flag_name, kind_name, dependency)                                                                                                  \
    case bit:                                                                                                                                                    \
        result.flag = UI_BoxFlag_##flag_name;                                                                                                                    \
        result.name = S8(#flag_name);                                                                                                                            \
        result.kind = UI_BoxFlagSupport_##kind_name;                                                                                                             \
        result.implemented = true;                                                                                                                               \
        result.renderer_dependency = dependency;                                                                                                                 \
        break
    switch (bit_index)
    {
        UI_BOX_FLAG_INFO(0, MouseClickable, Interaction, false);
        UI_BOX_FLAG_INFO(1, KeyboardClickable, Interaction, false);
        UI_BOX_FLAG_INFO(2, DropSite, Interaction, false);
        UI_BOX_FLAG_INFO(3, ClickToFocus, Interaction, false);
        UI_BOX_FLAG_INFO(4, Scroll, Interaction, false);
        UI_BOX_FLAG_INFO(5, ViewScrollX, Interaction, false);
        UI_BOX_FLAG_INFO(6, ViewScrollY, Interaction, false);
        UI_BOX_FLAG_INFO(7, ViewClampX, Interaction, false);
        UI_BOX_FLAG_INFO(8, ViewClampY, Interaction, false);
        UI_BOX_FLAG_INFO(9, FocusHot, Interaction, false);
        UI_BOX_FLAG_INFO(10, FocusActive, Interaction, false);
        UI_BOX_FLAG_INFO(11, FocusHotDisabled, Interaction, false);
        UI_BOX_FLAG_INFO(12, FocusActiveDisabled, Interaction, false);
        UI_BOX_FLAG_INFO(13, DefaultFocusNavX, Interaction, false);
        UI_BOX_FLAG_INFO(14, DefaultFocusNavY, Interaction, false);
        UI_BOX_FLAG_INFO(15, DefaultFocusEdit, Interaction, false);
        UI_BOX_FLAG_INFO(16, FocusNavSkip, Interaction, false);
        UI_BOX_FLAG_INFO(17, DisableTruncatedHover, Interaction, false);
        UI_BOX_FLAG_INFO(18, Disabled, Interaction, false);
        UI_BOX_FLAG_INFO(19, FloatingX, Layout, false);
        UI_BOX_FLAG_INFO(20, FloatingY, Layout, false);
        UI_BOX_FLAG_INFO(21, FixedWidth, Layout, false);
        UI_BOX_FLAG_INFO(22, FixedHeight, Layout, false);
        UI_BOX_FLAG_INFO(23, AllowOverflowX, Layout, false);
        UI_BOX_FLAG_INFO(24, AllowOverflowY, Layout, false);
        UI_BOX_FLAG_INFO(25, SkipViewOffX, Layout, false);
        UI_BOX_FLAG_INFO(26, SkipViewOffY, Layout, false);
        UI_BOX_FLAG_INFO(27, DrawDropShadow, Appearance, false);
        UI_BOX_FLAG_INFO(28, DrawBackgroundBlur, Appearance, false);
        UI_BOX_FLAG_INFO(29, DrawBackground, Appearance, false);
        UI_BOX_FLAG_INFO(30, DrawBorder, Appearance, false);
        UI_BOX_FLAG_INFO(31, DrawSideTop, Appearance, false);
        UI_BOX_FLAG_INFO(32, DrawSideBottom, Appearance, false);
        UI_BOX_FLAG_INFO(33, DrawSideLeft, Appearance, false);
        UI_BOX_FLAG_INFO(34, DrawSideRight, Appearance, false);
        UI_BOX_FLAG_INFO(35, DrawText, Appearance, false);
        UI_BOX_FLAG_INFO(36, DrawTextFastpathCodepoint, Appearance, false);
        UI_BOX_FLAG_INFO(37, DrawTextWeak, Appearance, false);
        UI_BOX_FLAG_INFO(38, DrawHotEffects, Appearance, false);
        UI_BOX_FLAG_INFO(39, DrawActiveEffects, Appearance, false);
        UI_BOX_FLAG_INFO(40, DrawOverlay, Appearance, false);
        UI_BOX_FLAG_INFO(41, DrawBucket, Appearance, true);
        UI_BOX_FLAG_INFO(42, DrawFadeTop, Appearance, false);
        UI_BOX_FLAG_INFO(43, DrawFadeBottom, Appearance, false);
        UI_BOX_FLAG_INFO(44, DrawFadeLeft, Appearance, false);
        UI_BOX_FLAG_INFO(45, DrawFadeRight, Appearance, false);
        UI_BOX_FLAG_INFO(46, Clip, Appearance, true);
        UI_BOX_FLAG_INFO(47, AnimatePosX, Appearance, false);
        UI_BOX_FLAG_INFO(48, AnimatePosY, Appearance, false);
        UI_BOX_FLAG_INFO(49, DisableTextTrunc, Appearance, false);
        UI_BOX_FLAG_INFO(50, DisableIDString, Appearance, false);
        UI_BOX_FLAG_INFO(51, DisableFocusBorder, Appearance, false);
        UI_BOX_FLAG_INFO(52, DisableFocusOverlay, Appearance, false);
        UI_BOX_FLAG_INFO(53, HasDisplayString, Appearance, false);
        UI_BOX_FLAG_INFO(54, HasFuzzyMatchRanges, Appearance, false);
        UI_BOX_FLAG_INFO(55, RoundChildrenByParent, Appearance, true);
        UI_BOX_FLAG_INFO(56, SquishAnchored, Appearance, false);
        case 63:
            result.flag = UI_BoxFlag_Debug;
            result.name = S8("Debug");
            result.kind = UI_BoxFlagSupport_Debug;
            result.implemented = true;
            result.renderer_dependency = false;
            break;
        default:
            break;
    }
#undef UI_BOX_FLAG_INFO
    return result;
}

UI_Size ui_size(UI_SizeKind kind, f32 value, f32 strictness)
{
    UI_Size result = {.kind = kind, .value = value, .strictness = strictness};
    return result;
}

UI_Size ui_pixels(u32 width, f32 strictness)
{
    return ui_size(UI_SizeKind_Pixels, (f32)width, strictness);
}

UI_Size ui_percentage(f32 percentage, f32 strictness)
{
    return ui_size(UI_SizeKind_ParentPct, percentage, strictness);
}

UI_Size ui_em(f32 value, f32 strictness)
{
    f32 font_size = ui_top_font_size();
    BUSTER_CHECK(font_size != 0);
    return ui_size(UI_SizeKind_Pixels, value * font_size, strictness);
}

UI_Size ui_text_dim(f32 padding, f32 strictness)
{
    return ui_size(UI_SizeKind_TextContent, padding, strictness);
}

UI_Size ui_children_sum(f32 strictness)
{
    return ui_size(UI_SizeKind_ChildrenSum, 0.0f, strictness);
}

UI_State* ui_state_allocate(RenderingHandle* rendering, RenderingWindowHandle* window)
{
    Arena* arena = arena_create((ArenaCreation){
        .reserved_size = BUSTER_GB(8),
        .granularity = BUSTER_MB(2),
        .initial_size = BUSTER_MB(2),
    });
    UI_State* state = arena_allocate(arena, UI_State, 1);
    state->arena = arena;
    state->rendering = rendering;
    state->rendering_window = window;
    state->box_table_size = 4096;
    state->box_table = arena_allocate(arena, UI_BoxHashSlot, state->box_table_size);
    state->active_box_capacity = state->box_table_size;
    state->active_boxes = arena_allocate(arena, UI_Box*, state->active_box_capacity);

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
        .child_layout_axis = AXIS2_Y,
        .fixed_x = 0,
        .fixed_y = 0,
        .fixed_width = 0,
        .fixed_height = 0,
        .pref_width = {.kind = UI_SizeKind_Null, .value = 0, .strictness = 1},
        .pref_height = {.kind = UI_SizeKind_Null, .value = 0, .strictness = 1},
        .min_width = 0,
        .min_height = 0,
        .flags = (UI_BoxFlags){0},
        .background_color = float4_make(0.12f, 0.12f, 0.14f, 1.0f),
        .text_color = float4_make(0.85f, 0.86f, 0.88f, 1.0f),
        .border_color = float4_make(0.22f, 0.23f, 0.26f, 1.0f),
        .font_size = 12.0f,
        .text_padding = 4.0f,
        .text_alignment = UI_TextAlign_Left,
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

BUSTER_GLOBAL_LOCAL u64 ui_box_slot_from_key(UI_Key key)
{
    BUSTER_CHECK(ui_state->box_table_size != 0);
    return key.value & (ui_state->box_table_size - 1);
}

UI_Box* ui_box_from_key(UI_Key key)
{
    UI_Box* result = 0;
    if (!ui_key_match(key, ui_key_zero()))
    {
        u64 slot_index = ui_box_slot_from_key(key);
        for (UI_Box* box = ui_state->box_table[slot_index].first; box; box = box->hash_next)
        {
            if (ui_key_match(box->key, key))
            {
                result = box;
                break;
            }
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL void ui_box_hash_push(UI_Box* box)
{
    u64 slot_index = ui_box_slot_from_key(box->key);
    UI_BoxHashSlot* slot = &ui_state->box_table[slot_index];
    if (!slot->last)
    {
        slot->first = slot->last = box;
    }
    else
    {
        slot->last->hash_next = box;
        box->hash_prev = slot->last;
        slot->last = box;
    }
    BUSTER_CHECK(ui_state->box_count != UINT64_MAX);
    ui_state->box_count += 1;
}

BUSTER_GLOBAL_LOCAL void ui_box_hash_remove(UI_Box* box, u64 slot_index)
{
    UI_BoxHashSlot* slot = &ui_state->box_table[slot_index];
    if (box->hash_prev)
    {
        box->hash_prev->hash_next = box->hash_next;
    }
    if (box->hash_next)
    {
        box->hash_next->hash_prev = box->hash_prev;
    }
    if (slot->first == box)
    {
        slot->first = box->hash_next;
    }
    if (slot->last == box)
    {
        slot->last = box->hash_prev;
    }
    box->hash_next = 0;
    box->hash_prev = 0;
    BUSTER_CHECK(ui_state->box_count != 0);
    ui_state->box_count -= 1;
}

BUSTER_GLOBAL_LOCAL void ui_box_equip_tree_links(UI_Box* box, UI_Box* parent)
{
    box->parent = parent;
    box->build_order = ui_state->next_build_order;
    box->draw_order = ui_state->next_draw_order;
    ui_state->next_build_order += 1;
    ui_state->next_draw_order += 1;
    if (parent)
    {
        if (!parent->last)
        {
            parent->first = parent->last = box;
        }
        else
        {
            parent->last->next = box;
            box->prev = parent->last;
            parent->last = box;
        }
        parent->child_count += 1;
    }
    else
    {
        ui_state->root = box;
    }
}

UI_Box* ui_build_box_from_key(UI_BoxFlags flags, UI_Key key)
{
    UI_Box* box = 0;
    bool box_is_new = false;
    bool key_is_zero = ui_key_match(key, ui_key_zero());
    if (!key_is_zero)
    {
        box = ui_box_from_key(key);
        if (box && box->last_touched_build_index == ui_state->build_index)
        {
            key = ui_key_zero();
            key_is_zero = true;
            box = 0;
        }
    }

    if (!box)
    {
        if (key_is_zero)
        {
            box = arena_allocate(ui_build_arena(), UI_Box, 1);
            box_is_new = true;
        }
        else if (ui_state->first_free_box)
        {
            box = ui_state->first_free_box;
            ui_state->first_free_box = ui_state->first_free_box->next;
            memset(box, 0, sizeof(*box));
            box_is_new = true;
            box->first_touched_build_index = ui_state->build_index;
            box->key = key;
            ui_box_hash_push(box);
        }
        else
        {
            box = arena_allocate(ui_state->arena, UI_Box, 1);
            box_is_new = true;
            memset(box, 0, sizeof(*box));
            box->first_touched_build_index = ui_state->build_index;
            box->key = key;
            ui_box_hash_push(box);
        }
    }

    UI_Box* hash_next = (box_is_new && key_is_zero) ? 0 : box->hash_next;
    UI_Box* hash_prev = (box_is_new && key_is_zero) ? 0 : box->hash_prev;
    UI_Key old_key = box_is_new ? key : box->key;
    u64 first_touched = box_is_new ? ui_state->build_index : (box->first_touched_build_index ? box->first_touched_build_index : ui_state->build_index);
    f32 hot_t = box_is_new ? 0.0f : box->hot_t;
    f32 active_t = box_is_new ? 0.0f : box->active_t;
    float2 view_off = box_is_new ? float2_make(0, 0) : box->view_off;
    float2 view_bounds = box_is_new ? float2_make(0, 0) : box->view_bounds;
    F32Interval2 previous_rect = box_is_new ? (F32Interval2){0} : box->rect;
    F32Interval2 previous_clip_rect = box_is_new ? (F32Interval2){0} : box->clip_rect;
    bool previous_visible = !box_is_new && box->visible;
    bool previous_text_truncated = !box_is_new && box->text_truncated;
    u64 previous_text_visible_length = box_is_new ? 0 : box->text_visible_length;
    bool has_previous_rect = !box_is_new && box->last_touched_build_index + 1 == ui_state->build_index;

    memset(box, 0, sizeof(*box));

    box->hash_next = hash_next;
    box->hash_prev = hash_prev;
    box->key = key_is_zero ? ui_key_zero() : old_key;
    box->first_touched_build_index = first_touched;
    box->last_touched_build_index = ui_state->build_index;
    box->hot_t = hot_t;
    box->active_t = active_t;
    box->view_off = view_off;
    box->view_bounds = view_bounds;
    box->rect = previous_rect;
    box->clip_rect = previous_clip_rect;
    box->visible = previous_visible;
    box->text_truncated = previous_text_truncated;
    box->text_visible_length = previous_text_visible_length;
    box->has_previous_rect = has_previous_rect;

    UI_Box* parent = ui_top_parent();
    ui_box_equip_tree_links(box, parent);

    UI_BoxFlags stack_flags = ui_top_flags();
    box->flags = flags | stack_flags;
    if (parent && (parent->flags & UI_BoxFlag_Disabled))
    {
        box->flags |= UI_BoxFlag_Disabled;
    }
    box->fixed_position = float2_make(ui_top_fixed_x(), ui_top_fixed_y());
    box->fixed_size = float2_make(ui_top_fixed_width(), ui_top_fixed_height());
    box->min_size = float2_make(ui_top_min_width(), ui_top_min_height());
    box->pref_size[AXIS2_X] = ui_top_pref_width();
    box->pref_size[AXIS2_Y] = ui_top_pref_height();
    box->child_layout_axis = ui_top_child_layout_axis();
    box->background_color = ui_top_background_color();
    box->text_color = ui_top_text_color();
    box->border_color = ui_top_border_color();
    box->background_blur_radius = UI_BACKGROUND_BLUR_RADIUS_DEFAULT;
    box->font_size = ui_top_font_size();
    box->text_padding = ui_top_text_padding();
    box->text_align = ui_top_text_alignment();
    box->draw_pass = (box->flags & UI_BoxFlag_DrawBucket) ? 1u : 0u;
    box->renderer_dependency_flags = 0;
    if (box->flags & UI_BoxFlag_DrawBackgroundBlur)
    {
        box->renderer_dependency_flags |= UI_BoxRendererDependency_BackgroundBlur;
    }
    if (box->flags & UI_BoxFlag_DrawBucket)
    {
        box->renderer_dependency_flags |= UI_BoxRendererDependency_BucketSubmission;
    }
    if (box->flags & UI_BoxFlag_RoundChildrenByParent)
    {
        box->renderer_dependency_flags |= UI_BoxRendererDependency_CornerRadii;
    }
    if (box->flags & UI_BoxFlag_Clip)
    {
        box->renderer_dependency_flags |= UI_BoxRendererDependency_TextScissor;
    }
    if (box->renderer_dependency_flags)
    {
        box->state_flags |= UI_BoxState_RendererDependency;
    }
    if (box->flags & UI_BoxFlag_Scroll)
    {
        box->state_flags |= UI_BoxState_Scrollable;
    }
    for (u64 i = 0; i < BUSTER_ARRAY_LENGTH(box->corner_radii); i += 1)
    {
        box->corner_radii[i] = 3.0f;
    }

    if (ui_state->stacks.fixed_width_length != 0)
    {
        box->flags |= UI_BoxFlag_FixedWidth;
    }
    if (ui_state->stacks.fixed_height_length != 0)
    {
        box->flags |= UI_BoxFlag_FixedHeight;
    }

    ui_stack_auto_pop_all(ui_state);
    return box;
}

UI_Box* ui_build_box_from_string(UI_BoxFlags flags, String8 string)
{
    UI_BoxFlags effective_flags = flags | ui_top_flags();
    String8 key_string = string;
    UI_Key key = (effective_flags & UI_BoxFlag_DisableIDString) ? ui_key_from_literal_string(ui_key_zero(), key_string)
                                                                : ui_key_from_string(ui_key_zero(), key_string);
    UI_Box* box = ui_build_box_from_key(flags, key);
    box->raw_string = string.length != 0 ? string_duplicate_arena(ui_build_arena(), string, false) : string;
    box->display_string = (effective_flags & UI_BoxFlag_DisableIDString) ? ui_utf8_sanitize(ui_build_arena(), string)
                                                                         : ui_utf8_sanitize(ui_build_arena(), ui_display_part_from_key_string(string));
    box->string = box->display_string;
    return box;
}

UI_Box* ui_build_box_from_stringf(UI_BoxFlags flags, String8 format, ...)
{
    Arena* arena = ui_build_arena();
    va_list args;
    va_start(args, format);
    String8 string = string_format_va(arena, format, args, STRING_FORMAT_VA_GP_SLOTS(3));
    va_end(args);
    UI_Box* result = ui_build_box_from_string(flags, string);
    return result;
}

UI_Box* ui_box_make(UI_BoxFlags flags, String8 string)
{
    return ui_build_box_from_string(flags, string);
}

UI_Box* ui_box_make_format(UI_BoxFlags flags, String8 format, ...)
{
    Arena* arena = ui_build_arena();
    va_list args;
    va_start(args, format);
    String8 string = string_format_va(arena, format, args, STRING_FORMAT_VA_GP_SLOTS(3));
    va_end(args);
    return ui_box_make(flags, string);
}

void ui_box_set_display_string(UI_Box* box, String8 string)
{
    if (box)
    {
        String8 display_string = ui_utf8_sanitize(ui_build_arena(), string);
        box->display_string = display_string;
        box->string = display_string;
        box->flags |= UI_BoxFlag_HasDisplayString;
    }
}

void ui_box_set_fastpath_codepoint(UI_Box* box, u32 codepoint)
{
    if (box)
    {
        bool valid = codepoint <= 0x10ffffu && !(codepoint >= 0xd800u && codepoint <= 0xdfffu);
        box->fastpath_codepoint = valid ? codepoint : 0;
    }
}

void ui_box_set_fuzzy_match_ranges(UI_Box* box, UI_FuzzyMatchRange* ranges, u64 count)
{
    if (box)
    {
        if (!ranges || count == 0)
        {
            box->fuzzy_match_ranges = 0;
            box->fuzzy_match_range_count = 0;
            box->flags &= ~UI_BoxFlag_HasFuzzyMatchRanges;
            return;
        }

        bool ranges_are_valid = count <= (u64)-1 / sizeof(*ranges);
        for (u64 index = 0; ranges_are_valid && index < count; index += 1)
        {
            ranges_are_valid = ranges[index].one_past_last >= ranges[index].first;
        }

        Arena* arena = ui_build_arena();
        u64 byte_count = ranges_are_valid ? count * sizeof(*ranges) : 0;
        u64 position = arena ? arena->position : 0;
        bool enough_arena = ranges_are_valid && ui_arena_try_advance(arena, &position, byte_count, BUSTER_ALIGN_OF(UI_FuzzyMatchRange));
        if (enough_arena)
        {
            UI_FuzzyMatchRange* copied_ranges = arena_allocate(arena, UI_FuzzyMatchRange, count);
            memcpy(copied_ranges, ranges, byte_count);
            box->fuzzy_match_ranges = copied_ranges;
            box->fuzzy_match_range_count = count;
            box->flags |= UI_BoxFlag_HasFuzzyMatchRanges;
        }
        else if (!box->fuzzy_match_ranges || box->fuzzy_match_range_count == 0)
        {
            box->fuzzy_match_ranges = 0;
            box->fuzzy_match_range_count = 0;
            box->flags &= ~UI_BoxFlag_HasFuzzyMatchRanges;
        }
        else
        {
            // A failed replacement leaves the previous complete copy intact.
            // No arena position or box range metadata is changed on failure.
            if (box->fuzzy_match_range_count != 0)
            {
                box->flags |= UI_BoxFlag_HasFuzzyMatchRanges;
            }
        }
    }
}

void ui_box_set_background_blur_radius(UI_Box* box, u32 radius)
{
    if (box)
    {
        box->background_blur_radius = ui_background_blur_radius_clamp(radius);
    }
}

void ui_box_set_corner_radii(UI_Box* box, f32 radius)
{
    if (box)
    {
        for (u64 index = 0; index < BUSTER_ARRAY_LENGTH(box->corner_radii); index += 1)
        {
            box->corner_radii[index] = BUSTER_MAX(0.0f, radius);
        }
    }
}

bool ui_box_is_visible(UI_Box* box)
{
    return box && box->visible;
}

bool ui_box_is_focused(UI_Box* box)
{
    return box && (box->state_flags & (UI_BoxState_FocusHot | UI_BoxState_FocusActive | UI_BoxState_FocusEdit));
}

bool ui_box_has_tooltip(UI_Box* box)
{
    return box && box->tooltip_visible;
}

void ui_box_scroll_by(UI_Box* box, float2 delta)
{
    if (box)
    {
        if (box->flags & UI_BoxFlag_ViewScrollX)
        {
            float2_element(box->view_off, AXIS2_X) += float2_element(delta, AXIS2_X);
        }
        if (box->flags & UI_BoxFlag_ViewScrollY)
        {
            float2_element(box->view_off, AXIS2_Y) += float2_element(delta, AXIS2_Y);
        }
    }
}

float2 ui_box_scroll_offset(UI_Box* box)
{
    return box ? box->view_off : float2_make(0.0f, 0.0f);
}

UI_BoxFlags ui_box_flags_from_box_flags(UI_BoxFlags flags)
{
    return flags;
}

UI_BoxRec ui_box_rec_df(UI_Box* box, UI_Box* root, u64 sibling_offset, u64 child_offset)
{
    UI_BoxRec result = {0};
    if (box)
    {
        UI_Box* child = *(UI_Box**)((u8*)box + child_offset);
        if (child)
        {
            result.next = child;
            result.push_count = 1;
        }
        else
        {
            for (UI_Box* p = box; p && p != root; p = p->parent)
            {
                UI_Box* sibling = *(UI_Box**)((u8*)p + sibling_offset);
                if (sibling)
                {
                    result.next = sibling;
                    break;
                }
                result.pop_count += 1;
            }
        }
    }

    return result;
}

BUSTER_GLOBAL_LOCAL void ui_prune_untouched_boxes(void)
{
    if (ui_state->active_box_capacity < ui_state->box_count)
    {
        u64 capacity = ui_state->active_box_capacity ? ui_state->active_box_capacity : 1;
        while (capacity < ui_state->box_count)
        {
            BUSTER_CHECK(capacity <= UINT64_MAX / 2);
            capacity *= 2;
        }
        // Event routing has already consumed the preceding frame's list, so
        // growth does not need to copy entries that this pass will replace.
        ui_state->active_boxes = arena_allocate(ui_state->arena, UI_Box*, capacity);
        ui_state->active_box_capacity = capacity;
    }

    u64 active_box_count = 0;
    for (u64 slot_index = 0; slot_index < ui_state->box_table_size; slot_index += 1)
    {
        UI_BoxHashSlot* slot = &ui_state->box_table[slot_index];
        for (UI_Box *box = slot->first, *next = 0; box; box = next)
        {
            next = box->hash_next;
            if (box->last_touched_build_index < ui_state->build_index)
            {
                ui_box_hash_remove(box, slot_index);
                box->next = ui_state->first_free_box;
                ui_state->first_free_box = box;
            }
            else
            {
                BUSTER_CHECK(active_box_count < ui_state->active_box_capacity);
                ui_state->active_boxes[active_box_count] = box;
                active_box_count += 1;
            }
        }
    }
    BUSTER_CHECK(active_box_count == ui_state->box_count);
    ui_state->active_box_count = active_box_count;
}

UI_EventList ui_event_list_from_wm_events(Arena* arena, WmWindowHandle* window, WmEventList event_queue)
{
    UI_EventList result = {0};
    for (WmEvent* event = event_queue.first; event; event = event->next)
    {
        if (event->window && event->window != window)
        {
            continue;
        }

        UI_Event ui_event = {0};
        ui_event.key = event->key;
        ui_event.modifiers = event->modifiers;
        ui_event.pos = float2_make((f32)event->position.x, (f32)event->position.y);

        switch (event->kind)
        {
        case WM_EVENT_KEY_PRESS:
        {
            ui_event.kind = UI_EventKind_Press;
        }
        break;
        case WM_EVENT_KEY_RELEASE:
        {
            ui_event.kind = UI_EventKind_Release;
        }
        break;
        case WM_EVENT_BUTTON_PRESS:
        {
            if (event->key == WM_KEY_MOUSE_WHEEL_UP || event->key == WM_KEY_MOUSE_WHEEL_DOWN || event->key == WM_KEY_MOUSE_WHEEL_LEFT ||
                event->key == WM_KEY_MOUSE_WHEEL_RIGHT)
            {
                ui_event.kind = UI_EventKind_Scroll;
                if (event->key == WM_KEY_MOUSE_WHEEL_UP)
                {
                    ui_event.delta = float2_make(0.0f, 1.0f);
                }
                else if (event->key == WM_KEY_MOUSE_WHEEL_DOWN)
                {
                    ui_event.delta = float2_make(0.0f, -1.0f);
                }
                else if (event->key == WM_KEY_MOUSE_WHEEL_LEFT)
                {
                    ui_event.delta = float2_make(-1.0f, 0.0f);
                }
                else if (event->key == WM_KEY_MOUSE_WHEEL_RIGHT)
                {
                    ui_event.delta = float2_make(1.0f, 0.0f);
                }
            }
            else
            {
                ui_event.kind = UI_EventKind_Press;
            }
        }
        break;
        case WM_EVENT_BUTTON_RELEASE:
        {
            ui_event.kind = UI_EventKind_Release;
        }
        break;
        case WM_EVENT_TEXT_INPUT:
        {
            ui_event.kind = UI_EventKind_Text;
            ui_event.string = event->text;
        }
        break;
        case WM_EVENT_FILE_DROP:
        {
            ui_event.kind = UI_EventKind_FileDrop;
            ui_event.paths = event->paths;
        }
        break;
        case WM_EVENT_MOUSE_MOVE:
        {
            ui_event.kind = UI_EventKind_MouseMove;
        }
        break;
        default:
            break;
        }

        if (ui_event.kind != UI_EventKind_Null)
        {
            ui_event_list_push(arena, &result, &ui_event);
        }
    }
    return result;
}

void ui_build_begin(WmHandle* windowing, WmWindowHandle* window, f64 frame_time, UI_EventList events)
{
    BUSTER_CHECK(ui_state != 0);

    ui_state->build_index += 1;
    arena_reset_to_start(ui_build_arena());
    ui_stack_reset(ui_state);
    ui_state->previous_root = ui_state->root;
    ui_state->root = 0;
    if (ui_state->focus_changed_pending && ui_state->focus_changed_delivery_build_index < ui_state->build_index)
    {
        ui_state->focus_changed_pending = 0;
        ui_state->focus_changed_key = ui_key_zero();
        ui_state->focus_changed_delivery_build_index = 0;
    }
    ui_state->previous_mouse = ui_state->mouse;
    ui_state->next_build_order = 0;
    ui_state->next_draw_order = 0;
    ui_state->focus_changed = 0;
    ui_state->focus_hot_key = ui_key_zero();
    ui_state->windowing = windowing;
    ui_state->window = window;
    ui_state->events = events;
    ui_state->frame_time = frame_time;
    ui_state->is_animating = 0;
    // Keep the previous completed pointer position while the router walks
    // events chronologically.  The final pointer position is published only
    // after ownership has been assigned, so an earlier event cannot observe a
    // later move.
    ui_state->mouse = ui_state->previous_mouse;

    bool has_active = false;
    for (u64 i = 0; i < (u64)UI_MouseButtonKind_COUNT; i += 1)
    {
        has_active = has_active || !ui_key_match(ui_state->active_box_key[i], ui_key_zero());
    }
    if (!has_active)
    {
        ui_state->hot_box_key = ui_key_zero();
    }

    for (u64 i = 0; i < (u64)UI_MouseButtonKind_COUNT; i += 1)
    {
        if (!ui_key_match(ui_state->active_box_key[i], ui_key_zero()))
        {
            UI_Box* active_box = ui_box_from_key(ui_state->active_box_key[i]);
            if (!active_box || (active_box->flags & UI_BoxFlag_Disabled) || !(active_box->flags & UI_BoxFlag_MouseClickable))
            {
                ui_state->active_box_key[i] = ui_key_zero();
            }
        }
    }

    ui_route_event_owners();

    for (UI_EventNode* event = ui_state->events.first; event; event = event->next)
    {
        bool is_mouse = false;
        ui_mouse_button_kind_from_key(event->v.key, &is_mouse);
        if (event->v.kind == UI_EventKind_MouseMove || event->v.kind == UI_EventKind_Scroll ||
            (is_mouse && (event->v.kind == UI_EventKind_Press || event->v.kind == UI_EventKind_Release)))
        {
            ui_state->mouse = event->v.pos;
        }
    }

    RenderingWindowSize framebuffer_dimensions = {.width = 800, .height = 600};
    if (ui_state->rendering_window)
    {
        framebuffer_dimensions = rendering_window_get_size(ui_state->rendering_window);
    }
    f32 width = (f32)framebuffer_dimensions.width;
    f32 height = (f32)framebuffer_dimensions.height;

    ui_set_next_fixed_width(width);
    ui_set_next_fixed_height(height);
    ui_set_next_child_layout_axis(AXIS2_Y);
    UI_Box* root = ui_build_box_from_stringf((UI_BoxFlags){0}, S8("###window_root_{u64:x}"), (u64)window);
    root->fixed_position = float2_make(0.0f, 0.0f);
    root->fixed_size = float2_make(width, height);
    root->rect = ui_rect_make(0.0f, 0.0f, width, height);
    root->flags |= UI_BoxFlag_FixedSize;
    ui_state->root = root;

    ui_push_parent(root);
    ui_push_font_size(12.0f);
    ui_push_text_padding(4.0f);
    ui_push_text_color(float4_make(0.86f, 0.87f, 0.90f, 1.0f));
    ui_push_background_color(float4_make(0.13f, 0.13f, 0.15f, 1.0f));
    ui_push_border_color(float4_make(0.25f, 0.25f, 0.30f, 1.0f));
    ui_push_pref_width(ui_percentage(1.0f, 0.0f));
    ui_push_pref_height(ui_percentage(1.0f, 0.0f));
    ui_push_child_layout_axis(AXIS2_Y);
}

BUSTER_GLOBAL_LOCAL f32 ui_text_size_for_axis(UI_Box* box, Axis2 axis)
{
    f32 result = 0.0f;
    f32 padding = box->pref_size[axis].value + box->text_padding * 2.0f;
    if (axis == AXIS2_X)
    {
        result = (f32)ui_utf8_codepoint_count(box->string) * box->font_size * 0.60f + padding;
    }
    else
    {
        result = box->font_size * 1.35f + padding;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL void ui_calc_sizes_standalone(UI_Box* box, Axis2 axis)
{
    for (UI_Box* b = box; b; b = ui_box_rec_df_pre(b, box).next)
    {
        switch (b->pref_size[axis].kind)
        {
        default:
            break;
        case UI_SizeKind_Pixels:
        {
            float2_element(b->fixed_size, axis) = floor_f32(b->pref_size[axis].value);
        }
        break;
        case UI_SizeKind_TextContent:
        {
            float2_element(b->fixed_size, axis) = floor_f32(ui_text_size_for_axis(b, axis));
        }
        break;
        }
    }
}

BUSTER_GLOBAL_LOCAL void ui_calc_sizes_upwards_dependent(UI_Box* box, Axis2 axis)
{
    for (UI_Box* b = box; b; b = ui_box_rec_df_pre(b, box).next)
    {
        if (b->pref_size[axis].kind == UI_SizeKind_ParentPct && !(b->flags & (UI_BoxFlag_FixedWidth << axis)))
        {
            UI_Box* parent = b->parent;
            if (parent)
            {
                f32 percentage = BUSTER_CLAMP(0.0f, b->pref_size[axis].value, 1.0f);
                float2_element(b->fixed_size, axis) = floor_f32(float2_element(parent->fixed_size, axis) * percentage);
            }
        }
    }
}

BUSTER_GLOBAL_LOCAL void ui_calc_sizes_downwards_dependent(UI_Box* root, Axis2 axis)
{
    UI_BoxRec rec = {0};
    for (UI_Box* box = root; box; box = rec.next)
    {
        rec = ui_box_rec_df_pre(box, root);
        s32 pop_idx = 0;
        for (UI_Box* b = box; b && pop_idx <= rec.pop_count; b = b->parent, pop_idx += 1)
        {
            if (b->pref_size[axis].kind == UI_SizeKind_ChildrenSum && !(b->flags & (UI_BoxFlag_FixedWidth << axis)))
            {
                f32 sum = 0.0f;
                for (UI_Box* child = b->first; child; child = child->next)
                {
                    if (!(child->flags & (UI_BoxFlag_FloatingX << axis)))
                    {
                        if (axis == b->child_layout_axis)
                        {
                            sum += float2_element(child->fixed_size, axis);
                        }
                        else
                        {
                            sum = BUSTER_MAX(sum, float2_element(child->fixed_size, axis));
                        }
                    }
                }
                float2_element(b->fixed_size, axis) = floor_f32(sum);
            }
        }
    }
}

BUSTER_GLOBAL_LOCAL void ui_layout_enforce_constraints(UI_Box* root, Axis2 axis)
{
    for (UI_Box* box = root; box; box = ui_box_rec_df_pre(box, root).next)
    {
        if (!(box->flags & (UI_BoxFlag_AllowOverflowX << axis)))
        {
            f32 allowed_size = float2_element(box->fixed_size, axis);
            if (axis != box->child_layout_axis)
            {
                // Cross-axis children overlap in the layout axis, so each
                // flexible child must fit the parent's constraint on its own.
                // Sharing one violation budget across siblings leaves every
                // child wider than the available cross-axis extent.
                for (UI_Box* child = box->first; child; child = child->next)
                {
                    if (!(child->flags & (UI_BoxFlag_FloatingX << axis)))
                    {
                        f32 child_size = float2_element(child->fixed_size, axis);
                        f32 strictness = BUSTER_CLAMP(0.0f, child->pref_size[axis].strictness, 1.0f);
                        f32 flexible_budget = child_size * (1.0f - strictness);
                        f32 violation = child_size - allowed_size;
                        if (violation > 0.0f && flexible_budget > 0.0f)
                        {
                            float2_element(child->fixed_size, axis) = child_size - BUSTER_MIN(violation, flexible_budget);
                        }
                    }
                }
            }
            else
            {
                f32 total_size = 0.0f;
                f32 total_weighted_size = 0.0f;
                for (UI_Box* child = box->first; child; child = child->next)
                {
                    if (!(child->flags & (UI_BoxFlag_FloatingX << axis)))
                    {
                        total_size += float2_element(child->fixed_size, axis);
                        f32 strictness = BUSTER_CLAMP(0.0f, child->pref_size[axis].strictness, 1.0f);
                        total_weighted_size += float2_element(child->fixed_size, axis) * (1.0f - strictness);
                    }
                }

                f32 violation = total_size - allowed_size;
                if (violation > 0.0f && total_weighted_size > 0.0f)
                {
                    for (UI_Box* child = box->first; child; child = child->next)
                    {
                        if (!(child->flags & (UI_BoxFlag_FloatingX << axis)))
                        {
                            f32 strictness = BUSTER_CLAMP(0.0f, child->pref_size[axis].strictness, 1.0f);
                            f32 fixup_budget = float2_element(child->fixed_size, axis) * (1.0f - strictness);
                            f32 fixup_pct = BUSTER_CLAMP(0.0f, violation / total_weighted_size, 1.0f);
                            float2_element(child->fixed_size, axis) -= fixup_budget * fixup_pct;
                        }
                    }
                }
            }
        }

        for (UI_Box* child = box->first; child; child = child->next)
        {
            float2_element(child->fixed_size, axis) = BUSTER_MAX(float2_element(child->fixed_size, axis), float2_element(child->min_size, axis));
        }
    }
}

BUSTER_GLOBAL_LOCAL void ui_layout_position(UI_Box* root, Axis2 axis)
{
    for (UI_Box* box = root; box; box = ui_box_rec_df_pre(box, root).next)
    {
        if (!box->parent)
        {
            float2_element(box->rect.p0, axis) = float2_element(box->fixed_position, axis);
            float2_element(box->rect.p1, axis) = float2_element(box->rect.p0, axis) + float2_element(box->fixed_size, axis);
        }

        f32 layout_position = 0.0f;
        f32 bounds = 0.0f;
        for (UI_Box* child = box->first; child; child = child->next)
        {
            f32 child_pos = 0.0f;
            if (child->flags & (UI_BoxFlag_FloatingX << axis))
            {
                child_pos = float2_element(child->fixed_position, axis);
            }
            else if (axis == box->child_layout_axis)
            {
                child_pos = layout_position;
                layout_position += float2_element(child->fixed_size, axis);
            }
            else
            {
                child_pos = 0.0f;
            }

            f32 old_p0 = float2_element(child->rect.p0, axis);
            f32 view_offset = 0.0f;
            if (!(child->flags & (UI_BoxFlag_SkipViewOffX << axis)))
            {
                view_offset = float2_element(box->view_off, axis);
            }
            f32 desired_p0 = floor_f32(float2_element(box->rect.p0, axis) + child_pos - view_offset);
            if ((child->flags & (UI_BoxFlag_AnimatePosX << axis)) && child->has_previous_rect)
            {
                // ui_build_begin receives milliseconds from the window loop.
                f32 animation_t = ui_state->frame_time > 0.0 ? BUSTER_CLAMP(0.0f, (f32)(ui_state->frame_time * 0.001 * 12.0), 1.0f) : 1.0f;
                desired_p0 = floor_f32(old_p0 + (desired_p0 - old_p0) * animation_t);
                ui_state->is_animating = ui_state->is_animating || desired_p0 != floor_f32(float2_element(box->rect.p0, axis) + child_pos - view_offset);
            }
            float2_element(child->rect.p0, axis) = desired_p0;
            float2_element(child->rect.p1, axis) = floor_f32(desired_p0 + float2_element(child->fixed_size, axis));
            float2_element(child->position_delta, axis) = desired_p0 - old_p0;

            if ((box->flags & UI_BoxFlag_SquishAnchored) && (child->flags & (UI_BoxFlag_FloatingX << axis)) &&
                !(box->flags & (UI_BoxFlag_AllowOverflowX << axis)))
            {
                f32 parent_start = float2_element(box->rect.p0, axis);
                f32 parent_end = float2_element(box->rect.p1, axis);
                f32 child_size = float2_element(child->fixed_size, axis);
                f32 clamped_p0 = BUSTER_CLAMP(parent_start, float2_element(child->rect.p0, axis), BUSTER_MAX(parent_start, parent_end - child_size));
                float2_element(child->rect.p0, axis) = clamped_p0;
                float2_element(child->rect.p1, axis) = clamped_p0 + child_size;
            }
            bounds = BUSTER_MAX(bounds, child_pos + float2_element(child->fixed_size, axis));
        }
        float2_element(box->view_bounds, axis) = bounds;
    }
}

BUSTER_GLOBAL_LOCAL void ui_layout_root(UI_Box* root, Axis2 axis)
{
    ui_calc_sizes_standalone(root, axis);
    ui_calc_sizes_upwards_dependent(root, axis);
    ui_calc_sizes_downwards_dependent(root, axis);
    ui_layout_enforce_constraints(root, axis);
    ui_layout_position(root, axis);

    bool changed = false;
    for (UI_Box* box = root; box; box = ui_box_rec_df_pre(box, root).next)
    {
        f32 size = float2_element(box->rect.p1, axis) - float2_element(box->rect.p0, axis);
        f32 content = float2_element(box->view_bounds, axis);
        f32 maximum = BUSTER_MAX(0.0f, content - size);
        if (box->flags & (UI_BoxFlag_ViewClampX << axis))
        {
            f32 old_offset = float2_element(box->view_off, axis);
            f32 new_offset = BUSTER_CLAMP(0.0f, old_offset, maximum);
            float2_element(box->view_off, axis) = new_offset;
            changed = changed || old_offset != new_offset;
        }
    }
    if (changed)
    {
        ui_layout_position(root, axis);
    }
}

BUSTER_GLOBAL_LOCAL String8 ui_tooltip_make_text(Arena* arena, String8 source, u64 max_columns, u64 max_lines, u64* line_count_out,
                                                  u64* max_line_columns_out, bool* truncated_out)
{
    String8 result = {0};
    *line_count_out = 0;
    *max_line_columns_out = 0;
    *truncated_out = false;
    if (arena && source.length != 0 && ui_string8_source_is_valid(source) && max_columns != 0 && max_lines != 0 && max_lines <= (u64)-1 / max_columns)
    {
        u64 full_columns = ui_utf8_codepoint_count(source);
        u64 column_capacity = max_lines * max_columns;
        bool truncated = full_columns > column_capacity;
        if (!truncated || max_columns >= 3)
        {
            u64 source_columns = truncated ? column_capacity - 3 : full_columns;
            u64 output_capacity = source.length;
            if (output_capacity > (u64)-1 - source.length || output_capacity + source.length > (u64)-1 - max_lines ||
                output_capacity + source.length + max_lines > (u64)-1 - 4)
            {
                return result;
            }
            output_capacity += source.length + max_lines + 4;

            u64 position = arena->position;
            if (ui_arena_try_advance(arena, &position, output_capacity, BUSTER_ALIGN_OF(char8)))
            {
                char8* output = arena_allocate(arena, char8, output_capacity);
                u64 output_length = 0;
                u64 source_position = 0;
                u64 copied_columns = 0;
                u64 line_count = 1;
                u64 line_columns = 0;
                u64 max_line_columns = 0;
                while (source_position < source.length && copied_columns < source_columns)
                {
                    u64 sequence_length = ui_utf8_sequence_length(source, source_position);
                    sequence_length = sequence_length ? sequence_length : 1;
                    if (line_columns == max_columns)
                    {
                        output[output_length++] = '\n';
                        line_count += 1;
                        line_columns = 0;
                    }

                    u32 codepoint = ui_utf8_codepoint_at(source, source_position);
                    if (codepoint == '\n')
                    {
                        output[output_length++] = ' ';
                    }
                    else
                    {
                        memcpy(output + output_length, source.pointer + source_position, sequence_length);
                        output_length += sequence_length;
                    }
                    source_position += sequence_length;
                    copied_columns += 1;
                    line_columns += 1;
                    max_line_columns = BUSTER_MAX(max_line_columns, line_columns);
                }

                truncated = truncated || source_position < source.length;
                if (truncated)
                {
                    if (line_columns + 3 > max_columns)
                    {
                        output[output_length++] = '\n';
                        line_count += 1;
                        line_columns = 0;
                    }
                    output[output_length++] = '.';
                    output[output_length++] = '.';
                    output[output_length++] = '.';
                    line_columns += 3;
                    max_line_columns = BUSTER_MAX(max_line_columns, line_columns);
                }

                result.pointer = output;
                result.length = output_length;
                *line_count_out = line_count;
                *max_line_columns_out = max_line_columns;
                *truncated_out = truncated;
            }
        }
    }

    return result;
}

BUSTER_GLOBAL_LOCAL void ui_layout_compute_clips(UI_Box* root)
{
    for (UI_Box* box = root; box; box = ui_box_rec_df_pre(box, root).next)
    {
        F32Interval2 clip = box->parent ? box->parent->clip_rect : box->rect;
        if (box->parent && (box->parent->flags & UI_BoxFlag_Clip))
        {
            clip = ui_rect_intersect(clip, box->parent->rect);
        }
        box->clip_rect = clip;
        box->visible = ui_rect_has_area(ui_rect_intersect(box->rect, clip));
        box->state_flags &= UI_BoxState_RendererDependency | UI_BoxState_Scrollable;
        if (box->visible)
        {
            box->state_flags |= UI_BoxState_Visible;
        }
        if (box->visible && (clip.x0 > box->rect.x0 || clip.y0 > box->rect.y0 || clip.x1 < box->rect.x1 || clip.y1 < box->rect.y1))
        {
            box->state_flags |= UI_BoxState_Clipped;
        }
        if (ui_key_match(box->key, ui_state->hot_box_key))
        {
            box->state_flags |= UI_BoxState_Hot;
        }
        if (ui_key_match(box->key, ui_state->active_box_key[UI_MouseButtonKind_Left]))
        {
            box->state_flags |= UI_BoxState_Active;
        }
        if (ui_key_match(box->key, ui_state->focus_hot_key) && !(box->flags & UI_BoxFlag_FocusHotDisabled))
        {
            box->state_flags |= UI_BoxState_FocusHot;
        }
        if (ui_key_match(box->key, ui_state->focus_active_key) && !(box->flags & UI_BoxFlag_FocusActiveDisabled))
        {
            box->state_flags |= UI_BoxState_FocusActive;
        }
        if (ui_key_match(box->key, ui_state->focus_edit_key))
        {
            box->state_flags |= UI_BoxState_FocusEdit;
        }

        F32Interval2 text_clip = ui_box_text_clip_rect(box);
        f32 text_left = BUSTER_MAX(box->rect.x0 + box->text_padding, text_clip.x0);
        f32 text_right = BUSTER_MIN(box->rect.x1 - box->text_padding, text_clip.x1 - box->text_padding);
        f32 available_width = BUSTER_MAX(0.0f, text_right - text_left);
        f32 character_width = BUSTER_MAX(0.001f, box->font_size * 0.60f);
        u64 full_columns = ui_utf8_codepoint_count(box->string);
        u64 available_columns = available_width >= character_width ? (u64)(available_width / character_width) : 0;
        u64 visible_columns = full_columns;
        if (!(box->flags & UI_BoxFlag_DisableTextTrunc))
        {
            if (full_columns > available_columns)
            {
                visible_columns = available_columns > 3 ? available_columns - 3 : 0;
            }
        }
        box->text_visible_columns = visible_columns;
        box->text_visible_length = ui_utf8_byte_offset_for_columns(box->string, visible_columns);
        box->text_truncated = visible_columns < full_columns;
        box->tooltip_visible = false;
        box->tooltip_rect = (F32Interval2){0};
        box->tooltip_string = (String8){0};
        box->tooltip_line_count = 0;
        box->tooltip_text_truncated = false;
        if (box->text_truncated)
        {
            box->state_flags |= UI_BoxState_TextTruncated;
        }
        if (box->flags & UI_BoxFlag_DrawTextFastpathCodepoint)
        {
            box->state_flags |= UI_BoxState_TextFastpath;
        }
        if (box->flags & UI_BoxFlag_DrawOverlay)
        {
            box->state_flags |= UI_BoxState_Overlay;
        }
        if (box->flags & (UI_BoxFlag_DrawFadeTop | UI_BoxFlag_DrawFadeBottom | UI_BoxFlag_DrawFadeLeft | UI_BoxFlag_DrawFadeRight))
        {
            box->state_flags |= UI_BoxState_Fade;
        }
    }

    // Tooltip hover follows the completed draw order.  First find the actual
    // topmost visible draw target at the pointer; a later non-truncated overlay
    // must occlude an eligible truncated label just as it occludes its pixels.
    UI_Box* topmost_draw_target = 0;
    for (UI_Box* box = root; box; box = ui_box_rec_df_pre(box, root).next)
    {
        if (ui_box_draws_content(box) && ui_box_contains_point(box, ui_state->mouse) &&
            (!topmost_draw_target || box->draw_order >= topmost_draw_target->draw_order))
        {
            topmost_draw_target = box;
        }
    }

    // Only the topmost eligible box owns the full-text tooltip for this frame.
    UI_Box* tooltip_target = 0;
    for (UI_Box* box = root; box; box = ui_box_rec_df_pre(box, root).next)
    {
        if (box->text_truncated && !(box->flags & UI_BoxFlag_DisableTruncatedHover) && box->visible && box->string.length != 0 &&
            ui_box_contains_point(box, ui_state->mouse) && (!tooltip_target || box->draw_order >= tooltip_target->draw_order))
        {
            tooltip_target = box;
        }
    }
    if (tooltip_target && tooltip_target == topmost_draw_target)
    {
        UI_Box* box = tooltip_target;
        f32 root_width = BUSTER_MAX(0.0f, root->rect.x1 - root->rect.x0);
        f32 root_height = BUSTER_MAX(0.0f, root->rect.y1 - root->rect.y0);
        f32 character_width = BUSTER_MAX(0.001f, box->font_size * 0.60f);
        f32 tooltip_line_height = BUSTER_MAX(1.0f, box->font_size * 1.35f);
        f32 tooltip_content_width = BUSTER_MAX(0.0f, root_width - box->text_padding * 2.0f);
        f32 tooltip_content_height = BUSTER_MAX(0.0f, root_height - box->text_padding * 2.0f);
        u64 tooltip_columns = tooltip_content_width >= character_width ? (u64)(tooltip_content_width / character_width) : 0;
        u64 tooltip_lines = tooltip_content_height >= tooltip_line_height ? (u64)(tooltip_content_height / tooltip_line_height) : 0;
        u64 tooltip_text_line_count = 0;
        u64 tooltip_max_line_columns = 0;
        bool tooltip_text_truncated = false;
        String8 tooltip_string = ui_tooltip_make_text(ui_build_arena(), box->string, tooltip_columns, tooltip_lines, &tooltip_text_line_count,
                                                       &tooltip_max_line_columns, &tooltip_text_truncated);
        f32 tooltip_width = (f32)tooltip_max_line_columns * character_width + box->text_padding * 2.0f;
        f32 tooltip_height = (f32)tooltip_text_line_count * tooltip_line_height + box->text_padding * 2.0f;
        f32 tooltip_x = box->rect.x0;
        f32 tooltip_y = box->rect.y1 + 4.0f;
        if (tooltip_string.length != 0 && tooltip_width <= root_width && tooltip_height <= root_height)
        {
            if (tooltip_x + tooltip_width > root->rect.x1)
            {
                tooltip_x = root->rect.x1 - tooltip_width;
            }
            if (tooltip_y + tooltip_height > root->rect.y1)
            {
                tooltip_y = box->rect.y0 - tooltip_height - 4.0f;
            }
            f32 max_tooltip_x = BUSTER_MAX(root->rect.x0, root->rect.x1 - tooltip_width);
            f32 max_tooltip_y = BUSTER_MAX(root->rect.y0, root->rect.y1 - tooltip_height);
            tooltip_x = BUSTER_CLAMP(root->rect.x0, tooltip_x, max_tooltip_x);
            tooltip_y = BUSTER_CLAMP(root->rect.y0, tooltip_y, max_tooltip_y);
            box->tooltip_rect = ui_rect_make(tooltip_x, tooltip_y, tooltip_x + tooltip_width, tooltip_y + tooltip_height);
            box->tooltip_string = tooltip_string;
            box->tooltip_line_count = tooltip_text_line_count;
            box->tooltip_text_truncated = tooltip_text_truncated;
            box->tooltip_visible = true;
            box->state_flags |= UI_BoxState_Tooltip;
        }
    }
}

BUSTER_GLOBAL_LOCAL bool ui_box_draws_content(UI_Box* box)
{
    if (box && box->visible && ui_rect_has_area(box->rect))
    {
        UI_BoxFlags direct_draw_flags = UI_BoxFlag_DrawBackgroundBlur | UI_BoxFlag_DrawDropShadow | UI_BoxFlag_DrawBackground | UI_BoxFlag_DrawBorder |
                                        UI_BoxFlag_DrawSideTop | UI_BoxFlag_DrawSideBottom | UI_BoxFlag_DrawSideLeft | UI_BoxFlag_DrawSideRight |
                                        UI_BoxFlag_DrawOverlay | UI_BoxFlag_DrawFadeTop | UI_BoxFlag_DrawFadeBottom | UI_BoxFlag_DrawFadeLeft |
                                        UI_BoxFlag_DrawFadeRight | UI_BoxFlag_Debug;
        if (box->flags & direct_draw_flags)
        {
            return true;
        }
        if ((box->flags & UI_BoxFlag_DrawText) && box->string.length != 0)
        {
            return true;
        }
        if ((box->flags & UI_BoxFlag_HasFuzzyMatchRanges) && box->fuzzy_match_ranges && box->fuzzy_match_range_count != 0)
        {
            return true;
        }
        if (!(box->flags & UI_BoxFlag_DisableFocusBorder) && (box->state_flags & UI_BoxState_FocusActive))
        {
            return true;
        }
    }

    return false;
}

void ui_build_end(void)
{
    if (ui_state->stacks.parent_length != 0)
    {
        BUSTER_UNUSED(ui_pop_parent());
    }

    ui_prune_untouched_boxes();
    // A release may have been routed to a previous capture, while this build
    // can omit, disable, or remove clickability from that owner.  Prune before
    // layout derives UI_BoxState_Active so the completed box cannot publish a
    // stale active state and a replacement cannot inherit the capture.
    ui_prune_active_keys();
    ui_prune_focus_keys();

    if (ui_state->root)
    {
        for (Axis2 axis = 0; axis < AXIS2_COUNT; axis += 1)
        {
            ui_layout_root(ui_state->root, axis);
        }
        ui_layout_compute_clips(ui_state->root);
    }

    for (u64 active_box_index = 0; active_box_index < ui_state->active_box_count; active_box_index += 1)
    {
        UI_Box* box = ui_state->active_boxes[active_box_index];
        if ((box->flags & UI_BoxFlag_RoundChildrenByParent) && box->first && box->last)
        {
            for (UI_Box* child = box; child; child = ui_box_rec_df_pre(child, box).next)
            {
                if (floor_f32(child->rect.x0) <= floor_f32(box->rect.x0) && floor_f32(child->rect.y0) <= floor_f32(box->rect.y0))
                {
                    child->corner_radii[CORNER_00] = box->corner_radii[CORNER_00];
                }
                if (floor_f32(child->rect.x1) >= floor_f32(box->rect.x1) && floor_f32(child->rect.y0) <= floor_f32(box->rect.y0))
                {
                    child->corner_radii[CORNER_10] = box->corner_radii[CORNER_10];
                }
                if (floor_f32(child->rect.x0) <= floor_f32(box->rect.x0) && floor_f32(child->rect.y1) >= floor_f32(box->rect.y1))
                {
                    child->corner_radii[CORNER_01] = box->corner_radii[CORNER_01];
                }
                if (floor_f32(child->rect.x1) >= floor_f32(box->rect.x1) && floor_f32(child->rect.y1) >= floor_f32(box->rect.y1))
                {
                    child->corner_radii[CORNER_11] = box->corner_radii[CORNER_11];
                }
            }

            if (box->child_layout_axis == AXIS2_X)
            {
                box->first->corner_radii[CORNER_00] = box->corner_radii[CORNER_00];
                box->first->corner_radii[CORNER_01] = box->corner_radii[CORNER_01];
                box->last->corner_radii[CORNER_10] = box->corner_radii[CORNER_10];
                box->last->corner_radii[CORNER_11] = box->corner_radii[CORNER_11];
            }
            else
            {
                box->first->corner_radii[CORNER_00] = box->corner_radii[CORNER_00];
                box->first->corner_radii[CORNER_10] = box->corner_radii[CORNER_10];
                box->last->corner_radii[CORNER_01] = box->corner_radii[CORNER_01];
                box->last->corner_radii[CORNER_11] = box->corner_radii[CORNER_11];
            }
        }
    }

    f32 hot_rate = 0.35f;
    f32 active_rate = 0.45f;
    for (u64 active_box_index = 0; active_box_index < ui_state->active_box_count; active_box_index += 1)
    {
        UI_Box* box = ui_state->active_boxes[active_box_index];
        bool hot = ui_key_match(box->key, ui_state->hot_box_key);
        bool active = ui_key_match(box->key, ui_state->active_box_key[UI_MouseButtonKind_Left]);
        box->hot_t += ((f32)hot - box->hot_t) * hot_rate;
        box->active_t += ((f32)active - box->active_t) * active_rate;
        ui_state->is_animating = ui_state->is_animating || (box->hot_t > 0.01f && box->hot_t < 0.99f) || (box->active_t > 0.01f && box->active_t < 0.99f);
    }

}

BUSTER_GLOBAL_LOCAL UI_MouseButtonKind ui_mouse_button_kind_from_key(WmKey key, bool* is_mouse)
{
    UI_MouseButtonKind result = UI_MouseButtonKind_Left;
    *is_mouse = true;
    if (key == WM_KEY_MOUSE_LEFT)
    {
        result = UI_MouseButtonKind_Left;
    }
    else if (key == WM_KEY_MOUSE_MIDDLE)
    {
        result = UI_MouseButtonKind_Middle;
    }
    else if (key == WM_KEY_MOUSE_RIGHT)
    {
        result = UI_MouseButtonKind_Right;
    }
    else
    {
        *is_mouse = false;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool ui_box_contains_point(UI_Box* box, float2 point)
{
    bool result;
    if (!box || !box->visible)
    {
        result = false;
    }
    else
    {
        F32Interval2 effective_rect = ui_rect_intersect(box->rect, box->clip_rect);
        result = ui_rect_has_area(effective_rect) && ui_rect_contains(effective_rect, point);
    }

    return result;
}

BUSTER_GLOBAL_LOCAL UI_Box* ui_topmost_box_at_point_for_build(float2 point, UI_BoxFlags required_flags, bool allow_disabled, u64 build_index)
{
    UI_Box* result = 0;
    UI_Box* disabled_blocker = 0;
    u64 result_order = 0;
    u64 blocker_order = 0;
    for (u64 active_box_index = 0; active_box_index < ui_state->active_box_count; active_box_index += 1)
    {
        UI_Box* box = ui_state->active_boxes[active_box_index];
        if (box->last_touched_build_index != build_index || !ui_box_contains_point(box, point))
        {
            continue;
        }
        if (!allow_disabled && (box->flags & UI_BoxFlag_Disabled) && (!disabled_blocker || box->build_order >= blocker_order))
        {
            disabled_blocker = box;
            blocker_order = box->build_order;
        }
        if ((box->flags & required_flags) && (allow_disabled || !(box->flags & UI_BoxFlag_Disabled)) && (!result || box->build_order >= result_order))
        {
            result = box;
            result_order = box->build_order;
        }
    }
    if (disabled_blocker && (!result || blocker_order >= result_order))
    {
        result = disabled_blocker;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL UI_Box* ui_topmost_focus_box_at_point_for_build(float2 point, u64 build_index)
{
    UI_Box* result = 0;
    UI_Box* disabled_blocker = 0;
    u64 result_order = 0;
    u64 blocker_order = 0;
    UI_BoxFlags focus_flags = UI_BoxFlag_FocusHot | UI_BoxFlag_FocusActive | UI_BoxFlag_KeyboardClickable | UI_BoxFlag_ClickToFocus |
                              UI_BoxFlag_DefaultFocusNavX | UI_BoxFlag_DefaultFocusNavY | UI_BoxFlag_DefaultFocusEdit;
    for (u64 active_box_index = 0; active_box_index < ui_state->active_box_count; active_box_index += 1)
    {
        UI_Box* box = ui_state->active_boxes[active_box_index];
        if (box->last_touched_build_index != build_index || !ui_box_contains_point(box, point))
        {
            continue;
        }
        if ((box->flags & UI_BoxFlag_Disabled) && (!disabled_blocker || box->build_order >= blocker_order))
        {
            disabled_blocker = box;
            blocker_order = box->build_order;
        }
        if ((box->flags & focus_flags) && ui_box_focusable(box, false) && (!result || box->build_order >= result_order))
        {
            result = box;
            result_order = box->build_order;
        }
    }
    if (disabled_blocker && (!result || blocker_order >= result_order))
    {
        result = disabled_blocker;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool ui_event_belongs_to_box(UI_Event* event, UI_Box* box, UI_BoxFlags required_flags)
{
    BUSTER_UNUSED(required_flags);
    if (event && box)
    {
        if (event->owner_assigned)
        {
            return event->owner_key != 0 && event->owner_key == box->key.value;
        }
    }

    return false;
}

BUSTER_GLOBAL_LOCAL SliceString8 ui_copy_drop_paths(SliceString8 paths)
{
    return ui_copy_string8_slice_checked(ui_build_arena(), paths);
}

BUSTER_GLOBAL_LOCAL bool ui_event_updates_pointer(UI_Event* event)
{
    if (!event)
    {
        return false;
    }
    bool is_mouse = false;
    ui_mouse_button_kind_from_key(event->key, &is_mouse);
    return event->kind == UI_EventKind_MouseMove || event->kind == UI_EventKind_Scroll ||
           (is_mouse && (event->kind == UI_EventKind_Press || event->kind == UI_EventKind_Release));
}

BUSTER_GLOBAL_LOCAL void ui_route_pointer_targets_at(float2 point, u64 build_index, UI_Key* hot, UI_Key* focus_hot)
{
    UI_Box* hot_target = ui_topmost_box_at_point_for_build(point, UI_BoxFlag_MouseClickable, false, build_index);
    UI_Box* focus_target = ui_topmost_focus_box_at_point_for_build(point, build_index);
    if (hot)
    {
        *hot = hot_target && !(hot_target->flags & UI_BoxFlag_Disabled) ? hot_target->key : ui_key_zero();
    }
    if (focus_hot)
    {
        *focus_hot = focus_target && !(focus_target->flags & UI_BoxFlag_Disabled) ? focus_target->key : ui_key_zero();
    }
}

BUSTER_GLOBAL_LOCAL bool ui_focus_navigation_event(UI_Event* event, UI_BoxFlags* axis_flag, UI_FocusDirection* direction)
{
    bool result;
    if (!event || event->kind != UI_EventKind_Press || !axis_flag || !direction)
    {
        result = false;
    }
    else
    {
        *axis_flag = 0;
        *direction = UI_FocusDirection_LinearForward;
        if (event->key == WM_KEY_TAB)
        {
            *axis_flag = UI_BoxFlag_DefaultFocusNavX | UI_BoxFlag_DefaultFocusNavY;
            *direction = (event->modifiers & (1u << WM_MODIFIER_SHIFT)) ? UI_FocusDirection_LinearBackward : UI_FocusDirection_LinearForward;
        }
        else if (event->key == WM_KEY_LEFT)
        {
            *axis_flag = UI_BoxFlag_DefaultFocusNavX;
            *direction = UI_FocusDirection_Left;
        }
        else if (event->key == WM_KEY_RIGHT)
        {
            *axis_flag = UI_BoxFlag_DefaultFocusNavX;
            *direction = UI_FocusDirection_Right;
        }
        else if (event->key == WM_KEY_UP)
        {
            *axis_flag = UI_BoxFlag_DefaultFocusNavY;
            *direction = UI_FocusDirection_Up;
        }
        else if (event->key == WM_KEY_DOWN)
        {
            *axis_flag = UI_BoxFlag_DefaultFocusNavY;
            *direction = UI_FocusDirection_Down;
        }
        result = *axis_flag != 0;
    }

    return result;
}

BUSTER_GLOBAL_LOCAL void ui_route_event_owners(void)
{
    u64 previous_build_index = ui_state->build_index ? ui_state->build_index - 1 : 0;
    ui_state->pointer_targets_assigned = previous_build_index != 0;

    UI_Key provisional_hot = ui_state->hot_box_key;
    UI_Key provisional_focus_hot = ui_state->focus_hot_key;
    UI_Key provisional_focus_active = ui_state->focus_active_key;
    UI_Key provisional_focus_edit = ui_state->focus_edit_key;
    UI_Key provisional_active[UI_MouseButtonKind_COUNT] = {0};
    for (u64 i = 0; i < (u64)UI_MouseButtonKind_COUNT; i += 1)
    {
        provisional_active[i] = ui_state->active_box_key[i];
    }

    if (ui_state->pointer_targets_assigned)
    {
        ui_route_pointer_targets_at(ui_state->previous_mouse, previous_build_index, &provisional_hot, &provisional_focus_hot);
    }

    // Ownership and capture/focus transitions are computed in this one
    // chronological pass. Signals later walk the same event list in box
    // order, but only report the transitions recorded on their owned events;
    // they must not replay or reorder these global state changes.
    for (UI_EventNode* node = ui_state->events.first; node; node = node->next)
    {
        UI_Event* event = &node->v;
        event->owner_key = 0;
        event->owner_assigned = 0;
        event->route_flags = UI_EventRouteFlag_None;

        bool is_mouse = false;
        UI_MouseButtonKind button = ui_mouse_button_kind_from_key(event->key, &is_mouse);
        UI_Box* target = 0;
        if (ui_state->pointer_targets_assigned && ui_event_updates_pointer(event))
        {
            ui_route_pointer_targets_at(event->pos, previous_build_index, &provisional_hot, &provisional_focus_hot);
        }
        if (is_mouse && event->kind == UI_EventKind_Release)
        {
            event->owner_assigned = 1;
            if (!ui_key_match(provisional_active[button], ui_key_zero()))
            {
                event->owner_key = provisional_active[button].value;
            }
            provisional_active[button] = ui_key_zero();
        }
        else if (event->kind == UI_EventKind_Text)
        {
            if (!ui_key_match(provisional_focus_edit, ui_key_zero()))
            {
                event->owner_key = provisional_focus_edit.value;
            }
            else if (!ui_key_match(provisional_focus_hot, ui_key_zero()))
            {
                event->owner_key = provisional_focus_hot.value;
            }
            event->owner_assigned = 1;
        }
        else if (event->kind == UI_EventKind_Press && !is_mouse)
        {
            UI_BoxFlags axis_flag = 0;
            UI_FocusDirection direction = UI_FocusDirection_LinearForward;
            event->owner_assigned = 1;
            bool edit_owns_horizontal_arrow = !ui_key_match(provisional_focus_edit, ui_key_zero()) &&
                                               (event->key == WM_KEY_LEFT || event->key == WM_KEY_RIGHT);
            if (ui_focus_navigation_event(event, &axis_flag, &direction) && !edit_owns_horizontal_arrow)
            {
                UI_Box* candidate = ui_focus_navigation_candidate(provisional_focus_active, axis_flag, direction, previous_build_index, ui_state->previous_root);
                if (candidate)
                {
                    UI_Key next_focus_edit = (candidate->flags & UI_BoxFlag_DefaultFocusEdit) ? candidate->key : ui_key_zero();
                    bool focus_changed = !ui_key_match(provisional_focus_active, candidate->key) || !ui_key_match(provisional_focus_edit, next_focus_edit);
                    provisional_focus_hot = candidate->key;
                    provisional_focus_active = candidate->key;
                    provisional_focus_edit = next_focus_edit;
                    if (focus_changed)
                    {
                        ui_state->focus_changed = 1;
                        ui_state->focus_changed_key = candidate->key;
                        ui_state->focus_changed_delivery_build_index = ui_state->build_index + 1;
                        ui_state->focus_changed_pending = 1;
                    }
                    // Navigation is resolved against the previous tree here;
                    // removing it prevents build-order signals from replaying
                    // the transition after the new tree is built.
                    ui_eat_event(event);
                }
            }
            else if (!ui_key_match(provisional_focus_active, ui_key_zero()))
            {
                event->owner_key = provisional_focus_active.value;
            }
        }
        else if (event->kind == UI_EventKind_Scroll)
        {
            event->owner_assigned = 1;
            if (ui_state->pointer_targets_assigned)
            {
                target = ui_topmost_box_at_point_for_build(event->pos, UI_BoxFlag_Scroll, false, previous_build_index);
            }
        }
        else if (event->kind == UI_EventKind_FileDrop)
        {
            event->owner_assigned = 1;
            if (ui_state->pointer_targets_assigned)
            {
                target = ui_topmost_box_at_point_for_build(event->pos, UI_BoxFlag_DropSite, false, previous_build_index);
            }
        }
        else if (is_mouse && event->kind == UI_EventKind_Press)
        {
            event->owner_assigned = 1;
            if (ui_state->pointer_targets_assigned)
            {
                target = ui_topmost_box_at_point_for_build(event->pos, UI_BoxFlag_MouseClickable, false, previous_build_index);
            }
            provisional_active[button] = ui_key_zero();
            if (target && !(target->flags & UI_BoxFlag_Disabled) && (target->flags & UI_BoxFlag_MouseClickable))
            {
                provisional_active[button] = target->key;
                if ((target->flags & (UI_BoxFlag_ClickToFocus | UI_BoxFlag_FocusActive | UI_BoxFlag_DefaultFocusEdit)) && ui_box_focusable(target, true))
                {
                    UI_Key next_focus_edit = (target->flags & UI_BoxFlag_DefaultFocusEdit) ? target->key : ui_key_zero();
                    bool focus_changed = !ui_key_match(provisional_focus_active, target->key) || !ui_key_match(provisional_focus_edit, next_focus_edit);
                    provisional_focus_hot = target->key;
                    provisional_focus_active = target->key;
                    provisional_focus_edit = next_focus_edit;
                    if (focus_changed)
                    {
                        event->route_flags |= UI_EventRouteFlag_FocusChanged;
                        ui_state->focus_changed = 1;
                    }
                }
            }
        }

        if (target)
        {
            event->owner_key = target->key.value;
            event->owner_assigned = 1;
        }
    }

    ui_state->hot_box_key = provisional_hot;
    ui_state->focus_hot_key = provisional_focus_hot;
    ui_state->focus_active_key = provisional_focus_active;
    ui_state->focus_edit_key = provisional_focus_edit;
    for (u64 i = 0; i < (u64)UI_MouseButtonKind_COUNT; i += 1)
    {
        ui_state->active_box_key[i] = provisional_active[i];
    }
}

BUSTER_GLOBAL_LOCAL bool ui_box_focusable(UI_Box* box, bool active)
{
    if (!box || ui_key_match(box->key, ui_key_zero()) || (box->flags & UI_BoxFlag_FocusNavSkip) || (box->flags & UI_BoxFlag_Disabled))
    {
        return false;
    }
    if (!active && (box->flags & UI_BoxFlag_FocusHotDisabled))
    {
        return false;
    }
    if (active && (box->flags & UI_BoxFlag_FocusActiveDisabled))
    {
        return false;
    }
    return active ? !!(box->flags & (UI_BoxFlag_FocusActive | UI_BoxFlag_KeyboardClickable | UI_BoxFlag_ClickToFocus | UI_BoxFlag_DefaultFocusNavX |
                                     UI_BoxFlag_DefaultFocusNavY | UI_BoxFlag_DefaultFocusEdit))
                  : !!(box->flags & (UI_BoxFlag_FocusHot | UI_BoxFlag_FocusActive | UI_BoxFlag_KeyboardClickable | UI_BoxFlag_ClickToFocus | UI_BoxFlag_DefaultFocusNavX |
                                     UI_BoxFlag_DefaultFocusNavY | UI_BoxFlag_DefaultFocusEdit));
}

BUSTER_GLOBAL_LOCAL bool ui_box_is_current(UI_Box* box)
{
    return box && box->last_touched_build_index == ui_state->build_index;
}

BUSTER_GLOBAL_LOCAL void ui_prune_active_keys(void)
{
    for (u64 i = 0; i < (u64)UI_MouseButtonKind_COUNT; i += 1)
    {
        UI_Key active_key = ui_state->active_box_key[i];
        if (!ui_key_match(active_key, ui_key_zero()))
        {
            UI_Box* active_box = ui_box_from_key(active_key);
            if (!ui_box_is_current(active_box) || (active_box->flags & UI_BoxFlag_Disabled) || !(active_box->flags & UI_BoxFlag_MouseClickable))
            {
                ui_state->active_box_key[i] = ui_key_zero();
            }
        }
    }
}

BUSTER_GLOBAL_LOCAL void ui_signal_add_focus_state(UI_Signal* signal, UI_Box* box)
{
    if (!ui_key_match(box->key, ui_key_zero()) && !(box->flags & UI_BoxFlag_Disabled) &&
        ((ui_key_match(ui_state->focus_active_key, box->key) && !(box->flags & UI_BoxFlag_FocusActiveDisabled)) ||
         (ui_key_match(ui_state->focus_edit_key, box->key) && !(box->flags & UI_BoxFlag_FocusActiveDisabled))))
    {
        signal->f |= UI_SignalFlag_Focused;
        signal->focused = 1;
    }
}

UI_Signal ui_signal_from_box(UI_Box* box)
{
    UI_Signal sig = {.box = box};
    if (!box || !ui_box_is_current(box))
    {
        return sig;
    }

    bool disabled = !!(box->flags & UI_BoxFlag_Disabled);
    if (!disabled && ui_state->focus_changed_pending && ui_state->focus_changed_delivery_build_index == ui_state->build_index &&
        ui_key_match(ui_state->focus_changed_key, box->key) && ui_key_match(ui_state->focus_active_key, box->key) && ui_box_focusable(box, true))
    {
        sig.f |= UI_SignalFlag_FocusChanged;
        sig.focus_changed = 1;
        ui_state->focus_changed_pending = 0;
        ui_state->focus_changed_key = ui_key_zero();
        ui_state->focus_changed_delivery_build_index = 0;
    }

    bool mouse_over = ui_box_contains_point(box, ui_state->mouse);
    if (mouse_over)
    {
        sig.f |= UI_SignalFlag_MouseOver;
        sig.mouse_over = 1;
    }

    bool pointer_focus_target = ui_state->pointer_targets_assigned && ui_key_match(ui_state->focus_hot_key, box->key);
    if (mouse_over && pointer_focus_target && ui_box_focusable(box, false))
    {
        sig.f |= UI_SignalFlag_Hovering;
        sig.hovering = 1;
    }
    ui_signal_add_focus_state(&sig, box);
    if (disabled)
    {
        return sig;
    }

    UI_EventIterator iterator = ui_event_iterator_initialize(ui_state);
    UI_Event* event;
    while ((event = ui_next_event(&iterator)))
    {
        bool is_mouse = false;
        UI_MouseButtonKind button = ui_mouse_button_kind_from_key(event->key, &is_mouse);
        bool event_in_bounds = ui_box_contains_point(box, event->pos);
        bool event_owned = event->owner_assigned && event->owner_key == box->key.value;
        if (event->kind == UI_EventKind_Text && (box->flags & UI_BoxFlag_DrawTextFastpathCodepoint) && box->fastpath_codepoint != 0 && !disabled &&
            (ui_key_match(ui_state->focus_hot_key, box->key) || event_owned) && ui_box_focusable(box, false) && ui_utf8_codepoint_count(event->string) == 1 &&
            ui_utf8_codepoint_at(event->string, 0) == box->fastpath_codepoint && (!event->owner_assigned || event_owned))
        {
            sig.f |= UI_SignalFlag_KeyboardPressed;
            sig.clicked_left = 1;
            sig.key = event->key;
            sig.modifiers = event->modifiers;
            ui_eat_event(event);
        }
        else if (event->kind == UI_EventKind_Scroll && (box->flags & UI_BoxFlag_Scroll) && event_in_bounds && !disabled &&
            ui_event_belongs_to_box(event, box, UI_BoxFlag_Scroll))
        {
            float2 delta = float2_make(float2_element(event->delta, AXIS2_X) * 32.0f, float2_element(event->delta, AXIS2_Y) * 32.0f);
            if (!(box->flags & UI_BoxFlag_ViewScrollX))
            {
                float2_element(delta, AXIS2_X) = 0.0f;
            }
            if (!(box->flags & UI_BoxFlag_ViewScrollY))
            {
                float2_element(delta, AXIS2_Y) = 0.0f;
            }
            ui_box_scroll_by(box, delta);
            sig.f |= UI_SignalFlag_Scrolled;
            sig.scrolled = 1;
            sig.scroll_delta = delta;
            ui_eat_event(event);
        }
        else if (event->kind == UI_EventKind_FileDrop && (box->flags & UI_BoxFlag_DropSite) && event_in_bounds && !disabled &&
                 ui_event_belongs_to_box(event, box, UI_BoxFlag_DropSite))
        {
            sig.f |= UI_SignalFlag_Dropped;
            sig.dropped = 1;
            sig.drop_paths = ui_copy_drop_paths(event->paths);
            ui_eat_event(event);
        }
        else if ((box->flags & UI_BoxFlag_MouseClickable) && is_mouse && event->kind == UI_EventKind_Press && event_in_bounds && !disabled &&
                 ui_event_belongs_to_box(event, box, UI_BoxFlag_MouseClickable))
        {
            if (button == UI_MouseButtonKind_Left)
            {
                sig.f |= UI_SignalFlag_LeftPressed;
                sig.pressed_left = 1;
            }
            if (event->route_flags & UI_EventRouteFlag_FocusChanged)
            {
                sig.f |= UI_SignalFlag_FocusChanged;
                sig.focus_changed = 1;
            }
            ui_eat_event(event);
        }
        else if ((box->flags & UI_BoxFlag_MouseClickable) && is_mouse && event->kind == UI_EventKind_Release &&
                 !disabled && ui_event_belongs_to_box(event, box, UI_BoxFlag_MouseClickable))
        {
            if (button == UI_MouseButtonKind_Left)
            {
                sig.f |= UI_SignalFlag_LeftReleased;
                sig.released_left = 1;
                if (event_in_bounds)
                {
                    sig.f |= UI_SignalFlag_LeftClicked;
                    sig.clicked_left = 1;
                }
            }
            ui_eat_event(event);
        }
        else if ((box->flags & UI_BoxFlag_KeyboardClickable) && event->kind == UI_EventKind_Press &&
                 (event->key == WM_KEY_RETURN || event->key == WM_KEY_SPACE) &&
                 !disabled && ui_box_focusable(box, true) &&
                 (event->owner_assigned ? event->owner_key == box->key.value : ui_key_match(ui_state->focus_active_key, box->key)))
        {
            sig.f |= UI_SignalFlag_KeyboardPressed;
            sig.clicked_left = 1;
            sig.key = event->key;
            sig.modifiers = event->modifiers;
            ui_eat_event(event);
        }
    }

    if (!disabled && (box->flags & UI_BoxFlag_MouseClickable) && ui_key_match(ui_state->active_box_key[UI_MouseButtonKind_Left], box->key))
    {
        sig.f |= UI_SignalFlag_Dragging;
        sig.dragging = 1;
    }
    if (!ui_key_match(box->key, ui_key_zero()) && !(box->flags & UI_BoxFlag_Disabled) &&
        ((ui_key_match(ui_state->focus_active_key, box->key) && !(box->flags & UI_BoxFlag_FocusActiveDisabled)) ||
         (ui_key_match(ui_state->focus_edit_key, box->key) && !(box->flags & UI_BoxFlag_FocusActiveDisabled))))
    {
        sig.f |= UI_SignalFlag_Focused;
        sig.focused = 1;
    }
    return sig;
}

BUSTER_GLOBAL_LOCAL UI_Box* ui_focus_scope_from_box(UI_Box* box, UI_Box* root)
{
    UI_Box* result = box ? box->parent : root;
    for (UI_Box* parent = box ? box->parent : root; parent; parent = parent->parent)
    {
        if (parent != root && (parent->flags & (UI_BoxFlag_DefaultFocusNavX | UI_BoxFlag_DefaultFocusNavY)))
        {
            result = parent;
            break;
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool ui_box_in_focus_scope(UI_Box* box, UI_Box* scope)
{
    if (box && scope && box != scope)
    {
        for (UI_Box* parent = box->parent; parent; parent = parent->parent)
        {
            if (parent == scope)
            {
                return true;
            }
        }
    }

    return false;
}

BUSTER_GLOBAL_LOCAL UI_Box* ui_focus_navigation_candidate(UI_Key current_key, UI_BoxFlags axis_flag, UI_FocusDirection direction, u64 build_index, UI_Box* root)
{
    UI_Box* first = 0;
    UI_Box* last = 0;
    UI_Box* current = ui_box_from_key(current_key);
    bool current_found = current && current->last_touched_build_index == build_index;
    if (!current_found)
    {
        current = 0;
    }
    UI_Box* scope = ui_focus_scope_from_box(current, root);
    UI_Box* directional = 0;
    f32 directional_score = 0.0f;
    for (u64 active_box_index = 0; active_box_index < ui_state->active_box_count; active_box_index += 1)
    {
        UI_Box* box = ui_state->active_boxes[active_box_index];
        if (box->last_touched_build_index != build_index || !ui_box_in_focus_scope(box, scope) || !(box->flags & axis_flag) || !ui_box_focusable(box, true))
        {
            continue;
        }
        if (!first || box->build_order < first->build_order)
        {
            first = box;
        }
        if (!last || box->build_order > last->build_order)
        {
            last = box;
        }
        if (current_found && ui_key_match(box->key, current_key) && direction >= UI_FocusDirection_Left)
        {
            continue;
        }
        if (current_found && direction >= UI_FocusDirection_Left)
        {
            f32 current_x = (current->rect.x0 + current->rect.x1) * 0.5f;
            f32 current_y = (current->rect.y0 + current->rect.y1) * 0.5f;
            f32 candidate_x = (box->rect.x0 + box->rect.x1) * 0.5f;
            f32 candidate_y = (box->rect.y0 + box->rect.y1) * 0.5f;
            f32 primary_delta = 0.0f;
            f32 cross_delta = 0.0f;
            bool in_direction = false;
            if (direction == UI_FocusDirection_Left || direction == UI_FocusDirection_Right)
            {
                primary_delta = candidate_x - current_x;
                cross_delta = fabs_f32(candidate_y - current_y);
                in_direction = direction == UI_FocusDirection_Left ? primary_delta < 0.0f : primary_delta > 0.0f;
            }
            else
            {
                primary_delta = candidate_y - current_y;
                cross_delta = fabs_f32(candidate_x - current_x);
                in_direction = direction == UI_FocusDirection_Up ? primary_delta < 0.0f : primary_delta > 0.0f;
            }
            if (in_direction)
            {
                f32 score = fabs_f32(primary_delta) * 1024.0f + cross_delta;
                if (!directional || score < directional_score || (score == directional_score && box->build_order < directional->build_order))
                {
                    directional = box;
                    directional_score = score;
                }
            }
        }
    }
    if (direction >= UI_FocusDirection_Left)
    {
        if (directional)
        {
            return directional;
        }
        // Directional navigation wraps within the current parent scope.
        for (u64 active_box_index = 0; active_box_index < ui_state->active_box_count; active_box_index += 1)
        {
            UI_Box* box = ui_state->active_boxes[active_box_index];
            if (box->last_touched_build_index != build_index || !ui_box_in_focus_scope(box, scope) || !(box->flags & axis_flag) || !ui_box_focusable(box, true) || ui_key_match(box->key, current_key))
            {
                continue;
            }
            bool better = false;
            if (!directional)
            {
                better = true;
            }
            else if (direction == UI_FocusDirection_Left || direction == UI_FocusDirection_Right)
            {
                f32 value = (box->rect.x0 + box->rect.x1) * 0.5f;
                f32 old_value = (directional->rect.x0 + directional->rect.x1) * 0.5f;
                better = direction == UI_FocusDirection_Left ? value > old_value : value < old_value;
            }
            else if (directional)
            {
                f32 value = (box->rect.y0 + box->rect.y1) * 0.5f;
                f32 old_value = (directional->rect.y0 + directional->rect.y1) * 0.5f;
                better = direction == UI_FocusDirection_Up ? value > old_value : value < old_value;
            }
            if (better)
            {
                directional = box;
            }
        }
        return directional;
    }

    if (!current_found)
    {
        return direction == UI_FocusDirection_LinearBackward ? last : first;
    }
    if (direction == UI_FocusDirection_LinearBackward)
    {
        UI_Box* before = 0;
        for (u64 active_box_index = 0; active_box_index < ui_state->active_box_count; active_box_index += 1)
        {
            UI_Box* box = ui_state->active_boxes[active_box_index];
            if (box->last_touched_build_index == build_index && ui_box_in_focus_scope(box, scope) && (box->flags & axis_flag) && ui_box_focusable(box, true) && box->build_order < current->build_order &&
                (!before || box->build_order > before->build_order))
            {
                before = box;
            }
        }
        return before ? before : last;
    }
    UI_Box* after = 0;
    for (u64 active_box_index = 0; active_box_index < ui_state->active_box_count; active_box_index += 1)
    {
        UI_Box* box = ui_state->active_boxes[active_box_index];
        if (box->last_touched_build_index == build_index && ui_box_in_focus_scope(box, scope) && (box->flags & axis_flag) && ui_box_focusable(box, true) && box->build_order > current->build_order &&
            (!after || box->build_order < after->build_order))
        {
            after = box;
        }
    }
    return after ? after : first;
}

BUSTER_GLOBAL_LOCAL void ui_prune_focus_keys(void)
{
    UI_Key zero = ui_key_zero();
    if (!ui_key_match(ui_state->focus_hot_key, zero))
    {
        UI_Box* box = ui_box_from_key(ui_state->focus_hot_key);
        if (!box || !ui_box_is_current(box) || !ui_box_focusable(box, false))
        {
            ui_state->focus_hot_key = zero;
        }
    }
    if (!ui_key_match(ui_state->focus_active_key, zero))
    {
        UI_Box* box = ui_box_from_key(ui_state->focus_active_key);
        if (!box || !ui_box_is_current(box) || !ui_box_focusable(box, true))
        {
            ui_state->focus_active_key = zero;
        }
    }
    if (!ui_key_match(ui_state->focus_edit_key, zero))
    {
        UI_Box* box = ui_box_from_key(ui_state->focus_edit_key);
        if (!box || !ui_box_is_current(box) || !ui_box_focusable(box, true))
        {
            ui_state->focus_edit_key = zero;
        }
    }
    if (ui_state->focus_changed_pending)
    {
        UI_Box* box = ui_box_from_key(ui_state->focus_changed_key);
        if (!box || !ui_box_is_current(box) || !ui_box_focusable(box, true))
        {
            ui_state->focus_changed_pending = 0;
            ui_state->focus_changed_key = zero;
            ui_state->focus_changed_delivery_build_index = 0;
        }
    }
}

BUSTER_GLOBAL_LOCAL float2 ui_box_text_position(UI_Box* box)
{
    float2 rect_dim = ui_rect_dim(box->rect);
    float2 result = float2_make(box->rect.x0 + box->text_padding, box->rect.y0 + BUSTER_MAX(0.0f, (float2_element(rect_dim, AXIS2_Y) - box->font_size) * 0.5f));
    u64 columns = (box->flags & UI_BoxFlag_DisableTextTrunc) ? ui_utf8_codepoint_count(box->string) : box->text_visible_columns;
    if (box->text_truncated && !(box->flags & UI_BoxFlag_DisableTextTrunc))
    {
        columns += 3;
    }
    f32 estimated_width = (f32)columns * box->font_size * 0.60f;
    if (box->text_align == UI_TextAlign_Center)
    {
        float2_element(result, AXIS2_X) = floor_f32((box->rect.x0 + box->rect.x1) * 0.5f - estimated_width * 0.5f);
        float2_element(result, AXIS2_X) = BUSTER_CLAMP_BOT(float2_element(result, AXIS2_X), box->rect.x0 + box->text_padding);
    }
    else if (box->text_align == UI_TextAlign_Right)
    {
        float2_element(result, AXIS2_X) = floor_f32(box->rect.x1 - estimated_width - box->text_padding);
        float2_element(result, AXIS2_X) = BUSTER_CLAMP_BOT(float2_element(result, AXIS2_X), box->rect.x0 + box->text_padding);
    }
    float2_element(result, AXIS2_Y) = floor_f32(float2_element(result, AXIS2_Y));
    return result;
}

BUSTER_GLOBAL_LOCAL void ui_draw_command_count_add(u64* count, u64 increment)
{
    BUSTER_CHECK(count != 0 && *count <= (u64)-1 - increment);
    if (count && *count <= (u64)-1 - increment)
    {
        *count += increment;
    }
}

BUSTER_GLOBAL_LOCAL u64 ui_draw_box_command_upper_bound(UI_Box* box)
{
    u64 result = 0;
    if (box && box->visible && ui_rect_has_area(box->rect))
    {
        if (box->flags & UI_BoxFlag_DrawBackgroundBlur)
        {
            ui_draw_command_count_add(&result, 1);
        }
        if (box->flags & UI_BoxFlag_DrawDropShadow)
        {
            ui_draw_command_count_add(&result, 1);
        }
        if (box->flags & UI_BoxFlag_DrawBackground)
        {
            ui_draw_command_count_add(&result, 1);
            if (box->flags & UI_BoxFlag_DrawHotEffects)
            {
                ui_draw_command_count_add(&result, 1);
            }
            if (box->flags & UI_BoxFlag_DrawActiveEffects)
            {
                ui_draw_command_count_add(&result, 2);
            }
        }
        if ((box->flags & UI_BoxFlag_DrawText) && box->string.length != 0)
        {
            ui_draw_command_count_add(&result, 1);
            if (box->text_truncated && !(box->flags & UI_BoxFlag_DisableTextTrunc))
            {
                ui_draw_command_count_add(&result, 1);
            }
            if (box->fastpath_codepoint != 0)
            {
                ui_draw_command_count_add(&result, 1);
            }
        }
        if ((box->flags & UI_BoxFlag_HasFuzzyMatchRanges) && box->fuzzy_match_ranges)
        {
            ui_draw_command_count_add(&result, box->fuzzy_match_range_count);
        }
        if (box->flags & UI_BoxFlag_DrawBorder)
        {
            ui_draw_command_count_add(&result, 4);
        }
        if (box->flags & UI_BoxFlag_DrawSideTop)
        {
            ui_draw_command_count_add(&result, 1);
        }
        if (box->flags & UI_BoxFlag_DrawSideBottom)
        {
            ui_draw_command_count_add(&result, 1);
        }
        if (box->flags & UI_BoxFlag_DrawSideLeft)
        {
            ui_draw_command_count_add(&result, 1);
        }
        if (box->flags & UI_BoxFlag_DrawSideRight)
        {
            ui_draw_command_count_add(&result, 1);
        }
        if (box->flags & UI_BoxFlag_DrawOverlay)
        {
            ui_draw_command_count_add(&result, 1);
            if (!(box->flags & UI_BoxFlag_DisableFocusOverlay) && (box->state_flags & (UI_BoxState_FocusActive | UI_BoxState_FocusEdit)))
            {
                ui_draw_command_count_add(&result, 1);
            }
        }
        if (!(box->flags & UI_BoxFlag_DisableFocusBorder) && (box->state_flags & UI_BoxState_FocusActive))
        {
            ui_draw_command_count_add(&result, 4);
        }
        if (box->flags & UI_BoxFlag_DrawFadeTop)
        {
            ui_draw_command_count_add(&result, 1);
        }
        if (box->flags & UI_BoxFlag_DrawFadeBottom)
        {
            ui_draw_command_count_add(&result, 1);
        }
        if (box->flags & UI_BoxFlag_DrawFadeLeft)
        {
            ui_draw_command_count_add(&result, 1);
        }
        if (box->flags & UI_BoxFlag_DrawFadeRight)
        {
            ui_draw_command_count_add(&result, 1);
        }
        if (box->flags & UI_BoxFlag_Debug)
        {
            ui_draw_command_count_add(&result, 4);
        }
    }

    return result;
}

BUSTER_GLOBAL_LOCAL u64 ui_draw_command_upper_bound(UI_Box* root)
{
    u64 result = 0;
    for (UI_Box* box = root; box; box = ui_box_rec_df_pre(box, root).next)
    {
        if (box == root)
        {
            ui_draw_command_count_add(&result, 5);
        }
        else
        {
            ui_draw_command_count_add(&result, ui_draw_box_command_upper_bound(box));
        }
        if (box->tooltip_visible && box->tooltip_string.length != 0 && ui_rect_has_area(box->tooltip_rect))
        {
            ui_draw_command_count_add(&result, 5);
            ui_draw_command_count_add(&result, box->tooltip_line_count);
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL void ui_draw_command_push(UI_DrawCommand command)
{
    BUSTER_CHECK(ui_state != 0 && ui_state->draw_commands != 0);
    if (ui_state && ui_state->draw_commands)
    {
        if (ui_state->draw_command_count >= ui_state->draw_command_capacity)
        {
            ui_state->draw_commands_complete = false;
            BUSTER_CHECK(false);
            return;
        }
        ui_state->draw_commands[ui_state->draw_command_count++] = command;
    }
}

BUSTER_GLOBAL_LOCAL void ui_draw_command_push_rect(F32Interval2 rect, float4* colors)
{
    UI_DrawCommand command;
    memset(&command, 0, sizeof(command));
    command.kind = UI_DrawCommandKind_Rect;
    command.box = ui_state ? ui_state->draw_command_box : 0;
    command.rect = rect;
    command.clip_rect = (ui_state && ui_state->draw_command_box) ? ui_state->draw_command_box->clip_rect : (F32Interval2){0};
    for (u64 index = 0; index < BUSTER_ARRAY_LENGTH(command.colors); index += 1)
    {
        command.colors[index] = colors[index];
    }
    if (ui_state && ui_state->draw_command_box)
    {
        UI_Box* box = ui_state->draw_command_box;
        command.corner_radii = float4_make(box->corner_radii[CORNER_00], box->corner_radii[CORNER_01], box->corner_radii[CORNER_10], box->corner_radii[CORNER_11]);
    }
    ui_draw_command_push(command);
}

BUSTER_GLOBAL_LOCAL void ui_draw_command_push_background_blur(UI_Box* box, F32Interval2 rect, u32 radius)
{
    UI_DrawCommand command;
    memset(&command, 0, sizeof(command));
    command.kind = UI_DrawCommandKind_BackgroundBlur;
    command.box = box;
    command.rect = rect;
    command.clip_rect = box ? box->clip_rect : (F32Interval2){0};
    command.blur_radius = radius;
    if (box)
    {
        command.corner_radii = float4_make(box->corner_radii[CORNER_00], box->corner_radii[CORNER_01], box->corner_radii[CORNER_10], box->corner_radii[CORNER_11]);
    }
    ui_draw_command_push(command);
}

BUSTER_GLOBAL_LOCAL void ui_draw_command_push_text(String8 text, float2 position, float4 color, F32Interval2 clip_rect)
{
    if (!ui_state || !ui_state->draw_command_box || text.length == 0)
    {
        return;
    }
    UI_Box* box = ui_state->draw_command_box;
    f32 width = (f32)ui_utf8_codepoint_count(text) * box->font_size * 0.60f;
    UI_DrawCommand command;
    memset(&command, 0, sizeof(command));
    command.kind = UI_DrawCommandKind_Text;
    command.box = box;
    command.rect = ui_rect_make(float2_element(position, AXIS2_X), float2_element(position, AXIS2_Y), float2_element(position, AXIS2_X) + width,
                                float2_element(position, AXIS2_Y) + box->font_size);
    command.clip_rect = clip_rect;
    for (u64 index = 0; index < BUSTER_ARRAY_LENGTH(command.colors); index += 1)
    {
        command.colors[index] = color;
    }
    command.corner_radii = float4_make(box->corner_radii[CORNER_00], box->corner_radii[CORNER_01], box->corner_radii[CORNER_10], box->corner_radii[CORNER_11]);
    command.text = text;
    ui_draw_command_push(command);
}

BUSTER_GLOBAL_LOCAL void ui_draw_rect(F32Interval2 rect, float4 color)
{
    float4 colors[] = {color, color, color, color};
    ui_draw_command_push_rect(rect, colors);
    if (ui_state->rendering_window && ui_rect_has_area(rect))
    {
        rendering_window_render_rect(ui_state->rendering_window, (RectDraw){
                                                                     .vertex = rect,
                                                                     .colors = {color, color, color, color},
                                                                 });
    }
}

BUSTER_GLOBAL_LOCAL void ui_draw_border(F32Interval2 rect, float4 color, f32 thickness)
{
    ui_draw_rect(ui_rect_make(rect.x0, rect.y0, rect.x1, rect.y0 + thickness), color);
    ui_draw_rect(ui_rect_make(rect.x0, rect.y1 - thickness, rect.x1, rect.y1), color);
    ui_draw_rect(ui_rect_make(rect.x0, rect.y0, rect.x0 + thickness, rect.y1), color);
    ui_draw_rect(ui_rect_make(rect.x1 - thickness, rect.y0, rect.x1, rect.y1), color);
}

BUSTER_GLOBAL_LOCAL void ui_draw_gradient_rect(F32Interval2 rect, float4 top_left, float4 top_right, float4 bottom_left, float4 bottom_right)
{
    float4 colors[] = {top_left, top_right, bottom_left, bottom_right};
    ui_draw_command_push_rect(rect, colors);
    if (ui_state->rendering_window && ui_rect_has_area(rect))
    {
        rendering_window_render_rect(ui_state->rendering_window, (RectDraw){
                                                                     .vertex = rect,
                                                                     .colors = {top_left, top_right, bottom_left, bottom_right},
                                                                 });
    }
}

BUSTER_GLOBAL_LOCAL F32Interval2 ui_box_draw_rect(UI_Box* box, F32Interval2 rect)
{
    if (box && (box->state_flags & UI_BoxState_Clipped))
    {
        rect = ui_rect_intersect(rect, box->clip_rect);
    }
    return rect;
}

BUSTER_GLOBAL_LOCAL void ui_draw_box_fades(UI_Box* box, F32Interval2 rect)
{
    float4 clear = float4_make(float4_element(box->background_color, 0), float4_element(box->background_color, 1), float4_element(box->background_color, 2), 0.0f);
    float4 solid = float4_make(float4_element(box->background_color, 0), float4_element(box->background_color, 1), float4_element(box->background_color, 2),
                               float4_element(box->background_color, 3) * 0.85f);
    f32 fade_size = BUSTER_MIN(16.0f, BUSTER_MIN(rect.x1 - rect.x0, rect.y1 - rect.y0) * 0.5f);
    if (box->flags & UI_BoxFlag_DrawFadeTop)
    {
        ui_draw_gradient_rect(ui_rect_make(rect.x0, rect.y0, rect.x1, rect.y0 + fade_size), solid, solid, clear, clear);
    }
    if (box->flags & UI_BoxFlag_DrawFadeBottom)
    {
        ui_draw_gradient_rect(ui_rect_make(rect.x0, rect.y1 - fade_size, rect.x1, rect.y1), clear, clear, solid, solid);
    }
    if (box->flags & UI_BoxFlag_DrawFadeLeft)
    {
        ui_draw_gradient_rect(ui_rect_make(rect.x0, rect.y0, rect.x0 + fade_size, rect.y1), solid, clear, solid, clear);
    }
    if (box->flags & UI_BoxFlag_DrawFadeRight)
    {
        ui_draw_gradient_rect(ui_rect_make(rect.x1 - fade_size, rect.y0, rect.x1, rect.y1), clear, solid, clear, solid);
    }
}

BUSTER_GLOBAL_LOCAL String8 ui_text_clip_to_rect(UI_Box* box, String8 text, float2* position, F32Interval2 clip)
{
    if (box)
    {
        if (!ui_rect_has_area(clip) || float2_element(*position, AXIS2_Y) < clip.y0 || float2_element(*position, AXIS2_Y) + box->font_size > clip.y1)
        {
            return (String8){0};
        }
        f32 character_width = BUSTER_MAX(0.001f, box->font_size * 0.60f);
        if (float2_element(*position, AXIS2_X) < clip.x0)
        {
            u64 skip_columns = (u64)((clip.x0 - float2_element(*position, AXIS2_X)) / character_width);
            while (skip_columns < ui_utf8_codepoint_count(text) && float2_element(*position, AXIS2_X) + (f32)skip_columns * character_width < clip.x0)
            {
                skip_columns += 1;
            }
            u64 skip_bytes = ui_utf8_byte_offset_for_columns(text, skip_columns);
            float2_element(*position, AXIS2_X) += (f32)ui_utf8_columns_for_byte_offset(text, skip_bytes) * character_width;
            text = string_slice(text, skip_bytes, text.length);
        }
        if (float2_element(*position, AXIS2_X) >= clip.x1)
        {
            return (String8){0};
        }
        u64 visible_columns = (u64)((clip.x1 - float2_element(*position, AXIS2_X)) / character_width);
        u64 visible_bytes = ui_utf8_byte_offset_for_columns(text, visible_columns);
        text.length = BUSTER_MIN(text.length, visible_bytes);
    }

    return text;
}

BUSTER_GLOBAL_LOCAL void ui_draw_text_clipped(UI_Box* box, String8 text, float4 color, float2* position, F32Interval2 clip_rect)
{
    if (box && text.length != 0)
    {
        text = ui_text_clip_to_rect(box, text, position, clip_rect);
        if (text.length == 0)
        {
            return;
        }
        ui_draw_command_push_text(text, *position, color, clip_rect);
        if (ui_state->rendering && ui_state->rendering_window)
        {
            rendering_window_render_text(ui_state->rendering, ui_state->rendering_window, text, color, RENDER_FONT_TYPE_MONOSPACE,
                                         float2_element(*position, AXIS2_X), float2_element(*position, AXIS2_Y));
        }
    }
}

BUSTER_GLOBAL_LOCAL void ui_draw_text_unclipped(UI_Box* box, String8 text, float4 color, float2 position, F32Interval2 clip_rect)
{
    ui_draw_text_clipped(box, text, color, &position, clip_rect);
}

BUSTER_GLOBAL_LOCAL void ui_draw_box(UI_Box* box)
{
    if (box && box->visible && ui_rect_has_area(box->rect))
    {
        F32Interval2 rect = ui_box_draw_rect(box, box->rect);
        if (!ui_rect_has_area(rect))
        {
            return;
        }

        UI_Box* previous_draw_box = ui_state->draw_command_box;
        ui_state->draw_command_box = box;

        if (box->flags & UI_BoxFlag_DrawBackgroundBlur)
        {
            u32 radius = ui_background_blur_radius_clamp(box->background_blur_radius);
            box->background_blur_radius = radius;
            float4 corner_radii = float4_make(box->corner_radii[CORNER_00], box->corner_radii[CORNER_01], box->corner_radii[CORNER_10], box->corner_radii[CORNER_11]);
            ui_draw_command_push_background_blur(box, rect, radius);
            if (ui_state->rendering_window && !rendering_window_render_background_blur_rounded(ui_state->rendering_window, rect, radius, corner_radii))
            {
                ui_state->draw_renderer_succeeded = false;
            }
        }

        if (box->flags & UI_BoxFlag_DrawDropShadow)
        {
            F32Interval2 shadow = ui_box_draw_rect(box, ui_rect_make(rect.x0 + 4, rect.y0 + 4, rect.x1 + 4, rect.y1 + 4));
            ui_draw_rect(shadow, float4_make(0.0f, 0.0f, 0.0f, 0.22f));
        }

        if (box->flags & UI_BoxFlag_DrawBackground)
        {
            float4 color = box->background_color;
            if (box->flags & UI_BoxFlag_Disabled)
            {
                float4_element(color, 3) *= 0.55f;
            }
            ui_draw_rect(rect, color);

            if (box->flags & UI_BoxFlag_DrawHotEffects)
            {
                f32 t = box->hot_t * (1.0f - box->active_t);
                if (t > 0.01f)
                {
                    ui_draw_rect(rect, float4_make(1.0f, 1.0f, 1.0f, 0.08f * t));
                }
            }

            if (box->flags & UI_BoxFlag_DrawActiveEffects)
            {
                f32 t = box->active_t;
                if (t > 0.01f)
                {
                    f32 h = BUSTER_CLAMP(1.0f, (rect.y1 - rect.y0) * 0.25f, box->font_size * 1.2f);
                    ui_draw_rect(ui_rect_make(rect.x0, rect.y0, rect.x1, rect.y0 + h), float4_make(0.0f, 0.0f, 0.0f, 0.12f * t));
                    ui_draw_rect(ui_rect_make(rect.x0, rect.y1 - h, rect.x1, rect.y1), float4_make(1.0f, 1.0f, 1.0f, 0.06f * t));
                }
            }
        }

        if (box->flags & UI_BoxFlag_DrawText)
        {
            u64 text_length = box->flags & UI_BoxFlag_DisableTextTrunc ? box->string.length : box->text_visible_length;
            String8 text = string_slice(box->string, 0, BUSTER_MIN(text_length, box->string.length));
            float2 p = ui_box_text_position(box);
            F32Interval2 text_clip = ui_box_text_clip_rect(box);
            float4 color = box->text_color;
            if (box->flags & UI_BoxFlag_DrawTextWeak)
            {
                float4_element(color, 3) *= 0.55f;
            }
            if (box->fastpath_codepoint != 0 && text.length != 0)
            {
                u64 position = 0;
                u64 column = 0;
                while (position < text.length)
                {
                    u64 sequence_length = ui_utf8_sequence_length(text, position);
                    sequence_length = sequence_length ? sequence_length : 1;
                    if (ui_utf8_codepoint_at(text, position) == box->fastpath_codepoint)
                    {
                        f32 x0 = float2_element(p, AXIS2_X) + (f32)column * box->font_size * 0.60f;
                        F32Interval2 underline = ui_box_draw_rect(box, ui_rect_make(x0, rect.y1 - 2.0f, x0 + box->font_size * 0.60f, rect.y1));
                        ui_draw_rect(underline, float4_make(0.35f, 0.65f, 1.0f, 0.65f));
                        break;
                    }
                    position += sequence_length;
                    column += 1;
                }
            }
            text = ui_text_clip_to_rect(box, text, &p, text_clip);
            ui_draw_text_clipped(box, text, color, &p, text_clip);
            if (box->text_truncated && !(box->flags & UI_BoxFlag_DisableTextTrunc))
            {
                f32 ellipsis_x = float2_element(p, AXIS2_X) + (f32)ui_utf8_codepoint_count(text) * box->font_size * 0.60f;
                f32 ellipsis_width = box->font_size * 1.80f;
                f32 ellipsis_right = BUSTER_MIN(box->rect.x1 - box->text_padding, text_clip.x1 - box->text_padding);
                if (ellipsis_x + ellipsis_width <= ellipsis_right + 0.001f)
                {
                    float2 ellipsis_position = float2_make(ellipsis_x, float2_element(p, AXIS2_Y));
                    ui_draw_text_clipped(box, S8("..."), color, &ellipsis_position, text_clip);
                }
            }
        }

        if ((box->flags & UI_BoxFlag_HasFuzzyMatchRanges) && box->fuzzy_match_ranges)
        {
            for (u64 range_index = 0; range_index < box->fuzzy_match_range_count; range_index += 1)
            {
                UI_FuzzyMatchRange range = box->fuzzy_match_ranges[range_index];
                u64 first_byte = BUSTER_MIN(range.first, box->text_visible_length);
                u64 last_byte = BUSTER_MIN(range.one_past_last, box->text_visible_length);
                u64 first = ui_utf8_columns_for_byte_offset(box->string, first_byte);
                u64 last = ui_utf8_columns_for_byte_offset(box->string, last_byte);
                if (last > first)
                {
                    float2 text_position = ui_box_text_position(box);
                    f32 x0 = float2_element(text_position, AXIS2_X) + (f32)first * box->font_size * 0.60f;
                    f32 x1 = float2_element(text_position, AXIS2_X) + (f32)last * box->font_size * 0.60f;
                    F32Interval2 highlight = ui_box_draw_rect(box, ui_rect_make(x0, rect.y1 - 2.0f, x1, rect.y1));
                    ui_draw_rect(highlight, box->border_color);
                }
            }
        }

        if (box->flags & UI_BoxFlag_DrawBorder)
        {
            ui_draw_border(rect, box->border_color, 1.0f);
        }
        if (box->flags & UI_BoxFlag_DrawSideTop)
        {
            ui_draw_rect(ui_rect_make(rect.x0, rect.y0, rect.x1, rect.y0 + 1), box->border_color);
        }
        if (box->flags & UI_BoxFlag_DrawSideBottom)
        {
            ui_draw_rect(ui_rect_make(rect.x0, rect.y1 - 1, rect.x1, rect.y1), box->border_color);
        }
        if (box->flags & UI_BoxFlag_DrawSideLeft)
        {
            ui_draw_rect(ui_rect_make(rect.x0, rect.y0, rect.x0 + 1, rect.y1), box->border_color);
        }
        if (box->flags & UI_BoxFlag_DrawSideRight)
        {
            ui_draw_rect(ui_rect_make(rect.x1 - 1, rect.y0, rect.x1, rect.y1), box->border_color);
        }

        if (box->flags & UI_BoxFlag_DrawOverlay)
        {
            ui_draw_rect(rect, float4_make(1.0f, 1.0f, 1.0f, 0.035f));
            if (!(box->flags & UI_BoxFlag_DisableFocusOverlay) && (box->state_flags & (UI_BoxState_FocusActive | UI_BoxState_FocusEdit)))
            {
                ui_draw_rect(rect, float4_make(1.0f, 1.0f, 1.0f, 0.05f));
            }
        }
        if (!(box->flags & UI_BoxFlag_DisableFocusBorder) && (box->state_flags & UI_BoxState_FocusActive))
        {
            ui_draw_border(rect, float4_make(0.35f, 0.65f, 1.0f, 0.85f), 1.0f);
        }
        ui_draw_box_fades(box, rect);

        if (box->flags & UI_BoxFlag_Debug)
        {
            ui_draw_border(rect, float4_make(1.0f, 0.0f, 1.0f, 0.65f), 1.0f);
        }
        ui_state->draw_command_box = previous_draw_box;
    }
}

BUSTER_GLOBAL_LOCAL void ui_draw_tree(UI_Box* root)
{
    for (UI_Box* box = root; box; box = ui_box_rec_df_pre(box, root).next)
    {
        if (box != root)
        {
            ui_draw_box(box);
        }
    }
}

BUSTER_GLOBAL_LOCAL void ui_draw_tooltip_text(UI_Box* box)
{
    if (!box || box->tooltip_string.length == 0)
    {
        return;
    }

    f32 line_height = BUSTER_MAX(1.0f, box->font_size * 1.35f);
    f32 x = box->tooltip_rect.x0 + box->text_padding;
    f32 y = box->tooltip_rect.y0 + box->text_padding;
    u64 line_start = 0;
    for (u64 position = 0; position <= box->tooltip_string.length; position += 1)
    {
        if (position == box->tooltip_string.length || box->tooltip_string.pointer[position] == '\n')
        {
            String8 line = string_slice(box->tooltip_string, line_start, position);
            float2 line_position = float2_make(x, y);
            ui_draw_text_unclipped(box, line, box->text_color, line_position, box->tooltip_rect);
            y += line_height;
            line_start = position + 1;
        }
    }
}

BUSTER_GLOBAL_LOCAL void ui_draw_tooltips(UI_Box* root)
{
    for (UI_Box* box = root; box; box = ui_box_rec_df_pre(box, root).next)
    {
        if (!box->tooltip_visible || box->string.length == 0 || !ui_rect_has_area(box->tooltip_rect))
        {
            continue;
        }
        UI_Box* previous_draw_box = ui_state->draw_command_box;
        ui_state->draw_command_box = box;
        ui_draw_rect(box->tooltip_rect, float4_make(0.08f, 0.08f, 0.095f, 0.98f));
        ui_draw_border(box->tooltip_rect, float4_make(0.35f, 0.38f, 0.46f, 1.0f), 1.0f);
        ui_draw_tooltip_text(box);
        ui_state->draw_command_box = previous_draw_box;
    }
}

bool ui_draw(void)
{
    UI_Box* root = ui_state->root;
    ui_state->draw_commands = 0;
    ui_state->draw_command_count = 0;
    ui_state->draw_command_capacity = 0;
    ui_state->draw_commands_complete = false;
    ui_state->draw_renderer_succeeded = true;
    ui_state->draw_command_box = 0;
    if (!root)
    {
        ui_state->draw_commands_complete = true;
        return true;
    }

    ui_state->draw_command_capacity = ui_draw_command_upper_bound(root);
    if (ui_state->draw_command_capacity > (u64)-1 / sizeof(UI_DrawCommand))
    {
        BUSTER_CHECK(false);
        return false;
    }
    u64 position = ui_build_arena()->position;
    bool capacity_fits = ui_arena_try_advance(ui_build_arena(), &position, ui_state->draw_command_capacity * sizeof(UI_DrawCommand),
                                              BUSTER_ALIGN_OF(UI_DrawCommand));
    BUSTER_CHECK(capacity_fits);
    if (!capacity_fits)
    {
        ui_state->draw_command_capacity = 0;
        return false;
    }
    ui_state->draw_commands = arena_allocate(ui_build_arena(), UI_DrawCommand, ui_state->draw_command_capacity);
    ui_state->draw_commands_complete = true;
    ui_state->draw_command_box = root;

    //- rjf: draw UI background & simple window border
    ui_draw_rect(root->rect, float4_make(0.08f, 0.08f, 0.095f, 1.0f));
    ui_draw_border(root->rect, float4_make(0.18f, 0.18f, 0.22f, 1.0f), 1.0f);

    // Keep tree order for all boxes. The renderer has no bucket submission
    // operation, and a global second pass would paint bucket parents over
    // their normal children. The dependency bit and draw_pass remain visible
    // to a future renderer command boundary.
    ui_draw_tree(root);
    ui_draw_tooltips(root);
    ui_state->draw_command_box = 0;
    return ui_state->draw_commands_complete && ui_state->draw_renderer_succeeded;
}
