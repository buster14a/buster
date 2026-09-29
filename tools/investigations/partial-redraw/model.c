/* Isolated CPU research model; no production renderer or GPU execution.
 * Orientation: oracle_render independently evaluates each pixel; command_plan
 * and tile_plan prove dirtiness without consulting output pixels; replay uses
 * current-order region replay for both incremental and unconditional redraw.
 * Contract: contract.md. Fixed 128x96 target, <=64 stable-ID commands, integer
 * clipped rectangles, RGBA8 plus nearest 8x8 R8 coverage, immutable epochs.
 * RGB is round-nearest source-over; destination alpha is replaced by source
 * alpha, matching the observed blend factors but not GPU numerical behavior.
 * No allocation, recursion, callbacks, threads, synchronization, or GPU API.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define BUSTER_WIDTH 128
#define BUSTER_HEIGHT 96
#define BUSTER_TILE 16
#define BUSTER_TILE_COLUMNS (BUSTER_WIDTH / BUSTER_TILE)
#define BUSTER_TILE_ROWS (BUSTER_HEIGHT / BUSTER_TILE)
#define BUSTER_TILES (BUSTER_TILE_COLUMNS * BUSTER_TILE_ROWS)
#define BUSTER_PIXELS (BUSTER_WIDTH * BUSTER_HEIGHT)
#define BUSTER_COMMANDS 64
#define BUSTER_TEXTURES 8
#define BUSTER_TEXELS 64
#define BUSTER_RANDOM_TRANSITIONS 2000
#define BUSTER_TIMING_BLOCKS 7
#define BUSTER_TIMING_REPEATS 100

typedef struct Rect { int x0, y0, x1, y1; } Rect;
typedef struct Pixel { uint8_t r, g, b, a; } Pixel;
typedef struct Texture { uint32_t generation; uint8_t coverage[BUSTER_TEXELS]; } Texture;
typedef struct Command
{
    Rect rect, clip;
    Pixel color;
    uint32_t generation;
    uint8_t id, texture, textured;
} Command;
typedef struct Frame
{
    Command commands[BUSTER_COMMANDS];
    Texture textures[BUSTER_TEXTURES];
    Pixel clear;
    uint32_t count;
    int width, height;
    bool unsupported;
} Frame;
typedef struct Lists { uint8_t count[BUSTER_TILES]; uint8_t ids[BUSTER_TILES][BUSTER_COMMANDS]; } Lists;
typedef struct Metrics
{
    uint64_t candidate_tests, blended_samples, replayed_commands, dependency_edges;
    uint64_t id_comparisons, command_comparisons, tile_membership_tests;
    uint64_t tile_count_comparisons, tile_id_comparisons, replay_coverage_tests;
    uint64_t replay_pixel_tests, clear_pixel_tests;
    uint32_t dirty_pixels, changed_pixels;
} Metrics;
typedef struct Plan
{
    uint64_t bits[BUSTER_PIXELS / 64];
    uint64_t tiles;
    Metrics metrics;
    bool full, rejected;
} Plan;

static int maximum(int a, int b) { int result = a > b ? a : b; return result; }
static int minimum(int a, int b) { int result = a < b ? a : b; return result; }
static bool pixel_equal(Pixel a, Pixel b)
{
    bool result = a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
    return result;
}
static bool rect_equal(Rect a, Rect b)
{
    bool result = a.x0 == b.x0 && a.y0 == b.y0 && a.x1 == b.x1 && a.y1 == b.y1;
    return result;
}
static Rect intersection(Rect a, Rect b)
{
    Rect result = {maximum(a.x0, b.x0), maximum(a.y0, b.y0), minimum(a.x1, b.x1), minimum(a.y1, b.y1)};
    return result;
}
static bool nonempty(Rect a) { bool result = a.x0 < a.x1 && a.y0 < a.y1; return result; }
static Rect bounds(const Frame *frame, const Command *command)
{
    Rect target = {0, 0, frame->width, frame->height};
    Rect result = intersection(intersection(command->rect, command->clip), target);
    return result;
}
static bool command_equal(Command a, Command b)
{
    bool result = a.id == b.id && rect_equal(a.rect, b.rect) && rect_equal(a.clip, b.clip) &&
        pixel_equal(a.color, b.color) && a.textured == b.textured &&
        (!a.textured || (a.texture == b.texture && a.generation == b.generation));
    return result;
}
static bool valid_frame(const Frame *frame)
{
    bool result = frame->count <= BUSTER_COMMANDS && frame->clear.a == 255 && frame->width > 0 && frame->height > 0 &&
        frame->width <= BUSTER_WIDTH && frame->height <= BUSTER_HEIGHT;
    uint64_t ids = 0;
    for (uint32_t i = 0; result && i < frame->count; i += 1)
    {
        const Command *command = &frame->commands[i];
        result = command->id < BUSTER_COMMANDS && command->textured <= 1;
        if (result)
        {
            uint64_t bit = UINT64_C(1) << command->id;
            result = !(ids & bit);
            ids |= bit;
        }
        if (result && command->textured)
        {
            result = command->texture < BUSTER_TEXTURES &&
                command->generation == frame->textures[command->texture].generation;
        }
        Rect rects[2] = {command->rect, command->clip};
        for (int j = 0; result && j < 2; j += 1)
        {
            Rect r = rects[j];
            result = r.x0 >= -256 && r.y0 >= -256 && r.x1 <= 256 && r.y1 <= 256 &&
                r.x0 <= r.x1 && r.y0 <= r.y1;
        }
    }
    return result;
}
static unsigned alpha_at(const Frame *frame, const Command *command, int x, int y)
{
    unsigned result = command->color.a;
    if (command->textured)
    {
        int tx = (8 * (2 * (x - command->rect.x0) + 1)) / (2 * (command->rect.x1 - command->rect.x0));
        int ty = (8 * (2 * (y - command->rect.y0) + 1)) / (2 * (command->rect.y1 - command->rect.y0));
        tx = maximum(0, minimum(7, tx));
        ty = maximum(0, minimum(7, ty));
        unsigned coverage = frame->textures[command->texture].coverage[ty * 8 + tx];
        result = (result * coverage + 127) / 255;
    }
    return result;
}
static Pixel blend(Pixel old, Pixel source, unsigned alpha)
{
    Pixel result = {
        (uint8_t)((source.r * alpha + old.r * (255 - alpha) + 127) / 255),
        (uint8_t)((source.g * alpha + old.g * (255 - alpha) + 127) / 255),
        (uint8_t)((source.b * alpha + old.b * (255 - alpha) + 127) / 255),
        (uint8_t)alpha,
    };
    return result;
}
static void oracle_render(const Frame *frame, Pixel *output)
{
    for (int y = 0; y < BUSTER_HEIGHT; y += 1)
    {
        for (int x = 0; x < BUSTER_WIDTH; x += 1)
        {
            Pixel value = frame->clear;
            for (uint32_t i = 0; i < frame->count; i += 1)
            {
                const Command *c = &frame->commands[i];
                if (x < frame->width && y < frame->height && x >= c->rect.x0 && x < c->rect.x1 &&
                    y >= c->rect.y0 && y < c->rect.y1 && x >= c->clip.x0 && x < c->clip.x1 &&
                    y >= c->clip.y0 && y < c->clip.y1)
                {
                    unsigned alpha = c->color.a;
                    if (c->textured)
                    {
                        unsigned u = (unsigned)(8 * (2 * (x - c->rect.x0) + 1) / (2 * (c->rect.x1 - c->rect.x0)));
                        unsigned v = (unsigned)(8 * (2 * (y - c->rect.y0) + 1) / (2 * (c->rect.y1 - c->rect.y0)));
                        alpha = (alpha * frame->textures[c->texture].coverage[v * 8 + u] + 127) / 255;
                    }
                    value.r = (uint8_t)(((unsigned)c->color.r * alpha + (unsigned)value.r * (255 - alpha) + 127) / 255);
                    value.g = (uint8_t)(((unsigned)c->color.g * alpha + (unsigned)value.g * (255 - alpha) + 127) / 255);
                    value.b = (uint8_t)(((unsigned)c->color.b * alpha + (unsigned)value.b * (255 - alpha) + 127) / 255);
                    value.a = (uint8_t)alpha;
                }
            }
            output[y * BUSTER_WIDTH + x] = value;
        }
    }
}
static void mark(Plan *plan, Rect r)
{
    for (int y = r.y0; y < r.y1; y += 1)
    {
        for (int x = r.x0; x < r.x1; x += 1)
        {
            unsigned p = (unsigned)(y * BUSTER_WIDTH + x);
            uint64_t bit = UINT64_C(1) << (p % 64);
            if (!(plan->bits[p / 64] & bit))
            {
                plan->metrics.dirty_pixels += 1;
                plan->bits[p / 64] |= bit;
            }
            unsigned tile = (unsigned)(y / BUSTER_TILE * BUSTER_TILE_COLUMNS + x / BUSTER_TILE);
            plan->tiles |= UINT64_C(1) << tile;
        }
    }
}
static void full_plan(Plan *plan)
{
    memset(plan, 0, sizeof(*plan));
    plan->full = true;
    /* Full replay ignores the damage mask. Do not construct it pixel by pixel. */
    plan->tiles = (UINT64_C(1) << BUSTER_TILES) - 1;
    plan->metrics.dirty_pixels = BUSTER_PIXELS;
}
static void id_map(const Frame *frame, uint8_t map[BUSTER_COMMANDS])
{
    memset(map, 255, BUSTER_COMMANDS);
    for (uint32_t i = 0; i < frame->count; i += 1) { map[frame->commands[i].id] = (uint8_t)i; }
}
static bool needs_fallback(const Frame *old, const Frame *now, bool history)
{
    bool result = !history || old->unsupported || now->unsupported ||
        old->width != BUSTER_WIDTH || old->height != BUSTER_HEIGHT ||
        now->width != BUSTER_WIDTH || now->height != BUSTER_HEIGHT ||
        old->width != now->width || old->height != now->height || !pixel_equal(old->clear, now->clear);
    for (int i = 0; i < BUSTER_TEXTURES; i += 1)
    {
        if (old->textures[i].generation == now->textures[i].generation &&
            memcmp(old->textures[i].coverage, now->textures[i].coverage, BUSTER_TEXELS))
        {
            /* Violating immutable-epoch contract cannot certify unchanged users. */
            result = true;
        }
    }
    return result;
}
static void command_plan(const Frame *old, const Frame *now, bool history, Plan *plan)
{
    memset(plan, 0, sizeof(*plan));
    if (!valid_frame(now)) { full_plan(plan); plan->rejected = true; }
    else if (!history || !valid_frame(old)) { full_plan(plan); }
    else if (needs_fallback(old, now, history)) { full_plan(plan); }
    else
    {
        uint8_t previous[BUSTER_COMMANDS], current[BUSTER_COMMANDS];
        id_map(old, previous);
        id_map(now, current);
        for (int id = 0; id < BUSTER_COMMANDS; id += 1)
        {
            unsigned a = previous[id], b = current[id];
            plan->metrics.candidate_tests += 1;
            plan->metrics.id_comparisons += 1;
            bool changed = a != b;
            if (a != 255 && b != 255)
            {
                plan->metrics.command_comparisons += 1;
                changed |= !command_equal(old->commands[a], now->commands[b]);
            }
            if (changed)
            {
                if (a != 255) { mark(plan, bounds(old, &old->commands[a])); }
                if (b != 255) { mark(plan, bounds(now, &now->commands[b])); }
            }
        }
    }
}
static Rect tile_rect(int tile)
{
    int x = tile % BUSTER_TILE_COLUMNS * BUSTER_TILE;
    int y = tile / BUSTER_TILE_COLUMNS * BUSTER_TILE;
    Rect result = {x, y, x + BUSTER_TILE, y + BUSTER_TILE};
    return result;
}
static void build_lists(const Frame *frame, Lists *lists, Metrics *metrics)
{
    memset(lists, 0, sizeof(*lists));
    for (uint32_t i = 0; i < frame->count; i += 1)
    {
        Rect r = bounds(frame, &frame->commands[i]);
        for (int tile = 0; tile < BUSTER_TILES; tile += 1)
        {
            metrics->candidate_tests += 1;
            metrics->tile_membership_tests += 1;
            if (nonempty(intersection(r, tile_rect(tile))))
            {
                lists->ids[tile][lists->count[tile]] = frame->commands[i].id;
                lists->count[tile] += 1;
                metrics->dependency_edges += 1;
            }
        }
    }
}
static void tile_plan(const Frame *old, const Frame *now, const Lists *previous, bool history, Plan *plan, Lists *current)
{
    memset(plan, 0, sizeof(*plan));
    memset(current, 0, sizeof(*current));
    if (!valid_frame(now)) { full_plan(plan); plan->rejected = true; }
    else
    {
        build_lists(now, current, &plan->metrics);
        if (!history || !valid_frame(old) || needs_fallback(old, now, history))
        {
            Metrics metrics = plan->metrics;
            full_plan(plan);
            plan->metrics.candidate_tests = metrics.candidate_tests;
            plan->metrics.dependency_edges = metrics.dependency_edges;
            plan->metrics.tile_membership_tests = metrics.tile_membership_tests;
        }
        else
        {
            uint8_t old_map[BUSTER_COMMANDS], new_map[BUSTER_COMMANDS];
            id_map(old, old_map);
            id_map(now, new_map);
            for (int tile = 0; tile < BUSTER_TILES; tile += 1)
            {
                plan->metrics.tile_count_comparisons += 1;
                bool changed = previous->count[tile] != current->count[tile];
                for (unsigned j = 0; !changed && j < current->count[tile]; j += 1)
                {
                    unsigned a = previous->ids[tile][j], b = current->ids[tile][j];
                    plan->metrics.candidate_tests += 1;
                    plan->metrics.tile_id_comparisons += 1;
                    changed = a != b || old_map[a] == 255;
                    if (!changed)
                    {
                        plan->metrics.command_comparisons += 1;
                        changed = !command_equal(old->commands[old_map[a]], now->commands[new_map[b]]);
                    }
                }
                if (changed) { mark(plan, tile_rect(tile)); }
            }
        }
    }
}
static void draw_region(const Frame *frame, const Command *command, Rect region, Plan *plan, Pixel *output)
{
    plan->metrics.replayed_commands += 1;
    plan->metrics.replay_pixel_tests += (uint64_t)(region.x1 - region.x0) * (uint64_t)(region.y1 - region.y0);
    for (int y = region.y0; y < region.y1; y += 1)
    {
        for (int x = region.x0; x < region.x1; x += 1)
        {
            unsigned p = (unsigned)(y * BUSTER_WIDTH + x);
            if (plan->full || (plan->bits[p / 64] & (UINT64_C(1) << (p % 64))))
            {
                output[p] = blend(output[p], command->color, alpha_at(frame, command, x, y));
                plan->metrics.blended_samples += 1;
            }
        }
    }
}
static void clear_pixels(const Frame *frame, Plan *plan, Pixel *output)
{
    if (!plan->rejected)
    {
        plan->metrics.clear_pixel_tests += BUSTER_PIXELS;
        for (unsigned p = 0; p < BUSTER_PIXELS; p += 1)
        {
            if (plan->full || (plan->bits[p / 64] & (UINT64_C(1) << (p % 64)))) { output[p] = frame->clear; }
        }
    }
}
static void replay(const Frame *frame, Plan *plan, Pixel *output)
{
    clear_pixels(frame, plan, output);
    if (!plan->rejected)
    {
        for (uint32_t i = 0; i < frame->count; i += 1)
        {
            const Command *command = &frame->commands[i];
            Rect r = bounds(frame, command);
            if (plan->full)
            {
                plan->metrics.candidate_tests += 1;
                plan->metrics.replay_coverage_tests += 1;
                if (nonempty(r)) { draw_region(frame, command, r, plan, output); }
            }
            else
            {
                for (int tile = 0; tile < BUSTER_TILES; tile += 1)
                {
                    if (plan->tiles & (UINT64_C(1) << tile))
                    {
                        plan->metrics.candidate_tests += 1;
                        plan->metrics.replay_coverage_tests += 1;
                        Rect region = intersection(r, tile_rect(tile));
                        if (nonempty(region)) { draw_region(frame, command, region, plan, output); }
                    }
                }
            }
        }
    }
}
static void replay_tiles(const Frame *frame, const Lists *current, Plan *plan, Pixel *output)
{
    if (plan->full || plan->rejected) { replay(frame, plan, output); }
    else
    {
        clear_pixels(frame, plan, output);
        uint8_t map[BUSTER_COMMANDS];
        id_map(frame, map);
        for (int tile = 0; tile < BUSTER_TILES; tile += 1)
        {
            if (plan->tiles & (UINT64_C(1) << tile))
            {
                for (unsigned j = 0; j < current->count[tile]; j += 1)
                {
                    const Command *command = &frame->commands[map[current->ids[tile][j]]];
                    Rect region = intersection(bounds(frame, command), tile_rect(tile));
                    plan->metrics.candidate_tests += 1;
                    plan->metrics.replay_coverage_tests += 1;
                    draw_region(frame, command, region, plan, output);
                }
            }
        }
    }
}
static Frame initial_frame(void)
{
    Frame result;
    memset(&result, 0, sizeof(result));
    result.width = BUSTER_WIDTH;
    result.height = BUSTER_HEIGHT;
    result.clear = (Pixel){17, 29, 43, 255};
    for (int texture = 0; texture < BUSTER_TEXTURES; texture += 1)
    {
        result.textures[texture].generation = 1;
        for (int p = 0; p < BUSTER_TEXELS; p += 1) { result.textures[texture].coverage[p] = (uint8_t)((p * 37 + texture * 11) % 256); }
    }
    return result;
}
static Command rectangle(unsigned id, Rect r, Pixel color)
{
    Command result;
    memset(&result, 0, sizeof(result));
    result.id = (uint8_t)id;
    result.rect = r;
    result.clip = (Rect){0, 0, BUSTER_WIDTH, BUSTER_HEIGHT};
    result.color = color;
    return result;
}
static unsigned changed_count(const Pixel *a, const Pixel *b)
{
    unsigned result = 0;
    for (int p = 0; p < BUSTER_PIXELS; p += 1) { result += !pixel_equal(a[p], b[p]); }
    return result;
}
static bool check_transition(const Frame *old, const Frame *now, bool history, const char *name, bool print)
{
    static Pixel previous_pixels[BUSTER_PIXELS], expected[BUSTER_PIXELS], actual[BUSTER_PIXELS];
    Plan plan;
    Lists old_lists, new_lists;
    Metrics ignored = {0};
    bool result = valid_frame(old) && valid_frame(now);
    if (result)
    {
        oracle_render(old, previous_pixels);
        oracle_render(now, expected);
        build_lists(old, &old_lists, &ignored);
        unsigned changed = changed_count(previous_pixels, expected);
        for (int strategy = 0; strategy < 3; strategy += 1)
        {
            memcpy(actual, previous_pixels, sizeof(actual));
            if (strategy == 0) { full_plan(&plan); }
            else if (strategy == 1) { command_plan(old, now, history, &plan); }
            else { tile_plan(old, now, &old_lists, history, &plan, &new_lists); }
            if (strategy == 2) { replay_tiles(now, &new_lists, &plan, actual); }
            else { replay(now, &plan, actual); }
            plan.metrics.changed_pixels = changed;
            unsigned differences = changed_count(actual, expected);
            if (differences)
            {
                printf("FAIL,%s,strategy=%d,differing_pixels=%u\n", name, strategy, differences);
                result = false;
            }
            if (print)
            {
                const char *strategy_name = strategy == 0 ? "full" : strategy == 1 ? "command" : "tile";
                size_t retained = strategy == 0 ? 0 : sizeof(Frame) + (strategy == 2 ? sizeof(Lists) : 0);
                printf("counts,%s,%s,%u,%u,%llu,%llu,%llu,%llu,%zu,%zu,%d\n", name, strategy_name,
                    changed, plan.metrics.dirty_pixels, (unsigned long long)plan.metrics.candidate_tests,
                    (unsigned long long)plan.metrics.blended_samples, (unsigned long long)plan.metrics.replayed_commands,
                    (unsigned long long)plan.metrics.dependency_edges, retained, sizeof(previous_pixels), plan.full);
                printf("work,%s,%s,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu\n", name, strategy_name,
                    (unsigned long long)plan.metrics.id_comparisons,
                    (unsigned long long)plan.metrics.command_comparisons,
                    (unsigned long long)plan.metrics.tile_membership_tests,
                    (unsigned long long)plan.metrics.tile_count_comparisons,
                    (unsigned long long)plan.metrics.tile_id_comparisons,
                    (unsigned long long)plan.metrics.replay_coverage_tests,
                    (unsigned long long)plan.metrics.replay_pixel_tests,
                    (unsigned long long)plan.metrics.clear_pixel_tests);
            }
        }
    }
    return result;
}

#include "fixtures.inc"
#include "benchmark.inc"
int main(int argc, char **argv)
{
    bool okay = check_fixtures();
    okay = check_random() && okay;
    printf("counts,workload,strategy,changed_pixels,dirty_pixels,candidate_tests,blended_samples,replayed_commands,dependency_edges,retained_metadata_bytes,history_bytes,full_fallback\n");
    printf("work,workload,strategy,id_comparisons,command_comparisons,tile_membership_tests,tile_count_comparisons,tile_id_comparisons,replay_coverage_tests,replay_pixel_tests,clear_pixel_tests\n");
    printf("storage,frame_bytes=%zu,dependency_snapshot_bytes=%zu,plan_scratch_bytes=%zu,damage_mask_bytes=%zu,dirty_tile_bits_bytes=%zu,history_bytes=%zu,id_map_scratch_bytes=%d,resources=%d\n",
        sizeof(Frame), sizeof(Lists), sizeof(Plan), sizeof(((Plan *)0)->bits), sizeof(((Plan *)0)->tiles),
        sizeof(Pixel) * BUSTER_PIXELS, 2 * BUSTER_COMMANDS, BUSTER_TEXTURES);
    for (int workload = 0; workload < 6; workload += 1)
    {
        Frame old, now;
        make_workload(workload, &old, &now);
        okay = check_transition(&old, &now, true, workload_name(workload), true) && okay;
    }
    printf("checks,%s\n", okay ? "PASS" : "FAIL");
    if (okay && argc == 2 && !strcmp(argv[1], "--bench")) { okay = benchmark_models(); }
    else if (argc > 1) { okay = false; }
    return okay ? 0 : 1;
}
