// First-party generated P6 pixels and filesystem fixtures. No external assets.
// These checks exercise real file descriptors, one worker and move ownership;
// they do not establish native rendering or a decoder preemption deadline.
#include <buster/tests/image_browser_linux_test.h>
#if BUSTER_INCLUDE_TESTS
#include <buster/lib/system_headers.h>
#include <buster/lib/os_internal.h>
#include <buster/lib/string.h>
#include <buster/apps/image_browser/image_browser_linux.h>
#include <buster/apps/image_browser/image_browser_linux_internal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

BUSTER_GLOBAL_LOCAL u32 image_browser_linux_assertions;
BUSTER_GLOBAL_LOCAL u32 image_browser_linux_failures;

BUSTER_GLOBAL_LOCAL void image_browser_linux_check(bool condition, char const* description)
{
    image_browser_linux_assertions += 1;
    if (!condition)
    {
        image_browser_linux_failures += 1;
        fprintf(stderr, "FAIL: image_browser_linux: %s\n", description);
    }
}

BUSTER_GLOBAL_LOCAL String8 image_browser_linux_path(char const* path)
{
    return (String8){.pointer = (char8*)path, .length = (u64)strlen(path)};
}

BUSTER_GLOBAL_LOCAL bool image_browser_linux_fixture_path(char* output, size_t capacity, char const* directory, char const* name)
{
    int length = snprintf(output, capacity, "%s/%s", directory, name);
    bool result = length >= 0 && (size_t)length < capacity;
    return result;
}

BUSTER_GLOBAL_LOCAL bool image_browser_linux_write_fixture(char const* path, u8 const* bytes, size_t count)
{
    int descriptor = open(path, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
    bool result = descriptor >= 0;
    size_t used = 0;
    while (result && used < count)
    {
        ssize_t transferred = write(descriptor, bytes + used, count - used);
        if (transferred > 0)
        {
            used += (size_t)transferred;
        }
        else if (transferred < 0 && errno == EINTR)
        {
        }
        else
        {
            result = false;
        }
    }
    if (descriptor >= 0)
    {
        result = close(descriptor) == 0 && result;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool image_browser_linux_sparse_fixture(char const* path)
{
    int descriptor = open(path, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
    bool result = descriptor >= 0;
    if (result)
    {
        result = ftruncate(descriptor, (off_t)(IMAGE_BROWSER_MAX_ENCODED_BYTES + 1u)) == 0;
    }
    if (descriptor >= 0)
    {
        result = close(descriptor) == 0 && result;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL u64 image_browser_linux_test_clock(bool* valid)
{
    struct timespec time = {0};
    *valid = clock_gettime(CLOCK_MONOTONIC, &time) == 0 && time.tv_sec >= 0 &&
             time.tv_nsec >= 0 && time.tv_nsec < 1000000000L;
    *valid = *valid && (u64)time.tv_sec <= (UINT64_MAX - 999u) / 1000u;
    u64 result = *valid ? (u64)time.tv_sec * 1000u + (u64)time.tv_nsec / 1000000u : 0;
    return result;
}

BUSTER_GLOBAL_LOCAL bool image_browser_linux_wait(ImageBrowserWorker* worker, ImageBrowserResult* result, bool take)
{
    bool clock_valid = false;
    u64 start = image_browser_linux_test_clock(&clock_valid);
    bool ready = false;
    bool waiting = clock_valid;
    while (waiting && !ready)
    {
        if (take)
        {
            ready = image_browser_worker_take(worker, result);
        }
        else
        {
            int locked = pthread_mutex_lock(&worker->mutex);
            if (locked == 0)
            {
                ready = worker->has_result;
                waiting = pthread_mutex_unlock(&worker->mutex) == 0;
            }
            else
            {
                waiting = false;
            }
        }
        u64 now = image_browser_linux_test_clock(&clock_valid);
        waiting = waiting && clock_valid && now >= start && now - start < 10000u;
        if (waiting && !ready)
        {
            struct timespec delay = {.tv_sec = 0, .tv_nsec = 1000000};
            waiting = nanosleep(&delay, 0) == 0 || errno == EINTR;
        }
    }
    return ready;
}

BUSTER_GLOBAL_LOCAL bool image_browser_linux_pixels(ImageBrowserResult const* result, u32 width, u32 height, u8 const* expected, u64 count)
{
    Image const* image = &result->decoded.image;
    Arena* arena = result->output_arena;
    u64 address = (u64)(uintptr_t)image->pixels.pointer;
    u64 base = (u64)(uintptr_t)arena;
    bool owned = arena && image->pixels.pointer && address >= base &&
                 address - base >= arena_minimum_position && address - base <= arena->position &&
                 count <= arena->position - (address - base);
    bool valid = result->status == IMAGE_BROWSER_LOAD_SUCCESS && result->decoded.status == IMAGE_DECODE_SUCCESS &&
                 image->width == width && image->height == height && image->stride == width * 4u &&
                 image->pixels.length == count && owned;
    if (valid)
    {
        valid = memcmp(image->pixels.pointer, expected, (size_t)count) == 0;
    }
    return valid;
}

BUSTER_GLOBAL_LOCAL bool image_browser_linux_start_request(ImageBrowserWorker* worker, ImageBrowserState* state,
                                                          u64 index, String8 path, ImageBrowserRequest* request)
{
    bool result = image_browser_request(state, index) && image_browser_begin_load(state, request);
    if (result)
    {
        image_browser_worker_set_generation(worker, request->generation);
        result = image_browser_worker_submit(worker, *request, path);
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool image_browser_linux_complete_request(ImageBrowserWorker* worker, ImageBrowserState* state,
                                                             ImageBrowserResult* completed)
{
    bool result = image_browser_linux_wait(worker, completed, true);
    image_browser_linux_check(result, "worker result arrives within bounded fixture wait");
    if (result)
    {
        result = image_browser_complete(state, completed);
        image_browser_linux_check(result && !completed->output_arena, "completion moves sole output ownership");
    }
    return result;
}

BUSTER_GLOBAL_LOCAL void image_browser_linux_worker_checks(ImageBrowserCatalog* catalog, char const* sparse,
                                                          char const* fifo, char const* link, char const* first_file,
                                                          char const* second_file)
{
    static u8 const first_rgba[] = {
        255, 0, 0, 255, 0, 255, 0, 255,
        0, 0, 255, 255, 255, 255, 255, 255,
    };
    static u8 const second_rgba[] = {255, 255, 0, 255};
    ImageBrowserState state;
    image_browser_state_initialize(&state, 128, 96);
    ImageBrowserWorker worker = {0};
    s32 error = 0;
    bool started = image_browser_worker_start(&worker, &error);
    image_browser_linux_check(started && error == 0, "persistent real OS worker starts");
    if (started)
    {
        ImageBrowserRequest request = {0};
        ImageBrowserResult completed = {0};
        bool progressing = image_browser_linux_start_request(&worker, &state, 0, catalog->paths.pointer[0], &request);
        image_browser_linux_check(progressing, "first immutable catalog path submitted");
        if (progressing)
        {
            progressing = image_browser_linux_complete_request(&worker, &state, &completed);
        }
        if (progressing)
        {
            image_browser_linux_check(image_browser_publish(&state), "current first image published");
            image_browser_linux_check(image_browser_linux_pixels(&state.published, 2, 2, first_rgba, sizeof(first_rgba)),
                                      "decoded first P6 owns canonical RGBA pixels");
            ImageBrowserResult absent = {0};
            image_browser_linux_check(!image_browser_worker_take(&worker, &absent), "moved worker slot cannot yield a second result");
            image_browser_result_release(&absent);
            progressing = image_browser_linux_start_request(&worker, &state, 0, catalog->paths.pointer[0], &request);
            image_browser_linux_check(progressing, "outstanding generation submitted");
        }
        if (progressing)
        {
            // The earlier job may be queued, reading, decoding or complete.
            // Supersession must be correct for all of these scheduling outcomes.
            progressing = image_browser_request(&state, 1);
            image_browser_worker_set_generation(&worker, state.generation);
            image_browser_linux_check(progressing, "new generation supersedes the outstanding request");
            progressing = progressing && image_browser_linux_complete_request(&worker, &state, &completed);
        }
        if (progressing)
        {
            image_browser_linux_check(state.completed.request.generation == request.generation &&
                                      (state.completed.status == IMAGE_BROWSER_LOAD_SUCCESS ||
                                       state.completed.status == IMAGE_BROWSER_LOAD_CANCELLED),
                                      "superseded result retains its identity and honest status");
            image_browser_linux_check(!image_browser_publish(&state) && !state.has_completed,
                                      "superseded result cannot publish and its owner is consumed");
            image_browser_linux_check(image_browser_linux_pixels(&state.published, 2, 2, first_rgba, sizeof(first_rgba)),
                                      "old output survives another source/scratch lifetime");
            progressing = image_browser_begin_load(&state, &request);
            image_browser_linux_check(progressing, "latest pending generation begins after stale completion");
            if (progressing)
            {
                progressing = image_browser_worker_submit(&worker, request, catalog->paths.pointer[1]);
                image_browser_linux_check(progressing, "latest pending immutable path submitted");
            }
            if (progressing)
            {
                progressing = image_browser_linux_complete_request(&worker, &state, &completed);
            }
        }
        if (progressing)
        {
            image_browser_linux_check(image_browser_publish(&state), "latest generation publishes");
            image_browser_linux_check(image_browser_linux_pixels(&state.published, 1, 1, second_rgba, sizeof(second_rgba)),
                                      "latest generation has independent expected pixels");
            char const* rejected_paths[] = {sparse, fifo, link};
            ImageBrowserLoadStatus statuses[] = {IMAGE_BROWSER_LOAD_ENCODED_LIMIT, IMAGE_BROWSER_LOAD_READ_ERROR, IMAGE_BROWSER_LOAD_READ_ERROR};
            for (u32 index = 0; progressing && index < 3; index += 1)
            {
                progressing = image_browser_linux_start_request(&worker, &state, 2u + index,
                    image_browser_linux_path(rejected_paths[index]), &request);
                image_browser_linux_check(progressing, "bounded refusal fixture submitted");
                if (progressing)
                {
                    progressing = image_browser_linux_complete_request(&worker, &state, &completed);
                }
                if (progressing)
                {
                    image_browser_linux_check(state.completed.status == statuses[index], "encoded limit or nonregular/link refusal is explicit");
                    image_browser_linux_check(state.completed.status != IMAGE_BROWSER_LOAD_READ_ERROR ||
                                              state.completed.system_error != 0, "file refusal retains system error");
                    image_browser_linux_check(image_browser_publish(&state) &&
                                              state.published.status == statuses[index], "current error replaces old image honestly");
                }
            }
        }

        if (progressing)
        {
            progressing = image_browser_linux_start_request(&worker, &state, 0, catalog->paths.pointer[0], &request);
            image_browser_linux_check(progressing, "final result submitted for retained-join ownership test");
            if (progressing)
            {
                progressing = image_browser_linux_wait(&worker, &completed, false);
                image_browser_linux_check(progressing, "final result remains worker-owned until join");
                if (progressing)
                {
                    image_browser_linux_check(!image_browser_worker_submit(&worker, request, catalog->paths.pointer[0]),
                                              "untaken result blocks a second slot admission");
                }
            }
        }
        image_browser_result_release(&completed);
        image_browser_shutdown(&state);
        ImageBrowserResult joined = {0};
        bool has_joined = false;
        bool stopped = false;
        if (progressing)
        {
            OsThreadHandle* thread = worker.thread;
            pthread_mutex_lock(&worker.mutex);
            Arena* output = worker.result.output_arena;
            u64 generation = worker.result.request.generation;
            pthread_mutex_unlock(&worker.mutex);
            os_resource_test_clear();
            os_resource_test_fail_on_call(OS_RESOURCE_TEST_THREAD_JOIN, 0);
            stopped = image_browser_worker_stop_join(&worker, &joined, &has_joined);
            image_browser_linux_check(!stopped && !has_joined && !joined.output_arena,
                                      "injected join failure does not transfer a result");
            bool resources_retained = !stopped && worker.initialized && worker.thread == thread;
            image_browser_linux_check(resources_retained, "failed join retains worker and thread ownership");
            bool retained = false;
            if (resources_retained && pthread_mutex_lock(&worker.mutex) == 0)
            {
                retained = worker.has_result && worker.result.output_arena == output &&
                           worker.result.request.generation == generation;
                retained = pthread_mutex_unlock(&worker.mutex) == 0 && retained;
            }
            image_browser_linux_check(retained && output != 0, "failed join retains result and working synchronization");
            if (resources_retained)
            {
                image_browser_linux_check(!image_browser_worker_submit(&worker, request, catalog->paths.pointer[0]),
                                          "stopping worker refuses further admission before join retry");
            }
            image_browser_linux_check(!image_browser_state_destroy(&state), "active ownership prevents premature state destruction");
            os_resource_test_clear();
        }
        if (!stopped)
        {
            stopped = image_browser_worker_stop_join(&worker, &joined, &has_joined);
        }
        image_browser_linux_check(stopped && !worker.initialized && !worker.thread, "successful join consumes worker resources");
        if (stopped && has_joined)
        {
            image_browser_catalog_destroy(catalog);
            image_browser_linux_check(unlink(first_file) == 0 && unlink(second_file) == 0,
                                      "source files and borrowed catalog paths removed after join");
            image_browser_linux_check(image_browser_linux_pixels(&joined, 2, 2, first_rgba, sizeof(first_rgba)),
                                      "decoded result survives source/catalog/worker teardown");
            bool delivered = image_browser_complete(&state, &joined);
            image_browser_linux_check(delivered && !joined.output_arena, "joined result delivered to shutdown releases output");
        }
        else if (state.has_active)
        {
            image_browser_linux_check(false, "active load has a joined result");
        }
        image_browser_result_release(&joined);
        image_browser_linux_check(image_browser_state_destroy(&state), "joined shutdown releases serial state ownership");
        image_browser_linux_check(os_is_only_live_thread(), "worker is no longer a live OS thread");
        if (stopped)
        {
            ImageBrowserResult again = {0};
            bool has_again = true;
            image_browser_linux_check(image_browser_worker_stop_join(&worker, &again, &has_again) &&
                                      !has_again && !again.output_arena, "repeated worker shutdown is harmless");
            image_browser_result_release(&again);
        }
    }
}

// Comparison-counting oracle: the former halving-gap Shell sort, kept here so
// the merge sort is checked against the exact order the old code produced.
BUSTER_GLOBAL_LOCAL u64 image_browser_linux_oracle_comparisons;

BUSTER_GLOBAL_LOCAL s32 image_browser_linux_oracle_compare(String8 left, String8 right)
{
    image_browser_linux_oracle_comparisons += 1;
    u64 common = left.length < right.length ? left.length : right.length;
    s32 result = (s32)memcmp(left.pointer, right.pointer, (size_t)common);
    if (!result && left.length != right.length)
    {
        result = left.length < right.length ? -1 : 1;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL void image_browser_linux_oracle_sort(String8* paths, u64 count)
{
    for (u64 gap = count / 2; gap; gap /= 2)
    {
        for (u64 index = gap; index < count; index += 1)
        {
            String8 value = paths[index];
            u64 position = index;
            while (position >= gap && image_browser_linux_oracle_compare(paths[position - gap], value) > 0)
            {
                paths[position] = paths[position - gap];
                position -= gap;
            }
            paths[position] = value;
        }
    }
}

typedef enum ImageBrowserSortShape
{
    IMAGE_BROWSER_SORT_SHAPE_SHELL_WORST,
    IMAGE_BROWSER_SORT_SHAPE_SORTED,
    IMAGE_BROWSER_SORT_SHAPE_REVERSE,
    IMAGE_BROWSER_SORT_SHAPE_RANDOM,
    IMAGE_BROWSER_SORT_SHAPE_EQUAL,
    IMAGE_BROWSER_SORT_SHAPE_COMMON_PREFIX,
    IMAGE_BROWSER_SORT_SHAPE_PREFIX_CHAIN,
    IMAGE_BROWSER_SORT_SHAPE_COUNT,
} ImageBrowserSortShape;

BUSTER_GLOBAL_LOCAL u32 image_browser_linux_sort_rank(ImageBrowserSortShape shape, u64 count, u64 index, u32* state)
{
    u32 result = (u32)index;
    if (shape == IMAGE_BROWSER_SORT_SHAPE_SHELL_WORST)
    {
        // For count = 2m: m, 0, m+1, 1, ..., 2m-1, m-1. Every halving-gap pass
        // before the last leaves the final pass m(m+1)/2 shifts.
        u64 m = count / 2;
        result = (u32)((index & 1) ? index / 2 : m + index / 2);
    }
    else if (shape == IMAGE_BROWSER_SORT_SHAPE_REVERSE)
    {
        result = (u32)(count - 1 - index);
    }
    else if (shape == IMAGE_BROWSER_SORT_SHAPE_RANDOM || shape == IMAGE_BROWSER_SORT_SHAPE_PREFIX_CHAIN)
    {
        *state ^= *state << 13;
        *state ^= *state >> 17;
        *state ^= *state << 5;
        result = *state % (u32)(count * 4);
    }
    else if (shape == IMAGE_BROWSER_SORT_SHAPE_EQUAL)
    {
        result = 7;
    }
    return result;
}

// Builds `count` paths of the given shape into one malloc'd pool.
BUSTER_GLOBAL_LOCAL bool image_browser_linux_sort_paths_build(ImageBrowserSortShape shape, u64 count, String8** paths, char** pool)
{
    u64 prefix_length = shape == IMAGE_BROWSER_SORT_SHAPE_COMMON_PREFIX ? 300 : 3;
    u64 stride = prefix_length + 16 + (shape == IMAGE_BROWSER_SORT_SHAPE_PREFIX_CHAIN ? count : 0);
    *paths = (String8*)malloc((size_t)(count * sizeof(String8)));
    *pool = (char*)malloc((size_t)(count * stride));
    bool result = *paths && *pool;
    u32 state = 0x9e3779b9u ^ (u32)count;
    for (u64 index = 0; result && index < count; index += 1)
    {
        char* text = *pool + index * stride;
        u32 rank = image_browser_linux_sort_rank(shape, count, index, &state);
        u64 length = 0;
        if (shape == IMAGE_BROWSER_SORT_SHAPE_PREFIX_CHAIN)
        {
            // Every path is a prefix of every longer one.
            length = 1 + rank % count;
            memset(text, 'a', (size_t)length);
        }
        else
        {
            memset(text, shape == IMAGE_BROWSER_SORT_SHAPE_COMMON_PREFIX ? 'p' : 'd', (size_t)prefix_length);
            int digits = snprintf(text + prefix_length, 16, "%08u", (unsigned)rank);
            result = digits == 8;
            length = prefix_length + 8;
        }
        (*paths)[index] = (String8){.pointer = (char8*)text, .length = length};
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool image_browser_linux_sort_matches(String8 const* left, String8 const* right, u64 count)
{
    bool result = true;
    for (u64 index = 0; result && index < count; index += 1)
    {
        result = left[index].length == right[index].length &&
                 memcmp(left[index].pointer, right[index].pointer, (size_t)left[index].length) == 0;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL u64 image_browser_linux_sort_log2_ceiling(u64 count)
{
    u64 result = 0;
    while (((u64)1 << result) < count)
    {
        result += 1;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL void image_browser_linux_sort_checks(void)
{
    // Merge sort must match the old sort's final order and stay within
    // count * ceil(log2 count) comparisons for every shape and size.
    u64 const sizes[] = {0, 1, 2, 3, 5, 100, 1024, 2048, 4095, 4096};
    u64 shell_worst_oracle = 0;
    u64 shell_worst_merge = 0;
    for (u32 shape_index = 0; shape_index < IMAGE_BROWSER_SORT_SHAPE_COUNT; shape_index += 1)
    {
        for (u32 size_index = 0; size_index < BUSTER_ARRAY_LENGTH(sizes); size_index += 1)
        {
            u64 count = sizes[size_index];
            ImageBrowserSortShape shape = (ImageBrowserSortShape)shape_index;
            if (shape == IMAGE_BROWSER_SORT_SHAPE_SHELL_WORST && (count & 1))
            {
                continue;
            }
            String8* actual = 0;
            char* actual_pool = 0;
            String8* expected = 0;
            char* expected_pool = 0;
            String8* scratch = (String8*)malloc((size_t)((count ? count : 1) * sizeof(String8)));
            bool built = image_browser_linux_sort_paths_build(shape, count, &actual, &actual_pool) &&
                         image_browser_linux_sort_paths_build(shape, count, &expected, &expected_pool) && scratch;
            image_browser_linux_check(built, "sort fixture built");
            if (built)
            {
                image_browser_linux_oracle_comparisons = 0;
                image_browser_linux_oracle_sort(expected, count);
                u64 comparisons = image_browser_linux_test_sort(actual, scratch, count);
                image_browser_linux_check(image_browser_linux_sort_matches(actual, expected, count),
                                          "merge sort final order matches the old sort");
                bool ordered = true;
                for (u64 index = 1; ordered && index < count; index += 1)
                {
                    u64 common = actual[index - 1].length < actual[index].length ? actual[index - 1].length : actual[index].length;
                    int order = memcmp(actual[index - 1].pointer, actual[index].pointer, (size_t)common);
                    ordered = order < 0 || (order == 0 && actual[index - 1].length <= actual[index].length);
                }
                image_browser_linux_check(ordered, "merge sort output is bytewise lexical order");
                image_browser_linux_check(comparisons <= count * image_browser_linux_sort_log2_ceiling(count),
                                          "merge sort comparisons stay within count * ceil(log2 count)");
                if (shape == IMAGE_BROWSER_SORT_SHAPE_SHELL_WORST && count == 4096)
                {
                    shell_worst_oracle = image_browser_linux_oracle_comparisons;
                    shell_worst_merge = comparisons;
                }
            }
            free(actual);
            free(actual_pool);
            free(expected);
            free(expected_pool);
            free(scratch);
        }
    }
    // The constructed family drives the old sort to m(m+1)/2 shifts in its
    // last pass (2,098,176 at 4096); the merge sort stays near n log n.
    image_browser_linux_check(shell_worst_oracle > 2000000, "constructed family is quadratic for the halving-gap sort");
    image_browser_linux_check(shell_worst_merge && shell_worst_merge <= 4096u * 12u, "merge sort is bounded on the constructed family");
    fprintf(stderr, "image_browser_linux_sort: 4096-path Shell-worst family: old sort %llu comparisons, merge sort %llu\n",
            (unsigned long long)shell_worst_oracle, (unsigned long long)shell_worst_merge);
}

bool image_browser_run_linux_tests(void)
{
    image_browser_linux_assertions = 0;
    image_browser_linux_failures = 0;
    char directory[] = "/tmp/buster-image-browser-tests-XXXXXX";
    char first[256] = {0};
    char second[256] = {0};
    char sparse[256] = {0};
    char fifo[256] = {0};
    char link[256] = {0};
    bool created = mkdtemp(directory) != 0;
    image_browser_linux_check(created, "exclusive synthetic fixture directory created");
    if (created)
    {
        bool paths = image_browser_linux_fixture_path(first, sizeof(first), directory, "alpha.ppm") &&
                     image_browser_linux_fixture_path(second, sizeof(second), directory, "beta.ppm") &&
                     image_browser_linux_fixture_path(sparse, sizeof(sparse), directory, "limit.ppm") &&
                     image_browser_linux_fixture_path(fifo, sizeof(fifo), directory, "pipe.ppm") &&
                     image_browser_linux_fixture_path(link, sizeof(link), directory, "link.ppm");
        image_browser_linux_check(paths, "fixture paths fit bounded storage");
        static u8 const first_p6[] = {
            'P', '6', '\n', '2', ' ', '2', '\n', '2', '5', '5', '\n',
            255, 0, 0, 0, 255, 0, 0, 0, 255, 255, 255, 255,
        };
        static u8 const second_p6[] = {'P', '6', '\n', '1', ' ', '1', '\n', '2', '5', '5', '\n', 255, 255, 0};
        bool fixtures = paths && image_browser_linux_write_fixture(first, first_p6, sizeof(first_p6)) &&
                        image_browser_linux_write_fixture(second, second_p6, sizeof(second_p6)) &&
                        mkfifo(fifo, 0600) == 0 && symlink("alpha.ppm", link) == 0;
        image_browser_linux_check(fixtures, "first-party P6, FIFO and link fixtures created");
        if (fixtures)
        {
            ImageBrowserCatalog catalog = {0};
            s32 error = 0;
            bool opened = image_browser_catalog_open(image_browser_linux_path(directory), &catalog, &error);
            image_browser_linux_check(opened && error == 0 && catalog.paths.length == 2,
                                      "flat catalog excludes FIFO and symlink entries");
            bool usable = opened && catalog.arena && catalog.paths.pointer && catalog.paths.length == 2;
            if (usable)
            {
                image_browser_linux_check(string_equal(catalog.paths.pointer[0], image_browser_linux_path(first)) &&
                                          string_equal(catalog.paths.pointer[1], image_browser_linux_path(second)),
                                          "immutable catalog has deterministic path order");
                ImageBrowserCatalog image_catalog = {0};
                bool selected = image_browser_catalog_open(image_browser_linux_path(second), &image_catalog, &error);
                image_browser_linux_check(selected && image_catalog.paths.length == 2 && image_catalog.initial_index == 1,
                                          "opening an image selects it within the directory snapshot");
                image_browser_catalog_destroy(&image_catalog);
                char const* denied[] = {fifo, link};
                for (u32 index = 0; index < 2; index += 1)
                {
                    ImageBrowserCatalog rejected = {0};
                    bool accepted = image_browser_catalog_open(image_browser_linux_path(denied[index]), &rejected, &error);
                    image_browser_linux_check(!accepted && error != 0 && !rejected.arena, "direct FIFO/link input is refused");
                    image_browser_catalog_destroy(&rejected);
                }
                bool limited = image_browser_linux_sparse_fixture(sparse);
                image_browser_linux_check(limited, "sparse encoded-limit fixture created without bulk asset bytes");
                image_browser_linux_check(catalog.paths.length == 2, "catalog snapshot stays immutable after new file creation");
                if (limited)
                {
                    image_browser_linux_worker_checks(&catalog, sparse, fifo, link, first, second);
                }
            }
            image_browser_catalog_destroy(&catalog);
            image_browser_linux_check(!catalog.arena && !catalog.paths.pointer && !catalog.paths.length,
                                      "catalog destruction clears ownership");
        }
        char const* paths_to_remove[] = {first, second, sparse, fifo, link};
        for (u32 index = 0; index < 5; index += 1)
        {
            if (paths_to_remove[index][0])
            {
                int removed = unlink(paths_to_remove[index]);
                image_browser_linux_check(removed == 0 || errno == ENOENT, "owned fixture removed");
            }
        }
        image_browser_linux_check(rmdir(directory) == 0, "owned fixture directory removed");
    }
    os_resource_test_clear();
    image_browser_linux_sort_checks();
    fprintf(stderr, "image_browser_linux_tests: %u/%u assertions passed\n",
            (unsigned)(image_browser_linux_assertions - image_browser_linux_failures), (unsigned)image_browser_linux_assertions);
    return image_browser_linux_failures == 0;
}
#endif
