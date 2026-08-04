// Raddebugger-inspired immediate-mode UI core, adapted to Buster's renderer/window layer.

#include <buster/lib/ui_core.h>
#include <buster/lib/string.h>
#include <buster/lib/float.h>
#include <buster/lib/os.h>

BUSTER_V_IMPL UI_State* ui_state;

BUSTER_GLOBAL_LOCAL void ui_process_focus_navigation(void);
BUSTER_GLOBAL_LOCAL void ui_prune_focus_keys(void);

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
        node->v.string = string_duplicate_arena(arena, node->v.string, false);
    }
    if (node->v.paths.length != 0)
    {
        SliceString8 paths = {
            .pointer = arena_allocate(arena, String8, node->v.paths.length),
            .length = node->v.paths.length,
        };
        for (u64 i = 0; i < paths.length; i += 1)
        {
            paths.pointer[i] = string_duplicate_arena(arena, node->v.paths.pointer[i], false);
        }
        node->v.paths = paths;
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
    u64 index = string_first_sequence(string, S8("###"));
    if (index < string.length)
    {
        result = string_slice(string, index, string.length);
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
        UI_BOX_FLAG_INFO(28, DrawBackgroundBlur, Appearance, true);
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
        UI_BOX_FLAG_INFO(46, Clip, Appearance, false);
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
    box->fixed_position = float2_make(ui_top_fixed_x(), ui_top_fixed_y());
    box->fixed_size = float2_make(ui_top_fixed_width(), ui_top_fixed_height());
    box->min_size = float2_make(ui_top_min_width(), ui_top_min_height());
    box->pref_size[AXIS2_X] = ui_top_pref_width();
    box->pref_size[AXIS2_Y] = ui_top_pref_height();
    box->child_layout_axis = ui_top_child_layout_axis();
    box->background_color = ui_top_background_color();
    box->text_color = ui_top_text_color();
    box->border_color = ui_top_border_color();
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
    if (effective_flags & UI_BoxFlag_DisableIDString)
    {
        key_string = ui_display_part_from_key_string(string);
    }
    UI_Key key = ui_key_from_string(ui_key_zero(), key_string);
    UI_Box* box = ui_build_box_from_key(flags, key);
    box->raw_string = string;
    box->display_string = ui_display_part_from_key_string(string);
    box->string = box->display_string;
    return box;
}

UI_Box* ui_build_box_from_stringf(UI_BoxFlags flags, String8 format, ...)
{
    Arena* arena = ui_build_arena();
    va_list args;
    va_start(args, format);
    String8 string = string_format_va(arena, format, args);
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
    String8 string = string_format_va(arena, format, args);
    va_end(args);
    return ui_box_make(flags, string);
}

void ui_box_set_display_string(UI_Box* box, String8 string)
{
    if (box)
    {
        box->display_string = string;
        box->string = string;
        box->flags |= UI_BoxFlag_HasDisplayString;
    }
}

void ui_box_set_fuzzy_match_ranges(UI_Box* box, UI_FuzzyMatchRange* ranges, u64 count)
{
    if (box)
    {
        box->fuzzy_match_ranges = ranges;
        box->fuzzy_match_range_count = count;
        if (count != 0)
        {
            box->flags |= UI_BoxFlag_HasFuzzyMatchRanges;
        }
        else
        {
            box->flags &= ~UI_BoxFlag_HasFuzzyMatchRanges;
        }
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
    if (!box)
    {
        return result;
    }
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
    return result;
}

BUSTER_GLOBAL_LOCAL void ui_prune_untouched_boxes(void)
{
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
        }
    }
}

BUSTER_GLOBAL_LOCAL bool ui_wm_key_is_mouse(WmKey key)
{
    bool result = (key >= WM_KEY_MOUSE_LEFT && key <= WM_KEY_MOUSE_FORWARD);
    return result;
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
    ui_state->root = 0;
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

    for (UI_EventNode* event = events.first; event; event = event->next)
    {
        switch (event->v.kind)
        {
        case UI_EventKind_MouseMove:
        case UI_EventKind_Scroll:
        {
            ui_state->mouse = event->v.pos;
        }
        break;
        case UI_EventKind_Press:
        case UI_EventKind_Release:
        {
            if (ui_wm_key_is_mouse(event->v.key))
            {
                ui_state->mouse = event->v.pos;
            }
        }
        break;
        default:
            break;
        }
    }

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
            if (!active_box || (active_box->flags & UI_BoxFlag_Disabled))
            {
                ui_state->active_box_key[i] = ui_key_zero();
            }
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
        result = (f32)box->string.length * box->font_size * 0.60f + padding;
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
            f32 total_size = 0.0f;
            f32 total_weighted_size = 0.0f;
            for (UI_Box* child = box->first; child; child = child->next)
            {
                if (!(child->flags & (UI_BoxFlag_FloatingX << axis)))
                {
                    if (axis == box->child_layout_axis)
                    {
                        total_size += float2_element(child->fixed_size, axis);
                    }
                    else
                    {
                        total_size = BUSTER_MAX(total_size, float2_element(child->fixed_size, axis));
                    }
                    total_weighted_size += float2_element(child->fixed_size, axis) * (1.0f - child->pref_size[axis].strictness);
                }
            }

            f32 violation = total_size - allowed_size;
            if (violation > 0 && total_weighted_size > 0)
            {
                for (UI_Box* child = box->first; child; child = child->next)
                {
                    if (!(child->flags & (UI_BoxFlag_FloatingX << axis)))
                    {
                        f32 fixup_budget = float2_element(child->fixed_size, axis) * (1.0f - child->pref_size[axis].strictness);
                        f32 fixup_pct = BUSTER_CLAMP(0.0f, violation / total_weighted_size, 1.0f);
                        float2_element(child->fixed_size, axis) -= fixup_budget * fixup_pct;
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
                f32 animation_t = ui_state->frame_time > 0.0 ? BUSTER_CLAMP(0.0f, (f32)(ui_state->frame_time * 12.0), 1.0f) : 1.0f;
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
        box->visible = ui_rect_has_area(box->rect) && ui_rect_has_area(clip);
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
        if (ui_key_match(box->key, ui_state->focus_hot_key))
        {
            box->state_flags |= UI_BoxState_FocusHot;
        }
        if (ui_key_match(box->key, ui_state->focus_active_key))
        {
            box->state_flags |= UI_BoxState_FocusActive;
        }
        if (ui_key_match(box->key, ui_state->focus_edit_key))
        {
            box->state_flags |= UI_BoxState_FocusEdit;
        }

        f32 available_width = BUSTER_MAX(0.0f, box->rect.x1 - box->rect.x0 - box->text_padding * 2.0f);
        if (box->flags & UI_BoxFlag_Clip)
        {
            available_width = BUSTER_MAX(0.0f, BUSTER_MIN(available_width, box->clip_rect.x1 - box->rect.x0 - box->text_padding));
        }
        f32 character_width = BUSTER_MAX(0.001f, box->font_size * 0.60f);
        u64 visible_length = box->string.length;
        if (!(box->flags & UI_BoxFlag_DisableTextTrunc))
        {
            visible_length = BUSTER_MIN(box->string.length, (u64)(available_width / character_width));
        }
        box->text_visible_length = visible_length;
        box->text_truncated = visible_length < box->string.length;
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
}

void ui_build_end(void)
{
    if (ui_state->stacks.parent_length != 0)
    {
        BUSTER_UNUSED(ui_pop_parent());
    }

    ui_prune_untouched_boxes();
    ui_prune_focus_keys();
    ui_process_focus_navigation();

    if (ui_state->root)
    {
        for (Axis2 axis = 0; axis < AXIS2_COUNT; axis += 1)
        {
            ui_layout_root(ui_state->root, axis);
        }
        ui_layout_compute_clips(ui_state->root);
    }

    for (u64 slot_index = 0; slot_index < ui_state->box_table_size; slot_index += 1)
    {
        for (UI_Box* box = ui_state->box_table[slot_index].first; box; box = box->hash_next)
        {
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

                box->first->corner_radii[CORNER_00] = box->corner_radii[CORNER_00];
                box->first->corner_radii[CORNER_10] = box->corner_radii[CORNER_10];
                box->last->corner_radii[CORNER_01] = box->corner_radii[CORNER_01];
                box->last->corner_radii[CORNER_11] = box->corner_radii[CORNER_11];
            }
        }
    }

    f32 hot_rate = 0.35f;
    f32 active_rate = 0.45f;
    for (u64 slot_index = 0; slot_index < ui_state->box_table_size; slot_index += 1)
    {
        for (UI_Box* box = ui_state->box_table[slot_index].first; box; box = box->hash_next)
        {
            bool hot = ui_key_match(box->key, ui_state->hot_box_key);
            bool active = ui_key_match(box->key, ui_state->active_box_key[UI_MouseButtonKind_Left]);
            box->hot_t += ((f32)hot - box->hot_t) * hot_rate;
            box->active_t += ((f32)active - box->active_t) * active_rate;
            ui_state->is_animating = ui_state->is_animating || (box->hot_t > 0.01f && box->hot_t < 0.99f) || (box->active_t > 0.01f && box->active_t < 0.99f);
        }
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
    if (!box || !ui_rect_contains(box->rect, point))
    {
        return false;
    }
    if (box->clip_rect.x1 > box->clip_rect.x0 && box->clip_rect.y1 > box->clip_rect.y0 && !ui_rect_contains(box->clip_rect, point))
    {
        return false;
    }
    if ((box->flags & UI_BoxFlag_DisableTruncatedHover) && box->text_truncated)
    {
        return false;
    }
    return true;
}

BUSTER_GLOBAL_LOCAL UI_Box* ui_topmost_box_at_point(float2 point, UI_BoxFlags required_flags, bool allow_disabled)
{
    UI_Box* result = 0;
    u64 result_order = 0;
    for (u64 slot_index = 0; slot_index < ui_state->box_table_size; slot_index += 1)
    {
        for (UI_Box* box = ui_state->box_table[slot_index].first; box; box = box->hash_next)
        {
            if (box->last_touched_build_index != ui_state->build_index || !(box->flags & required_flags) || !ui_box_contains_point(box, point))
            {
                continue;
            }
            if (!allow_disabled && (box->flags & UI_BoxFlag_Disabled))
            {
                continue;
            }
            if (!result || box->build_order >= result_order)
            {
                result = box;
                result_order = box->build_order;
            }
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool ui_box_focusable(UI_Box* box, bool active)
{
    if (!box || (box->flags & UI_BoxFlag_FocusNavSkip))
    {
        return false;
    }
    if (box->flags & UI_BoxFlag_Disabled)
    {
        return (active && (box->flags & UI_BoxFlag_FocusActiveDisabled)) || (!active && (box->flags & UI_BoxFlag_FocusHotDisabled));
    }
    return active ? !!(box->flags & (UI_BoxFlag_FocusActive | UI_BoxFlag_KeyboardClickable | UI_BoxFlag_ClickToFocus | UI_BoxFlag_DefaultFocusNavX |
                                     UI_BoxFlag_DefaultFocusNavY | UI_BoxFlag_DefaultFocusEdit))
                  : !!(box->flags & (UI_BoxFlag_FocusHot | UI_BoxFlag_FocusActive | UI_BoxFlag_KeyboardClickable | UI_BoxFlag_ClickToFocus | UI_BoxFlag_DefaultFocusNavX |
                                     UI_BoxFlag_DefaultFocusNavY | UI_BoxFlag_DefaultFocusEdit));
}

BUSTER_GLOBAL_LOCAL void ui_set_focus(UI_Box* box, bool edit)
{
    UI_Key key = box ? box->key : ui_key_zero();
    UI_Key edit_key = edit ? key : ui_key_zero();
    if (!ui_key_match(ui_state->focus_active_key, key) || !ui_key_match(ui_state->focus_edit_key, edit_key))
    {
        ui_state->focus_changed = 1;
    }
    ui_state->focus_hot_key = key;
    ui_state->focus_active_key = key;
    ui_state->focus_edit_key = edit_key;
}

BUSTER_GLOBAL_LOCAL bool ui_box_is_current(UI_Box* box)
{
    return box && box->last_touched_build_index == ui_state->build_index;
}

BUSTER_GLOBAL_LOCAL void ui_signal_add_focus_state(UI_Signal* signal, UI_Box* box)
{
    if (ui_key_match(ui_state->focus_active_key, box->key) || ui_key_match(ui_state->focus_edit_key, box->key))
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

    bool mouse_over = ui_box_contains_point(box, ui_state->mouse);
    if (mouse_over)
    {
        sig.f |= UI_SignalFlag_MouseOver;
        sig.mouse_over = 1;
    }

    bool disabled = !!(box->flags & UI_BoxFlag_Disabled);
    if (mouse_over && ui_box_focusable(box, false))
    {
        ui_state->focus_hot_key = box->key;
        sig.f |= UI_SignalFlag_Hovering;
        sig.hovering = 1;
    }
    if ((box->flags & UI_BoxFlag_MouseClickable) && mouse_over && !disabled)
    {
        UI_Box* pointer_target = ui_topmost_box_at_point(ui_state->mouse, UI_BoxFlag_MouseClickable, false);
        if (!pointer_target || ui_key_match(pointer_target->key, box->key))
        {
            ui_state->hot_box_key = box->key;
        }
    }

    ui_signal_add_focus_state(&sig, box);
    if (disabled && !(box->flags & (UI_BoxFlag_FocusHotDisabled | UI_BoxFlag_FocusActiveDisabled)))
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
        if (event->kind == UI_EventKind_Scroll && (box->flags & UI_BoxFlag_Scroll) && event_in_bounds && !disabled)
        {
            UI_Box* scroll_target = ui_topmost_box_at_point(event->pos, UI_BoxFlag_Scroll, false);
            if (!scroll_target || ui_key_match(scroll_target->key, box->key))
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
        }
        else if (event->kind == UI_EventKind_FileDrop && (box->flags & UI_BoxFlag_DropSite) && event_in_bounds && !disabled)
        {
            UI_Box* drop_target = ui_topmost_box_at_point(event->pos, UI_BoxFlag_DropSite, false);
            if (!drop_target || ui_key_match(drop_target->key, box->key))
            {
                sig.f |= UI_SignalFlag_Dropped;
                sig.dropped = 1;
                ui_eat_event(event);
            }
        }
        else if ((box->flags & UI_BoxFlag_MouseClickable) && is_mouse && event->kind == UI_EventKind_Press && event_in_bounds && !disabled)
        {
            UI_Box* pointer_target = ui_topmost_box_at_point(event->pos, UI_BoxFlag_MouseClickable, false);
            if (!pointer_target || ui_key_match(pointer_target->key, box->key))
            {
                ui_state->hot_box_key = box->key;
                ui_state->active_box_key[button] = box->key;
                if (button == UI_MouseButtonKind_Left)
                {
                    sig.f |= UI_SignalFlag_LeftPressed;
                    sig.pressed_left = 1;
                }
                if (box->flags & (UI_BoxFlag_ClickToFocus | UI_BoxFlag_FocusActive | UI_BoxFlag_DefaultFocusEdit))
                {
                    UI_Key old_focus_active_key = ui_state->focus_active_key;
                    UI_Key old_focus_edit_key = ui_state->focus_edit_key;
                    ui_set_focus(box, !!(box->flags & UI_BoxFlag_DefaultFocusEdit));
                    if (!ui_key_match(old_focus_active_key, ui_state->focus_active_key) || !ui_key_match(old_focus_edit_key, ui_state->focus_edit_key))
                    {
                        sig.f |= UI_SignalFlag_FocusChanged;
                        sig.focus_changed = 1;
                    }
                }
                ui_eat_event(event);
            }
        }
        else if ((box->flags & UI_BoxFlag_MouseClickable) && is_mouse && event->kind == UI_EventKind_Release &&
                 ui_key_match(ui_state->active_box_key[button], box->key) && !disabled)
        {
            ui_state->active_box_key[button] = ui_key_zero();
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
                 ui_key_match(ui_state->focus_active_key, box->key) && !disabled)
        {
            sig.f |= UI_SignalFlag_KeyboardPressed;
            sig.clicked_left = 1;
            sig.key = event->key;
            sig.modifiers = event->modifiers;
            ui_eat_event(event);
        }
    }

    if (!disabled && ui_key_match(ui_state->active_box_key[UI_MouseButtonKind_Left], box->key))
    {
        sig.f |= UI_SignalFlag_Dragging;
        sig.dragging = 1;
    }
    if (ui_key_match(ui_state->focus_active_key, box->key) || ui_key_match(ui_state->focus_edit_key, box->key))
    {
        sig.f |= UI_SignalFlag_Focused;
        sig.focused = 1;
    }
    return sig;
}

BUSTER_GLOBAL_LOCAL UI_Box* ui_focus_navigation_candidate(UI_Key current_key, UI_BoxFlags axis_flag, bool backwards)
{
    UI_Box* first = 0;
    UI_Box* last = 0;
    UI_Box* after = 0;
    UI_Box* before = 0;
    u64 current_order = 0;
    bool current_found = false;
    for (u64 slot_index = 0; slot_index < ui_state->box_table_size; slot_index += 1)
    {
        for (UI_Box* box = ui_state->box_table[slot_index].first; box; box = box->hash_next)
        {
            if (!ui_box_is_current(box) || !(box->flags & axis_flag) || !ui_box_focusable(box, true))
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
            if (ui_key_match(box->key, current_key))
            {
                current_found = true;
                current_order = box->build_order;
            }
        }
    }
    if (current_found)
    {
        for (u64 slot_index = 0; slot_index < ui_state->box_table_size; slot_index += 1)
        {
            for (UI_Box* box = ui_state->box_table[slot_index].first; box; box = box->hash_next)
            {
                if (!ui_box_is_current(box) || !(box->flags & axis_flag) || !ui_box_focusable(box, true))
                {
                    continue;
                }
                if (box->build_order > current_order && (!after || box->build_order < after->build_order))
                {
                    after = box;
                }
                if (box->build_order < current_order && (!before || box->build_order > before->build_order))
                {
                    before = box;
                }
            }
        }
    }
    UI_Box* result = 0;
    if (!current_found)
    {
        result = backwards ? last : first;
    }
    else
    {
        result = backwards ? (before ? before : last) : (after ? after : first);
    }
    return result;
}

BUSTER_GLOBAL_LOCAL void ui_process_focus_navigation(void)
{
    UI_EventIterator iterator = ui_event_iterator_initialize(ui_state);
    UI_Event* event;
    while ((event = ui_next_event(&iterator)))
    {
        if (event->kind != UI_EventKind_Press)
        {
            continue;
        }
        UI_BoxFlags axis_flag = 0;
        bool backwards = false;
        if (event->key == WM_KEY_TAB)
        {
            axis_flag = UI_BoxFlag_DefaultFocusNavX | UI_BoxFlag_DefaultFocusNavY;
            backwards = !!(event->modifiers & (1u << WM_MODIFIER_SHIFT));
        }
        else if (event->key == WM_KEY_LEFT)
        {
            axis_flag = UI_BoxFlag_DefaultFocusNavX;
            backwards = true;
        }
        else if (event->key == WM_KEY_RIGHT)
        {
            axis_flag = UI_BoxFlag_DefaultFocusNavX;
        }
        else if (event->key == WM_KEY_UP)
        {
            axis_flag = UI_BoxFlag_DefaultFocusNavY;
            backwards = true;
        }
        else if (event->key == WM_KEY_DOWN)
        {
            axis_flag = UI_BoxFlag_DefaultFocusNavY;
        }
        if (axis_flag)
        {
            UI_Box* candidate = ui_focus_navigation_candidate(ui_state->focus_active_key, axis_flag, backwards);
            if (candidate)
            {
                bool edit = !!(candidate->flags & UI_BoxFlag_DefaultFocusEdit);
                ui_set_focus(candidate, edit);
                ui_eat_event(event);
            }
        }
    }
}

BUSTER_GLOBAL_LOCAL void ui_prune_focus_keys(void)
{
    UI_Key zero = ui_key_zero();
    if (!ui_key_match(ui_state->focus_hot_key, zero))
    {
        UI_Box* box = ui_box_from_key(ui_state->focus_hot_key);
        if (!box || !ui_box_is_current(box))
        {
            ui_state->focus_hot_key = zero;
        }
    }
    if (!ui_key_match(ui_state->focus_active_key, zero))
    {
        UI_Box* box = ui_box_from_key(ui_state->focus_active_key);
        if (!box || !ui_box_is_current(box))
        {
            ui_state->focus_active_key = zero;
        }
    }
    if (!ui_key_match(ui_state->focus_edit_key, zero))
    {
        UI_Box* box = ui_box_from_key(ui_state->focus_edit_key);
        if (!box || !ui_box_is_current(box))
        {
            ui_state->focus_edit_key = zero;
        }
    }
}

BUSTER_GLOBAL_LOCAL float2 ui_box_text_position(UI_Box* box)
{
    float2 rect_dim = ui_rect_dim(box->rect);
    float2 result = float2_make(box->rect.x0 + box->text_padding, box->rect.y0 + BUSTER_MAX(0.0f, (float2_element(rect_dim, AXIS2_Y) - box->font_size) * 0.5f));
    f32 estimated_width = (f32)(box->text_visible_length ? box->text_visible_length : box->string.length) * box->font_size * 0.60f;
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

BUSTER_GLOBAL_LOCAL void ui_draw_rect(F32Interval2 rect, float4 color)
{
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

BUSTER_GLOBAL_LOCAL void ui_draw_box(UI_Box* box)
{
    if (!box || !box->visible || !ui_rect_has_area(box->rect))
    {
        return;
    }

    F32Interval2 rect = ui_box_draw_rect(box, box->rect);
    if (!ui_rect_has_area(rect))
    {
        return;
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
        if ((box->flags & UI_BoxFlag_DrawTextFastpathCodepoint) && text.length > 1)
        {
            u64 codepoint_length = 1;
            while (codepoint_length < text.length && (((u8)text.pointer[codepoint_length]) & 0xc0u) == 0x80u)
            {
                codepoint_length += 1;
            }
            text.length = codepoint_length;
        }
        float2 p = ui_box_text_position(box);
        float4 color = box->text_color;
        if (box->flags & UI_BoxFlag_DrawTextWeak)
        {
            float4_element(color, 3) *= 0.55f;
        }
        if (ui_state->rendering && ui_state->rendering_window && text.length != 0)
        {
            rendering_window_render_text(ui_state->rendering, ui_state->rendering_window, text, color, RENDER_FONT_TYPE_MONOSPACE,
                                         float2_element(p, AXIS2_X), float2_element(p, AXIS2_Y));
        }
        if (box->text_truncated && !(box->flags & UI_BoxFlag_DisableTextTrunc) && text.length != 0)
        {
            f32 ellipsis_x = float2_element(p, AXIS2_X) + (f32)text.length * box->font_size * 0.60f;
            if (ellipsis_x + box->font_size * 1.8f < rect.x1)
            {
                rendering_window_render_text(ui_state->rendering, ui_state->rendering_window, S8("..."), color, RENDER_FONT_TYPE_MONOSPACE,
                                             ellipsis_x, float2_element(p, AXIS2_Y));
            }
        }
    }

    if ((box->flags & UI_BoxFlag_HasFuzzyMatchRanges) && box->fuzzy_match_ranges)
    {
        for (u64 range_index = 0; range_index < box->fuzzy_match_range_count; range_index += 1)
        {
            UI_FuzzyMatchRange range = box->fuzzy_match_ranges[range_index];
            u64 first = BUSTER_MIN(range.first, box->text_visible_length);
            u64 last = BUSTER_MIN(range.one_past_last, box->text_visible_length);
            if (last > first)
            {
                f32 x0 = box->rect.x0 + box->text_padding + (f32)first * box->font_size * 0.60f;
                f32 x1 = box->rect.x0 + box->text_padding + (f32)last * box->font_size * 0.60f;
                ui_draw_rect(ui_rect_make(x0, rect.y1 - 2.0f, x1, rect.y1), box->border_color);
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
}

BUSTER_GLOBAL_LOCAL void ui_draw_tree(UI_Box* root, bool bucket)
{
    for (UI_Box* box = root; box; box = ui_box_rec_df_pre(box, root).next)
    {
        if (box != root && !!(box->flags & UI_BoxFlag_DrawBucket) == bucket)
        {
            ui_draw_box(box);
        }
    }
}

void ui_draw(void)
{
    UI_Box* root = ui_state->root;
    if (!root || !ui_state->rendering_window || !ui_state->rendering)
    {
        return;
    }

    //- rjf: draw UI background & simple window border
    ui_draw_rect(root->rect, float4_make(0.08f, 0.08f, 0.095f, 1.0f));
    ui_draw_border(root->rect, float4_make(0.18f, 0.18f, 0.22f, 1.0f), 1.0f);

    ui_draw_tree(root, false);
    ui_draw_tree(root, true);
}
