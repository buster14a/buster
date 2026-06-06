#pragma once

#include <buster/base.h>

typedef struct WmHandle WmHandle;
typedef struct WmWindowHandle WmWindowHandle;

typedef enum WmEventKind
{
    WM_EVENT_WINDOW_CLOSE,
    WM_EVENT_COUNT,
} WmEventKind;

typedef struct WmEvent WmEvent;
struct WmEvent
{
    WmEvent* previous;
    WmEvent* next;
    WmWindowHandle* window;
    WmEventKind kind;
    u8 reserved[4];
};

typedef struct WmEventList WmEventList;
struct WmEventList
{
    WmEvent* first;
    WmEvent* last;
    u64 count;
};


typedef struct WmWindowSize WmWindowSize;
struct WmWindowSize
{
    u16 width;
    u16 height;
};

typedef void WmWindowRefresh(WmWindowHandle* window, void* context);

typedef struct WmWindowCreate WmWindowCreate;
struct WmWindowCreate
{
    String8 name;
    void* context;
    WmWindowRefresh* refresh_callback;
    WmWindowSize size;
    u8 reserved[4];
};

BUSTER_F_DECL WmHandle* wm_initialize(void);
BUSTER_F_DECL void wm_deinitialize(WmHandle* windowing);
BUSTER_F_DECL WmWindowHandle* wm_window_create(WmHandle* windowing, WmWindowCreate create);
BUSTER_F_DECL WmWindowSize wm_window_get_framebuffer_size(WmHandle* windowing, WmWindowHandle* wm_window);
BUSTER_F_DECL WmEventList wm_poll_events(Arena* arena, WmHandle* windowing);

BUSTER_F_DECL void* wm_handle_native_from_wm(WmHandle* windowing);
BUSTER_F_DECL void* wm_window_handle_native_from_wm(WmWindowHandle* window);
