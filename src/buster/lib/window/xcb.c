#include <buster/lib/window/internal.h>

#define BUSTER_X11_XDND_MAX_TRANSFER_BYTES BUSTER_NATIVE_FILE_DROP_MAX_PATH_BYTES

u64 wm_x11_utf8_result_length(int result, u64 capacity)
{
    return result > 0 && (u64)result < capacity ? (u64)result : 0;
}

bool wm_x11_xdnd_version_supported(u8 version)
{
    return version >= 3 && version <= 5;
}

bool wm_x11_xdnd_transaction_matches(u32 active_source, u32 active_target, u32 source, u32 target)
{
    return active_source != 0 && active_target != 0 && active_source == source && active_target == target;
}

u32 wm_x11_xdnd_source_watch_event_mask(u32 previous_mask)
{
    return previous_mask | XCB_EVENT_MASK_STRUCTURE_NOTIFY;
}

u32 wm_x11_xdnd_source_restore_event_mask(u32 saved_mask)
{
    return saved_mask;
}

bool wm_x11_xdnd_source_destroyed(bool source_destroy_observed, u32 source, u32 destroyed_window)
{
    return source_destroy_observed && source != XCB_WINDOW_NONE && source == destroyed_window;
}

u64 wm_x11_xdnd_max_transfer_bytes(void)
{
    return BUSTER_X11_XDND_MAX_TRANSFER_BYTES;
}

bool wm_x11_xdnd_transfer_length_allowed(u64 current_length, u64 incoming_length)
{
    return current_length <= BUSTER_X11_XDND_MAX_TRANSFER_BYTES && incoming_length <= BUSTER_X11_XDND_MAX_TRANSFER_BYTES - current_length;
}

BUSTER_GLOBAL_LOCAL s16 wm_x11_s16_from_s32(s32 value)
{
    s16 result = 0;
    if (value <= -32768)
    {
        result = -32768;
    }
    else if (value >= 32767)
    {
        result = 32767;
    }
    else
    {
        result = (s16)value;
    }
    return result;
}

WmOffset wm_x11_drop_position_from_root(s32 root_x, s32 root_y, s32 window_root_x, s32 window_root_y)
{
    return (WmOffset){
        .x = wm_x11_s16_from_s32(root_x - window_root_x),
        .y = wm_x11_s16_from_s32(root_y - window_root_y),
    };
}

BUSTER_GLOBAL_LOCAL xcb_screen_t* wm_x11_screen_from_handle(WmHandle* windowing);

BUSTER_GLOBAL_LOCAL WmWindowHandle* wm_x11_window_from_xcb(WmHandle* handle, xcb_window_t window)
{
    WmWindowHandle* result = 0;

    SliceWmWindowHandle windows = get_windows(handle);
    for (u64 i = 0; i < windows.length; i += 1)
    {
        WmWindowHandle* candidate = &windows.pointer[i];
        if (candidate->handle == window)
        {
            result = candidate;
            break;
        }
    }

    return result;
}

BUSTER_GLOBAL_LOCAL WmWindowHandle* wm_x11_window_from_ic(WmHandle* handle, xcb_xic_t ic)
{
    WmWindowHandle* result = 0;

    if (ic)
    {
        SliceWmWindowHandle windows = get_windows(handle);

        for (u64 i = 0; i < windows.length; i += 1)
        {
            WmWindowHandle* window = &windows.pointer[i];
            if (window->ic == ic)
            {
                result = window;
                break;
            }
        }
    }

    return result;
}

BUSTER_GLOBAL_LOCAL u32 wm_x11_window_event_mask(WmHandle* handle)
{
    u32 result = XCB_EVENT_MASK_KEY_PRESS | XCB_EVENT_MASK_KEY_RELEASE | XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_BUTTON_RELEASE |
                 XCB_EVENT_MASK_POINTER_MOTION | XCB_EVENT_MASK_BUTTON_MOTION | XCB_EVENT_MASK_EXPOSURE | XCB_EVENT_MASK_STRUCTURE_NOTIFY |
                 XCB_EVENT_MASK_FOCUS_CHANGE | XCB_EVENT_MASK_PROPERTY_CHANGE | handle->xim_forward_event_mask | handle->xim_synchronous_event_mask;
    return result;
}

BUSTER_GLOBAL_LOCAL void wm_x11_window_update_event_mask(WmWindowHandle* window)
{
    if (window && window->owner && window->owner->connection && window->handle)
    {
        u32 event_mask = wm_x11_window_event_mask(window->owner);
        xcb_change_window_attributes(window->owner->connection, window->handle, XCB_CW_EVENT_MASK, &event_mask);
    }
}

BUSTER_GLOBAL_LOCAL u8 wm_x11_modifiers_from_state(uint16_t state)
{
    bool mod_control = (state & XCB_KEY_BUT_MASK_CONTROL) != 0;
    bool mod_shift = (state & XCB_KEY_BUT_MASK_SHIFT) != 0;
    bool mod_alt = (state & XCB_KEY_BUT_MASK_MOD_1) != 0;

    return (u8)(((u8)mod_control << WM_MODIFIER_CONTROL) | ((u8)mod_shift << WM_MODIFIER_SHIFT) | ((u8)mod_alt << WM_MODIFIER_ALT));
}

BUSTER_GLOBAL_LOCAL WmKey wm_x11_key_from_xkb_name(const char* name)
{
    WmKey result = WM_KEY_NULL;

    if (name)
    {
        if (strcmp(name, "ESC") == 0)
            result = WM_KEY_ESC;
        else if (strcmp(name, "FK01") == 0)
            result = WM_KEY_F1;
        else if (strcmp(name, "FK02") == 0)
            result = WM_KEY_F2;
        else if (strcmp(name, "FK03") == 0)
            result = WM_KEY_F3;
        else if (strcmp(name, "FK04") == 0)
            result = WM_KEY_F4;
        else if (strcmp(name, "FK05") == 0)
            result = WM_KEY_F5;
        else if (strcmp(name, "FK06") == 0)
            result = WM_KEY_F6;
        else if (strcmp(name, "FK07") == 0)
            result = WM_KEY_F7;
        else if (strcmp(name, "FK08") == 0)
            result = WM_KEY_F8;
        else if (strcmp(name, "FK09") == 0)
            result = WM_KEY_F9;
        else if (strcmp(name, "FK10") == 0)
            result = WM_KEY_F10;
        else if (strcmp(name, "FK11") == 0)
            result = WM_KEY_F11;
        else if (strcmp(name, "FK12") == 0)
            result = WM_KEY_F12;
        else if (strcmp(name, "FK13") == 0)
            result = WM_KEY_F13;
        else if (strcmp(name, "FK14") == 0)
            result = WM_KEY_F14;
        else if (strcmp(name, "FK15") == 0)
            result = WM_KEY_F15;
        else if (strcmp(name, "FK16") == 0)
            result = WM_KEY_F16;
        else if (strcmp(name, "FK17") == 0)
            result = WM_KEY_F17;
        else if (strcmp(name, "FK18") == 0)
            result = WM_KEY_F18;
        else if (strcmp(name, "FK19") == 0)
            result = WM_KEY_F19;
        else if (strcmp(name, "FK20") == 0)
            result = WM_KEY_F20;
        else if (strcmp(name, "FK21") == 0)
            result = WM_KEY_F21;
        else if (strcmp(name, "FK22") == 0)
            result = WM_KEY_F22;
        else if (strcmp(name, "FK23") == 0)
            result = WM_KEY_F23;
        else if (strcmp(name, "FK24") == 0)
            result = WM_KEY_F24;
        else if (strcmp(name, "FK25") == 0)
            result = WM_KEY_F25;
        else if (strcmp(name, "FK26") == 0)
            result = WM_KEY_F26;
        else if (strcmp(name, "FK27") == 0)
            result = WM_KEY_F27;
        else if (strcmp(name, "FK28") == 0)
            result = WM_KEY_F28;
        else if (strcmp(name, "FK29") == 0)
            result = WM_KEY_F29;
        else if (strcmp(name, "FK30") == 0)
            result = WM_KEY_F30;
        else if (strcmp(name, "FK31") == 0)
            result = WM_KEY_F31;
        else if (strcmp(name, "FK32") == 0)
            result = WM_KEY_F32;
        else if (strcmp(name, "FK33") == 0)
            result = WM_KEY_F33;
        else if (strcmp(name, "FK34") == 0)
            result = WM_KEY_F34;
        else if (strcmp(name, "FK35") == 0)
            result = WM_KEY_F35;
        else if (strcmp(name, "TLDE") == 0)
            result = WM_KEY_BACKTICK;
        else if (strcmp(name, "AE01") == 0)
            result = WM_KEY_1;
        else if (strcmp(name, "AE02") == 0)
            result = WM_KEY_2;
        else if (strcmp(name, "AE03") == 0)
            result = WM_KEY_3;
        else if (strcmp(name, "AE04") == 0)
            result = WM_KEY_4;
        else if (strcmp(name, "AE05") == 0)
            result = WM_KEY_5;
        else if (strcmp(name, "AE06") == 0)
            result = WM_KEY_6;
        else if (strcmp(name, "AE07") == 0)
            result = WM_KEY_7;
        else if (strcmp(name, "AE08") == 0)
            result = WM_KEY_8;
        else if (strcmp(name, "AE09") == 0)
            result = WM_KEY_9;
        else if (strcmp(name, "AE10") == 0)
            result = WM_KEY_0;
        else if (strcmp(name, "AE11") == 0)
            result = WM_KEY_MINUS;
        else if (strcmp(name, "AE12") == 0)
            result = WM_KEY_EQUAL;
        else if (strcmp(name, "BKSP") == 0)
            result = WM_KEY_BACKSPACE;
        else if (strcmp(name, "TAB") == 0)
            result = WM_KEY_TAB;
        else if (strcmp(name, "AD01") == 0)
            result = WM_KEY_Q;
        else if (strcmp(name, "AD02") == 0)
            result = WM_KEY_W;
        else if (strcmp(name, "AD03") == 0)
            result = WM_KEY_E;
        else if (strcmp(name, "AD04") == 0)
            result = WM_KEY_R;
        else if (strcmp(name, "AD05") == 0)
            result = WM_KEY_T;
        else if (strcmp(name, "AD06") == 0)
            result = WM_KEY_Y;
        else if (strcmp(name, "AD07") == 0)
            result = WM_KEY_U;
        else if (strcmp(name, "AD08") == 0)
            result = WM_KEY_I;
        else if (strcmp(name, "AD09") == 0)
            result = WM_KEY_O;
        else if (strcmp(name, "AD10") == 0)
            result = WM_KEY_P;
        else if (strcmp(name, "AD11") == 0)
            result = WM_KEY_LEFT_BRACKET;
        else if (strcmp(name, "AD12") == 0)
            result = WM_KEY_RIGHT_BRACKET;
        else if (strcmp(name, "BKSL") == 0)
            result = WM_KEY_BACKWARD_SLASH;
        else if (strcmp(name, "RTRN") == 0)
            result = WM_KEY_RETURN;
        else if (strcmp(name, "CAPS") == 0)
            result = WM_KEY_CAPS_LOCK;
        else if (strcmp(name, "AC01") == 0)
            result = WM_KEY_A;
        else if (strcmp(name, "AC02") == 0)
            result = WM_KEY_S;
        else if (strcmp(name, "AC03") == 0)
            result = WM_KEY_D;
        else if (strcmp(name, "AC04") == 0)
            result = WM_KEY_F;
        else if (strcmp(name, "AC05") == 0)
            result = WM_KEY_G;
        else if (strcmp(name, "AC06") == 0)
            result = WM_KEY_H;
        else if (strcmp(name, "AC07") == 0)
            result = WM_KEY_J;
        else if (strcmp(name, "AC08") == 0)
            result = WM_KEY_K;
        else if (strcmp(name, "AC09") == 0)
            result = WM_KEY_L;
        else if (strcmp(name, "AC10") == 0)
            result = WM_KEY_SEMICOLON;
        else if (strcmp(name, "AC11") == 0)
            result = WM_KEY_SINGLE_QUOTE;
        else if (strcmp(name, "LFSH") == 0)
            result = WM_KEY_SHIFT;
        else if (strcmp(name, "RTSH") == 0)
            result = WM_KEY_SHIFT;
        else if (strcmp(name, "AB01") == 0)
            result = WM_KEY_Z;
        else if (strcmp(name, "AB02") == 0)
            result = WM_KEY_X;
        else if (strcmp(name, "AB03") == 0)
            result = WM_KEY_C;
        else if (strcmp(name, "AB04") == 0)
            result = WM_KEY_V;
        else if (strcmp(name, "AB05") == 0)
            result = WM_KEY_B;
        else if (strcmp(name, "AB06") == 0)
            result = WM_KEY_N;
        else if (strcmp(name, "AB07") == 0)
            result = WM_KEY_M;
        else if (strcmp(name, "AB08") == 0)
            result = WM_KEY_COMMA;
        else if (strcmp(name, "AB09") == 0)
            result = WM_KEY_DOT;
        else if (strcmp(name, "AB10") == 0)
            result = WM_KEY_FORWARD_SLASH;
        else if (strcmp(name, "LCTL") == 0)
            result = WM_KEY_CONTROL;
        else if (strcmp(name, "RCTL") == 0)
            result = WM_KEY_CONTROL;
        else if (strcmp(name, "LALT") == 0)
            result = WM_KEY_ALT;
        else if (strcmp(name, "RALT") == 0)
            result = WM_KEY_ALT;
        else if (strcmp(name, "SPCE") == 0)
            result = WM_KEY_SPACE;
        else if (strcmp(name, "COMP") == 0)
            result = WM_KEY_MENU;
        else if (strcmp(name, "MENU") == 0)
            result = WM_KEY_MENU;
        else if (strcmp(name, "INS") == 0)
            result = WM_KEY_INSERT;
        else if (strcmp(name, "HOME") == 0)
            result = WM_KEY_HOME;
        else if (strcmp(name, "PGUP") == 0)
            result = WM_KEY_PAGE_UP;
        else if (strcmp(name, "DELE") == 0)
            result = WM_KEY_DELETE;
        else if (strcmp(name, "END") == 0)
            result = WM_KEY_END;
        else if (strcmp(name, "PGDN") == 0)
            result = WM_KEY_PAGE_DOWN;
        else if (strcmp(name, "UP") == 0)
            result = WM_KEY_UP;
        else if (strcmp(name, "DOWN") == 0)
            result = WM_KEY_DOWN;
        else if (strcmp(name, "LEFT") == 0)
            result = WM_KEY_LEFT;
        else if (strcmp(name, "RGHT") == 0)
            result = WM_KEY_RIGHT;
        else if (strcmp(name, "NMLK") == 0)
            result = WM_KEY_NUM_LOCK;
        else if (strcmp(name, "KPDV") == 0)
            result = WM_KEY_NUM_SLASH;
        else if (strcmp(name, "KPMU") == 0)
            result = WM_KEY_NUM_STAR;
        else if (strcmp(name, "KPSU") == 0)
            result = WM_KEY_NUM_MINUS;
        else if (strcmp(name, "KPAD") == 0)
            result = WM_KEY_NUM_PLUS;
        else if (strcmp(name, "KPEN") == 0)
            result = WM_KEY_RETURN;
        else if (strcmp(name, "KPDL") == 0)
            result = WM_KEY_NUM_DOT;
        else if (strcmp(name, "KP0") == 0)
            result = WM_KEY_NUM_0;
        else if (strcmp(name, "KP1") == 0)
            result = WM_KEY_NUM_1;
        else if (strcmp(name, "KP2") == 0)
            result = WM_KEY_NUM_2;
        else if (strcmp(name, "KP3") == 0)
            result = WM_KEY_NUM_3;
        else if (strcmp(name, "KP4") == 0)
            result = WM_KEY_NUM_4;
        else if (strcmp(name, "KP5") == 0)
            result = WM_KEY_NUM_5;
        else if (strcmp(name, "KP6") == 0)
            result = WM_KEY_NUM_6;
        else if (strcmp(name, "KP7") == 0)
            result = WM_KEY_NUM_7;
        else if (strcmp(name, "KP8") == 0)
            result = WM_KEY_NUM_8;
        else if (strcmp(name, "KP9") == 0)
            result = WM_KEY_NUM_9;
    }

    return result;
}

BUSTER_GLOBAL_LOCAL void wm_x11_window_deinitialize_compose(WmWindowHandle* window)
{
    if (window->xkb_compose_state)
    {
        xkb_compose_state_unref(window->xkb_compose_state);
        window->xkb_compose_state = 0;
    }
}

BUSTER_GLOBAL_LOCAL void wm_x11_window_initialize_compose(WmWindowHandle* window)
{
    if (window->owner && window->owner->xkb_compose_table && !window->xkb_compose_state)
    {
        window->xkb_compose_state = xkb_compose_state_new(window->owner->xkb_compose_table, XKB_COMPOSE_STATE_NO_FLAGS);
    }
}

BUSTER_GLOBAL_LOCAL void wm_x11_window_reset_compose_all(WmHandle* handle)
{
    SliceWmWindowHandle windows = get_windows(handle);

    for (u64 i = 0; i < windows.length; i += 1)
    {
        WmWindowHandle* window = &windows.pointer[i];
        if (window->xkb_compose_state)
        {
            xkb_compose_state_reset(window->xkb_compose_state);
        }
    }
}

BUSTER_GLOBAL_LOCAL void wm_x11_window_deinitialize_compose_all(WmHandle* handle)
{
    SliceWmWindowHandle windows = get_windows(handle);

    for (u64 i = 0; i < windows.length; i += 1)
    {
        wm_x11_window_deinitialize_compose(&windows.pointer[i]);
    }
}

BUSTER_GLOBAL_LOCAL void wm_x11_window_initialize_compose_all(WmHandle* handle)
{
    SliceWmWindowHandle windows = get_windows(handle);

    for (u64 i = 0; i < windows.length; i += 1)
    {
        WmWindowHandle* window = &windows.pointer[i];
        wm_x11_window_initialize_compose(window);
    }
}

BUSTER_GLOBAL_LOCAL void wm_x11_xkb_deinitialize(WmHandle* handle)
{
    wm_x11_window_deinitialize_compose_all(handle);

    if (handle->xkb_compose_table)
    {
        xkb_compose_table_unref(handle->xkb_compose_table);
        handle->xkb_compose_table = 0;
    }

    if (handle->xkb_state)
    {
        xkb_state_unref(handle->xkb_state);
        handle->xkb_state = 0;
    }

    if (handle->xkb_keymap)
    {
        xkb_keymap_unref(handle->xkb_keymap);
        handle->xkb_keymap = 0;
    }

    if (handle->xkb_context)
    {
        xkb_context_unref(handle->xkb_context);
        handle->xkb_context = 0;
    }

    handle->xkb_device_id = 0;
    handle->xkb_base_event = 0;
}

BUSTER_GLOBAL_LOCAL void wm_x11_xkb_select_events(WmHandle* handle)
{
    if (handle->xkb_device_id >= 0)
    {
        u16 event_mask = XCB_XKB_EVENT_TYPE_NEW_KEYBOARD_NOTIFY | XCB_XKB_EVENT_TYPE_MAP_NOTIFY | XCB_XKB_EVENT_TYPE_STATE_NOTIFY;
        u16 keyboard_details = XCB_XKB_NKN_DETAIL_KEYCODES | XCB_XKB_NKN_DETAIL_DEVICE_ID;
        u16 state_details = XCB_XKB_STATE_PART_MODIFIER_STATE | XCB_XKB_STATE_PART_MODIFIER_BASE | XCB_XKB_STATE_PART_MODIFIER_LATCH |
                            XCB_XKB_STATE_PART_MODIFIER_LOCK | XCB_XKB_STATE_PART_GROUP_STATE | XCB_XKB_STATE_PART_GROUP_BASE | XCB_XKB_STATE_PART_GROUP_LATCH |
                            XCB_XKB_STATE_PART_GROUP_LOCK;
        u16 map_parts = XCB_XKB_MAP_PART_KEY_TYPES | XCB_XKB_MAP_PART_KEY_SYMS | XCB_XKB_MAP_PART_MODIFIER_MAP | XCB_XKB_MAP_PART_EXPLICIT_COMPONENTS |
                        XCB_XKB_MAP_PART_KEY_ACTIONS | XCB_XKB_MAP_PART_KEY_BEHAVIORS | XCB_XKB_MAP_PART_VIRTUAL_MODS | XCB_XKB_MAP_PART_VIRTUAL_MOD_MAP;
        xcb_xkb_select_events_details_t details = {
            .affectNewKeyboard = keyboard_details,
            .newKeyboardDetails = keyboard_details,
            .affectState = state_details,
            .stateDetails = state_details,
        };

        xcb_xkb_select_events_aux(handle->connection, (xcb_xkb_device_spec_t)handle->xkb_device_id, event_mask, 0, event_mask, map_parts, map_parts, &details);
    }
}

BUSTER_GLOBAL_LOCAL void wm_x11_xkb_refresh_state(WmHandle* handle)
{
    if (handle->xkb_keymap && handle->xkb_device_id >= 0)
    {
        struct xkb_state* state = xkb_x11_state_new_from_device(handle->xkb_keymap, handle->connection, handle->xkb_device_id);
        if (state)
        {
            if (handle->xkb_state)
            {
                xkb_state_unref(handle->xkb_state);
            }
            handle->xkb_state = state;
        }
    }
}

BUSTER_GLOBAL_LOCAL void wm_x11_xkb_refresh_keymap(WmHandle* handle)
{
    if (handle->xkb_context && handle->xkb_device_id >= 0)
    {
        struct xkb_keymap* keymap = xkb_x11_keymap_new_from_device(handle->xkb_context, handle->connection, handle->xkb_device_id, XKB_KEYMAP_COMPILE_NO_FLAGS);
        if (keymap)
        {
            struct xkb_state* state = xkb_x11_state_new_from_device(keymap, handle->connection, handle->xkb_device_id);
            if (state)
            {
                if (handle->xkb_state)
                {
                    xkb_state_unref(handle->xkb_state);
                }
                if (handle->xkb_keymap)
                {
                    xkb_keymap_unref(handle->xkb_keymap);
                }
                handle->xkb_keymap = keymap;
                handle->xkb_state = state;
                wm_x11_window_reset_compose_all(handle);
            }
            else
            {
                xkb_keymap_unref(keymap);
            }
        }
    }
}

BUSTER_GLOBAL_LOCAL void wm_x11_xkb_initialize_compose(WmHandle* handle)
{
    if (handle->xkb_context)
    {
        char* locale = setlocale(LC_CTYPE, 0);
        if (!locale || strcmp(locale, "C") == 0 || strcmp(locale, "POSIX") == 0)
        {
            locale = setlocale(LC_CTYPE, "");
        }
        if (!locale)
        {
            locale = "C";
        }

        struct xkb_compose_table* compose_table = xkb_compose_table_new_from_locale(handle->xkb_context, locale, XKB_COMPOSE_COMPILE_NO_FLAGS);
        if (compose_table)
        {
            handle->xkb_compose_table = compose_table;
            wm_x11_window_initialize_compose_all(handle);
        }
    }
}

BUSTER_GLOBAL_LOCAL void wm_x11_xkb_initialize(WmHandle* handle)
{
    u8 base_event = 0;
    if (xkb_x11_setup_xkb_extension(handle->connection, XKB_X11_MIN_MAJOR_XKB_VERSION, XKB_X11_MIN_MINOR_XKB_VERSION, XKB_X11_SETUP_XKB_EXTENSION_NO_FLAGS, 0,
                                    0, &base_event, 0))
    {
        int32_t device_id = xkb_x11_get_core_keyboard_device_id(handle->connection);
        if (device_id >= 0)
        {
            struct xkb_context* context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
            if (context)
            {
                struct xkb_keymap* keymap = xkb_x11_keymap_new_from_device(context, handle->connection, device_id, XKB_KEYMAP_COMPILE_NO_FLAGS);
                if (keymap)
                {
                    struct xkb_state* state = xkb_x11_state_new_from_device(keymap, handle->connection, device_id);
                    if (state)
                    {
                        handle->xkb_context = context;
                        handle->xkb_keymap = keymap;
                        handle->xkb_state = state;
                        handle->xkb_device_id = device_id;
                        handle->xkb_base_event = base_event;
                        wm_x11_xkb_initialize_compose(handle);
                        wm_x11_xkb_select_events(handle);
                    }
                    else
                    {
                        xkb_keymap_unref(keymap);
                    }
                }

                if (!handle->xkb_context)
                {
                    xkb_context_unref(context);
                }
            }
        }
    }
}

BUSTER_GLOBAL_LOCAL bool wm_x11_handle_xkb_event(WmHandle* handle, xcb_generic_event_t* event)
{
    xcb_xkb_state_notify_event_t* xkb_event = (xcb_xkb_state_notify_event_t*)event;
    bool result = true;

    switch (xkb_event->xkbType)
    {
        break;
    case XCB_XKB_NEW_KEYBOARD_NOTIFY:
    {
        wm_x11_xkb_deinitialize(handle);
        wm_x11_xkb_initialize(handle);
    }
    break;
    case XCB_XKB_MAP_NOTIFY:
    {
        wm_x11_xkb_refresh_keymap(handle);
    }
    break;
    case XCB_XKB_STATE_NOTIFY:
    {
        wm_x11_xkb_refresh_state(handle);
    }
    break;
    default:
    {
        result = false;
    }
    }

    return result;
}

BUSTER_GLOBAL_LOCAL u16 wm_xim_read_u16(uint8_t* data)
{
    u16 result = 0;
    memcpy(&result, data, sizeof(result));
    return result;
}

BUSTER_GLOBAL_LOCAL u32 wm_xim_read_u32(uint8_t* data)
{
    u32 result = 0;
    memcpy(&result, data, sizeof(result));
    return result;
}

BUSTER_GLOBAL_LOCAL bool wm_xim_style_is_supported(WmHandle* handle, u32 style)
{
    bool result = handle->xim_supported_input_style_count == 0;

    for (u32 i = 0; !result && i < handle->xim_supported_input_style_count; i += 1)
    {
        result = handle->xim_supported_input_styles[i] == style;
    }

    return result;
}

BUSTER_GLOBAL_LOCAL u32 const wm_xim_input_style_candidates[] = {
    XCB_IM_PreeditNothing | XCB_IM_StatusNothing,     XCB_IM_PreeditNothing | XCB_IM_StatusNone,      XCB_IM_PreeditNone | XCB_IM_StatusNothing,
    XCB_IM_PreeditNone | XCB_IM_StatusNone,           XCB_IM_PreeditPosition | XCB_IM_StatusNothing,  XCB_IM_PreeditPosition | XCB_IM_StatusNone,
    XCB_IM_PreeditCallbacks | XCB_IM_StatusCallbacks, XCB_IM_PreeditCallbacks | XCB_IM_StatusNothing, XCB_IM_PreeditCallbacks | XCB_IM_StatusNone,
};

u32 wm_xim_next_input_style_attempt(u32 current)
{
    return current < BUSTER_ARRAY_LENGTH(wm_xim_input_style_candidates) ? current + 1 : current;
}

BUSTER_GLOBAL_LOCAL bool wm_xim_select_input_style(WmHandle* handle, WmWindowHandle* window, u32* input_style_out)
{
    bool result = false;
    for (u32 i = window->xim_input_style_attempt_index; !result && i < BUSTER_ARRAY_LENGTH(wm_xim_input_style_candidates); i += 1)
    {
        u32 candidate = wm_xim_input_style_candidates[i];
        if (wm_xim_style_is_supported(handle, candidate))
        {
            *input_style_out = candidate;
            window->xim_input_style_attempt_index = i;
            result = true;
        }
    }

    if (!result && handle->xim_supported_input_style_count == 0 &&
        window->xim_input_style_attempt_index < BUSTER_ARRAY_LENGTH(wm_xim_input_style_candidates))
    {
        *input_style_out = wm_xim_input_style_candidates[window->xim_input_style_attempt_index];
        result = true;
    }

    return result;
}

BUSTER_GLOBAL_LOCAL void wm_xim_parse_input_styles_reply(WmHandle* handle, xcb_im_get_im_values_reply_fr_t* reply)
{
    handle->xim_supported_input_style_count = 0;

    if (reply)
    {
        for (u32 attribute_index = 0; attribute_index < reply->im_attribute_returned.size; attribute_index += 1)
        {
            xcb_im_ximattribute_fr_t* attribute = reply->im_attribute_returned.items + attribute_index;
            if (attribute->value && attribute->value_length >= 4)
            {
                uint8_t* value = attribute->value;
                u16 style_count = wm_xim_read_u16(value);
                u32 value_offset = 4;
                for (u32 style_index = 0; style_index < style_count && value_offset + 4 <= attribute->value_length &&
                                          handle->xim_supported_input_style_count < BUSTER_ARRAY_LENGTH(handle->xim_supported_input_styles);
                     style_index += 1, value_offset += 4)
                {
                    handle->xim_supported_input_styles[handle->xim_supported_input_style_count] = wm_xim_read_u32(value + value_offset);
                    handle->xim_supported_input_style_count += 1;
                }
            }
        }
    }
}

BUSTER_GLOBAL_LOCAL void wm_xim_create_ic(WmHandle* handle);

BUSTER_GLOBAL_LOCAL void wm_xim_set_event_mask_callback(xcb_xim_t* im, xcb_xic_t ic, uint32_t forward_event_mask, uint32_t synchronous_event_mask,
                                                        void* user_data)
{
    BUSTER_UNUSED(im);

    WmHandle* handle = (WmHandle*)user_data;
    handle->xim_forward_event_mask = forward_event_mask;
    handle->xim_synchronous_event_mask = synchronous_event_mask;

    WmWindowHandle* window = wm_x11_window_from_ic(handle, ic);
    if (window)
    {
        wm_x11_window_update_event_mask(window);
    }
    else
    {
        SliceWmWindowHandle windows = get_windows(handle);
        for (u64 i = 0; i < windows.length; i += 1)
        {
            WmWindowHandle* w = &windows.pointer[i];
            wm_x11_window_update_event_mask(w);
        }
    }

    if (handle->connection)
    {
        xcb_flush(handle->connection);
    }
}

BUSTER_GLOBAL_LOCAL void wm_xim_commit_string_callback(xcb_xim_t* im, xcb_xic_t ic, uint32_t flag, char* str, uint32_t length, uint32_t* keysym,
                                                       size_t keysym_count, void* user_data)
{
    BUSTER_UNUSED(flag);
    BUSTER_UNUSED(keysym);
    BUSTER_UNUSED(keysym_count);

    WmHandle* handle = (WmHandle*)user_data;
    if (str && length && handle->poll_arena && handle->poll_event_list)
    {
        char* converted = 0;
        String8 text = {0};
        if (xcb_xim_get_encoding(im) == XCB_XIM_UTF8_STRING)
        {
            text = string_from_pointer_length((char8*)str, length);
        }
        else
        {
            size_t converted_length = 0;
            converted = xcb_compound_text_to_utf8(str, length, &converted_length);
            if (converted)
            {
                text = string_from_pointer_length((char8*)converted, converted_length);
            }
        }

        if (text.pointer && text.length)
        {
            WmWindowHandle* window = wm_x11_window_from_ic(handle, ic);
            if (!window)
            {
                window = handle->focused_window;
            }

            text = string_duplicate_arena(handle->poll_arena, text, false);
            wm_event_push(handle, (WmEvent){
                                      .kind = WM_EVENT_TEXT_INPUT,
                                      .window = window,
                                      .text = text,
                                  });
        }

        free(converted);
    }
}

BUSTER_GLOBAL_LOCAL void wm_xim_forward_event_callback(xcb_xim_t* im, xcb_xic_t ic, xcb_key_press_event_t* event, void* user_data)
{
    BUSTER_UNUSED(im);
    BUSTER_UNUSED(ic);

    BUSTER_UNUSED(event);
    BUSTER_UNUSED(user_data);
}

BUSTER_GLOBAL_LOCAL void wm_xim_create_ic_callback(xcb_xim_t* im, xcb_xic_t new_ic, void* user_data)
{
    WmWindowHandle* window = (WmWindowHandle*)user_data;
    window->xim_create_ic_pending = false;
    window->ic = new_ic;

    if (new_ic)
    {
        if (window->focused)
        {
            xcb_xim_set_ic_focus(im, new_ic);
        }
    }
    else
    {
        window->xim_input_style_attempt_index = wm_xim_next_input_style_attempt(window->xim_input_style_attempt_index);
    }
}

BUSTER_GLOBAL_LOCAL void wm_xim_get_im_values_callback(xcb_xim_t* im, xcb_im_get_im_values_reply_fr_t* reply, void* user_data)
{
    BUSTER_UNUSED(im);

    WmHandle* handle = (WmHandle*)user_data;
    handle->xim_input_styles_pending = false;
    handle->xim_input_styles_ready = true;
    SliceWmWindowHandle windows = get_windows(handle);
    for (u64 i = 0; i < windows.length; i += 1)
    {
        windows.pointer[i].xim_input_style_attempt_index = 0;
    }
    wm_xim_parse_input_styles_reply(handle, reply);
    wm_xim_create_ic(handle);
}

BUSTER_GLOBAL_LOCAL void xcb_xim_open_callback_implementation(xcb_xim_t* im, void* user_data)
{
    BUSTER_UNUSED(im);

    WmHandle* handle = (WmHandle*)user_data;
    handle->xim_open = true;
    wm_xim_create_ic(handle);
}

BUSTER_GLOBAL_LOCAL void wm_xim_create_ic_for_window(WmWindowHandle* window)
{
    WmHandle* handle = window->owner;
    if (handle->xim && handle->xim_open && window->handle && !window->ic && !window->xim_create_ic_pending)
    {
        if (!handle->xim_input_styles_ready)
        {
            if (!handle->xim_input_styles_pending)
            {
                handle->xim_input_styles_pending = true;
                if (!xcb_xim_get_im_values(handle->xim, wm_xim_get_im_values_callback, handle, XCB_XIM_XNQueryInputStyle, NULL))
                {
                    handle->xim_input_styles_pending = false;
                    handle->xim_input_styles_ready = true;
                    handle->xim_supported_input_style_count = 0;
                }
            }
        }

        while (handle->xim_input_styles_ready)
        {
            u32 input_style = 0;
            if (!wm_xim_select_input_style(handle, window, &input_style))
            {
                break;
            }
            bool requested = false;

            if ((input_style & XCB_IM_PreeditPosition) != 0)
            {
                xcb_point_t spot = {0};
                xcb_xim_nested_list preedit = xcb_xim_create_nested_list(handle->xim, XCB_XIM_XNSpotLocation, &spot, NULL);
                if (preedit.data && preedit.length)
                {
                    requested = xcb_xim_create_ic(handle->xim, wm_xim_create_ic_callback, window, XCB_XIM_XNInputStyle, &input_style, XCB_XIM_XNClientWindow,
                                                  &window->handle, XCB_XIM_XNFocusWindow, &window->handle, XCB_XIM_XNPreeditAttributes, &preedit, NULL);
                }
                free(preedit.data);
            }
            else
            {
                requested = xcb_xim_create_ic(handle->xim, wm_xim_create_ic_callback, window, XCB_XIM_XNInputStyle, &input_style, XCB_XIM_XNClientWindow,
                                              &window->handle, XCB_XIM_XNFocusWindow, &window->handle, NULL);
            }

            if (requested)
            {
                window->xim_create_ic_pending = true;
                break;
            }
            else
            {
                window->xim_input_style_attempt_index = wm_xim_next_input_style_attempt(window->xim_input_style_attempt_index);
            }
        }
    }
}

BUSTER_GLOBAL_LOCAL void wm_xim_create_ic(WmHandle* handle)
{
    SliceWmWindowHandle windows = get_windows(handle);
    for (u64 i = 0; i < windows.length; i += 1)
    {
        WmWindowHandle* window = &windows.pointer[i];
        wm_xim_create_ic_for_window(window);
    }
}

typedef enum X11Atom
{
    X11_ATOM_WM_PROTOCOLS,
    X11_ATOM_WM_DELETE_WINDOW,
    X11_ATOM_WM_NAME,
    X11_ATOM_NET_WM_NAME,
    X11_ATOM_UTF8_STRING,
    X11_ATOM_WM_CLASS,
    X11_ATOM_RESOURCE_MANAGER,
    X11_ATOM_XDND_AWARE,
    X11_ATOM_XDND_ENTER,
    X11_ATOM_XDND_POSITION,
    X11_ATOM_XDND_STATUS,
    X11_ATOM_XDND_TYPE_LIST,
    X11_ATOM_XDND_ACTION_COPY,
    X11_ATOM_XDND_DROP,
    X11_ATOM_XDND_LEAVE,
    X11_ATOM_XDND_FINISHED,
    X11_ATOM_XDND_SELECTION,
    X11_ATOM_XDND_SELECTION_PROPERTY,
    X11_ATOM_TEXT_URI_LIST,
    X11_ATOM_INCR,
    X11_ATOM_COUNT,
} X11Atom;

BUSTER_GLOBAL_LOCAL String8 atom_names[X11_ATOM_COUNT] = {
    S8_INITIALIZER("WM_PROTOCOLS"),
    S8_INITIALIZER("WM_DELETE_WINDOW"),
    S8_INITIALIZER("WM_NAME"),
    S8_INITIALIZER("_NET_WM_NAME"),
    S8_INITIALIZER("UTF8_STRING"),
    S8_INITIALIZER("WM_CLASS"),
    S8_INITIALIZER("RESOURCE_MANAGER"),
    S8_INITIALIZER("XdndAware"),
    S8_INITIALIZER("XdndEnter"),
    S8_INITIALIZER("XdndPosition"),
    S8_INITIALIZER("XdndStatus"),
    S8_INITIALIZER("XdndTypeList"),
    S8_INITIALIZER("XdndActionCopy"),
    S8_INITIALIZER("XdndDrop"),
    S8_INITIALIZER("XdndLeave"),
    S8_INITIALIZER("XdndFinished"),
    S8_INITIALIZER("XdndSelection"),
    S8_INITIALIZER("BusterXdndSelection"),
    S8_INITIALIZER("text/uri-list"),
    S8_INITIALIZER("INCR"),
};

BUSTER_GLOBAL_LOCAL xcb_intern_atom_reply_t* atom_replies[BUSTER_ARRAY_LENGTH(atom_names)];
BUSTER_GLOBAL_LOCAL xcb_intern_atom_cookie_t atom_cookies[BUSTER_ARRAY_LENGTH(atom_names)];

BUSTER_GLOBAL_LOCAL xcb_atom_t wm_x11_atom(X11Atom atom)
{
    xcb_atom_t result = XCB_ATOM_NONE;
    if (atom_replies[atom])
    {
        result = atom_replies[atom]->atom;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool wm_x11_ascii_equal_ignore_case(String8 value, const char* literal)
{
    u64 literal_length = 0;
    while (literal[literal_length])
    {
        literal_length += 1;
    }

    bool result = value.length == literal_length;
    for (u64 index = 0; result && index < value.length; index += 1)
    {
        u8 a = (u8)value.pointer[index];
        u8 b = (u8)literal[index];
        if ('A' <= a && a <= 'Z')
        {
            a = (u8)(a + ('a' - 'A'));
        }
        if ('A' <= b && b <= 'Z')
        {
            b = (u8)(b + ('a' - 'A'));
        }
        result = a == b;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL u8 wm_x11_hex_value(u8 code_unit)
{
    u8 result = 0xffu;
    if ('0' <= code_unit && code_unit <= '9')
    {
        result = (u8)(code_unit - '0');
    }
    else if ('a' <= code_unit && code_unit <= 'f')
    {
        result = (u8)(code_unit - 'a' + 10);
    }
    else if ('A' <= code_unit && code_unit <= 'F')
    {
        result = (u8)(code_unit - 'A' + 10);
    }
    return result;
}

BUSTER_GLOBAL_LOCAL u64 wm_x11_file_uri_decoded_length(String8 uri, u64* path_start_out, bool* valid_out)
{
    bool valid = uri.pointer != 0 && uri.length >= 5 && uri.pointer[4] == ':' &&
                 wm_x11_ascii_equal_ignore_case(string_from_pointer_length(uri.pointer, 4), "file");
    u64 path_start = 5;
    if (valid && path_start + 1 < uri.length && uri.pointer[path_start] == '/' && uri.pointer[path_start + 1] == '/')
    {
        u64 authority_start = path_start + 2;
        u64 authority_end = authority_start;
        while (authority_end < uri.length && uri.pointer[authority_end] != '/' && uri.pointer[authority_end] != '?' && uri.pointer[authority_end] != '#')
        {
            authority_end += 1;
        }
        String8 authority = string_from_pointer_length(uri.pointer + authority_start, authority_end - authority_start);
        valid = authority.length == 0 || wm_x11_ascii_equal_ignore_case(authority, "localhost");
        valid = valid && authority_end < uri.length && uri.pointer[authority_end] == '/';
        path_start = authority_end;
    }
    else if (valid)
    {
        valid = path_start < uri.length && uri.pointer[path_start] == '/';
    }

    u64 decoded_length = 0;
    for (u64 index = path_start; valid && index < uri.length; index += 1)
    {
        u8 code_unit = (u8)uri.pointer[index];
        if (code_unit == '?' || code_unit == '#' || code_unit < 0x20u || code_unit == 0x7fu)
        {
            valid = false;
        }
        else if (code_unit == '%')
        {
            if (uri.length - index < 3)
            {
                valid = false;
            }
            else
            {
                u8 high = wm_x11_hex_value((u8)uri.pointer[index + 1]);
                u8 low = wm_x11_hex_value((u8)uri.pointer[index + 2]);
                u8 decoded = (u8)((high << 4) | low);
                valid = high != 0xffu && low != 0xffu && decoded != 0 && decoded >= 0x20u && decoded != 0x7fu;
                index += 2;
            }
        }
        if (valid)
        {
            if (decoded_length == UINT64_MAX)
            {
                valid = false;
            }
            else
            {
                decoded_length += 1;
            }
        }
    }

    if (!valid || decoded_length == 0)
    {
        valid = false;
        decoded_length = 0;
    }

    if (path_start_out)
    {
        *path_start_out = path_start;
    }
    if (valid_out)
    {
        *valid_out = valid;
    }
    return decoded_length;
}

String8 wm_x11_decode_file_uri(Arena* arena, String8 uri, bool* valid_out)
{
    u64 path_start = 0;
    bool valid = false;
    u64 decoded_length = wm_x11_file_uri_decoded_length(uri, &path_start, &valid);
    String8 result = {0};
    if (valid)
    {
        result.pointer = arena_allocate(arena, char8, decoded_length);
        result.length = decoded_length;
        u64 output_index = 0;
        for (u64 index = path_start; index < uri.length; index += 1)
        {
            u8 code_unit = (u8)uri.pointer[index];
            if (code_unit == '%')
            {
                u8 high = wm_x11_hex_value((u8)uri.pointer[index + 1]);
                u8 low = wm_x11_hex_value((u8)uri.pointer[index + 2]);
                result.pointer[output_index] = (char8)((high << 4) | low);
                output_index += 1;
                index += 2;
            }
            else
            {
                result.pointer[output_index] = (char8)code_unit;
                output_index += 1;
            }
        }
        valid = wm_file_path_is_valid(result);
        if (!valid)
        {
            result = (String8){0};
        }
    }
    if (valid_out)
    {
        *valid_out = valid;
    }
    return result;
}

SliceString8 wm_x11_parse_uri_list(Arena* arena, String8 uri_list)
{
    u64 candidate_count = 0;
    u64 decoded_path_bytes = 0;
    bool budget_valid = true;
    u64 line_start = 0;
    for (u64 index = 0; index <= uri_list.length && budget_valid; index += 1)
    {
        bool at_end = index == uri_list.length;
        if (at_end || uri_list.pointer[index] == '\n')
        {
            u64 line_end = index;
            if (line_end > line_start && uri_list.pointer[line_end - 1] == '\r')
            {
                line_end -= 1;
            }
            if (line_end > line_start && uri_list.pointer[line_start] != '#')
            {
                if (candidate_count >= BUSTER_NATIVE_FILE_DROP_MAX_PATH_COUNT)
                {
                    budget_valid = false;
                }
                else
                {
                    candidate_count += 1;
                    String8 uri = string_from_pointer_length(uri_list.pointer + line_start, line_end - line_start);
                    bool uri_valid = false;
                    u64 path_length = wm_x11_file_uri_decoded_length(uri, 0, &uri_valid);
                    if (uri_valid)
                    {
                        if (path_length > BUSTER_NATIVE_FILE_DROP_MAX_PATH_BYTES - decoded_path_bytes)
                        {
                            budget_valid = false;
                        }
                        else
                        {
                            decoded_path_bytes += path_length;
                        }
                    }
                }
            }
            line_start = index + 1;
        }
    }

    SliceString8 result = {0};
    if (budget_valid && candidate_count != 0)
    {
        u64 accepted_count = 0;
        line_start = 0;
        for (u64 index = 0; index <= uri_list.length; index += 1)
        {
            bool at_end = index == uri_list.length;
            if (at_end || uri_list.pointer[index] == '\n')
            {
                u64 line_end = index;
                if (line_end > line_start && uri_list.pointer[line_end - 1] == '\r')
                {
                    line_end -= 1;
                }
                if (line_end > line_start && uri_list.pointer[line_start] != '#')
                {
                    bool uri_valid = false;
                    String8 uri = string_from_pointer_length(uri_list.pointer + line_start, line_end - line_start);
                    wm_x11_file_uri_decoded_length(uri, 0, &uri_valid);
                    if (uri_valid)
                    {
                        if (accepted_count == UINT64_MAX)
                        {
                            return result;
                        }
                        accepted_count += 1;
                    }
                }
                line_start = index + 1;
            }
        }

        if (wm_native_file_drop_array_size_allowed(accepted_count, sizeof(String8)))
        {
            if (accepted_count != 0)
            {
                result.pointer = arena_allocate(arena, String8, accepted_count);
                result.length = accepted_count;
            }
            u64 output_index = 0;
            line_start = 0;
            for (u64 index = 0; index <= uri_list.length; index += 1)
            {
                bool at_end = index == uri_list.length;
                if (at_end || uri_list.pointer[index] == '\n')
                {
                    u64 line_end = index;
                    if (line_end > line_start && uri_list.pointer[line_end - 1] == '\r')
                    {
                        line_end -= 1;
                    }
                    if (line_end > line_start && uri_list.pointer[line_start] != '#')
                    {
                        bool valid = false;
                        String8 uri = string_from_pointer_length(uri_list.pointer + line_start, line_end - line_start);
                        String8 path = wm_x11_decode_file_uri(arena, uri, &valid);
                        if (valid)
                        {
                            result.pointer[output_index] = path;
                            output_index += 1;
                        }
                    }
                    line_start = index + 1;
                }
            }
            result.length = output_index;
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL void wm_x11_xdnd_clear_transfer_data(WmHandle* windowing)
{
    if (windowing->xdnd_transfer_arena)
    {
        arena_reset_to_start(windowing->xdnd_transfer_arena);
    }
    windowing->xdnd_transfer_data = (String8){0};
    windowing->xdnd_transfer_capacity = 0;
    windowing->xdnd_transfer_expected = 0;
    windowing->xdnd_incremental = false;
}

BUSTER_GLOBAL_LOCAL bool wm_x11_xdnd_change_source_event_mask(WmHandle* windowing, xcb_window_t source, u32 event_mask)
{
    bool result = false;
    if (windowing->connection && source != XCB_WINDOW_NONE)
    {
        xcb_void_cookie_t cookie = xcb_change_window_attributes_checked(windowing->connection, source, XCB_CW_EVENT_MASK, &event_mask);
        xcb_generic_error_t* error = xcb_request_check(windowing->connection, cookie);
        result = error == 0;
        free(error);
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool wm_x11_xdnd_watch_source(WmHandle* windowing, xcb_window_t source)
{
    bool result = false;
    if (windowing->connection && source != XCB_WINDOW_NONE)
    {
        WmWindowHandle* known_window = wm_x11_window_from_xcb(windowing, source);
        if (known_window)
        {
            windowing->xdnd_source_destroy_observed = true;
            result = true;
        }
        else
        {
            xcb_generic_error_t* error = 0;
            xcb_get_window_attributes_cookie_t cookie = xcb_get_window_attributes(windowing->connection, source);
            xcb_get_window_attributes_reply_t* reply = xcb_get_window_attributes_reply(windowing->connection, cookie, &error);
            if (reply)
            {
                u32 saved_mask = reply->your_event_mask;
                u32 watched_mask = wm_x11_xdnd_source_watch_event_mask(saved_mask);
                free(reply);
                if (wm_x11_xdnd_change_source_event_mask(windowing, source, watched_mask))
                {
                    windowing->xdnd_source_event_mask = saved_mask;
                    windowing->xdnd_source_event_mask_saved = true;
                    windowing->xdnd_source_destroy_observed = true;
                    result = true;
                }
            }
            free(error);
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL void wm_x11_xdnd_restore_source_watch(WmHandle* windowing)
{
    if (windowing->xdnd_source_event_mask_saved && windowing->connection && windowing->xdnd_source != XCB_WINDOW_NONE)
    {
        u32 saved_mask = wm_x11_xdnd_source_restore_event_mask(windowing->xdnd_source_event_mask);
        // A source can disappear between the transaction reset and this checked restore.
        // BadWindow is deliberately ignored; no stale observation remains in our state.
        wm_x11_xdnd_change_source_event_mask(windowing, windowing->xdnd_source, saved_mask);
    }
}

BUSTER_GLOBAL_LOCAL void wm_x11_xdnd_reset(WmHandle* windowing)
{
    wm_x11_xdnd_restore_source_watch(windowing);
    wm_x11_xdnd_clear_transfer_data(windowing);
    windowing->xdnd_source = XCB_WINDOW_NONE;
    windowing->xdnd_source_event_mask = 0;
    windowing->xdnd_source_event_mask_saved = false;
    windowing->xdnd_source_destroy_observed = false;
    windowing->xdnd_window = 0;
    windowing->xdnd_property = XCB_ATOM_NONE;
    windowing->xdnd_position_time = XCB_CURRENT_TIME;
    windowing->xdnd_position = (WmOffset){0};
    windowing->xdnd_version = 0;
    windowing->xdnd_active = false;
    windowing->xdnd_uri_list_supported = false;
    windowing->xdnd_position_seen = false;
    windowing->xdnd_accept = false;
    windowing->xdnd_drop_pending = false;
}

BUSTER_GLOBAL_LOCAL bool wm_x11_xdnd_append_transfer(WmHandle* windowing, const u8* bytes, u64 length)
{
    bool result = windowing->xdnd_transfer_arena != 0 &&
                  wm_x11_xdnd_transfer_length_allowed(windowing->xdnd_transfer_data.length, length);
    if (result && length != 0)
    {
        u64 required = windowing->xdnd_transfer_data.length + length;
        if (required > windowing->xdnd_transfer_capacity)
        {
            u64 capacity = windowing->xdnd_transfer_capacity ? windowing->xdnd_transfer_capacity : 4096;
            while (capacity < required)
            {
                if (capacity > UINT64_MAX / 2)
                {
                    capacity = required;
                    break;
                }
                capacity *= 2;
            }
            char8* destination = arena_allocate(windowing->xdnd_transfer_arena, char8, capacity);
            if (windowing->xdnd_transfer_data.length != 0)
            {
                memcpy(destination, windowing->xdnd_transfer_data.pointer, windowing->xdnd_transfer_data.length);
            }
            windowing->xdnd_transfer_data.pointer = destination;
            windowing->xdnd_transfer_capacity = capacity;
        }
        memcpy(windowing->xdnd_transfer_data.pointer + windowing->xdnd_transfer_data.length, bytes, length);
        windowing->xdnd_transfer_data.length = required;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL u32 wm_x11_xdnd_property_request_length(u64 remaining_bytes)
{
    u64 result = remaining_bytes > UINT64_MAX - 3 ? UINT64_MAX : (remaining_bytes + 3) / 4;
    return result > UINT32_MAX ? UINT32_MAX : (u32)result;
}

BUSTER_GLOBAL_LOCAL bool wm_x11_xdnd_property_contains_atom(WmHandle* windowing, xcb_window_t window, xcb_atom_t property, xcb_atom_t wanted)
{
    bool result = false;
    u32 offset = 0;
    while (!result)
    {
        xcb_get_property_cookie_t cookie = xcb_get_property(windowing->connection, false, window, property, XCB_ATOM_ATOM, offset, UINT32_MAX);
        xcb_get_property_reply_t* reply = xcb_get_property_reply(windowing->connection, cookie, 0);
        if (!reply)
        {
            break;
        }
        int value_length = xcb_get_property_value_length(reply);
        if (reply->type != XCB_ATOM_ATOM || reply->format != 32 || value_length < 0 || (value_length % (int)sizeof(u32)) != 0)
        {
            free(reply);
            break;
        }
        u32* values = (u32*)xcb_get_property_value(reply);
        u32 value_count = (u32)value_length / (u32)sizeof(u32);
        for (u32 index = 0; index < value_count; index += 1)
        {
            if (values[index] == wanted)
            {
                result = true;
                break;
            }
        }
        u32 bytes_after = reply->bytes_after;
        u32 increment = ((u32)value_length + 3u) / 4u;
        free(reply);
        if (result || bytes_after == 0 || increment == 0 || offset > UINT32_MAX - increment)
        {
            break;
        }
        offset += increment;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool wm_x11_xdnd_source_supports_uri_list(WmHandle* windowing, xcb_client_message_event_t* message)
{
    xcb_atom_t uri_list = wm_x11_atom(X11_ATOM_TEXT_URI_LIST);
    bool result = uri_list != XCB_ATOM_NONE;
    if (result)
    {
        result = message->data.data32[2] == uri_list || message->data.data32[3] == uri_list || message->data.data32[4] == uri_list;
    }
    if (!result && (message->data.data32[1] & 1u) != 0 && wm_x11_atom(X11_ATOM_XDND_TYPE_LIST) != XCB_ATOM_NONE)
    {
        result = wm_x11_xdnd_property_contains_atom(windowing, message->data.data32[0], wm_x11_atom(X11_ATOM_XDND_TYPE_LIST), uri_list);
    }
    return result;
}

BUSTER_GLOBAL_LOCAL void wm_x11_xdnd_send_message(WmHandle* windowing, xcb_window_t destination, xcb_atom_t type, u32 data0, u32 data1, u32 data2,
                                                   u32 data3, u32 data4)
{
    if (windowing->connection && destination != XCB_WINDOW_NONE && type != XCB_ATOM_NONE)
    {
        xcb_client_message_event_t message = {0};
        message.response_type = XCB_CLIENT_MESSAGE;
        message.window = destination;
        message.type = type;
        message.format = 32;
        message.data.data32[0] = data0;
        message.data.data32[1] = data1;
        message.data.data32[2] = data2;
        message.data.data32[3] = data3;
        message.data.data32[4] = data4;
        xcb_send_event(windowing->connection, false, destination, XCB_EVENT_MASK_NO_EVENT, (const char*)&message);
        xcb_flush(windowing->connection);
    }
}

BUSTER_GLOBAL_LOCAL void wm_x11_xdnd_send_status(WmHandle* windowing, bool accepted)
{
    if (windowing->xdnd_active && windowing->xdnd_window)
    {
        xcb_atom_t action = accepted ? wm_x11_atom(X11_ATOM_XDND_ACTION_COPY) : XCB_ATOM_NONE;
        wm_x11_xdnd_send_message(windowing, windowing->xdnd_source, wm_x11_atom(X11_ATOM_XDND_STATUS), windowing->xdnd_window->handle, accepted ? 1u : 0u, 0,
                                 0, action);
    }
}

BUSTER_GLOBAL_LOCAL void wm_x11_xdnd_send_finished(WmHandle* windowing, bool accepted)
{
    if (windowing->xdnd_active && windowing->xdnd_window)
    {
        xcb_atom_t action = accepted ? wm_x11_atom(X11_ATOM_XDND_ACTION_COPY) : XCB_ATOM_NONE;
        wm_x11_xdnd_send_message(windowing, windowing->xdnd_source, wm_x11_atom(X11_ATOM_XDND_FINISHED), windowing->xdnd_window->handle, accepted ? 1u : 0u,
                                 action, 0, 0);
    }
}

BUSTER_GLOBAL_LOCAL bool wm_x11_xdnd_position_from_event(WmHandle* windowing, xcb_window_t target, u32 packed_position, WmOffset* position)
{
    bool result = false;
    xcb_screen_t* screen = wm_x11_screen_from_handle(windowing);
    if (screen && position)
    {
        s32 root_x = (s32)(s16)(packed_position >> 16);
        s32 root_y = (s32)(s16)(packed_position & 0xffffu);
        xcb_translate_coordinates_cookie_t cookie = xcb_translate_coordinates(windowing->connection, screen->root, target, (s16)root_x, (s16)root_y);
        xcb_translate_coordinates_reply_t* reply = xcb_translate_coordinates_reply(windowing->connection, cookie, 0);
        if (reply)
        {
            *position = wm_x11_drop_position_from_root(root_x, root_y, root_x - (s32)reply->dst_x, root_y - (s32)reply->dst_y);
            result = true;
            free(reply);
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool wm_x11_xdnd_read_property(WmHandle* windowing, xcb_window_t requestor, xcb_atom_t property)
{
    bool result = true;
    u32 offset = 0;
    while (result)
    {
        u64 current_length = windowing->xdnd_transfer_data.length;
        u64 remaining_bytes = current_length <= BUSTER_X11_XDND_MAX_TRANSFER_BYTES ? BUSTER_X11_XDND_MAX_TRANSFER_BYTES - current_length : 0;
        u32 request_length = wm_x11_xdnd_property_request_length(remaining_bytes);
        xcb_get_property_cookie_t cookie = xcb_get_property(windowing->connection, false, requestor, property, XCB_ATOM_ANY, offset, request_length);
        xcb_get_property_reply_t* reply = xcb_get_property_reply(windowing->connection, cookie, 0);
        if (!reply)
        {
            result = false;
            break;
        }
        int value_length = xcb_get_property_value_length(reply);
        if (reply->type != wm_x11_atom(X11_ATOM_TEXT_URI_LIST) || reply->format != 8 || value_length < 0)
        {
            result = false;
        }
        else if (!wm_x11_xdnd_append_transfer(windowing, (const u8*)xcb_get_property_value(reply), (u64)value_length))
        {
            result = false;
        }
        u32 bytes_after = reply->bytes_after;
        u32 increment = ((u32)(value_length > 0 ? value_length : 0) + 3u) / 4u;
        free(reply);
        if (!result || bytes_after == 0)
        {
            break;
        }
        if (increment == 0 || offset > UINT32_MAX - increment)
        {
            result = false;
            break;
        }
        offset += increment;
    }
    xcb_delete_property(windowing->connection, requestor, property);
    return result;
}

BUSTER_GLOBAL_LOCAL bool wm_x11_xdnd_read_increment(WmHandle* windowing, bool* complete)
{
    bool result = true;
    *complete = false;
    u64 current_length = windowing->xdnd_transfer_data.length;
    u64 remaining_bytes = current_length <= BUSTER_X11_XDND_MAX_TRANSFER_BYTES ? BUSTER_X11_XDND_MAX_TRANSFER_BYTES - current_length : 0;
    u32 request_length = wm_x11_xdnd_property_request_length(remaining_bytes);
    xcb_get_property_cookie_t cookie = xcb_get_property(windowing->connection, true, windowing->xdnd_window->handle, windowing->xdnd_property, XCB_ATOM_ANY, 0,
                                                        request_length);
    xcb_get_property_reply_t* reply = xcb_get_property_reply(windowing->connection, cookie, 0);
    if (!reply)
    {
        result = false;
    }
    else
    {
        int value_length = xcb_get_property_value_length(reply);
        if (reply->bytes_after != 0 || value_length < 0 || (value_length != 0 && (reply->type != wm_x11_atom(X11_ATOM_TEXT_URI_LIST) || reply->format != 8)))
        {
            result = false;
        }
        else if (!wm_x11_xdnd_append_transfer(windowing, (const u8*)xcb_get_property_value(reply), (u64)value_length))
        {
            result = false;
        }
        *complete = value_length == 0;
        free(reply);
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool wm_x11_xdnd_start_transfer(WmHandle* windowing)
{
    bool result = false;
    wm_x11_xdnd_clear_transfer_data(windowing);
    u32 request_length = wm_x11_xdnd_property_request_length(BUSTER_X11_XDND_MAX_TRANSFER_BYTES);
    xcb_get_property_cookie_t cookie = xcb_get_property(windowing->connection, false, windowing->xdnd_window->handle, windowing->xdnd_property, XCB_ATOM_ANY, 0,
                                                        request_length);
    xcb_get_property_reply_t* reply = xcb_get_property_reply(windowing->connection, cookie, 0);
    if (reply)
    {
        int value_length = xcb_get_property_value_length(reply);
        if (reply->type == wm_x11_atom(X11_ATOM_INCR))
        {
            if (reply->format == 32 && value_length >= (int)sizeof(u32))
            {
                // The advertised INCR length is only a hint. Every chunk is bounded by
                // BUSTER_X11_XDND_MAX_TRANSFER_BYTES before entering the transfer arena.
                windowing->xdnd_transfer_expected = *(u32*)xcb_get_property_value(reply);
                windowing->xdnd_incremental = true;
                xcb_delete_property(windowing->connection, windowing->xdnd_window->handle, windowing->xdnd_property);
                result = true;
            }
        }
        else
        {
            result = reply->type == wm_x11_atom(X11_ATOM_TEXT_URI_LIST) && reply->format == 8 && value_length >= 0;
        }
        free(reply);
        if (result && !windowing->xdnd_incremental)
        {
            result = wm_x11_xdnd_read_property(windowing, windowing->xdnd_window->handle, windowing->xdnd_property);
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL void wm_x11_xdnd_complete(WmHandle* windowing, bool transfer_success)
{
    bool accepted = false;
    if (transfer_success && windowing->event_arena && windowing->xdnd_transfer_data.length != 0)
    {
        SliceString8 paths = wm_x11_parse_uri_list(windowing->event_arena, windowing->xdnd_transfer_data);
        if (paths.length != 0)
        {
            WmOffset position = {.x = 0, .y = 0};
            if (windowing->xdnd_position_seen)
            {
                position = windowing->xdnd_position;
            }
            wm_event_push(windowing, (WmEvent){
                                         .kind = WM_EVENT_FILE_DROP,
                                         .window = windowing->xdnd_window,
                                         .position = position,
                                         .paths = paths,
                                     });
            accepted = true;
        }
    }
    if (windowing->xdnd_drop_pending && windowing->xdnd_window && windowing->xdnd_property != XCB_ATOM_NONE)
    {
        xcb_delete_property(windowing->connection, windowing->xdnd_window->handle, windowing->xdnd_property);
    }
    wm_x11_xdnd_send_finished(windowing, accepted);
    wm_x11_xdnd_reset(windowing);
}

BUSTER_GLOBAL_LOCAL void wm_x11_window_set_metadata(WmWindowHandle* window, WmWindowCreate create)
{
    if (window && window->owner && window->owner->connection && window->handle)
    {
        xcb_connection_t* connection = window->owner->connection;

        if (create.name.pointer && create.name.length)
        {
            xcb_atom_t wm_name = wm_x11_atom(X11_ATOM_WM_NAME);
            if (wm_name)
            {
                xcb_change_property(connection, XCB_PROP_MODE_REPLACE, window->handle, wm_name, XCB_ATOM_STRING, 8, (u32)create.name.length,
                                    create.name.pointer);
            }

            xcb_atom_t net_wm_name = wm_x11_atom(X11_ATOM_NET_WM_NAME);
            xcb_atom_t utf8_string = wm_x11_atom(X11_ATOM_UTF8_STRING);
            if (net_wm_name && utf8_string)
            {
                xcb_change_property(connection, XCB_PROP_MODE_REPLACE, window->handle, net_wm_name, utf8_string, 8, (u32)create.name.length,
                                    create.name.pointer);
            }
        }

        const char wm_class[] = "buster\0buster";
        xcb_atom_t wm_class_atom = wm_x11_atom(X11_ATOM_WM_CLASS);
        if (wm_class_atom)
        {
            xcb_change_property(connection, XCB_PROP_MODE_REPLACE, window->handle, wm_class_atom, XCB_ATOM_STRING, 8, sizeof(wm_class), wm_class);
        }

        xcb_atom_t xdnd_aware = wm_x11_atom(X11_ATOM_XDND_AWARE);
        if (xdnd_aware)
        {
            u32 xdnd_version = 5;
            xcb_change_property(connection, XCB_PROP_MODE_REPLACE, window->handle, xdnd_aware, XCB_ATOM_ATOM, 32, 1, &xdnd_version);
        }
    }
}

BUSTER_GLOBAL_LOCAL xcb_screen_t* wm_x11_screen_from_handle(WmHandle* windowing)
{
    xcb_screen_t* result = 0;
    if (windowing && windowing->setup)
    {
        int screen_index = 0;
        for (xcb_screen_iterator_t iterator = xcb_setup_roots_iterator(windowing->setup); iterator.rem; xcb_screen_next(&iterator), screen_index += 1)
        {
            if (screen_index == windowing->screen_id)
            {
                result = iterator.data;
                break;
            }
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool wm_x11_dpi_from_xresources(String8 resources, f32* out_dpi)
{
    bool result = false;
    u64 dpi_name_index = string_first_sequence(resources, S8("Xft.dpi"));
    if (dpi_name_index != BUSTER_STRING_NO_MATCH)
    {
        u64 value_index = dpi_name_index + S8("Xft.dpi").length;
        while (value_index < resources.length)
        {
            char8 code_unit = resources.pointer[value_index];
            if (code_unit == ':' || code_unit == '=' || code_unit == ' ' || code_unit == '\t')
            {
                value_index += 1;
            }
            else
            {
                break;
            }
        }

        u64 integer_part = 0;
        bool has_digit = false;
        while (value_index < resources.length)
        {
            char8 code_unit = resources.pointer[value_index];
            if ('0' <= code_unit && code_unit <= '9')
            {
                integer_part = integer_part * 10 + (u64)(code_unit - '0');
                value_index += 1;
                has_digit = true;
            }
            else
            {
                break;
            }
        }

        f32 dpi = (f32)integer_part;
        if (value_index < resources.length && resources.pointer[value_index] == '.')
        {
            value_index += 1;
            f32 place = 0.1f;
            while (value_index < resources.length)
            {
                char8 code_unit = resources.pointer[value_index];
                if ('0' <= code_unit && code_unit <= '9')
                {
                    dpi += (f32)(code_unit - '0') * place;
                    place *= 0.1f;
                    value_index += 1;
                    has_digit = true;
                }
                else
                {
                    break;
                }
            }
        }

        if (has_digit && 24.0f <= dpi && dpi <= 768.0f)
        {
            *out_dpi = dpi;
            result = true;
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL f32 wm_x11_dpi_from_screen(WmHandle* windowing)
{
    f32 result = 96.0f;
    xcb_screen_t* screen = wm_x11_screen_from_handle(windowing);
    if (screen)
    {
        bool dpi_from_resources = false;
        xcb_atom_t resource_manager = wm_x11_atom(X11_ATOM_RESOURCE_MANAGER);
        if (resource_manager)
        {
            xcb_get_property_cookie_t cookie = xcb_get_property(windowing->connection, false, screen->root, resource_manager, XCB_ATOM_STRING, 0, 16 * 1024);
            xcb_get_property_reply_t* reply = xcb_get_property_reply(windowing->connection, cookie, 0);
            if (reply)
            {
                int value_length = xcb_get_property_value_length(reply);
                if (value_length > 0)
                {
                    String8 resources = string_from_pointer_length((char8*)xcb_get_property_value(reply), (u64)value_length);
                    dpi_from_resources = wm_x11_dpi_from_xresources(resources, &result);
                }
                free(reply);
            }
        }

        if (!dpi_from_resources && screen->width_in_pixels && screen->width_in_millimeters)
        {
            f32 dpi = ((f32)screen->width_in_pixels * 25.4f) / (f32)screen->width_in_millimeters;
            if (24.0f <= dpi && dpi <= 768.0f)
            {
                result = dpi;
            }
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL void wm_platform_poll_events(Arena* arena, WmHandle* windowing)
{
    xcb_generic_event_t* event;
    xcb_connection_t* connection = windowing->connection;

    while ((event = xcb_poll_for_event(connection)))
    {
        u8 event_type = event->response_type & 0x7f;
        if (event_type == 0)
        {
            xcb_generic_error_t* error = (xcb_generic_error_t*)event;
            string_print(S8("XCB error: code {u8}, major {u8}, minor {u16}\n"), error->error_code, error->major_code, error->minor_code);
            free(event);
            continue;
        }

        bool filtered = windowing->xim && xcb_xim_filter_event(windowing->xim, event);
        if (!filtered)
        {
            if (windowing->xkb_base_event && event_type == windowing->xkb_base_event)
            {
                wm_x11_handle_xkb_event(windowing, event);
            }
            else
            {
                switch (event_type)
                {
                    break;
                case XCB_KEY_PRESS:   // 2
                case XCB_KEY_RELEASE: // 3
                {
                    xcb_key_press_event_t* key_event = (xcb_key_press_event_t*)event;
                    WmWindowHandle* window = wm_x11_window_from_xcb(windowing, key_event->event);
                    if (window)
                    {
                        windowing->focused_window = window;
                    }

                    u8 modifiers = wm_x11_modifiers_from_state(key_event->state);
                    bool text_handled_by_xim = false;

                    if (event_type == XCB_KEY_PRESS && windowing->xim && window && window->ic)
                    {
                        text_handled_by_xim = xcb_xim_forward_event(windowing->xim, window->ic, key_event);
                    }

                    xkb_keysym_t key_symbol = XKB_KEY_NoSymbol;
                    xkb_keysym_t key_symbol_without_modifiers = XKB_KEY_NoSymbol;
                    if (windowing->xkb_state)
                    {
                        key_symbol = xkb_state_key_get_one_sym(windowing->xkb_state, key_event->detail);
                        key_symbol_without_modifiers = key_symbol;
                    }
                    else if (windowing->key_symbols)
                    {
                        int column = (key_event->state & XCB_KEY_BUT_MASK_SHIFT) ? 1 : 0;
                        key_symbol = xcb_key_symbols_get_keysym(windowing->key_symbols, key_event->detail, column);
                        key_symbol_without_modifiers = xcb_key_symbols_get_keysym(windowing->key_symbols, key_event->detail, 0);
                    }

                    if (event_type == XCB_KEY_PRESS)
                    {
                        struct xkb_compose_state* compose_state = window ? window->xkb_compose_state : 0;
                        bool mod_control = (key_event->state & XCB_KEY_BUT_MASK_CONTROL) != 0;
                        bool mod_alt = (key_event->state & XCB_KEY_BUT_MASK_MOD_1) != 0;

                        int composed_text_length = 0;

                        if (event_type == XCB_KEY_PRESS && !text_handled_by_xim && (windowing->xkb_state || windowing->key_symbols) && !mod_control && !mod_alt)
                        {
                            char text_buffer[64] = {0};
                            bool consumed = false;

                            if (windowing->xkb_state)
                            {
                                if (compose_state)
                                {
                                    enum xkb_compose_feed_result feed_result = xkb_compose_state_feed(compose_state, key_symbol);
                                    if (feed_result == XKB_COMPOSE_FEED_ACCEPTED)
                                    {
                                        enum xkb_compose_status compose_status = xkb_compose_state_get_status(compose_state);
                                        switch (compose_status)
                                        {
                                            break;
                                        case XKB_COMPOSE_COMPOSED:
                                        {
                                            composed_text_length = xkb_compose_state_get_utf8(compose_state, text_buffer, sizeof(text_buffer));
                                            if (composed_text_length == 0)
                                            {
                                                xkb_keysym_t composed_key_symbol = xkb_compose_state_get_one_sym(compose_state);
                                                int text_length_with_null = xkb_keysym_to_utf8(composed_key_symbol, text_buffer, sizeof(text_buffer));
                                                if (text_length_with_null > 0)
                                                {
                                                    composed_text_length = text_length_with_null - 1;
                                                }
                                            }

                                            consumed = true;
                                            xkb_compose_state_reset(compose_state);
                                        }
                                        break;
                                        case XKB_COMPOSE_COMPOSING:
                                        {
                                            consumed = true;
                                        }
                                        break;
                                        case XKB_COMPOSE_CANCELLED:
                                        {
                                            xkb_compose_state_reset(compose_state);
                                        }
                                        break;
                                        case XKB_COMPOSE_NOTHING:
                                        {
                                        }
                                        }
                                    }
                                }

                                if (!consumed && composed_text_length == 0)
                                {
                                    composed_text_length = xkb_state_key_get_utf8(windowing->xkb_state, key_event->detail, text_buffer, sizeof(text_buffer));
                                }
                            }
                            else
                            {
                                int text_length_with_nul = xkb_keysym_to_utf8(key_symbol, text_buffer, sizeof(text_buffer));
                                if (text_length_with_nul > 0)
                                {
                                    composed_text_length = text_length_with_nul - 1;
                                }
                            }

                            // XKB reports conversion failures with a negative
                            // result. Drop only the text event; callers still
                            // receive the corresponding physical key event.
                            u64 text_length = wm_x11_utf8_result_length(composed_text_length, sizeof(text_buffer));

                            if (text_length > 0)
                            {
                                String8 text = string_duplicate_arena(arena, string_from_pointer_length(text_buffer, text_length), false);
                                wm_event_push(windowing, (WmEvent){
                                                             .kind = WM_EVENT_TEXT_INPUT,
                                                             .window = window,
                                                             .text = text,
                                                         });
                            }
                        }
                    }

                    if (windowing->xkb_state)
                    {
                        enum xkb_key_direction direction = event_type == XCB_KEY_PRESS ? XKB_KEY_DOWN : XKB_KEY_UP;
                        xkb_state_update_key(windowing->xkb_state, key_event->detail, direction);
                    }

                    WmKey key = WM_KEY_NULL;
                    if (windowing->xkb_keymap)
                    {
                        key = wm_x11_key_from_xkb_name(xkb_keymap_key_get_name(windowing->xkb_keymap, key_event->detail));
                    }

                    if (key == WM_KEY_NULL)
                    {
                        xkb_keysym_t key_map_symbol = key_symbol;
                        if (!windowing->xkb_keymap && key_symbol_without_modifiers != XKB_KEY_NoSymbol)
                        {
                            key_map_symbol = key_symbol_without_modifiers;
                        }

                        switch (key_map_symbol)
                        {
                            break;
                        case XKB_KEY_Escape:
                            key = WM_KEY_ESC;

                            break;
                        case XKB_KEY_F1:
                            key = WM_KEY_F1;
                            break;
                        case XKB_KEY_F2:
                            key = WM_KEY_F2;
                            break;
                        case XKB_KEY_F3:
                            key = WM_KEY_F3;
                            break;
                        case XKB_KEY_F4:
                            key = WM_KEY_F4;
                            break;
                        case XKB_KEY_F5:
                            key = WM_KEY_F5;
                            break;
                        case XKB_KEY_F6:
                            key = WM_KEY_F6;
                            break;
                        case XKB_KEY_F7:
                            key = WM_KEY_F7;
                            break;
                        case XKB_KEY_F8:
                            key = WM_KEY_F8;
                            break;
                        case XKB_KEY_F9:
                            key = WM_KEY_F9;
                            break;
                        case XKB_KEY_F10:
                            key = WM_KEY_F10;
                            break;
                        case XKB_KEY_F11:
                            key = WM_KEY_F11;
                            break;
                        case XKB_KEY_F12:
                            key = WM_KEY_F12;
                            break;
                        case XKB_KEY_F13:
                            key = WM_KEY_F13;
                            break;
                        case XKB_KEY_F14:
                            key = WM_KEY_F14;
                            break;
                        case XKB_KEY_F15:
                            key = WM_KEY_F15;
                            break;
                        case XKB_KEY_F16:
                            key = WM_KEY_F16;
                            break;
                        case XKB_KEY_F17:
                            key = WM_KEY_F17;
                            break;
                        case XKB_KEY_F18:
                            key = WM_KEY_F18;
                            break;
                        case XKB_KEY_F19:
                            key = WM_KEY_F19;
                            break;
                        case XKB_KEY_F20:
                            key = WM_KEY_F20;
                            break;
                        case XKB_KEY_F21:
                            key = WM_KEY_F21;
                            break;
                        case XKB_KEY_F22:
                            key = WM_KEY_F22;
                            break;
                        case XKB_KEY_F23:
                            key = WM_KEY_F23;
                            break;
                        case XKB_KEY_F24:
                            key = WM_KEY_F24;
                            break;
                        case XKB_KEY_F25:
                            key = WM_KEY_F25;
                            break;
                        case XKB_KEY_F26:
                            key = WM_KEY_F26;
                            break;
                        case XKB_KEY_F27:
                            key = WM_KEY_F27;
                            break;
                        case XKB_KEY_F28:
                            key = WM_KEY_F28;
                            break;
                        case XKB_KEY_F29:
                            key = WM_KEY_F29;
                            break;
                        case XKB_KEY_F30:
                            key = WM_KEY_F30;
                            break;
                        case XKB_KEY_F31:
                            key = WM_KEY_F31;
                            break;
                        case XKB_KEY_F32:
                            key = WM_KEY_F32;
                            break;
                        case XKB_KEY_F33:
                            key = WM_KEY_F33;
                            break;
                        case XKB_KEY_F34:
                            key = WM_KEY_F34;
                            break;
                        case XKB_KEY_F35:
                            key = WM_KEY_F35;

                            break;
                        case XKB_KEY_minus:
                            key = WM_KEY_MINUS;
                            break;
                        case XKB_KEY_equal:
                            key = WM_KEY_EQUAL;
                            break;
                        case XKB_KEY_BackSpace:
                            key = WM_KEY_BACKSPACE;
                            break;
                        case XKB_KEY_Tab:
                            key = WM_KEY_TAB;
                            // break; case XKB_KEY_tick: key = WM_KEY_TICK;
                            break;
                        case XKB_KEY_asciitilde:
                            key = WM_KEY_TILDE;
                            break;
                        case XKB_KEY_grave:
                            key = WM_KEY_BACKTICK;
                            break;
                        case XKB_KEY_bracketleft:
                            key = WM_KEY_LEFT_BRACKET;
                            break;
                        case XKB_KEY_bracketright:
                            key = WM_KEY_RIGHT_BRACKET;
                            break;
                        case XKB_KEY_braceleft:
                            key = WM_KEY_LEFT_BRACE;
                            break;
                        case XKB_KEY_braceright:
                            key = WM_KEY_RIGHT_BRACE;
                            break;
                        case XKB_KEY_parenleft:
                            key = WM_KEY_LEFT_PARENTHESIS;
                            break;
                        case XKB_KEY_parenright:
                            key = WM_KEY_RIGHT_PARENTHESIS;
                            break;
                        case XKB_KEY_slash:
                            key = WM_KEY_FORWARD_SLASH;
                            break;
                        case XKB_KEY_backslash:
                            key = WM_KEY_BACKWARD_SLASH;
                            break;
                        case XKB_KEY_colon:
                            key = WM_KEY_COLON;
                            break;
                        case XKB_KEY_semicolon:
                            key = WM_KEY_SEMICOLON;
                            break;
                        case XKB_KEY_apostrophe:
                            key = WM_KEY_SINGLE_QUOTE;
                            break;
                        case XKB_KEY_quotedbl:
                            key = WM_KEY_DOUBLE_QUOTE;
                            break;
                        case XKB_KEY_Return:
                            key = WM_KEY_RETURN;
                            break;
                        case XKB_KEY_comma:
                            key = WM_KEY_COMMA;
                            break;
                        case XKB_KEY_period:
                            key = WM_KEY_DOT;
                            break;
                        case XKB_KEY_space:
                            key = WM_KEY_SPACE;
                            break;
                        case XKB_KEY_bar:
                            key = WM_KEY_BAR;
                            break;
                        case XKB_KEY_underscore:
                            key = WM_KEY_UNDERSCORE;
                            break;
                        case XKB_KEY_exclam:
                            key = WM_KEY_EXCLAMATION;
                            break;
                        case XKB_KEY_at:
                            key = WM_KEY_AT;
                            break;
                        case XKB_KEY_numbersign:
                            key = WM_KEY_HASH;
                            break;
                        case XKB_KEY_dollar:
                            key = WM_KEY_DOLLAR;
                            break;
                        case XKB_KEY_percent:
                            key = WM_KEY_PERCENTAGE;
                            break;
                        case XKB_KEY_asciicircum:
                            key = WM_KEY_CIRCUMFLEX;
                            break;
                        case XKB_KEY_ampersand:
                            key = WM_KEY_AMPERSAND;
                            break;
                        case XKB_KEY_asterisk:
                            key = WM_KEY_ASTERISK;
                            break;
                        case XKB_KEY_plus:
                            key = WM_KEY_PLUS;

                            break;
                        case XKB_KEY_Menu:
                            key = WM_KEY_MENU;
                            break;
                        case XKB_KEY_Caps_Lock:
                            key = WM_KEY_CAPS_LOCK;
                            break;
                        case XKB_KEY_Scroll_Lock:
                            key = WM_KEY_SCROLL_LOCK;
                            break;
                        case XKB_KEY_Insert:
                            key = WM_KEY_INSERT;
                            break;
                        case XKB_KEY_Pause:
                            key = WM_KEY_PAUSE;
                            break;
                        case XKB_KEY_Home:
                            key = WM_KEY_HOME;
                            break;
                        case XKB_KEY_End:
                            key = WM_KEY_END;
                            break;
                        case XKB_KEY_Page_Up:
                            key = WM_KEY_PAGE_UP;
                            break;
                        case XKB_KEY_Page_Down:
                            key = WM_KEY_PAGE_DOWN;
                            break;
                        case XKB_KEY_Delete:
                            key = WM_KEY_DELETE;

                            break;
                        case XKB_KEY_Up:
                            key = WM_KEY_UP;
                            break;
                        case XKB_KEY_Down:
                            key = WM_KEY_DOWN;
                            break;
                        case XKB_KEY_Left:
                            key = WM_KEY_LEFT;
                            break;
                        case XKB_KEY_Right:
                            key = WM_KEY_RIGHT;

                            break;
                        case XKB_KEY_Control_L:
                            key = WM_KEY_CONTROL;
                            break;
                        case XKB_KEY_Control_R:
                            key = WM_KEY_CONTROL;
                            break;
                        case XKB_KEY_Shift_L:
                            key = WM_KEY_SHIFT;
                            break;
                        case XKB_KEY_Shift_R:
                            key = WM_KEY_SHIFT;
                            break;
                        case XKB_KEY_Alt_L:
                            key = WM_KEY_ALT;
                            break;
                        case XKB_KEY_Alt_R:
                            key = WM_KEY_ALT;

                            break;
                        case XKB_KEY_Num_Lock:
                            key = WM_KEY_NUM_LOCK;
                            break;
                        case XKB_KEY_KP_Divide:
                            key = WM_KEY_NUM_SLASH;
                            break;
                        case XKB_KEY_KP_Multiply:
                            key = WM_KEY_NUM_STAR;
                            break;
                        case XKB_KEY_KP_Subtract:
                            key = WM_KEY_NUM_MINUS;
                            break;
                        case XKB_KEY_KP_Add:
                            key = WM_KEY_NUM_PLUS;
                            break;
                        case XKB_KEY_KP_Decimal:
                            key = WM_KEY_NUM_DOT;

                            break;
                        case XKB_KEY_KP_0:
                            key = WM_KEY_NUM_0;
                            break;
                        case XKB_KEY_KP_1:
                            key = WM_KEY_NUM_1;
                            break;
                        case XKB_KEY_KP_2:
                            key = WM_KEY_NUM_2;
                            break;
                        case XKB_KEY_KP_3:
                            key = WM_KEY_NUM_3;
                            break;
                        case XKB_KEY_KP_4:
                            key = WM_KEY_NUM_4;
                            break;
                        case XKB_KEY_KP_5:
                            key = WM_KEY_NUM_5;
                            break;
                        case XKB_KEY_KP_6:
                            key = WM_KEY_NUM_6;
                            break;
                        case XKB_KEY_KP_7:
                            key = WM_KEY_NUM_7;
                            break;
                        case XKB_KEY_KP_8:
                            key = WM_KEY_NUM_8;
                            break;
                        case XKB_KEY_KP_9:
                            key = WM_KEY_NUM_9;

                            break;
                        case XKB_KEY_0:
                            key = WM_KEY_0;
                            break;
                        case XKB_KEY_1:
                            key = WM_KEY_1;
                            break;
                        case XKB_KEY_2:
                            key = WM_KEY_2;
                            break;
                        case XKB_KEY_3:
                            key = WM_KEY_3;
                            break;
                        case XKB_KEY_4:
                            key = WM_KEY_4;
                            break;
                        case XKB_KEY_5:
                            key = WM_KEY_5;
                            break;
                        case XKB_KEY_6:
                            key = WM_KEY_6;
                            break;
                        case XKB_KEY_7:
                            key = WM_KEY_7;
                            break;
                        case XKB_KEY_8:
                            key = WM_KEY_8;
                            break;
                        case XKB_KEY_9:
                            key = WM_KEY_9;

                            break;
                        case XKB_KEY_A:
                        case XKB_KEY_a:
                            key = WM_KEY_A;
                            break;
                        case XKB_KEY_B:
                        case XKB_KEY_b:
                            key = WM_KEY_B;
                            break;
                        case XKB_KEY_C:
                        case XKB_KEY_c:
                            key = WM_KEY_C;
                            break;
                        case XKB_KEY_D:
                        case XKB_KEY_d:
                            key = WM_KEY_D;
                            break;
                        case XKB_KEY_E:
                        case XKB_KEY_e:
                            key = WM_KEY_E;
                            break;
                        case XKB_KEY_F:
                        case XKB_KEY_f:
                            key = WM_KEY_F;
                            break;
                        case XKB_KEY_G:
                        case XKB_KEY_g:
                            key = WM_KEY_G;
                            break;
                        case XKB_KEY_H:
                        case XKB_KEY_h:
                            key = WM_KEY_H;
                            break;
                        case XKB_KEY_I:
                        case XKB_KEY_i:
                            key = WM_KEY_I;
                            break;
                        case XKB_KEY_J:
                        case XKB_KEY_j:
                            key = WM_KEY_J;
                            break;
                        case XKB_KEY_K:
                        case XKB_KEY_k:
                            key = WM_KEY_K;
                            break;
                        case XKB_KEY_L:
                        case XKB_KEY_l:
                            key = WM_KEY_L;
                            break;
                        case XKB_KEY_M:
                        case XKB_KEY_m:
                            key = WM_KEY_M;
                            break;
                        case XKB_KEY_N:
                        case XKB_KEY_n:
                            key = WM_KEY_N;
                            break;
                        case XKB_KEY_O:
                        case XKB_KEY_o:
                            key = WM_KEY_O;
                            break;
                        case XKB_KEY_P:
                        case XKB_KEY_p:
                            key = WM_KEY_P;
                            break;
                        case XKB_KEY_Q:
                        case XKB_KEY_q:
                            key = WM_KEY_Q;
                            break;
                        case XKB_KEY_R:
                        case XKB_KEY_r:
                            key = WM_KEY_R;
                            break;
                        case XKB_KEY_S:
                        case XKB_KEY_s:
                            key = WM_KEY_S;
                            break;
                        case XKB_KEY_T:
                        case XKB_KEY_t:
                            key = WM_KEY_T;
                            break;
                        case XKB_KEY_U:
                        case XKB_KEY_u:
                            key = WM_KEY_U;
                            break;
                        case XKB_KEY_V:
                        case XKB_KEY_v:
                            key = WM_KEY_V;
                            break;
                        case XKB_KEY_W:
                        case XKB_KEY_w:
                            key = WM_KEY_W;
                            break;
                        case XKB_KEY_X:
                        case XKB_KEY_x:
                            key = WM_KEY_X;
                            break;
                        case XKB_KEY_Y:
                        case XKB_KEY_y:
                            key = WM_KEY_Y;
                            break;
                        case XKB_KEY_Z:
                        case XKB_KEY_z:
                            key = WM_KEY_Z;

                            break;
                        default:
                        {
                        }
                        }
                    }

                    wm_event_push(windowing, (WmEvent){
                                                 .kind = event_type == XCB_KEY_PRESS ? WM_EVENT_KEY_PRESS : WM_EVENT_KEY_RELEASE,
                                                 .window = window,
                                                 .modifiers = modifiers,
                                                 .key = key,
                                             });

                    // xcb_key_press_event_t(3)                                                                   XCB Events xcb_key_press_event_t(3)
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
                    //                  The type of this event, in this case XCB_KEY_RELEASE. This field is also present in the xcb_generic_event_t and can be
                    //                  used to tell events apart from each other.
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
                    //        event_x   If same_screen is true, this is the X coordinate relative to the event window's origin. Otherwise, event_x will be set
                    //        to zero.
                    //
                    //        event_y   If same_screen is true, this is the Y coordinate relative to the event window's origin. Otherwise, event_y will be set
                    //        to zero.
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
                    // X Version 11                                                                             libxcb 1.17.0 xcb_key_press_event_t(3)
                }
                break;
                case XCB_BUTTON_PRESS:   // 4
                case XCB_BUTTON_RELEASE: // 5
                {
                    xcb_button_press_event_t* button_event = (xcb_button_press_event_t*)event;
                    u8 modifiers = wm_x11_modifiers_from_state(button_event->state);

                    u8 button = button_event->detail;

                    WmKey key = 0;

                    switch (button)
                    {
                        break;
                    case 1:
                    {
                        key = WM_KEY_MOUSE_LEFT;
                    }
                    break;
                    case 2:
                    {
                        key = WM_KEY_MOUSE_MIDDLE;
                    }
                    break;
                    case 3:
                    {
                        key = WM_KEY_MOUSE_RIGHT;
                    }
                    break;
                    case 4:
                    {
                        key = WM_KEY_MOUSE_WHEEL_UP;
                    }
                    break;
                    case 5:
                    {
                        key = WM_KEY_MOUSE_WHEEL_DOWN;
                    }
                    break;
                    case 6:
                    {
                        key = WM_KEY_MOUSE_WHEEL_LEFT;
                    }
                    break;
                    case 7:
                    {
                        key = WM_KEY_MOUSE_WHEEL_RIGHT;
                    }
                    break;
                    case 8:
                    {
                        key = WM_KEY_MOUSE_BACK;
                    }
                    break;
                    case 9:
                    {
                        key = WM_KEY_MOUSE_FORWARD;
                    }
                    break;
                    case 10:
                    case 11:
                    {
                    }
                    break;
                    default:
                    {
                    }
                    }

                    if (key != WM_KEY_NULL)
                    {
                        WmWindowHandle* window = wm_x11_window_from_xcb(windowing, button_event->event);
                        wm_event_push(windowing, (WmEvent){
                                                     .kind = event_type == XCB_BUTTON_PRESS ? WM_EVENT_BUTTON_PRESS : WM_EVENT_BUTTON_RELEASE,
                                                     .window = window,
                                                     .key = key,
                                                     .modifiers = modifiers,
                                                     .position = (WmOffset){.x = (s16)button_event->event_x, .y = (s16)button_event->event_y},
                                                 });
                    }

                    // xcb_button_press_event_t(3)                                                                XCB Events xcb_button_press_event_t(3)
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
                    //                  The type of this event, in this case XCB_BUTTON_RELEASE. This field is also present in the xcb_generic_event_t and can
                    //                  be used to tell events apart from each other.
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
                    //        event_x   If same_screen is true, this is the X coordinate relative to the event window's origin. Otherwise, event_x will be set
                    //        to zero.
                    //
                    //        event_y   If same_screen is true, this is the Y coordinate relative to the event window's origin. Otherwise, event_y will be set
                    //        to zero.
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
                    // X Version 11                                                                             libxcb 1.17.0 xcb_button_press_event_t(3)

                    xcb_button_t k = button_event->detail;
                    BUSTER_UNUSED(k);
                }
                break;
                case XCB_MOTION_NOTIFY: // 6
                {
                    xcb_motion_notify_event_t* motion_notify_event = (xcb_motion_notify_event_t*)event;
                    wm_event_push(windowing, (WmEvent){
                                                 .kind = WM_EVENT_MOUSE_MOVE,
                                                 .window = wm_x11_window_from_xcb(windowing, motion_notify_event->event),
                                                 .position = (WmOffset){.x = (s16)motion_notify_event->event_x, .y = (s16)motion_notify_event->event_y},
                                             });
                    // xcb_motion_notify_event_t(3)                                                               XCB Events xcb_motion_notify_event_t(3)
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
                    //                  The type of this event, in this case XCB_MOTION_NOTIFY. This field is also present in the xcb_generic_event_t and can be
                    //                  used to tell events apart from each other.
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
                    //        event_x   If same_screen is true, this is the X coordinate relative to the event window's origin. Otherwise, event_x will be set
                    //        to zero.
                    //
                    //        event_y   If same_screen is true, this is the Y coordinate relative to the event window's origin. Otherwise, event_y will be set
                    //        to zero.
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
                    // X Version 11                                                                             libxcb 1.17.0 xcb_motion_notify_event_t(3)
                }
                break;
                case XCB_ENTER_NOTIFY: // 7
                case XCB_LEAVE_NOTIFY: // 8
                {
                    xcb_enter_notify_event_t* enter_leave_event = (xcb_enter_notify_event_t*)event;
                    BUSTER_UNUSED(enter_leave_event);

                    //                 xcb_enter_notify_event_t(3)                                                                XCB Events
                    //                 xcb_enter_notify_event_t(3)
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
                    //                  The type of this event, in this case XCB_LEAVE_NOTIFY. This field is also present in the xcb_generic_event_t and can be
                    //                  used to tell events apart from each other.
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
                    //        child     If the event window has subwindows and the final pointer position is in one of them, then child is set to that
                    //        subwindow, XCB_WINDOW_NONE otherwise.
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
                    // X Version 11                                                                             libxcb 1.17.0 xcb_enter_notify_event_t(3)
                }
                break;
                case XCB_FOCUS_IN:  // 9
                case XCB_FOCUS_OUT: // 10
                {
                    //                 xcb_focus_in_event_t(3)                                                                    XCB Events
                    //                 xcb_focus_in_event_t(3)
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
                    //                  The type of this event, in this case XCB_FOCUS_OUT. This field is also present in the xcb_generic_event_t and can be
                    //                  used to tell events apart from each other.
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
                    // X Version 11                                                                             libxcb 1.17.0 xcb_focus_in_event_t(3)

                    xcb_focus_in_event_t* focus_event = (xcb_focus_in_event_t*)event;
                    WmWindowHandle* focus_window = wm_x11_window_from_xcb(windowing, focus_event->event);
                    if (focus_window)
                    {
                        if (event_type == XCB_FOCUS_IN)
                        {
                            if (windowing->focused_window && windowing->focused_window != focus_window)
                            {
                                windowing->focused_window->focused = false;
                                if (windowing->xim && windowing->focused_window->ic)
                                {
                                    xcb_xim_unset_ic_focus(windowing->xim, windowing->focused_window->ic);
                                }
                            }

                            windowing->focused_window = focus_window;
                            focus_window->focused = true;
                            wm_xim_create_ic_for_window(focus_window);
                            if (windowing->xim && focus_window->ic)
                            {
                                xcb_xim_set_ic_focus(windowing->xim, focus_window->ic);
                            }
                            wm_event_push(windowing, (WmEvent){
                                                         .kind = WM_EVENT_WINDOW_FOCUS,
                                                         .window = focus_window,
                                                     });
                        }
                        else
                        {
                            focus_window->focused = false;
                            if (windowing->focused_window == focus_window)
                            {
                                windowing->focused_window = 0;
                            }
                            if (windowing->xim && focus_window->ic)
                            {
                                xcb_xim_unset_ic_focus(windowing->xim, focus_window->ic);
                            }
                            wm_event_push(windowing, (WmEvent){
                                                         .kind = WM_EVENT_WINDOW_UNFOCUS,
                                                         .window = focus_window,
                                                     });
                        }
                    }

                    u8 k = focus_event->detail;
                    BUSTER_UNUSED(k);
                }
                break;
                case XCB_KEYMAP_NOTIFY: // 11
                {
                    // xcb_keymap_notify_event_t(3)                                                               XCB Events xcb_keymap_notify_event_t(3)
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
                    //                  The type of this event, in this case XCB_KEYMAP_NOTIFY. This field is also present in the xcb_generic_event_t and can be
                    //                  used to tell events apart from each other.
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
                    // X Version 11                                                                             libxcb 1.17.0 xcb_keymap_notify_event_t(3)
                }
                break;
                case XCB_EXPOSE: // 12
                {
                    // This is the classical repaint event
                    xcb_expose_event_t* expose_event = (xcb_expose_event_t*)event;
                    BUSTER_UNUSED(expose_event);

                    //                 xcb_expose_event_t(3)                                                                      XCB Events
                    //                 xcb_expose_event_t(3)
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
                    //                  The type of this event, in this case XCB_EXPOSE. This field is also present in the xcb_generic_event_t and can be used
                    //                  to tell events apart from each other.
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
                    //        count     The  amount  of  Expose events following this one. Simple applications that do not want to optimize redisplay by
                    //        distinguishing between subareas of its window can just ignore
                    //                  all Expose events with nonzero counts and perform full redisplays on events with zero counts.
                    //
                    // DESCRIPTION
                    // SEE ALSO
                    //        xcb_generic_event_t(3)
                    //
                    // AUTHOR
                    //        Generated from xproto.xml. Contact xcb@lists.freedesktop.org for corrections and improvements.
                    //
                    // X Version 11                                                                             libxcb 1.17.0 xcb_expose_event_t(3)
                }
                break;
                case XCB_GRAPHICS_EXPOSURE: // 13
                {
                    xcb_graphics_exposure_event_t* graphics_exposure_event = (xcb_graphics_exposure_event_t*)event;
                    u8 k = graphics_exposure_event->response_type;
                    BUSTER_UNUSED(k);

                    //                 xcb_graphics_exposure_event_t(3)                                                           XCB Events
                    //                 xcb_graphics_exposure_event_t(3)
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
                    //                  The type of this event, in this case XCB_GRAPHICS_EXPOSURE. This field is also present in the xcb_generic_event_t and
                    //                  can be used to tell events apart from each other.
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
                    // X Version 11                                                                             libxcb 1.17.0 xcb_graphics_exposure_event_t(3)
                }
                break;
                case XCB_NO_EXPOSURE: // 14
                {
                    xcb_no_exposure_event_t* no_exposure_event = (xcb_no_exposure_event_t*)event;
                    u8 k = no_exposure_event->response_type;
                    BUSTER_UNUSED(k);

                    //                 xcb_no_exposure_event_t(3)                                                                 XCB Events
                    //                 xcb_no_exposure_event_t(3)
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
                    //                  The type of this event, in this case XCB_NO_EXPOSURE. This field is also present in the xcb_generic_event_t and can be
                    //                  used to tell events apart from each other.
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
                    // X Version 11                                                                             libxcb 1.17.0 xcb_no_exposure_event_t(3)
                }
                break;
                case XCB_VISIBILITY_NOTIFY: // 15
                {
                    xcb_visibility_notify_event_t* visibility_notify_event = (xcb_visibility_notify_event_t*)event;
                    BUSTER_UNUSED(visibility_notify_event);
                    //                 xcb_visibility_notify_event_t(3)                                                           XCB Events
                    //                 xcb_visibility_notify_event_t(3)
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
                    //                  The type of this event, in this case XCB_VISIBILITY_NOTIFY. This field is also present in the xcb_generic_event_t and
                    //                  can be used to tell events apart from each other.
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
                    // X Version 11                                                                             libxcb 1.17.0 xcb_visibility_notify_event_t(3)
                }
                break;
                case XCB_CREATE_NOTIFY: // 16
                {
                    xcb_create_notify_event_t* create_notify_event = (xcb_create_notify_event_t*)event;
                    //                 xcb_create_notify_event_t(3)                                                               XCB Events
                    //                 xcb_create_notify_event_t(3)
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
                    //                  The type of this event, in this case XCB_CREATE_NOTIFY. This field is also present in the xcb_generic_event_t and can be
                    //                  used to tell events apart from each other.
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
                    // X Version 11                                                                             libxcb 1.17.0 xcb_create_notify_event_t(3)

                    u8 k = create_notify_event->response_type;
                    BUSTER_UNUSED(k);
                }
                break;
                case XCB_DESTROY_NOTIFY: // 17
                {
                    xcb_destroy_notify_event_t* destroy_notify_event = (xcb_destroy_notify_event_t*)event;
                    bool source_destroyed = wm_x11_xdnd_source_destroyed(windowing->xdnd_source_destroy_observed, windowing->xdnd_source,
                                                                        destroy_notify_event->window);
                    if (source_destroyed ||
                        (windowing->xdnd_window && destroy_notify_event->window == windowing->xdnd_window->handle))
                    {
                        wm_x11_xdnd_reset(windowing);
                    }

                    //                 xcb_destroy_notify_event_t(3)                                                              XCB Events
                    //                 xcb_destroy_notify_event_t(3)
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
                    //                  The type of this event, in this case XCB_DESTROY_NOTIFY. This field is also present in the xcb_generic_event_t and can
                    //                  be used to tell events apart from each other.
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
                    // X Version 11                                                                             libxcb 1.17.0 xcb_destroy_notify_event_t(3)
                    u8 k = destroy_notify_event->response_type;

                    BUSTER_UNUSED(k);
                }
                break;
                case XCB_UNMAP_NOTIFY: // 18
                {
                    xcb_unmap_notify_event_t* unmap_notify_event = (xcb_unmap_notify_event_t*)event;
                    u8 k = unmap_notify_event->response_type;
                    BUSTER_UNUSED(k);

                    //                 xcb_unmap_notify_event_t(3)                                                                XCB Events
                    //                 xcb_unmap_notify_event_t(3)
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
                    //                  The type of this event, in this case XCB_UNMAP_NOTIFY. This field is also present in the xcb_generic_event_t and can be
                    //                  used to tell events apart from each other.
                    //
                    //        sequence  The sequence number of the last request processed by the X11 server.
                    //
                    //        event     The reconfigured window or its parent, depending on whether StructureNotify or SubstructureNotify was selected.
                    //
                    //        window    The window that was unmapped.
                    //
                    //        from_configure
                    //                  Set to 1 if the event was generated as a result of a resizing of the window's parent when window had a win_gravity of
                    //                  UnmapGravity.
                    //
                    // DESCRIPTION
                    // SEE ALSO
                    //        xcb_generic_event_t(3), xcb_unmap_window(3)
                    //
                    // AUTHOR
                    //        Generated from xproto.xml. Contact xcb@lists.freedesktop.org for corrections and improvements.
                    //
                    // X Version 11                                                                             libxcb 1.17.0 xcb_unmap_notify_event_t(3)
                }
                break;
                case XCB_MAP_NOTIFY: // 19
                {
                    xcb_map_notify_event_t* map_notify_event = (xcb_map_notify_event_t*)event;
                    u8 k = map_notify_event->response_type;
                    BUSTER_UNUSED(k);

                    //                 xcb_map_notify_event_t(3)                                                                  XCB Events
                    //                 xcb_map_notify_event_t(3)
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
                    //                  The type of this event, in this case XCB_MAP_NOTIFY. This field is also present in the xcb_generic_event_t and can be
                    //                  used to tell events apart from each other.
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
                    // X Version 11                                                                             libxcb 1.17.0 xcb_map_notify_event_t(3)
                }
                break;
                case XCB_MAP_REQUEST: // 20
                {
                    xcb_map_request_event_t* map_request_event = (xcb_map_request_event_t*)event;
                    u8 k = map_request_event->response_type;
                    BUSTER_UNUSED(k);

                    //                 xcb_map_request_event_t(3)                                                                 XCB Events
                    //                 xcb_map_request_event_t(3)
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
                    //                  The type of this event, in this case XCB_MAP_REQUEST. This field is also present in the xcb_generic_event_t and can be
                    //                  used to tell events apart from each other.
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
                    // X Version 11                                                                             libxcb 1.17.0 xcb_map_request_event_t(3)
                }
                break;
                case XCB_REPARENT_NOTIFY: // 21
                {
                    xcb_reparent_notify_event_t* reparent_notify_event = (xcb_reparent_notify_event_t*)event;
                    u8 k = reparent_notify_event->response_type;
                    BUSTER_UNUSED(k);

                    //                 xcb_reparent_notify_event_t(3)                                                             XCB Events
                    //                 xcb_reparent_notify_event_t(3)
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
                    //                  The type of this event, in this case XCB_REPARENT_NOTIFY. This field is also present in the xcb_generic_event_t and can
                    //                  be used to tell events apart from each other.
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
                    // X Version 11                                                                             libxcb 1.17.0 xcb_reparent_notify_event_t(3)
                }
                break;
                case XCB_CONFIGURE_NOTIFY: // 22
                {
                    xcb_configure_notify_event_t* configure_notify_event = (xcb_configure_notify_event_t*)event;
                    u16 width = configure_notify_event->width;
                    u16 height = configure_notify_event->height;

                    WmWindowHandle* window = wm_x11_window_from_xcb(windowing, configure_notify_event->window);
                    if (window)
                    {
                        wm_event_push(windowing, (WmEvent){
                                                     .kind = WM_EVENT_WINDOW_RESIZE,
                                                     .window = window,
                                                     .position = (WmOffset){.width = width, .height = height},
                                                 });
                    }

                    //                 xcb_configure_notify_event_t(3)                                                            XCB Events
                    //                 xcb_configure_notify_event_t(3)
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
                    //                  The type of this event, in this case XCB_CONFIGURE_NOTIFY. This field is also present in the xcb_generic_event_t and can
                    //                  be used to tell events apart from each other.
                    //
                    //        sequence  The sequence number of the last request processed by the X11 server.
                    //
                    //        event     The reconfigured window or its parent, depending on whether StructureNotify or SubstructureNotify was selected.
                    //
                    //        window    The window whose size, position, border, and/or stacking order was changed.
                    //
                    //        above_sibling
                    //                  If  XCB_NONE,  the window is on the bottom of the stack with respect to sibling windows. However, if set to a sibling
                    //                  window, the window is placed on top of this sibling win‐ dow.
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
                    // X Version 11                                                                             libxcb 1.17.0 xcb_configure_notify_event_t(3)
                }
                break;
                case XCB_CONFIGURE_REQUEST: // 23
                {
                    xcb_configure_request_event_t* configure_request_event = (xcb_configure_request_event_t*)event;

                    //                 xcb_configure_request_event_t(3)                                                           XCB Events
                    //                 xcb_configure_request_event_t(3)
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
                    //                  The type of this event, in this case XCB_CONFIGURE_REQUEST. This field is also present in the xcb_generic_event_t and
                    //                  can be used to tell events apart from each other.
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
                    // X Version 11                                                                             libxcb 1.17.0 xcb_configure_request_event_t(3)

                    u8 k = configure_request_event->response_type;
                    BUSTER_UNUSED(k);
                }
                break;
                case XCB_GRAVITY_NOTIFY: // 24
                {
                    xcb_gravity_notify_event_t* gravity_notify_event = (xcb_gravity_notify_event_t*)event;
                    u8 k = gravity_notify_event->response_type;
                    BUSTER_UNUSED(k);

                    // xcb_gravity_notify_event_t(3)                                                              XCB Events xcb_gravity_notify_event_t(3)
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
                    //                  The type of this event, in this case XCB_GRAVITY_NOTIFY. This field is also present in the xcb_generic_event_t and can
                    //                  be used to tell events apart from each other.
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
                    // X Version 11                                                                             libxcb 1.17.0 xcb_gravity_notify_event_t(3)
                }
                break;
                case XCB_RESIZE_REQUEST: // 25
                {
                    xcb_resize_request_event_t* resize_request_event = (xcb_resize_request_event_t*)event;
                    u8 k = resize_request_event->response_type;
                    BUSTER_UNUSED(k);

                    //                 xcb_resize_request_event_t(3)                                                              XCB Events
                    //                 xcb_resize_request_event_t(3)
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
                    //                  The type of this event, in this case XCB_RESIZE_REQUEST. This field is also present in the xcb_generic_event_t and can
                    //                  be used to tell events apart from each other.
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
                    // X Version 11                                                                             libxcb 1.17.0 xcb_resize_request_event_t(3)
                }
                break;
                case XCB_CIRCULATE_NOTIFY:  // 26
                case XCB_CIRCULATE_REQUEST: // 27
                {
                    xcb_circulate_notify_event_t* circulate_event = (xcb_circulate_notify_event_t*)event;
                    u8 k = circulate_event->response_type;
                    BUSTER_UNUSED(k);

                    //                 xcb_circulate_notify_event_t(3)                                                            XCB Events
                    //                 xcb_circulate_notify_event_t(3)
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
                    //                  The type of this event, in this case XCB_CIRCULATE_REQUEST. This field is also present in the xcb_generic_event_t and
                    //                  can be used to tell events apart from each other.
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
                    // X Version 11                                                                             libxcb 1.17.0 xcb_circulate_notify_event_t(3)
                }
                break;
                case XCB_PROPERTY_NOTIFY: // 28
                {
                    xcb_property_notify_event_t* property_notify_event = (xcb_property_notify_event_t*)event;
                    if (windowing->xdnd_active && windowing->xdnd_drop_pending && windowing->xdnd_incremental && windowing->xdnd_window &&
                        property_notify_event->window == windowing->xdnd_window->handle && property_notify_event->atom == windowing->xdnd_property &&
                        property_notify_event->state == XCB_PROPERTY_NEW_VALUE)
                    {
                        bool complete = false;
                        bool success = wm_x11_xdnd_read_increment(windowing, &complete);
                        if (!success)
                        {
                            wm_x11_xdnd_complete(windowing, false);
                        }
                        else if (complete)
                        {
                            wm_x11_xdnd_complete(windowing, true);
                        }
                    }

                    //                 xcb_property_notify_event_t(3)                                                             XCB Events
                    //                 xcb_property_notify_event_t(3)
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
                    //                  The type of this event, in this case XCB_PROPERTY_NOTIFY. This field is also present in the xcb_generic_event_t and can
                    //                  be used to tell events apart from each other.
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
                    // X Version 11                                                                             libxcb 1.17.0 xcb_property_notify_event_t(3)
                }
                break;
                case XCB_SELECTION_CLEAR: // 29
                {
                    xcb_selection_clear_event_t* selection_clear_event = (xcb_selection_clear_event_t*)event;
                    u8 k = selection_clear_event->response_type;
                    BUSTER_UNUSED(k);

                    //                 xcb_selection_clear_event_t(3)                                                             XCB Events
                    //                 xcb_selection_clear_event_t(3)
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
                    //                  The type of this event, in this case XCB_SELECTION_CLEAR. This field is also present in the xcb_generic_event_t and can
                    //                  be used to tell events apart from each other.
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
                    // X Version 11                                                                             libxcb 1.17.0 xcb_selection_clear_event_t(3)
                }
                break;
                case XCB_SELECTION_REQUEST: // 30
                {
                    xcb_selection_request_event_t* selection_request_event = (xcb_selection_request_event_t*)event;
                    u8 k = selection_request_event->response_type;
                    BUSTER_UNUSED(k);

                    //                 xcb_selection_request_event_t(3)                                                           XCB Events
                    //                 xcb_selection_request_event_t(3)
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
                    //                  The type of this event, in this case XCB_SELECTION_REQUEST. This field is also present in the xcb_generic_event_t and
                    //                  can be used to tell events apart from each other.
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
                    // X Version 11                                                                             libxcb 1.17.0 xcb_selection_request_event_t(3)
                }
                break;
                case XCB_SELECTION_NOTIFY: // 31
                {
                    xcb_selection_notify_event_t* selection_notify_event = (xcb_selection_notify_event_t*)event;
                    bool current = windowing->xdnd_active && windowing->xdnd_drop_pending && windowing->xdnd_window &&
                                   selection_notify_event->requestor == windowing->xdnd_window->handle &&
                                   selection_notify_event->selection == wm_x11_atom(X11_ATOM_XDND_SELECTION) &&
                                   selection_notify_event->target == wm_x11_atom(X11_ATOM_TEXT_URI_LIST);
                    if (current && selection_notify_event->property == XCB_ATOM_NONE)
                    {
                        wm_x11_xdnd_complete(windowing, false);
                    }
                    else if (current && selection_notify_event->property == windowing->xdnd_property)
                    {
                        bool success = wm_x11_xdnd_start_transfer(windowing);
                        if (!success)
                        {
                            wm_x11_xdnd_complete(windowing, false);
                        }
                        else if (!windowing->xdnd_incremental)
                        {
                            wm_x11_xdnd_complete(windowing, true);
                        }
                    }

                    //                 xcb_selection_notify_event_t(3)                                                            XCB Events
                    //                 xcb_selection_notify_event_t(3)
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
                    //                  The type of this event, in this case XCB_SELECTION_NOTIFY. This field is also present in the xcb_generic_event_t and can
                    //                  be used to tell events apart from each other.
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
                    // X Version 11                                                                             libxcb 1.17.0 xcb_selection_notify_event_t(3)
                }
                break;
                case XCB_COLORMAP_NOTIFY: // 32
                {
                    xcb_colormap_notify_event_t* colormap_notify_event = (xcb_colormap_notify_event_t*)event;
                    u8 k = colormap_notify_event->response_type;
                    BUSTER_UNUSED(k);

                    //                 xcb_colormap_notify_event_t(3)                                                             XCB Events
                    //                 xcb_colormap_notify_event_t(3)
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
                    //                  The type of this event, in this case XCB_COLORMAP_NOTIFY. This field is also present in the xcb_generic_event_t and can
                    //                  be used to tell events apart from each other.
                    //
                    //        sequence  The sequence number of the last request processed by the X11 server.
                    //
                    //        window    The window whose associated colormap is changed, installed or uninstalled.
                    //
                    //        colormap  The colormap which is changed, installed or uninstalled. This is XCB_NONE when the colormap is changed by a call to
                    //        FreeColormap.
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
                    // X Version 11                                                                             libxcb 1.17.0 xcb_colormap_notify_event_t(3)
                }
                break;
                case XCB_CLIENT_MESSAGE: // 33
                {
                    xcb_client_message_event_t* client_message_event = (xcb_client_message_event_t*)event;
                    xcb_atom_t message_type = client_message_event->type;
                    bool message_format_32 = client_message_event->format == 32;
                    if (message_format_32 && message_type == wm_x11_atom(X11_ATOM_WM_PROTOCOLS) &&
                        client_message_event->data.data32[0] == wm_x11_atom(X11_ATOM_WM_DELETE_WINDOW))
                    {
                        wm_event_push(windowing, (WmEvent){
                                                     .kind = WM_EVENT_WINDOW_CLOSE,
                                                     .window = wm_x11_window_from_xcb(windowing, client_message_event->window),
                                                 });
                    }
                    else if (message_format_32 && message_type == wm_x11_atom(X11_ATOM_XDND_ENTER))
                    {
                        WmWindowHandle* window = wm_x11_window_from_xcb(windowing, client_message_event->window);
                        u8 version = (u8)(client_message_event->data.data32[1] >> 24);
                        if (windowing->xdnd_active)
                        {
                            if (windowing->xdnd_drop_pending)
                            {
                                wm_x11_xdnd_complete(windowing, false);
                            }
                            else
                            {
                                wm_x11_xdnd_reset(windowing);
                            }
                        }
                        xcb_window_t source = client_message_event->data.data32[0];
                        if (window && source != XCB_WINDOW_NONE && wm_x11_xdnd_version_supported(version) && wm_x11_xdnd_watch_source(windowing, source))
                        {
                            windowing->xdnd_source = source;
                            windowing->xdnd_window = window;
                            windowing->xdnd_version = version;
                            windowing->xdnd_active = true;
                            windowing->xdnd_uri_list_supported = wm_x11_xdnd_source_supports_uri_list(windowing, client_message_event);
                        }
                    }
                    else if (message_format_32 && message_type == wm_x11_atom(X11_ATOM_XDND_POSITION))
                    {
                        WmWindowHandle* window = wm_x11_window_from_xcb(windowing, client_message_event->window);
                        bool current = window && windowing->xdnd_active &&
                                       wm_x11_xdnd_transaction_matches(windowing->xdnd_source, windowing->xdnd_window ? windowing->xdnd_window->handle : 0,
                                                                       client_message_event->data.data32[0], client_message_event->window);
                        if (current)
                        {
                            WmOffset position = {0};
                            bool position_valid = wm_x11_xdnd_position_from_event(windowing, window->handle, client_message_event->data.data32[2], &position);
                            windowing->xdnd_position = position;
                            windowing->xdnd_position_seen = position_valid;
                            windowing->xdnd_position_time = client_message_event->data.data32[3];
                            windowing->xdnd_accept = windowing->xdnd_uri_list_supported && position_valid;
                            wm_x11_xdnd_send_status(windowing, windowing->xdnd_accept);
                        }
                    }
                    else if (message_format_32 && message_type == wm_x11_atom(X11_ATOM_XDND_LEAVE))
                    {
                        bool current = windowing->xdnd_active &&
                                       wm_x11_xdnd_transaction_matches(windowing->xdnd_source, windowing->xdnd_window ? windowing->xdnd_window->handle : 0,
                                                                       client_message_event->data.data32[0], client_message_event->window);
                        if (current)
                        {
                            if (windowing->xdnd_drop_pending)
                            {
                                wm_x11_xdnd_complete(windowing, false);
                            }
                            else
                            {
                                wm_x11_xdnd_reset(windowing);
                            }
                        }
                    }
                    else if (message_format_32 && message_type == wm_x11_atom(X11_ATOM_XDND_DROP))
                    {
                        bool current = windowing->xdnd_active && windowing->xdnd_window &&
                                       wm_x11_xdnd_transaction_matches(windowing->xdnd_source, windowing->xdnd_window->handle,
                                                                       client_message_event->data.data32[0], client_message_event->window);
                        if (current && windowing->xdnd_accept && windowing->xdnd_position_seen && wm_x11_atom(X11_ATOM_XDND_SELECTION) != XCB_ATOM_NONE &&
                            wm_x11_atom(X11_ATOM_TEXT_URI_LIST) != XCB_ATOM_NONE && wm_x11_atom(X11_ATOM_XDND_SELECTION_PROPERTY) != XCB_ATOM_NONE)
                        {
                            windowing->xdnd_property = wm_x11_atom(X11_ATOM_XDND_SELECTION_PROPERTY);
                            xcb_timestamp_t selection_time = client_message_event->data.data32[2];
                            if (selection_time == XCB_CURRENT_TIME)
                            {
                                selection_time = windowing->xdnd_position_time;
                            }
                            xcb_convert_selection(connection, windowing->xdnd_window->handle, wm_x11_atom(X11_ATOM_XDND_SELECTION),
                                                  wm_x11_atom(X11_ATOM_TEXT_URI_LIST), windowing->xdnd_property, selection_time);
                            windowing->xdnd_drop_pending = true;
                            xcb_flush(connection);
                        }
                        else if (current)
                        {
                            wm_x11_xdnd_complete(windowing, false);
                        }
                    }
                    else if (message_format_32 && message_type == wm_x11_atom(X11_ATOM_XDND_STATUS))
                    {
                        // XdndStatus is normally sent by this target to the source. A
                        // status arriving at the target is stale and must not change
                        // the active transaction.
                    }
                    else if (message_format_32 && message_type == wm_x11_atom(X11_ATOM_XDND_FINISHED))
                    {
                        if (windowing->xdnd_active && windowing->xdnd_window && client_message_event->window == windowing->xdnd_source &&
                            client_message_event->data.data32[0] == windowing->xdnd_window->handle)
                        {
                            wm_x11_xdnd_reset(windowing);
                        }
                    }
                }
                break;
                case XCB_MAPPING_NOTIFY: // 32
                {
                    xcb_mapping_notify_event_t* mapping_notify_event = (xcb_mapping_notify_event_t*)event;
                    u8 k = mapping_notify_event->response_type;
                    BUSTER_UNUSED(k);
                    wm_x11_xkb_refresh_keymap(windowing);

                    //                 xcb_mapping_notify_event_t(3)                                                              XCB Events
                    //                 xcb_mapping_notify_event_t(3)
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
                    //                  The type of this event, in this case XCB_MAPPING_NOTIFY. This field is also present in the xcb_generic_event_t and can
                    //                  be used to tell events apart from each other.
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
                    // X Version 11                                                                             libxcb 1.17.0 xcb_mapping_notify_event_t(3)
                }
                break;
                default:
                {
                    string_print(S8("Unknown event type: {u8:x}\n"), event_type);
                }
                }
            }
        }

        free(event);
        wm_xim_create_ic(windowing);
    }
    windowing->poll_arena = 0;
    windowing->poll_event_list = 0;
}

BUSTER_GLOBAL_LOCAL WmHandle* wm_platform_initialize(void)
{
    WmHandle* result = {0};
    bool success = false;
    BUSTER_LSAN_DISABLE();
    int screen_id = 0;
    xcb_connection_t* connection = xcb_connect(0, &screen_id);
    BUSTER_LSAN_ENABLE();
    if (connection)
    {
        const xcb_setup_t* setup = xcb_get_setup(connection);
        if (setup)
        {
            windowing_handle = (WmHandle){
                .connection = connection,
                .setup = setup,
                .screen_id = screen_id,
                .key_symbols = xcb_key_symbols_alloc(connection),
                .xdnd_transfer_arena = arena_create((ArenaCreation){0}),
            };
            wm_x11_xdnd_reset(&windowing_handle);
            wm_x11_xkb_initialize(&windowing_handle);
            xcb_compound_text_init();
            windowing_handle.xim = xcb_xim_create(connection, screen_id, 0);
            if (windowing_handle.xim)
            {
                xcb_xim_im_callback cb = {0};
                cb.set_event_mask = wm_xim_set_event_mask_callback;
                cb.forward_event = wm_xim_forward_event_callback;
                cb.commit_string = wm_xim_commit_string_callback;
                xcb_xim_set_im_callback(windowing_handle.xim, &cb, &windowing_handle);
                xcb_xim_set_use_utf8_string(windowing_handle.xim, true);
                xcb_xim_set_use_compound_text(windowing_handle.xim, true);
                xcb_xim_open(windowing_handle.xim, &xcb_xim_open_callback_implementation, true, &windowing_handle);
            }
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
            success = true;
        }
    }
    if (success)
    {
        result = &windowing_handle;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL void wm_platform_deinitialize(WmHandle* windowing)
{
    if (windowing->connection)
    {
        wm_x11_xdnd_reset(windowing);
        if (windowing->xim)
        {
            SliceWmWindowHandle windows = get_windows(windowing);
            for (u64 i = 0; i < windows.length; i += 1)
            {
                WmWindowHandle* window = &windows.pointer[i];
                if (window->ic)
                {
                    xcb_xim_destroy_ic(windowing->xim, window->ic, 0, 0);
                    window->ic = 0;
                }
            }
            xcb_xim_close(windowing->xim);
            xcb_xim_destroy(windowing->xim);
            windowing->xim = 0;
        }
        if (windowing->key_symbols)
        {
            xcb_key_symbols_free(windowing->key_symbols);
            windowing->key_symbols = 0;
        }
        wm_x11_xkb_deinitialize(windowing);
        xcb_disconnect(windowing->connection);
        u64 atom_count = BUSTER_ARRAY_LENGTH(atom_names);
        for (u64 i = 0; i < atom_count; i += 1)
        {
            free(atom_replies[i]);
            atom_replies[i] = 0;
        }
        if (windowing->xdnd_transfer_arena)
        {
            arena_destroy(windowing->xdnd_transfer_arena, 1);
            windowing->xdnd_transfer_arena = 0;
        }
    }
}

WmWindowHandle* wm_window_create(WmHandle* windowing, WmWindowCreate create)
{
    WmWindowHandle* result = {0};
    xcb_connection_t* connection = windowing->connection;
    const xcb_setup_t* setup = windowing->setup;
    xcb_screen_t* screen = 0;
    int screen_index = 0;
    for (xcb_screen_iterator_t screen_iterator = xcb_setup_roots_iterator(setup); screen_iterator.rem; xcb_screen_next(&screen_iterator), screen_index += 1)
    {
        if (screen_index == windowing->screen_id)
        {
            screen = screen_iterator.data;
            break;
        }
    }
    if (screen)
    {
        xcb_window_t window_id = xcb_generate_id(connection);
        xcb_window_t parent_window = screen->root;
        u16 border_width = 10;
        xcb_create_window(connection, XCB_COPY_FROM_PARENT, window_id, parent_window, 0, 0, create.size.width, create.size.height, border_width,
                          XCB_WINDOW_CLASS_INPUT_OUTPUT, screen->root_visual, 0, 0);
        u32 event_mask = wm_x11_window_event_mask(windowing);
        xcb_change_window_attributes(connection, window_id, XCB_CW_EVENT_MASK, &event_mask);
        xcb_atom_t wm_protocols = wm_x11_atom(X11_ATOM_WM_PROTOCOLS);
        xcb_atom_t wm_delete_window = wm_x11_atom(X11_ATOM_WM_DELETE_WINDOW);
        if (wm_protocols && wm_delete_window)
        {
            xcb_change_property(connection, XCB_PROP_MODE_REPLACE, window_id, wm_protocols, XCB_ATOM_ATOM, 32, 1, &wm_delete_window);
        }
        BUSTER_CHECK(window_id != 0);
        result = arena_allocate(windowing->window_arena, WmWindowHandle, 1);
        *result = (WmWindowHandle){
            .handle = window_id,
            .owner = windowing,
        };
        wm_x11_window_set_metadata(result, create);
        xcb_map_window(connection, window_id);
        xcb_flush(connection);
        wm_x11_window_initialize_compose(result);
        wm_xim_create_ic_for_window(result);
    }
    else
    {
        string_print(S8("No screen found\n"));
    }
    return result;
}

WmRect wm_window_get_framebuffer_rect(WmHandle* windowing, WmWindowHandle* wm_window)
{
    WmRect result = {0};
    if (wm_window)
    {
        xcb_connection_t* connection = windowing->connection;
        xcb_get_geometry_cookie_t cookie = xcb_get_geometry(connection, wm_window->handle);
        xcb_get_geometry_reply_t* reply = xcb_get_geometry_reply(connection, cookie, 0);
        if (reply)
        {
            result.x0 = 0;
            result.y0 = 0;
            result.x1 = reply->width;
            result.y1 = reply->height;
            free(reply);
        }
    }
    return result;
}

f32 wm_window_get_dpi(WmHandle* windowing, WmWindowHandle* wm_window)
{
    f32 result = 96.0f;
    BUSTER_UNUSED(wm_window);
    if (windowing)
    {
        result = wm_x11_dpi_from_screen(windowing);
    }
    return result;
}

WmNativeSurface wm_window_get_native_surface(WmHandle* windowing, WmWindowHandle* window)
{
    WmNativeSurface result = {0};
    result.display = windowing->connection;
    result.window = (void*)(u64)window->handle;
    result.kind = WM_NATIVE_SURFACE_XCB;
    return result;
}

bool wm_window_is_visible(WmHandle* windowing)
{
    BUSTER_UNUSED(windowing);
    return true;
}
