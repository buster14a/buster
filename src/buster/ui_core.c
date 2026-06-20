// Raddebugger-inspired immediate-mode UI core, adapted to Buster's renderer/window layer.

#include <buster/ui_core.h>
#include <buster/string.h>
#include <buster/float.h>
#include <buster/os.h>

BUSTER_V_IMPL UI_State* ui_state;

BUSTER_GLOBAL_LOCAL F32Interval2 ui_rect_make(f32 x0, f32 y0, f32 x1, f32 y1)
{
    return (F32Interval2){ .x0 = x0, .y0 = y0, .x1 = x1, .y1 = y1 };
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

BUSTER_GLOBAL_LOCAL float4 ui_color_mul_alpha(float4 color, f32 alpha)
{
    float4_element(color, 3) *= alpha;
    return color;
}

BUSTER_GLOBAL_LOCAL UI_Box* ui_nil_box(void)
{
    return 0;
}

BUSTER_GLOBAL_LOCAL bool ui_box_is_nil(UI_Box* box)
{
    return box == 0;
}

BUSTER_GLOBAL_LOCAL void ui_stack_reset(UI_State* state)
{
    memset(&state->stacks, 0, sizeof(state->stacks));
}

#define UI_STACK_TOP_IMPL(name, type) \
    type ui_top_##name(void) \
    { \
        type result = ui_state->stack_nulls.name; \
        if (ui_state->stacks.name##_length != 0) \
        { \
            result = ui_state->stacks.name[ui_state->stacks.name##_length - 1]; \
        } \
        return result; \
    }

#define UI_STACK_PUSH_IMPL(name, type) \
    type ui_push_##name(type v) \
    { \
        type old = ui_top_##name(); \
        BUSTER_CHECK(ui_state->stacks.name##_length < UI_STACK_CAPACITY); \
        ui_state->stacks.name[ui_state->stacks.name##_length++] = v; \
        ui_state->stacks.name##_auto_pop = 0; \
        return old; \
    }

#define UI_STACK_POP_IMPL(name, type) \
    type ui_pop_##name(void) \
    { \
        BUSTER_CHECK(ui_state->stacks.name##_length != 0); \
        type result = ui_state->stacks.name[--ui_state->stacks.name##_length]; \
        ui_state->stacks.name##_auto_pop = 0; \
        return result; \
    }

#define UI_STACK_SET_NEXT_IMPL(name, type) \
    type ui_set_next_##name(type v) \
    { \
        type old = ui_top_##name(); \
        BUSTER_CHECK(ui_state->stacks.name##_length < UI_STACK_CAPACITY); \
        ui_state->stacks.name[ui_state->stacks.name##_length++] = v; \
        ui_state->stacks.name##_auto_pop = 1; \
        return old; \
    }

#define UI_STACK_IMPL(name, type) \
    UI_STACK_TOP_IMPL(name, type) \
    UI_STACK_PUSH_IMPL(name, type) \
    UI_STACK_POP_IMPL(name, type) \
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
#define UI_STACK_AUTO_POP(name) do { if (state->stacks.name##_auto_pop && state->stacks.name##_length != 0) { state->stacks.name##_length -= 1; state->stacks.name##_auto_pop = 0; } } while (0)
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
        if (node->prev) { node->prev->next = node->next; }
        if (node->next) { node->next->prev = node->prev; }
        if (list->first == node) { list->first = node->next; }
        if (list->last == node) { list->last = node->prev; }
        if (list->count != 0) { list->count -= 1; }
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
    UI_Key result = { value };
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

UI_Size ui_size(UI_SizeKind kind, f32 value, f32 strictness)
{
    UI_Size result = { .kind = kind, .value = value, .strictness = strictness };
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
        .pref_width = { .kind = UI_SizeKind_Null, .value = 0, .strictness = 1 },
        .pref_height = { .kind = UI_SizeKind_Null, .value = 0, .strictness = 1 },
        .min_width = 0,
        .min_height = 0,
        .flags = 0,
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
    if (box->hash_prev) { box->hash_prev->hash_next = box->hash_next; }
    if (box->hash_next) { box->hash_next->hash_prev = box->hash_prev; }
    if (slot->first == box) { slot->first = box->hash_next; }
    if (slot->last == box) { slot->last = box->hash_prev; }
    box->hash_next = 0;
    box->hash_prev = 0;
}

BUSTER_GLOBAL_LOCAL void ui_box_equip_tree_links(UI_Box* box, UI_Box* parent)
{
    box->parent = parent;
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
    UI_Key key = ui_key_from_string(ui_key_zero(), string);
    UI_Box* box = ui_build_box_from_key(flags, key);
    box->string = ui_display_part_from_key_string(string);
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

UI_BoxFlags ui_box_flags_from_widget_flags(UI_WidgetFlags flags)
{
    UI_BoxFlags result = 0;
    if (flags.disabled) { result |= UI_BoxFlag_Disabled; }
    if (flags.mouse_clickable) { result |= UI_BoxFlag_MouseClickable; }
    if (flags.keyboard_pressable) { result |= UI_BoxFlag_KeyboardClickable; }
    if (flags.draw_text) { result |= UI_BoxFlag_DrawText; }
    if (flags.draw_background) { result |= UI_BoxFlag_DrawBackground; }
    if (flags.overflow_x) { result |= UI_BoxFlag_AllowOverflowX; }
    if (flags.overflow_y) { result |= UI_BoxFlag_AllowOverflowY; }
    if (flags.floating_x) { result |= UI_BoxFlag_FloatingX; }
    if (flags.floating_y) { result |= UI_BoxFlag_FloatingY; }
    if (flags.draw_border) { result |= UI_BoxFlag_DrawBorder; }
    if (flags.draw_hot_effects) { result |= UI_BoxFlag_DrawHotEffects; }
    if (flags.draw_active_effects) { result |= UI_BoxFlag_DrawActiveEffects; }
    return result;
}

UI_Widget* ui_widget_make(UI_WidgetFlags flags, String8 string)
{
    return ui_build_box_from_string(ui_box_flags_from_widget_flags(flags), string);
}

UI_Widget* ui_widget_make_format(UI_WidgetFlags flags, String8 format, ...)
{
    Arena* arena = ui_build_arena();
    va_list args;
    va_start(args, format);
    String8 string = string_format_va(arena, format, args);
    va_end(args);
    return ui_widget_make(flags, string);
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

BUSTER_GLOBAL_LOCAL void ui_prune_boxes(void)
{
    for (u64 slot_index = 0; slot_index < ui_state->box_table_size; slot_index += 1)
    {
        UI_BoxHashSlot* slot = &ui_state->box_table[slot_index];
        for (UI_Box* box = slot->first, *next = 0; box; box = next)
        {
            next = box->hash_next;
            if (box->last_touched_build_index + 1 < ui_state->build_index)
            {
                ui_box_hash_remove(box, slot_index);
                box->next = ui_state->first_free_box;
                ui_state->first_free_box = box;
            }
        }
    }
}

u8 ui_build_begin(WmHandle* windowing, WmWindowHandle* window, f64 frame_time, WmEventList event_queue)
{
    ui_state->build_index += 1;
    arena_reset_to_start(ui_build_arena());
    ui_stack_reset(ui_state);
    ui_state->root = 0;
    ui_state->windowing = windowing;
    ui_state->window = window;
    ui_state->events = (UI_EventList){0};
    ui_state->frame_time = frame_time;
    ui_state->is_animating = 0;

    u8 open = 1;
    for (WmEvent* event = event_queue.first; event; event = event->next)
    {
        if (event->window && event->window != window)
        {
            continue;
        }
        switch (event->kind)
        {
            case WM_EVENT_WINDOW_CLOSE:
            {
                open = 0;
            } break;
            case WM_EVENT_MOUSE_MOVE:
            case WM_EVENT_BUTTON_PRESS:
            case WM_EVENT_BUTTON_RELEASE:
            {
                ui_state->mouse = float2_make((f32)event->position.x, (f32)event->position.y);
            } break;
            default: break;
        }

        UI_Event ui_event = {0};
        ui_event.key = event->key;
        ui_event.modifiers = event->modifiers;
        ui_event.pos = float2_make((f32)event->position.x, (f32)event->position.y);

        switch (event->kind)
        {
            case WM_EVENT_KEY_PRESS:
            case WM_EVENT_BUTTON_PRESS:
            {
                ui_event.kind = UI_EventKind_Press;
            } break;
            case WM_EVENT_KEY_RELEASE:
            case WM_EVENT_BUTTON_RELEASE:
            {
                ui_event.kind = UI_EventKind_Release;
            } break;
            case WM_EVENT_TEXT_INPUT:
            {
                ui_event.kind = UI_EventKind_Text;
                ui_event.string = event->text;
            } break;
            case WM_EVENT_MOUSE_MOVE:
            {
                ui_event.kind = UI_EventKind_MouseMove;
            } break;
            default: break;
        }

        if (ui_event.kind != UI_EventKind_Null)
        {
            ui_event_list_push(ui_build_arena(), &ui_state->events, &ui_event);
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

    ui_prune_boxes();

    if (open)
    {
        RenderingWindowSize framebuffer_dimensions = rendering_window_get_size(ui_state->rendering_window);
        ui_set_next_pref_width(ui_pixels(framebuffer_dimensions.width, 1.0f));
        ui_set_next_pref_height(ui_pixels(framebuffer_dimensions.height, 1.0f));
        ui_set_next_child_layout_axis(AXIS2_Y);
        UI_Box* root = ui_build_box_from_stringf(0, S8("###window_root_{u64:x}"), (u64)window);
        root->fixed_size = float2_make((f32)framebuffer_dimensions.width, (f32)framebuffer_dimensions.height);
        root->rect = ui_rect_make(0.0f, 0.0f, (f32)framebuffer_dimensions.width, (f32)framebuffer_dimensions.height);
        root->flags |= UI_BoxFlag_FixedSize | UI_BoxFlag_AllowOverflow;

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

    return open;
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
            default: break;
            case UI_SizeKind_Pixels:
            {
                float2_element(b->fixed_size, axis) = floor_f32(b->pref_size[axis].value);
            } break;
            case UI_SizeKind_TextContent:
            {
                float2_element(b->fixed_size, axis) = floor_f32(ui_text_size_for_axis(b, axis));
            } break;
        }
    }
}

BUSTER_GLOBAL_LOCAL void ui_calc_sizes_upwards_dependent(UI_Box* box, Axis2 axis)
{
    for (UI_Box* b = box; b; b = ui_box_rec_df_pre(b, box).next)
    {
        if (b->pref_size[axis].kind == UI_SizeKind_ParentPct)
        {
            UI_Box* fixed_parent = 0;
            for (UI_Box* p = b->parent; p; p = p->parent)
            {
                if (float2_element(p->fixed_size, axis) > 0 || p->pref_size[axis].kind == UI_SizeKind_Pixels || p->pref_size[axis].kind == UI_SizeKind_TextContent || p->pref_size[axis].kind == UI_SizeKind_ParentPct)
                {
                    fixed_parent = p;
                    break;
                }
            }
            if (fixed_parent)
            {
                float2_element(b->fixed_size, axis) = floor_f32(float2_element(fixed_parent->fixed_size, axis) * b->pref_size[axis].value);
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
            if (b->pref_size[axis].kind == UI_SizeKind_ChildrenSum)
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
            float2_element(child->rect.p0, axis) = floor_f32(float2_element(box->rect.p0, axis) + child_pos - float2_element(box->view_off, axis));
            float2_element(child->rect.p1, axis) = floor_f32(float2_element(child->rect.p0, axis) + float2_element(child->fixed_size, axis));
            float2_element(child->position_delta, axis) = float2_element(child->rect.p0, axis) - old_p0;
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
}

void ui_build_end(void)
{
    if (ui_state->stacks.parent_length != 0)
    {
        BUSTER_UNUSED(ui_pop_parent());
    }

    if (ui_state->root)
    {
        for (Axis2 axis = 0; axis < AXIS2_COUNT; axis += 1)
        {
            ui_layout_root(ui_state->root, axis);
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
    if (key == WM_KEY_MOUSE_LEFT) { result = UI_MouseButtonKind_Left; }
    else if (key == WM_KEY_MOUSE_MIDDLE) { result = UI_MouseButtonKind_Middle; }
    else if (key == WM_KEY_MOUSE_RIGHT) { result = UI_MouseButtonKind_Right; }
    else { *is_mouse = false; }
    return result;
}

UI_Signal ui_signal_from_box(UI_Box* box)
{
    UI_Signal sig = { .box = box };
    if (!box || (box->flags & UI_BoxFlag_Disabled))
    {
        return sig;
    }

    F32Interval2 rect = box->rect;
    bool mouse_over = ui_rect_contains(rect, ui_state->mouse);
    if (mouse_over)
    {
        sig.f |= UI_SignalFlag_MouseOver;
        sig.mouse_over = 1;
    }

    if ((box->flags & UI_BoxFlag_MouseClickable) && mouse_over && (ui_key_match(ui_state->hot_box_key, ui_key_zero()) || ui_key_match(ui_state->hot_box_key, box->key)))
    {
        ui_state->hot_box_key = box->key;
        sig.f |= UI_SignalFlag_Hovering;
        sig.hovering = 1;
    }

    UI_EventIterator iterator = ui_event_iterator_initialize(ui_state);
    UI_Event* event;
    while ((event = ui_next_event(&iterator)))
    {
        bool is_mouse = false;
        UI_MouseButtonKind button = ui_mouse_button_kind_from_key(event->key, &is_mouse);
        bool event_in_bounds = ui_rect_contains(rect, event->pos);
        if ((box->flags & UI_BoxFlag_MouseClickable) && is_mouse && event->kind == UI_EventKind_Press && event_in_bounds)
        {
            ui_state->hot_box_key = box->key;
            ui_state->active_box_key[button] = box->key;
            if (button == UI_MouseButtonKind_Left)
            {
                sig.f |= UI_SignalFlag_LeftPressed;
                sig.pressed_left = 1;
            }
            ui_eat_event(event);
        }
        else if ((box->flags & UI_BoxFlag_MouseClickable) && is_mouse && event->kind == UI_EventKind_Release && ui_key_match(ui_state->active_box_key[button], box->key))
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
        else if ((box->flags & UI_BoxFlag_KeyboardClickable) && event->kind == UI_EventKind_Press && event->key == WM_KEY_RETURN && ui_key_match(ui_state->hot_box_key, box->key))
        {
            sig.f |= UI_SignalFlag_KeyboardPressed;
            sig.clicked_left = 1;
            ui_eat_event(event);
        }
    }

    return sig;
}

UI_Signal ui_signal_from_widget(UI_Widget* widget)
{
    return ui_signal_from_box(widget);
}

BUSTER_GLOBAL_LOCAL float2 ui_box_text_position(UI_Box* box)
{
    float2 rect_dim = ui_rect_dim(box->rect);
    float2 result = float2_make(box->rect.x0 + box->text_padding, box->rect.y0 + BUSTER_MAX(0.0f, (float2_element(rect_dim, AXIS2_Y) - box->font_size) * 0.5f));
    f32 estimated_width = (f32)box->string.length * box->font_size * 0.60f;
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
    rendering_window_render_rect(ui_state->rendering_window, (RectDraw){
        .vertex = rect,
        .colors = { color, color, color, color },
    });
}

BUSTER_GLOBAL_LOCAL void ui_draw_border(F32Interval2 rect, float4 color, f32 thickness)
{
    ui_draw_rect(ui_rect_make(rect.x0, rect.y0, rect.x1, rect.y0 + thickness), color);
    ui_draw_rect(ui_rect_make(rect.x0, rect.y1 - thickness, rect.x1, rect.y1), color);
    ui_draw_rect(ui_rect_make(rect.x0, rect.y0, rect.x0 + thickness, rect.y1), color);
    ui_draw_rect(ui_rect_make(rect.x1 - thickness, rect.y0, rect.x1, rect.y1), color);
}

void ui_draw(void)
{
    UI_Box* root = ui_state->root;
    if (!root)
    {
        return;
    }

    //- rjf: draw UI background & simple window border
    ui_draw_rect(root->rect, float4_make(0.08f, 0.08f, 0.095f, 1.0f));
    ui_draw_border(root->rect, float4_make(0.18f, 0.18f, 0.22f, 1.0f), 1.0f);

    //- rjf: recurse & draw boxes.  This mirrors the structure of raddebugger's
    // draw block: shadow/background/hot+active effects/text, then border/sides.
    for (UI_Box* box = root; box; box = ui_box_rec_df_pre(box, root).next)
    {
        if (box == root)
        {
            continue;
        }

        F32Interval2 rect = box->rect;
        if (rect.x1 <= rect.x0 || rect.y1 <= rect.y0)
        {
            continue;
        }

        if (box->flags & UI_BoxFlag_DrawDropShadow)
        {
            F32Interval2 shadow = ui_rect_make(rect.x0 + 4, rect.y0 + 4, rect.x1 + 4, rect.y1 + 4);
            ui_draw_rect(shadow, float4_make(0.0f, 0.0f, 0.0f, 0.22f));
        }

        if (box->flags & UI_BoxFlag_DrawBackground)
        {
            float4 color = box->background_color;
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
            float2 p = ui_box_text_position(box);
            rendering_window_render_text(ui_state->rendering, ui_state->rendering_window, box->string, box->text_color, RENDER_FONT_TYPE_MONOSPACE, float2_element(p, AXIS2_X), float2_element(p, AXIS2_Y));
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

        if (box->flags & UI_BoxFlag_Debug)
        {
            ui_draw_border(rect, float4_make(1.0f, 0.0f, 1.0f, 0.65f), 1.0f);
        }
    }
}
