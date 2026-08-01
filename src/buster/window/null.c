#define BUSTER_WINDOW_BACKEND_NULL 1
#include <buster/window/internal.h>

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
    result = arena_allocate(windowing->window_arena, WmWindowHandle, 1);
    *result = (WmWindowHandle){
        .owner = windowing,
        .size = create.size,
    };
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
    return result;
}
WmNativeSurface wm_window_get_native_surface(WmHandle* windowing, WmWindowHandle* window)
{
    WmNativeSurface result = {0};
    BUSTER_UNUSED(windowing);
    BUSTER_UNUSED(window);
    return result;
}
bool wm_window_is_visible(WmHandle* windowing)
{
    BUSTER_UNUSED(windowing);
    return true;
}
