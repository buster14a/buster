// Headless UI scalability regressions. test_ui_scale compiles the actual
// ui_core module and checks, with work counters instead of timers, that keyed
// box lookup, keyboard focus navigation and fuzzy-match highlight drawing stay
// linear in the number of boxes or in text length plus range count, while the
// observable behavior of the box table, navigation and highlight rectangles is
// unchanged. Only the unused native rendering boundary is supplied; no compiler
// or desktop window/backend dependency belongs to this component runner.
//
// Map: ui_scale_check and the key helpers, the keyed-box lookup scaling
// (ui_scale_lookup_scaling) and box-table behavior (ui_scale_table_behavior)
// cases, the focus-navigation oracle (ui_scale_focus_oracle), its tree builders
// and equivalence (ui_scale_focus_behavior) and deep-spine scaling
// (ui_scale_focus_scaling) cases, the fuzzy-highlight oracle (ui_scale_oracle_columns),
// equivalence (ui_scale_fuzzy_behavior) and scaling (ui_scale_fuzzy_scaling) cases,
// then main.

#include <buster/lib/system_headers.h>
#include <buster/lib/os.h>
#include <buster/lib/ui_core.h>
#include <buster/lib/string.h>
#include <stdio.h>

BUSTER_V_IMPL OsState os_state;
BUSTER_GLOBAL_LOCAL ProgramState ui_scale_program;
BUSTER_V_IMPL ProgramState* program_state = &ui_scale_program;
BUSTER_GLOBAL_LOCAL u32 ui_scale_renderer_calls;
BUSTER_GLOBAL_LOCAL u32 ui_scale_assertions;
BUSTER_GLOBAL_LOCAL u32 ui_scale_failures;

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
    ui_scale_renderer_calls += 1;
    return (RenderingWindowSize){0};
}

void rendering_window_render_rect(RenderingWindowHandle* window, RectDraw draw)
{
    BUSTER_UNUSED(window);
    BUSTER_UNUSED(draw);
    ui_scale_renderer_calls += 1;
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
    ui_scale_renderer_calls += 1;
}

bool rendering_window_render_background_blur_rounded(RenderingWindowHandle* window, F32Interval2 rect, u32 radius, float4 corner_radii)
{
    BUSTER_UNUSED(window);
    BUSTER_UNUSED(rect);
    BUSTER_UNUSED(radius);
    BUSTER_UNUSED(corner_radii);
    ui_scale_renderer_calls += 1;
    return false;
}

// The box table's fixed ordering-slot count that predates the lookup index. The
// dense active list keeps this slot/chain order, so it stays observable.
#define UI_SCALE_ORDER_SLOT_COUNT 4096u

BUSTER_GLOBAL_LOCAL bool ui_scale_check(bool condition, String8 operation, u64 value)
{
    ui_scale_assertions += 1;
    if (!condition)
    {
        ui_scale_failures += 1;
        fprintf(stderr, "ui_scale_component_tests: %.*s failed (%llu)\n", (int)operation.length, operation.pointer, (unsigned long long)value);
    }
    return condition;
}

// Deterministic, well-mixed nonzero key generator.
BUSTER_GLOBAL_LOCAL u64 ui_scale_mix(u64 value)
{
    u64 z = value + 0x9e3779b97f4a7c15ull;
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
    z = z ^ (z >> 31);
    return z ? z : 1;
}

typedef enum UI_ScaleKeyShape
{
    UI_ScaleKeyShape_Mixed,
    // Every key shares its low twelve bits: the worst case for a table that
    // masks the key with a fixed 4096-slot count.
    UI_ScaleKeyShape_SameLowBits,
    // Sequential small integers.
    UI_ScaleKeyShape_Sequential,
} UI_ScaleKeyShape;

BUSTER_GLOBAL_LOCAL UI_Key ui_scale_key(UI_ScaleKeyShape shape, u64 index)
{
    UI_Key result = ui_key_make(1);
    if (shape == UI_ScaleKeyShape_Mixed)
    {
        result = ui_key_make(ui_scale_mix(index + 0x1000000ull));
    }
    else if (shape == UI_ScaleKeyShape_SameLowBits)
    {
        result = ui_key_make((index + 1) * UI_SCALE_ORDER_SLOT_COUNT + 7);
    }
    else
    {
        result = ui_key_make(index + 1);
    }
    return result;
}

BUSTER_GLOBAL_LOCAL UI_Box* ui_scale_build_leaf(UI_Key key)
{
    ui_set_next_fixed_width(10.0f);
    ui_set_next_fixed_height(10.0f);
    return ui_build_box_from_key(0, key);
}

BUSTER_GLOBAL_LOCAL void ui_scale_frame_begin(UI_State* state)
{
    ui_state_select(state);
    ui_build_begin(0, 0, 16.0, (UI_EventList){0});
}

typedef struct UI_ScaleLookupWork UI_ScaleLookupWork;
struct UI_ScaleLookupWork
{
    u64 lookups;
    u64 probes;
};

// One frame that creates `count` keyed leaves, then one frame that finds every
// one of them again. Returns the lookup work of the creating and re-finding frames.
BUSTER_GLOBAL_LOCAL void ui_scale_lookup_case(UI_ScaleKeyShape shape, u64 count, UI_ScaleLookupWork* create_work, UI_ScaleLookupWork* find_work)
{
    UI_State* state = ui_state_allocate(0, 0);
    if (ui_scale_check(state != 0, S8("state allocation"), count))
    {
        u64 lookups_before = state->box_key_lookups;
        u64 probes_before = state->box_key_probes;
        ui_scale_frame_begin(state);
        for (u64 index = 0; index < count; index += 1)
        {
            UI_Box* box = ui_scale_build_leaf(ui_scale_key(shape, index));
            BUSTER_UNUSED(box);
        }
        ui_build_end();
        create_work->lookups = state->box_key_lookups - lookups_before;
        create_work->probes = state->box_key_probes - probes_before;
        ui_scale_check(state->box_count == count + 1 && state->active_box_count == count + 1, S8("created box population"), state->box_count);
        // Growth is geometric: the index covers the population, stays a power of
        // two, and rehashes each box O(1) times amortized.
        ui_scale_check(state->box_index_size >= state->box_count && (state->box_index_size & (state->box_index_size - 1)) == 0,
                       S8("lookup index covers the population"), state->box_index_size);
        ui_scale_check(state->box_index_moves <= 2 * state->box_count, S8("rehash work is amortized constant per box"), state->box_index_moves);

        lookups_before = state->box_key_lookups;
        probes_before = state->box_key_probes;
        ui_scale_frame_begin(state);
        bool stable = true;
        for (u64 index = 0; index < count; index += 1)
        {
            UI_Key key = ui_scale_key(shape, index);
            UI_Box* before = ui_box_from_key(key);
            UI_Box* box = ui_scale_build_leaf(key);
            stable = stable && before == box;
        }
        ui_build_end();
        find_work->lookups = state->box_key_lookups - lookups_before;
        find_work->probes = state->box_key_probes - probes_before;
        ui_scale_check(stable && state->box_count == count + 1, S8("keys resolve to the same boxes next frame"), count);
        ui_state_deinitialize(state);
    }
}

BUSTER_GLOBAL_LOCAL void ui_scale_lookup_scaling(void)
{
    static const u64 counts[] = {1024, 4096, 4097, 16384, 65536};
    static const struct
    {
        UI_ScaleKeyShape shape;
        String8 name;
    } shapes[] = {
        {UI_ScaleKeyShape_Mixed, S8("mixed")},
        {UI_ScaleKeyShape_SameLowBits, S8("same-low-bits")},
        {UI_ScaleKeyShape_Sequential, S8("sequential")},
    };
    for (u64 shape_index = 0; shape_index < BUSTER_ARRAY_LENGTH(shapes); shape_index += 1)
    {
        f64 first_probe_rate = 0.0;
        for (u64 count_index = 0; count_index < BUSTER_ARRAY_LENGTH(counts); count_index += 1)
        {
            UI_ScaleLookupWork create_work = {0};
            UI_ScaleLookupWork find_work = {0};
            u64 count = counts[count_index];
            ui_scale_lookup_case(shapes[shape_index].shape, count, &create_work, &find_work);
            f64 create_rate = (f64)create_work.probes / (f64)create_work.lookups;
            f64 find_rate = (f64)find_work.probes / (f64)find_work.lookups;
            printf("ui_scale: keys=%-5s n=%-6llu create lookups=%llu probes=%llu (%.2f/lookup) refind lookups=%llu probes=%llu (%.2f/lookup)\n",
                   (char*)shapes[shape_index].name.pointer, (unsigned long long)count, (unsigned long long)create_work.lookups,
                   (unsigned long long)create_work.probes, create_rate, (unsigned long long)find_work.lookups,
                   (unsigned long long)find_work.probes, find_rate);
            // A bounded load factor keeps the average chain short at every size, on
            // both sides of the old fixed 4096-slot capacity.
            ui_scale_check(create_work.lookups >= count && find_work.lookups >= count, S8("every keyed build looks its key up"), count);
            ui_scale_check(create_rate < 2.0, S8("creating lookups average under two probes"), (u64)(create_rate * 100.0));
            ui_scale_check(find_rate < 2.0, S8("refinding lookups average under two probes"), (u64)(find_rate * 100.0));
            if (count_index == 0)
            {
                first_probe_rate = find_rate;
            }
            else
            {
                ui_scale_check(find_rate < first_probe_rate * 2.0 + 1.0, S8("probe rate does not grow with population"), count);
            }
        }
    }
}

typedef struct UI_ScaleOrderEntry UI_ScaleOrderEntry;
struct UI_ScaleOrderEntry
{
    UI_Key key;
    UI_Box* box;
    bool present;
};

// Expected dense active-list order: the box table orders boxes by their key's
// low 12 bits, then by the order they entered the table. `entries` is already
// in table-entry order, so a stable counting sort by slot reproduces it.
BUSTER_GLOBAL_LOCAL bool ui_scale_active_order_matches(UI_State* state, UI_ScaleOrderEntry* entries, u64 entry_count, u64* sorted_storage)
{
    u64 slot_counts[UI_SCALE_ORDER_SLOT_COUNT + 1] = {0};
    u64 present_count = 0;
    for (u64 index = 0; index < entry_count; index += 1)
    {
        if (entries[index].present)
        {
            slot_counts[(entries[index].key.value & (UI_SCALE_ORDER_SLOT_COUNT - 1)) + 1] += 1;
            present_count += 1;
        }
    }
    for (u64 slot = 0; slot < UI_SCALE_ORDER_SLOT_COUNT; slot += 1)
    {
        slot_counts[slot + 1] += slot_counts[slot];
    }
    for (u64 index = 0; index < entry_count; index += 1)
    {
        if (entries[index].present)
        {
            u64 slot = entries[index].key.value & (UI_SCALE_ORDER_SLOT_COUNT - 1);
            sorted_storage[slot_counts[slot]] = index;
            slot_counts[slot] += 1;
        }
    }
    bool matches = state->active_box_count == present_count && state->box_count == present_count;
    for (u64 position = 0; matches && position < present_count; position += 1)
    {
        matches = state->active_boxes[position] == entries[sorted_storage[position]].box;
    }
    return matches;
}

// Box-table behavior across growth, duplicate and zero keys, pruning and
// free-list reuse, with the key population crossing the old 4096 capacity.
BUSTER_GLOBAL_LOCAL void ui_scale_table_behavior(Arena* arena)
{
    enum
    {
        initial_count = 9000,
        added_count = 3000,
        recycled_count = 2000,
    };
    UI_State* state = ui_state_allocate(0, 0);
    if (ui_scale_check(state != 0, S8("behavior state allocation"), 0))
    {
        u64 entry_capacity = 1 + initial_count + added_count + recycled_count;
        UI_ScaleOrderEntry* entries = arena_allocate(arena, UI_ScaleOrderEntry, entry_capacity);
        u64* sorted = arena_allocate(arena, u64, entry_capacity);
        u64 entry_count = 0;

        // Frame 1: the root plus `initial_count` keyed boxes, growing the table
        // from empty past its initial capacity.
        ui_scale_frame_begin(state);
        entries[entry_count] = (UI_ScaleOrderEntry){.key = state->root->key, .box = state->root, .present = true};
        entry_count += 1;
        for (u64 index = 0; index < initial_count; index += 1)
        {
            UI_Key key = ui_scale_key(UI_ScaleKeyShape_Mixed, index);
            UI_Box* box = ui_scale_build_leaf(key);
            entries[entry_count] = (UI_ScaleOrderEntry){.key = key, .box = box, .present = true};
            entry_count += 1;
        }
        ui_build_end();
        ui_scale_check(ui_scale_active_order_matches(state, entries, entry_count, sorted), S8("active order after growth"), state->box_count);

        // Frame 2: reverse build order, a duplicate key and zero keys. Boxes keep
        // their addresses; the duplicate and the zero keys are unkeyed boxes
        // that never enter the table.
        u64 box_count_before = state->box_count;
        ui_scale_frame_begin(state);
        bool same_addresses = true;
        for (u64 index = initial_count; index > 0; index -= 1)
        {
            UI_ScaleOrderEntry* entry = &entries[index];
            UI_Box* box = ui_scale_build_leaf(entry->key);
            same_addresses = same_addresses && box == entry->box && ui_box_from_key(entry->key) == entry->box;
        }
        ui_scale_check(same_addresses, S8("box addresses stable across frames and growth"), 0);
        UI_Box* duplicate = ui_scale_build_leaf(entries[5].key);
        UI_Box* zero_a = ui_scale_build_leaf(ui_key_zero());
        UI_Box* zero_b = ui_scale_build_leaf(ui_key_zero());
        ui_scale_check(duplicate != entries[5].box && ui_key_match(duplicate->key, ui_key_zero()), S8("duplicate key becomes an unkeyed box"), 0);
        ui_scale_check(ui_box_from_key(entries[5].key) == entries[5].box, S8("duplicate does not displace the keyed box"), 0);
        ui_scale_check(zero_a != zero_b && ui_key_match(zero_a->key, ui_key_zero()) && ui_key_match(zero_b->key, ui_key_zero()), S8("zero keys are distinct unkeyed boxes"), 0);
        ui_scale_check(ui_box_from_key(ui_key_zero()) == 0, S8("zero key never resolves"), 0);
        ui_scale_check(ui_box_from_key(ui_scale_key(UI_ScaleKeyShape_Mixed, initial_count + added_count + 99)) == 0, S8("missing key does not resolve"), 0);
        ui_build_end();
        ui_scale_check(state->box_count == box_count_before && ui_scale_active_order_matches(state, entries, entry_count, sorted), S8("order and population unchanged by unkeyed boxes"), state->box_count);

        // Frame 3: keep the root and even keys, add new keys. Odd keys are pruned
        // at the end of the frame and their boxes go to the free list.
        ui_scale_frame_begin(state);
        for (u64 index = 1; index <= initial_count; index += 1)
        {
            if ((index & 1) == 0)
            {
                UI_Box* box = ui_scale_build_leaf(entries[index].key);
                BUSTER_UNUSED(box);
            }
            else
            {
                entries[index].present = false;
            }
        }
        u64 first_added = entry_count;
        for (u64 index = 0; index < added_count; index += 1)
        {
            UI_Key key = ui_scale_key(UI_ScaleKeyShape_Mixed, initial_count + index);
            UI_Box* box = ui_scale_build_leaf(key);
            entries[entry_count] = (UI_ScaleOrderEntry){.key = key, .box = box, .present = true};
            entry_count += 1;
        }
        ui_build_end();
        u64 removed = initial_count / 2;
        ui_scale_check(state->box_count == 1 + initial_count - removed + added_count, S8("population after pruning"), state->box_count);
        bool removed_gone = true;
        bool kept_found = true;
        for (u64 index = 1; index <= initial_count; index += 1)
        {
            UI_Box* found = ui_box_from_key(entries[index].key);
            removed_gone = removed_gone && ((index & 1) == 0 || found == 0);
            kept_found = kept_found && ((index & 1) == 1 || found == entries[index].box);
        }
        ui_scale_check(removed_gone, S8("pruned keys no longer resolve"), 0);
        ui_scale_check(kept_found, S8("kept keys keep their boxes"), 0);
        ui_scale_check(ui_scale_active_order_matches(state, entries, entry_count, sorted), S8("active order after pruning"), state->box_count);
        bool added_found = true;
        for (u64 added = first_added; added < entry_count; added += 1)
        {
            added_found = added_found && ui_box_from_key(entries[added].key) == entries[added].box;
        }
        ui_scale_check(added_found, S8("added keys resolve"), 0);

        // Frame 4: the survivors again plus fresh keys. Every fresh key must take
        // a freed box, and the table order must still match the entry order.
        ui_scale_frame_begin(state);
        for (u64 index = 0; index < entry_count; index += 1)
        {
            if (entries[index].present && index != 0)
            {
                UI_Box* box = ui_scale_build_leaf(entries[index].key);
                BUSTER_UNUSED(box);
            }
        }
        u64 first_recycled = entry_count;
        for (u64 index = 0; index < recycled_count; index += 1)
        {
            UI_Key key = ui_scale_key(UI_ScaleKeyShape_Mixed, initial_count + added_count + index);
            UI_Box* box = ui_scale_build_leaf(key);
            entries[entry_count] = (UI_ScaleOrderEntry){.key = key, .box = box, .present = true};
            entry_count += 1;
        }
        ui_build_end();
        bool recycled = true;
        for (u64 added = first_recycled; added < entry_count; added += 1)
        {
            bool found = false;
            for (u64 index = 1; index <= initial_count && !found; index += 2)
            {
                found = entries[added].box == entries[index].box;
            }
            recycled = recycled && found && ui_box_from_key(entries[added].key) == entries[added].box;
        }
        ui_scale_check(recycled, S8("new keys reuse freed boxes"), 0);
        ui_scale_check(ui_scale_active_order_matches(state, entries, entry_count, sorted), S8("active order after free-list reuse"), state->box_count);
        ui_state_deinitialize(state);
    }
}

// ---------------------------------------------------------------------------
// Focus navigation.
//
// The oracle below is the pre-fix algorithm: it decides scope membership by
// walking every candidate's parent chain, and scans the dense active list with
// the same tie-breaking. It reads only public UI_State/UI_Box fields, so the
// production implementation can change its data structures freely while its
// selected keys must stay identical.

typedef enum UI_ScaleDirection
{
    UI_ScaleDirection_Forward,
    UI_ScaleDirection_Backward,
    UI_ScaleDirection_Left,
    UI_ScaleDirection_Right,
    UI_ScaleDirection_Up,
    UI_ScaleDirection_Down,
    UI_ScaleDirection_Count,
} UI_ScaleDirection;

BUSTER_GLOBAL_LOCAL bool ui_scale_oracle_focusable(UI_Box* box)
{
    bool result = false;
    if (box && !ui_key_match(box->key, ui_key_zero()) && !(box->flags & (UI_BoxFlag_FocusNavSkip | UI_BoxFlag_Disabled | UI_BoxFlag_FocusActiveDisabled)))
    {
        result = !!(box->flags & (UI_BoxFlag_FocusActive | UI_BoxFlag_KeyboardClickable | UI_BoxFlag_ClickToFocus | UI_BoxFlag_DefaultFocusNavX |
                                  UI_BoxFlag_DefaultFocusNavY | UI_BoxFlag_DefaultFocusEdit));
    }
    return result;
}

BUSTER_GLOBAL_LOCAL UI_Box* ui_scale_oracle_scope(UI_Box* box, UI_Box* root)
{
    UI_Box* result = box ? box->parent : root;
    bool found = false;
    for (UI_Box* parent = box ? box->parent : root; parent && !found; parent = parent->parent)
    {
        if (parent != root && (parent->flags & (UI_BoxFlag_DefaultFocusNavX | UI_BoxFlag_DefaultFocusNavY)))
        {
            result = parent;
            found = true;
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool ui_scale_oracle_in_scope(UI_Box* box, UI_Box* scope)
{
    bool result = false;
    if (box && scope && box != scope)
    {
        for (UI_Box* parent = box->parent; parent && !result; parent = parent->parent)
        {
            result = parent == scope;
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL f32 ui_scale_center(F32Interval2 rect, bool x_axis)
{
    return x_axis ? (rect.x0 + rect.x1) * 0.5f : (rect.y0 + rect.y1) * 0.5f;
}

BUSTER_GLOBAL_LOCAL f32 ui_scale_abs(f32 value)
{
    return value < 0.0f ? -value : value;
}

BUSTER_GLOBAL_LOCAL UI_Box* ui_scale_focus_oracle(UI_State* state, UI_Key current_key, UI_ScaleDirection direction)
{
    UI_BoxFlags axis_flag = UI_BoxFlag_DefaultFocusNavX | UI_BoxFlag_DefaultFocusNavY;
    if (direction == UI_ScaleDirection_Left || direction == UI_ScaleDirection_Right)
    {
        axis_flag = UI_BoxFlag_DefaultFocusNavX;
    }
    else if (direction == UI_ScaleDirection_Up || direction == UI_ScaleDirection_Down)
    {
        axis_flag = UI_BoxFlag_DefaultFocusNavY;
    }
    bool directional_request = direction >= UI_ScaleDirection_Left;
    bool horizontal = direction == UI_ScaleDirection_Left || direction == UI_ScaleDirection_Right;
    bool negative = direction == UI_ScaleDirection_Left || direction == UI_ScaleDirection_Up;
    u64 build_index = state->build_index;
    UI_Box* root = state->root;
    UI_Box* current = ui_box_from_key(current_key);
    bool current_found = current && current->last_touched_build_index == build_index;
    if (!current_found)
    {
        current = 0;
    }
    UI_Box* scope = ui_scale_oracle_scope(current, root);
    UI_Box* first = 0;
    UI_Box* last = 0;
    UI_Box* directional = 0;
    f32 directional_score = 0.0f;
    for (u64 index = 0; index < state->active_box_count; index += 1)
    {
        UI_Box* box = state->active_boxes[index];
        if (box->last_touched_build_index != build_index || !ui_scale_oracle_in_scope(box, scope) || !(box->flags & axis_flag) || !ui_scale_oracle_focusable(box))
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
        if (current_found && directional_request && !ui_key_match(box->key, current_key))
        {
            f32 primary_delta = ui_scale_center(box->rect, horizontal) - ui_scale_center(current->rect, horizontal);
            f32 cross_delta = ui_scale_abs(ui_scale_center(box->rect, !horizontal) - ui_scale_center(current->rect, !horizontal));
            bool in_direction = negative ? primary_delta < 0.0f : primary_delta > 0.0f;
            if (in_direction)
            {
                f32 score = ui_scale_abs(primary_delta) * 1024.0f + cross_delta;
                if (!directional || score < directional_score || (score == directional_score && box->build_order < directional->build_order))
                {
                    directional = box;
                    directional_score = score;
                }
            }
        }
    }
    UI_Box* result = 0;
    if (directional_request)
    {
        result = directional;
        if (!result)
        {
            // Directional navigation wraps to the far side of the scope; ties keep
            // the first box in active-list order.
            for (u64 index = 0; index < state->active_box_count; index += 1)
            {
                UI_Box* box = state->active_boxes[index];
                if (box->last_touched_build_index != build_index || !ui_scale_oracle_in_scope(box, scope) || !(box->flags & axis_flag) ||
                    !ui_scale_oracle_focusable(box) || ui_key_match(box->key, current_key))
                {
                    continue;
                }
                bool better = !result;
                if (result)
                {
                    f32 value = ui_scale_center(box->rect, horizontal);
                    f32 old_value = ui_scale_center(result->rect, horizontal);
                    better = negative ? value > old_value : value < old_value;
                }
                if (better)
                {
                    result = box;
                }
            }
        }
    }
    else if (!current_found)
    {
        result = direction == UI_ScaleDirection_Backward ? last : first;
    }
    else
    {
        UI_Box* best = 0;
        for (u64 index = 0; index < state->active_box_count; index += 1)
        {
            UI_Box* box = state->active_boxes[index];
            if (box->last_touched_build_index == build_index && ui_scale_oracle_in_scope(box, scope) && (box->flags & axis_flag) && ui_scale_oracle_focusable(box))
            {
                if (direction == UI_ScaleDirection_Backward)
                {
                    if (box->build_order < current->build_order && (!best || box->build_order > best->build_order))
                    {
                        best = box;
                    }
                }
                else if (box->build_order > current->build_order && (!best || box->build_order < best->build_order))
                {
                    best = box;
                }
            }
        }
        result = best ? best : (direction == UI_ScaleDirection_Backward ? last : first);
    }
    return result;
}

typedef struct UI_ScaleNode UI_ScaleNode;
struct UI_ScaleNode
{
    // Index of the parent node, or -1 for a child of the window root. Parents
    // precede their children.
    s32 parent;
    UI_BoxFlags flags;
    UI_Key key;
    f32 x;
    f32 y;
};

#define UI_SCALE_FOCUSABLE (UI_BoxFlag_KeyboardClickable | UI_BoxFlag_FocusActive | UI_BoxFlag_DefaultFocusNavX | UI_BoxFlag_DefaultFocusNavY)
#define UI_SCALE_SCOPE (UI_BoxFlag_DefaultFocusNavX | UI_BoxFlag_DefaultFocusNavY | UI_BoxFlag_FocusNavSkip)

typedef struct UI_ScaleTree UI_ScaleTree;
struct UI_ScaleTree
{
    UI_ScaleNode* nodes;
    UI_Box** boxes;
    u64 count;
    u64 capacity;
};

BUSTER_GLOBAL_LOCAL UI_ScaleTree ui_scale_tree_allocate(Arena* arena, u64 capacity)
{
    UI_ScaleTree result = {
        .nodes = arena_allocate(arena, UI_ScaleNode, capacity), .boxes = arena_allocate(arena, UI_Box*, capacity), .count = 0, .capacity = capacity};
    return result;
}

// Returns the new node's index.
BUSTER_GLOBAL_LOCAL s32 ui_scale_tree_add(UI_ScaleTree* tree, s32 parent, UI_BoxFlags flags, f32 x, f32 y)
{
    BUSTER_CHECK(tree->count < tree->capacity);
    s32 result = (s32)tree->count;
    UI_ScaleNode* node = &tree->nodes[tree->count];
    node->parent = parent;
    node->flags = flags;
    node->key = ui_scale_key(UI_ScaleKeyShape_Mixed, 0x5000000ull + tree->count);
    node->x = x;
    node->y = y;
    tree->count += 1;
    return result;
}

// Eligibility mix for leaf `index`: disabled, skipped, X-only and
// active-disabled leaves interleave with ordinary focusable ones.
BUSTER_GLOBAL_LOCAL UI_BoxFlags ui_scale_leaf_flags(u64 index)
{
    UI_BoxFlags flags = UI_SCALE_FOCUSABLE;
    if (index % 4 == 3)
    {
        flags |= UI_BoxFlag_Disabled;
    }
    if (index % 5 == 4)
    {
        flags |= UI_BoxFlag_FocusNavSkip;
    }
    if (index % 7 == 6)
    {
        flags = UI_BoxFlag_KeyboardClickable | UI_BoxFlag_FocusActive | UI_BoxFlag_DefaultFocusNavX;
    }
    if (index % 11 == 10)
    {
        flags |= UI_BoxFlag_FocusActiveDisabled;
    }
    return flags;
}

// A scope whose spine is `levels` unflagged containers deep, each carrying one
// leaf. `scope_period` > 0 turns every that-many-th spine container into a
// nested scope. With `plain` set every leaf is an ordinary focusable box,
// which is what the scaling case needs; otherwise eligibility varies.
BUSTER_GLOBAL_LOCAL UI_ScaleTree ui_scale_tree_chain(Arena* arena, u64 levels, u64 scope_period, bool plain)
{
    UI_ScaleTree tree = ui_scale_tree_allocate(arena, 2 * levels + 8);
    s32 spine = ui_scale_tree_add(&tree, -1, UI_SCALE_SCOPE, 0.0f, 0.0f);
    for (u64 level = 0; level < levels; level += 1)
    {
        bool nested_scope = scope_period != 0 && level % scope_period == scope_period - 1;
        spine = ui_scale_tree_add(&tree, spine, nested_scope ? UI_SCALE_SCOPE : 0, 0.0f, 0.0f);
        BUSTER_UNUSED(ui_scale_tree_add(&tree, spine, plain ? UI_SCALE_FOCUSABLE : ui_scale_leaf_flags(level), (f32)((level * 37) % 90), (f32)((level * 11) % 60)));
    }
    return tree;
}

// A root-level scope holding `branches` unflagged branches of `leaves` leaves.
BUSTER_GLOBAL_LOCAL UI_ScaleTree ui_scale_tree_comb(Arena* arena, u64 branches, u64 leaves)
{
    UI_ScaleTree tree = ui_scale_tree_allocate(arena, 2 + branches * (leaves + 1));
    s32 scope = ui_scale_tree_add(&tree, -1, UI_SCALE_SCOPE, 0.0f, 0.0f);
    u64 leaf_index = 0;
    for (u64 branch = 0; branch < branches; branch += 1)
    {
        s32 branch_node = ui_scale_tree_add(&tree, scope, 0, 0.0f, 0.0f);
        for (u64 leaf = 0; leaf < leaves; leaf += 1)
        {
            BUSTER_UNUSED(ui_scale_tree_add(&tree, branch_node, ui_scale_leaf_flags(leaf_index), (f32)(branch * 12), (f32)(leaf * 12)));
            leaf_index += 1;
        }
    }
    return tree;
}

// Two sibling scopes of grid leaves with deliberately repeated centers, so
// directional ties and wraps depend on active-list order, plus unscoped leaves
// directly under the root.
BUSTER_GLOBAL_LOCAL UI_ScaleTree ui_scale_tree_broad(Arena* arena, u64 leaves)
{
    UI_ScaleTree tree = ui_scale_tree_allocate(arena, 3 * leaves + 8);
    s32 first_scope = ui_scale_tree_add(&tree, -1, UI_SCALE_SCOPE, 0.0f, 0.0f);
    for (u64 index = 0; index < leaves; index += 1)
    {
        BUSTER_UNUSED(ui_scale_tree_add(&tree, first_scope, ui_scale_leaf_flags(index), (f32)((index % 9) * 30), (f32)((index / 9) * 30)));
    }
    s32 second_scope = ui_scale_tree_add(&tree, -1, UI_SCALE_SCOPE, 0.0f, 0.0f);
    for (u64 index = 0; index < leaves / 2; index += 1)
    {
        BUSTER_UNUSED(ui_scale_tree_add(&tree, second_scope, ui_scale_leaf_flags(index + 3), (f32)((index % 5) * 30), (f32)((index / 5) * 30)));
    }
    for (u64 index = 0; index < leaves / 4; index += 1)
    {
        BUSTER_UNUSED(ui_scale_tree_add(&tree, -1, ui_scale_leaf_flags(index + 1), (f32)((index % 4) * 30), (f32)((index / 4) * 30)));
    }
    return tree;
}

// Builds the tree under the window root. The 64-entry parent stack is kept at
// one entry (the current parent is swapped in per node), so tree depth is not
// limited by the stack.
BUSTER_GLOBAL_LOCAL void ui_scale_tree_build(UI_State* state, UI_ScaleTree* tree)
{
    UI_Box* root = state->root;
    for (u64 index = 0; index < tree->count; index += 1)
    {
        UI_ScaleNode* node = &tree->nodes[index];
        BUSTER_UNUSED(ui_pop_parent());
        ui_push_parent(node->parent >= 0 ? tree->boxes[node->parent] : root);
        ui_set_next_fixed_x(node->x);
        ui_set_next_fixed_y(node->y);
        ui_set_next_fixed_width(20.0f);
        ui_set_next_fixed_height(20.0f);
        tree->boxes[index] = ui_build_box_from_key(node->flags | UI_BoxFlag_FloatingX | UI_BoxFlag_FloatingY, node->key);
    }
    BUSTER_UNUSED(ui_pop_parent());
    ui_push_parent(root);
}

BUSTER_GLOBAL_LOCAL WmKey ui_scale_direction_key(UI_ScaleDirection direction)
{
    WmKey result = WM_KEY_TAB;
    if (direction == UI_ScaleDirection_Left)
    {
        result = WM_KEY_LEFT;
    }
    else if (direction == UI_ScaleDirection_Right)
    {
        result = WM_KEY_RIGHT;
    }
    else if (direction == UI_ScaleDirection_Up)
    {
        result = WM_KEY_UP;
    }
    else if (direction == UI_ScaleDirection_Down)
    {
        result = WM_KEY_DOWN;
    }
    return result;
}

typedef struct UI_ScaleNavigation UI_ScaleNavigation;
struct UI_ScaleNavigation
{
    UI_Key selected;
    u64 calls;
    u64 steps;
};

// Presses the key for `direction` with focus on `current_key` in a frame that
// rebuilds `tree`. Returns the focus key the pre-build router selected and the
// scope work that routing performed.
BUSTER_GLOBAL_LOCAL UI_ScaleNavigation ui_scale_navigate(UI_State* state, Arena* arena, UI_ScaleTree* tree, UI_Key current_key, UI_ScaleDirection direction)
{
    UI_ScaleNavigation result = {0};
    UI_Event event = {
        .kind = UI_EventKind_Press,
        .key = ui_scale_direction_key(direction),
        .modifiers = direction == UI_ScaleDirection_Backward ? (u8)(1u << WM_MODIFIER_SHIFT) : (u8)0,
    };
    UI_EventList events = {0};
    ui_event_list_push(arena, &events, &event);
    state->focus_active_key = current_key;
    u64 calls_before = state->focus_navigation_calls;
    u64 steps_before = state->focus_scope_steps;
    ui_state_select(state);
    ui_build_begin(0, 0, 16.0, events);
    result.selected = state->focus_active_key;
    result.calls = state->focus_navigation_calls - calls_before;
    result.steps = state->focus_scope_steps - steps_before;
    ui_scale_tree_build(state, tree);
    ui_build_end();
    return result;
}

BUSTER_GLOBAL_LOCAL void ui_scale_focus_equivalence(Arena* arena, String8 name, UI_ScaleTree* tree)
{
    UI_State* state = ui_state_allocate(0, 0);
    if (ui_scale_check(state != 0, S8("focus state allocation"), 0))
    {
        ui_scale_frame_begin(state);
        ui_scale_tree_build(state, tree);
        ui_build_end();
        u64 compared = 0;
        u64 moved = 0;
        u64 mismatches = 0;
        // Every node as the current key (including unkeyed-eligible and ineligible
        // ones), plus an unknown key and the empty key, in all six directions.
        for (u64 index = 0; index < tree->count + 2; index += 1)
        {
            UI_Key current = ui_key_zero();
            if (index < tree->count)
            {
                current = tree->nodes[index].key;
            }
            else if (index == tree->count)
            {
                current = ui_scale_key(UI_ScaleKeyShape_Mixed, 77);
            }
            for (u64 direction = 0; direction < UI_ScaleDirection_Count; direction += 1)
            {
                UI_Box* expected_box = ui_scale_focus_oracle(state, current, (UI_ScaleDirection)direction);
                UI_Key expected = expected_box ? expected_box->key : current;
                UI_ScaleNavigation navigation = ui_scale_navigate(state, arena, tree, current, (UI_ScaleDirection)direction);
                compared += 1;
                moved += !ui_key_match(expected, current);
                mismatches += !ui_key_match(navigation.selected, expected);
            }
        }
        printf("ui_scale: focus equivalence %-10s nodes=%llu comparisons=%llu moved=%llu mismatches=%llu\n", (char*)name.pointer,
               (unsigned long long)tree->count, (unsigned long long)compared, (unsigned long long)moved, (unsigned long long)mismatches);
        ui_scale_check(mismatches == 0, name, mismatches);
        // Guard against a vacuous comparison: most requests must select something new.
        ui_scale_check(moved * 3 > compared, S8("equivalence cases exercise real navigation"), moved);
        ui_state_deinitialize(state);
    }
}

// Hand-computed controls, independent of the oracle: an outer scope holding a
// button and an inner scope with two buttons (and a disabled one that is never
// selected). Navigation inside the inner scope wraps within it, while the outer
// button reaches the whole outer scope in build order.
BUSTER_GLOBAL_LOCAL void ui_scale_focus_hand_controls(Arena* arena)
{
    UI_ScaleTree tree = ui_scale_tree_allocate(arena, 8);
    s32 outer = ui_scale_tree_add(&tree, -1, UI_SCALE_SCOPE, 0.0f, 0.0f);
    s32 outer_button = ui_scale_tree_add(&tree, outer, UI_SCALE_FOCUSABLE, 0.0f, 0.0f);
    s32 inner = ui_scale_tree_add(&tree, outer, UI_SCALE_SCOPE, 0.0f, 30.0f);
    s32 inner_first = ui_scale_tree_add(&tree, inner, UI_SCALE_FOCUSABLE, 0.0f, 30.0f);
    s32 inner_second = ui_scale_tree_add(&tree, inner, UI_SCALE_FOCUSABLE, 0.0f, 60.0f);
    BUSTER_UNUSED(ui_scale_tree_add(&tree, inner, UI_SCALE_FOCUSABLE | UI_BoxFlag_Disabled, 0.0f, 90.0f));
    UI_State* state = ui_state_allocate(0, 0);
    if (ui_scale_check(state != 0, S8("hand control state allocation"), 0))
    {
        ui_scale_frame_begin(state);
        ui_scale_tree_build(state, &tree);
        ui_build_end();
        UI_Key outer_key = tree.nodes[outer_button].key;
        UI_Key first_key = tree.nodes[inner_first].key;
        UI_Key second_key = tree.nodes[inner_second].key;
        UI_Key none = ui_key_zero();
        ui_scale_check(ui_key_match(ui_scale_navigate(state, arena, &tree, first_key, UI_ScaleDirection_Forward).selected, second_key),
                       S8("tab inside the inner scope"), 0);
        ui_scale_check(ui_key_match(ui_scale_navigate(state, arena, &tree, second_key, UI_ScaleDirection_Forward).selected, first_key),
                       S8("tab wraps inside the inner scope"), 0);
        ui_scale_check(ui_key_match(ui_scale_navigate(state, arena, &tree, first_key, UI_ScaleDirection_Backward).selected, second_key),
                       S8("shift-tab wraps inside the inner scope"), 0);
        ui_scale_check(ui_key_match(ui_scale_navigate(state, arena, &tree, first_key, UI_ScaleDirection_Down).selected, second_key),
                       S8("down inside the inner scope"), 0);
        ui_scale_check(ui_key_match(ui_scale_navigate(state, arena, &tree, second_key, UI_ScaleDirection_Down).selected, first_key),
                       S8("down wraps inside the inner scope"), 0);
        ui_scale_check(ui_key_match(ui_scale_navigate(state, arena, &tree, outer_key, UI_ScaleDirection_Forward).selected, first_key),
                       S8("outer button tabs into build order"), 0);
        ui_scale_check(ui_key_match(ui_scale_navigate(state, arena, &tree, outer_key, UI_ScaleDirection_Backward).selected, second_key),
                       S8("outer button shift-tabs to the last eligible box"), 0);
        ui_scale_check(ui_key_match(ui_scale_navigate(state, arena, &tree, none, UI_ScaleDirection_Forward).selected, outer_key),
                       S8("no focus tabs to the first eligible box"), 0);
        ui_scale_check(ui_key_match(ui_scale_navigate(state, arena, &tree, none, UI_ScaleDirection_Backward).selected, second_key),
                       S8("no focus shift-tabs to the last eligible box"), 0);
        ui_state_deinitialize(state);
    }
}

BUSTER_GLOBAL_LOCAL void ui_scale_focus_behavior(Arena* arena)
{
    ui_scale_focus_hand_controls(arena);
    UI_ScaleTree chain = ui_scale_tree_chain(arena, 48, 0, false);
    ui_scale_focus_equivalence(arena, S8("chain"), &chain);
    UI_ScaleTree nested = ui_scale_tree_chain(arena, 60, 7, false);
    ui_scale_focus_equivalence(arena, S8("nested"), &nested);
    UI_ScaleTree comb = ui_scale_tree_comb(arena, 14, 6);
    ui_scale_focus_equivalence(arena, S8("comb"), &comb);
    UI_ScaleTree broad = ui_scale_tree_broad(arena, 80);
    ui_scale_focus_equivalence(arena, S8("broad"), &broad);
}

// Navigation over a deep spine: the old per-candidate ancestor walk made one
// request cost about one step per ancestor of every box, quadratic in depth.
BUSTER_GLOBAL_LOCAL void ui_scale_focus_scaling(Arena* arena)
{
    static const u64 depths[] = {500, 1000, 2000, 4000};
    f64 first_rate = 0.0;
    for (u64 depth_index = 0; depth_index < BUSTER_ARRAY_LENGTH(depths); depth_index += 1)
    {
        u64 levels = depths[depth_index];
        UI_ScaleTree tree = ui_scale_tree_chain(arena, levels, 0, true);
        UI_State* state = ui_state_allocate(0, 0);
        if (ui_scale_check(state != 0, S8("scaling state allocation"), levels))
        {
            ui_scale_frame_begin(state);
            ui_scale_tree_build(state, &tree);
            ui_build_end();
            // The leaf in the middle of the spine, so both before/after searches have work.
            UI_Key current = tree.nodes[1 + 2 * (levels / 2) + 1].key;
            u64 worst_steps = 0;
            bool selected_match = true;
            for (u64 direction = 0; direction < UI_ScaleDirection_Count; direction += 1)
            {
                UI_Box* expected_box = ui_scale_focus_oracle(state, current, (UI_ScaleDirection)direction);
                UI_Key expected = expected_box ? expected_box->key : current;
                UI_ScaleNavigation navigation = ui_scale_navigate(state, arena, &tree, current, (UI_ScaleDirection)direction);
                selected_match = selected_match && ui_key_match(navigation.selected, expected) && navigation.calls == 1;
                worst_steps = BUSTER_MAX(worst_steps, navigation.steps);
            }
            f64 rate = (f64)worst_steps / (f64)state->box_count;
            printf("ui_scale: focus depth=%-5llu boxes=%-6llu worst-request steps=%llu (%.2f/box)\n", (unsigned long long)levels,
                   (unsigned long long)state->box_count, (unsigned long long)worst_steps, rate);
            ui_scale_check(selected_match, S8("deep spine navigation matches the oracle"), levels);
            // Linear work: a bounded number of steps per box, independent of depth.
            ui_scale_check(rate < 8.0, S8("navigation steps per box stay bounded"), (u64)(rate * 100.0));
            if (depth_index == 0)
            {
                first_rate = rate;
            }
            else
            {
                ui_scale_check(rate < first_rate * 1.5 + 1.0, S8("steps per box do not grow with depth"), levels);
            }
            ui_state_deinitialize(state);
        }
    }
}

// ---------------------------------------------------------------------------
// Fuzzy-match highlight drawing (#2447). The drawing path used to convert both
// byte endpoints of every range to columns by decoding from byte zero, so R
// ranges over L bytes cost O(R x L). The oracle below is that algorithm,
// written independently of ui_core, so the emitted rectangles and the old
// decode work can both be compared.

typedef struct UI_ScaleFuzzyCase UI_ScaleFuzzyCase;
struct UI_ScaleFuzzyCase
{
    String8 text;
    UI_BoxFlags flags;
    UI_TextAlign align;
    f32 width;
    // Nonzero wraps the text box in a narrower clipping parent of this width.
    f32 clip_width;
};

typedef struct UI_ScaleFuzzyFrame UI_ScaleFuzzyFrame;
struct UI_ScaleFuzzyFrame
{
    UI_State* state;
    UI_Box* box;
    F32Interval2* rects;
    u64 rect_count;
    u64 decodes;
};

BUSTER_GLOBAL_LOCAL u64 ui_scale_oracle_decodes;

// Independent strict UTF-8 sequence length; 0 for an invalid or truncated sequence.
BUSTER_GLOBAL_LOCAL u64 ui_scale_oracle_sequence_length(String8 string, u64 position)
{
    u8 first = (u8)string.pointer[position];
    u64 length = first < 0x80u ? 1 : (first >= 0xc2u && first <= 0xdfu) ? 2 : (first >= 0xe0u && first <= 0xefu) ? 3 : (first >= 0xf0u && first <= 0xf4u) ? 4 : 0;
    bool valid = length != 0 && length <= string.length - position;
    for (u64 index = 1; valid && index < length; index += 1)
    {
        u8 continuation = (u8)string.pointer[position + index];
        valid = continuation >= 0x80u && continuation <= 0xbfu;
    }
    if (valid && length > 1)
    {
        u8 second = (u8)string.pointer[position + 1];
        valid = !((first == 0xe0u && second < 0xa0u) || (first == 0xedu && second > 0x9fu) || (first == 0xf0u && second < 0x90u) ||
                  (first == 0xf4u && second > 0x8fu));
    }
    return valid ? length : 0;
}

// The previous column lookup: decode from byte zero, counting each decoded
// sequence, and floor an offset inside a multibyte sequence to its start.
BUSTER_GLOBAL_LOCAL u64 ui_scale_oracle_columns(String8 string, u64 byte_offset)
{
    u64 result = 0;
    u64 position = 0;
    byte_offset = BUSTER_MIN(byte_offset, string.length);
    while (position < byte_offset)
    {
        u64 length = ui_scale_oracle_sequence_length(string, position);
        length = length ? length : 1;
        if (length > byte_offset - position)
        {
            break;
        }
        position += length;
        result += 1;
        ui_scale_oracle_decodes += 1;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL void ui_scale_fuzzy_frame_begin(Arena* arena, UI_ScaleFuzzyCase* test, UI_FuzzyMatchRange* ranges, u64 count, UI_ScaleFuzzyFrame* frame)
{
    memset(frame, 0, sizeof(*frame));
    frame->state = ui_state_allocate(0, 0);
    if (ui_scale_check(frame->state != 0, S8("fuzzy state allocation"), count))
    {
        ui_scale_frame_begin(frame->state);
        UI_Box* clip_parent = 0;
        if (test->clip_width > 0.0f)
        {
            ui_set_next_fixed_width(test->clip_width);
            ui_set_next_fixed_height(20.0f);
            clip_parent = ui_box_make(UI_BoxFlag_Clip | UI_BoxFlag_AllowOverflowX, S8("fuzzy clip"));
            ui_push_parent(clip_parent);
        }
        ui_set_next_font_size(10.0f);
        ui_set_next_text_padding(0.0f);
        ui_set_next_fixed_width(test->width);
        ui_set_next_fixed_height(20.0f);
        ui_set_next_text_alignment(test->align);
        frame->box = ui_box_make(UI_BoxFlag_DrawText | test->flags | (test->clip_width > 0.0f ? UI_BoxFlag_AllowOverflowX : 0), test->text);
        ui_box_set_fuzzy_match_ranges(frame->box, ranges, count);
        if (clip_parent)
        {
            BUSTER_UNUSED(ui_pop_parent());
        }
        ui_build_end();
        ui_scale_check(frame->box->fuzzy_match_range_count == count, S8("fuzzy ranges retained"), count);
        u64 decodes_before = frame->state->utf8_column_decodes;
        ui_scale_check(ui_draw(), S8("fuzzy draw succeeds"), count);
        frame->decodes = frame->state->utf8_column_decodes - decodes_before;
        frame->rects = arena_allocate(arena, F32Interval2, count + 1);
        for (u64 index = 0; index < frame->state->draw_command_count; index += 1)
        {
            UI_DrawCommand* command = &frame->state->draw_commands[index];
            // Highlights are the two-pixel strips on the box's bottom edge; the box's tooltip and
            // other rectangles are not part of the comparison.
            if (command->box == frame->box && command->kind == UI_DrawCommandKind_Rect && command->rect.y1 == frame->box->rect.y1 &&
                command->rect.y1 - command->rect.y0 == 2.0f && frame->rect_count < count)
            {
                frame->rects[frame->rect_count] = command->rect;
                frame->rect_count += 1;
            }
        }
    }
}

// Origin of the text for the case: the left edge of a reference highlight that
// starts at the first column.
BUSTER_GLOBAL_LOCAL f32 ui_scale_fuzzy_origin(Arena* arena, UI_ScaleFuzzyCase* test)
{
    UI_FuzzyMatchRange first_column = {.first = 0, .one_past_last = (u64)-1};
    UI_ScaleFuzzyFrame reference;
    f32 origin = 0.0f;
    ui_scale_fuzzy_frame_begin(arena, test, &first_column, 1, &reference);
    if (test->text.length == 0)
    {
        origin = 0.0f;
    }
    else if (ui_scale_check(reference.rect_count == 1, S8("origin reference highlight"), reference.rect_count))
    {
        origin = reference.rects[0].x0;
    }
    ui_state_deinitialize(reference.state);
    return origin;
}

// Draws `ranges` and compares every emitted highlight with the old algorithm,
// in range order. Returns the decode counts of the new and the old algorithm.
BUSTER_GLOBAL_LOCAL bool ui_scale_fuzzy_compare(Arena* arena, UI_ScaleFuzzyCase* test, UI_FuzzyMatchRange* ranges, u64 count, bool run_oracle, String8 name,
                                                u64* new_decodes, u64* old_decodes)
{
    bool same = true;
    UI_ScaleFuzzyFrame frame;
    ui_scale_fuzzy_frame_begin(arena, test, ranges, count, &frame);
    *new_decodes = frame.decodes;
    *old_decodes = 0;
    if (run_oracle && frame.state && frame.box)
    {
        UI_Box* box = frame.box;
        f32 origin = ui_scale_fuzzy_origin(arena, test);
        String8 string = box->string;
        u64 expected_count = 0;
        ui_scale_oracle_decodes = 0;
        for (u64 index = 0; index < count; index += 1)
        {
            u64 first_byte = BUSTER_MIN(ranges[index].first, box->text_visible_length);
            u64 last_byte = BUSTER_MIN(ranges[index].one_past_last, box->text_visible_length);
            u64 first = ui_scale_oracle_columns(string, first_byte);
            u64 last = ui_scale_oracle_columns(string, last_byte);
            if (last > first)
            {
                f32 x0 = origin + (f32)first * box->font_size * 0.60f;
                f32 x1 = origin + (f32)last * box->font_size * 0.60f;
                F32Interval2 rect = {.x0 = x0, .y0 = box->rect.y1 - 2.0f, .x1 = x1, .y1 = box->rect.y1};
                if (box->state_flags & UI_BoxState_Clipped)
                {
                    rect.x0 = BUSTER_MAX(rect.x0, box->clip_rect.x0);
                    rect.y0 = BUSTER_MAX(rect.y0, box->clip_rect.y0);
                    rect.x1 = BUSTER_MIN(rect.x1, box->clip_rect.x1);
                    rect.y1 = BUSTER_MIN(rect.y1, box->clip_rect.y1);
                }
                if (expected_count >= frame.rect_count || memcmp(&frame.rects[expected_count], &rect, sizeof(rect)) != 0)
                {
                    same = false;
                }
                expected_count += 1;
            }
        }
        *old_decodes = ui_scale_oracle_decodes;
        same = same && expected_count == frame.rect_count;
        ui_scale_check(same, name, expected_count);
    }
    ui_state_deinitialize(frame.state);
    return same;
}

BUSTER_GLOBAL_LOCAL void ui_scale_fuzzy_shuffle(UI_FuzzyMatchRange* ranges, u64 count, u64 seed)
{
    for (u64 index = count; index > 1; index -= 1)
    {
        u64 other = ui_scale_mix(seed + index) % index;
        UI_FuzzyMatchRange swap = ranges[index - 1];
        ranges[index - 1] = ranges[other];
        ranges[other] = swap;
    }
}

// Every (first, one_past_last) byte pair with first <= one_past_last (the setter
// rejects reversed ranges), including empty, mid-sequence, past-the-end and duplicate ranges, in a shuffled order.
BUSTER_GLOBAL_LOCAL void ui_scale_fuzzy_behavior(Arena* arena)
{
    static const struct
    {
        String8 text;
        String8 name;
    } texts[] = {
        {S8("hello world"), S8("fuzzy ascii oracle")},
        {S8("a\xc3\xa9\xe6\x97\xa5\xf0\x9f\x98\x80" "b"), S8("fuzzy multibyte oracle")},
        {S8("x\xff" "y\xc3"), S8("fuzzy invalid utf8 oracle")},
        {S8("\xf0\x9f\x98\x80\xf0\x9f\x98\x80\xf0\x9f\x98\x80\xf0\x9f\x98\x80"), S8("fuzzy four-byte oracle")},
    };
    static const struct
    {
        UI_BoxFlags flags;
        UI_TextAlign align;
        f32 width;
        f32 clip_width;
        String8 name;
    } layouts[] = {
        {0, UI_TextAlign_Left, 200.0f, 0.0f, S8("left")},
        {0, UI_TextAlign_Left, 38.0f, 0.0f, S8("truncated")},
        {UI_BoxFlag_DisableTextTrunc, UI_TextAlign_Left, 38.0f, 0.0f, S8("untruncated")},
        {UI_BoxFlag_DisableTextTrunc, UI_TextAlign_Center, 200.0f, 0.0f, S8("centered")},
        {0, UI_TextAlign_Right, 200.0f, 0.0f, S8("right")},
        {UI_BoxFlag_DisableTextTrunc, UI_TextAlign_Left, 200.0f, 27.0f, S8("clipped")},
    };
    for (u64 text_index = 0; text_index < BUSTER_ARRAY_LENGTH(texts); text_index += 1)
    {
        u64 limit = texts[text_index].text.length + 3;
        u64 count = (limit + 1) * (limit + 2) / 2 + 16;
        UI_FuzzyMatchRange* ranges = arena_allocate(arena, UI_FuzzyMatchRange, count);
        u64 used = 0;
        for (u64 first = 0; first <= limit; first += 1)
        {
            for (u64 last = first; last <= limit; last += 1)
            {
                ranges[used] = (UI_FuzzyMatchRange){.first = first, .one_past_last = last};
                used += 1;
            }
        }
        ranges[used++] = (UI_FuzzyMatchRange){.first = 0, .one_past_last = (u64)-1};
        ranges[used++] = (UI_FuzzyMatchRange){.first = (u64)-1, .one_past_last = (u64)-1};
        u64 pair_count = used - 2;
        for (; used < count; used += 1)
        {
            ranges[used] = ranges[used * 7 % pair_count];
        }
        ui_scale_fuzzy_shuffle(ranges, count, text_index);
        for (u64 layout_index = 0; layout_index < BUSTER_ARRAY_LENGTH(layouts); layout_index += 1)
        {
            UI_ScaleFuzzyCase test = {
                .text = texts[text_index].text,
                .flags = layouts[layout_index].flags,
                .align = layouts[layout_index].align,
                .width = layouts[layout_index].width,
                .clip_width = layouts[layout_index].clip_width,
            };
            u64 new_decodes = 0;
            u64 old_decodes = 0;
            ui_scale_fuzzy_compare(arena, &test, ranges, count, true, texts[text_index].name, &new_decodes, &old_decodes);
            ui_scale_check(new_decodes <= texts[text_index].text.length * 3, S8("fuzzy decode bounded by the text"), new_decodes);
        }
    }
    // Zero ranges and an empty string draw nothing and decode nothing.
    UI_ScaleFuzzyCase empty = {.text = S8(""), .align = UI_TextAlign_Left, .width = 50.0f};
    UI_FuzzyMatchRange any = {.first = 0, .one_past_last = 5};
    u64 new_decodes = 1;
    u64 old_decodes = 0;
    ui_scale_fuzzy_compare(arena, &empty, &any, 1, true, S8("fuzzy empty text oracle"), &new_decodes, &old_decodes);
    ui_scale_check(new_decodes == 0, S8("empty text decodes nothing"), new_decodes);
}

BUSTER_GLOBAL_LOCAL String8 ui_scale_fuzzy_text(Arena* arena, bool multibyte, u64 characters, u64* offsets)
{
    static const char* const glyphs[] = {"a", "\xc3\xa9", "\xe6\x97\xa5", "\xf0\x9f\x98\x80"};
    char8* buffer = arena_allocate(arena, char8, characters * 4 + 1);
    u64 length = 0;
    for (u64 index = 0; index < characters; index += 1)
    {
        const char* glyph = multibyte ? glyphs[index % 4] : glyphs[0];
        u64 glyph_length = multibyte ? (index % 4) + 1 : 1;
        offsets[index] = length;
        memcpy(buffer + length, glyph, glyph_length);
        length += glyph_length;
    }
    offsets[characters] = length;
    return (String8){.pointer = buffer, .length = length};
}

// One-character ranges spread over the whole text: work counters must grow
// with L + R rather than L x R, in the text length and in the range count.
BUSTER_GLOBAL_LOCAL void ui_scale_fuzzy_scaling(Arena* arena)
{
    static const u64 characters[] = {1024, 4096, 16384};
    static const u64 range_counts[] = {64, 1024, 4096};
    for (u64 multibyte = 0; multibyte < 2; multibyte += 1)
    {
        for (u64 length_index = 0; length_index < BUSTER_ARRAY_LENGTH(characters); length_index += 1)
        {
            u64 count = characters[length_index];
            u64* offsets = arena_allocate(arena, u64, count + 1);
            String8 text = ui_scale_fuzzy_text(arena, multibyte != 0, count, offsets);
            for (u64 range_index = 0; range_index < BUSTER_ARRAY_LENGTH(range_counts); range_index += 1)
            {
                u64 range_count = BUSTER_MIN(range_counts[range_index], count);
                UI_FuzzyMatchRange* ranges = arena_allocate(arena, UI_FuzzyMatchRange, range_count);
                for (u64 index = 0; index < range_count; index += 1)
                {
                    u64 character = (count - 1) - index * (count / range_count);
                    ranges[index] = (UI_FuzzyMatchRange){.first = offsets[character], .one_past_last = offsets[character + 1]};
                }
                UI_ScaleFuzzyCase test = {.text = text, .flags = UI_BoxFlag_DisableTextTrunc, .align = UI_TextAlign_Left, .width = (f32)text.length * 6.0f + 20.0f};
                // The O(R x L) oracle is only affordable on the smaller text sizes.
                bool run_oracle = count <= 4096;
                u64 new_decodes = 0;
                u64 old_decodes = 0;
                ui_scale_fuzzy_compare(arena, &test, ranges, range_count, run_oracle, S8("scaling draw matches the oracle"), &new_decodes, &old_decodes);
                printf("ui_scale: fuzzy %s chars=%-6llu bytes=%-6llu ranges=%-5llu decodes=%llu old-algorithm decodes=%llu\n", multibyte ? "multibyte" : "ascii    ",
                       (unsigned long long)count, (unsigned long long)text.length, (unsigned long long)range_count, (unsigned long long)new_decodes,
                       (unsigned long long)old_decodes);
                // One decode per sequence up to the last highlighted byte, never per range.
                ui_scale_check(new_decodes <= count, S8("fuzzy decode work is bounded by the text length"), new_decodes);
                if (run_oracle && range_count >= 1024)
                {
                    ui_scale_check(old_decodes > new_decodes * 16, S8("fuzzy decode work dropped well below the old algorithm"), old_decodes);
                }
            }
        }
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
    ui_scale_lookup_scaling();
    ui_scale_table_behavior(arena);
    ui_scale_focus_behavior(arena);
    ui_scale_focus_scaling(arena);
    ui_scale_fuzzy_behavior(arena);
    ui_scale_fuzzy_scaling(arena);
    ui_scale_check(ui_scale_renderer_calls == 0, S8("headless rendering boundary"), ui_scale_renderer_calls);
    printf("ui_scale_component_tests: %u/%u assertions passed\n", (unsigned)(ui_scale_assertions - ui_scale_failures), (unsigned)ui_scale_assertions);
    thread_context_release(context);
    arena_destroy(arena, 1);
    program_state->arena = 0;
    return ui_scale_failures != 0;
}
