// Desktop component entry point for test_ui_slider. It initializes foundation
// runtime state, invokes ui_slider_tests, and supplies an unused native renderer
// boundary. The compiler and desktop rendering/window backends stay separate.
#include <buster/lib/system_headers.h>
#include <buster/lib/os.h>
#include <buster/lib/ui_builder.h>
#include <buster/lib/string.h>
#include <buster/tests/ui_slider_test.h>
#include <stdio.h>

BUSTER_V_IMPL OsState os_state;
BUSTER_GLOBAL_LOCAL ProgramState ui_slider_program;
BUSTER_V_IMPL ProgramState* program_state = &ui_slider_program;
BUSTER_GLOBAL_LOCAL u32 ui_slider_renderer_calls;

#if BUSTER_UNITY_BUILD
#include <buster/lib/arena.c>
#include <buster/lib/integer.c>
#include <buster/lib/string.c>
#include <buster/lib/os.c>
#include <buster/lib/file.c>
#include <buster/lib/hash.c>
#include <buster/lib/time.c>
#include <buster/lib/float.c>
#include <buster/lib/ui_core.c>
#include <buster/lib/ui_builder.c>
#include <buster/tests/ui_slider_test.c>
#endif

RenderingWindowSize rendering_window_get_size(RenderingWindowHandle* window)
{
    BUSTER_UNUSED(window);
    ui_slider_renderer_calls += 1;
    return (RenderingWindowSize){0};
}

void rendering_window_render_rect(RenderingWindowHandle* window, RectDraw draw)
{
    BUSTER_UNUSED(window);
    BUSTER_UNUSED(draw);
    ui_slider_renderer_calls += 1;
}

void rendering_window_render_text(RenderingHandle* rendering, RenderingWindowHandle* window, String8 string, float4 color,
                                RenderFontType font_type, f32 x_offset, f32 y_offset)
{
    BUSTER_UNUSED(rendering);
    BUSTER_UNUSED(window);
    BUSTER_UNUSED(string);
    BUSTER_UNUSED(color);
    BUSTER_UNUSED(font_type);
    BUSTER_UNUSED(x_offset);
    BUSTER_UNUSED(y_offset);
    ui_slider_renderer_calls += 1;
}

bool rendering_window_render_background_blur_rounded(RenderingWindowHandle* window, F32Interval2 rect, u32 radius, float4 corner_radii)
{
    BUSTER_UNUSED(window);
    BUSTER_UNUSED(rect);
    BUSTER_UNUSED(radius);
    BUSTER_UNUSED(corner_radii);
    ui_slider_renderer_calls += 1;
    return false;
}

void buster_test_error_arguments(UnitTestArguments* arguments, u32 line, String8 function, String8 file_path, String8 format, ...)
{
    BUSTER_UNUSED(arguments);
    va_list arguments_list;
    va_start(arguments_list, format);
    String8 message = string_format_va(program_state->arena, format, arguments_list, STRING_FORMAT_VA_GP_SLOTS(8));
    va_end(arguments_list);
    fprintf(stderr, "%.*s:%u: %.*s: %.*s\n", (int)file_path.length, file_path.pointer, (unsigned)line,
            (int)function.length, function.pointer, (int)message.length, message.pointer);
}

int main(void)
{
    os_state.page_size = os_get_page_size();
    os_state.allocation_granularity = os_state.page_size;
    os_state.large_page_size = BUSTER_MB(2);
    ThreadContext* context = thread_context_allocate();
    thread_context_select(context);
    Arena* arena = arena_create((ArenaCreation){0});
    program_state->arena = arena;
    UnitTestArguments arguments = {.arena = arena};
    UnitTestResult result = ui_slider_tests(&arguments);
    BUSTER_TEST(&arguments, ui_slider_renderer_calls == 0);
    printf("ui_slider_tests: %llu/%llu assertions passed; renderer_calls=%u\n", (unsigned long long)result.succeeded_test_count,
           (unsigned long long)result.test_count, (unsigned)ui_slider_renderer_calls);
    thread_context_release(context);
    arena_destroy(arena, 1);
    program_state->arena = 0;
    return result.succeeded_test_count != result.test_count;
}
