#include <buster/lib/window/internal.h>

BUSTER_GLOBAL_LOCAL WmWindowHandle* wm_win32_window_from_win32(WmHandle* handle, HWND window)
{
    WmWindowHandle* result = 0;

    SliceWmWindowHandle windows = get_windows(handle);
    for (u64 i = 0; i < windows.length; i += 1)
    {
        WmWindowHandle* candidate = &windows.pointer[i];
        if (candidate->handle == window)
        {
            result = candidate;
            break;
        }
    }

    return result;
}
BUSTER_GLOBAL_LOCAL const wchar_t* graphical_window_class_name = L"graphical_window";
#ifndef WM_DPICHANGED
#define WM_DPICHANGED 0x02E0
#endif
#ifndef WM_MOUSEHWHEEL
#define WM_MOUSEHWHEEL 0x020E
#endif
#ifndef WM_XBUTTONDOWN
#define WM_XBUTTONDOWN 0x020B
#endif
#ifndef WM_XBUTTONUP
#define WM_XBUTTONUP 0x020C
#endif
#ifndef VK_OEM_102
#define VK_OEM_102 0xE2
#endif
typedef BOOL WINAPI WmWin32SetProcessDpiAwarenessContext(void* value);
typedef HRESULT WINAPI WmWin32SetProcessDpiAwareness(int value);
typedef UINT WINAPI WmWin32GetDpiForWindow(HWND hwnd);

// TinyCC's Windows headers do not provide shellapi.h. Keep these direct
// shell32 declarations here so the backend can use its minimal Windows
// headers without depending on that optional header.
extern void WINAPI DragAcceptFiles(HWND window, BOOL accept);
extern UINT WINAPI DragQueryFileW(void* drop_handle, UINT index, wchar_t* path, UINT path_length);
extern BOOL WINAPI DragQueryPoint(void* drop_handle, POINT* point);
extern void WINAPI DragFinish(void* drop_handle);

BUSTER_GLOBAL_LOCAL WmWin32GetDpiForWindow* wm_win32_get_dpi_for_window;
#define WM_WIN32_DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 ((void*)-4)

typedef union WmWin32Procedure
{
    FARPROC procedure;
    WmWin32SetProcessDpiAwarenessContext* set_process_dpi_awareness_context;
    WmWin32SetProcessDpiAwareness* set_process_dpi_awareness;
    WmWin32GetDpiForWindow* get_dpi_for_window;
} WmWin32Procedure;

BUSTER_GLOBAL_LOCAL void wm_win32_initialize_dpi_awareness(void)
{
    bool dpi_aware = false;
    HMODULE user32 = LoadLibraryA("user32.dll");
    if (user32)
    {
        WmWin32Procedure set_process_dpi_awareness_context_procedure = {.procedure = GetProcAddress(user32, "SetProcessDpiAwarenessContext")};
        WmWin32SetProcessDpiAwarenessContext* set_process_dpi_awareness_context = set_process_dpi_awareness_context_procedure.set_process_dpi_awareness_context;
        WmWin32Procedure get_dpi_for_window_procedure = {.procedure = GetProcAddress(user32, "GetDpiForWindow")};
        wm_win32_get_dpi_for_window = get_dpi_for_window_procedure.get_dpi_for_window;
        if (set_process_dpi_awareness_context)
        {
            dpi_aware = set_process_dpi_awareness_context(WM_WIN32_DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2) != 0;
        }
    }

    if (!dpi_aware)
    {
        HMODULE shcore = LoadLibraryA("shcore.dll");
        if (shcore)
        {
            WmWin32Procedure set_process_dpi_awareness_procedure = {.procedure = GetProcAddress(shcore, "SetProcessDpiAwareness")};
            WmWin32SetProcessDpiAwareness* set_process_dpi_awareness = set_process_dpi_awareness_procedure.set_process_dpi_awareness;
            if (set_process_dpi_awareness)
            {
                dpi_aware = set_process_dpi_awareness(2) >= 0;
            }
            FreeLibrary(shcore);
        }
    }

    if (!dpi_aware)
    {
        SetProcessDPIAware();
    }
}

BUSTER_GLOBAL_LOCAL WmOffset wm_win32_position_from_lparam(LPARAM lparam)
{
    uintptr_t packed = (uintptr_t)lparam;
    return (WmOffset){
        .x = (s16)(u16)(packed & 0xffffu),
        .y = (s16)(u16)((packed >> 16) & 0xffffu),
    };
}

BUSTER_GLOBAL_LOCAL WmOffset wm_win32_position_from_point(POINT point)
{
    return (WmOffset){
        .x = (s16)(u16)(u32)point.x,
        .y = (s16)(u16)(u32)point.y,
    };
}

BUSTER_GLOBAL_LOCAL WmOffset wm_win32_wheel_position(HWND window_handle, LPARAM lparam)
{
    WmOffset result = wm_win32_position_from_lparam(lparam);
    POINT point = {
        .x = (LONG)result.x,
        .y = (LONG)result.y,
    };
    if (ScreenToClient(window_handle, &point))
    {
        result = wm_win32_position_from_point(point);
    }
    return result;
}

BUSTER_GLOBAL_LOCAL u8 wm_win32_modifiers_from_keyboard(void)
{
    bool control = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    bool alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
    return (u8)(((u8)control << WM_MODIFIER_CONTROL) | ((u8)shift << WM_MODIFIER_SHIFT) | ((u8)alt << WM_MODIFIER_ALT));
}

BUSTER_GLOBAL_LOCAL u8 wm_win32_modifiers_from_mouse(WPARAM wparam)
{
    u32 state = (u32)wparam;
    bool control = (state & MK_CONTROL) != 0;
    bool shift = (state & MK_SHIFT) != 0;
    bool alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
    return (u8)(((u8)control << WM_MODIFIER_CONTROL) | ((u8)shift << WM_MODIFIER_SHIFT) | ((u8)alt << WM_MODIFIER_ALT));
}

BUSTER_GLOBAL_LOCAL WmKey wm_win32_mouse_key(UINT message, WPARAM wparam)
{
    WmKey result = WM_KEY_NULL;
    switch (message)
    {
    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP:
        result = WM_KEY_MOUSE_LEFT;
        break;
    case WM_MBUTTONDOWN:
    case WM_MBUTTONUP:
        result = WM_KEY_MOUSE_MIDDLE;
        break;
    case WM_RBUTTONDOWN:
    case WM_RBUTTONUP:
        result = WM_KEY_MOUSE_RIGHT;
        break;
    case WM_XBUTTONDOWN:
    case WM_XBUTTONUP:
    {
        u16 button = (u16)(((uintptr_t)wparam >> 16) & 0xffffu);
        if (button == 1)
        {
            result = WM_KEY_MOUSE_BACK;
        }
        else if (button == 2)
        {
            result = WM_KEY_MOUSE_FORWARD;
        }
    }
    break;
    default:
        break;
    }
    return result;
}

enum
{
    WM_WIN32_MOUSE_BUTTON_LEFT = 1u << 0,
    WM_WIN32_MOUSE_BUTTON_MIDDLE = 1u << 1,
    WM_WIN32_MOUSE_BUTTON_RIGHT = 1u << 2,
    WM_WIN32_MOUSE_BUTTON_BACK = 1u << 3,
    WM_WIN32_MOUSE_BUTTON_FORWARD = 1u << 4,
};

BUSTER_GLOBAL_LOCAL u8 wm_win32_mouse_button_mask(WmKey key)
{
    u8 result = 0;
    switch (key)
    {
    case WM_KEY_MOUSE_LEFT:
        result = WM_WIN32_MOUSE_BUTTON_LEFT;
        break;
    case WM_KEY_MOUSE_MIDDLE:
        result = WM_WIN32_MOUSE_BUTTON_MIDDLE;
        break;
    case WM_KEY_MOUSE_RIGHT:
        result = WM_WIN32_MOUSE_BUTTON_RIGHT;
        break;
    case WM_KEY_MOUSE_BACK:
        result = WM_WIN32_MOUSE_BUTTON_BACK;
        break;
    case WM_KEY_MOUSE_FORWARD:
        result = WM_WIN32_MOUSE_BUTTON_FORWARD;
        break;
    default:
        break;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL WmOffset wm_win32_cursor_position(HWND window_handle)
{
    POINT point = {0};
    if (!GetCursorPos(&point) || !ScreenToClient(window_handle, &point))
    {
        point = (POINT){0};
    }
    return wm_win32_position_from_point(point);
}

BUSTER_GLOBAL_LOCAL void wm_win32_push_mouse_releases(WmHandle* windowing, WmWindowHandle* window, HWND window_handle)
{
    if (window && window->held_mouse_buttons)
    {
        static const u8 button_masks[] = {
            WM_WIN32_MOUSE_BUTTON_LEFT,
            WM_WIN32_MOUSE_BUTTON_MIDDLE,
            WM_WIN32_MOUSE_BUTTON_RIGHT,
            WM_WIN32_MOUSE_BUTTON_BACK,
            WM_WIN32_MOUSE_BUTTON_FORWARD,
        };
        static const WmKey button_keys[] = {
            WM_KEY_MOUSE_LEFT,
            WM_KEY_MOUSE_MIDDLE,
            WM_KEY_MOUSE_RIGHT,
            WM_KEY_MOUSE_BACK,
            WM_KEY_MOUSE_FORWARD,
        };
        u8 held_mouse_buttons = window->held_mouse_buttons;
        WmOffset position = wm_win32_cursor_position(window_handle);
        window->held_mouse_buttons = 0;

        for (u64 i = 0; i < BUSTER_ARRAY_LENGTH(button_masks); i += 1)
        {
            if (held_mouse_buttons & button_masks[i])
            {
                wm_event_push(windowing, (WmEvent){
                                                     .kind = WM_EVENT_BUTTON_RELEASE,
                                                     .window = window,
                                                     .key = button_keys[i],
                                                     .modifiers = wm_win32_modifiers_from_mouse(0),
                                                     .position = position,
                                                 });
            }
        }
    }
}

BUSTER_GLOBAL_LOCAL WmKey wm_win32_keyboard_key(WPARAM wparam)
{
    UINT virtual_key = (UINT)wparam;
    WmKey result = WM_KEY_NULL;

    if (virtual_key >= '0' && virtual_key <= '9')
    {
        result = (WmKey)(WM_KEY_0 + (virtual_key - '0'));
    }
    else if (virtual_key >= 'A' && virtual_key <= 'Z')
    {
        result = (WmKey)(WM_KEY_A + (virtual_key - 'A'));
    }
    else if (virtual_key >= VK_F1 && virtual_key <= VK_F24)
    {
        result = (WmKey)(WM_KEY_F1 + (virtual_key - VK_F1));
    }
    else
    {
        switch (virtual_key)
        {
        case VK_ESCAPE:
            result = WM_KEY_ESC;
            break;
        case VK_OEM_MINUS:
            result = WM_KEY_MINUS;
            break;
        case VK_OEM_PLUS:
            result = WM_KEY_EQUAL;
            break;
        case VK_BACK:
            result = WM_KEY_BACKSPACE;
            break;
        case VK_TAB:
            result = WM_KEY_TAB;
            break;
        case VK_OEM_3:
            result = WM_KEY_BACKTICK;
            break;
        case VK_OEM_4:
            result = WM_KEY_LEFT_BRACKET;
            break;
        case VK_OEM_6:
            result = WM_KEY_RIGHT_BRACKET;
            break;
        case VK_OEM_5:
        case VK_OEM_102:
            result = WM_KEY_BACKWARD_SLASH;
            break;
        case VK_OEM_1:
            result = WM_KEY_SEMICOLON;
            break;
        case VK_OEM_7:
            result = WM_KEY_SINGLE_QUOTE;
            break;
        case VK_RETURN:
            result = WM_KEY_RETURN;
            break;
        case VK_OEM_COMMA:
            result = WM_KEY_COMMA;
            break;
        case VK_OEM_PERIOD:
            result = WM_KEY_DOT;
            break;
        case VK_SPACE:
            result = WM_KEY_SPACE;
            break;
        case VK_OEM_2:
            result = WM_KEY_FORWARD_SLASH;
            break;
        case VK_APPS:
            result = WM_KEY_MENU;
            break;
        case VK_CAPITAL:
            result = WM_KEY_CAPS_LOCK;
            break;
        case VK_SCROLL:
            result = WM_KEY_SCROLL_LOCK;
            break;
        case VK_INSERT:
            result = WM_KEY_INSERT;
            break;
        case VK_PAUSE:
            result = WM_KEY_PAUSE;
            break;
        case VK_HOME:
            result = WM_KEY_HOME;
            break;
        case VK_END:
            result = WM_KEY_END;
            break;
        case VK_PRIOR:
            result = WM_KEY_PAGE_UP;
            break;
        case VK_NEXT:
            result = WM_KEY_PAGE_DOWN;
            break;
        case VK_DELETE:
            result = WM_KEY_DELETE;
            break;
        case VK_UP:
            result = WM_KEY_UP;
            break;
        case VK_DOWN:
            result = WM_KEY_DOWN;
            break;
        case VK_LEFT:
            result = WM_KEY_LEFT;
            break;
        case VK_RIGHT:
            result = WM_KEY_RIGHT;
            break;
        case VK_LCONTROL:
        case VK_RCONTROL:
        case VK_CONTROL:
            result = WM_KEY_CONTROL;
            break;
        case VK_LSHIFT:
        case VK_RSHIFT:
        case VK_SHIFT:
            result = WM_KEY_SHIFT;
            break;
        case VK_LMENU:
        case VK_RMENU:
        case VK_MENU:
            result = WM_KEY_ALT;
            break;
        case VK_NUMLOCK:
            result = WM_KEY_NUM_LOCK;
            break;
        case VK_DIVIDE:
            result = WM_KEY_NUM_SLASH;
            break;
        case VK_MULTIPLY:
            result = WM_KEY_NUM_STAR;
            break;
        case VK_SUBTRACT:
            result = WM_KEY_NUM_MINUS;
            break;
        case VK_ADD:
            result = WM_KEY_NUM_PLUS;
            break;
        case VK_DECIMAL:
            result = WM_KEY_NUM_DOT;
            break;
        case VK_NUMPAD0:
        case VK_NUMPAD1:
        case VK_NUMPAD2:
        case VK_NUMPAD3:
        case VK_NUMPAD4:
        case VK_NUMPAD5:
        case VK_NUMPAD6:
        case VK_NUMPAD7:
        case VK_NUMPAD8:
        case VK_NUMPAD9:
            result = (WmKey)(WM_KEY_NUM_0 + (virtual_key - VK_NUMPAD0));
            break;
        default:
            break;
        }
    }

    return result;
}

BUSTER_GLOBAL_LOCAL void wm_win32_push_text_units(WmHandle* windowing, WmWindowHandle* window, const char16* units, u64 unit_count)
{
    if (window && unit_count != 0)
    {
        String8 text = string8_from_string16(windowing->event_arena, (String16){.pointer = (char16*)units, .length = unit_count}, false);
        wm_event_push(windowing, (WmEvent){
                                         .kind = WM_EVENT_TEXT_INPUT,
                                         .window = window,
                                         .text = text,
                                     });
    }
}

BUSTER_GLOBAL_LOCAL void wm_win32_push_text_replacement(WmHandle* windowing, WmWindowHandle* window)
{
    char16 replacement = (char16)0xfffdu;
    wm_win32_push_text_units(windowing, window, &replacement, 1);
}

BUSTER_GLOBAL_LOCAL void wm_win32_push_text_code_unit(WmHandle* windowing, WmWindowHandle* window, char16 code_unit)
{
    if (!window)
    {
        return;
    }

    if (code_unit >= (char16)0xd800u && code_unit <= (char16)0xdbffu)
    {
        if (window->pending_high_surrogate)
        {
            wm_win32_push_text_replacement(windowing, window);
        }
        window->pending_high_surrogate = code_unit;
    }
    else if (code_unit >= (char16)0xdc00u && code_unit <= (char16)0xdfffu)
    {
        if (window->pending_high_surrogate)
        {
            char16 units[] = {window->pending_high_surrogate, code_unit};
            window->pending_high_surrogate = 0;
            wm_win32_push_text_units(windowing, window, units, BUSTER_ARRAY_LENGTH(units));
        }
        else
        {
            wm_win32_push_text_replacement(windowing, window);
        }
    }
    else
    {
        if (window->pending_high_surrogate)
        {
            wm_win32_push_text_replacement(windowing, window);
            window->pending_high_surrogate = 0;
        }
        wm_win32_push_text_units(windowing, window, &code_unit, 1);
    }
}

BUSTER_GLOBAL_LOCAL void wm_win32_flush_pending_text(WmHandle* windowing, WmWindowHandle* window)
{
    if (window && window->pending_high_surrogate)
    {
        wm_win32_push_text_replacement(windowing, window);
        window->pending_high_surrogate = 0;
    }
}

BUSTER_GLOBAL_LOCAL void wm_win32_push_file_drop(WmHandle* windowing, WmWindowHandle* window, WPARAM wparam)
{
    TemporalArena scratch = scratch_begin(0, 0);
    void* drop_handle = (void*)(uintptr_t)wparam;
    UINT path_count = DragQueryFileW(drop_handle, ~(UINT)0, 0, 0);
    String8* path_values = path_count ? arena_allocate(windowing->event_arena, String8, path_count) : 0;

    for (UINT path_index = 0; path_index < path_count; path_index += 1)
    {
        UINT path_length = DragQueryFileW(drop_handle, path_index, 0, 0);
        char16* path = arena_allocate(scratch.arena, char16, (u64)path_length + 1);
        UINT copied_length = DragQueryFileW(drop_handle, path_index, (wchar_t*)path, path_length + 1);
        path_values[path_index] = string8_from_string16(windowing->event_arena, (String16){.pointer = path, .length = copied_length}, false);
    }

    POINT point = {0};
    DragQueryPoint(drop_handle, &point);

    DragFinish(drop_handle);
    if (window)
    {
        wm_event_push(windowing, (WmEvent){
                                         .kind = WM_EVENT_FILE_DROP,
                                         .window = window,
                                         .position = wm_win32_position_from_point(point),
                                         .paths = (SliceString8){.pointer = path_values, .length = path_count},
                                     });
    }
    scratch_end(scratch);
}

#if BUSTER_INCLUDE_TESTS
BUSTER_TEST_F_DECL WmKey wm_win32_keyboard_key_for_test(u32 virtual_key)
{
    return wm_win32_keyboard_key((WPARAM)virtual_key);
}

BUSTER_TEST_F_DECL WmEventList wm_win32_text_events_for_test(Arena* arena, String16 units, bool flush)
{
    WmHandle windowing = {.event_arena = arena};
    WmWindowHandle* window = arena_allocate(arena, WmWindowHandle, 1);
    *window = (WmWindowHandle){0};
    for (u64 i = 0; i < units.length; i += 1)
    {
        wm_win32_push_text_code_unit(&windowing, window, units.pointer[i]);
    }
    if (flush)
    {
        wm_win32_flush_pending_text(&windowing, window);
    }
    return windowing.event_list;
}
#endif

BUSTER_GLOBAL_LOCAL LRESULT window_callback(HWND window_handle, UINT message, WPARAM wparam, LPARAM lparam)
{
    LRESULT result = 0;

    bool call_default_window_proc = !windowing_handle.event_arena;

    if (!call_default_window_proc)
    {
        WmWindowHandle* window = wm_win32_window_from_win32(&windowing_handle, window_handle);

        switch (message)
        {
        case WM_SIZE:
        {
            if (window)
            {
                u32 packed = (u32)(uintptr_t)lparam;
                wm_event_push(&windowing_handle, (WmEvent){
                                                         .kind = WM_EVENT_WINDOW_RESIZE,
                                                         .window = window,
                                                         .position = (WmOffset){
                                                             .width = (WmUnit)(packed & 0xffffu),
                                                             .height = (WmUnit)((packed >> 16) & 0xffffu),
                                                         },
                                                     });
            }
        }
        break;
        case WM_PAINT:
        {
            PAINTSTRUCT paint = {0};
            if (BeginPaint(window_handle, &paint))
            {
#if BUSTER_LINK_LIBC
                update();
#endif
                EndPaint(window_handle, &paint);
            }
            DwmFlush();
        }
        break;
        case WM_CLOSE:
        {
            wm_event_push(&windowing_handle, (WmEvent){
                                                 .kind = WM_EVENT_WINDOW_CLOSE,
                                                 .window = window,
                                             });
        }
        break;
        case WM_LBUTTONUP:
        case WM_MBUTTONUP:
        case WM_RBUTTONUP:
        case WM_XBUTTONUP:
        {
            WmKey key = wm_win32_mouse_key(message, wparam);
            u8 button_mask = wm_win32_mouse_button_mask(key);
            if (window && key != WM_KEY_NULL && button_mask && (window->held_mouse_buttons & button_mask))
            {
                wm_event_push(&windowing_handle, (WmEvent){
                                                         .kind = WM_EVENT_BUTTON_RELEASE,
                                                         .window = window,
                                                         .key = key,
                                                         .modifiers = wm_win32_modifiers_from_mouse(wparam),
                                                         .position = wm_win32_position_from_lparam(lparam),
                                                     });
                window->held_mouse_buttons &= (u8)~button_mask;
            }
            if ((!window || !window->held_mouse_buttons) && GetCapture() == window_handle)
            {
                ReleaseCapture();
            }
            if (message == WM_XBUTTONUP && key != WM_KEY_NULL)
            {
                result = TRUE;
            }
        }
        break;
        case WM_LBUTTONDOWN:
        case WM_MBUTTONDOWN:
        case WM_RBUTTONDOWN:
        case WM_XBUTTONDOWN:
        {
            WmKey key = wm_win32_mouse_key(message, wparam);
            if (window && key != WM_KEY_NULL)
            {
                u8 button_mask = wm_win32_mouse_button_mask(key);
                if (button_mask)
                {
                    if (!window->held_mouse_buttons)
                    {
                        SetCapture(window_handle);
                    }
                    window->held_mouse_buttons |= button_mask;
                }
                wm_event_push(&windowing_handle, (WmEvent){
                                                         .kind = WM_EVENT_BUTTON_PRESS,
                                                         .window = window,
                                                         .key = key,
                                                         .modifiers = wm_win32_modifiers_from_mouse(wparam),
                                                         .position = wm_win32_position_from_lparam(lparam),
                                                     });
            }
            if (message == WM_XBUTTONDOWN && key != WM_KEY_NULL)
            {
                result = TRUE;
            }
        }
        break;
        case WM_MOUSEMOVE:
        {
            if (window)
            {
                wm_event_push(&windowing_handle, (WmEvent){
                                                         .kind = WM_EVENT_MOUSE_MOVE,
                                                         .window = window,
                                                         .modifiers = wm_win32_modifiers_from_mouse(wparam),
                                                         .position = wm_win32_position_from_lparam(lparam),
                                                     });
            }
        }
        break;
        case WM_MOUSEWHEEL:
        {
            s16 delta = (s16)(u16)(((uintptr_t)wparam >> 16) & 0xffffu);
            WmKey key = delta > 0 ? WM_KEY_MOUSE_WHEEL_UP : delta < 0 ? WM_KEY_MOUSE_WHEEL_DOWN : WM_KEY_NULL;
            if (window && key != WM_KEY_NULL)
            {
                wm_event_push(&windowing_handle, (WmEvent){
                                                         .kind = WM_EVENT_BUTTON_PRESS,
                                                         .window = window,
                                                         .key = key,
                                                         .modifiers = wm_win32_modifiers_from_mouse(wparam),
                                                         .position = wm_win32_wheel_position(window_handle, lparam),
                                                     });
            }
        }
        break;
        case WM_MOUSEHWHEEL:
        {
            s16 delta = (s16)(u16)(((uintptr_t)wparam >> 16) & 0xffffu);
            WmKey key = delta > 0 ? WM_KEY_MOUSE_WHEEL_RIGHT : delta < 0 ? WM_KEY_MOUSE_WHEEL_LEFT : WM_KEY_NULL;
            if (window && key != WM_KEY_NULL)
            {
                wm_event_push(&windowing_handle, (WmEvent){
                                                         .kind = WM_EVENT_BUTTON_PRESS,
                                                         .window = window,
                                                         .key = key,
                                                         .modifiers = wm_win32_modifiers_from_mouse(wparam),
                                                         .position = wm_win32_wheel_position(window_handle, lparam),
                                                     });
            }
        }
        break;
        case WM_CAPTURECHANGED:
        {
            wm_win32_push_mouse_releases(&windowing_handle, window, window_handle);
        }
        break;
        case WM_SYSKEYDOWN:
        case WM_SYSKEYUP:
        case WM_KEYDOWN:
        case WM_KEYUP:
        {
            WmKey key = wm_win32_keyboard_key(wparam);
            if (window && key != WM_KEY_NULL)
            {
                bool pressed = message == WM_KEYDOWN || message == WM_SYSKEYDOWN;
                wm_event_push(&windowing_handle, (WmEvent){
                                                         .kind = pressed ? WM_EVENT_KEY_PRESS : WM_EVENT_KEY_RELEASE,
                                                         .window = window,
                                                         .key = key,
                                                         .modifiers = wm_win32_modifiers_from_keyboard(),
                                                     });
            }
            if (message == WM_SYSKEYDOWN || message == WM_SYSKEYUP)
            {
                call_default_window_proc = true;
            }
        }
        break;
        case WM_SYSCHAR:
        {
            call_default_window_proc = true;
        }
        break;
        case WM_CHAR:
        {
            wm_win32_push_text_code_unit(&windowing_handle, window, (char16)(u16)wparam);
        }
        break;
        case WM_SETFOCUS:
        {
            if (window)
            {
                wm_event_push(&windowing_handle, (WmEvent){
                                                         .kind = WM_EVENT_WINDOW_FOCUS,
                                                         .window = window,
                                                     });
            }
        }
        break;
        case WM_KILLFOCUS:
        {
            wm_win32_flush_pending_text(&windowing_handle, window);
            wm_win32_push_mouse_releases(&windowing_handle, window, window_handle);
            if (GetCapture() == window_handle)
            {
                ReleaseCapture();
            }
            if (window)
            {
                wm_event_push(&windowing_handle, (WmEvent){
                                                         .kind = WM_EVENT_WINDOW_UNFOCUS,
                                                         .window = window,
                                                     });
            }
        }
        break;
        case WM_DPICHANGED:
        {
            RECT* suggested = (RECT*)lparam;
            if (suggested)
            {
                SetWindowPos(window_handle, 0,
                             suggested->left,
                             suggested->top,
                             suggested->right - suggested->left,
                             suggested->bottom - suggested->top,
                             SWP_NOZORDER | SWP_NOACTIVATE);
            }
        }
        break;
        case WM_DROPFILES:
        {
            wm_win32_push_file_drop(&windowing_handle, window, wparam);
        }
        break;
        case WM_NCPAINT:
        case WM_DWMCOMPOSITIONCHANGED:
        case WM_WINDOWPOSCHANGED:
        case WM_SETICON:
        case WM_SETTEXT:
        case WM_NCACTIVATE:
        case WM_NCCALCSIZE:
        case WM_NCHITTEST:
        case WM_SETCURSOR:
        {
            call_default_window_proc = true;
        }
        break;
        default:
        {
            call_default_window_proc = true;
        }
        }
    }

    if (call_default_window_proc)
    {
        result = DefWindowProcW(window_handle, message, wparam, lparam);
    }

    return result;
}

BUSTER_GLOBAL_LOCAL void wm_platform_poll_events(Arena* arena, WmHandle* windowing)
{
    BUSTER_UNUSED(arena);
    BUSTER_UNUSED(windowing);
    while (true)
    {
        MSG msg;
        BOOL peek = PeekMessageW(&msg, 0, 0, 0, PM_REMOVE);
        if (!peek)
        {
            break;
        }

        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

BUSTER_GLOBAL_LOCAL WmHandle* wm_platform_initialize(void)
{
    WmHandle* result = {0};
    bool success = false;
    wm_win32_initialize_dpi_awareness();
    windowing_handle.instance = GetModuleHandleW(0);
    {
        WNDCLASSEXW wndclass = {sizeof(wndclass)};
        wndclass.lpfnWndProc = &window_callback;
        wndclass.hInstance = windowing_handle.instance;
        wndclass.lpszClassName = graphical_window_class_name;
        wndclass.hCursor = LoadCursorW(0, IDC_ARROW);
        wndclass.hIcon = LoadIcon(windowing_handle.instance, MAKEINTRESOURCE(1));
        wndclass.style = CS_VREDRAW | CS_HREDRAW;
        ATOM wndatom = RegisterClassExW(&wndclass);
        (void)wndatom;
    }
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
    TemporalArena temp = scratch_begin(0, 0);
    u32 use_default_position = true;
    DWORD style_flags = WS_EX_APPWINDOW;
    HWND window_handle = CreateWindowExW(style_flags, graphical_window_class_name, string16_from_string8(temp.arena, create.name, true).pointer,
                                         WS_OVERLAPPEDWINDOW | WS_SIZEBOX, use_default_position ? CW_USEDEFAULT : 0, use_default_position ? CW_USEDEFAULT : 0,
                                         create.size.width, create.size.height, 0, 0, windowing->instance, 0);
    if (window_handle)
    {
        DragAcceptFiles(window_handle, true);
        result = arena_allocate(windowing->window_arena, WmWindowHandle, 1);
        *result = (WmWindowHandle){
            .owner = windowing,
            .handle = window_handle,
        };
        ShowWindow(window_handle, SW_SHOW);
    }
    scratch_end(temp);
    return result;
}

WmRect wm_window_get_framebuffer_rect(WmHandle* windowing, WmWindowHandle* wm_window)
{
    WmRect result = {0};
    if (wm_window)
    {
        BUSTER_UNUSED(windowing);
        RECT rect;
        if (GetClientRect(wm_window->handle, &rect))
        {
            result.x0 = (WmUnit)rect.left;
            result.x1 = (WmUnit)rect.right;
            result.y0 = (WmUnit)rect.top;
            result.y1 = (WmUnit)rect.bottom;
        }
    }
    return result;
}

f32 wm_window_get_dpi(WmHandle* windowing, WmWindowHandle* wm_window)
{
    f32 result = 96.0f;
    BUSTER_UNUSED(windowing);
    HWND window_handle = wm_window->handle;
    if (window_handle)
    {
        if (wm_win32_get_dpi_for_window)
        {
            result = (f32)wm_win32_get_dpi_for_window(window_handle);
        }
        else
        {
            HDC device_context = GetDC(window_handle);
            if (device_context)
            {
                result = (f32)GetDeviceCaps(device_context, LOGPIXELSX);
                ReleaseDC(window_handle, device_context);
            }
        }
    }
    return result;
}

WmNativeSurface wm_window_get_native_surface(WmHandle* windowing, WmWindowHandle* window)
{
    WmNativeSurface result = {0};
    result.display = windowing->instance;
    result.window = window->handle;
    result.kind = WM_NATIVE_SURFACE_WIN32;
    return result;
}

bool wm_window_is_visible(WmHandle* windowing)
{
    BUSTER_UNUSED(windowing);
    return true;
}
