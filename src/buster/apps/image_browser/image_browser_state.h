#pragma once

// Image browser's serial state and move-owned decode results.
// request/begin_load retain only the latest pending request and one active load.
// complete/publish perform synchronized handoff and reject stale generations.
// decode uses immutable source bytes and caller-bounded output/scratch arenas.
// result_release destroys its output arena. shutdown forbids publication;
// state_destroy requires the worker's joined result before releasing state.
// fit/zoom/pan operate in oriented-image viewport coordinates.
//
// Only the owner thread mutates ImageBrowserState. The worker receives a request
// by value and owns source/output/scratch until its result is synchronized and
// handed back. Source and scratch are loader-owned. A result owns only output.
// ImageBrowserFrameWork coalesces owner-thread content redraws and repaint-only
// requests; it owns no canvas pixels or native window resources.

#include <buster/lib/image.h>

#define IMAGE_BROWSER_MAX_ENCODED_BYTES BUSTER_MB(32)
#define IMAGE_BROWSER_MAX_DECODED_BYTES BUSTER_MB(32)
#define IMAGE_BROWSER_MAX_SCRATCH_BYTES BUSTER_MB(96)
#define IMAGE_BROWSER_ARENA_OVERHEAD BUSTER_KB(4)
#define IMAGE_BROWSER_MAX_PIXELS (IMAGE_BROWSER_MAX_DECODED_BYTES / 4u)
#define IMAGE_BROWSER_MIN_SCALE (1.0 / 64.0)
#define IMAGE_BROWSER_MAX_SCALE 64.0

typedef struct ImageBrowserRequest ImageBrowserRequest;
struct ImageBrowserRequest
{
    u64 generation;
    u64 file_index;
};

typedef enum ImageBrowserLoadStatus
{
    IMAGE_BROWSER_LOAD_SUCCESS,
    IMAGE_BROWSER_LOAD_READ_ERROR,
    IMAGE_BROWSER_LOAD_ENCODED_LIMIT,
    IMAGE_BROWSER_LOAD_DECODE_ERROR,
    IMAGE_BROWSER_LOAD_CANCELLED,
} ImageBrowserLoadStatus;

typedef struct ImageBrowserResult ImageBrowserResult;
struct ImageBrowserResult
{
    ImageBrowserRequest request;
    ImageBrowserLoadStatus status;
    ImageDecodeResult decoded;
    Arena* output_arena;
    s32 system_error;
    u64 encoded_size;
};

typedef struct ImageBrowserView ImageBrowserView;
struct ImageBrowserView
{
    u32 width;
    u32 height;
    // Viewport pixels per oriented image pixel. Pan is image centre relative
    // to viewport centre, in viewport pixels.
    f64 scale;
    f64 pan_x;
    f64 pan_y;
};

typedef struct ImageBrowserState ImageBrowserState;
struct ImageBrowserState
{
    u64 generation;
    ImageBrowserRequest pending;
    ImageBrowserRequest active;
    ImageBrowserResult completed;
    ImageBrowserResult published;
    ImageBrowserView view;
    bool has_pending;
    bool has_active;
    bool has_completed;
    bool has_published;
    bool shutting_down;
};

typedef struct ImageBrowserFrameWork ImageBrowserFrameWork;
struct ImageBrowserFrameWork
{
    bool rasterize_pending;
    bool present_pending;
};

// complete consumes *result only for the matching active request. An unmatched
// result remains caller-owned. publish consumes completed even if stale.
// A current error replaces the old image. While loading, the previous output
// allocation stays alive, but the app hides it whenever a request is pending,
// active, or the published generation differs from the current generation.
BUSTER_F_DECL void image_browser_state_initialize(ImageBrowserState* state, u32 width, u32 height);
BUSTER_F_DECL void image_browser_frame_request_draw(ImageBrowserFrameWork* work);
BUSTER_F_DECL void image_browser_frame_request_repaint(ImageBrowserFrameWork* work);
BUSTER_F_DECL bool image_browser_frame_take(ImageBrowserFrameWork* work, bool* rasterize);
BUSTER_F_DECL bool image_browser_request(ImageBrowserState* state, u64 file_index);
BUSTER_F_DECL bool image_browser_begin_load(ImageBrowserState* state, ImageBrowserRequest* request);
BUSTER_F_DECL bool image_browser_complete(ImageBrowserState* state, ImageBrowserResult* result);
BUSTER_F_DECL bool image_browser_publish(ImageBrowserState* state);
BUSTER_F_DECL void image_browser_result_release(ImageBrowserResult* result);
BUSTER_F_DECL void image_browser_shutdown(ImageBrowserState* state);
// false means a worker remains active. Join it and deliver/release its result
// before retrying; this call never frees live worker storage.
BUSTER_F_DECL bool image_browser_state_destroy(ImageBrowserState* state);

// Output ownership transfers to the result even on failure. Arenas are fresh,
// distinct, no_pool, and bounded to their byte ceiling plus ARENA_OVERHEAD.
// Precommit the reservation at arena_create: creation can fail gracefully;
// later arena commitment failure is fatal. Encoded bytes remain readable and
// immutable for this synchronous call.
BUSTER_F_DECL ImageDecodeOptions image_browser_decode_options(Arena* scratch);
BUSTER_F_DECL ImageBrowserResult image_browser_decode(ImageBrowserRequest request, ByteSlice encoded,
                                                     Arena* output, Arena* scratch, ImageFormat format_hint);

BUSTER_F_DECL void image_browser_display_dimensions(ImageBrowserState const* state, u32* width, u32* height);
BUSTER_F_DECL void image_browser_resize(ImageBrowserState* state, u32 width, u32 height);
BUSTER_F_DECL void image_browser_fit(ImageBrowserState* state);
BUSTER_F_DECL void image_browser_actual_size(ImageBrowserState* state);
BUSTER_F_DECL void image_browser_zoom(ImageBrowserState* state, f64 factor, f64 anchor_x, f64 anchor_y);
BUSTER_F_DECL void image_browser_pan(ImageBrowserState* state, f64 delta_x, f64 delta_y);
