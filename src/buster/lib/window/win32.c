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
typedef BOOL WINAPI WmWin32SetProcessDpiAwarenessContext(void* value);
typedef HRESULT WINAPI WmWin32SetProcessDpiAwareness(int value);
typedef UINT WINAPI WmWin32GetDpiForWindow(HWND hwnd);
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

BUSTER_GLOBAL_LOCAL LONG get_window_style(HWND window_handle)
{
    LONG result = window_handle ? GetWindowLongW(window_handle, GWL_STYLE) : 0;
    return result;
}

BUSTER_GLOBAL_LOCAL bool is_fullscreen_style(LONG style)
{
    bool result = !(style & WS_OVERLAPPEDWINDOW);
    return result;
}

BUSTER_GLOBAL_LOCAL bool is_fullscreen_window(HWND window_handle)
{
    return is_fullscreen_style(get_window_style(window_handle));
}

BUSTER_GLOBAL_LOCAL bool wm_rect_contains(WmRect r, WmOffset x)
{
    bool c = (r.min.x <= x.x && x.x < r.max.x && r.min.y <= x.y && x.y < r.max.y);
    return c;
}

BUSTER_GLOBAL_LOCAL LRESULT window_callback(HWND window_handle, UINT message, WPARAM wparam, LPARAM lparam)
{
    LRESULT result = 0;

    bool call_default_window_proc = !windowing_handle.event_arena;

    if (!call_default_window_proc)
    {
        WmWindowHandle* window = wm_win32_window_from_win32(&windowing_handle, window_handle);

        switch (message)
        {
            break;
        case WM_ENTERSIZEMOVE:
        {
            windowing_handle.resizing = true;
        }
        break;
        case WM_EXITSIZEMOVE:
        {
            windowing_handle.resizing = false;
        }
        break;
        case WM_SIZE:
        case WM_PAINT:
        {
            PAINTSTRUCT paint = {0};
            BeginPaint(window_handle, &paint);
#if BUSTER_LINK_LIBC
            update();
#endif
            EndPaint(window_handle, &paint);
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
        {
            os_fail();
        }
        break;
        case WM_LBUTTONDOWN:
        case WM_MBUTTONDOWN:
        case WM_RBUTTONDOWN:
        {
            os_fail();
        }
        break;
        case WM_MOUSEMOVE:
        {
            os_fail();
        }
        break;
        case WM_MOUSEWHEEL:
        {
            os_fail();
        }
        break;
        case WM_MOUSEHWHEEL:
        {
            os_fail();
        }
        break;
        case WM_SYSKEYDOWN:
        case WM_SYSKEYUP:
        {
            os_fail();
        }
        case WM_KEYDOWN:
        case WM_KEYUP:
        {
            os_fail();
        }
        break;
        case WM_SYSCHAR:
        {
            os_fail();
        }
        break;
        case WM_CHAR:
        {
            os_fail();
        }
        break;
        case WM_KILLFOCUS:
        {
            os_fail();
        }
        break;
        case WM_SETCURSOR:
        {
            WmRect rect = wm_window_get_framebuffer_rect(&windowing_handle, window);
            WmOffset mouse_point = {0};
            POINT p;
            if (GetCursorPos(&p))
            {
                if (ScreenToClient(window_handle, &p))
                {
                    mouse_point.x = (WmUnit)p.x;
                    mouse_point.y = (WmUnit)p.y;
                }
            }

            bool is_fullscreen = is_fullscreen_window(window_handle);
            bool on_border = false;

            if (window && window->custom_border && !is_fullscreen)
            {
                BUSTER_TODO();
            }
            if (!windowing_handle.resizing && !on_border && wm_rect_contains(rect, mouse_point))
            {
                // TODO
                // SetCursor();
            }
            else
            {
                BUSTER_TODO();
            }

            os_fail();
        }
        break;
        case WM_DPICHANGED:
        {
#if 0
                RECT* suggested = (RECT*)lparam;
                SetWindowPos(window_handle, 0,
                        suggested->left,
                        suggested->top,
                        suggested->right - suggested->left,
                        suggested->bottom - suggested->top,
                        SWP_NOZORDER | SWP_NOACTIVATE);
#else
            os_fail();
#endif
        }
        break;
        case WM_DROPFILES:
        {
            os_fail();
        }
        break;
        case WM_NCPAINT:
        {
            os_fail();
        }
        break;
        case WM_DWMCOMPOSITIONCHANGED:
        {
            os_fail();
        }
        break;
        case WM_WINDOWPOSCHANGED:
        {
            os_fail();
        }
        // TODO: undocumented messages
        // break;
        // case WM_NCUAHDRAWCAPTION:
        // case WM_NCUAHDRAWFRAME:
        // {
        // }
        break;
        case WM_SETICON:
        case WM_SETTEXT:
        {
            os_fail();
        }
        break;
        case WM_NCACTIVATE:
        {
            os_fail();
        }
        break;
        case WM_NCCALCSIZE:
        {
            os_fail();
        }
        break;
        case WM_NCHITTEST:
        {
            // TODO: improve
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
