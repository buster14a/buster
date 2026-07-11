#pragma once

#include <buster/window.h>

#if BUSTER_LINUX && !defined(BUSTER_WINDOW_BACKEND_NULL)
#include <locale.h>
#include <stdlib.h>
#include <string.h>
#include <xcb/xcb.h>
#include <xcb/xkb.h>
#include <xcb/xcb_keysyms.h>
#include <xcb-imdkit/imclient.h>
#include <xcb-imdkit/encoding.h>
#include <xkbcommon/xkbcommon-compose.h>
#include <xkbcommon/xkbcommon.h>
#include <xkbcommon/xkbcommon-x11.h>
#elif defined(__APPLE__)
#include <buster/window/apple_helpers.h>
#if BUSTER_IOS
#include <pthread.h>
#include <time.h>
#endif
#elif BUSTER_ANDROID
#include <android/native_window.h>
#include <android/log.h>
#include <android/input.h>
#include <android/configuration.h>
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wstrict-prototypes"
#endif
#include <android_native_app_glue.h>
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
#endif

#include <buster/string.h>
#include <buster/os.h>
#include <buster/arena.h>
#include <buster/file.h>
#include <buster/font_provider.h>
#include <buster/ui_core.h>
#include <buster/entry_point.h>

#if defined(_WIN32)
#include <buster/system_headers.h>
#include <dwmapi.h>
#endif

struct WmHandle
{
#if defined(BUSTER_WINDOW_BACKEND_NULL)
#elif BUSTER_LINUX
    xcb_connection_t* connection;
    const xcb_setup_t* setup;
    int screen_id;
    xcb_key_symbols_t* key_symbols;
    struct xkb_context* xkb_context;
    struct xkb_keymap* xkb_keymap;
    struct xkb_state* xkb_state;
    struct xkb_compose_table* xkb_compose_table;
    int32_t xkb_device_id;
    u8 xkb_base_event;
    xcb_xim_t* xim;
    bool xim_open;
    bool xim_input_styles_pending;
    bool xim_input_styles_ready;
    u32 xim_forward_event_mask;
    u32 xim_synchronous_event_mask;
    u32 xim_supported_input_style_count;
    u32 xim_supported_input_styles[32];
    Arena* poll_arena;
    WmEventList* poll_event_list;
    WmWindowHandle* focused_window;
#elif defined(_WIN32)
    HINSTANCE instance;
#elif defined(__APPLE__)
    id application;
#if BUSTER_IOS
    bool quit;
    bool window_lost;
#endif
#elif BUSTER_ANDROID
    struct android_app* app;
    bool quit;
    bool window_lost;
#endif
    Arena* window_arena;
    Arena* event_arena;
    WmEventList event_list;

    bool resizing;
};

struct WmWindowHandle
{
    WmHandle* owner;
#if defined(BUSTER_WINDOW_BACKEND_NULL)
    WmOffset size;
#elif BUSTER_LINUX
    struct xkb_compose_state* xkb_compose_state;
    xcb_window_t handle;
    xcb_xic_t ic;
    u32 xim_input_style_attempt_index;
    bool focused;
    bool xim_create_ic_pending;
#elif defined(_WIN32)
    HWND handle;
#elif defined(__APPLE__)
#if BUSTER_IOS
    id view;
    id metal_layer;
    WmOffset size;
#endif
#elif BUSTER_ANDROID
    struct ANativeWindow* native_window;
    WmOffset size;
#else
    WmOffset size;
#endif
    bool custom_border;
};

#if defined(BUSTER_WINDOW_IMPLEMENTATION)
#define BUSTER_WINDOW_INTERNAL_LINKAGE BUSTER_GLOBAL_LOCAL
#else
#define BUSTER_WINDOW_INTERNAL_LINKAGE extern
#endif

BUSTER_WINDOW_INTERNAL_LINKAGE WmHandle windowing_handle;
BUSTER_WINDOW_INTERNAL_LINKAGE WmEvent* wm_event_push(WmHandle* windowing, WmEvent event);
BUSTER_WINDOW_INTERNAL_LINKAGE SliceWmWindowHandle get_windows(WmHandle* handle);

#undef BUSTER_WINDOW_INTERNAL_LINKAGE
