// Runnable native image browser: Linux x86-64, XCB window and CPU raster.
// process_arguments/entry_point own the CLI and native lifecycle.
// image_browser_app_select/dispatch drive one immutable catalogue and actor state.
// image_browser_app_render presents only the current completed generation.
// image_browser_app_smoke validates actual server readback before joined shutdown.
// This is a separate application; no UI/media consumer is added to headless ide.

#define BUSTER_USE_GRAPHICS 0
#include <buster/lib/base.h>
#include <buster/lib/entry_point.h>
#include <buster/lib/arena.h>
#include <buster/lib/os.h>
#include <buster/lib/string.h>
#include <buster/lib/window.h>
#include <buster/lib/rendering_raster.h>
#include <buster/apps/image_browser/image_browser_state.h>
#include <buster/apps/image_browser/image_browser_linux.h>
#if BUSTER_INCLUDE_TESTS
#include <buster/lib/rendering/raster_internal.h>
#include <xcb/xcb.h>
#include <xcb/xcb_keysyms.h>
#include <stdlib.h>
#endif
#include <stdio.h>
#include <time.h>
#include <errno.h>

#if !BUSTER_LINUX || !BUSTER_CPU_ARCH_X86_64 || !BUSTER_LINK_LIBC
#error image_browser requires Linux x86-64 with libc
#endif

#if BUSTER_UNITY_BUILD
#include <buster/lib/os.c>
#include <buster/lib/entry_point.c>
#include <buster/lib/arena.c>
#include <buster/lib/integer.c>
#include <buster/lib/string.c>
#include <buster/lib/file.c>
#include <buster/lib/hash.c>
#include <buster/lib/time.c>
#include <buster/lib/float.c>
#include <buster/lib/target.c>
#include <buster/lib/image.c>
#include <buster/lib/window.c>
#include <buster/lib/rendering_raster.c>
#include <buster/apps/image_browser/image_browser_state.c>
#include <buster/apps/image_browser/image_browser_linux.c>
#endif

#define IMAGE_BROWSER_EVENT_BYTES BUSTER_MB(32)
#define IMAGE_BROWSER_REFRESH_MS UINT64_C(100)
#define IMAGE_BROWSER_SMOKE_TIMEOUT_MS UINT64_C(10000)

typedef struct ImageBrowserProgram ImageBrowserProgram;
struct ImageBrowserProgram
{
    ProgramState state;
    String8 input_path;
    bool help;
    bool smoke;
};

BUSTER_GLOBAL_LOCAL ImageBrowserProgram image_browser_program;
BUSTER_V_IMPL ProgramState* program_state = &image_browser_program.state;

typedef struct ImageBrowserApplication ImageBrowserApplication;
struct ImageBrowserApplication
{
    ImageBrowserState state;
    ImageBrowserCatalog catalog;
    ImageBrowserWorker worker;
    WmHandle* wm;
    WmWindowHandle* window;
    RenderingRasterPresenter presenter;
    Arena* events;
    Arena* canvas_arena;
    u64 selected;
    u64 refresh_time;
    u64 smoke_generation;
    u32 smoke_phase;
    WmOffset drag_position;
    bool worker_started;
    bool dragging;
    bool dirty;
    bool closing;
    bool smoke_complete;
};

BUSTER_GLOBAL_LOCAL void image_browser_usage(void)
{
    fprintf(stderr, "usage: image_browser <image-or-directory>\n");
#if BUSTER_INCLUDE_TESTS
    fprintf(stderr, "       image_browser src/buster/tests/image_browser/fixtures --smoke\n");
#endif
    fprintf(stderr, "Linux x86-64 / XCB CPU raster. PNG, sequential JPEG, GIF first image, BMP, TGA, QOI, PNM/PAM.\n"
                    "Left/Right or PgUp/PgDn navigate; wheel or +/- zoom; left drag pans; F fits; 1 sets actual size; R reloads; Esc closes.\n"
                    "Detailed metadata is printed to stderr. No animation playback, progressive/CMYK JPEG, color management, or drag/drop.\n");
}

ProcessResult process_arguments(void)
{
    SliceString8 arguments = program_state->input.arguments;
    ProcessResult result = PROCESS_RESULT_SUCCESS;
    if (arguments.length == 2 && (string_equal(arguments.pointer[1], S8("--help")) || string_equal(arguments.pointer[1], S8("-h"))))
    {
        image_browser_program.help = true;
    }
    else if (arguments.length == 2)
    {
        image_browser_program.input_path = arguments.pointer[1];
    }
#if BUSTER_INCLUDE_TESTS
    else if (arguments.length == 3 && string_equal(arguments.pointer[2], S8("--smoke")))
    {
        image_browser_program.input_path = arguments.pointer[1];
        image_browser_program.smoke = true;
    }
#endif
    else
    {
        image_browser_usage();
        result = PROCESS_RESULT_FAILED;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL u64 image_browser_clock_ms(bool* valid)
{
    struct timespec stamp = {0};
    u64 result = 0;
    *valid = clock_gettime(CLOCK_MONOTONIC, &stamp) == 0 && stamp.tv_sec >= 0 && stamp.tv_nsec >= 0;
    if (*valid)
    {
        u64 seconds = (u64)stamp.tv_sec;
        *valid = seconds <= (UINT64_MAX - UINT64_C(999)) / UINT64_C(1000);
        if (*valid)
        {
            result = seconds * UINT64_C(1000) + (u64)stamp.tv_nsec / UINT64_C(1000000);
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL char const* image_browser_format_name(ImageFormat format)
{
    char const* result;
    switch (format)
    {
    case IMAGE_FORMAT_PNG: result = "PNG"; break;
    case IMAGE_FORMAT_JPEG: result = "JPEG"; break;
    case IMAGE_FORMAT_GIF: result = "GIF"; break;
    case IMAGE_FORMAT_BMP: result = "BMP"; break;
    case IMAGE_FORMAT_TGA: result = "TGA"; break;
    case IMAGE_FORMAT_QOI: result = "QOI"; break;
    case IMAGE_FORMAT_PNM: result = "PNM/PAM"; break;
    case IMAGE_FORMAT_WEBP: result = "WebP"; break;
    case IMAGE_FORMAT_TIFF: result = "TIFF"; break;
    case IMAGE_FORMAT_BIG_TIFF: result = "BigTIFF"; break;
    case IMAGE_FORMAT_ICO: result = "ICO/CUR"; break;
    case IMAGE_FORMAT_AVIF: result = "AVIF"; break;
    case IMAGE_FORMAT_HEIF: result = "HEIF"; break;
    case IMAGE_FORMAT_JPEG_XL: result = "JPEG XL"; break;
    case IMAGE_FORMAT_JPEG_2000: result = "JPEG 2000"; break;
    case IMAGE_FORMAT_PSD: result = "PSD"; break;
    case IMAGE_FORMAT_OPENEXR: result = "OpenEXR"; break;
    case IMAGE_FORMAT_HDR: result = "HDR"; break;
    case IMAGE_FORMAT_DDS: result = "DDS"; break;
    case IMAGE_FORMAT_KTX1: result = "KTX1"; break;
    case IMAGE_FORMAT_KTX2: result = "KTX2"; break;
    case IMAGE_FORMAT_PFM: result = "PFM"; break;
    default: result = "unknown"; break;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL char const* image_browser_decode_status_name(ImageDecodeStatus status)
{
    char const* result;
    switch (status)
    {
    case IMAGE_DECODE_SUCCESS: result = "decoded"; break;
    case IMAGE_DECODE_INVALID_ARGUMENT: result = "invalid decode argument"; break;
    case IMAGE_DECODE_UNRECOGNIZED_FORMAT: result = "unrecognized format"; break;
    case IMAGE_DECODE_TRUNCATED: result = "truncated file"; break;
    case IMAGE_DECODE_MALFORMED: result = "malformed image"; break;
    case IMAGE_DECODE_UNSUPPORTED_FORMAT: result = "unsupported format"; break;
    case IMAGE_DECODE_UNSUPPORTED_FEATURE: result = "unsupported image feature"; break;
    case IMAGE_DECODE_CHECKSUM_MISMATCH: result = "checksum mismatch"; break;
    case IMAGE_DECODE_LIMIT_EXCEEDED: result = "image resource limit"; break;
    case IMAGE_DECODE_CAPACITY_EXCEEDED: result = "decode arena capacity"; break;
    default: result = "unknown decode status"; break;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL char const* image_browser_load_status_name(ImageBrowserResult const* result)
{
    char const* name;
    switch (result->status)
    {
    case IMAGE_BROWSER_LOAD_SUCCESS: name = "decoded"; break;
    case IMAGE_BROWSER_LOAD_READ_ERROR: name = "file/resource error"; break;
    case IMAGE_BROWSER_LOAD_ENCODED_LIMIT: name = "encoded file exceeds 32 MiB"; break;
    case IMAGE_BROWSER_LOAD_DECODE_ERROR: name = image_browser_decode_status_name(result->decoded.status); break;
    case IMAGE_BROWSER_LOAD_CANCELLED: name = "load cancelled"; break;
    default: name = "resource error"; break;
    }
    return name;
}

// Titles must remain valid bounded UTF-8 even for arbitrary POSIX filename bytes.
// Printable ASCII passes through; every other byte has an explicit hex escape.
BUSTER_GLOBAL_LOCAL void image_browser_safe_path(String8 path, char buffer[512])
{
    static char const hex[] = "0123456789abcdef";
    u64 used = 0;
    u64 index = 0;
    for (; index < path.length && used < 504; index += 1)
    {
        u8 byte = path.pointer[index];
        if (byte >= 32 && byte < 127)
        {
            buffer[used] = (char)byte;
            used += 1;
        }
        else
        {
            buffer[used + 0] = '\\';
            buffer[used + 1] = 'x';
            buffer[used + 2] = hex[byte >> 4u];
            buffer[used + 3] = hex[byte & 15u];
            used += 4;
        }
    }
    if (index < path.length)
    {
        buffer[used + 0] = '.';
        buffer[used + 1] = '.';
        buffer[used + 2] = '.';
        used += 3;
    }
    buffer[used] = 0;
}

BUSTER_GLOBAL_LOCAL bool image_browser_app_loading(ImageBrowserApplication const* app)
{
    bool result = app->state.has_pending || app->state.has_active || !app->state.has_published ||
                  app->state.published.request.generation != app->state.generation;
    return result;
}

BUSTER_GLOBAL_LOCAL bool image_browser_app_title(ImageBrowserApplication* app)
{
    char path[512];
    char title[1536];
    image_browser_safe_path(app->catalog.paths.pointer[app->selected], path);
    int length;
    if (image_browser_app_loading(app))
    {
        length = snprintf(title, sizeof(title), "Buster Image Browser | Loading %s | %llu/%llu", path,
                          (unsigned long long)(app->selected + 1), (unsigned long long)app->catalog.paths.length);
    }
    else if (app->state.published.status == IMAGE_BROWSER_LOAD_SUCCESS)
    {
        ImageInformation const* info = &app->state.published.decoded.information;
        length = snprintf(title, sizeof(title),
            "Buster Image Browser | %s | %s %ux%u src:%uch/%ub alpha:%s orient:%u frames:%u%s | %.1f%% | %llu/%llu",
            path, image_browser_format_name(info->format), info->width, info->height,
            (unsigned)info->source_channel_count, (unsigned)info->source_bits_per_channel,
            info->has_alpha ? "yes" : "no", (unsigned)info->orientation, info->frame_count,
            info->has_more_images ? " first/default only" : "", app->state.view.scale * 100.0,
            (unsigned long long)(app->selected + 1), (unsigned long long)app->catalog.paths.length);
    }
    else
    {
        length = snprintf(title, sizeof(title), "Buster Image Browser | %s | ERROR: %s (system=%d) | %llu/%llu",
                          path, image_browser_load_status_name(&app->state.published), app->state.published.system_error,
                          (unsigned long long)(app->selected + 1), (unsigned long long)app->catalog.paths.length);
    }
    bool result = length >= 0 && (u64)length < sizeof(title);
    if (result)
    {
        result = wm_window_set_title(app->wm, app->window, (String8){.pointer = (char8*)title, .length = (u64)length});
    }
    return result;
}

BUSTER_GLOBAL_LOCAL void image_browser_app_metadata(ImageBrowserApplication const* app)
{
    char path[512];
    image_browser_safe_path(app->catalog.paths.pointer[app->selected], path);
    ImageBrowserResult const* loaded = &app->state.published;
    if (loaded->status == IMAGE_BROWSER_LOAD_SUCCESS)
    {
        ImageInformation const* info = &loaded->decoded.information;
        fprintf(stderr,
            "%s: %s coded=%ux%u channels=%u bits=%u alpha=%s orientation=%u frames=%u animated=%s first/default-only=%s color-flags=0x%x qoi-colorspace=%u encoded=%llu bytes; no ICC/gamma conversion\n",
            path, image_browser_format_name(info->format), info->width, info->height,
            (unsigned)info->source_channel_count, (unsigned)info->source_bits_per_channel,
            info->has_alpha ? "yes" : "no", (unsigned)info->orientation, info->frame_count,
            info->is_animated ? "yes" : "no", info->has_more_images ? "yes" : "no",
            info->color_metadata_flags, (unsigned)info->qoi_colorspace, (unsigned long long)loaded->encoded_size);
    }
    else
    {
        fprintf(stderr, "%s: %s; system=%d format=%s byte=%llu feature=%u limit=%u observed=%llu allowed=%llu\n",
            path, image_browser_load_status_name(loaded), loaded->system_error,
            image_browser_format_name(loaded->decoded.information.format), (unsigned long long)loaded->decoded.error_offset,
            (unsigned)loaded->decoded.unsupported_feature, (unsigned)loaded->decoded.exceeded_limit,
            (unsigned long long)loaded->decoded.observed_value, (unsigned long long)loaded->decoded.limit_value);
    }
}

BUSTER_GLOBAL_LOCAL bool image_browser_app_select(ImageBrowserApplication* app, bool next)
{
    if (next)
    {
        app->selected = app->selected + 1 < app->catalog.paths.length ? app->selected + 1 : 0;
    }
    else
    {
        app->selected = app->selected ? app->selected - 1 : app->catalog.paths.length - 1;
    }
    bool result = image_browser_request(&app->state, app->selected);
    if (result)
    {
        image_browser_worker_set_generation(&app->worker, app->state.generation);
        app->dragging = false;
        app->dirty = true;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool image_browser_app_dispatch(ImageBrowserApplication* app, WmEventList events)
{
    bool result = true;
    for (WmEvent* event = events.first; event && result && !app->closing; event = event->next)
    {
        if (event->window == app->window)
        {
            if (event->kind == WM_EVENT_WINDOW_CLOSE)
            {
#if BUSTER_INCLUDE_TESTS
                if (image_browser_program.smoke && app->smoke_phase == 3)
                {
                    app->smoke_complete = true;
                }
#endif
                app->closing = true;
            }
            else if (event->kind == WM_EVENT_WINDOW_UNFOCUS)
            {
                app->dragging = false;
            }
            else if (event->kind == WM_EVENT_WINDOW_RESIZE)
            {
                image_browser_resize(&app->state, event->position.width, event->position.height);
                app->dirty = true;
            }
            else if (event->kind == WM_EVENT_MOUSE_MOVE && app->dragging)
            {
                image_browser_pan(&app->state, (f64)event->position.x - app->drag_position.x,
                                  (f64)event->position.y - app->drag_position.y);
                app->drag_position = event->position;
                app->dirty = true;
            }
            else if (event->kind == WM_EVENT_BUTTON_RELEASE && event->key == WM_KEY_MOUSE_LEFT)
            {
                app->dragging = false;
            }
            else if (event->kind == WM_EVENT_BUTTON_PRESS)
            {
                if (event->key == WM_KEY_MOUSE_LEFT)
                {
                    app->dragging = true;
                    app->drag_position = event->position;
                }
                else if (event->key == WM_KEY_MOUSE_WHEEL_UP || event->key == WM_KEY_MOUSE_WHEEL_DOWN)
                {
                    image_browser_zoom(&app->state, event->key == WM_KEY_MOUSE_WHEEL_UP ? 1.25 : 0.8,
                                       event->position.x, event->position.y);
                    app->dirty = true;
                }
            }
            else if (event->kind == WM_EVENT_KEY_PRESS)
            {
                switch (event->key)
                {
                case WM_KEY_ESC: app->closing = true; break;
                case WM_KEY_RIGHT:
                case WM_KEY_PAGE_DOWN: result = image_browser_app_select(app, true); break;
                case WM_KEY_LEFT:
                case WM_KEY_PAGE_UP: result = image_browser_app_select(app, false); break;
                case WM_KEY_F: image_browser_fit(&app->state); app->dirty = true; break;
                case WM_KEY_1: image_browser_actual_size(&app->state); app->dirty = true; break;
                case WM_KEY_R:
                    app->dragging = false;
                    result = image_browser_request(&app->state, app->selected);
                    if (result)
                    {
                        image_browser_worker_set_generation(&app->worker, app->state.generation);
                        app->dirty = true;
                    }
                    break;
                case WM_KEY_PLUS:
                case WM_KEY_EQUAL:
                    image_browser_zoom(&app->state, 1.25, (f64)app->state.view.width * 0.5, (f64)app->state.view.height * 0.5);
                    app->dirty = true;
                    break;
                case WM_KEY_MINUS:
                    image_browser_zoom(&app->state, 0.8, (f64)app->state.view.width * 0.5, (f64)app->state.view.height * 0.5);
                    app->dirty = true;
                    break;
                default: break;
                }
            }
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool image_browser_app_worker(ImageBrowserApplication* app)
{
    bool result = true;
    ImageBrowserResult completed = {0};
    if (image_browser_worker_take(&app->worker, &completed))
    {
        result = image_browser_complete(&app->state, &completed);
        if (!result)
        {
            image_browser_result_release(&completed);
            fprintf(stderr, "image_browser: worker result identity mismatch\n");
        }
        else if (image_browser_publish(&app->state))
        {
            image_browser_app_metadata(app);
            app->dirty = true;
        }
    }
    ImageBrowserRequest request = {0};
    if (result && image_browser_begin_load(&app->state, &request))
    {
        result = image_browser_worker_submit(&app->worker, request, app->catalog.paths.pointer[request.file_index]);
        if (!result)
        {
            ImageBrowserResult rejected = {.request = request, .status = IMAGE_BROWSER_LOAD_READ_ERROR, .system_error = EIO};
            image_browser_complete(&app->state, &rejected);
            image_browser_publish(&app->state);
            fprintf(stderr, "image_browser: worker submission failed\n");
        }
    }
    return result;
}

#if BUSTER_INCLUDE_TESTS
BUSTER_GLOBAL_LOCAL bool image_browser_app_fixture_matches(ImageBrowserApplication const* app)
{
    // Independent handcrafted source/metadata oracle; server readback below
    // separately proves native presentation agrees with the raster canvas.
    static u8 const first_rgba[] = {
        255, 0, 0, 255, 0, 255, 0, 255,
        0, 0, 255, 255, 255, 255, 255, 255,
    };
    static u8 const second_rgba[] = {
        255, 255, 0, 255, 0, 255, 255, 255, 255, 0, 255, 255,
        12, 34, 56, 255, 100, 150, 200, 255, 255, 128, 0, 255,
    };
    ImageDecodeResult const* decoded = &app->state.published.decoded;
    ImageInformation const* info = &decoded->information;
    bool first = app->selected == app->catalog.initial_index;
    u32 width = first ? 2u : 3u;
    u8 const* expected = first ? first_rgba : second_rgba;
    u64 expected_size = first ? sizeof(first_rgba) : sizeof(second_rgba);
    bool result = app->catalog.paths.length >= 2 &&
                  decoded->status == IMAGE_DECODE_SUCCESS &&
                  info->format == IMAGE_FORMAT_PNM && info->width == width && info->height == 2 &&
                  info->source_channel_count == 3 && info->source_bits_per_channel == 8 && !info->has_alpha &&
                  info->orientation == IMAGE_ORIENTATION_TOP_LEFT && info->frame_count == 1 &&
                  !info->is_animated && !info->has_more_images && info->color_metadata_flags == 0 &&
                  info->source_color_model == IMAGE_COLOR_MODEL_RGB &&
                  decoded->image.width == width && decoded->image.height == 2 && decoded->image.stride == width * 4u &&
                  decoded->image.pixels.pointer && decoded->image.pixels.length == expected_size;
    if (result)
    {
        result = memcmp(decoded->image.pixels.pointer, expected, (size_t)expected_size) == 0;
    }
    return result;
}
#endif

BUSTER_GLOBAL_LOCAL bool image_browser_app_render(ImageBrowserApplication* app)
{
    WmRect geometry = wm_window_get_framebuffer_rect(app->wm, app->window);
    u32 width = geometry.x1;
    u32 height = geometry.y1;
    bool result = width && height && width <= BUSTER_RASTER_MAX_DIMENSION && height <= BUSTER_RASTER_MAX_DIMENSION;
    if (!result)
    {
        fprintf(stderr, "image_browser: invalid/unavailable native drawable or canvas exceeds 4096x4096\n");
    }
    if (result)
    {
        image_browser_resize(&app->state, width, height);
        arena_reset_to_start(app->canvas_arena);
        u64 bytes = (u64)width * height * 4u;
        RenderingRasterCanvas canvas = {
            .pixels = {.pointer = arena_allocate(app->canvas_arena, u8, bytes), .length = bytes},
            .width = width, .height = height, .stride = width * 4u,
        };
        RenderingRasterSource source = {0};
        u32 display_width = 0;
        u32 display_height = 0;
        if (!image_browser_app_loading(app) && app->state.published.status == IMAGE_BROWSER_LOAD_SUCCESS)
        {
            Image const* image = &app->state.published.decoded.image;
            source = (RenderingRasterSource){
                .pixels = image->pixels, .width = image->width, .height = image->height, .stride = image->stride,
                .orientation = (u32)app->state.published.decoded.information.orientation,
            };
            image_browser_display_dimensions(&app->state, &display_width, &display_height);
        }
        RenderingRasterView view = {
            .x = (f64)width * 0.5 + app->state.view.pan_x - (f64)display_width * app->state.view.scale * 0.5,
            .y = (f64)height * 0.5 + app->state.view.pan_y - (f64)display_height * app->state.view.scale * 0.5,
            .zoom = app->state.view.scale,
        };
        result = rendering_raster_draw(canvas, source, view) && rendering_raster_present(&app->presenter, canvas);
        if (!result)
        {
            fprintf(stderr, "image_browser: raster draw or native presentation failed\n");
        }
#if BUSTER_INCLUDE_TESTS
        if (result && image_browser_program.smoke && !image_browser_app_loading(app))
        {
            result = app->state.published.status == IMAGE_BROWSER_LOAD_SUCCESS && image_browser_app_fixture_matches(app);
            if (result)
            {
                result = rendering_raster_readback_matches_for_test(&app->presenter, canvas);
            }
            if (!result)
            {
                fprintf(stderr, "image_browser: native smoke independent fixture/readback failed\n");
            }
        }
#endif
    }
    return result;
}

#if BUSTER_INCLUDE_TESTS
// Test-only native injection uses the existing XCB connection and selected
// key map. Every control reaches the server and the regular bounded event poll;
// no smoke step mutates application selection or its viewport directly.
typedef struct ImageBrowserSmokeNative ImageBrowserSmokeNative;
struct ImageBrowserSmokeNative
{
    xcb_connection_t* connection;
    xcb_window_t window;
    xcb_window_t root;
};

BUSTER_GLOBAL_LOCAL bool image_browser_smoke_native(ImageBrowserApplication const* app, ImageBrowserSmokeNative* native)
{
    WmNativeSurface surface = app->presenter.surface;
    *native = (ImageBrowserSmokeNative){0};
    bool result = surface.kind == WM_NATIVE_SURFACE_XCB && surface.display && surface.window && (u64)surface.window <= UINT32_MAX;
    if (result)
    {
        native->connection = (xcb_connection_t*)surface.display;
        native->window = (xcb_window_t)(u64)surface.window;
        xcb_generic_error_t* error = 0;
        xcb_get_geometry_reply_t* geometry = xcb_get_geometry_reply(
            native->connection, xcb_get_geometry(native->connection, native->window), &error);
        result = geometry && !error && !xcb_connection_has_error(native->connection);
        if (result)
        {
            native->root = geometry->root;
        }
        free(error);
        free(geometry);
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool image_browser_smoke_send(ImageBrowserSmokeNative native, u32 mask, void const* event)
{
    xcb_generic_error_t* error = xcb_request_check(native.connection,
        xcb_send_event_checked(native.connection, 0, native.window, mask, (char const*)event));
    bool result = !error && !xcb_connection_has_error(native.connection);
    free(error);
    if (result)
    {
        result = xcb_flush(native.connection) > 0;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool image_browser_smoke_key(ImageBrowserSmokeNative native, xcb_keysym_t keysym)
{
    xcb_key_symbols_t* symbols = xcb_key_symbols_alloc(native.connection);
    xcb_keycode_t* keycodes = symbols ? xcb_key_symbols_get_keycode(symbols, keysym) : 0;
    xcb_keycode_t keycode = 0;
    if (keycodes)
    {
        for (xcb_keycode_t* at = keycodes; *at && !keycode; ++at)
        {
            if (xcb_key_symbols_get_keysym(symbols, *at, 0) == keysym)
            {
                keycode = *at;
            }
        }
    }
    bool result = keycode != 0;
    if (result)
    {
        xcb_key_press_event_t event = {
            .response_type = XCB_KEY_PRESS, .detail = keycode, .time = XCB_CURRENT_TIME,
            .root = native.root, .event = native.window, .same_screen = 1,
        };
        result = image_browser_smoke_send(native, XCB_EVENT_MASK_KEY_PRESS, &event);
        if (result)
        {
            event.response_type = XCB_KEY_RELEASE;
            result = image_browser_smoke_send(native, XCB_EVENT_MASK_KEY_RELEASE, &event);
        }
    }
    free(keycodes);
    if (symbols)
    {
        xcb_key_symbols_free(symbols);
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool image_browser_smoke_button(ImageBrowserSmokeNative native, u8 button, bool press, s16 x, s16 y)
{
    xcb_button_press_event_t event = {
        .response_type = press ? XCB_BUTTON_PRESS : XCB_BUTTON_RELEASE, .detail = button,
        .time = XCB_CURRENT_TIME, .root = native.root, .event = native.window,
        .root_x = x, .root_y = y, .event_x = x, .event_y = y, .same_screen = 1,
        .state = (!press && button == 1) ? XCB_KEY_BUT_MASK_BUTTON_1 : 0,
    };
    bool result = image_browser_smoke_send(native, press ? XCB_EVENT_MASK_BUTTON_PRESS : XCB_EVENT_MASK_BUTTON_RELEASE, &event);
    return result;
}

BUSTER_GLOBAL_LOCAL bool image_browser_smoke_viewport(ImageBrowserSmokeNative native)
{
    // Keysym 0x31 is '1' in the actual server map; then wheel-up and one drag.
    bool result = image_browser_smoke_key(native, UINT32_C(0x31)) &&
                  image_browser_smoke_button(native, 4, true, 16, 12) &&
                  image_browser_smoke_button(native, 4, false, 16, 12) &&
                  image_browser_smoke_button(native, 1, true, 32, 24);
    if (result)
    {
        xcb_motion_notify_event_t motion = {
            .response_type = XCB_MOTION_NOTIFY, .detail = XCB_MOTION_NORMAL, .time = XCB_CURRENT_TIME,
            .root = native.root, .event = native.window,
            .root_x = 39, .root_y = 19, .event_x = 39, .event_y = 19,
            .state = XCB_KEY_BUT_MASK_BUTTON_1, .same_screen = 1,
        };
        result = image_browser_smoke_send(native, XCB_EVENT_MASK_POINTER_MOTION, &motion);
    }
    if (result)
    {
        result = image_browser_smoke_button(native, 1, false, 39, 19);
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool image_browser_smoke_close(ImageBrowserSmokeNative native)
{
    xcb_generic_error_t* protocols_error = 0;
    xcb_generic_error_t* delete_error = 0;
    xcb_intern_atom_reply_t* protocols = xcb_intern_atom_reply(native.connection,
        xcb_intern_atom(native.connection, 0, 12, "WM_PROTOCOLS"), &protocols_error);
    xcb_intern_atom_reply_t* deletion = xcb_intern_atom_reply(native.connection,
        xcb_intern_atom(native.connection, 0, 16, "WM_DELETE_WINDOW"), &delete_error);
    bool result = protocols && deletion && !protocols_error && !delete_error && !xcb_connection_has_error(native.connection);
    if (result)
    {
        xcb_client_message_event_t event = {
            .response_type = XCB_CLIENT_MESSAGE, .format = 32, .window = native.window, .type = protocols->atom,
        };
        event.data.data32[0] = deletion->atom;
        event.data.data32[1] = XCB_CURRENT_TIME;
        result = image_browser_smoke_send(native, XCB_EVENT_MASK_NO_EVENT, &event);
    }
    free(protocols_error);
    free(delete_error);
    free(protocols);
    free(deletion);
    return result;
}

BUSTER_GLOBAL_LOCAL bool image_browser_app_smoke(ImageBrowserApplication* app)
{
    bool result = true;
    if (!image_browser_app_loading(app) && app->state.published.status == IMAGE_BROWSER_LOAD_SUCCESS)
    {
        ImageBrowserSmokeNative native = {0};
        if (app->smoke_phase == 0)
        {
            app->smoke_generation = app->state.generation;
            // Keysym 0xff53 is Right; phase advances only after checked send.
            result = image_browser_smoke_native(app, &native) &&
                     image_browser_smoke_key(native, UINT32_C(0xff53));
            if (result)
            {
                app->smoke_phase = 1;
            }
        }
        else if (app->smoke_phase == 1 && app->state.generation > app->smoke_generation &&
                 app->state.published.request.generation == app->state.generation &&
                 app->selected != app->catalog.initial_index)
        {
            result = image_browser_smoke_native(app, &native) && image_browser_smoke_viewport(native);
            if (result)
            {
                app->smoke_phase = 2;
            }
        }
        else if (app->smoke_phase == 2 && !app->dragging)
        {
            f64 expected_x = ((f64)app->state.view.width * 0.5 - 16.0) * 0.25 + 7.0;
            f64 expected_y = ((f64)app->state.view.height * 0.5 - 12.0) * 0.25 - 5.0;
            if (app->state.view.scale == 1.25 && app->state.view.pan_x == expected_x && app->state.view.pan_y == expected_y)
            {
                // Called only after this frame's independent fixture oracle and
                // server readback. Completion requires the ensuing native close.
                result = image_browser_smoke_native(app, &native) && image_browser_smoke_close(native);
                if (result)
                {
                    app->smoke_phase = 3;
                }
            }
        }
    }
    if (!result)
    {
        fprintf(stderr, "image_browser: native smoke checked input injection failed (phase=%u)\n", app->smoke_phase);
    }
    return result;
}
#endif

BUSTER_GLOBAL_LOCAL bool image_browser_run(void)
{
    ImageBrowserApplication app = {0};
    image_browser_state_initialize(&app.state, image_browser_program.smoke ? 128u : 1000u, image_browser_program.smoke ? 96u : 768u);
    s32 error = 0;
    bool result = image_browser_catalog_open(image_browser_program.input_path, &app.catalog, &error);
    if (!result)
    {
        fprintf(stderr, "image_browser: cannot create bounded image catalogue (system=%d)\n", error);
    }
    if (result)
    {
        app.selected = app.catalog.initial_index;
        app.events = arena_create((ArenaCreation){
            .reserved_size = IMAGE_BROWSER_EVENT_BYTES, .initial_size = IMAGE_BROWSER_EVENT_BYTES, .flags = {.no_pool = 1},
        });
        u64 canvas_reservation = BUSTER_RASTER_MAX_CANVAS_BYTES + IMAGE_BROWSER_ARENA_OVERHEAD;
        app.canvas_arena = arena_create((ArenaCreation){
            .reserved_size = canvas_reservation, .initial_size = canvas_reservation, .flags = {.no_pool = 1},
        });
        result = app.events && app.canvas_arena;
        if (!result)
        {
            fprintf(stderr, "image_browser: event/canvas arena reservation or commitment failed\n");
        }
    }
    if (result)
    {
        app.wm = wm_initialize();
        result = app.wm != 0;
        if (!result)
        {
            fprintf(stderr, "image_browser: cannot initialize native XCB windowing\n");
        }
    }
    if (result)
    {
        app.window = wm_window_create(app.wm, (WmWindowCreate){
            .name = S8("Buster Image Browser"), .disable_file_drop = true,
            .size = {.width = (WmUnit)app.state.view.width, .height = (WmUnit)app.state.view.height},
        });
        result = app.window && rendering_raster_initialize(&app.presenter, wm_window_get_native_surface(app.wm, app.window));
        if (!result)
        {
            fprintf(stderr, "image_browser: native window or supported XCB TrueColor visual unavailable\n");
        }
    }
    if (result)
    {
        result = image_browser_worker_start(&app.worker, &error);
        app.worker_started = result;
        if (!result)
        {
            fprintf(stderr, "image_browser: cannot start loader worker (system=%d)\n", error);
        }
    }
    if (result)
    {
        result = image_browser_request(&app.state, app.selected);
        image_browser_worker_set_generation(&app.worker, app.state.generation);
        app.dirty = true;
    }
    bool clock_valid = false;
    u64 start = image_browser_clock_ms(&clock_valid);
    if (result && !clock_valid)
    {
        fprintf(stderr, "image_browser: monotonic clock unavailable\n");
        result = false;
    }
    while (result && !app.closing)
    {
        arena_reset_to_start(app.events);
        WmEventList events = {0};
        result = wm_poll_events_bounded(app.events, app.wm, 32, &events);
        if (!result)
        {
            fprintf(stderr, "image_browser: native event poll capacity/connection failure\n");
        }
        if (result)
        {
            result = image_browser_app_dispatch(&app, events);
        }
        if (result && !app.closing)
        {
            result = image_browser_app_worker(&app);
        }
        u64 now = image_browser_clock_ms(&clock_valid);
        if (result && !clock_valid)
        {
            fprintf(stderr, "image_browser: monotonic clock failed\n");
            result = false;
        }
        if (result && !app.closing && (app.dirty || now - app.refresh_time >= IMAGE_BROWSER_REFRESH_MS))
        {
            bool update_title = app.dirty;
            app.dirty = false;
            result = image_browser_app_render(&app);
            if (result && update_title)
            {
                result = image_browser_app_title(&app);
                if (!result)
                {
                    fprintf(stderr, "image_browser: native title update failed\n");
                }
            }
            app.refresh_time = now;
#if BUSTER_INCLUDE_TESTS
            if (result && image_browser_program.smoke)
            {
                result = image_browser_app_smoke(&app);
            }
#endif
        }
#if BUSTER_INCLUDE_TESTS
        if (result && image_browser_program.smoke && !app.smoke_complete && now - start >= IMAGE_BROWSER_SMOKE_TIMEOUT_MS)
        {
            fprintf(stderr, "image_browser: native smoke timeout after 10 seconds\n");
            result = false;
        }
#endif
        if (result && !app.closing)
        {
            struct timespec delay = {.tv_sec = 0, .tv_nsec = 10000000};
            if (nanosleep(&delay, 0) != 0 && errno != EINTR)
            {
                fprintf(stderr, "image_browser: idle wait failed\n");
                result = false;
            }
        }
    }

    image_browser_shutdown(&app.state);
    if (app.worker_started)
    {
        ImageBrowserResult joined = {0};
        bool has_joined = false;
        bool join_success = image_browser_worker_stop_join(&app.worker, &joined, &has_joined);
        if (!join_success)
        {
            join_success = image_browser_worker_stop_join(&app.worker, &joined, &has_joined);
        }
        if (!join_success)
        {
            // Returning would release the stack containing the still-live
            // worker and then free its borrowed catalogue paths. Fail-stop
            // before any state, presentation, catalogue or arena teardown.
            os_fail_message_raw(S8("image_browser: loader worker join failed twice; live ownership cannot be released"));
        }
        if (has_joined)
        {
            if (!image_browser_complete(&app.state, &joined))
            {
                image_browser_result_release(&joined);
                fprintf(stderr, "image_browser: joined result identity mismatch\n");
                result = false;
            }
        }
        else if (app.state.has_active)
        {
            fprintf(stderr, "image_browser: active load missing its joined result\n");
            result = false;
        }
    }
    if (!image_browser_state_destroy(&app.state))
    {
        fprintf(stderr, "image_browser: active worker ownership remains at shutdown\n");
        result = false;
    }
    if (!rendering_raster_deinitialize(&app.presenter))
    {
        fprintf(stderr, "image_browser: native presenter deinitialization failed\n");
        result = false;
    }
    if (app.wm)
    {
        wm_deinitialize(app.wm);
    }
    if (app.canvas_arena)
    {
        arena_destroy(app.canvas_arena, 1);
    }
    if (app.events)
    {
        arena_destroy(app.events, 1);
    }
    image_browser_catalog_destroy(&app.catalog);
#if BUSTER_INCLUDE_TESTS
    if (image_browser_program.smoke)
    {
        result = result && app.smoke_complete;
        fprintf(stderr, result ? "image_browser native smoke: PASS (independent decode oracle, native Right/actual-size/wheel/drag/WM_DELETE_WINDOW dispatch, present/server readback, joined shutdown)\n" :
                                 "image_browser native smoke: FAIL\n");
    }
#else
    BUSTER_UNUSED(start);
#endif
    return result;
}

ProcessResult entry_point(void)
{
    ProcessResult result = PROCESS_RESULT_SUCCESS;
    if (image_browser_program.help)
    {
        image_browser_usage();
    }
    else
    {
        result = image_browser_run() ? PROCESS_RESULT_SUCCESS : PROCESS_RESULT_FAILED;
    }
    return result;
}
