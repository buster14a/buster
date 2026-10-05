// Headless UI scalability regressions. test_ui_scale compiles the actual
// ui_core module and checks, with work counters instead of timers, that keyed
// box lookup and keyboard focus navigation stay linear in the number of boxes,
// while the observable behavior of the box table and of navigation is
// unchanged. Only the unused native rendering boundary is supplied; no compiler
// or desktop window/backend dependency belongs to this component runner.
//
// Map: ui_scale_check and the splitmix helpers, the keyed-box lookup scaling
// and behavior cases, then main.

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
    ui_scale_check(ui_scale_renderer_calls == 0, S8("headless rendering boundary"), ui_scale_renderer_calls);
    printf("ui_scale_component_tests: %u/%u assertions passed\n", (unsigned)(ui_scale_assertions - ui_scale_failures), (unsigned)ui_scale_assertions);
    thread_context_release(context);
    arena_destroy(arena, 1);
    program_state->arena = 0;
    return ui_scale_failures != 0;
}
