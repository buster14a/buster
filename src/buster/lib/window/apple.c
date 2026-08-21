#include <buster/lib/window/internal.h>

#if BUSTER_MACOS
// macOS tracks its NSWindows directly; iOS owns its single UIWindow elsewhere.
#define BUSTER_APPLE_MAX_WINDOW_COUNT (64)
BUSTER_GLOBAL_LOCAL id buster_apple_windows[BUSTER_APPLE_MAX_WINDOW_COUNT];
BUSTER_GLOBAL_LOCAL u32 buster_apple_window_count;
BUSTER_GLOBAL_LOCAL id buster_apple_run_loop_mode;
BUSTER_GLOBAL_LOCAL id buster_apple_file_url_type;
BUSTER_GLOBAL_LOCAL id buster_apple_legacy_file_url_type;

BUSTER_GLOBAL_LOCAL bool buster_apple_pasteboard_type_matches(id type, id expected)
{
    return type && expected && ((bool (*)(id, SEL, id))objc_msgSend)(type, buster_sel("isEqualToString:"), expected);
}

BUSTER_GLOBAL_LOCAL bool buster_apple_drag_has_file_type(id sender)
{
    bool result = false;
    id pasteboard = buster_msg_id(sender, "draggingPasteboard");
    id types = buster_msg_id(pasteboard, "types");
    if (types)
    {
        BusterNSUInteger type_count = buster_msg_ulong(types, "count");
        for (BusterNSUInteger type_index = 0; !result && type_index < type_count; type_index += 1)
        {
            id type = buster_msg_id_ulong(types, "objectAtIndex:", type_index);
            result = buster_apple_pasteboard_type_matches(type, buster_apple_file_url_type) ||
                     buster_apple_pasteboard_type_matches(type, buster_apple_legacy_file_url_type);
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL BusterCGPoint buster_apple_drag_content_point(id view, id sender, f64* height_out)
{
    BusterCGPoint result = {0};
    id window = buster_msg_id(view, "window");
    id destination_window = buster_msg_id(sender, "draggingDestinationWindow");
    if (destination_window)
    {
        window = destination_window;
    }
    if (window)
    {
        result = ((BusterCGPoint (*)(id, SEL))objc_msgSend)(sender, buster_sel("draggingLocation"));
        id content_view = buster_msg_id(window, "contentView");
        if (content_view)
        {
            result = ((BusterCGPoint (*)(id, SEL, BusterCGPoint, id))objc_msgSend)(content_view, buster_sel("convertPoint:fromView:"), result, 0);
            BusterCGRect bounds = ((BusterCGRect (*)(id, SEL))buster_msg_send_stret)(content_view, buster_sel("bounds"));
            *height_out = bounds.size.height;
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL String8 buster_apple_file_path_from_url(id url, bool* is_file_url_out)
{
    bool is_file_url = url && buster_msg_bool(url, "isFileURL");
    id path = is_file_url ? buster_msg_id(url, "path") : 0;
    const char* path_pointer = path ? ((const char* (*)(id, SEL))objc_msgSend)(path, buster_sel("UTF8String")) : 0;
    BusterNSUInteger path_length = path_pointer
                                       ? ((BusterNSUInteger (*)(id, SEL, BusterNSUInteger))objc_msgSend)(path, buster_sel("lengthOfBytesUsingEncoding:"), 4ul)
                                       : 0;
    if (is_file_url_out)
    {
        *is_file_url_out = is_file_url;
    }
    return string_from_pointer_length((char8*)path_pointer, (u64)path_length);
}

BUSTER_GLOBAL_LOCAL BusterNSUInteger buster_apple_dragging_entered(id self, SEL _cmd, id sender)
{
    BUSTER_UNUSED(self);
    BUSTER_UNUSED(_cmd);
    BusterNSUInteger source_mask = ((BusterNSUInteger (*)(id, SEL))objc_msgSend)(sender, buster_sel("draggingSourceOperationMask"));
    return buster_apple_drag_has_file_type(sender) && (source_mask & 1ul) ? 1ul : 0ul;
}

BUSTER_GLOBAL_LOCAL BusterNSUInteger buster_apple_dragging_updated(id self, SEL _cmd, id sender)
{
    return buster_apple_dragging_entered(self, _cmd, sender);
}

BUSTER_GLOBAL_LOCAL void buster_apple_dragging_exited(id self, SEL _cmd, id sender)
{
    BUSTER_UNUSED(self);
    BUSTER_UNUSED(_cmd);
    BUSTER_UNUSED(sender);
}

BUSTER_GLOBAL_LOCAL bool buster_apple_prepare_drag_operation(id self, SEL _cmd, id sender)
{
    BUSTER_UNUSED(self);
    BUSTER_UNUSED(_cmd);
    return buster_apple_drag_has_file_type(sender);
}

BUSTER_GLOBAL_LOCAL SliceString8 buster_apple_file_paths_from_drag(Arena* arena, id sender)
{
    SliceString8 result = {0};
    id pasteboard = buster_msg_id(sender, "draggingPasteboard");
    id url_class = (id)objc_getClass("NSURL");
    id classes[1] = {url_class};
    id class_array = ((id (*)(id, SEL, const id*, BusterNSUInteger))objc_msgSend)((id)objc_getClass("NSArray"), buster_sel("arrayWithObjects:count:"), classes, 1);
    id urls = ((id (*)(id, SEL, id, id))objc_msgSend)(pasteboard, buster_sel("readObjectsForClasses:options:"), class_array, 0);
    if (urls)
    {
        u64 url_count = (u64)buster_msg_ulong(urls, "count");
        // Every budget refusal leaves `result` empty, so a single gate carries
        // all three of them and the measuring pass stops as soon as one trips.
        bool within_budget =
            wm_native_file_drop_budget_allows(url_count, 0) && wm_native_file_drop_array_size_allowed(url_count, sizeof(WmAppleFileUrlPath));

        u64 output_bytes = 0;
        for (u64 url_index = 0; url_index < url_count && within_budget; url_index += 1)
        {
            id url = buster_msg_id_ulong(urls, "objectAtIndex:", (BusterNSUInteger)url_index);
            bool is_file_url = false;
            String8 path = buster_apple_file_path_from_url(url, &is_file_url);
            if (is_file_url && wm_file_path_is_valid(path))
            {
                within_budget = path.length <= BUSTER_NATIVE_FILE_DROP_MAX_PATH_BYTES - output_bytes;
                if (within_budget)
                {
                    output_bytes += path.length;
                }
            }
        }

        if (within_budget && wm_native_file_drop_budget_allows(url_count, output_bytes))
        {
            TemporalArena scratch = scratch_begin(0, 0);
            WmAppleFileUrlPath* values = url_count ? arena_allocate(scratch.arena, WmAppleFileUrlPath, url_count) : 0;
            for (u64 url_index = 0; url_index < url_count; url_index += 1)
            {
                id url = buster_msg_id_ulong(urls, "objectAtIndex:", (BusterNSUInteger)url_index);
                bool is_file_url = false;
                String8 path = buster_apple_file_path_from_url(url, &is_file_url);
                values[url_index] = (WmAppleFileUrlPath){
                    .path = path,
                    .is_file_url = is_file_url,
                };
            }
            result = wm_apple_file_paths_from_values(arena, (SliceWmAppleFileUrlPath){.pointer = values, .length = url_count});
            scratch_end(scratch);
        }
    }

    return result;
}

BUSTER_GLOBAL_LOCAL bool buster_apple_perform_drag_operation(id self, SEL _cmd, id sender)
{
    BUSTER_UNUSED(_cmd);
    bool result = false;
    if (buster_apple_drag_has_file_type(sender) && windowing_handle.event_arena)
    {
        SliceString8 paths = buster_apple_file_paths_from_drag(windowing_handle.event_arena, sender);
        if (paths.length != 0)
        {
            f64 content_height = 0;
            BusterCGPoint point = buster_apple_drag_content_point(self, sender, &content_height);
            id window = buster_msg_id(self, "window");
            wm_event_push(&windowing_handle, (WmEvent){
                                                   .kind = WM_EVENT_FILE_DROP,
                                                   .window = (WmWindowHandle*)window,
                                                   .position = wm_apple_drop_position_from_content_point(point.x, point.y, content_height),
                                                   .paths = paths,
                                               });
            result = true;
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL Class buster_apple_register_drag_view_class(void)
{
    Class existing = (Class)objc_getClass("BusterDragView");
    if (!existing)
    {
        Class view_class = objc_allocateClassPair((Class)objc_getClass("NSView"), "BusterDragView", 0);
        class_addMethod(view_class, buster_sel("draggingEntered:"), (IMP)buster_apple_dragging_entered, "L@:@");
        class_addMethod(view_class, buster_sel("draggingUpdated:"), (IMP)buster_apple_dragging_updated, "L@:@");
        class_addMethod(view_class, buster_sel("draggingExited:"), (IMP)buster_apple_dragging_exited, "v@:@");
        class_addMethod(view_class, buster_sel("prepareForDragOperation:"), (IMP)buster_apple_prepare_drag_operation, "B@:@");
        class_addMethod(view_class, buster_sel("performDragOperation:"), (IMP)buster_apple_perform_drag_operation, "B@:@");
        objc_registerClassPair(view_class);
        existing = view_class;
    }
    return existing;
}

BUSTER_GLOBAL_LOCAL void buster_apple_register_drag_types(id view)
{
    id types[2] = {buster_apple_file_url_type, buster_apple_legacy_file_url_type};
    id type_array = ((id (*)(id, SEL, const id*, BusterNSUInteger))objc_msgSend)((id)objc_getClass("NSArray"), buster_sel("arrayWithObjects:count:"), types, 2);
    buster_msg_void_id(view, "registerForDraggedTypes:", type_array);
}

BUSTER_GLOBAL_LOCAL id buster_apple_make_drag_view(id window)
{
    id old_view = buster_msg_id(window, "contentView");
    BusterCGRect bounds = ((BusterCGRect (*)(id, SEL))buster_msg_send_stret)(old_view, buster_sel("bounds"));
    Class view_class = buster_apple_register_drag_view_class();
    id view = buster_msg_id((id)view_class, "alloc");
    view = ((id (*)(id, SEL, BusterCGRect))objc_msgSend)(view, buster_sel("initWithFrame:"), bounds);
    if (view)
    {
        buster_msg_void_ulong(view, "setAutoresizingMask:", 18ul);
        buster_msg_void_id(window, "setContentView:", view);
        buster_apple_register_drag_types(view);
        buster_release(view);
    }
    return buster_msg_id(window, "contentView");
}

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
        buster_apple_file_url_type = buster_nsstring_from_cstring("public.file-url");
        buster_apple_legacy_file_url_type = buster_nsstring_from_cstring("NSURLPboardType");
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
    buster_release(buster_apple_file_url_type);
    buster_apple_file_url_type = 0;
    buster_release(buster_apple_legacy_file_url_type);
    buster_apple_legacy_file_url_type = 0;
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
        buster_apple_make_drag_view(window);
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
