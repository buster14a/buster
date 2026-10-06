// Actual Linux/XCB raster readback and wm lifecycle validation. These checks
// require an admitted real X server (Xvfb is sufficient). Missing DISPLAY is a
// separate failure-path invocation, never a native-rendering pass.
// Synthetic pixels are first-party test data; no external assets are read.
#include <buster/lib/system_headers.h>
#include <buster/lib/os.h>
#include <buster/lib/arena.h>
#include <buster/lib/window.h>
#include <buster/lib/rendering/raster_internal.h>
#include <xcb-imdkit/imdkit.h>
#include <stdio.h>

BUSTER_V_IMPL OsState os_state;
BUSTER_GLOBAL_LOCAL ProgramState raster_native_program;
BUSTER_V_IMPL ProgramState* program_state = &raster_native_program;
BUSTER_GLOBAL_LOCAL u32 raster_native_assertions;
BUSTER_GLOBAL_LOCAL u32 raster_native_failures;
BUSTER_GLOBAL_LOCAL u32 raster_native_campaign;
BUSTER_GLOBAL_LOCAL u32 raster_native_cycles;
BUSTER_GLOBAL_LOCAL u32 raster_native_encodings;

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

// Owning-layer state assertions follow the unity implementation include so
// internal linkage declarations keep the same scope in both build modes.
#include <buster/lib/window/internal.h>

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

BUSTER_GLOBAL_LOCAL void raster_native_window_arena_failure(void)
{
    // XDND staging is lazy; the first native arena is now the required window
    // arena. Empty the reuse pool so the reservation fault reaches that owner.
    BUSTER_UNUSED(arena_pool_release_thread());
    arena_test_fail_next_reserve();
    WmHandle* windowing = wm_initialize();
    raster_native_check(windowing == 0, "required window arena failure rejects native initialization");
    if (windowing)
    {
        wm_deinitialize(windowing);
    }
    wm_deinitialize(0);
    BUSTER_UNUSED(arena_pool_release_thread());
}

BUSTER_GLOBAL_LOCAL void raster_native_xim_scope(Arena* arena, WmHandle* windowing, WmWindowHandle* window)
{
    // This drives the callback reducer with project-authored UTF-8, separately
    // from a live provider. Native polling below checks scope teardown.
    char8 text[16385];
    memset(text, 'a', sizeof(text));
    u64 start = arena->position;
    raster_native_check(!wm_x11_xim_commit_for_test(windowing, window, 1, S8("a")) && arena->position == start,
                        "XIM callback outside poll cannot retain the caller arena");
    windowing->event_arena = arena;
    windowing->event_list = (WmEventList){0};
    windowing->poll_arena = arena;
    windowing->poll_event_list = &windowing->event_list;
    windowing->poll_commit_bytes = 0;
    windowing->poll_commit_count = 0;
    raster_native_check(!wm_x11_xim_commit_for_test(windowing, window, 4097, S8("a")) && arena->position == start,
                        "XIM oversized raw input rejected before conversion or allocation");
    raster_native_check(!wm_x11_xim_commit_for_test(windowing, window, 0, S8("a")) && arena->position == start,
                        "XIM empty raw input cannot publish output");
    raster_native_check(!wm_x11_xim_commit_for_test(windowing, window, 1, (String8){.length = 1}) && arena->position == start,
                        "XIM null conversion output rejected without arena mutation");
    raster_native_check(!wm_x11_xim_commit_for_test(windowing, window, 2, S8("\xc0\xaf")) && arena->position == start,
                        "XIM malformed UTF-8 rejected before event publication");
    raster_native_check(!wm_x11_xim_commit_for_test(windowing, window, 4096, (String8){text, sizeof(text)}) && arena->position == start,
                        "XIM oversized converted output rejected without arena mutation");
    raster_native_check(wm_x11_xim_commit_for_test(windowing, window, 4096, (String8){text, 16384}),
                        "XIM exact input and converted-output boundaries admitted");
    WmEvent* committed = windowing->event_list.first;
    raster_native_check(committed && committed->kind == WM_EVENT_TEXT_INPUT && committed->window == window &&
                        committed->text.length == 16384 && committed->text.pointer != text &&
                        memcmp(committed->text.pointer, text, 16384) == 0, "XIM accepted text is copied into event ownership");
    text[0] = 'b';
    raster_native_check(committed && committed->text.pointer[0] == 'a', "XIM copied bytes survive source mutation");
    for (u32 index = 0; index < 3; index += 1)
    {
        raster_native_check(wm_x11_xim_commit_for_test(windowing, window, 4096, (String8){text, 16384}),
                            "XIM bounded cumulative converted text admitted");
    }
    u64 accepted_end = arena->position;
    raster_native_check(windowing->poll_commit_bytes == 65536 &&
                        !wm_x11_xim_commit_for_test(windowing, window, 1, S8("a")) && arena->position == accepted_end && windowing->event_list.count == 4,
                        "XIM cumulative bytes reject the next commit atomically");
    arena_reset_to_start(arena);
    windowing->event_list = (WmEventList){0};
    windowing->poll_commit_bytes = 0;
    windowing->poll_commit_count = 0;
    for (u32 index = 0; index < 32; index += 1)
    {
        raster_native_check(wm_x11_xim_commit_for_test(windowing, window, 1, S8("a")), "XIM bounded commit count admitted");
    }
    accepted_end = arena->position;
    raster_native_check(!wm_x11_xim_commit_for_test(windowing, window, 1, S8("a")) && arena->position == accepted_end && windowing->event_list.count == 32,
                        "XIM 33rd commit is refused without publishing partial output");
    arena_reset_to_start(arena);
    windowing->event_list = (WmEventList){0};
    windowing->poll_commit_bytes = 0;
    windowing->poll_commit_count = 0;
    for (u32 index = 0; index < 32; index += 1)
    {
        raster_native_check(!wm_x11_xim_commit_for_test(windowing, window, 1, (String8){text, sizeof(text)}),
                            "XIM rejected conversion still charges callback work");
    }
    raster_native_check(!wm_x11_xim_commit_for_test(windowing, window, 1, S8("a")) && windowing->poll_commit_count == 32 &&
                        windowing->event_list.count == 0, "XIM work exhaustion blocks later conversion attempts");
    Arena* limited = arena_create((ArenaCreation){.reserved_size = BUSTER_MB(1), .initial_size = BUSTER_KB(64), .flags = {.no_pool = true}});
    raster_native_check(limited != 0, "XIM limited committed arena created");
    if (limited)
    {
        // Leave space for one byte, but no aligned event; reserved capacity is
        // deliberately larger than committed capacity.
        BUSTER_UNUSED(arena_allocate(limited, u8, limited->os_position - limited->position - 1));
        windowing->event_arena = limited;
        windowing->poll_arena = limited;
        windowing->poll_commit_count = 0;
        start = limited->position;
        raster_native_check(!wm_x11_xim_commit_for_test(windowing, window, 1, S8("a")) && limited->position == start,
                            "XIM checks committed text plus aligned event capacity before copying");
        arena_destroy(limited, 1);
    }
    windowing->event_arena = arena;
    windowing->poll_arena = arena;
    windowing->event_list = (WmEventList){0};
    windowing->poll_commit_bytes = 0;
    windowing->poll_commit_count = 0;
    raster_native_check(wm_x11_xim_commit_for_test(windowing, window, 4, S8("\xf0\x9f\x98\x80")) && windowing->event_list.count == 1 &&
                        windowing->event_list.first->text.length == 4, "XIM valid Unicode scalar reaches owned text event");
    arena_reset_to_start(arena);
    BUSTER_UNUSED(wm_poll_events(arena, windowing));
    raster_native_check(!windowing->poll_arena && !windowing->poll_event_list && !windowing->poll_commit_bytes && !windowing->poll_commit_count,
                        "actual native poll clears XIM callback destination and budget scope");
    start = arena->position;
    raster_native_check(!wm_x11_xim_commit_for_test(windowing, window, 1, S8("a")) && arena->position == start,
                        "XIM late callback after native poll cannot use stale storage");
    arena_reset_to_start(arena);
}

BUSTER_GLOBAL_LOCAL void raster_native_xdnd_message(Arena* arena, WmHandle* windowing, xcb_window_t target, xcb_window_t source,
                                                  char const* type, u32 data1, u32 data2)
{
    xcb_client_message_event_t message = {0};
    message.response_type = XCB_CLIENT_MESSAGE;
    message.format = 32;
    message.window = target;
    message.type = raster_native_atom(windowing->connection, type);
    message.data.data32[0] = source;
    message.data.data32[1] = data1;
    message.data.data32[2] = data2;
    xcb_generic_error_t* error = xcb_request_check(windowing->connection,
        xcb_send_event_checked(windowing->connection, 0, target, XCB_EVENT_MASK_NO_EVENT, (char const*)&message));
    raster_native_check(message.type != XCB_ATOM_NONE && !error, "checked native XDND message queued");
    free(error);
    BUSTER_UNUSED(wm_poll_events(arena, windowing));
    arena_reset_to_start(arena);
}

BUSTER_GLOBAL_LOCAL void raster_native_xdnd_budgets(Arena* arena, WmHandle* windowing, WmWindowHandle* window, WmNativeSurface surface)
{
    xcb_connection_t* connection = windowing->connection;
    xcb_window_t target = (xcb_window_t)(uintptr_t)surface.window;
    xcb_window_t source = xcb_generate_id(connection);
    xcb_screen_t* screen = xcb_setup_roots_iterator(xcb_get_setup(connection)).data;
    u32 source_mask = XCB_EVENT_MASK_PROPERTY_CHANGE;
    xcb_generic_error_t* error = xcb_request_check(connection, xcb_create_window_checked(connection, XCB_COPY_FROM_PARENT,
        source, screen->root, 0, 0, 8, 8, 0, XCB_WINDOW_CLASS_INPUT_OUTPUT, screen->root_visual, XCB_CW_EVENT_MASK, &source_mask));
    raster_native_check(!error, "native XDND source window created");
    free(error);
    xcb_atom_t type_list = raster_native_atom(connection, "XdndTypeList");
    xcb_atom_t uri_list = raster_native_atom(connection, "text/uri-list");
    xcb_atom_t atoms[1025];
    u32 accepted_positions[] = {0, 255, 256, 1023};
    u32 expected_replies[] = {1, 1, 2, 4};
    for (u32 test = 0; test < BUSTER_ARRAY_LENGTH(accepted_positions); test += 1)
    {
        for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(atoms); index += 1)
        {
            atoms[index] = XCB_ATOM_STRING;
        }
        atoms[accepted_positions[test]] = uri_list;
        error = xcb_request_check(connection, xcb_change_property_checked(connection, XCB_PROP_MODE_REPLACE, source,
            type_list, XCB_ATOM_ATOM, 32, 1024, atoms));
        raster_native_check(!error, "bounded native atom list installed");
        free(error);
        raster_native_xdnd_message(arena, windowing, target, source, "XdndEnter", (5u << 24) | 1u, 0);
        raster_native_check(windowing->xdnd_active && windowing->xdnd_uri_list_supported && !windowing->xdnd_transfer_arena,
                            "native type negotiation admits URI at reply/work boundary without staging allocation");
        raster_native_check(windowing->xdnd_type_test_reply_count == expected_replies[test] &&
                            windowing->xdnd_type_test_max_reply_atoms == 256 &&
                            windowing->xdnd_type_test_scanned_atoms == accepted_positions[test] + 1,
                            "actual native property reply and work observations respect independent limits");
        raster_native_xdnd_message(arena, windowing, target, source, "XdndLeave", 0, 0);
        xcb_get_window_attributes_reply_t* attributes = xcb_get_window_attributes_reply(connection, xcb_get_window_attributes(connection, source), 0);
        raster_native_check(attributes && attributes->your_event_mask == source_mask && !windowing->xdnd_active &&
                            !windowing->xdnd_transfer_arena, "cancelled native negotiation restores source ownership");
        free(attributes);
    }
    atoms[0] = uri_list;
    error = xcb_request_check(connection, xcb_change_property_checked(connection, XCB_PROP_MODE_REPLACE, source,
        type_list, XCB_ATOM_ATOM, 32, 1025, atoms));
    raster_native_check(!error, "oversized native atom list installed");
    free(error);
    raster_native_xdnd_message(arena, windowing, target, source, "XdndEnter", (5u << 24) | 1u, 0);
    raster_native_check(windowing->xdnd_active && !windowing->xdnd_uri_list_supported && !windowing->xdnd_transfer_arena,
                        "oversized list rejects even an early URI before scanning or staging");
    raster_native_check(windowing->xdnd_type_test_reply_count == 1 && windowing->xdnd_type_test_max_reply_atoms == 256 &&
                        windowing->xdnd_type_test_scanned_atoms == 0, "oversized native list receives one bounded reply and performs no atom scan");
    raster_native_xdnd_message(arena, windowing, target, source, "XdndLeave", 0, 0);
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(atoms); index += 1)
    {
        atoms[index] = XCB_ATOM_STRING;
    }
    for (u32 mode = 0; mode < 2; mode += 1)
    {
        error = xcb_request_check(connection, xcb_change_property_checked(connection, XCB_PROP_MODE_REPLACE, source,
            type_list, XCB_ATOM_ATOM, mode == 0 ? 32 : 8, mode == 0 ? 1024 : 4, atoms));
        raster_native_check(!error, "native absent-URI or wrong-format atom property installed");
        free(error);
        raster_native_xdnd_message(arena, windowing, target, source, "XdndEnter", (5u << 24) | 1u, 0);
        raster_native_check(!windowing->xdnd_uri_list_supported && !windowing->xdnd_transfer_arena &&
                            windowing->xdnd_type_test_scanned_atoms == (mode == 0 ? 1024u : 0u),
                            "native absent URI exhausts bounded work and malformed format rejects before scan");
        raster_native_xdnd_message(arena, windowing, target, source, "XdndLeave", 0, 0);
    }

    // Begin an ordinary inline negotiation, then exercise the same append owner
    // used by direct and INCR property reads without allocating X-server peaks.
    raster_native_xdnd_message(arena, windowing, target, source, "XdndEnter", 5u << 24, uri_list);
    raster_native_check(windowing->xdnd_active && windowing->xdnd_uri_list_supported, "inline native URI negotiation remains admitted");
    arena_test_fail_next_reserve();
    raster_native_check(!wm_x11_xdnd_append_for_test(windowing, S8("file:///tmp/bounded\r\n")) && !windowing->xdnd_transfer_arena &&
                        windowing->xdnd_transfer_data.length == 0, "XDND staging reservation refusal is recoverable");
    arena_test_fail_next_commit();
    raster_native_check(!wm_x11_xdnd_append_for_test(windowing, S8("file:///tmp/bounded\r\n")) && !windowing->xdnd_transfer_arena &&
                        windowing->xdnd_transfer_data.length == 0, "XDND staging commitment refusal releases its reservation");
    char8 chunk[32768];
    memset(chunk, 'x', sizeof(chunk));
    bool appended = true;
    char8* original = 0;
    for (u32 index = 0; appended && index < 512; index += 1)
    {
        appended = wm_x11_xdnd_append_for_test(windowing, (String8){chunk, sizeof(chunk)});
        if (!index)
        {
            original = windowing->xdnd_transfer_data.pointer;
        }
    }
    raster_native_check(appended && windowing->xdnd_transfer_data.length == 16777216 && windowing->xdnd_transfer_data.pointer == original &&
                        windowing->xdnd_transfer_arena && windowing->xdnd_transfer_arena->reserved_size == 16842752 &&
                        windowing->xdnd_transfer_arena->position == arena_minimum_position + 16777216,
                        "repeated append reaches payload boundary with one independently bounded retained mapping");
    raster_native_check(appended && original[0] == 'x' && original[16777215] == 'x', "XDND staged boundary bytes are preserved");
    raster_native_check(!wm_x11_xdnd_append_for_test(windowing, S8("x")) && windowing->xdnd_transfer_data.length == 16777216,
                        "XDND next byte is refused without changing retained data");
    raster_native_xdnd_message(arena, windowing, target, source, "XdndLeave", 0, 0);
    raster_native_check(!windowing->xdnd_transfer_arena && !windowing->xdnd_transfer_capacity &&
                        !windowing->xdnd_transfer_data.pointer && !windowing->xdnd_active, "native cancellation releases staging owner and payload");
    raster_native_xdnd_message(arena, windowing, target, source, "XdndEnter", 5u << 24, uri_list);
    raster_native_check(!wm_x11_xdnd_append_for_test(windowing, (String8){chunk, UINT64_MAX}) && !windowing->xdnd_transfer_arena,
                        "XDND oversized arithmetic rejects before allocating or reading supplied bytes");
    raster_native_check(wm_x11_xdnd_append_for_test(windowing, S8("file:///tmp/bounded\r\n")), "new transaction recovers after budget refusal");
    raster_native_xdnd_message(arena, windowing, target, source, "XdndEnter", 5u << 24, uri_list);
    raster_native_check(windowing->xdnd_active && !windowing->xdnd_transfer_arena && !windowing->xdnd_transfer_data.pointer,
                        "superseding native enter releases previous staging owner");
    raster_native_xdnd_message(arena, windowing, target, source, "XdndLeave", 0, 0);

    xcb_atom_t selection = raster_native_atom(connection, "XdndSelection");
    error = xcb_request_check(connection, xcb_set_selection_owner_checked(connection, source, selection, XCB_CURRENT_TIME));
    raster_native_check(!error, "native test source owns XDND selection");
    free(error);
    for (u32 mode = 0; mode < 3; mode += 1)
    {
        raster_native_xdnd_message(arena, windowing, target, source, "XdndEnter", 5u << 24, uri_list);
        raster_native_xdnd_message(arena, windowing, target, source, "XdndPosition", 0, (12u << 16) | 12u);
        raster_native_check(windowing->xdnd_accept, "native position admits bounded URI drop");
        raster_native_xdnd_message(arena, windowing, target, source, "XdndDrop", 0, XCB_CURRENT_TIME);
        raster_native_check(windowing->xdnd_drop_pending && windowing->xdnd_window == window,
                            "native drop requests owned selection without a premature completion");
        xcb_atom_t property = windowing->xdnd_property;
        u32 hint = UINT32_MAX;
        String8 payload = S8("file:///tmp/bounded%20drop\r\n");
        xcb_atom_t initial_type = mode == 1 ? raster_native_atom(connection, "INCR") : mode == 2 ? XCB_ATOM_STRING : uri_list;
        error = xcb_request_check(connection, xcb_change_property_checked(connection, XCB_PROP_MODE_REPLACE, target, property,
            initial_type, mode == 1 ? 32 : 8, mode == 1 ? 1 : (u32)payload.length, mode == 1 ? (void const*)&hint : (void const*)payload.pointer));
        raster_native_check(!error, "native source supplies direct or incremental selection property");
        free(error);
        xcb_selection_notify_event_t notify = {0};
        notify.response_type = XCB_SELECTION_NOTIFY;
        notify.requestor = target;
        notify.selection = selection;
        notify.target = uri_list;
        notify.property = property;
        error = xcb_request_check(connection, xcb_send_event_checked(connection, 0, target, XCB_EVENT_MASK_NO_EVENT, (char const*)&notify));
        raster_native_check(!error, "native selection notification queued");
        free(error);
        WmEventList completed = wm_poll_events(arena, windowing);
        if (mode == 1)
        {
            raster_native_check(windowing->xdnd_incremental && windowing->xdnd_transfer_expected == UINT32_MAX &&
                                !windowing->xdnd_transfer_arena, "INCR hint cannot preallocate unbounded staging");
            arena_reset_to_start(arena);
            char8* retained = 0;
            u32 offset = 0;
            for (u32 chunk_index = 0; chunk_index < 2; chunk_index += 1)
            {
                u32 length = chunk_index == 0 ? 8 : (u32)payload.length - offset;
                error = xcb_request_check(connection, xcb_change_property_checked(connection, XCB_PROP_MODE_REPLACE, target, property,
                    uri_list, 8, length, payload.pointer + offset));
                raster_native_check(!error, "native INCR chunk property installed");
                free(error);
                BUSTER_UNUSED(wm_poll_events(arena, windowing));
                offset += length;
                if (!chunk_index)
                {
                    retained = windowing->xdnd_transfer_data.pointer;
                }
                raster_native_check(windowing->xdnd_drop_pending && windowing->xdnd_transfer_data.length == offset &&
                                    windowing->xdnd_transfer_data.pointer == retained, "native INCR chunks retain one stable bounded buffer");
                arena_reset_to_start(arena);
            }
            error = xcb_request_check(connection, xcb_change_property_checked(connection, XCB_PROP_MODE_REPLACE, target, property, uri_list, 8, 0, 0));
            raster_native_check(!error, "native INCR terminator installed");
            free(error);
            completed = wm_poll_events(arena, windowing);
        }
        WmEvent* drop_event = 0;
        for (WmEvent* event = completed.first; event; event = event->next)
        {
            if (event->kind == WM_EVENT_FILE_DROP)
            {
                drop_event = event;
            }
        }
        if (mode == 2)
        {
            raster_native_check(!drop_event, "wrong native selection type rejects without publishing paths");
        }
        else
        {
            raster_native_check(drop_event && drop_event->window == window && drop_event->paths.length == 1 &&
                                drop_event->paths.pointer[0].length == 17 && memcmp(drop_event->paths.pointer[0].pointer, "/tmp/bounded drop", 17) == 0,
                                "native direct and INCR drops publish independently expected decoded path");
        }
        raster_native_check(!windowing->xdnd_active && !windowing->xdnd_transfer_arena && !windowing->xdnd_transfer_data.pointer,
                            "native selection completion releases transfer storage before caller consumes paths");
        arena_reset_to_start(arena);
    }
    error = xcb_request_check(connection, xcb_destroy_window_checked(connection, source));
    raster_native_check(!error, "native XDND source window destroyed");
    free(error);
}

typedef struct RasterNativeXimProvider RasterNativeXimProvider;
// First-party fixture using the already linked libxcb-imdkit server API; no
// upstream source is copied. Public API inspected at fcitx/xcb-imdkit tag 1.0.9,
// commit 44f5c8219bcae9e6afc2391dc50486efcf0bdf06 (imdkit.h: LGPL-2.1-only).
struct RasterNativeXimProvider
{
    xcb_connection_t* connection;
    xcb_im_t* server;
    xcb_im_input_context_t* input_context;
    xcb_window_t target;
    u32 created;
    u32 destroyed;
    u32 sync_replies;
};

BUSTER_GLOBAL_LOCAL void raster_native_xim_provider_callback(xcb_im_t* server, xcb_im_client_t* client, xcb_im_input_context_t* input_context,
                                                           const xcb_im_packet_header_fr_t* header, void* frame, void* argument, void* user_data)
{
    BUSTER_UNUSED(server);
    BUSTER_UNUSED(client);
    BUSTER_UNUSED(frame);
    BUSTER_UNUSED(argument);
    RasterNativeXimProvider* provider = (RasterNativeXimProvider*)user_data;
    if (header->major_opcode == XCB_XIM_CREATE_IC && input_context &&
        xcb_im_input_context_get_client_window(input_context) == provider->target)
    {
        // CREATE_IC precedes the reply; readiness also requires Buster's IC.
        provider->input_context = input_context;
        provider->created += 1;
    }
    else if (header->major_opcode == XCB_XIM_DESTROY_IC && provider->input_context == input_context)
    {
        provider->input_context = 0;
        provider->destroyed += 1;
    }
    else if (header->major_opcode == XCB_XIM_DISCONNECT)
    {
        provider->input_context = 0;
    }
    else if (header->major_opcode == XCB_XIM_SYNC_REPLY && provider->input_context == input_context)
    {
        provider->sync_replies += 1;
    }
}

BUSTER_GLOBAL_LOCAL u64 raster_native_xim_now_ms(void)
{
    struct timespec now;
    bool valid = clock_gettime(CLOCK_MONOTONIC, &now) == 0 && now.tv_sec >= 0;
    return valid ? (u64)now.tv_sec * 1000 + (u64)now.tv_nsec / 1000000 : UINT64_MAX;
}

BUSTER_GLOBAL_LOCAL bool raster_native_xim_pump(Arena* arena, WmHandle* windowing, RasterNativeXimProvider* provider, WmEventList* events)
{
    *events = (WmEventList){0};
    bool result = provider->connection && !xcb_connection_has_error(provider->connection);
    for (u32 count = 0; result && count < 64; count += 1)
    {
        xcb_generic_event_t* event = xcb_poll_for_event(provider->connection);
        if (!event)
        {
            break;
        }
        result = (event->response_type & 0x7fu) != 0;
        if (result)
        {
            BUSTER_UNUSED(xcb_im_filter_event(provider->server, event));
        }
        free(event);
    }
    if (result)
    {
        // Make provider output visible before the client is polled, including
        // non-protocol X requests queued while handling a provider event.
        result = xcb_flush(provider->connection) > 0;
    }
    if (result && windowing && windowing->connection)
    {
        result = wm_poll_events_bounded(arena, windowing, 32, events);
    }
    if (result)
    {
        struct pollfd descriptors[2] = {{.fd = xcb_get_file_descriptor(provider->connection), .events = POLLIN}};
        nfds_t count = 1;
        if (windowing && windowing->connection)
        {
            descriptors[count++] = (struct pollfd){.fd = xcb_get_file_descriptor(windowing->connection), .events = POLLIN};
        }
        // Both peers are driven on this thread. No wait-for-event can block one
        // endpoint while the other needs it to finish the handshake or ACK.
        int waited = poll(descriptors, count, 5);
        result = waited >= 0 || errno == EINTR;
        for (nfds_t index = 0; result && index < count; index += 1)
        {
            result = !(descriptors[index].revents & (POLLERR | POLLHUP | POLLNVAL));
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool raster_native_xim_commit(Arena* arena, WmHandle* windowing, WmWindowHandle* window,
                                                RasterNativeXimProvider* provider, String8 wire, String8 expected, String8* borrowed)
{
    *borrowed = (String8){0};
    arena_reset_to_start(arena);
    Arena* payload_arena = arena_create((ArenaCreation){.reserved_size = BUSTER_KB(64), .initial_size = BUSTER_KB(64), .flags = {.no_pool = true}});
    u64 start = raster_native_xim_now_ms();
    bool result = payload_arena && wire.pointer && wire.length && wire.length <= 4097 &&
                  provider->input_context && start <= UINT64_MAX - 5000;
    u32 wanted_sync = provider->sync_replies + 1;
    if (result)
    {
        char8* payload = arena_allocate(payload_arena, char8, wire.length);
        memcpy(payload, wire.pointer, wire.length);
        xcb_im_commit_string(provider->server, provider->input_context, XCB_XIM_LOOKUP_CHARS, payload, (u32)wire.length, 0);
        memset(payload, 'z', wire.length);
        // Asynchronous commits have no preceding COMMIT ACK. The subsequent
        // SYNC reply proves the client consumed this ordered commit, including
        // a deliberately refused commit, rather than mistaking silence for it.
        xcb_im_sync_xlib(provider->server, provider->input_context);
        xcb_flush(provider->connection);
    }
    if (payload_arena)
    {
        arena_destroy(payload_arena, 1);
    }
    u32 text_count = 0;
    WmEvent* text_event = 0;
    for (u32 pass = 0; result && provider->sync_replies < wanted_sync && pass < 2048; pass += 1)
    {
        u64 now = raster_native_xim_now_ms();
        result = now < start + 5000;
        if (result)
        {
            WmEventList events;
            result = raster_native_xim_pump(arena, windowing, provider, &events);
            for (WmEvent* event = events.first; result && event; event = event->next)
            {
                if (event->kind == WM_EVENT_TEXT_INPUT)
                {
                    text_count += 1;
                    text_event = event;
                }
            }
        }
    }
    result = result && provider->sync_replies == wanted_sync && !windowing->poll_arena && !windowing->poll_event_list;
    if (result && expected.length)
    {
        result = text_count == 1 && text_event && text_event->window == window && text_event->text.length == expected.length &&
                 arena_range_contains(arena, arena_minimum_position, arena->position, text_event) &&
                 arena_range_contains(arena, arena_minimum_position, arena->position, (char8*)text_event + sizeof(*text_event) - 1) &&
                 arena_range_contains(arena, arena_minimum_position, arena->position, text_event->text.pointer) &&
                 arena_range_contains(arena, arena_minimum_position, arena->position, text_event->text.pointer + expected.length - 1);
        if (result)
        {
            // Client parser storage has already been released on callback
            // return. Overwrite unrelated scratch too before reading the slice.
            TemporalArena scratch = scratch_begin(&arena, 1);
            result = scratch.arena != 0;
            if (result)
            {
                memset(arena_allocate(scratch.arena, u8, BUSTER_KB(64)), 0x5a, BUSTER_KB(64));
                scratch_end(scratch);
                result = memcmp(text_event->text.pointer, expected.pointer, expected.length) == 0;
            }
            if (result)
            {
                *borrowed = text_event->text;
            }
        }
    }
    else if (result)
    {
        result = text_count == 0;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL void raster_native_xim_provider(Arena* arena, u32 mode)
{
    u32 failures_before = raster_native_failures;
    RasterNativeXimProvider provider = {0};
    int screen_id = 0;
    provider.connection = xcb_connect(0, &screen_id);
    bool ready = provider.connection && !xcb_connection_has_error(provider.connection);
    char const* stage = "provider-connection";
    u32 passes = 0;
    char name[80] = {0};
    char modifiers[88] = {0};
    int named = snprintf(name, sizeof(name), "buster-native-xim-%ld-%u", (long)getpid(), (unsigned)mode);
    int modified = snprintf(modifiers, sizeof(modifiers), "@im=%s", name);
    ready = ready && named > 0 && named < (int)sizeof(name) && modified > 0 && modified < (int)sizeof(modifiers);
    char* old_modifiers = getenv("XMODIFIERS");
    char* saved_modifiers = old_modifiers ? strdup(old_modifiers) : 0;
    ready = ready && (!old_modifiers || saved_modifiers);
    bool environment_changed = false;
    bool opened = false;
    xcb_window_t server_window = XCB_WINDOW_NONE;
    WmHandle* windowing = 0;
    WmWindowHandle* window = 0;
    String8 final_text = {0};
    String8 unicode = S8("A\xc3\xb1\xe4\xb8\xad\xf0\x9f\x98\x80");
    // Independent wire/golden bytes from Compound Text 1.1's Latin-1 designation
    // (xorg.freedesktop.org/archive/current/doc/xorg-docs/ctext/ctext.html).
    String8 compound_wire = S8("A\x1b-A\xe9");
    String8 compound_expected = S8("A\xc3\xa9");
    String8 wire = mode == 0 ? unicode : compound_wire;
    String8 expected = mode == 0 ? unicode : compound_expected;
    if (ready)
    {
        stage = "provider-window";
        xcb_screen_iterator_t screens = xcb_setup_roots_iterator(xcb_get_setup(provider.connection));
        for (int index = 0; index < screen_id && screens.rem; index += 1)
        {
            xcb_screen_next(&screens);
        }
        ready = screens.rem != 0;
        if (ready)
        {
            server_window = xcb_generate_id(provider.connection);
            xcb_generic_error_t* error = xcb_request_check(provider.connection, xcb_create_window_checked(provider.connection,
                XCB_COPY_FROM_PARENT, server_window, screens.data->root, 0, 0, 1, 1, 0, XCB_WINDOW_CLASS_INPUT_OUTPUT, screens.data->root_visual, 0, 0));
            ready = !error;
            free(error);
        }
    }
    if (ready)
    {
        stage = "provider-create";
        u32 style = XCB_IM_PreeditNothing | XCB_IM_StatusNothing;
        xcb_im_styles_t styles = {1, &style};
        char* encoding_name = mode == 0 ? "UTF8_STRING" : "COMPOUND_TEXT";
        xcb_im_encodings_t encodings = {1, &encoding_name};
        provider.server = xcb_im_create(provider.connection, screen_id, server_window, name, XCB_IM_ALL_LOCALES,
            &styles, 0, 0, &encodings, XCB_EVENT_MASK_KEY_PRESS, raster_native_xim_provider_callback, &provider);
        ready = provider.server != 0;
        if (ready)
        {
            xcb_im_set_use_sync_mode(provider.server, false);
            stage = "provider-open";
            opened = xcb_im_open_im(provider.server);
            ready = opened;
        }
        if (ready)
        {
            stage = "environment-select";
            environment_changed = setenv("XMODIFIERS", modifiers, 1) == 0;
            ready = environment_changed;
        }
    }
    if (ready)
    {
        stage = "client-wm-xim";
        windowing = wm_initialize();
        ready = windowing && windowing->xim;
        if (ready)
        {
            stage = "client-window";
            window = wm_window_create(windowing, (WmWindowCreate){.name = S8("Buster native XIM provider gate"),
                .size = {.width = 16, .height = 16}, .disable_file_drop = true});
            ready = window != 0;
            if (ready)
            {
                provider.target = window->handle;
            }
        }
    }
    u64 start = raster_native_xim_now_ms();
    ready = ready && start <= UINT64_MAX - 5000;
    for (; ready && !(provider.input_context && window->ic && windowing->xim_open) && passes < 2048; passes += 1)
    {
        stage = "handshake-deadline";
        ready = raster_native_xim_now_ms() < start + 5000;
        if (ready)
        {
            arena_reset_to_start(arena);
            WmEventList events;
            stage = "handshake-transport";
            ready = raster_native_xim_pump(arena, windowing, &provider, &events);
        }
    }
    printf("XIM_HANDSHAKE_V1 campaign=%u encoding=%s stage=%s passes=%u elapsed_ms=%llu provider_error=%d client_error=%d client_open=%u client_ic=%u created=%u\n",
           (unsigned)raster_native_campaign, mode == 0 ? "utf8" : "compound", ready ? "final-context-encoding" : stage,
           (unsigned)passes, (unsigned long long)(raster_native_xim_now_ms() - start),
           provider.connection ? xcb_connection_has_error(provider.connection) : -1,
           windowing && windowing->connection ? xcb_connection_has_error(windowing->connection) : -1,
           windowing ? (unsigned)windowing->xim_open : 0, window ? (unsigned)window->ic : 0, (unsigned)provider.created);
    ready = ready && provider.input_context && provider.created == 1 && window->ic && windowing->xim_open &&
            xcb_xim_get_encoding(windowing->xim) == (mode == 0 ? XCB_XIM_UTF8_STRING : XCB_XIM_COMPOUND_TEXT);
    raster_native_encodings += ready;
    raster_native_check(ready, "real XIM provider handshakes and both nonzero input contexts negotiate the intended encoding");
    if (ready)
    {
        raster_native_check(raster_native_xim_commit(arena, windowing, window, &provider, wire, expected, &final_text),
                            "real XIM callback yields independent Unicode in caller-owned event storage after native payload release");
        if (mode == 0)
        {
            char8 large[4097];
            memset(large, 'x', sizeof(large));
            String8 boundary = {large, 4096};
            raster_native_check(raster_native_xim_commit(arena, windowing, window, &provider, boundary, boundary, &final_text),
                                "real UTF-8 XIM transport admits the exact raw-input boundary");
            raster_native_check(raster_native_xim_commit(arena, windowing, window, &provider, (String8){large, sizeof(large)}, (String8){0}, &final_text),
                                "real XIM oversized commit is consumed and refused without a text event");
            raster_native_check(raster_native_xim_commit(arena, windowing, window, &provider, S8("\xc0\xaf"), (String8){0}, &final_text),
                                "real XIM malformed UTF-8 is consumed and refused without a text event");
        }
        raster_native_check(raster_native_xim_commit(arena, windowing, window, &provider, wire, expected, &final_text),
                            "real XIM remains usable after admitted and refused commits");
    }
    if (windowing)
    {
        wm_deinitialize(windowing);
        wm_deinitialize(windowing);
    }
    if (opened)
    {
        start = raster_native_xim_now_ms();
        bool drained = start <= UINT64_MAX - 5000;
        for (u32 pass = 0; drained && provider.input_context && pass < 2048; pass += 1)
        {
            drained = raster_native_xim_now_ms() < start + 5000;
            if (drained)
            {
                WmEventList events;
                drained = raster_native_xim_pump(arena, 0, &provider, &events);
            }
        }
        raster_native_check(!ready || (drained && !provider.input_context && provider.destroyed == 1),
                            "real XIM teardown consumes the server's borrowed input context");
        xcb_im_close_im(provider.server);
        // close_im queues removal of our root-window advertisement. Complete
        // an ordered X-server round trip before releasing this connection.
        xcb_flush(provider.connection);
        xcb_generic_error_t* error = 0;
        xcb_get_input_focus_reply_t* reply = xcb_get_input_focus_reply(provider.connection,
            xcb_get_input_focus(provider.connection), &error);
        raster_native_check(!ready || (reply && !error), "ephemeral XIM provider advertisement removal reaches the X server");
        free(error);
        free(reply);
    }
    if (provider.server)
    {
        xcb_im_destroy(provider.server);
    }
    if (provider.connection)
    {
        xcb_disconnect(provider.connection);
    }
    if (environment_changed)
    {
        int restored = saved_modifiers ? setenv("XMODIFIERS", saved_modifiers, 1) : unsetenv("XMODIFIERS");
        raster_native_check(restored == 0, "real XIM provider selection environment restored");
    }
    free(saved_modifiers);
    raster_native_check(!ready || (final_text.pointer && final_text.length == expected.length &&
                        memcmp(final_text.pointer, expected.pointer, expected.length) == 0),
                        "caller-owned committed text survives complete client and provider shutdown");
    printf("XIM_PROVIDER_GATE_V1 encoding=%s status=%s created=%u destroyed=%u sync_replies=%u\n",
           mode == 0 ? "utf8" : "compound", !ready ? "provider-unavailable-or-handshake-failed" :
           raster_native_failures == failures_before ? "passed" : "failed",
           (unsigned)provider.created, (unsigned)provider.destroyed, (unsigned)provider.sync_replies);
    arena_reset_to_start(arena);
}

BUSTER_GLOBAL_LOCAL void raster_native_cycle(Arena* arena, u32 cycle)
{
    printf("WM_LIFECYCLE_V1 campaign=%u cycle=%u status=started\n", (unsigned)raster_native_campaign, (unsigned)cycle);
    WmHandle* windowing = wm_initialize();
    raster_native_check(windowing != 0, "real wm initialization");
    raster_native_cycles += windowing != 0;
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
                raster_native_xim_scope(arena, windowing, window);
                if (cycle == 0)
                {
                    raster_native_xdnd_budgets(arena, windowing, window, surface);
                }
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
        raster_native_check(wm_x11_xdnd_append_for_test(windowing, S8("shutdown-owned-staging")), "shutdown fixture owns bounded staging");
        wm_deinitialize(windowing);
        wm_deinitialize(windowing);
        raster_native_check(!windowing->xdnd_transfer_arena && !windowing->connection && !windowing->xdnd_transfer_data.pointer,
                            "repeated native shutdown releases active staging and connection");
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
        char const* repetition_text = getenv("BUSTER_NATIVE_CAMPAIGN_REPETITIONS");
        u32 repetitions = 1;
        if (repetition_text)
        {
            char* end = 0;
            unsigned long parsed = strtoul(repetition_text, &end, 10);
            bool valid = end && !*end && parsed >= 1 && parsed <= 64;
            raster_native_check(valid, "native campaign repetition bound");
            repetitions = valid ? (u32)parsed : 1;
        }
        for (raster_native_campaign = 0; raster_native_campaign < repetitions; raster_native_campaign += 1)
        {
            raster_native_window_arena_failure();
            for (u32 cycle = 0; cycle < 3; cycle += 1)
            {
                raster_native_cycle(arena, cycle);
            }
            raster_native_xim_provider(arena, 0);
            raster_native_xim_provider(arena, 1);
        }
        printf("NATIVE_CASES_V1 cycles=%u/%u encodings=%u/%u\n", (unsigned)raster_native_cycles,
               (unsigned)(3 * repetitions), (unsigned)raster_native_encodings, (unsigned)(2 * repetitions));
        raster_native_check(raster_native_cycles == 3 * repetitions && raster_native_encodings == 2 * repetitions,
                            "all declared lifecycle and real encoding cases executed");
    }
    printf("rendering_raster_native_tests: %u/%u assertions passed; mode=%s\n",
           (unsigned)(raster_native_assertions - raster_native_failures), (unsigned)raster_native_assertions,
           failure_mode ? "unavailable-display" : "actual-xcb-readback");
    thread_context_release(context);
    arena_destroy(arena, 1);
    program_state->arena = 0;
    return raster_native_failures != 0;
}
