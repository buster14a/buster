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
        auto event_type = event->response_type & ~0x80;
        string8_print(S8("Event type: {u8}. ("), event_type);
        bool unimplemented = true;

        switch (event_type)
        {
            break;
            case XCB_KEY_PRESS: // 2
            case XCB_KEY_RELEASE: // 3
            {
                switch (event_type)
                {
                    break; case XCB_KEY_PRESS: string8_print(S8("KEY_PRESS"));
                    break; case XCB_KEY_RELEASE: string8_print(S8("KEY_RELEASE"));
                    break; default: BUSTER_UNREACHABLE();
                }

                let key_event = (xcb_key_press_event_t*)event;

                // xcb_key_press_event_t(3)                                                                   XCB Events                                                                  xcb_key_press_event_t(3)
                //
                // NAME
                //        xcb_key_press_event_t - a key was pressed/released
                //
                // SYNOPSIS
                //        #include <xcb/xproto.h>
                //
                //    Event datastructure
                //        typedef struct xcb_key_press_event_t {
                //            uint8_t         response_type;
                //            xcb_keycode_t   detail;
                //            uint16_t        sequence;
                //            xcb_timestamp_t time;
                //            xcb_window_t    root;
                //            xcb_window_t    event;
                //            xcb_window_t    child;
                //            int16_t         root_x;
                //            int16_t         root_y;
                //            int16_t         event_x;
                //            int16_t         event_y;
                //            uint16_t        state;
                //            uint8_t         same_screen;
                //            uint8_t         pad0;
                //        } xcb_key_press_event_t;
                //
                // EVENT FIELDS
                //        response_type
                //                  The type of this event, in this case XCB_KEY_RELEASE. This field is also present in the xcb_generic_event_t and can be used to tell events apart from each other.
                //
                //        sequence  The sequence number of the last request processed by the X11 server.
                //
                //        detail    The keycode (a number representing a physical key on the keyboard) of the key which was pressed.
                //
                //        time      Time when the event was generated (in milliseconds).
                //
                //        root      The root window of child.
                //
                //        event     NOT YET DOCUMENTED.
                //
                //        child     NOT YET DOCUMENTED.
                //
                //        root_x    The X coordinate of the pointer relative to the root window at the time of the event.
                //
                //        root_y    The Y coordinate of the pointer relative to the root window at the time of the event.
                //
                //        event_x   If same_screen is true, this is the X coordinate relative to the event window's origin. Otherwise, event_x will be set to zero.
                //
                //        event_y   If same_screen is true, this is the Y coordinate relative to the event window's origin. Otherwise, event_y will be set to zero.
                //
                //        state     The logical state of the pointer buttons and modifier keys just prior to the event.
                //
                //        same_screen
                //                  Whether the event window is on the same screen as the root window.
                //
                // DESCRIPTION
                // SEE ALSO
                //        xcb_generic_event_t(3), xcb_grab_key(3), xcb_grab_keyboard(3)
                //
                // AUTHOR
                //        Generated from xproto.xml. Contact xcb@lists.freedesktop.org for corrections and improvements.
                //
                // X Version 11                                                                             libxcb 1.17.0                                                                 xcb_key_press_event_t(3)
                
                let k = key_event->detail;
                BUSTER_UNUSED(k);
            }
            break;
            case XCB_BUTTON_PRESS: // 4
            case XCB_BUTTON_RELEASE: // 5
            {
                switch (event_type)
                {
                    break; case XCB_BUTTON_PRESS: string8_print(S8("BUTTON_PRESS"));
                    break; case XCB_BUTTON_RELEASE: string8_print(S8("BUTTON_RELEASE"));
                    break; default: BUSTER_UNREACHABLE();
                }

                let button_event = (xcb_button_press_event_t*)event;

                // xcb_button_press_event_t(3)                                                                XCB Events                                                               xcb_button_press_event_t(3)
                //
                // NAME
                //        xcb_button_press_event_t - a mouse button was pressed/released
                //
                // SYNOPSIS
                //        #include <xcb/xproto.h>
                //
                //    Event datastructure
                //        typedef struct xcb_button_press_event_t {
                //            uint8_t         response_type;
                //            xcb_button_t    detail;
                //            uint16_t        sequence;
                //            xcb_timestamp_t time;
                //            xcb_window_t    root;
                //            xcb_window_t    event;
                //            xcb_window_t    child;
                //            int16_t         root_x;
                //            int16_t         root_y;
                //            int16_t         event_x;
                //            int16_t         event_y;
                //            uint16_t        state;
                //            uint8_t         same_screen;
                //            uint8_t         pad0;
                //        } xcb_button_press_event_t;
                //
                // EVENT FIELDS
                //        response_type
                //                  The type of this event, in this case XCB_BUTTON_RELEASE. This field is also present in the xcb_generic_event_t and can be used to tell events apart from each other.
                //
                //        sequence  The sequence number of the last request processed by the X11 server.
                //
                //        detail    The keycode (a number representing a physical key on the keyboard) of the key which was pressed.
                //
                //        time      Time when the event was generated (in milliseconds).
                //
                //        root      The root window of child.
                //
                //        event     NOT YET DOCUMENTED.
                //
                //        child     NOT YET DOCUMENTED.
                //
                //        root_x    The X coordinate of the pointer relative to the root window at the time of the event.
                //
                //        root_y    The Y coordinate of the pointer relative to the root window at the time of the event.
                //
                //        event_x   If same_screen is true, this is the X coordinate relative to the event window's origin. Otherwise, event_x will be set to zero.
                //
                //        event_y   If same_screen is true, this is the Y coordinate relative to the event window's origin. Otherwise, event_y will be set to zero.
                //
                //        state     The logical state of the pointer buttons and modifier keys just prior to the event.
                //
                //        same_screen
                //                  Whether the event window is on the same screen as the root window.
                //
                // DESCRIPTION
                // SEE ALSO
                //        xcb_generic_event_t(3), xcb_grab_button(3), xcb_grab_pointer(3)
                //
                // AUTHOR
                //        Generated from xproto.xml. Contact xcb@lists.freedesktop.org for corrections and improvements.
                //
                // X Version 11                                                                             libxcb 1.17.0                                                              xcb_button_press_event_t(3)

                let k = button_event->detail;
                BUSTER_UNUSED(k);
            }
            break; case XCB_MOTION_NOTIFY: // 6
            {
                string8_print(S8("MOTION_NOTIFY"));
                let motion_notify_event = (xcb_motion_notify_event_t*)event;
                let k = motion_notify_event->detail;

                // xcb_motion_notify_event_t(3)                                                               XCB Events                                                              xcb_motion_notify_event_t(3)
                //
                // NAME
                //        xcb_motion_notify_event_t - a key was pressed
                //
                // SYNOPSIS
                //        #include <xcb/xproto.h>
                //
                //    Event datastructure
                //        typedef struct xcb_motion_notify_event_t {
                //            uint8_t         response_type;
                //            uint8_t         detail;
                //            uint16_t        sequence;
                //            xcb_timestamp_t time;
                //            xcb_window_t    root;
                //            xcb_window_t    event;
                //            xcb_window_t    child;
                //            int16_t         root_x;
                //            int16_t         root_y;
                //            int16_t         event_x;
                //            int16_t         event_y;
                //            uint16_t        state;
                //            uint8_t         same_screen;
                //            uint8_t         pad0;
                //        } xcb_motion_notify_event_t;
                //
                // EVENT FIELDS
                //        response_type
                //                  The type of this event, in this case XCB_MOTION_NOTIFY. This field is also present in the xcb_generic_event_t and can be used to tell events apart from each other.
                //
                //        sequence  The sequence number of the last request processed by the X11 server.
                //
                //        detail    The keycode (a number representing a physical key on the keyboard) of the key which was pressed.
                //
                //        time      Time when the event was generated (in milliseconds).
                //
                //        root      The root window of child.
                //
                //        event     NOT YET DOCUMENTED.
                //
                //        child     NOT YET DOCUMENTED.
                //
                //        root_x    The X coordinate of the pointer relative to the root window at the time of the event.
                //
                //        root_y    The Y coordinate of the pointer relative to the root window at the time of the event.
                //
                //        event_x   If same_screen is true, this is the X coordinate relative to the event window's origin. Otherwise, event_x will be set to zero.
                //
                //        event_y   If same_screen is true, this is the Y coordinate relative to the event window's origin. Otherwise, event_y will be set to zero.
                //
                //        state     The logical state of the pointer buttons and modifier keys just prior to the event.
                //
                //        same_screen
                //                  Whether the event window is on the same screen as the root window.
                //
                // DESCRIPTION
                // SEE ALSO
                //        xcb_generic_event_t(3), xcb_grab_key(3), xcb_grab_keyboard(3)
                //
                // AUTHOR
                //        Generated from xproto.xml. Contact xcb@lists.freedesktop.org for corrections and improvements.
                //
                // X Version 11                                                                             libxcb 1.17.0                                                             xcb_motion_notify_event_t(3)

                BUSTER_UNUSED(k);
            }
            break;
            case XCB_ENTER_NOTIFY: // 7
            case XCB_LEAVE_NOTIFY: // 8
            {
                switch (event_type)
                {
                    break; case XCB_ENTER_NOTIFY: string8_print(S8("ENTER_NOTIFY"));
                    break; case XCB_LEAVE_NOTIFY: string8_print(S8("LEAVE_NOTIFY"));
                    break; default: BUSTER_UNREACHABLE();
                }

                let enter_leave_event = (xcb_enter_notify_event_t*)event;

                //                 xcb_enter_notify_event_t(3)                                                                XCB Events                                                               xcb_enter_notify_event_t(3)
                //
                // NAME
                //        xcb_enter_notify_event_t - the pointer is in a different window
                //
                // SYNOPSIS
                //        #include <xcb/xproto.h>
                //
                //    Event datastructure
                //        typedef struct xcb_enter_notify_event_t {
                //            uint8_t         response_type;
                //            uint8_t         detail;
                //            uint16_t        sequence;
                //            xcb_timestamp_t time;
                //            xcb_window_t    root;
                //            xcb_window_t    event;
                //            xcb_window_t    child;
                //            int16_t         root_x;
                //            int16_t         root_y;
                //            int16_t         event_x;
                //            int16_t         event_y;
                //            uint16_t        state;
                //            uint8_t         mode;
                //            uint8_t         same_screen_focus;
                //        } xcb_enter_notify_event_t;
                //
                // EVENT FIELDS
                //        response_type
                //                  The type of this event, in this case XCB_LEAVE_NOTIFY. This field is also present in the xcb_generic_event_t and can be used to tell events apart from each other.
                //
                //        sequence  The sequence number of the last request processed by the X11 server.
                //
                //        detail    NOT YET DOCUMENTED.
                //
                //        time      NOT YET DOCUMENTED.
                //
                //        root      The root window for the final cursor position.
                //
                //        event     The window on which the event was generated.
                //
                //        child     If the event window has subwindows and the final pointer position is in one of them, then child is set to that subwindow, XCB_WINDOW_NONE otherwise.
                //
                //        root_x    The pointer X coordinate relative to root's origin at the time of the event.
                //
                //        root_y    The pointer Y coordinate relative to root's origin at the time of the event.
                //
                //        event_x   If event is on the same screen as root, this is the pointer X coordinate relative to the event window's origin.
                //
                //        event_y   If event is on the same screen as root, this is the pointer Y coordinate relative to the event window's origin.
                //
                //        state     NOT YET DOCUMENTED.
                //
                //        mode
                //
                //        same_screen_focus
                //                  NOT YET DOCUMENTED.
                //
                // DESCRIPTION
                // SEE ALSO
                //        xcb_generic_event_t(3)
                //
                // AUTHOR
                //        Generated from xproto.xml. Contact xcb@lists.freedesktop.org for corrections and improvements.
                //
                // X Version 11                                                                             libxcb 1.17.0                                                              xcb_enter_notify_event_t(3)

                let k = enter_leave_event->detail;
                BUSTER_UNUSED(k);
            }
            break; case XCB_FOCUS_IN: // 9
            break; case XCB_FOCUS_OUT: // 10
            {
                switch (event_type)
                {
                    break; case XCB_FOCUS_IN: string8_print(S8("FOCUS_IN"));
                    break; case XCB_FOCUS_OUT: string8_print(S8("FOCUS_OUT"));
                    break; default: BUSTER_UNREACHABLE();
                }
                
                //                 xcb_focus_in_event_t(3)                                                                    XCB Events                                                                   xcb_focus_in_event_t(3)
                //
                // NAME
                //        xcb_focus_in_event_t - NOT YET DOCUMENTED
                //
                // SYNOPSIS
                //        #include <xcb/xproto.h>
                //
                //    Event datastructure
                //        typedef struct xcb_focus_in_event_t {
                //            uint8_t      response_type;
                //            uint8_t      detail;
                //            uint16_t     sequence;
                //            xcb_window_t event;
                //            uint8_t      mode;
                //            uint8_t      pad0[3];
                //        } xcb_focus_in_event_t;
                //
                // EVENT FIELDS
                //        response_type
                //                  The type of this event, in this case XCB_FOCUS_OUT. This field is also present in the xcb_generic_event_t and can be used to tell events apart from each other.
                //
                //        sequence  The sequence number of the last request processed by the X11 server.
                //
                //        detail
                //
                //        event     The window on which the focus event was generated. This is the window used by the X server to report the event.
                //
                //        mode
                //
                // DESCRIPTION
                // SEE ALSO
                //        xcb_generic_event_t(3)
                //
                // AUTHOR
                //        Generated from xproto.xml. Contact xcb@lists.freedesktop.org for corrections and improvements.
                //
                // X Version 11                                                                             libxcb 1.17.0                                                                  xcb_focus_in_event_t(3)

                let focus_event = (xcb_focus_in_event_t*)event;
                let k = focus_event->detail;
                BUSTER_UNUSED(k);
            }
            break; case XCB_KEYMAP_NOTIFY: // 11
            {
                string8_print(S8("KEYMAP_NOTIFY"));
                
                // xcb_keymap_notify_event_t(3)                                                               XCB Events                                                              xcb_keymap_notify_event_t(3)
                //
                // NAME
                //        xcb_keymap_notify_event_t -
                //
                // SYNOPSIS
                //        #include <xcb/xproto.h>
                //
                //    Event datastructure
                //        typedef struct xcb_keymap_notify_event_t {
                //            uint8_t response_type;
                //            uint8_t keys[31];
                //        } xcb_keymap_notify_event_t;
                //
                // EVENT FIELDS
                //        response_type
                //                  The type of this event, in this case XCB_KEYMAP_NOTIFY. This field is also present in the xcb_generic_event_t and can be used to tell events apart from each other.
                //
                //        sequence  The sequence number of the last request processed by the X11 server.
                //
                //        keys      NOT YET DOCUMENTED.
                //
                // DESCRIPTION
                // SEE ALSO
                // AUTHOR
                //        Generated from xproto.xml. Contact xcb@lists.freedesktop.org for corrections and improvements.
                //
                // X Version 11                                                                             libxcb 1.17.0                                                             xcb_keymap_notify_event_t(3)


                let keymap_notify_event = (xcb_keymap_notify_event_t*)event;
                let k = keymap_notify_event->response_type;
                BUSTER_UNUSED(k);
            }
            break; case XCB_EXPOSE: // 12
            {
                // This is the classical repaint event
                string8_print(S8("EXPOSE"));
                let expose_event = (xcb_expose_event_t*)event;
                string8_print(S8("\nRepaint ({u16}). X: {u16}. Y: {u16}. Width: {u16}. Height: {u16}\n"), expose_event->count, expose_event->x, expose_event->y, expose_event->width, expose_event->height);
                unimplemented = false;

                //                 xcb_expose_event_t(3)                                                                      XCB Events                                                                     xcb_expose_event_t(3)
                //
                // NAME
                //        xcb_expose_event_t - NOT YET DOCUMENTED
                //
                // SYNOPSIS
                //        #include <xcb/xproto.h>
                //
                //    Event datastructure
                //        typedef struct xcb_expose_event_t {
                //            uint8_t      response_type;
                //            uint8_t      pad0;
                //            uint16_t     sequence;
                //            xcb_window_t window;
                //            uint16_t     x;
                //            uint16_t     y;
                //            uint16_t     width;
                //            uint16_t     height;
                //            uint16_t     count;
                //            uint8_t      pad1[2];
                //        } xcb_expose_event_t;
                //
                // EVENT FIELDS
                //        response_type
                //                  The type of this event, in this case XCB_EXPOSE. This field is also present in the xcb_generic_event_t and can be used to tell events apart from each other.
                //
                //        sequence  The sequence number of the last request processed by the X11 server.
                //
                //        window    The exposed (damaged) window.
                //
                //        x         The X coordinate of the left-upper corner of the exposed rectangle, relative to the window's origin.
                //
                //        y         The Y coordinate of the left-upper corner of the exposed rectangle, relative to the window's origin.
                //
                //        width     The width of the exposed rectangle.
                //
                //        height    The height of the exposed rectangle.
                //
                //        count     The  amount  of  Expose events following this one. Simple applications that do not want to optimize redisplay by distinguishing between subareas of its window can just ignore
                //                  all Expose events with nonzero counts and perform full redisplays on events with zero counts.
                //
                // DESCRIPTION
                // SEE ALSO
                //        xcb_generic_event_t(3)
                //
                // AUTHOR
                //        Generated from xproto.xml. Contact xcb@lists.freedesktop.org for corrections and improvements.
                //
                // X Version 11                                                                             libxcb 1.17.0                                                                    xcb_expose_event_t(3)
            }
            break; case XCB_GRAPHICS_EXPOSURE: // 13
            {
                string8_print(S8("GRAPHICS_EXPOSURE"));
                let graphics_exposure_event = (xcb_graphics_exposure_event_t*)event;
                let k = graphics_exposure_event->response_type;
                BUSTER_UNUSED(k);

                //                 xcb_graphics_exposure_event_t(3)                                                           XCB Events                                                          xcb_graphics_exposure_event_t(3)
                //
                // NAME
                //        xcb_graphics_exposure_event_t -
                //
                // SYNOPSIS
                //        #include <xcb/xproto.h>
                //
                //    Event datastructure
                //        typedef struct xcb_graphics_exposure_event_t {
                //            uint8_t        response_type;
                //            uint8_t        pad0;
                //            uint16_t       sequence;
                //            xcb_drawable_t drawable;
                //            uint16_t       x;
                //            uint16_t       y;
                //            uint16_t       width;
                //            uint16_t       height;
                //            uint16_t       minor_opcode;
                //            uint16_t       count;
                //            uint8_t        major_opcode;
                //            uint8_t        pad1[3];
                //        } xcb_graphics_exposure_event_t;
                //
                // EVENT FIELDS
                //        response_type
                //                  The type of this event, in this case XCB_GRAPHICS_EXPOSURE. This field is also present in the xcb_generic_event_t and can be used to tell events apart from each other.
                //
                //        sequence  The sequence number of the last request processed by the X11 server.
                //
                //        drawable  NOT YET DOCUMENTED.
                //
                //        x         NOT YET DOCUMENTED.
                //
                //        y         NOT YET DOCUMENTED.
                //
                //        width     NOT YET DOCUMENTED.
                //
                //        height    NOT YET DOCUMENTED.
                //
                //        minor_opcode
                //                  NOT YET DOCUMENTED.
                //
                //        count     NOT YET DOCUMENTED.
                //
                //        major_opcode
                //                  NOT YET DOCUMENTED.
                //
                // DESCRIPTION
                // SEE ALSO
                // AUTHOR
                //        Generated from xproto.xml. Contact xcb@lists.freedesktop.org for corrections and improvements.
                //
                // X Version 11                                                                             libxcb 1.17.0                                                         xcb_graphics_exposure_event_t(3)

            }
            break; case XCB_NO_EXPOSURE: // 14
            {
                string8_print(S8("NO_EXPOSURE"));

                let no_exposure_event = (xcb_no_exposure_event_t*)event;
                let k = no_exposure_event->response_type;
                BUSTER_UNUSED(k);

                //                 xcb_no_exposure_event_t(3)                                                                 XCB Events                                                                xcb_no_exposure_event_t(3)
                //
                // NAME
                //        xcb_no_exposure_event_t -
                //
                // SYNOPSIS
                //        #include <xcb/xproto.h>
                //
                //    Event datastructure
                //        typedef struct xcb_no_exposure_event_t {
                //            uint8_t        response_type;
                //            uint8_t        pad0;
                //            uint16_t       sequence;
                //            xcb_drawable_t drawable;
                //            uint16_t       minor_opcode;
                //            uint8_t        major_opcode;
                //            uint8_t        pad1;
                //        } xcb_no_exposure_event_t;
                //
                // EVENT FIELDS
                //        response_type
                //                  The type of this event, in this case XCB_NO_EXPOSURE. This field is also present in the xcb_generic_event_t and can be used to tell events apart from each other.
                //
                //        sequence  The sequence number of the last request processed by the X11 server.
                //
                //        drawable  NOT YET DOCUMENTED.
                //
                //        minor_opcode
                //                  NOT YET DOCUMENTED.
                //
                //        major_opcode
                //                  NOT YET DOCUMENTED.
                //
                // DESCRIPTION
                // SEE ALSO
                // AUTHOR
                //        Generated from xproto.xml. Contact xcb@lists.freedesktop.org for corrections and improvements.
                //
                // X Version 11                                                                             libxcb 1.17.0                                                               xcb_no_exposure_event_t(3)

            }
            break; case XCB_VISIBILITY_NOTIFY: // 15
            {
                string8_print(S8("VISIBILITY_NOTIFY"));

                let visibility_notify_event = (xcb_visibility_notify_event_t*)event;

                string8_print(S8("\nVisibility changed: {u8:x}\n"), visibility_notify_event->state);
                unimplemented = false;
                //                 xcb_visibility_notify_event_t(3)                                                           XCB Events                                                          xcb_visibility_notify_event_t(3)
                //
                // NAME
                //        xcb_visibility_notify_event_t -
                //
                // SYNOPSIS
                //        #include <xcb/xproto.h>
                //
                //    Event datastructure
                //        typedef struct xcb_visibility_notify_event_t {
                //            uint8_t      response_type;
                //            uint8_t      pad0;
                //            uint16_t     sequence;
                //            xcb_window_t window;
                //            uint8_t      state;
                //            uint8_t      pad1[3];
                //        } xcb_visibility_notify_event_t;
                //
                // EVENT FIELDS
                //        response_type
                //                  The type of this event, in this case XCB_VISIBILITY_NOTIFY. This field is also present in the xcb_generic_event_t and can be used to tell events apart from each other.
                //
                //        sequence  The sequence number of the last request processed by the X11 server.
                //
                //        window    NOT YET DOCUMENTED.
                //
                //        state     NOT YET DOCUMENTED.
                //
                // DESCRIPTION
                // SEE ALSO
                // AUTHOR
                //        Generated from xproto.xml. Contact xcb@lists.freedesktop.org for corrections and improvements.
                //
                // X Version 11                                                                             libxcb 1.17.0                                                         xcb_visibility_notify_event_t(3)
            }
            break; case XCB_CREATE_NOTIFY: // 16
            {
                string8_print(S8("CREATE_NOTIFY"));

                let create_notify_event = (xcb_create_notify_event_t*)event;
                //                 xcb_create_notify_event_t(3)                                                               XCB Events                                                              xcb_create_notify_event_t(3)
                //
                // NAME
                //        xcb_create_notify_event_t -
                //
                // SYNOPSIS
                //        #include <xcb/xproto.h>
                //
                //    Event datastructure
                //        typedef struct xcb_create_notify_event_t {
                //            uint8_t      response_type;
                //            uint8_t      pad0;
                //            uint16_t     sequence;
                //            xcb_window_t parent;
                //            xcb_window_t window;
                //            int16_t      x;
                //            int16_t      y;
                //            uint16_t     width;
                //            uint16_t     height;
                //            uint16_t     border_width;
                //            uint8_t      override_redirect;
                //            uint8_t      pad1;
                //        } xcb_create_notify_event_t;
                //
                // EVENT FIELDS
                //        response_type
                //                  The type of this event, in this case XCB_CREATE_NOTIFY. This field is also present in the xcb_generic_event_t and can be used to tell events apart from each other.
                //
                //        sequence  The sequence number of the last request processed by the X11 server.
                //
                //        parent    NOT YET DOCUMENTED.
                //
                //        window    NOT YET DOCUMENTED.
                //
                //        x         NOT YET DOCUMENTED.
                //
                //        y         NOT YET DOCUMENTED.
                //
                //        width     NOT YET DOCUMENTED.
                //
                //        height    NOT YET DOCUMENTED.
                //
                //        border_width
                //                  NOT YET DOCUMENTED.
                //
                //        override_redirect
                //                  NOT YET DOCUMENTED.
                //
                // DESCRIPTION
                // SEE ALSO
                // AUTHOR
                //        Generated from xproto.xml. Contact xcb@lists.freedesktop.org for corrections and improvements.
                //
                // X Version 11                                                                             libxcb 1.17.0                                                             xcb_create_notify_event_t(3)
                
                let k = create_notify_event->response_type;
                BUSTER_UNUSED(k);
            }
            break; case XCB_DESTROY_NOTIFY: // 17
            {
                string8_print(S8("DESTROY_NOTIFY"));

                let destroy_notify_event = (xcb_destroy_notify_event_t*)event;

                //                 xcb_destroy_notify_event_t(3)                                                              XCB Events                                                             xcb_destroy_notify_event_t(3)
                //
                // NAME
                //        xcb_destroy_notify_event_t - a window is destroyed
                //
                // SYNOPSIS
                //        #include <xcb/xproto.h>
                //
                //    Event datastructure
                //        typedef struct xcb_destroy_notify_event_t {
                //            uint8_t      response_type;
                //            uint8_t      pad0;
                //            uint16_t     sequence;
                //            xcb_window_t event;
                //            xcb_window_t window;
                //        } xcb_destroy_notify_event_t;
                //
                // EVENT FIELDS
                //        response_type
                //                  The type of this event, in this case XCB_DESTROY_NOTIFY. This field is also present in the xcb_generic_event_t and can be used to tell events apart from each other.
                //
                //        sequence  The sequence number of the last request processed by the X11 server.
                //
                //        event     The reconfigured window or its parent, depending on whether StructureNotify or SubstructureNotify was selected.
                //
                //        window    The window that is destroyed.
                //
                // DESCRIPTION
                // SEE ALSO
                //        xcb_generic_event_t(3), xcb_destroy_window(3)
                //
                // AUTHOR
                //        Generated from xproto.xml. Contact xcb@lists.freedesktop.org for corrections and improvements.
                //
                // X Version 11                                                                             libxcb 1.17.0                                                            xcb_destroy_notify_event_t(3)
                let k = destroy_notify_event->response_type;

                BUSTER_UNUSED(k);

            }
            break; case XCB_UNMAP_NOTIFY: // 18
            {
                string8_print(S8("UNMAP_NOTIFY"));

                let unmap_notify_event = (xcb_unmap_notify_event_t*)event;
                let k = unmap_notify_event->response_type;
                BUSTER_UNUSED(k);

                //                 xcb_unmap_notify_event_t(3)                                                                XCB Events                                                               xcb_unmap_notify_event_t(3)
                //
                // NAME
                //        xcb_unmap_notify_event_t - a window is unmapped
                //
                // SYNOPSIS
                //        #include <xcb/xproto.h>
                //
                //    Event datastructure
                //        typedef struct xcb_unmap_notify_event_t {
                //            uint8_t      response_type;
                //            uint8_t      pad0;
                //            uint16_t     sequence;
                //            xcb_window_t event;
                //            xcb_window_t window;
                //            uint8_t      from_configure;
                //            uint8_t      pad1[3];
                //        } xcb_unmap_notify_event_t;
                //
                // EVENT FIELDS
                //        response_type
                //                  The type of this event, in this case XCB_UNMAP_NOTIFY. This field is also present in the xcb_generic_event_t and can be used to tell events apart from each other.
                //
                //        sequence  The sequence number of the last request processed by the X11 server.
                //
                //        event     The reconfigured window or its parent, depending on whether StructureNotify or SubstructureNotify was selected.
                //
                //        window    The window that was unmapped.
                //
                //        from_configure
                //                  Set to 1 if the event was generated as a result of a resizing of the window's parent when window had a win_gravity of UnmapGravity.
                //
                // DESCRIPTION
                // SEE ALSO
                //        xcb_generic_event_t(3), xcb_unmap_window(3)
                //
                // AUTHOR
                //        Generated from xproto.xml. Contact xcb@lists.freedesktop.org for corrections and improvements.
                //
                // X Version 11                                                                             libxcb 1.17.0                                                              xcb_unmap_notify_event_t(3)

            }
            break; case XCB_MAP_NOTIFY: // 19
            {
                string8_print(S8("MAP_NOTIFY"));

                let map_notify_event = (xcb_map_notify_event_t*)event;
                let k = map_notify_event->response_type;
                BUSTER_UNUSED(k);
                string8_print(S8("\nWindow is showing!\n"));
                unimplemented = false;

                //                 xcb_map_notify_event_t(3)                                                                  XCB Events                                                                 xcb_map_notify_event_t(3)
                //
                // NAME
                //        xcb_map_notify_event_t - a window was mapped
                //
                // SYNOPSIS
                //        #include <xcb/xproto.h>
                //
                //    Event datastructure
                //        typedef struct xcb_map_notify_event_t {
                //            uint8_t      response_type;
                //            uint8_t      pad0;
                //            uint16_t     sequence;
                //            xcb_window_t event;
                //            xcb_window_t window;
                //            uint8_t      override_redirect;
                //            uint8_t      pad1[3];
                //        } xcb_map_notify_event_t;
                //
                // EVENT FIELDS
                //        response_type
                //                  The type of this event, in this case XCB_MAP_NOTIFY. This field is also present in the xcb_generic_event_t and can be used to tell events apart from each other.
                //
                //        sequence  The sequence number of the last request processed by the X11 server.
                //
                //        event     The window which was mapped or its parent, depending on whether StructureNotify or SubstructureNotify was selected.
                //
                //        window    The window that was mapped.
                //
                //        override_redirect
                //                  Window managers should ignore this window if override_redirect is 1.
                //
                // DESCRIPTION
                // SEE ALSO
                //        xcb_generic_event_t(3), xcb_map_window(3)
                //
                // AUTHOR
                //        Generated from xproto.xml. Contact xcb@lists.freedesktop.org for corrections and improvements.
                //
                // X Version 11                                                                             libxcb 1.17.0                                                                xcb_map_notify_event_t(3)

            }
            break; case XCB_MAP_REQUEST: // 20
            {
                string8_print(S8("MAP_REQUEST"));

                let map_request_event = (xcb_map_request_event_t*)event;
                let k = map_request_event->response_type;
                BUSTER_UNUSED(k);

                //                 xcb_map_request_event_t(3)                                                                 XCB Events                                                                xcb_map_request_event_t(3)
                //
                // NAME
                //        xcb_map_request_event_t - window wants to be mapped
                //
                // SYNOPSIS
                //        #include <xcb/xproto.h>
                //
                //    Event datastructure
                //        typedef struct xcb_map_request_event_t {
                //            uint8_t      response_type;
                //            uint8_t      pad0;
                //            uint16_t     sequence;
                //            xcb_window_t parent;
                //            xcb_window_t window;
                //        } xcb_map_request_event_t;
                //
                // EVENT FIELDS
                //        response_type
                //                  The type of this event, in this case XCB_MAP_REQUEST. This field is also present in the xcb_generic_event_t and can be used to tell events apart from each other.
                //
                //        sequence  The sequence number of the last request processed by the X11 server.
                //
                //        parent    The parent of window.
                //
                //        window    The window to be mapped.
                //
                // DESCRIPTION
                // SEE ALSO
                //        xcb_generic_event_t(3), xcb_map_window(3)
                //
                // AUTHOR
                //        Generated from xproto.xml. Contact xcb@lists.freedesktop.org for corrections and improvements.
                //
                // X Version 11                                                                             libxcb 1.17.0                                                               xcb_map_request_event_t(3)

            }
            break; case XCB_REPARENT_NOTIFY: // 21
            {
                string8_print(S8("REPARENT_NOTIFY"));

                let reparent_notify_event = (xcb_reparent_notify_event_t*)event;
                let k = reparent_notify_event->response_type;
                BUSTER_UNUSED(k);

                //                 xcb_reparent_notify_event_t(3)                                                             XCB Events                                                            xcb_reparent_notify_event_t(3)
                //
                // NAME
                //        xcb_reparent_notify_event_t -
                //
                // SYNOPSIS
                //        #include <xcb/xproto.h>
                //
                //    Event datastructure
                //        typedef struct xcb_reparent_notify_event_t {
                //            uint8_t      response_type;
                //            uint8_t      pad0;
                //            uint16_t     sequence;
                //            xcb_window_t event;
                //            xcb_window_t window;
                //            xcb_window_t parent;
                //            int16_t      x;
                //            int16_t      y;
                //            uint8_t      override_redirect;
                //            uint8_t      pad1[3];
                //        } xcb_reparent_notify_event_t;
                //
                // EVENT FIELDS
                //        response_type
                //                  The type of this event, in this case XCB_REPARENT_NOTIFY. This field is also present in the xcb_generic_event_t and can be used to tell events apart from each other.
                //
                //        sequence  The sequence number of the last request processed by the X11 server.
                //
                //        event     NOT YET DOCUMENTED.
                //
                //        window    NOT YET DOCUMENTED.
                //
                //        parent    NOT YET DOCUMENTED.
                //
                //        x         NOT YET DOCUMENTED.
                //
                //        y         NOT YET DOCUMENTED.
                //
                //        override_redirect
                //                  NOT YET DOCUMENTED.
                //
                // DESCRIPTION
                // SEE ALSO
                // AUTHOR
                //        Generated from xproto.xml. Contact xcb@lists.freedesktop.org for corrections and improvements.
                //
                // X Version 11                                                                             libxcb 1.17.0                                                           xcb_reparent_notify_event_t(3)

            }
            break; case XCB_CONFIGURE_NOTIFY: // 22
            {
                string8_print(S8("CONFIGURE_NOTIFY"));

                let configure_notify_event = (xcb_configure_notify_event_t*)event;
                let width = configure_notify_event->width;
                let height = configure_notify_event->height;

                string8_print(S8("\nConfiguration changed! Width: {s16}. Height: {s16}\n"), width, height);

                unimplemented = false;

                //                 xcb_configure_notify_event_t(3)                                                            XCB Events                                                           xcb_configure_notify_event_t(3)
                //
                // NAME
                //        xcb_configure_notify_event_t - NOT YET DOCUMENTED
                //
                // SYNOPSIS
                //        #include <xcb/xproto.h>
                //
                //    Event datastructure
                //        typedef struct xcb_configure_notify_event_t {
                //            uint8_t      response_type;
                //            uint8_t      pad0;
                //            uint16_t     sequence;
                //            xcb_window_t event;
                //            xcb_window_t window;
                //            xcb_window_t above_sibling;
                //            int16_t      x;
                //            int16_t      y;
                //            uint16_t     width;
                //            uint16_t     height;
                //            uint16_t     border_width;
                //            uint8_t      override_redirect;
                //            uint8_t      pad1;
                //        } xcb_configure_notify_event_t;
                //
                // EVENT FIELDS
                //        response_type
                //                  The type of this event, in this case XCB_CONFIGURE_NOTIFY. This field is also present in the xcb_generic_event_t and can be used to tell events apart from each other.
                //
                //        sequence  The sequence number of the last request processed by the X11 server.
                //
                //        event     The reconfigured window or its parent, depending on whether StructureNotify or SubstructureNotify was selected.
                //
                //        window    The window whose size, position, border, and/or stacking order was changed.
                //
                //        above_sibling
                //                  If  XCB_NONE,  the window is on the bottom of the stack with respect to sibling windows. However, if set to a sibling window, the window is placed on top of this sibling win‐
                //                  dow.
                //
                //        x         The X coordinate of the upper-left outside corner of window, relative to the parent window's origin.
                //
                //        y         The Y coordinate of the upper-left outside corner of window, relative to the parent window's origin.
                //
                //        width     The inside width of window, not including the border.
                //
                //        height    The inside height of window, not including the border.
                //
                //        border_width
                //                  The border width of window.
                //
                //        override_redirect
                //                  Window managers should ignore this window if override_redirect is 1.
                //
                // DESCRIPTION
                // SEE ALSO
                //        xcb_generic_event_t(3), xcb_free_colormap(3)
                //
                // AUTHOR
                //        Generated from xproto.xml. Contact xcb@lists.freedesktop.org for corrections and improvements.
                //
                // X Version 11                                                                             libxcb 1.17.0                                                          xcb_configure_notify_event_t(3)

            }
            break; case XCB_CONFIGURE_REQUEST: // 23
            {
                string8_print(S8("CONFIGURE_REQUEST"));
                let configure_request_event = (xcb_configure_request_event_t*)event;

                //                 xcb_configure_request_event_t(3)                                                           XCB Events                                                          xcb_configure_request_event_t(3)
                //
                // NAME
                //        xcb_configure_request_event_t -
                //
                // SYNOPSIS
                //        #include <xcb/xproto.h>
                //
                //    Event datastructure
                //        typedef struct xcb_configure_request_event_t {
                //            uint8_t      response_type;
                //            uint8_t      stack_mode;
                //            uint16_t     sequence;
                //            xcb_window_t parent;
                //            xcb_window_t window;
                //            xcb_window_t sibling;
                //            int16_t      x;
                //            int16_t      y;
                //            uint16_t     width;
                //            uint16_t     height;
                //            uint16_t     border_width;
                //            uint16_t     value_mask;
                //        } xcb_configure_request_event_t;
                //
                // EVENT FIELDS
                //        response_type
                //                  The type of this event, in this case XCB_CONFIGURE_REQUEST. This field is also present in the xcb_generic_event_t and can be used to tell events apart from each other.
                //
                //        sequence  The sequence number of the last request processed by the X11 server.
                //
                //        stack_mode
                //                  NOT YET DOCUMENTED.
                //
                //        parent    NOT YET DOCUMENTED.
                //
                //        window    NOT YET DOCUMENTED.
                //
                //        sibling   NOT YET DOCUMENTED.
                //
                //        x         NOT YET DOCUMENTED.
                //
                //        y         NOT YET DOCUMENTED.
                //
                //        width     NOT YET DOCUMENTED.
                //
                //        height    NOT YET DOCUMENTED.
                //
                //        border_width
                //                  NOT YET DOCUMENTED.
                //
                //        value_mask
                //                  NOT YET DOCUMENTED.
                //
                // DESCRIPTION
                // SEE ALSO
                // AUTHOR
                //        Generated from xproto.xml. Contact xcb@lists.freedesktop.org for corrections and improvements.
                //
                // X Version 11                                                                             libxcb 1.17.0                                                         xcb_configure_request_event_t(3)

                let k = configure_request_event->response_type;
                BUSTER_UNUSED(k);
            }
            break; case XCB_GRAVITY_NOTIFY: // 24
            {
                string8_print(S8("GRAVITY_NOTIFY"));
                let gravity_notify_event = (xcb_gravity_notify_event_t*)event;
                let k = gravity_notify_event->response_type;
                BUSTER_UNUSED(k);

                // xcb_gravity_notify_event_t(3)                                                              XCB Events                                                             xcb_gravity_notify_event_t(3)
                //
                // NAME
                //        xcb_gravity_notify_event_t -
                //
                // SYNOPSIS
                //        #include <xcb/xproto.h>
                //
                //    Event datastructure
                //        typedef struct xcb_gravity_notify_event_t {
                //            uint8_t      response_type;
                //            uint8_t      pad0;
                //            uint16_t     sequence;
                //            xcb_window_t event;
                //            xcb_window_t window;
                //            int16_t      x;
                //            int16_t      y;
                //        } xcb_gravity_notify_event_t;
                //
                // EVENT FIELDS
                //        response_type
                //                  The type of this event, in this case XCB_GRAVITY_NOTIFY. This field is also present in the xcb_generic_event_t and can be used to tell events apart from each other.
                //
                //        sequence  The sequence number of the last request processed by the X11 server.
                //
                //        event     NOT YET DOCUMENTED.
                //
                //        window    NOT YET DOCUMENTED.
                //
                //        x         NOT YET DOCUMENTED.
                //
                //        y         NOT YET DOCUMENTED.
                //
                // DESCRIPTION
                // SEE ALSO
                // AUTHOR
                //        Generated from xproto.xml. Contact xcb@lists.freedesktop.org for corrections and improvements.
                //
                // X Version 11                                                                             libxcb 1.17.0                                                            xcb_gravity_notify_event_t(3)


            }
            break; case XCB_RESIZE_REQUEST: // 25
            {
                string8_print(S8("RESIZE_REQUEST"));
                let resize_request_event = (xcb_resize_request_event_t*)event;
                let k = resize_request_event->response_type;
                BUSTER_UNUSED(k);

                //                 xcb_resize_request_event_t(3)                                                              XCB Events                                                             xcb_resize_request_event_t(3)
                //
                // NAME
                //        xcb_resize_request_event_t -
                //
                // SYNOPSIS
                //        #include <xcb/xproto.h>
                //
                //    Event datastructure
                //        typedef struct xcb_resize_request_event_t {
                //            uint8_t      response_type;
                //            uint8_t      pad0;
                //            uint16_t     sequence;
                //            xcb_window_t window;
                //            uint16_t     width;
                //            uint16_t     height;
                //        } xcb_resize_request_event_t;
                //
                // EVENT FIELDS
                //        response_type
                //                  The type of this event, in this case XCB_RESIZE_REQUEST. This field is also present in the xcb_generic_event_t and can be used to tell events apart from each other.
                //
                //        sequence  The sequence number of the last request processed by the X11 server.
                //
                //        window    NOT YET DOCUMENTED.
                //
                //        width     NOT YET DOCUMENTED.
                //
                //        height    NOT YET DOCUMENTED.
                //
                // DESCRIPTION
                // SEE ALSO
                // AUTHOR
                //        Generated from xproto.xml. Contact xcb@lists.freedesktop.org for corrections and improvements.
                //
                // X Version 11                                                                             libxcb 1.17.0                                                            xcb_resize_request_event_t(3)

            }
            break;
            case XCB_CIRCULATE_NOTIFY: // 26
            case XCB_CIRCULATE_REQUEST: // 27
            {
                switch (event_type)
                {
                    break; case XCB_CIRCULATE_NOTIFY: string8_print(S8("CIRCULATE_NOTIFY"));
                    break; case XCB_CIRCULATE_REQUEST: string8_print(S8("CIRCULATE_REQUEST"));
                    break; default: BUSTER_UNREACHABLE();
                }

                let circulate_event = (xcb_circulate_notify_event_t*)event;
                let k = circulate_event->response_type;
                BUSTER_UNUSED(k);

                //                 xcb_circulate_notify_event_t(3)                                                            XCB Events                                                           xcb_circulate_notify_event_t(3)
                //
                // NAME
                //        xcb_circulate_notify_event_t - NOT YET DOCUMENTED
                //
                // SYNOPSIS
                //        #include <xcb/xproto.h>
                //
                //    Event datastructure
                //        typedef struct xcb_circulate_notify_event_t {
                //            uint8_t      response_type;
                //            uint8_t      pad0;
                //            uint16_t     sequence;
                //            xcb_window_t event;
                //            xcb_window_t window;
                //            uint8_t      pad1[4];
                //            uint8_t      place;
                //            uint8_t      pad2[3];
                //        } xcb_circulate_notify_event_t;
                //
                // EVENT FIELDS
                //        response_type
                //                  The type of this event, in this case XCB_CIRCULATE_REQUEST. This field is also present in the xcb_generic_event_t and can be used to tell events apart from each other.
                //
                //        sequence  The sequence number of the last request processed by the X11 server.
                //
                //        event     Either the restacked window or its parent, depending on whether StructureNotify or SubstructureNotify was selected.
                //
                //        window    The restacked window.
                //
                //        place
                //
                // DESCRIPTION
                // SEE ALSO
                //        xcb_generic_event_t(3), xcb_circulate_window(3)
                //
                // AUTHOR
                //        Generated from xproto.xml. Contact xcb@lists.freedesktop.org for corrections and improvements.
                //
                // X Version 11                                                                             libxcb 1.17.0                                                          xcb_circulate_notify_event_t(3)

            }
            break; case XCB_PROPERTY_NOTIFY: // 28
            {
                string8_print(S8("PROPERTY_NOTIFY"));
                let property_notify_event = (xcb_property_notify_event_t*)event;
                let property_atom = property_notify_event->atom;
                let name_cookie = xcb_get_atom_name(connection, property_atom);
                xcb_generic_error_t* error;
                xcb_get_atom_name_reply_t* reply = xcb_get_atom_name_reply(connection, name_cookie, &error);
                if (reply)
                {
                    let name_pointer = xcb_get_atom_name_name(reply);
                    let name_length = xcb_get_atom_name_name_length(reply);
                    String8 name = { .pointer = name_pointer, .length = (u64)name_length };
                    string8_print(S8("\nProperty changed: {S8}\n"), name);
                    unimplemented = false;
                }

                //                 xcb_property_notify_event_t(3)                                                             XCB Events                                                            xcb_property_notify_event_t(3)
                //
                // NAME
                //        xcb_property_notify_event_t - a window property changed
                //
                // SYNOPSIS
                //        #include <xcb/xproto.h>
                //
                //    Event datastructure
                //        typedef struct xcb_property_notify_event_t {
                //            uint8_t         response_type;
                //            uint8_t         pad0;
                //            uint16_t        sequence;
                //            xcb_window_t    window;
                //            xcb_atom_t      atom;
                //            xcb_timestamp_t time;
                //            uint8_t         state;
                //            uint8_t         pad1[3];
                //        } xcb_property_notify_event_t;
                //
                // EVENT FIELDS
                //        response_type
                //                  The type of this event, in this case XCB_PROPERTY_NOTIFY. This field is also present in the xcb_generic_event_t and can be used to tell events apart from each other.
                //
                //        sequence  The sequence number of the last request processed by the X11 server.
                //
                //        window    The window whose associated property was changed.
                //
                //        atom      The property's atom, to indicate which property was changed.
                //
                //        time      A timestamp of the server time when the property was changed.
                //
                //        state
                //
                // DESCRIPTION
                // SEE ALSO
                //        xcb_generic_event_t(3), xcb_change_property(3)
                //
                // AUTHOR
                //        Generated from xproto.xml. Contact xcb@lists.freedesktop.org for corrections and improvements.
                //
                // X Version 11                                                                             libxcb 1.17.0                                                           xcb_property_notify_event_t(3)

            }
            break; case XCB_SELECTION_CLEAR: // 29
            {
                string8_print(S8("SELECTION_CLEAR"));

                let selection_clear_event = (xcb_selection_clear_event_t*)event;
                let k = selection_clear_event->response_type;
                BUSTER_UNUSED(k);

                //                 xcb_selection_clear_event_t(3)                                                             XCB Events                                                            xcb_selection_clear_event_t(3)
                //
                // NAME
                //        xcb_selection_clear_event_t -
                //
                // SYNOPSIS
                //        #include <xcb/xproto.h>
                //
                //    Event datastructure
                //        typedef struct xcb_selection_clear_event_t {
                //            uint8_t         response_type;
                //            uint8_t         pad0;
                //            uint16_t        sequence;
                //            xcb_timestamp_t time;
                //            xcb_window_t    owner;
                //            xcb_atom_t      selection;
                //        } xcb_selection_clear_event_t;
                //
                // EVENT FIELDS
                //        response_type
                //                  The type of this event, in this case XCB_SELECTION_CLEAR. This field is also present in the xcb_generic_event_t and can be used to tell events apart from each other.
                //
                //        sequence  The sequence number of the last request processed by the X11 server.
                //
                //        time      NOT YET DOCUMENTED.
                //
                //        owner     NOT YET DOCUMENTED.
                //
                //        selection NOT YET DOCUMENTED.
                //
                // DESCRIPTION
                // SEE ALSO
                // AUTHOR
                //        Generated from xproto.xml. Contact xcb@lists.freedesktop.org for corrections and improvements.
                //
                // X Version 11                                                                             libxcb 1.17.0                                                           xcb_selection_clear_event_t(3)

            }
            break; case XCB_SELECTION_REQUEST: // 30
            {
                string8_print(S8("SELECTION_REQUEST"));

                let selection_request_event = (xcb_selection_request_event_t*)event;
                let k = selection_request_event->response_type;
                BUSTER_UNUSED(k);

                //                 xcb_selection_request_event_t(3)                                                           XCB Events                                                          xcb_selection_request_event_t(3)
                //
                // NAME
                //        xcb_selection_request_event_t -
                //
                // SYNOPSIS
                //        #include <xcb/xproto.h>
                //
                //    Event datastructure
                //        typedef struct xcb_selection_request_event_t {
                //            uint8_t         response_type;
                //            uint8_t         pad0;
                //            uint16_t        sequence;
                //            xcb_timestamp_t time;
                //            xcb_window_t    owner;
                //            xcb_window_t    requestor;
                //            xcb_atom_t      selection;
                //            xcb_atom_t      target;
                //            xcb_atom_t      property;
                //        } xcb_selection_request_event_t;
                //
                // EVENT FIELDS
                //        response_type
                //                  The type of this event, in this case XCB_SELECTION_REQUEST. This field is also present in the xcb_generic_event_t and can be used to tell events apart from each other.
                //
                //        sequence  The sequence number of the last request processed by the X11 server.
                //
                //        time      NOT YET DOCUMENTED.
                //
                //        owner     NOT YET DOCUMENTED.
                //
                //        requestor NOT YET DOCUMENTED.
                //
                //        selection NOT YET DOCUMENTED.
                //
                //        target    NOT YET DOCUMENTED.
                //
                //        property  NOT YET DOCUMENTED.
                //
                // DESCRIPTION
                // SEE ALSO
                // AUTHOR
                //        Generated from xproto.xml. Contact xcb@lists.freedesktop.org for corrections and improvements.
                //
                // X Version 11                                                                             libxcb 1.17.0                                                         xcb_selection_request_event_t(3)

            }
            break; case XCB_SELECTION_NOTIFY: // 31
            {
                string8_print(S8("SELECTION_NOTIFY"));

                let selection_notify_event = (xcb_selection_notify_event_t*)event;
                let k = selection_notify_event->response_type;
                BUSTER_UNUSED(k);

                //                 xcb_selection_notify_event_t(3)                                                            XCB Events                                                           xcb_selection_notify_event_t(3)
                //
                // NAME
                //        xcb_selection_notify_event_t -
                //
                // SYNOPSIS
                //        #include <xcb/xproto.h>
                //
                //    Event datastructure
                //        typedef struct xcb_selection_notify_event_t {
                //            uint8_t         response_type;
                //            uint8_t         pad0;
                //            uint16_t        sequence;
                //            xcb_timestamp_t time;
                //            xcb_window_t    requestor;
                //            xcb_atom_t      selection;
                //            xcb_atom_t      target;
                //            xcb_atom_t      property;
                //        } xcb_selection_notify_event_t;
                //
                // EVENT FIELDS
                //        response_type
                //                  The type of this event, in this case XCB_SELECTION_NOTIFY. This field is also present in the xcb_generic_event_t and can be used to tell events apart from each other.
                //
                //        sequence  The sequence number of the last request processed by the X11 server.
                //
                //        time      NOT YET DOCUMENTED.
                //
                //        requestor NOT YET DOCUMENTED.
                //
                //        selection NOT YET DOCUMENTED.
                //
                //        target    NOT YET DOCUMENTED.
                //
                //        property  NOT YET DOCUMENTED.
                //
                // DESCRIPTION
                // SEE ALSO
                // AUTHOR
                //        Generated from xproto.xml. Contact xcb@lists.freedesktop.org for corrections and improvements.
                //
                // X Version 11                                                                             libxcb 1.17.0                                                          xcb_selection_notify_event_t(3)

            }
            break; case XCB_COLORMAP_NOTIFY: // 32
            {
                string8_print(S8("COLORMAP_NOTIFY"));

                let colormap_notify_event = (xcb_colormap_notify_event_t*)event;
                let k = colormap_notify_event->response_type;
                BUSTER_UNUSED(k);

                //                 xcb_colormap_notify_event_t(3)                                                             XCB Events                                                            xcb_colormap_notify_event_t(3)
                //
                // NAME
                //        xcb_colormap_notify_event_t - the colormap for some window changed
                //
                // SYNOPSIS
                //        #include <xcb/xproto.h>
                //
                //    Event datastructure
                //        typedef struct xcb_colormap_notify_event_t {
                //            uint8_t        response_type;
                //            uint8_t        pad0;
                //            uint16_t       sequence;
                //            xcb_window_t   window;
                //            xcb_colormap_t colormap;
                //            uint8_t        _new;
                //            uint8_t        state;
                //            uint8_t        pad1[2];
                //        } xcb_colormap_notify_event_t;
                //
                // EVENT FIELDS
                //        response_type
                //                  The type of this event, in this case XCB_COLORMAP_NOTIFY. This field is also present in the xcb_generic_event_t and can be used to tell events apart from each other.
                //
                //        sequence  The sequence number of the last request processed by the X11 server.
                //
                //        window    The window whose associated colormap is changed, installed or uninstalled.
                //
                //        colormap  The colormap which is changed, installed or uninstalled. This is XCB_NONE when the colormap is changed by a call to FreeColormap.
                //
                //        _new      NOT YET DOCUMENTED.
                //
                //        state
                //
                // DESCRIPTION
                // SEE ALSO
                //        xcb_generic_event_t(3), xcb_free_colormap(3)
                //
                // AUTHOR
                //        Generated from xproto.xml. Contact xcb@lists.freedesktop.org for corrections and improvements.
                //
                // X Version 11                                                                             libxcb 1.17.0                                                           xcb_colormap_notify_event_t(3)

            }
            break; case XCB_CLIENT_MESSAGE: // 33
            {
                string8_print(S8("CLIENT_MESSAGE"));
                let client_message_event = (xcb_client_message_event_t*)event;
                if (client_message_event->data.data32[0] == atom_replies[X11_ATOM_WM_DELETE_WINDOW]->atom)
                {
                    string8_print(S8("WM_DELETE_WINDOW"));
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

                    unimplemented = false;
                }
                else
                {
                    unimplemented = true;
                }
            }
            break; case XCB_MAPPING_NOTIFY: // 32
            {
                string8_print(S8("MAPPING_NOTIFY"));

                let mapping_notify_event = (xcb_mapping_notify_event_t*)event;
                let k = mapping_notify_event->response_type;
                BUSTER_UNUSED(k);

                //                 xcb_mapping_notify_event_t(3)                                                              XCB Events                                                             xcb_mapping_notify_event_t(3)
                //
                // NAME
                //        xcb_mapping_notify_event_t - keyboard mapping changed
                //
                // SYNOPSIS
                //        #include <xcb/xproto.h>
                //
                //    Event datastructure
                //        typedef struct xcb_mapping_notify_event_t {
                //            uint8_t       response_type;
                //            uint8_t       pad0;
                //            uint16_t      sequence;
                //            uint8_t       request;
                //            xcb_keycode_t first_keycode;
                //            uint8_t       count;
                //            uint8_t       pad1;
                //        } xcb_mapping_notify_event_t;
                //
                // EVENT FIELDS
                //        response_type
                //                  The type of this event, in this case XCB_MAPPING_NOTIFY. This field is also present in the xcb_generic_event_t and can be used to tell events apart from each other.
                //
                //        sequence  The sequence number of the last request processed by the X11 server.
                //
                //        request
                //
                //        first_keycode
                //                  The first number in the range of the altered mapping.
                //
                //        count     The number of keycodes altered.
                //
                // DESCRIPTION
                // SEE ALSO
                //        xcb_generic_event_t(3)
                //
                // AUTHOR
                //        Generated from xproto.xml. Contact xcb@lists.freedesktop.org for corrections and improvements.
                //
                // X Version 11                                                                             libxcb 1.17.0                                                            xcb_mapping_notify_event_t(3)

            }
            break; default:
            {
                string8_print(S8("UNKNOWN"));
            }
        }

        string8_print(S8(")\n"));
        free(event);

        if (unimplemented)
        {
            string8_print(S8("Unimplemented X event\n"));
            os_fail();
        }
    }
#endif

    return event_list;
}
