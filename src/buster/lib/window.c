#define BUSTER_WINDOW_IMPLEMENTATION 1
#include <buster/lib/window/internal.h>
#undef BUSTER_WINDOW_IMPLEMENTATION

BUSTER_GLOBAL_LOCAL WmHandle windowing_handle = {0};

BUSTER_GLOBAL_LOCAL WmEvent* wm_event_push(WmHandle* windowing, WmEvent event)
{
    WmEvent* result = arena_allocate(windowing->event_arena, WmEvent, 1);
    *result = event;

    if (windowing->event_list.last)
    {
        result->previous = windowing->event_list.last;
        windowing->event_list.last->next = result;
    }
    else
    {
        windowing->event_list.first = result;
    }

    windowing->event_list.last = result;
    windowing->event_list.count += 1;
    return result;
}

BUSTER_GLOBAL_LOCAL BUSTER_UNUSED_DECL SliceWmWindowHandle get_windows(WmHandle* handle)
{
    SliceWmWindowHandle result = {0};

    if (handle && handle->window_arena)
    {
        Arena* arena = handle->window_arena;
        u64 byte_count = arena->position - arena_minimum_position;
        u64 window_size = sizeof(WmWindowHandle);
        BUSTER_CHECK(byte_count % window_size == 0);
        result.pointer = (WmWindowHandle*)(arena + 1);
        result.length = byte_count / window_size;
    }

    return result;
}

#if BUSTER_LINUX
#include <buster/lib/window/xcb.c>
#elif defined(_WIN32)
#include <buster/lib/window/win32.c>
#elif defined(__APPLE__)
#include <buster/lib/window/apple.c>
#if BUSTER_IOS
#include <buster/lib/window/ios.c>
#endif
#elif BUSTER_ANDROID
#include <buster/lib/window/android.c>
#else
#include <buster/lib/window/null.c>
#endif

WmHandle* wm_initialize(void)
{
    WmHandle* result = wm_platform_initialize();
    if (result)
    {
        result->window_arena = arena_create((ArenaCreation){0});
    }
    return result;
}

void wm_deinitialize(WmHandle* windowing)
{
    wm_platform_deinitialize(windowing);
    if (windowing && windowing->window_arena)
    {
        arena_destroy(windowing->window_arena, 1);
        windowing->window_arena = 0;
    }
}

WmOffset offset_from_rect(WmRect rect)
{
    return (WmOffset){
        .width = rect.right - rect.left,
        .height = rect.bottom - rect.top,
    };
}

WmEventList wm_poll_events(Arena* arena, WmHandle* windowing)
{
    windowing->event_arena = arena;
    windowing->event_list = (WmEventList){0};

#if BUSTER_LINUX || defined(_WIN32) || defined(__APPLE__) || BUSTER_ANDROID
    wm_platform_poll_events(arena, windowing);
#endif

    return windowing->event_list;
}
