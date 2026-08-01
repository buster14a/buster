#include <buster/window/internal.h>

// UIKit owns the main thread and run loop; the buster IDE loop runs on a worker
// thread (started in didFinishLaunching). UIWindow/UIView must be touched only
// on the main thread, so the delegate builds them there and publishes them here
// for the worker thread to wrap in wm_window_create (mirrors how Android waits
// for android_app->window).
BUSTER_GLOBAL_LOCAL id buster_ios_window = 0;
BUSTER_GLOBAL_LOCAL id buster_ios_view = 0;
BUSTER_GLOBAL_LOCAL id buster_ios_metal_layer = 0;
BUSTER_GLOBAL_LOCAL volatile bool buster_ios_window_ready = false;
BUSTER_GLOBAL_LOCAL volatile bool buster_ios_quit = false;
BUSTER_GLOBAL_LOCAL f32 buster_ios_scale = 1.0f;

// Touches are delivered on the main thread while events are drained on the
// worker thread, so the two are decoupled by this small lock-protected ring.
#define BUSTER_IOS_INPUT_QUEUE_CAPACITY (256)
BUSTER_GLOBAL_LOCAL WmEvent buster_ios_input_queue[BUSTER_IOS_INPUT_QUEUE_CAPACITY];
BUSTER_GLOBAL_LOCAL u64 buster_ios_input_head = 0;
BUSTER_GLOBAL_LOCAL u64 buster_ios_input_tail = 0;
BUSTER_GLOBAL_LOCAL pthread_mutex_t buster_ios_input_mutex = PTHREAD_MUTEX_INITIALIZER;

BUSTER_GLOBAL_LOCAL void buster_ios_input_push(WmEvent event)
{
    pthread_mutex_lock(&buster_ios_input_mutex);
    u64 next = (buster_ios_input_head + 1) % BUSTER_IOS_INPUT_QUEUE_CAPACITY;
    if (next != buster_ios_input_tail)
    {
        buster_ios_input_queue[buster_ios_input_head] = event;
        buster_ios_input_head = next;
    }
    pthread_mutex_unlock(&buster_ios_input_mutex);
}

BUSTER_GLOBAL_LOCAL bool buster_ios_input_pop(WmEvent* out_event)
{
    bool result = false;
    pthread_mutex_lock(&buster_ios_input_mutex);
    if (buster_ios_input_tail != buster_ios_input_head)
    {
        *out_event = buster_ios_input_queue[buster_ios_input_tail];
        buster_ios_input_tail = (buster_ios_input_tail + 1) % BUSTER_IOS_INPUT_QUEUE_CAPACITY;
        result = true;
    }
    pthread_mutex_unlock(&buster_ios_input_mutex);
    return result;
}

// A single-touch tap is mapped to the mouse events the UI already understands,
// matching the Android translation.
BUSTER_GLOBAL_LOCAL WmOffset buster_ios_touch_position(id self, id touches)
{
    id touch = buster_msg_id(touches, "anyObject");
    BusterCGPoint point = {.x = 0, .y = 0};
    if (touch)
    {
        point = ((BusterCGPoint (*)(id, SEL, id))objc_msgSend)(touch, buster_sel("locationInView:"), self);
    }
    return (WmOffset){
        .x = (WmUnit)((f32)point.x * buster_ios_scale),
        .y = (WmUnit)((f32)point.y * buster_ios_scale),
    };
}

BUSTER_GLOBAL_LOCAL void buster_ios_touches_began(id self, SEL _cmd, id touches, id event)
{
    BUSTER_UNUSED(_cmd);
    BUSTER_UNUSED(event);
    WmOffset position = buster_ios_touch_position(self, touches);
    buster_ios_input_push((WmEvent){.kind = WM_EVENT_MOUSE_MOVE, .position = position});
    buster_ios_input_push((WmEvent){.kind = WM_EVENT_BUTTON_PRESS, .key = WM_KEY_MOUSE_LEFT, .position = position});
}

BUSTER_GLOBAL_LOCAL void buster_ios_touches_moved(id self, SEL _cmd, id touches, id event)
{
    BUSTER_UNUSED(_cmd);
    BUSTER_UNUSED(event);
    WmOffset position = buster_ios_touch_position(self, touches);
    buster_ios_input_push((WmEvent){.kind = WM_EVENT_MOUSE_MOVE, .position = position});
}

BUSTER_GLOBAL_LOCAL void buster_ios_touches_ended(id self, SEL _cmd, id touches, id event)
{
    BUSTER_UNUSED(_cmd);
    BUSTER_UNUSED(event);
    WmOffset position = buster_ios_touch_position(self, touches);
    buster_ios_input_push((WmEvent){.kind = WM_EVENT_MOUSE_MOVE, .position = position});
    buster_ios_input_push((WmEvent){.kind = WM_EVENT_BUTTON_RELEASE, .key = WM_KEY_MOUSE_LEFT, .position = position});
}

// +[BusterMetalView layerClass] -> CAMetalLayer, so the view is backed directly
// by a Metal drawable layer (the iOS equivalent of attaching a CAMetalLayer to
// an NSView on macOS).
BUSTER_GLOBAL_LOCAL Class buster_ios_view_layer_class(id self, SEL _cmd)
{
    BUSTER_UNUSED(self);
    BUSTER_UNUSED(_cmd);
    return (Class)objc_getClass("CAMetalLayer");
}

BUSTER_GLOBAL_LOCAL Class buster_ios_register_view_class(void)
{
    Class existing = (Class)objc_getClass("BusterMetalView");
    if (existing)
    {
        return existing;
    }

    Class view_class = objc_allocateClassPair((Class)objc_getClass("UIView"), "BusterMetalView", 0);
    // +layerClass is a class method, so it lives on the metaclass.
    class_addMethod(object_getClass((id)view_class), buster_sel("layerClass"), (IMP)buster_ios_view_layer_class, "#@:");
    class_addMethod(view_class, buster_sel("touchesBegan:withEvent:"), (IMP)buster_ios_touches_began, "v@:@@");
    class_addMethod(view_class, buster_sel("touchesMoved:withEvent:"), (IMP)buster_ios_touches_moved, "v@:@@");
    class_addMethod(view_class, buster_sel("touchesEnded:withEvent:"), (IMP)buster_ios_touches_ended, "v@:@@");
    class_addMethod(view_class, buster_sel("touchesCancelled:withEvent:"), (IMP)buster_ios_touches_ended, "v@:@@");
    objc_registerClassPair(view_class);
    return view_class;
}

// pthread entry that runs the IDE loop (buster_ios_worker_entry calls exit()).
BUSTER_GLOBAL_LOCAL void* buster_ios_worker_thread(void* arg)
{
    BUSTER_UNUSED(arg);
    buster_ios_worker_entry();
    return 0;
}

// -[BusterAppDelegate application:didFinishLaunchingWithOptions:]: build the
// window/view on the main thread, publish them, then start the IDE worker.
BUSTER_GLOBAL_LOCAL bool buster_ios_did_finish_launching(id self, SEL _cmd, id application, id options)
{
    BUSTER_UNUSED(self);
    BUSTER_UNUSED(_cmd);
    BUSTER_UNUSED(application);
    BUSTER_UNUSED(options);

    id screen = buster_msg_id((id)objc_getClass("UIScreen"), "mainScreen");
    BusterCGRect bounds = ((BusterCGRect (*)(id, SEL))objc_msgSend)(screen, buster_sel("bounds"));
    buster_ios_scale = (f32)((BusterCGFloat (*)(id, SEL))objc_msgSend)(screen, buster_sel("nativeScale"));

    id window = buster_msg_id((id)objc_getClass("UIWindow"), "alloc");
    window = ((id (*)(id, SEL, BusterCGRect))objc_msgSend)(window, buster_sel("initWithFrame:"), bounds);

    Class view_class = buster_ios_register_view_class();
    id view = buster_msg_id((id)view_class, "alloc");
    view = ((id (*)(id, SEL, BusterCGRect))objc_msgSend)(view, buster_sel("initWithFrame:"), bounds);
    ((void (*)(id, SEL, BusterCGFloat))objc_msgSend)(view, buster_sel("setContentScaleFactor:"), (BusterCGFloat)buster_ios_scale);

    id view_controller = buster_msg_id(buster_msg_id((id)objc_getClass("UIViewController"), "alloc"), "init");
    buster_msg_void_id(view_controller, "setView:", view);
    buster_msg_void_id(window, "setRootViewController:", view_controller);
    buster_msg_void(window, "makeKeyAndVisible");

    id layer = buster_msg_id(view, "layer");

    buster_ios_window = window;
    buster_ios_view = view;
    buster_ios_metal_layer = layer;
    buster_ios_window_ready = true;

    pthread_t thread;
    if (pthread_create(&thread, 0, buster_ios_worker_thread, 0) == 0)
    {
        pthread_detach(thread);
    }

    return true;
}

BUSTER_GLOBAL_LOCAL Class buster_ios_register_delegate_class(void)
{
    Class existing = (Class)objc_getClass("BusterAppDelegate");
    if (existing)
    {
        return existing;
    }

    Class delegate_class = objc_allocateClassPair((Class)objc_getClass("UIResponder"), "BusterAppDelegate", 0);
    class_addProtocol(delegate_class, objc_getProtocol("UIApplicationDelegate"));
    class_addMethod(delegate_class, buster_sel("application:didFinishLaunchingWithOptions:"), (IMP)buster_ios_did_finish_launching, "B@:@@");
    objc_registerClassPair(delegate_class);
    return delegate_class;
}

extern int UIApplicationMain(int argc, char* argv[], id principal_class_name, id delegate_class_name);

void buster_ios_application_main(int argc, char* argv[])
{
    buster_ios_register_delegate_class();
    id delegate_name = buster_nsstring_from_cstring("BusterAppDelegate");
    UIApplicationMain(argc, argv, 0, delegate_name);
}

BUSTER_GLOBAL_LOCAL void wm_platform_poll_events(Arena* arena, WmHandle* windowing)
{
    BUSTER_UNUSED(arena);
    {
        // Touches are enqueued by the UIView on the main thread; drain them here
        // on the worker thread into the per-poll event list.
        WmEvent input_event;
        SliceWmWindowHandle windows = get_windows(windowing);
        while (buster_ios_input_pop(&input_event))
        {
            if (windows.length)
            {
                input_event.window = &windows.pointer[0];
            }
            wm_event_push(windowing, input_event);
        }

        if (buster_ios_quit)
        {
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
    success = true;
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
    while (!buster_ios_window_ready && !buster_ios_quit)
    {
        struct timespec sleep_time = {.tv_sec = 0, .tv_nsec = 1000000};
        nanosleep(&sleep_time, 0);
    }
    if (buster_ios_window_ready)
    {
        id layer = buster_ios_metal_layer;
        BusterCGRect bounds = ((BusterCGRect (*)(id, SEL))objc_msgSend)(buster_ios_view, buster_sel("bounds"));
        result = arena_allocate(windowing->window_arena, WmWindowHandle, 1);
        *result = (WmWindowHandle){
            .owner = windowing,
            .view = buster_ios_view,
            .metal_layer = layer,
            .size =
                {
                    .width = (WmUnit)((f32)bounds.size.width * buster_ios_scale),
                    .height = (WmUnit)((f32)bounds.size.height * buster_ios_scale),
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
        result.x1 = wm_window->size.width;
        result.y1 = wm_window->size.height;
    }
    return result;
}

f32 wm_window_get_dpi(WmHandle* windowing, WmWindowHandle* wm_window)
{
    f32 result = 96.0f;
    BUSTER_UNUSED(windowing);
    BUSTER_UNUSED(wm_window);
    result = 160.0f * buster_ios_scale;
    return result;
}

WmNativeSurface wm_window_get_native_surface(WmHandle* windowing, WmWindowHandle* window)
{
    WmNativeSurface result = {0};
    result.display = windowing->application;
    result.window = window->metal_layer;
    result.kind = WM_NATIVE_SURFACE_METAL_LAYER;
    return result;
}

bool wm_window_is_visible(WmHandle* windowing)
{
    BUSTER_UNUSED(windowing);
    return buster_ios_window_ready && !buster_ios_quit;
}
