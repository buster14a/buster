/* Hosted CPU diagnostic only. Raw fixed-count blocks; oracle/reset outside timing.
 * Same replay kernels and immutable binary for full/command/tile strategies.
 * Snapshot updates are timed for incremental strategies. No GPU frame inference.
 */
static volatile uint64_t timing_sink;
extern uint64_t observe_bytes(const void *pointer, size_t count);
extern void publish_snapshot(const void *pointer, size_t count);
static uint64_t wall_nanoseconds(void)
{
    struct timespec ts;
    (void)timespec_get(&ts, TIME_UTC);
    uint64_t result = (uint64_t)ts.tv_sec * UINT64_C(1000000000) + (uint64_t)ts.tv_nsec;
    return result;
}
static uint64_t pixel_checksum(const Pixel *pixels)
{
    uint64_t result = UINT64_C(1469598103934665603);
    for (int p = 0; p < BUSTER_PIXELS; p += 1)
    {
        result = (result ^ pixels[p].r) * UINT64_C(1099511628211);
        result = (result ^ pixels[p].g) * UINT64_C(1099511628211);
        result = (result ^ pixels[p].b) * UINT64_C(1099511628211);
        result = (result ^ pixels[p].a) * UINT64_C(1099511628211);
    }
    return result;
}
static bool benchmark_models(void)
{
    static Pixel previous[BUSTER_PIXELS], expected[BUSTER_PIXELS], output[BUSTER_PIXELS];
    Frame old, now, retained;
    Lists previous_lists, current_lists, retained_lists;
    Plan plan;
    bool result = true;
    printf("timing,workload,block,strategy,order,repeats,plan_us,replay_us,total_us\n");
    for (int workload = 0; workload < 6; workload += 1)
    {
        make_workload(workload, &old, &now);
        oracle_render(&old, previous);
        oracle_render(&now, expected);
        Metrics ignored = {0};
        build_lists(&old, &previous_lists, &ignored);
        for (int block = 0; block < BUSTER_TIMING_BLOCKS; block += 1)
        {
            for (int order = 0; order < 3; order += 1)
            {
                int strategy = (block + order) % 3;
                double planning = 0.0, rendering = 0.0;
                for (int repeat = 0; repeat < BUSTER_TIMING_REPEATS; repeat += 1)
                {
                    /* Reset the synthetic pair outside timing; real history persists. */
                    memcpy(output, previous, sizeof(output));
                    retained = old;
                    retained_lists = previous_lists;
                    uint64_t start = wall_nanoseconds();
                    if (strategy == 0) { full_plan(&plan); }
                    else if (strategy == 1)
                    {
                        command_plan(&retained, &now, true, &plan);
                        retained = now;
                        publish_snapshot(&retained, sizeof(retained));
                    }
                    else
                    {
                        tile_plan(&retained, &now, &retained_lists, true, &plan, &current_lists);
                        retained = now;
                        retained_lists = current_lists;
                        publish_snapshot(&retained, sizeof(retained));
                        publish_snapshot(&retained_lists, sizeof(retained_lists));
                    }
                    uint64_t planned = wall_nanoseconds();
                    if (strategy == 2) { replay_tiles(&now, &current_lists, &plan, output); }
                    else { replay(&now, &plan, output); }
                    uint64_t rendered = wall_nanoseconds();
                    if (planned < start || rendered < planned) { result = false; }
                    else
                    {
                        planning += (double)(planned - start) / 1000000000.0;
                        rendering += (double)(rendered - planned) / 1000000000.0;
                    }
                    /* Full output is observed after the timed region; no dead stores. */
                    timing_sink ^= pixel_checksum(output);
                    timing_sink ^= observe_bytes(output, sizeof(output));
                    if (strategy > 0) { timing_sink ^= observe_bytes(&retained, sizeof(retained)); }
                    if (strategy == 2) { timing_sink ^= observe_bytes(&retained_lists, sizeof(retained_lists)); }
                }
                if (changed_count(output, expected) || plan.rejected)
                {
                    printf("FAIL,timing-output,%s,strategy=%d\n", workload_name(workload), strategy);
                    result = false;
                }
                printf("timing,%s,%d,%s,%d,%d,%.6f,%.6f,%.6f\n",
                    workload_name(workload), block, strategy == 0 ? "full" : strategy == 1 ? "command" : "tile",
                    order, BUSTER_TIMING_REPEATS,
                    planning * 1000000.0 / BUSTER_TIMING_REPEATS,
                    rendering * 1000000.0 / BUSTER_TIMING_REPEATS,
                    (planning + rendering) * 1000000.0 / BUSTER_TIMING_REPEATS);
            }
        }
    }
    printf("timing_sink,%llu\n", (unsigned long long)timing_sink);
    return result;
}
