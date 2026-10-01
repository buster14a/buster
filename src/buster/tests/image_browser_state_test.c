// Standalone app-state/ownership tests. Native rendering has a separate gate.
// Real no_pool arenas and the public decoder exercise publication lifetime;
// request scheduling tests model only serialized owner-thread transitions.
// The P6 and SOF2 bytes below are project-created grammar fixtures. No external
// image asset, generator dependency or corpus is used.

#include <buster/tests/image_browser_state_test.h>
#if BUSTER_INCLUDE_TESTS
#include <buster/apps/image_browser/image_browser_state.h>
#include <stdio.h>

#define IMAGE_BROWSER_TEST(expression) \
    do \
    { \
        if (!(expression)) \
        { \
            fprintf(stderr, "image_browser_state_test:%d: %s\n", __LINE__, #expression); \
            failures += 1; \
        } \
    } while (0)

BUSTER_GLOBAL_LOCAL Arena* image_browser_test_arena(void)
{
    Arena* result = arena_create((ArenaCreation){
        .reserved_size = BUSTER_KB(64),
        .initial_size = BUSTER_KB(64),
        .flags = {.no_pool = 1},
    });
    return result;
}

BUSTER_GLOBAL_LOCAL ImageBrowserResult image_browser_test_decode(ImageBrowserRequest request)
{
    static u8 const fixture[] = {'P', '6', '\n', '2', ' ', '1', '\n', '2', '5', '5', '\n', 255, 0, 0, 0, 255, 0};
    Arena* source = image_browser_test_arena();
    Arena* output = image_browser_test_arena();
    Arena* scratch = image_browser_test_arena();
    u8* bytes = arena_allocate(source, u8, sizeof(fixture));
    memcpy(bytes, fixture, sizeof(fixture));
    ImageBrowserResult result = image_browser_decode(request, (ByteSlice){.pointer = bytes, .length = sizeof(fixture)},
                                                    output, scratch, IMAGE_FORMAT_UNKNOWN);
    // The source is deliberately overwritten and unmapped before any consumer
    // examines decoded pixels; ASan builds catch a retained encoded pointer.
    memset(bytes, 0, sizeof(fixture));
    arena_destroy(source, 1);
    arena_destroy(scratch, 1);
    return result;
}

BUSTER_GLOBAL_LOCAL bool image_browser_test_near(f64 actual, f64 expected)
{
    bool result = actual >= expected - 0.000000001 && actual <= expected + 0.000000001;
    return result;
}

BUSTER_GLOBAL_LOCAL s32 image_browser_test_transitions(void)
{
    s32 failures = 0;
    ImageBrowserState state;
    image_browser_state_initialize(&state, 4, 4);
    ImageBrowserRequest first = {0};
    ImageBrowserRequest latest = {0};
    IMAGE_BROWSER_TEST(!image_browser_begin_load(&state, &first));
    IMAGE_BROWSER_TEST(image_browser_request(&state, 0));
    IMAGE_BROWSER_TEST(image_browser_begin_load(&state, &first));
    IMAGE_BROWSER_TEST(first.generation == 1 && first.file_index == 0);
    IMAGE_BROWSER_TEST(image_browser_request(&state, 1));
    IMAGE_BROWSER_TEST(image_browser_request(&state, 2));
    IMAGE_BROWSER_TEST(state.has_active && state.has_pending);
    IMAGE_BROWSER_TEST(!image_browser_begin_load(&state, &latest));

    ImageBrowserResult stale = image_browser_test_decode(first);
    IMAGE_BROWSER_TEST(stale.status == IMAGE_BROWSER_LOAD_SUCCESS);
    IMAGE_BROWSER_TEST(image_browser_complete(&state, &stale));
    IMAGE_BROWSER_TEST(!stale.output_arena && !state.has_active && state.has_completed);
    IMAGE_BROWSER_TEST(!image_browser_begin_load(&state, &latest));
    IMAGE_BROWSER_TEST(!image_browser_publish(&state));
    IMAGE_BROWSER_TEST(!state.has_completed && !state.completed.output_arena && !state.has_published);
    IMAGE_BROWSER_TEST(image_browser_begin_load(&state, &latest));
    IMAGE_BROWSER_TEST(latest.generation == 3 && latest.file_index == 2);

    ImageBrowserResult orphan = image_browser_test_decode(latest);
    orphan.request.generation += 1;
    Arena* orphan_arena = orphan.output_arena;
    IMAGE_BROWSER_TEST(!image_browser_complete(&state, &orphan));
    IMAGE_BROWSER_TEST(orphan.output_arena == orphan_arena && state.has_active && !state.has_completed);
    image_browser_result_release(&orphan);
    IMAGE_BROWSER_TEST(!orphan.output_arena);

    ImageBrowserResult current = image_browser_test_decode(latest);
    Arena* current_arena = current.output_arena;
    IMAGE_BROWSER_TEST(image_browser_complete(&state, &current));
    IMAGE_BROWSER_TEST(!current.output_arena);
    IMAGE_BROWSER_TEST(image_browser_publish(&state));
    IMAGE_BROWSER_TEST(state.has_published && state.published.output_arena == current_arena);
    IMAGE_BROWSER_TEST(state.published.decoded.image.pixels.length == 8);
    if (state.published.decoded.image.pixels.length == 8)
    {
        u8 const* pixel = state.published.decoded.image.pixels.pointer;
        IMAGE_BROWSER_TEST(pixel[0] == 255 && pixel[1] == 0 && pixel[2] == 0 && pixel[3] == 255);
        IMAGE_BROWSER_TEST(pixel[4] == 0 && pixel[5] == 255 && pixel[6] == 0 && pixel[7] == 255);
    }
    IMAGE_BROWSER_TEST(image_browser_test_near(state.view.scale, 2.0));
    IMAGE_BROWSER_TEST(!image_browser_publish(&state));

    // A superseded error must not replace the currently published image.
    ImageBrowserRequest old_error_request = {0};
    IMAGE_BROWSER_TEST(image_browser_request(&state, 3));
    IMAGE_BROWSER_TEST(image_browser_begin_load(&state, &old_error_request));
    IMAGE_BROWSER_TEST(image_browser_request(&state, 4));
    ImageBrowserResult old_error = {.request = old_error_request, .status = IMAGE_BROWSER_LOAD_READ_ERROR, .system_error = 5};
    IMAGE_BROWSER_TEST(image_browser_complete(&state, &old_error));
    IMAGE_BROWSER_TEST(!image_browser_publish(&state));
    IMAGE_BROWSER_TEST(state.published.output_arena == current_arena);
    IMAGE_BROWSER_TEST(state.published.request.generation == latest.generation);

    // A current error replaces and releases the old image instead of showing
    // pixels under the failed file's metadata.
    ImageBrowserRequest error_request = {0};
    IMAGE_BROWSER_TEST(image_browser_begin_load(&state, &error_request));
    ImageBrowserResult error = {.request = error_request, .status = IMAGE_BROWSER_LOAD_READ_ERROR, .system_error = 13};
    IMAGE_BROWSER_TEST(image_browser_complete(&state, &error));
    IMAGE_BROWSER_TEST(image_browser_publish(&state));
    IMAGE_BROWSER_TEST(state.published.status == IMAGE_BROWSER_LOAD_READ_ERROR && state.published.system_error == 13);
    IMAGE_BROWSER_TEST(!state.published.output_arena && !state.published.decoded.image.pixels.pointer);
    u32 width = 99;
    u32 height = 99;
    image_browser_display_dimensions(&state, &width, &height);
    IMAGE_BROWSER_TEST(width == 0 && height == 0);

    ImageBrowserRequest cancelled_request = {0};
    IMAGE_BROWSER_TEST(image_browser_request(&state, 5));
    IMAGE_BROWSER_TEST(image_browser_begin_load(&state, &cancelled_request));
    ImageBrowserResult cancelled = {.request = cancelled_request, .status = IMAGE_BROWSER_LOAD_CANCELLED};
    IMAGE_BROWSER_TEST(image_browser_complete(&state, &cancelled));
    IMAGE_BROWSER_TEST(image_browser_publish(&state));
    IMAGE_BROWSER_TEST(state.published.status == IMAGE_BROWSER_LOAD_CANCELLED);
    image_browser_display_dimensions(&state, &width, &height);
    IMAGE_BROWSER_TEST(width == 0 && height == 0 && !state.published.output_arena);

    for (u64 iteration = 0; iteration < 64; iteration += 1)
    {
        ImageBrowserRequest request = {0};
        IMAGE_BROWSER_TEST(image_browser_request(&state, iteration % 3));
        IMAGE_BROWSER_TEST(image_browser_begin_load(&state, &request));
        ImageBrowserResult decoded = image_browser_test_decode(request);
        IMAGE_BROWSER_TEST(decoded.status == IMAGE_BROWSER_LOAD_SUCCESS);
        image_browser_pan(&state, 5, -5);
        image_browser_actual_size(&state);
        image_browser_zoom(&state, 2, 0, 0);
        IMAGE_BROWSER_TEST(image_browser_complete(&state, &decoded));
        IMAGE_BROWSER_TEST(image_browser_publish(&state));
        IMAGE_BROWSER_TEST(!state.has_active && !state.has_pending && !state.has_completed);
        IMAGE_BROWSER_TEST(state.published.request.file_index == iteration % 3);
        IMAGE_BROWSER_TEST(image_browser_test_near(state.view.scale, 2.0));
        IMAGE_BROWSER_TEST(state.view.pan_x == 0 && state.view.pan_y == 0);
    }
    IMAGE_BROWSER_TEST(image_browser_state_destroy(&state));
    IMAGE_BROWSER_TEST(!state.published.output_arena && !state.has_published);
    IMAGE_BROWSER_TEST(image_browser_state_destroy(&state));
    return failures;
}

BUSTER_GLOBAL_LOCAL s32 image_browser_test_shutdown(void)
{
    s32 failures = 0;
    ImageBrowserState state;
    image_browser_state_initialize(&state, 8, 8);
    ImageBrowserRequest request = {0};
    IMAGE_BROWSER_TEST(image_browser_request(&state, 0));
    IMAGE_BROWSER_TEST(image_browser_begin_load(&state, &request));
    ImageBrowserResult first = image_browser_test_decode(request);
    IMAGE_BROWSER_TEST(image_browser_complete(&state, &first));
    IMAGE_BROWSER_TEST(image_browser_publish(&state));
    Arena* published_arena = state.published.output_arena;

    IMAGE_BROWSER_TEST(image_browser_request(&state, 1));
    IMAGE_BROWSER_TEST(image_browser_begin_load(&state, &request));
    IMAGE_BROWSER_TEST(image_browser_request(&state, 2));
    IMAGE_BROWSER_TEST(!image_browser_state_destroy(&state));
    IMAGE_BROWSER_TEST(state.shutting_down && !state.has_pending && state.has_active);
    IMAGE_BROWSER_TEST(state.published.output_arena == published_arena);
    IMAGE_BROWSER_TEST(!image_browser_request(&state, 3));
    ImageBrowserRequest prohibited = {0};
    IMAGE_BROWSER_TEST(!image_browser_begin_load(&state, &prohibited));
    ImageBrowserResult joined = image_browser_test_decode(request);
    IMAGE_BROWSER_TEST(image_browser_complete(&state, &joined));
    IMAGE_BROWSER_TEST(!joined.output_arena && !state.has_active && !state.has_completed);
    IMAGE_BROWSER_TEST(!image_browser_publish(&state));
    IMAGE_BROWSER_TEST(image_browser_state_destroy(&state));
    IMAGE_BROWSER_TEST(!state.published.output_arena);

    // Shutdown also consumes a synchronized result that was not yet published.
    image_browser_state_initialize(&state, 8, 8);
    IMAGE_BROWSER_TEST(image_browser_request(&state, 0));
    IMAGE_BROWSER_TEST(image_browser_begin_load(&state, &request));
    ImageBrowserResult completed = image_browser_test_decode(request);
    IMAGE_BROWSER_TEST(image_browser_complete(&state, &completed));
    image_browser_shutdown(&state);
    IMAGE_BROWSER_TEST(!state.completed.output_arena && !state.has_completed);
    IMAGE_BROWSER_TEST(image_browser_state_destroy(&state));

    // Generation exhaustion refuses another request without colliding with an
    // outstanding result's identity.
    image_browser_state_initialize(&state, 8, 8);
    state.generation = UINT64_MAX;
    IMAGE_BROWSER_TEST(!image_browser_request(&state, 0));
    IMAGE_BROWSER_TEST(!state.has_pending && state.generation == UINT64_MAX);
    IMAGE_BROWSER_TEST(image_browser_state_destroy(&state));
    return failures;
}

BUSTER_GLOBAL_LOCAL s32 image_browser_test_decode_policy(void)
{
    s32 failures = 0;
    Arena* output = image_browser_test_arena();
    Arena* scratch = image_browser_test_arena();
    ImageBrowserRequest request = {.generation = 1, .file_index = 0};
    u8 byte = 0;
    ImageBrowserResult oversized = image_browser_decode(request,
        (ByteSlice){.pointer = &byte, .length = IMAGE_BROWSER_MAX_ENCODED_BYTES + 1}, output, scratch, IMAGE_FORMAT_UNKNOWN);
    IMAGE_BROWSER_TEST(oversized.status == IMAGE_BROWSER_LOAD_ENCODED_LIMIT);
    IMAGE_BROWSER_TEST(output->position == arena_minimum_position);
    image_browser_result_release(&oversized);
    arena_destroy(scratch, 1);

    static u8 const truncated[] = {'P', '6', '\n', '2', ' ', '1', '\n', '2', '5', '5', '\n', 255, 0, 0};
    output = image_browser_test_arena();
    scratch = image_browser_test_arena();
    ImageBrowserResult failed = image_browser_decode(request,
        (ByteSlice){.pointer = (u8*)truncated, .length = sizeof(truncated)}, output, scratch, IMAGE_FORMAT_UNKNOWN);
    IMAGE_BROWSER_TEST(failed.status == IMAGE_BROWSER_LOAD_DECODE_ERROR && failed.decoded.status == IMAGE_DECODE_TRUNCATED);
    IMAGE_BROWSER_TEST(!failed.decoded.image.pixels.pointer && output->position == arena_minimum_position);
    IMAGE_BROWSER_TEST(scratch->position == arena_minimum_position);
    image_browser_result_release(&failed);
    arena_destroy(scratch, 1);

    // A legal SOF2 frame header is unsupported coding in the actual decoder.
    static u8 const progressive[] = {0xff, 0xd8, 0xff, 0xc2, 0, 11, 8, 0, 1, 0, 1, 1, 1, 0x11, 0, 0xff, 0xd9};
    output = image_browser_test_arena();
    scratch = image_browser_test_arena();
    ImageBrowserResult unsupported = image_browser_decode(request,
        (ByteSlice){.pointer = (u8*)progressive, .length = sizeof(progressive)}, output, scratch, IMAGE_FORMAT_UNKNOWN);
    IMAGE_BROWSER_TEST(unsupported.status == IMAGE_BROWSER_LOAD_DECODE_ERROR);
    IMAGE_BROWSER_TEST(unsupported.decoded.status == IMAGE_DECODE_UNSUPPORTED_FEATURE);
    IMAGE_BROWSER_TEST(unsupported.decoded.unsupported_feature == IMAGE_UNSUPPORTED_FEATURE_CODING);
    IMAGE_BROWSER_TEST(!unsupported.decoded.image.pixels.pointer && output->position == arena_minimum_position);
    image_browser_result_release(&unsupported);
    arena_destroy(scratch, 1);

    output = image_browser_test_arena();
    scratch = image_browser_test_arena();
    ImageBrowserResult invalid = image_browser_decode(request, (ByteSlice){.length = 1},
                                                      output, scratch, IMAGE_FORMAT_UNKNOWN);
    IMAGE_BROWSER_TEST(invalid.decoded.status == IMAGE_DECODE_INVALID_ARGUMENT);
    image_browser_result_release(&invalid);
    arena_destroy(scratch, 1);

    // Decoder resource errors preserve category, numeric limit identity and
    // transactional output rather than producing partially initialized pixels.
    static u8 const too_wide[] = {'P', '6', '\n', '8', '1', '9', '3', ' ', '1', '\n', '2', '5', '5', '\n'};
    output = image_browser_test_arena();
    scratch = image_browser_test_arena();
    ImageBrowserResult limited = image_browser_decode(request,
        (ByteSlice){.pointer = (u8*)too_wide, .length = sizeof(too_wide)}, output, scratch, IMAGE_FORMAT_UNKNOWN);
    IMAGE_BROWSER_TEST(limited.decoded.status == IMAGE_DECODE_LIMIT_EXCEEDED);
    IMAGE_BROWSER_TEST(limited.decoded.exceeded_limit == IMAGE_EXCEEDED_LIMIT_WIDTH);
    IMAGE_BROWSER_TEST(limited.decoded.observed_value == 8193 && limited.decoded.limit_value == 8192);
    IMAGE_BROWSER_TEST(!limited.decoded.image.pixels.pointer && output->position == arena_minimum_position);
    image_browser_result_release(&limited);
    arena_destroy(scratch, 1);

    Arena* large_source = image_browser_test_arena();
    static u8 const wide_header[] = {'P', '6', '\n', '4', '0', '9', '6', ' ', '1', '\n', '2', '5', '5', '\n'};
    u64 encoded_size = sizeof(wide_header) + 4096u * 3u;
    u8* large_bytes = arena_allocate(large_source, u8, encoded_size);
    memcpy(large_bytes, wide_header, sizeof(wide_header));
    memset(large_bytes + sizeof(wide_header), 127, (size_t)(encoded_size - sizeof(wide_header)));
    output = arena_create((ArenaCreation){
        .reserved_size = BUSTER_KB(4), .initial_size = BUSTER_KB(4), .flags = {.no_pool = 1},
    });
    scratch = image_browser_test_arena();
    ImageBrowserResult capacity = image_browser_decode(request,
        (ByteSlice){.pointer = large_bytes, .length = encoded_size}, output, scratch, IMAGE_FORMAT_UNKNOWN);
    IMAGE_BROWSER_TEST(capacity.decoded.status == IMAGE_DECODE_CAPACITY_EXCEEDED);
    IMAGE_BROWSER_TEST(!capacity.decoded.image.pixels.pointer && output->position == arena_minimum_position);
    image_browser_result_release(&capacity);
    arena_destroy(scratch, 1);
    arena_destroy(large_source, 1);

    ImageDecodeOptions options = image_browser_decode_options(0);
    IMAGE_BROWSER_TEST(options.max_pixels == IMAGE_BROWSER_MAX_PIXELS);
    IMAGE_BROWSER_TEST(options.max_decoded_bytes == IMAGE_BROWSER_MAX_DECODED_BYTES);
    IMAGE_BROWSER_TEST(options.max_work == BUSTER_MB(256) && options.max_frames == 1024);
    return failures;
}

BUSTER_GLOBAL_LOCAL s32 image_browser_test_view(void)
{
    s32 failures = 0;
    ImageBrowserState state;
    image_browser_state_initialize(&state, 4, 4);
    ImageBrowserRequest request = {0};
    IMAGE_BROWSER_TEST(image_browser_request(&state, 0));
    IMAGE_BROWSER_TEST(image_browser_begin_load(&state, &request));
    ImageBrowserResult decoded = image_browser_test_decode(request);
    IMAGE_BROWSER_TEST(image_browser_complete(&state, &decoded));
    IMAGE_BROWSER_TEST(image_browser_publish(&state));

    image_browser_pan(&state, 1, -1);
    f64 anchor_x = 0;
    f64 anchor_y = 4;
    f64 before_x = (anchor_x - 2.0 - state.view.pan_x) / state.view.scale;
    f64 before_y = (anchor_y - 2.0 - state.view.pan_y) / state.view.scale;
    image_browser_zoom(&state, 2, anchor_x, anchor_y);
    f64 after_x = (anchor_x - 2.0 - state.view.pan_x) / state.view.scale;
    f64 after_y = (anchor_y - 2.0 - state.view.pan_y) / state.view.scale;
    IMAGE_BROWSER_TEST(image_browser_test_near(before_x, after_x));
    IMAGE_BROWSER_TEST(image_browser_test_near(before_y, after_y));
    IMAGE_BROWSER_TEST(image_browser_test_near(state.view.scale, 4.0));

    image_browser_zoom(&state, 64, 2, 2);
    IMAGE_BROWSER_TEST(state.view.scale == IMAGE_BROWSER_MAX_SCALE);
    for (u32 iteration = 0; iteration < 20; iteration += 1)
    {
        image_browser_zoom(&state, 0.5, 2, 2);
    }
    IMAGE_BROWSER_TEST(state.view.scale == IMAGE_BROWSER_MIN_SCALE);
    ImageBrowserView unchanged = state.view;
    image_browser_zoom(&state, 0, 2, 2);
    image_browser_zoom(&state, -1, 2, 2);
    image_browser_zoom(&state, 2, -1, 2);
    image_browser_pan(&state, 65537, 0);
    IMAGE_BROWSER_TEST(state.view.scale == unchanged.scale && state.view.pan_x == unchanged.pan_x && state.view.pan_y == unchanged.pan_y);

    image_browser_actual_size(&state);
    IMAGE_BROWSER_TEST(state.view.scale == 1.0 && state.view.pan_x == 0 && state.view.pan_y == 0);
    image_browser_resize(&state, 8, 4);
    image_browser_fit(&state);
    IMAGE_BROWSER_TEST(state.view.scale == 4.0);
    state.published.decoded.information.orientation = IMAGE_ORIENTATION_RIGHT_TOP;
    u32 width = 0;
    u32 height = 0;
    image_browser_display_dimensions(&state, &width, &height);
    IMAGE_BROWSER_TEST(width == 1 && height == 2);
    image_browser_fit(&state);
    IMAGE_BROWSER_TEST(state.view.scale == 2.0);

    image_browser_resize(&state, 0, 0);
    image_browser_fit(&state);
    IMAGE_BROWSER_TEST(state.view.scale == 1.0);
    image_browser_pan(&state, 65536, -65536);
    for (u32 iteration = 0; iteration < 32; iteration += 1)
    {
        image_browser_pan(&state, 65536, -65536);
    }
    IMAGE_BROWSER_TEST(state.view.pan_x < 600000 && state.view.pan_y > -600000);
    IMAGE_BROWSER_TEST(image_browser_state_destroy(&state));
    return failures;
}

s32 image_browser_run_state_tests(void)
{
    s32 failures = image_browser_test_transitions();
    failures += image_browser_test_shutdown();
    failures += image_browser_test_decode_policy();
    failures += image_browser_test_view();
    fprintf(stderr, "image browser state/ownership: %d failures\n", failures);
    return failures ? 1 : 0;
}

#undef IMAGE_BROWSER_TEST
#endif
