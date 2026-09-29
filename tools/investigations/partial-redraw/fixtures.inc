/* Bounded CPU-model fixtures; included by model.c, not a production module.
 * Exact RGBA equality was declared before execution. Deliberately incorrect
 * invalidators must fail their witnesses; their failure is test success.
 */
static bool require_condition(bool condition, const char *name)
{
    if (!condition) { printf("FAIL,fixture,%s\n", name); }
    return condition;
}
static bool flags_match(const Frame *old, const Frame *now, bool history, bool full, bool rejected)
{
    Plan command, tile;
    Lists previous, current;
    Metrics ignored = {0};
    memset(&previous, 0, sizeof(previous));
    if (valid_frame(old)) { build_lists(old, &previous, &ignored); }
    command_plan(old, now, history, &command);
    tile_plan(old, now, &previous, history, &tile, &current);
    bool result = command.full == full && tile.full == full &&
        command.rejected == rejected && tile.rejected == rejected;
    return result;
}
static bool rejected_replay_untouched(const Frame *old, const Frame *now)
{
    static Pixel before[BUSTER_PIXELS], after[BUSTER_PIXELS];
    memset(before, 7, sizeof(before));
    memcpy(after, before, sizeof(after));
    Plan plan;
    command_plan(old, now, true, &plan);
    replay(now, &plan, after);
    bool result = plan.rejected && !memcmp(before, after, sizeof(before));
    return result;
}
static bool mutation_witnesses(void)
{
    static Pixel history[BUSTER_PIXELS], expected[BUSTER_PIXELS], actual[BUSTER_PIXELS];
    Frame old = initial_frame();
    old.count = 1;
    old.commands[0] = rectangle(0, (Rect){8, 8, 16, 16}, (Pixel){255, 0, 0, 255});
    Frame now = old;
    now.commands[0].rect = (Rect){64, 64, 72, 72};
    oracle_render(&old, history);
    oracle_render(&now, expected);
    memcpy(actual, history, sizeof(actual));
    Plan wrong;
    memset(&wrong, 0, sizeof(wrong));
    mark(&wrong, bounds(&now, &now.commands[0]));
    replay(&now, &wrong, actual);
    unsigned move = changed_count(actual, expected);

    now = old;
    now.count = 0;
    oracle_render(&now, expected);
    memset(&wrong, 0, sizeof(wrong));
    memcpy(actual, history, sizeof(actual));
    replay(&now, &wrong, actual);
    unsigned removal = changed_count(actual, expected);

    old.commands[0] = rectangle(0, (Rect){0, 0, 32, 32}, (Pixel){255, 0, 0, 255});
    old.commands[1] = rectangle(1, (Rect){0, 0, 32, 32}, (Pixel){0, 0, 255, 128});
    old.count = 2;
    now = old;
    now.commands[0].color = (Pixel){0, 255, 0, 255};
    oracle_render(&old, history);
    oracle_render(&now, expected);
    memcpy(actual, history, sizeof(actual));
    full_plan(&wrong);
    draw_region(&now, &now.commands[0], bounds(&now, &now.commands[0]), &wrong, actual);
    unsigned changed_only = changed_count(actual, expected);

    old = initial_frame();
    old.count = 1;
    old.commands[0] = rectangle(0, (Rect){0, 0, 16, 16}, (Pixel){255, 255, 255, 255});
    old.commands[0].textured = 1;
    old.commands[0].generation = 1;
    now = old;
    now.textures[0].generation += 1;
    now.commands[0].generation += 1;
    for (int i = 0; i < BUSTER_TEXELS; i += 1) { now.textures[0].coverage[i] ^= 255; }
    oracle_render(&old, history);
    oracle_render(&now, expected);
    memcpy(actual, history, sizeof(actual));
    memset(&wrong, 0, sizeof(wrong));
    replay(&now, &wrong, actual);
    unsigned resource = changed_count(actual, expected);

    old = initial_frame(); old.count = 2;
    old.commands[0] = rectangle(0, (Rect){0, 0, 32, 32}, (Pixel){255, 0, 0, 128});
    old.commands[1] = rectangle(1, (Rect){0, 0, 32, 32}, (Pixel){0, 0, 255, 128});
    now = old; now.commands[0].color.g = 255; now.commands[1].color.r = 255;
    oracle_render(&now, expected);
    for (int p = 0; p < BUSTER_PIXELS; p += 1) { actual[p] = now.clear; }
    full_plan(&wrong);
    for (int overlap = 0; overlap < 2; overlap += 1)
    {
        for (unsigned i = 0; i < now.count; i += 1)
        {
            draw_region(&now, &now.commands[i], bounds(&now, &now.commands[i]), &wrong, actual);
        }
    }
    unsigned overlapping_replay = changed_count(actual, expected);
    printf("mutation_witness,new_bounds_move,%u\n", move);
    printf("mutation_witness,new_bounds_removal,%u\n", removal);
    printf("mutation_witness,changed_command_over_history,%u\n", changed_only);
    printf("mutation_witness,rectangle_only_epoch_omission,%u\n", resource);
    printf("mutation_witness,overlapping_regions_double_blend,%u\n", overlapping_replay);
    bool result = move > 0 && removal > 0 && changed_only > 0 && resource > 0 && overlapping_replay > 0;
    return result;
}
static bool check_fixtures(void)
{
    bool result = true;
    Pixel red = {255, 0, 0, 255}, blue = {0, 0, 255, 128};
    result &= require_condition(pixel_equal(blend(red, blue, 128), (Pixel){127, 0, 128, 128}), "blend_red_blue");
    result &= require_condition(pixel_equal(blend((Pixel){0, 255, 0, 255}, blue, 128), (Pixel){0, 127, 128, 128}), "blend_green_blue");
    result &= require_condition(pixel_equal(blend(red, blue, 0), (Pixel){255, 0, 0, 0}), "alpha_zero_replaces_alpha");

    Frame old = initial_frame();
    old.count = 2;
    old.commands[0] = rectangle(0, (Rect){0, 0, 32, 32}, red);
    old.commands[1] = rectangle(1, (Rect){0, 0, 32, 32}, blue);
    Frame now = old;
    static Pixel pixels[BUSTER_PIXELS];
    oracle_render(&old, pixels);
    result &= require_condition(pixel_equal(pixels[0], (Pixel){127, 0, 128, 128}), "oracle_red_blue_numerical_golden");
    now.commands[0].color = (Pixel){0, 255, 0, 255};
    result &= check_transition(&old, &now, true, "behind_transparency", false);
    now = old; now.commands[2] = rectangle(2, (Rect){16, 16, 48, 48}, (Pixel){31, 63, 95, 127}); now.count = 3;
    result &= check_transition(&old, &now, true, "command_addition", false);
    now = old; now.count = 1;
    result &= check_transition(&old, &now, true, "command_removal", false);
    now = old; now.commands[0].rect = (Rect){64, 64, 96, 96};
    result &= check_transition(&old, &now, true, "command_move", false);
    now = old; now.commands[0] = old.commands[1]; now.commands[1] = old.commands[0];
    result &= check_transition(&old, &now, true, "overlap_reorder", false);
    now = old; now.commands[1].clip = (Rect){8, 8, 12, 12};
    result &= check_transition(&old, &now, true, "clip_shrink", false);
    now = old; now.commands[1].color.a = 0;
    result &= check_transition(&old, &now, true, "alpha_zero_change", false);
    old = now; now.count = 1;
    result &= check_transition(&old, &now, true, "alpha_zero_removal", false);

    old = initial_frame();
    old.clear = (Pixel){0, 0, 0, 255};
    old.count = 1;
    old.commands[0] = rectangle(0, (Rect){-4, 0, 4, 8}, (Pixel){255, 255, 255, 255});
    old.commands[0].textured = 1; old.commands[0].generation = 1;
    for (int i = 0; i < BUSTER_TEXELS; i += 1) { old.textures[0].coverage[i] = (uint8_t)(i % 8 * 32); }
    oracle_render(&old, pixels);
    for (int x = 0; x < 4; x += 1)
    {
        uint8_t value = (uint8_t)((x + 4) * 32);
        result &= require_condition(pixel_equal(pixels[x], (Pixel){value, value, value, value}), "clip_preserves_original_sampling");
    }
    old.commands[0].rect = (Rect){0, 0, 3, 8};
    oracle_render(&old, pixels);
    uint8_t texels[3] = {1, 4, 6};
    for (int x = 0; x < 3; x += 1)
    {
        uint8_t value = (uint8_t)(texels[x] * 32);
        result &= require_condition(pixel_equal(pixels[x], (Pixel){value, value, value, value}), "pixel_center_width_three");
    }
    now = old;
    now.commands[0].rect = (Rect){0, 0, 0, 8};
    result &= check_transition(&old, &now, true, "empty_textured_rect", false);
    now = old; now.textures[0].generation += 1; now.commands[0].generation += 1;
    for (int i = 0; i < BUSTER_TEXELS; i += 1) { now.textures[0].coverage[i] ^= 255; }
    result &= check_transition(&old, &now, true, "resource_generation", false);
    now = old; now.textures[0].coverage[0] ^= 255;
    result &= require_condition(flags_match(&old, &now, true, true, false), "immutable_epoch_violation_full");
    result &= check_transition(&old, &now, true, "resource_epoch_alias", false);
    now = old;
    result &= require_condition(flags_match(&old, &now, false, true, false), "history_lost_full");
    result &= check_transition(&old, &now, false, "history_lost", false);
    now.unsupported = true;
    result &= require_condition(flags_match(&old, &now, true, true, false), "contract_flag_full");
    result &= check_transition(&old, &now, true, "contract_flag_known_scene", false);
    now = old; now.clear.r += 1;
    result &= require_condition(flags_match(&old, &now, true, true, false), "global_clear_full");
    result &= check_transition(&old, &now, true, "global_clear", false);
    now = old; now.width = 64;
    result &= require_condition(flags_match(&old, &now, true, true, false), "target_change_full");
    result &= check_transition(&old, &now, true, "logical_target_change", false);
    old = now;
    result &= require_condition(flags_match(&old, &now, true, true, false), "outside_fixed_envelope_full");
    old.width = 0; now = initial_frame();
    result &= require_condition(flags_match(&old, &now, false, true, false), "invalid_old_no_history_full");
    result &= require_condition(flags_match(&old, &now, true, true, false), "invalid_old_full");
    old = initial_frame(); now = old; now.count = BUSTER_COMMANDS + 1;
    result &= require_condition(flags_match(&old, &now, true, true, true), "capacity_rejected");
    result &= require_condition(rejected_replay_untouched(&old, &now), "rejected_replay_untouched");
    now = old; now.count = 2; now.commands[0] = rectangle(0, (Rect){0, 0, 8, 8}, red); now.commands[1] = now.commands[0];
    result &= require_condition(flags_match(&old, &now, true, true, true), "duplicate_id_rejected");
    now = old; now.count = 1; now.commands[0] = rectangle(0, (Rect){-257, 0, 8, 8}, red);
    result &= require_condition(flags_match(&old, &now, true, true, true), "coordinate_rejected");
    now.commands[0].rect = (Rect){8, 0, 0, 8};
    result &= require_condition(flags_match(&old, &now, true, true, true), "reversed_bounds_rejected");
    now = old; now.count = 1; now.commands[0] = rectangle(BUSTER_COMMANDS, (Rect){0, 0, 8, 8}, red);
    result &= require_condition(flags_match(&old, &now, true, true, true), "id_range_rejected");
    now.commands[0].id = 0; now.commands[0].textured = 1; now.commands[0].texture = BUSTER_TEXTURES;
    result &= require_condition(flags_match(&old, &now, true, true, true), "texture_range_rejected");
    now.commands[0].texture = 0; now.commands[0].generation = 2;
    result &= require_condition(flags_match(&old, &now, true, true, true), "generation_mismatch_rejected");
    now = old; now.clear.a = 128;
    result &= require_condition(flags_match(&old, &now, true, true, true), "nonopaque_clear_rejected");
    result &= require_condition(mutation_witnesses(), "mutation_sensitivity");
    printf("focused_fixtures,%s\n", result ? "PASS" : "FAIL");
    return result;
}
static uint32_t random_next(uint32_t *state)
{
    uint32_t value = *state;
    value ^= value << 13; value ^= value >> 17; value ^= value << 5;
    *state = value;
    return value;
}
static Rect random_rectangle(uint32_t *state)
{
    int x = (int)(random_next(state) % 160) - 32;
    int y = (int)(random_next(state) % 128) - 32;
    Rect result = {x, y, x + (int)(random_next(state) % 65), y + (int)(random_next(state) % 65)};
    return result;
}
static Pixel random_pixel(uint32_t *state)
{
    uint32_t value = random_next(state);
    Pixel result = {(uint8_t)value, (uint8_t)(value >> 8), (uint8_t)(value >> 16), (uint8_t)(value >> 24)};
    return result;
}
static bool check_random(void)
{
    uint32_t state = UINT32_C(0xb057e2d1);
    Frame old = initial_frame();
    bool result = true;
    for (int iteration = 0; result && iteration < BUSTER_RANDOM_TRANSITIONS; iteration += 1)
    {
        Frame now = old;
        unsigned operation = random_next(&state) % 9;
        unsigned index = now.count ? random_next(&state) % now.count : 0;
        if ((operation == 0 || !now.count) && now.count < BUSTER_COMMANDS)
        {
            uint8_t map[BUSTER_COMMANDS]; id_map(&now, map);
            unsigned id = 0;
            while (id < BUSTER_COMMANDS && map[id] != 255) { id += 1; }
            now.commands[now.count] = rectangle(id, random_rectangle(&state), random_pixel(&state));
            now.count += 1;
        }
        else if (operation == 1 && now.count)
        {
            for (unsigned j = index; j + 1 < now.count; j += 1) { now.commands[j] = now.commands[j + 1]; }
            now.count -= 1;
        }
        else if (operation == 2) { now.commands[index].rect = random_rectangle(&state); }
        else if (operation == 3) { now.commands[index].clip = random_rectangle(&state); }
        else if (operation == 4) { now.commands[index].color = random_pixel(&state); }
        else if (operation == 5)
        {
            unsigned other = random_next(&state) % now.count;
            Command temporary = now.commands[index]; now.commands[index] = now.commands[other]; now.commands[other] = temporary;
        }
        else if (operation == 6)
        {
            Command *command = &now.commands[index];
            command->textured = 1; command->texture = (uint8_t)(random_next(&state) % BUSTER_TEXTURES);
            command->generation = now.textures[command->texture].generation;
        }
        else if (operation == 7)
        {
            unsigned texture = random_next(&state) % BUSTER_TEXTURES;
            now.textures[texture].generation += 1;
            for (int p = 0; p < BUSTER_TEXELS; p += 1) { now.textures[texture].coverage[p] = (uint8_t)random_next(&state); }
            for (unsigned j = 0; j < now.count; j += 1)
            {
                if (now.commands[j].textured && now.commands[j].texture == texture) { now.commands[j].generation = now.textures[texture].generation; }
            }
        }
        else { now.commands[index].color.a = 0; }
        result &= check_transition(&old, &now, iteration % 97 != 0, "fixed_seed_random", false);
        old = now;
    }
    printf("random_transitions,%d,seed=0xb057e2d1,%s\n", BUSTER_RANDOM_TRANSITIONS, result ? "PASS" : "FAIL");
    return result;
}
static const char *workload_name(int index)
{
    static const char *names[6] = {"unchanged", "sparse", "dense", "fragmented", "opaque_hidden", "glyph_epoch"};
    const char *result = names[index];
    return result;
}
static void make_workload(int index, Frame *old, Frame *now)
{
    *old = initial_frame();
    old->count = BUSTER_COMMANDS;
    for (unsigned i = 0; i < old->count; i += 1)
    {
        int x = (int)(i % 8) * 16, y = (int)(i / 8) * 12;
        old->commands[i] = rectangle(i, (Rect){x + 2, y + 2, x + 10, y + 10},
            (Pixel){(uint8_t)(i * 7), (uint8_t)(i * 17), (uint8_t)(i * 29), 160});
    }
    if (index == 2)
    {
        old->count = 48;
        for (unsigned i = 0; i < old->count; i += 1) { old->commands[i].rect = (Rect){0, 0, BUSTER_WIDTH, BUSTER_HEIGHT}; }
    }
    else if (index == 4)
    {
        old->count = 2;
        old->commands[0] = rectangle(0, (Rect){0, 0, BUSTER_WIDTH, BUSTER_HEIGHT}, (Pixel){255, 0, 0, 255});
        old->commands[1] = rectangle(1, (Rect){0, 0, BUSTER_WIDTH, BUSTER_HEIGHT}, (Pixel){31, 63, 95, 255});
    }
    else if (index == 5)
    {
        for (unsigned i = 0; i < old->count; i += 1)
        {
            old->commands[i].textured = 1; old->commands[i].texture = 0; old->commands[i].generation = 1;
        }
    }
    *now = *old;
    if (index == 1) { now->commands[27].color.r ^= 255; }
    else if (index == 2)
    {
        for (unsigned i = 0; i < now->count; i += 1) { now->commands[i].color.r ^= 255; now->commands[i].color.g ^= 255; }
    }
    else if (index == 3)
    {
        for (unsigned i = 0; i < now->count; i += 8) { now->commands[i].color.r ^= 255; }
    }
    else if (index == 4) { now->commands[0].color = (Pixel){0, 255, 0, 255}; }
    else if (index == 5)
    {
        now->textures[0].generation = 2;
        for (int p = 0; p < BUSTER_TEXELS; p += 1) { now->textures[0].coverage[p] ^= 255; }
        for (unsigned i = 0; i < now->count; i += 1) { now->commands[i].generation = 2; }
    }
}
