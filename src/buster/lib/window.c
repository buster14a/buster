// Platform-neutral front door of the windowing module: event-list
// ownership and window lifecycle policy. Native windowing (XCB, Win32,
// Cocoa, ...) lives in the backend implementation files under window/,
// selected and included through internal.h; renderers receive handles
// through WmNativeSurface rather than reaching into WmHandle (AGENTS.md).

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

BUSTER_GLOBAL_LOCAL bool wm_utf8_code_unit_is_continuation(u8 code_unit)
{
    return (code_unit & 0xc0u) == 0x80u;
}

BUSTER_GLOBAL_LOCAL bool wm_utf8_string_is_valid(String8 string)
{
    bool result = true;
    for (u64 index = 0; result && index < string.length;)
    {
        u8 first = (u8)string.pointer[index];
        if (first <= 0x7fu)
        {
            index += 1;
        }
        else if (first >= 0xc2u && first <= 0xdfu)
        {
            result = index + 1 < string.length && wm_utf8_code_unit_is_continuation((u8)string.pointer[index + 1]);
            index += result ? 2 : 1;
        }
        else if (first == 0xe0u)
        {
            result = index + 2 < string.length && (u8)string.pointer[index + 1] >= 0xa0u && (u8)string.pointer[index + 1] <= 0xbfu &&
                     wm_utf8_code_unit_is_continuation((u8)string.pointer[index + 2]);
            index += result ? 3 : 1;
        }
        else if (first >= 0xe1u && first <= 0xecu)
        {
            result = index + 2 < string.length && wm_utf8_code_unit_is_continuation((u8)string.pointer[index + 1]) &&
                     wm_utf8_code_unit_is_continuation((u8)string.pointer[index + 2]);
            index += result ? 3 : 1;
        }
        else if (first == 0xedu)
        {
            result = index + 2 < string.length && (u8)string.pointer[index + 1] >= 0x80u && (u8)string.pointer[index + 1] <= 0x9fu &&
                     wm_utf8_code_unit_is_continuation((u8)string.pointer[index + 2]);
            index += result ? 3 : 1;
        }
        else if (first >= 0xeeu && first <= 0xefu)
        {
            result = index + 2 < string.length && wm_utf8_code_unit_is_continuation((u8)string.pointer[index + 1]) &&
                     wm_utf8_code_unit_is_continuation((u8)string.pointer[index + 2]);
            index += result ? 3 : 1;
        }
        else if (first == 0xf0u)
        {
            result = index + 3 < string.length && (u8)string.pointer[index + 1] >= 0x90u && (u8)string.pointer[index + 1] <= 0xbfu &&
                     wm_utf8_code_unit_is_continuation((u8)string.pointer[index + 2]) && wm_utf8_code_unit_is_continuation((u8)string.pointer[index + 3]);
            index += result ? 4 : 1;
        }
        else if (first >= 0xf1u && first <= 0xf3u)
        {
            result = index + 3 < string.length && wm_utf8_code_unit_is_continuation((u8)string.pointer[index + 1]) &&
                     wm_utf8_code_unit_is_continuation((u8)string.pointer[index + 2]) && wm_utf8_code_unit_is_continuation((u8)string.pointer[index + 3]);
            index += result ? 4 : 1;
        }
        else if (first == 0xf4u)
        {
            result = index + 3 < string.length && (u8)string.pointer[index + 1] >= 0x80u && (u8)string.pointer[index + 1] <= 0x8fu &&
                     wm_utf8_code_unit_is_continuation((u8)string.pointer[index + 2]) && wm_utf8_code_unit_is_continuation((u8)string.pointer[index + 3]);
            index += result ? 4 : 1;
        }
        else
        {
            result = false;
            index += 1;
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool wm_file_path_is_valid(String8 path)
{
    bool result = path.pointer != 0 && path.length != 0 && wm_utf8_string_is_valid(path);
    for (u64 index = 0; result && index < path.length; index += 1)
    {
        result = path.pointer[index] != 0;
    }
    return result;
}

u64 wm_native_file_drop_max_path_count(void)
{
    return BUSTER_NATIVE_FILE_DROP_MAX_PATH_COUNT;
}

u64 wm_native_file_drop_max_path_bytes(void)
{
    return BUSTER_NATIVE_FILE_DROP_MAX_PATH_BYTES;
}

bool wm_native_file_drop_budget_allows(u64 path_count, u64 output_bytes)
{
    return path_count <= BUSTER_NATIVE_FILE_DROP_MAX_PATH_COUNT && output_bytes <= BUSTER_NATIVE_FILE_DROP_MAX_PATH_BYTES;
}

bool wm_native_file_drop_array_size_allowed(u64 count, u64 element_size)
{
    return element_size != 0 && count <= UINT64_MAX / element_size;
}

#if BUSTER_MACOS || BUSTER_INCLUDE_TESTS
SliceString8 wm_apple_file_paths_from_values(Arena* arena, SliceWmAppleFileUrlPath values)
{
    // Every budget refusal yields the empty slice, so one gate carries all three
    // of them and the measuring pass stops as soon as one trips.
    u64 valid_count = 0;
    u64 output_bytes = 0;
    bool within_budget = wm_native_file_drop_budget_allows(values.length, 0);
    for (u64 index = 0; index < values.length && within_budget; index += 1)
    {
        WmAppleFileUrlPath value = values.pointer[index];
        if (value.is_file_url && wm_file_path_is_valid(value.path))
        {
            within_budget = value.path.length <= BUSTER_NATIVE_FILE_DROP_MAX_PATH_BYTES - output_bytes;
            if (within_budget)
            {
                output_bytes += value.path.length;
                valid_count += 1;
            }
        }
    }

    SliceString8 result = {0};
    if (within_budget && wm_native_file_drop_budget_allows(valid_count, output_bytes) &&
        wm_native_file_drop_array_size_allowed(valid_count, sizeof(String8)) && valid_count != 0)
    {
        result.pointer = arena_allocate(arena, String8, valid_count);
        result.length = valid_count;
        u64 output_index = 0;
        for (u64 index = 0; index < values.length; index += 1)
        {
            WmAppleFileUrlPath value = values.pointer[index];
            if (value.is_file_url && wm_file_path_is_valid(value.path))
            {
                result.pointer[output_index] = string_duplicate_arena(arena, value.path, false);
                output_index += 1;
            }
        }
    }

    return result;
}

BUSTER_GLOBAL_LOCAL s16 wm_s16_from_f64(f64 value)
{
    s16 result = 0;
    if (value == value)
    {
        if (value <= -32768.0)
        {
            result = -32768;
        }
        else if (value >= 32767.0)
        {
            result = 32767;
        }
        else
        {
            result = (s16)value;
        }
    }
    return result;
}

BUSTER_UNUSED_DECL WmOffset wm_apple_drop_position_from_content_point(f64 x, f64 y, f64 height)
{
    return (WmOffset){
        .x = wm_s16_from_f64(x),
        .y = wm_s16_from_f64(height - y),
    };
}
#endif

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
