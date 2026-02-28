#pragma once

#include <buster/base.h>

typedef struct OsWindowingHandle OsWindowingHandle;
typedef struct OsWindowHandle OsWindowHandle;

STRUCT(OsWindowSize)
{
    u16 width;
    u16 height;
};

typedef void OsWindowRefresh(OsWindowHandle* window, void* context);

STRUCT(OsWindowCreate)
{
    StringOs name;
    void* context;
    OsWindowRefresh* refresh_callback;
    OsWindowSize size;
    u8 reserved[4];
};

BUSTER_DECL OsWindowingHandle* os_windowing_initialize();
BUSTER_DECL void os_windowing_deinitialize(OsWindowingHandle* windowing);
BUSTER_DECL OsWindowHandle* os_window_create(OsWindowingHandle* windowing, OsWindowCreate create);
BUSTER_DECL OsWindowSize os_window_get_framebuffer_size(OsWindowingHandle* windowing, OsWindowHandle* os_window);
BUSTER_IMPL OsWindowingEventList os_windowing_poll_events(Arena* arena, OsWindowingHandle* windowing);

BUSTER_DECL void* native_windowing_handle_from_os_windowing_handle(OsWindowingHandle* windowing);
BUSTER_DECL void* native_window_handle_from_os_window_handle(OsWindowHandle* window);
