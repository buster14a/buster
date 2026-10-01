// Image-browser state transitions, decode policy and viewport arithmetic.
// See image_browser_state.h for the serial ownership contract. This file knows
// no directory, native window, thread handle or presentation resource.

#include <buster/apps/image_browser/image_browser_state.h>

BUSTER_GLOBAL_LOCAL bool image_browser_request_equal(ImageBrowserRequest a, ImageBrowserRequest b)
{
    bool result = a.generation == b.generation && a.file_index == b.file_index;
    return result;
}

BUSTER_GLOBAL_LOCAL f64 image_browser_clamp_scale(f64 scale)
{
    f64 result = BUSTER_CLAMP(IMAGE_BROWSER_MIN_SCALE, scale, IMAGE_BROWSER_MAX_SCALE);
    return result;
}

void image_browser_state_initialize(ImageBrowserState* state, u32 width, u32 height)
{
    *state = (ImageBrowserState){.view = {.width = width, .height = height, .scale = 1.0}};
}

bool image_browser_request(ImageBrowserState* state, u64 file_index)
{
    bool result = !state->shutting_down && state->generation != UINT64_MAX;
    if (result)
    {
        state->generation += 1;
        state->pending = (ImageBrowserRequest){.generation = state->generation, .file_index = file_index};
        state->has_pending = true;
    }
    return result;
}

bool image_browser_begin_load(ImageBrowserState* state, ImageBrowserRequest* request)
{
    bool result = request && !state->shutting_down && state->has_pending &&
                  !state->has_active && !state->has_completed;
    if (result)
    {
        state->active = state->pending;
        state->pending = (ImageBrowserRequest){0};
        state->has_pending = false;
        state->has_active = true;
        *request = state->active;
    }
    return result;
}

void image_browser_result_release(ImageBrowserResult* result)
{
    if (result->output_arena)
    {
        arena_destroy(result->output_arena, 1);
    }
    *result = (ImageBrowserResult){0};
}

bool image_browser_complete(ImageBrowserState* state, ImageBrowserResult* result)
{
    bool accepted = result && state->has_active && !state->has_completed &&
                    image_browser_request_equal(result->request, state->active);
    if (accepted)
    {
        state->active = (ImageBrowserRequest){0};
        state->has_active = false;
        if (state->shutting_down)
        {
            image_browser_result_release(result);
        }
        else
        {
            state->completed = *result;
            state->has_completed = true;
            *result = (ImageBrowserResult){0};
        }
    }
    return accepted;
}

bool image_browser_publish(ImageBrowserState* state)
{
    bool published = state->has_completed && !state->shutting_down &&
                     state->completed.request.generation == state->generation;
    if (state->has_completed)
    {
        if (published)
        {
            image_browser_result_release(&state->published);
            state->published = state->completed;
            state->completed = (ImageBrowserResult){0};
            state->has_published = true;
            image_browser_fit(state);
        }
        else
        {
            image_browser_result_release(&state->completed);
        }
        state->has_completed = false;
    }
    return published;
}

void image_browser_shutdown(ImageBrowserState* state)
{
    state->shutting_down = true;
    state->pending = (ImageBrowserRequest){0};
    state->has_pending = false;
    if (state->has_completed)
    {
        image_browser_result_release(&state->completed);
        state->has_completed = false;
    }
}

bool image_browser_state_destroy(ImageBrowserState* state)
{
    image_browser_shutdown(state);
    bool result = !state->has_active;
    if (result)
    {
        image_browser_result_release(&state->published);
        state->has_published = false;
    }
    return result;
}

ImageDecodeOptions image_browser_decode_options(Arena* scratch)
{
    ImageDecodeOptions result = {
        .max_width = 8192,
        .max_height = 8192,
        .max_pixels = IMAGE_BROWSER_MAX_PIXELS,
        .max_decoded_bytes = IMAGE_BROWSER_MAX_DECODED_BYTES,
        .max_work = BUSTER_MB(256),
        .max_frames = 1024,
        .max_chunks = 16384,
        .max_segments = 16384,
        .max_blocks = IMAGE_BROWSER_MAX_PIXELS,
        .max_scans = 1024,
        .max_depth = 64,
        .scratch_arena = scratch,
    };
    return result;
}

ImageBrowserResult image_browser_decode(ImageBrowserRequest request, ByteSlice encoded,
                                        Arena* output, Arena* scratch, ImageFormat format_hint)
{
    ImageBrowserResult result = {.request = request, .output_arena = output, .encoded_size = encoded.length};
    bool valid = output && scratch && output != scratch &&
                 output->flags.no_pool && scratch->flags.no_pool &&
                 output->position == arena_minimum_position && scratch->position == arena_minimum_position &&
                 output->reserved_size <= IMAGE_BROWSER_MAX_DECODED_BYTES + IMAGE_BROWSER_ARENA_OVERHEAD &&
                 scratch->reserved_size <= IMAGE_BROWSER_MAX_SCRATCH_BYTES + IMAGE_BROWSER_ARENA_OVERHEAD &&
                 (!encoded.length || encoded.pointer);
    if (encoded.length > IMAGE_BROWSER_MAX_ENCODED_BYTES)
    {
        result.status = IMAGE_BROWSER_LOAD_ENCODED_LIMIT;
    }
    else if (!valid)
    {
        result.status = IMAGE_BROWSER_LOAD_DECODE_ERROR;
        result.decoded.status = IMAGE_DECODE_INVALID_ARGUMENT;
    }
    else
    {
        ImageDecodeOptions options = image_browser_decode_options(scratch);
        options.format_hint = format_hint;
        result.decoded = image_decode(output, encoded, options);
        result.status = result.decoded.status == IMAGE_DECODE_SUCCESS ? IMAGE_BROWSER_LOAD_SUCCESS : IMAGE_BROWSER_LOAD_DECODE_ERROR;
    }
    return result;
}

void image_browser_display_dimensions(ImageBrowserState const* state, u32* width, u32* height)
{
    u32 display_width = 0;
    u32 display_height = 0;
    if (state->has_published && state->published.status == IMAGE_BROWSER_LOAD_SUCCESS)
    {
        Image const* image = &state->published.decoded.image;
        ImageOrientation orientation = state->published.decoded.information.orientation;
        bool exchange = orientation >= IMAGE_ORIENTATION_LEFT_TOP && orientation <= IMAGE_ORIENTATION_LEFT_BOTTOM;
        display_width = exchange ? image->height : image->width;
        display_height = exchange ? image->width : image->height;
    }
    *width = display_width;
    *height = display_height;
}

void image_browser_fit(ImageBrowserState* state)
{
    u32 width = 0;
    u32 height = 0;
    image_browser_display_dimensions(state, &width, &height);
    f64 scale = 1.0;
    if (width && height && state->view.width && state->view.height)
    {
        f64 horizontal = (f64)state->view.width / (f64)width;
        f64 vertical = (f64)state->view.height / (f64)height;
        scale = BUSTER_MIN(horizontal, vertical);
    }
    state->view.scale = image_browser_clamp_scale(scale);
    state->view.pan_x = 0;
    state->view.pan_y = 0;
}

void image_browser_resize(ImageBrowserState* state, u32 width, u32 height)
{
    state->view.width = width;
    state->view.height = height;
}

void image_browser_actual_size(ImageBrowserState* state)
{
    state->view.scale = 1.0;
    state->view.pan_x = 0;
    state->view.pan_y = 0;
}

void image_browser_zoom(ImageBrowserState* state, f64 factor, f64 anchor_x, f64 anchor_y)
{
    // Native events supply finite viewport coordinates and bounded factors
    // (wheel: 1.25 or 0.8). Comparisons also reject NaN factors/coordinates.
    if (factor > 0 && factor <= IMAGE_BROWSER_MAX_SCALE &&
        anchor_x >= 0 && anchor_x <= state->view.width && anchor_y >= 0 && anchor_y <= state->view.height)
    {
        f64 previous = state->view.scale;
        f64 next = image_browser_clamp_scale(previous * factor);
        f64 relative_x = anchor_x - (f64)state->view.width * 0.5;
        f64 relative_y = anchor_y - (f64)state->view.height * 0.5;
        f64 ratio = next / previous;
        state->view.pan_x = relative_x - (relative_x - state->view.pan_x) * ratio;
        state->view.pan_y = relative_y - (relative_y - state->view.pan_y) * ratio;
        state->view.scale = next;
    }
}

void image_browser_pan(ImageBrowserState* state, f64 delta_x, f64 delta_y)
{
    // Keep repeated input bounded before the renderer converts coordinates.
    f64 limit = (f64)IMAGE_BROWSER_MAX_SCALE * 8192.0 + 65536.0;
    if (delta_x >= -65536.0 && delta_x <= 65536.0 && delta_y >= -65536.0 && delta_y <= 65536.0)
    {
        state->view.pan_x = BUSTER_CLAMP(-limit, state->view.pan_x + delta_x, limit);
        state->view.pan_y = BUSTER_CLAMP(-limit, state->view.pan_y + delta_y, limit);
    }
}
