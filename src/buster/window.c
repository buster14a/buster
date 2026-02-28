#pragma once
#include <buster/window.h>
#if defined (__linux__)
#include <xcb/xcb.h>
#endif
#include <buster/string8.h>
#include <buster/os.h>
#include <buster/assertion.h>
#include <buster/arena.h>
#include <buster/file.h>
#include <buster/truetype.h>
#include <buster/font_provider.h>
#include <buster/ui_core.h>

#ifdef __linux__
STRUCT(OsWindowingHandle)
{
    xcb_connection_t* connection;
    const xcb_setup_t* setup;
};
#else
#endif
BUSTER_GLOBAL_LOCAL OsWindowingHandle windowing_handle = {};

typedef enum X11Atom
{
    X11_ATOM_WM_PROTOCOLS,
    X11_ATOM_WM_DELETE_WINDOW,
    X11_ATOM_COUNT,
} X11Atom;

BUSTER_GLOBAL_LOCAL String8 atom_names[X11_ATOM_COUNT] = {
    S8("WM_PROTOCOLS"),
    S8("WM_DELETE_WINDOW"),
};

BUSTER_GLOBAL_LOCAL xcb_intern_atom_reply_t* atom_replies[BUSTER_ARRAY_LENGTH(atom_names)];
BUSTER_GLOBAL_LOCAL xcb_intern_atom_cookie_t atom_cookies[BUSTER_ARRAY_LENGTH(atom_names)];

BUSTER_IMPL OsWindowingHandle* os_windowing_initialize()
{
    OsWindowingHandle* result = {};
#if defined(__linux__)
    // XCB allocates global state via pthread_once on first connection that it
    // never frees. Disable LSAN for this call so those library-internal
    // allocations aren't reported as leaks.
    BUSTER_LSAN_DISABLE();
    let connection = xcb_connect(0, 0);
    BUSTER_LSAN_ENABLE();
    if (connection)
    {
        let setup = xcb_get_setup(connection);
        if (setup)
        {
            windowing_handle = (OsWindowingHandle){
                .connection = connection,
                .setup = setup,
            };

            u64 atom_count = BUSTER_ARRAY_LENGTH(atom_names);
            for (u64 i = 0; i < atom_count; i += 1)
            {
                String8 atom_name = atom_names[i];

                atom_cookies[i] = xcb_intern_atom(connection, 0, (u16)atom_name.length, atom_name.pointer);
            }

            for (u64 i = 0; i < atom_count; i += 1)
            {
                atom_replies[i] = xcb_intern_atom_reply(connection, atom_cookies[i], 0);
            }

            result = &windowing_handle;
        }
    }
#endif

    return result;
}

BUSTER_IMPL void os_windowing_deinitialize(OsWindowingHandle* windowing)
{
#if defined(__linux__)
    if (windowing->connection)
    {
        xcb_disconnect(windowing->connection);

        u64 atom_count = BUSTER_ARRAY_LENGTH(atom_names);
        for (u64 i = 0; i < atom_count; i += 1)
        {
            free(atom_replies[i]);
        }
    }
#endif
}

BUSTER_IMPL OsWindowHandle* os_window_create(OsWindowingHandle* windowing, OsWindowCreate create)
{
    OsWindowHandle* result = {};
#if defined(__linux__)
    let connection = windowing->connection;
    let setup = windowing->setup;
    xcb_screen_t* screen = 0;

    for (let screen_iterator = xcb_setup_roots_iterator(setup); screen_iterator.rem; xcb_screen_next(&screen_iterator))
    {
        screen = screen_iterator.data;
        if (screen)
        {
            break;
        }
    }

    if (screen)
    {
        let window_id = xcb_generate_id(connection);
        let parent_window = screen->root;
        u16 border_width = 10;

        xcb_create_window(connection, XCB_COPY_FROM_PARENT, window_id, parent_window, 0, 0, create.size.width, create.size.height, border_width, XCB_WINDOW_CLASS_INPUT_OUTPUT, screen->root_visual, 0, 0);
        u32 event_mask = XCB_EVENT_MASK_KEY_PRESS |
            XCB_EVENT_MASK_KEY_RELEASE |
            XCB_EVENT_MASK_BUTTON_PRESS |
            XCB_EVENT_MASK_BUTTON_RELEASE |
            XCB_EVENT_MASK_ENTER_WINDOW |
            XCB_EVENT_MASK_LEAVE_WINDOW |
            XCB_EVENT_MASK_POINTER_MOTION |
            XCB_EVENT_MASK_POINTER_MOTION_HINT |
            XCB_EVENT_MASK_BUTTON_1_MOTION |
            XCB_EVENT_MASK_BUTTON_2_MOTION |
            XCB_EVENT_MASK_BUTTON_3_MOTION |
            XCB_EVENT_MASK_BUTTON_4_MOTION |
            XCB_EVENT_MASK_BUTTON_5_MOTION |
            XCB_EVENT_MASK_BUTTON_MOTION |
            XCB_EVENT_MASK_KEYMAP_STATE |
            XCB_EVENT_MASK_EXPOSURE |
            XCB_EVENT_MASK_VISIBILITY_CHANGE |
            XCB_EVENT_MASK_STRUCTURE_NOTIFY |
            XCB_EVENT_MASK_RESIZE_REDIRECT |
            XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY |
            XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT |
            XCB_EVENT_MASK_FOCUS_CHANGE |
            XCB_EVENT_MASK_PROPERTY_CHANGE |
            XCB_EVENT_MASK_COLOR_MAP_CHANGE |
            XCB_EVENT_MASK_OWNER_GRAB_BUTTON;
        xcb_change_window_attributes(connection, window_id, XCB_CW_EVENT_MASK, &event_mask);
        xcb_map_window(connection, window_id);

        xcb_change_property(connection, XCB_PROP_MODE_REPLACE, window_id,
                atom_replies[X11_ATOM_WM_PROTOCOLS]->atom,
                XCB_ATOM_ATOM, 32, 1,
                &atom_replies[X11_ATOM_WM_DELETE_WINDOW]->atom);
        xcb_flush(connection);

        BUSTER_CHECK(window_id != 0);
        result = (OsWindowHandle*)(u64)window_id;
    }
    else
    {
        string8_print(S8("No screen found\n"));
    }
#else
#endif
    return result;
}

BUSTER_IMPL OsWindowSize os_window_get_framebuffer_size(OsWindowingHandle* windowing, OsWindowHandle* os_window)
{
    OsWindowSize result = {};
    xcb_connection_t* connection = windowing->connection;
    let window = (xcb_window_t)(u64)os_window;
    xcb_get_geometry_cookie_t cookie = xcb_get_geometry(connection, window);
    xcb_get_geometry_reply_t* reply = xcb_get_geometry_reply(connection, cookie, 0);
    result.width = reply->width;
    result.height = reply->height;
    free(reply);
    return result;
}

BUSTER_IMPL void* native_windowing_handle_from_os_windowing_handle(OsWindowingHandle* windowing)
{
    return windowing->connection;
}

BUSTER_IMPL void* native_window_handle_from_os_window_handle(OsWindowHandle* window)
{
    return window;
}

BUSTER_IMPL OsWindowingEventList os_windowing_poll_events(Arena* arena, OsWindowingHandle* windowing)
{
    OsWindowingEventList event_list = {};

#if defined(__linux__)
    xcb_generic_event_t *event;
    xcb_connection_t* connection = windowing->connection;

    while ((event = xcb_poll_for_event(connection)))
    {
        switch (event->response_type & ~0x80) {
            case XCB_EXPOSE:
                break;
            case XCB_KEY_PRESS:
                break;
            case XCB_CLIENT_MESSAGE:
                {
                    let client_message_event = (xcb_client_message_event_t*)event;
                    if (client_message_event->data.data32[0] == atom_replies[X11_ATOM_WM_DELETE_WINDOW]->atom)
                    {
                        let os_event = arena_allocate(arena, OsWindowingEvent, 1);
                        *os_event = (OsWindowingEvent) {
                            .kind = OS_WINDOWING_EVENT_WINDOW_CLOSE,
                            .window = (OsWindowHandle*)(u64)client_message_event->window,
                        };

                        if (event_list.last)
                        {
                            os_event->previous = event_list.last;
                            event_list.last->next = os_event;
                        }
                        else
                        {
                            event_list.first = os_event;
                        }

                        event_list.last = os_event;
                        event_list.count += 1;
                    }
                    else
                    {
                        BUSTER_TRAP();
                    }
                } break;
            case XCB_DESTROY_NOTIFY:
                BUSTER_TRAP();
            default:
                break;
        }

        free(event);
    }
#endif

    return event_list;
}
