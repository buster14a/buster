#include <buster/lib/window/internal.h>

struct android_app* buster_android_app = 0;

BUSTER_GLOBAL_LOCAL void buster_android_on_app_cmd(struct android_app* app, int32_t cmd)
{
    WmHandle* handle = (WmHandle*)app->userData;
    if (!handle)
    {
        return;
    }

    switch (cmd)
    {
    case APP_CMD_INIT_WINDOW:
        // A (new) native window is available; rendering can resume.
        handle->window_lost = false;
        break;
    case APP_CMD_TERM_WINDOW:
        // Backgrounded/locked/rotated: the native window is going away.
        // Stop rendering but keep running; do NOT treat this as a quit.
        handle->window_lost = true;
        break;
    case APP_CMD_DESTROY:
        handle->quit = true;
        break;
    default:
        break;
    }
}

// Translate Android touch into the mouse events the UI consumes. A tap is a
// mouse-move to the touch point plus a left button press/release.
BUSTER_GLOBAL_LOCAL int32_t buster_android_on_input_event(struct android_app* app, AInputEvent* event)
{
    WmHandle* windowing = (WmHandle*)app->userData;

    if (!windowing || AInputEvent_getType(event) != AINPUT_EVENT_TYPE_MOTION)
    {
        return 0;
    }

    int32_t action = AMotionEvent_getAction(event) & AMOTION_EVENT_ACTION_MASK;
    WmOffset position = {
        .x = (WmUnit)AMotionEvent_getX(event, 0),
        .y = (WmUnit)AMotionEvent_getY(event, 0),
    };

    switch (action)
    {
    case AMOTION_EVENT_ACTION_DOWN:
        wm_event_push(windowing, (WmEvent){.kind = WM_EVENT_MOUSE_MOVE, .position = position});
        wm_event_push(windowing, (WmEvent){.kind = WM_EVENT_BUTTON_PRESS, .key = WM_KEY_MOUSE_LEFT, .position = position});
        break;
    case AMOTION_EVENT_ACTION_MOVE:
        wm_event_push(windowing, (WmEvent){.kind = WM_EVENT_MOUSE_MOVE, .position = position});
        break;
    case AMOTION_EVENT_ACTION_UP:
    case AMOTION_EVENT_ACTION_CANCEL:
        wm_event_push(windowing, (WmEvent){.kind = WM_EVENT_MOUSE_MOVE, .position = position});
        wm_event_push(windowing, (WmEvent){.kind = WM_EVENT_BUTTON_RELEASE, .key = WM_KEY_MOUSE_LEFT, .position = position});
        break;
    default:
        break;
    }

    return 1;
}

BUSTER_GLOBAL_LOCAL void wm_platform_poll_events(Arena* arena, WmHandle* windowing)
{
    BUSTER_UNUSED(arena);
    {
        struct android_app* app = windowing->app;
        if (app)
        {
            // When backgrounded (no window) block until an event so we neither
            // busy-spin nor render to a dead surface; otherwise poll non-blocking.
            int timeout = ((windowing->window_lost || !app->window) && !windowing->quit) ? -1 : 0;
            int events;
            struct android_poll_source* source = 0;
            while (ALooper_pollOnce(timeout, 0, &events, (void**)&source) >= 0)
            {
                if (source)
                {
                    source->process(app, source);
                }
                if (app->destroyRequested)
                {
                    windowing->quit = true;
                    break;
                }
                timeout = 0;
            }

            // The native window changes across background/foreground cycles; keep
            // each window handle pointing at the current one.
            SliceWmWindowHandle windows = get_windows(windowing);
            for (u64 i = 0; i < windows.length; i += 1)
            {
                windows.pointer[i].native_window = app->window;
            }
        }

        if (windowing->quit)
        {
            SliceWmWindowHandle windows = get_windows(windowing);
            for (u64 i = 0; i < windows.length; i += 1)
            {
                wm_event_push(windowing, (WmEvent){
                                             .window = &windows.pointer[i],
                                             .kind = WM_EVENT_WINDOW_CLOSE,
                                         });
            }
        }
    }
}
BUSTER_GLOBAL_LOCAL WmHandle* wm_platform_initialize(void)
{
    WmHandle* result = {0};
    bool success = false;
    windowing_handle = (WmHandle){0};
    windowing_handle.app = buster_android_app;
    if (windowing_handle.app)
    {
        windowing_handle.app->userData = &windowing_handle;
        windowing_handle.app->onAppCmd = buster_android_on_app_cmd;
        windowing_handle.app->onInputEvent = buster_android_on_input_event;
        ANativeActivity_setWindowFlags(windowing_handle.app->activity, 0x00000400, 0);
        success = true;
    }
    if (success)
    {
        result = &windowing_handle;
    }
    return result;
}
BUSTER_GLOBAL_LOCAL void wm_platform_deinitialize(WmHandle* windowing)
{
    BUSTER_UNUSED(windowing);
}
WmWindowHandle* wm_window_create(WmHandle* windowing, WmWindowCreate create)
{
    WmWindowHandle* result = {0};
    BUSTER_UNUSED(create);
    struct android_app* app = windowing->app;
    while (app && !app->window && !windowing->quit)
    {
        int events;
        struct android_poll_source* source = 0;
        if (ALooper_pollOnce(-1, 0, &events, (void**)&source) >= 0)
        {
            if (source)
            {
                source->process(app, source);
            }
        }
    }
    if (app && app->window)
    {
        result = arena_allocate(windowing->window_arena, WmWindowHandle, 1);
        *result = (WmWindowHandle){
            .owner = windowing,
            .native_window = app->window,
            .size =
                {
                    .width = (WmUnit)ANativeWindow_getWidth(app->window),
                    .height = (WmUnit)ANativeWindow_getHeight(app->window),
                },
        };
    }
    return result;
}
WmRect wm_window_get_framebuffer_rect(WmHandle* windowing, WmWindowHandle* wm_window)
{
    WmRect result = {0};
    if (wm_window)
    {
        BUSTER_UNUSED(windowing);
        result.x0 = 0;
        result.y0 = 0;
        if (wm_window->native_window)
        {
            result.x1 = (WmUnit)ANativeWindow_getWidth(wm_window->native_window);
            result.y1 = (WmUnit)ANativeWindow_getHeight(wm_window->native_window);
        }
        else
        {
            result.x1 = wm_window->size.width;
            result.y1 = wm_window->size.height;
        }
    }
    return result;
}
f32 wm_window_get_dpi(WmHandle* windowing, WmWindowHandle* wm_window)
{
    f32 result = 96.0f;
    BUSTER_UNUSED(wm_window);
    if (windowing && windowing->app && windowing->app->config)
    {
        int32_t density = AConfiguration_getDensity(windowing->app->config);
        if (density > 0 && density != ACONFIGURATION_DENSITY_NONE && density != ACONFIGURATION_DENSITY_ANY)
        {
            result = (f32)density;
        }
    }
    return result;
}
WmNativeSurface wm_window_get_native_surface(WmHandle* windowing, WmWindowHandle* window)
{
    WmNativeSurface result = {0};
    BUSTER_UNUSED(windowing);
    result.window = window->native_window;
    result.kind = WM_NATIVE_SURFACE_ANDROID;
    return result;
}
bool wm_window_is_visible(WmHandle* windowing)
{
    return windowing && windowing->app && windowing->app->window != 0 && !windowing->window_lost && !windowing->quit;
}
