#pragma once

#include <buster/lib/base.h>

typedef u16 WmUnit;

// WmOffset deliberately uses signed coordinate members while size and
// rectangle extent members remain unsigned.
typedef union WmOffset WmOffset;
union WmOffset
{
    struct
    {
        WmUnit width, height;
    };
    struct
    {
        s16 x, y;
    };
    WmUnit values[2];
};

typedef union WmRect WmRect;
union WmRect
{
    struct
    {
        WmOffset min, max;
    };
    struct
    {
        WmOffset p0, p1;
    };
    struct
    {
        WmUnit x0, y0, x1, y1;
    };
    struct
    {
        WmUnit left, top, right, bottom;
    };
    WmOffset v[2];
};

typedef struct WmHandle WmHandle;
typedef struct WmWindowHandle WmWindowHandle;

typedef enum WmNativeSurfaceKind
{
    WM_NATIVE_SURFACE_NONE,
    WM_NATIVE_SURFACE_XCB,
    WM_NATIVE_SURFACE_WIN32,
    WM_NATIVE_SURFACE_APPKIT,
    WM_NATIVE_SURFACE_METAL_LAYER,
    WM_NATIVE_SURFACE_ANDROID,
} WmNativeSurfaceKind;

typedef struct WmNativeSurface WmNativeSurface;
struct WmNativeSurface
{
    void* display;
    void* window;
    WmNativeSurfaceKind kind;
    u8 reserved[4];
};

typedef struct SliceWmWindowHandle SliceWmWindowHandle;
struct SliceWmWindowHandle
{
    WmWindowHandle* pointer;
    u64 length;
};

typedef enum WmEventKind
{
    WM_EVENT_WINDOW_CLOSE,
    WM_EVENT_WINDOW_RESIZE,
    WM_EVENT_WINDOW_FOCUS,
    WM_EVENT_WINDOW_UNFOCUS,
    WM_EVENT_MOUSE_MOVE,
    WM_EVENT_KEY_PRESS,
    WM_EVENT_KEY_RELEASE,
    WM_EVENT_BUTTON_PRESS,
    WM_EVENT_BUTTON_RELEASE,
    WM_EVENT_TEXT_INPUT,
    WM_EVENT_FILE_DROP,
    WM_EVENT_COUNT,
} WmEventKind;

typedef enum WmModifier
{
    WM_MODIFIER_CONTROL,
    WM_MODIFIER_SHIFT,
    WM_MODIFIER_ALT,
} WmModifier;

enum WmKey
{
    WM_KEY_NULL,

    WM_KEY_ESC,

    WM_KEY_F1,
    WM_KEY_F2,
    WM_KEY_F3,
    WM_KEY_F4,
    WM_KEY_F5,
    WM_KEY_F6,
    WM_KEY_F7,
    WM_KEY_F8,
    WM_KEY_F9,
    WM_KEY_F10,
    WM_KEY_F11,
    WM_KEY_F12,
    WM_KEY_F13,
    WM_KEY_F14,
    WM_KEY_F15,
    WM_KEY_F16,
    WM_KEY_F17,
    WM_KEY_F18,
    WM_KEY_F19,
    WM_KEY_F20,
    WM_KEY_F21,
    WM_KEY_F22,
    WM_KEY_F23,
    WM_KEY_F24,
    WM_KEY_F25,
    WM_KEY_F26,
    WM_KEY_F27,
    WM_KEY_F28,
    WM_KEY_F29,
    WM_KEY_F30,
    WM_KEY_F31,
    WM_KEY_F32,
    WM_KEY_F33,
    WM_KEY_F34,
    WM_KEY_F35,

    WM_KEY_MINUS,
    WM_KEY_EQUAL,
    WM_KEY_BACKSPACE,
    WM_KEY_TAB,
    WM_KEY_TICK,
    WM_KEY_BACKTICK,
    WM_KEY_TILDE,
    WM_KEY_LEFT_BRACKET,
    WM_KEY_RIGHT_BRACKET,
    WM_KEY_LEFT_BRACE,
    WM_KEY_RIGHT_BRACE,
    WM_KEY_LEFT_PARENTHESIS,
    WM_KEY_RIGHT_PARENTHESIS,
    WM_KEY_FORWARD_SLASH,
    WM_KEY_BACKWARD_SLASH,
    WM_KEY_COLON,
    WM_KEY_SEMICOLON,
    WM_KEY_SINGLE_QUOTE,
    WM_KEY_DOUBLE_QUOTE,
    WM_KEY_RETURN,
    WM_KEY_COMMA,
    WM_KEY_DOT,
    WM_KEY_SPACE,
    WM_KEY_BAR,
    WM_KEY_UNDERSCORE,
    WM_KEY_EXCLAMATION,
    WM_KEY_AT,
    WM_KEY_HASH,
    WM_KEY_DOLLAR,
    WM_KEY_PERCENTAGE,
    WM_KEY_CIRCUMFLEX,
    WM_KEY_AMPERSAND,
    WM_KEY_ASTERISK,
    WM_KEY_PLUS,

    WM_KEY_MENU,
    WM_KEY_CAPS_LOCK,
    WM_KEY_SCROLL_LOCK,
    WM_KEY_INSERT,
    WM_KEY_PAUSE,
    WM_KEY_HOME,
    WM_KEY_END,
    WM_KEY_PAGE_UP,
    WM_KEY_PAGE_DOWN,
    WM_KEY_DELETE,

    WM_KEY_UP,
    WM_KEY_DOWN,
    WM_KEY_LEFT,
    WM_KEY_RIGHT,

    WM_KEY_CONTROL,
    WM_KEY_SHIFT,
    WM_KEY_ALT,

    WM_KEY_NUM_LOCK,
    WM_KEY_NUM_SLASH,
    WM_KEY_NUM_STAR,
    WM_KEY_NUM_MINUS,
    WM_KEY_NUM_PLUS,
    WM_KEY_NUM_DOT,

    WM_KEY_NUM_0,
    WM_KEY_NUM_1,
    WM_KEY_NUM_2,
    WM_KEY_NUM_3,
    WM_KEY_NUM_4,
    WM_KEY_NUM_5,
    WM_KEY_NUM_6,
    WM_KEY_NUM_7,
    WM_KEY_NUM_8,
    WM_KEY_NUM_9,

    WM_KEY_0,
    WM_KEY_1,
    WM_KEY_2,
    WM_KEY_3,
    WM_KEY_4,
    WM_KEY_5,
    WM_KEY_6,
    WM_KEY_7,
    WM_KEY_8,
    WM_KEY_9,

    WM_KEY_A,
    WM_KEY_B,
    WM_KEY_C,
    WM_KEY_D,
    WM_KEY_E,
    WM_KEY_F,
    WM_KEY_G,
    WM_KEY_H,
    WM_KEY_I,
    WM_KEY_J,
    WM_KEY_K,
    WM_KEY_L,
    WM_KEY_M,
    WM_KEY_N,
    WM_KEY_O,
    WM_KEY_P,
    WM_KEY_Q,
    WM_KEY_R,
    WM_KEY_S,
    WM_KEY_T,
    WM_KEY_U,
    WM_KEY_V,
    WM_KEY_W,
    WM_KEY_X,
    WM_KEY_Y,
    WM_KEY_Z,

    WM_KEY_MOUSE_LEFT,
    WM_KEY_MOUSE_MIDDLE,
    WM_KEY_MOUSE_RIGHT,
    WM_KEY_MOUSE_WHEEL_UP,
    WM_KEY_MOUSE_WHEEL_DOWN,
    WM_KEY_MOUSE_WHEEL_LEFT,
    WM_KEY_MOUSE_WHEEL_RIGHT,
    WM_KEY_MOUSE_BACK,
    WM_KEY_MOUSE_FORWARD,
    WM_KEY_COUNT,
};

typedef u8 WmKey;
BUSTER_CT_CHECK(WM_KEY_COUNT <= UINT8_MAX);

typedef struct WmEvent WmEvent;
struct WmEvent
{
    WmEvent* previous;
    WmEvent* next;
    WmWindowHandle* window;
    WmEventKind kind;
    WmKey key;
    u8 modifiers;
    WmOffset position;
    String8 text;
    SliceString8 paths;
    u8 reserved[4];
};

typedef struct WmEventList WmEventList;
struct WmEventList
{
    WmEvent* first;
    WmEvent* last;
    u64 count;
};

BUSTER_CT_CHECK(sizeof(WmOffset) == sizeof(u32));

typedef void WmWindowRefresh(WmWindowHandle* window, void* context);

typedef struct WmWindowCreate WmWindowCreate;
struct WmWindowCreate
{
    String8 name;
    void* context;
    WmOffset size;
    u8 reserved[4];
};

// Native drag destinations use this small value type to pass ordered NSURL
// paths through the platform adapter without retaining native objects. The
// output of the corresponding test seam is copied into its caller-owned arena.
typedef struct WmAppleFileUrlPath WmAppleFileUrlPath;
struct WmAppleFileUrlPath
{
    String8 path;
    bool is_file_url;
};

typedef struct SliceWmAppleFileUrlPath SliceWmAppleFileUrlPath;
struct SliceWmAppleFileUrlPath
{
    WmAppleFileUrlPath* pointer;
    u64 length;
};

BUSTER_F_DECL WmHandle* wm_initialize(void);
BUSTER_F_DECL void wm_deinitialize(WmHandle* windowing);
BUSTER_F_DECL WmWindowHandle* wm_window_create(WmHandle* windowing, WmWindowCreate create);
BUSTER_F_DECL WmRect wm_window_get_framebuffer_rect(WmHandle* windowing, WmWindowHandle* wm_window);
BUSTER_F_DECL f32 wm_window_get_dpi(WmHandle* windowing, WmWindowHandle* wm_window);
BUSTER_F_DECL WmEventList wm_poll_events(Arena* arena, WmHandle* windowing);

// False while the app is backgrounded/locked (no usable native window).
BUSTER_F_DECL bool wm_window_is_visible(WmHandle* windowing);

BUSTER_F_DECL WmNativeSurface wm_window_get_native_surface(WmHandle* windowing, WmWindowHandle* window);

BUSTER_F_DECL WmOffset offset_from_rect(WmRect rect);

#if BUSTER_ANDROID
struct android_app;
extern struct android_app* buster_android_app;
#endif

#if BUSTER_IOS
// Runs UIApplicationMain with the buster app delegate (called from main()).
BUSTER_F_DECL void buster_ios_application_main(int argc, char* argv[]);
// Worker-thread trampoline (defined in entry_point.c) for the compiler/test runner.
BUSTER_F_DECL ProcessResult buster_ios_worker_entry(void);
#endif
