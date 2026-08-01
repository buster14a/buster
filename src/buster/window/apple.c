#include <buster/window/internal.h>

#if BUSTER_MACOS
// macOS tracks its NSWindows directly; iOS owns its single UIWindow elsewhere.
#define BUSTER_APPLE_MAX_WINDOW_COUNT (64)
BUSTER_GLOBAL_LOCAL id buster_apple_windows[BUSTER_APPLE_MAX_WINDOW_COUNT];
BUSTER_GLOBAL_LOCAL u32 buster_apple_window_count;
BUSTER_GLOBAL_LOCAL id buster_apple_run_loop_mode;

BUSTER_GLOBAL_LOCAL void wm_platform_poll_events(Arena* arena, WmHandle* windowing)
{
    BUSTER_UNUSED(arena);
    id distant_past = buster_msg_id((id)objc_getClass("NSDate"), "distantPast");
    while (true)
    {
        BusterNSUInteger any_event_mask = ~0ul;
        id event = ((id (*)(id, SEL, BusterNSUInteger, id, id, bool))objc_msgSend)(windowing->application,
                                                                                   buster_sel("nextEventMatchingMask:untilDate:inMode:dequeue:"),
                                                                                   any_event_mask, distant_past, buster_apple_run_loop_mode, true);
        if (!event)
        {
            break;
        }

        buster_msg_void_id(windowing->application, "sendEvent:", event);
    }
    buster_msg_void(windowing->application, "updateWindows");

    for (u32 i = 0; i < buster_apple_window_count;)
    {
        id window = buster_apple_windows[i];
        if (!buster_msg_bool(window, "isVisible"))
        {
            wm_event_push(windowing, (WmEvent){
                                         .kind = WM_EVENT_WINDOW_CLOSE,
                                         .window = (WmWindowHandle*)window,
                                     });
            for (u32 j = i + 1; j < buster_apple_window_count; j += 1)
            {
                buster_apple_windows[j - 1] = buster_apple_windows[j];
            }
            buster_apple_window_count -= 1;
        }
        else
        {
            i += 1;
        }
    }
}

BUSTER_GLOBAL_LOCAL WmHandle* wm_platform_initialize(void)
{
    WmHandle* result = {0};
    bool success = false;
    id application_class = (id)objc_getClass("NSApplication");
    id application = buster_msg_id(application_class, "sharedApplication");
    if (application)
    {
        BusterNSInteger regular_activation_policy = 0;
        ((void (*)(id, SEL, BusterNSInteger))objc_msgSend)(application, buster_sel("setActivationPolicy:"), regular_activation_policy);
        buster_msg_void(application, "finishLaunching");
        windowing_handle.application = application;
        buster_apple_run_loop_mode = buster_nsstring_from_cstring("kCFRunLoopDefaultMode");
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

    for (u32 i = 0; i < buster_apple_window_count; i += 1)
    {
        buster_release(buster_apple_windows[i]);
        buster_apple_windows[i] = 0;
    }
    buster_apple_window_count = 0;
    buster_release(buster_apple_run_loop_mode);
    buster_apple_run_loop_mode = 0;
}

WmWindowHandle* wm_window_create(WmHandle* windowing, WmWindowCreate create)
{
    WmWindowHandle* result = {0};
    BUSTER_UNUSED(windowing);
    BUSTER_CHECK(buster_apple_window_count < BUSTER_APPLE_MAX_WINDOW_COUNT);
    id window_class = (id)objc_getClass("NSWindow");
    id window = buster_msg_id(window_class, "alloc");
    BusterCGRect content_rect = {
        .origin = {.x = 0, .y = 0},
        .size = {.width = create.size.width, .height = create.size.height},
    };
    BusterNSUInteger style_mask = (1ul << 0) | (1ul << 1) | (1ul << 2) | (1ul << 3);
    BusterNSUInteger backing_store_buffered = 2;
    bool defer_creation = false;
    window = ((id (*)(id, SEL, BusterCGRect, BusterNSUInteger, BusterNSUInteger, bool))objc_msgSend)(
        window, buster_sel("initWithContentRect:styleMask:backing:defer:"), content_rect, style_mask, backing_store_buffered, defer_creation);
    if (window)
    {
        id title = buster_nsstring_from_cstring((const char*)create.name.pointer);
        buster_msg_void_id(window, "setTitle:", title);
        buster_release(title);
        buster_msg_void_bool(window, "setReleasedWhenClosed:", false);
        buster_msg_void(window, "center");
        buster_msg_void_id(window, "makeKeyAndOrderFront:", 0);
        ((void (*)(id, SEL, bool))objc_msgSend)(windowing_handle.application, buster_sel("activateIgnoringOtherApps:"), true);
        buster_apple_windows[buster_apple_window_count] = window;
        buster_apple_window_count += 1;
        result = (WmWindowHandle*)window;
    }
    return result;
}

WmRect wm_window_get_framebuffer_rect(WmHandle* windowing, WmWindowHandle* wm_window)
{
    WmRect result = {0};
    if (wm_window)
    {
        BUSTER_UNUSED(windowing);
        id window = (id)wm_window;
        id content_view = buster_msg_id(window, "contentView");
        BusterCGRect bounds = ((BusterCGRect (*)(id, SEL))buster_msg_send_stret)(content_view, buster_sel("bounds"));
        BusterCGSize backing_size = ((BusterCGSize (*)(id, SEL, BusterCGSize))objc_msgSend)(content_view, buster_sel("convertSizeToBacking:"), bounds.size);
        result.x0 = 0;
        result.y0 = 0;
        result.x1 = (WmUnit)backing_size.width;
        result.y1 = (WmUnit)backing_size.height;
    }
    return result;
}

f32 wm_window_get_dpi(WmHandle* windowing, WmWindowHandle* wm_window)
{
    f32 result = 96.0f;
    BUSTER_UNUSED(windowing);
    BUSTER_UNUSED(wm_window);
    return result;
}

WmNativeSurface wm_window_get_native_surface(WmHandle* windowing, WmWindowHandle* window)
{
    WmNativeSurface result = {0};
    result.display = windowing->application;
    result.window = window;
    result.kind = WM_NATIVE_SURFACE_APPKIT;
    return result;
}

bool wm_window_is_visible(WmHandle* windowing)
{
    BUSTER_UNUSED(windowing);
    return true;
}
#endif
