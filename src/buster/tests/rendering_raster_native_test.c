// Actual Linux/XCB raster readback and wm lifecycle validation. These checks
// require an admitted real X server (Xvfb is sufficient). Missing DISPLAY is a
// separate failure-path invocation, never a native-rendering pass.
// Synthetic pixels are first-party test data; no external assets are read.
#include <buster/lib/system_headers.h>
#include <buster/lib/os.h>
#include <buster/lib/arena.h>
#include <buster/lib/window.h>
#include <buster/lib/window/internal.h>
#include <buster/lib/rendering/raster_internal.h>
#include <stdio.h>

BUSTER_V_IMPL OsState os_state;
BUSTER_GLOBAL_LOCAL ProgramState raster_native_program;
BUSTER_V_IMPL ProgramState* program_state = &raster_native_program;
BUSTER_GLOBAL_LOCAL u32 raster_native_assertions;
BUSTER_GLOBAL_LOCAL u32 raster_native_failures;

#if BUSTER_UNITY_BUILD
#include <buster/lib/arena.c>
#include <buster/lib/integer.c>
#include <buster/lib/string.c>
#include <buster/lib/os.c>
#include <buster/lib/file.c>
#include <buster/lib/hash.c>
#include <buster/lib/time.c>
#include <buster/lib/float.c>
#include <buster/lib/window.c>
#include <buster/lib/rendering_raster.c>
#endif

BUSTER_GLOBAL_LOCAL void raster_native_check(bool condition, char const* description)
{
    raster_native_assertions += 1;
    if (!condition)
    {
        raster_native_failures += 1;
        printf("FAIL: %s\n", description);
    }
}

BUSTER_GLOBAL_LOCAL bool raster_native_close_message(WmNativeSurface surface)
{
    xcb_connection_t* connection = (xcb_connection_t*)surface.display;
    xcb_window_t window = (xcb_window_t)(uintptr_t)surface.window;
    xcb_generic_error_t* error = 0;
    xcb_intern_atom_reply_t* protocols = xcb_intern_atom_reply(connection, xcb_intern_atom(connection, 0, 12, "WM_PROTOCOLS"), &error);
    bool result = protocols != 0 && error == 0;
    free(error);
    error = 0;
    xcb_intern_atom_reply_t* close = xcb_intern_atom_reply(connection, xcb_intern_atom(connection, 0, 16, "WM_DELETE_WINDOW"), &error);
    result = result && close != 0 && error == 0;
    free(error);
    error = 0;
    if (result)
    {
        xcb_client_message_event_t message = {0};
        message.response_type = XCB_CLIENT_MESSAGE;
        message.format = 32;
        message.window = window;
        message.type = protocols->atom;
        message.data.data32[0] = close->atom;
        message.data.data32[1] = XCB_CURRENT_TIME;
        error = xcb_request_check(connection, xcb_send_event_checked(connection, 0, window, XCB_EVENT_MASK_NO_EVENT, (char const*)&message));
        result = error == 0 && !xcb_connection_has_error(connection);
        free(error);
    }
    free(protocols);
    free(close);
    return result;
}

BUSTER_GLOBAL_LOCAL xcb_atom_t raster_native_atom(xcb_connection_t* connection, char const* name)
{
    xcb_generic_error_t* error = 0;
    xcb_intern_atom_reply_t* reply = xcb_intern_atom_reply(connection, xcb_intern_atom(connection, 0, (u16)strlen(name), name), &error);
    xcb_atom_t result = reply && !error ? reply->atom : XCB_ATOM_NONE;
    free(reply);
    free(error);
    return result;
}

BUSTER_GLOBAL_LOCAL void raster_native_file_drop_opt_out(Arena* arena, WmHandle* windowing, WmWindowHandle* window, WmNativeSurface surface, bool disabled)
{
    xcb_connection_t* connection = (xcb_connection_t*)surface.display;
    xcb_window_t native_window = (xcb_window_t)(uintptr_t)surface.window;
    xcb_atom_t aware = raster_native_atom(connection, "XdndAware");
    raster_native_check(aware != XCB_ATOM_NONE, "native drop advertisement atom available");
    xcb_generic_error_t* error = 0;
    xcb_get_property_reply_t* reply = xcb_get_property_reply(connection,
        xcb_get_property(connection, 0, native_window, aware, XCB_GET_PROPERTY_TYPE_ANY, 0, 1), &error);
    if (disabled)
    {
        raster_native_check(reply && !error && reply->type == XCB_ATOM_NONE && xcb_get_property_value_length(reply) == 0,
                            "opted-out window has no XdndAware advertisement");
    }
    else
    {
        raster_native_check(reply && !error && reply->type == XCB_ATOM_ATOM && reply->format == 32 &&
                            xcb_get_property_value_length(reply) == 4, "default window keeps native drop advertisement");
    }
    free(reply);
    free(error);
    if (disabled)
    {
        xcb_atom_t enter = raster_native_atom(connection, "XdndEnter");
        xcb_atom_t drop = raster_native_atom(connection, "XdndDrop");
        xcb_atom_t type_list = raster_native_atom(connection, "XdndTypeList");
        xcb_atom_t uri_list = raster_native_atom(connection, "text/uri-list");
        bool atoms_valid = enter != XCB_ATOM_NONE && drop != XCB_ATOM_NONE &&
                           type_list != XCB_ATOM_NONE && uri_list != XCB_ATOM_NONE;
        raster_native_check(atoms_valid, "native drop test atoms available");
        if (atoms_valid)
        {
            // This first-party same-window source advertises URI through a property.
            // Enter would read it and begin a transaction without the opt-out gate.
            error = xcb_request_check(connection, xcb_change_property_checked(connection,
                XCB_PROP_MODE_REPLACE, native_window, type_list, XCB_ATOM_ATOM, 32, 1, &uri_list));
            raster_native_check(error == 0, "native source type property created");
            free(error);
            xcb_client_message_event_t message = {0};
            message.response_type = XCB_CLIENT_MESSAGE;
            message.format = 32;
            message.window = native_window;
            message.type = enter;
            message.data.data32[0] = native_window;
            message.data.data32[1] = (5u << 24) | 1u;
            error = xcb_request_check(connection, xcb_send_event_checked(connection, 0,
                native_window, XCB_EVENT_MASK_NO_EVENT, (char const*)&message));
            raster_native_check(error == 0, "addressed native drop enter queued");
            free(error);
            WmEventList events = wm_poll_events(arena, windowing);
            bool emitted_drop = false;
            for (WmEvent* event = events.first; event; event = event->next)
            {
                emitted_drop = emitted_drop || (event->kind == WM_EVENT_FILE_DROP && event->window == window);
            }
            raster_native_check(!emitted_drop && !windowing->xdnd_active && !windowing->xdnd_drop_pending &&
                                windowing->xdnd_transfer_data.length == 0, "opt-out ignores enter before transaction or payload acquisition");
            arena_reset_to_start(arena);
            message.type = drop;
            message.data.data32[1] = 0;
            message.data.data32[2] = XCB_CURRENT_TIME;
            error = xcb_request_check(connection, xcb_send_event_checked(connection, 0,
                native_window, XCB_EVENT_MASK_NO_EVENT, (char const*)&message));
            raster_native_check(error == 0, "addressed native drop queued");
            free(error);
            events = wm_poll_events(arena, windowing);
            emitted_drop = false;
            for (WmEvent* event = events.first; event; event = event->next)
            {
                emitted_drop = emitted_drop || (event->kind == WM_EVENT_FILE_DROP && event->window == window);
            }
            raster_native_check(!emitted_drop && !windowing->xdnd_active && !windowing->xdnd_drop_pending &&
                                windowing->xdnd_transfer_data.length == 0, "opt-out ignores drop before selection conversion");
            arena_reset_to_start(arena);
        }
    }
}

BUSTER_GLOBAL_LOCAL void raster_native_bounded_poll(Arena* arena, WmHandle* windowing, WmWindowHandle* window, WmNativeSurface surface)
{
    // Drain startup events before submitting two distinct native pointer moves.
    BUSTER_UNUSED(wm_poll_events(arena, windowing));
    arena_reset_to_start(arena);
    xcb_connection_t* connection = (xcb_connection_t*)surface.display;
    for (u32 index = 0; index < 2; index += 1)
    {
        xcb_motion_notify_event_t motion = {0};
        motion.response_type = XCB_MOTION_NOTIFY;
        motion.event = (xcb_window_t)(uintptr_t)surface.window;
        motion.event_x = (s16)(7u + index * 4u);
        motion.event_y = 9;
        motion.same_screen = 1;
        xcb_generic_error_t* error = xcb_request_check(connection,
            xcb_send_event_checked(connection, 0, motion.event, XCB_EVENT_MASK_NO_EVENT, (char const*)&motion));
        raster_native_check(error == 0, "two native motions queued");
        free(error);
    }
    u32 observed = 0;
    for (u32 poll = 0; poll < 128 && observed < 2; poll += 1)
    {
        WmEventList events = {0};
        bool admitted = wm_poll_events_bounded(arena, windowing, 1, &events);
        raster_native_check(admitted, "one-native-event bounded poll admitted");
        raster_native_check(events.count <= 1, "bounded motion batch does not drain both native events");
        for (WmEvent* event = events.first; event; event = event->next)
        {
            if (event->kind == WM_EVENT_MOUSE_MOVE && event->window == window)
            {
                raster_native_check(event->position.x == (s16)(7u + observed * 4u) && event->position.y == 9, "queued native motion preserved in order");
                observed += 1;
            }
        }
        arena_reset_to_start(arena);
    }
    raster_native_check(observed == 2, "superseding poll preserves remaining native event");
    WmEventList invalid = {0};
    raster_native_check(!wm_poll_events_bounded(arena, windowing, 0, &invalid) && invalid.count == 0, "zero native poll bound refused");
    raster_native_check(!wm_poll_events_bounded(arena, windowing, 33, &invalid) && invalid.count == 0, "large native poll bound refused");
    Arena* small = arena_create((ArenaCreation){.reserved_size = BUSTER_KB(64), .initial_size = BUSTER_KB(64), .flags = {.no_pool = true}});
    raster_native_check(!wm_poll_events_bounded(small, windowing, 1, &invalid) && invalid.count == 0, "insufficient precommitted poll arena refused");
    arena_destroy(small, 1);
}

BUSTER_GLOBAL_LOCAL void raster_native_cycle(Arena* arena, u32 cycle)
{
    WmHandle* windowing = wm_initialize();
    raster_native_check(windowing != 0, "real wm initialization");
    if (windowing)
    {
        WmWindowHandle* window = wm_window_create(windowing, (WmWindowCreate){
            .name = S8("Buster raster native validation"),
            .size = {.width = 32, .height = 24},
            .disable_file_drop = cycle != 0,
        });
        raster_native_check(window != 0, "real wm window creation");
        if (window)
        {
            WmNativeSurface surface = wm_window_get_native_surface(windowing, window);
            raster_native_check(wm_window_set_title(windowing, window, S8("Buster raster metadata")), "checked native title update");
            xcb_connection_t* title_connection = (xcb_connection_t*)surface.display;
            xcb_get_property_reply_t* title_reply = xcb_get_property_reply(title_connection,
                xcb_get_property(title_connection, 0, (xcb_window_t)(uintptr_t)surface.window, XCB_ATOM_WM_NAME, XCB_ATOM_STRING, 0, 64), 0);
            raster_native_check(title_reply && xcb_get_property_value_length(title_reply) == 22 &&
                                memcmp(xcb_get_property_value(title_reply), "Buster raster metadata", 22) == 0, "native title property readback");
            free(title_reply);
            raster_native_check(!wm_window_set_title(windowing, window, (String8){.pointer = "bad\0title", .length = 9}), "embedded NUL title refused");
            RenderingRasterPresenter presenter = {0};
            bool initialized = rendering_raster_initialize(&presenter, surface);
            raster_native_check(initialized, "native visual admission and graphics context creation");
            if (initialized)
            {
                u8 source_pixels[24] = {
                    255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255,
                    255, 255, 0, 255, 255, 0, 255, 128, 0, 255, 255, 0,
                };
                u8 canvas_pixels[48 * 40 * 4];
                RenderingRasterSource source = {
                    .pixels = {source_pixels, sizeof(source_pixels)}, .width = 3, .height = 2, .stride = 12, .orientation = 1,
                };
                RenderingRasterCanvas canvas = {
                    .pixels = {canvas_pixels, sizeof(canvas_pixels)}, .width = 32, .height = 24, .stride = 32 * 4,
                };
                for (u32 orientation = 1; orientation <= 8; orientation += 1)
                {
                    source.orientation = orientation;
                    RenderingRasterView view = {.x = 3.0, .y = 4.0, .zoom = 4.0};
                    raster_native_check(rendering_raster_draw(canvas, source, view), "patterned orientation draw");
                    raster_native_check(rendering_raster_present(&presenter, canvas), "real patterned image presentation");
                    raster_native_check(rendering_raster_readback_matches_for_test(&presenter, canvas), "actual server pixels equal opaque canvas");
                }

                raster_native_bounded_poll(arena, windowing, window, surface);
                raster_native_file_drop_opt_out(arena, windowing, window, surface, cycle != 0);
                xcb_connection_t* connection = (xcb_connection_t*)surface.display;
                u32 values[] = {48, 40};
                xcb_generic_error_t* error = xcb_request_check(connection,
                    xcb_configure_window_checked(connection, (xcb_window_t)(uintptr_t)surface.window, XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT, values));
                raster_native_check(error == 0, "native resize request");
                free(error);
                WmRect size = wm_window_get_framebuffer_rect(windowing, window);
                raster_native_check(size.x1 == 48 && size.y1 == 40, "actual resized framebuffer query");
                WmEventList events = wm_poll_events(arena, windowing);
                bool resized = false;
                for (WmEvent* event = events.first; event; event = event->next)
                {
                    resized = resized || (event->kind == WM_EVENT_WINDOW_RESIZE && event->window == window &&
                                           event->position.width == 48 && event->position.height == 40);
                }
                raster_native_check(resized, "native resize event translation");
                arena_reset_to_start(arena);
                canvas.width = 48;
                canvas.height = 40;
                canvas.stride = 48 * 4;
                RenderingRasterView panned = {.x = -2.0 - cycle, .y = 7.0, .zoom = 5.0};
                raster_native_check(rendering_raster_draw(canvas, source, panned), "resized panned draw");
                raster_native_check(rendering_raster_present(&presenter, canvas), "resized panned native presentation");
                raster_native_check(rendering_raster_readback_matches_for_test(&presenter, canvas), "resized actual server pixels");

                raster_native_check(raster_native_close_message(surface), "native close protocol sent");
                events = wm_poll_events(arena, windowing);
                bool closed = false;
                for (WmEvent* event = events.first; event; event = event->next)
                {
                    closed = closed || (event->kind == WM_EVENT_WINDOW_CLOSE && event->window == window);
                }
                raster_native_check(closed, "native close event translation");
                arena_reset_to_start(arena);
                raster_native_check(rendering_raster_deinitialize(&presenter), "native graphics context release");
                raster_native_check(rendering_raster_deinitialize(&presenter), "repeated presenter shutdown");
            }
        }
        wm_deinitialize(windowing);
        wm_deinitialize(windowing);
        raster_native_check(true, "repeated wm shutdown returned");
    }
}

int main(int argc, char* argv[])
{
    os_state.page_size = os_get_page_size();
    os_state.allocation_granularity = os_state.page_size;
    os_state.large_page_size = BUSTER_MB(2);
    ThreadContext* context = thread_context_allocate();
    thread_context_select(context);
    Arena* arena = arena_create((ArenaCreation){.reserved_size = BUSTER_MB(32), .initial_size = BUSTER_MB(32), .flags = {.no_pool = true}});
    program_state->arena = arena;
    bool failure_mode = argc == 2 && strcmp(argv[1], "--no-display") == 0;
    if (failure_mode)
    {
        WmHandle* windowing = wm_initialize();
        raster_native_check(windowing == 0, "unavailable XCB connection is rejected");
        if (windowing)
        {
            wm_deinitialize(windowing);
        }
        wm_deinitialize(0);
    }
    else
    {
        for (u32 cycle = 0; cycle < 3; cycle += 1)
        {
            raster_native_cycle(arena, cycle);
        }
    }
    printf("rendering_raster_native_tests: %u/%u assertions passed; mode=%s\n",
           (unsigned)(raster_native_assertions - raster_native_failures), (unsigned)raster_native_assertions,
           failure_mode ? "unavailable-display" : "actual-xcb-readback");
    thread_context_release(context);
    arena_destroy(arena, 1);
    program_state->arena = 0;
    return raster_native_failures != 0;
}
