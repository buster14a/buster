#include <buster/lib/rendering/internal.h>

struct RenderingHandle
{
    u32 texture_count;
    FontTextureAtlas fonts[RENDER_FONT_TYPE_COUNT];
};

struct RenderingWindowHandle
{
    u32 width;
    u32 height;
    RenderingCommandStream* commands;
    RenderingScale scale;
    bool last_frame_error;
    u8 reserved[3];
};

BUSTER_GLOBAL_LOCAL RenderingHandle rendering_handle = {0};

RenderingHandle* rendering_initialize(Arena* arena)
{
    BUSTER_UNUSED(arena);
    rendering_handle = (RenderingHandle){0};
    return &rendering_handle;
}

RenderingWindowHandle* rendering_window_initialize(Arena* arena, WmHandle* windowing, RenderingHandle* rendering, WmWindowHandle* window)
{
    BUSTER_UNUSED(rendering);
    RenderingWindowHandle* result = arena_allocate(arena, RenderingWindowHandle, 1);
    WmRect rect = {0};
    if (windowing && window)
    {
        rect = wm_window_get_framebuffer_rect(windowing, window);
    }
    WmOffset size = offset_from_rect(rect);
    result->width = size.width;
    result->height = size.height;
    result->scale = (RenderingScale){.x = 1.0f, .y = 1.0f};
    result->last_frame_error = false;
    result->commands = arena_allocate(arena, RenderingCommandStream, 1);
    result->commands->vertex_cpu = arena_create((ArenaCreation){0});
    result->commands->index_cpu = arena_create((ArenaCreation){0});
    rendering_command_stream_bind_buffers(result->commands, result->commands->vertex_cpu, result->commands->index_cpu);
    rendering_command_stream_begin(result->commands, (RenderingWindowSize){.width = result->width, .height = result->height}, result->scale);
    return result;
}

RenderingCommandStream* rendering_window_command_stream(RenderingWindowHandle* window)
{
    return window ? window->commands : 0;
}

void rendering_window_set_content_scale_internal(RenderingWindowHandle* window, RenderingScale scale)
{
    if (!window)
    {
        return;
    }
    window->scale = rendering_scale_is_valid(scale) ? scale : (RenderingScale){.x = 1.0f, .y = 1.0f};
    rendering_command_stream_set_scale(window->commands, window->scale);
}

RenderingWindowSize rendering_window_get_size(RenderingWindowHandle* window)
{
    return window ? (RenderingWindowSize){
                       .width = window->width,
                       .height = window->height,
                   }
                 : (RenderingWindowSize){0};
}

bool rendering_window_set_size_for_test(RenderingWindowHandle* window, RenderingWindowSize size)
{
    bool result = window && size.width && size.height;
    if (result)
    {
        window->width = size.width;
        window->height = size.height;
    }

    return result;
}

void rendering_window_rect_texture_update_begin(RenderingWindowHandle* window)
{
    BUSTER_UNUSED(window);
}

TextureIndex rendering_texture_create(RenderingHandle* rendering, TextureMemory texture_memory)
{
    BUSTER_UNUSED(texture_memory);
    TextureIndex result = {.value = UINT32_MAX};
    if (rendering)
    {
        result = (TextureIndex){.value = rendering->texture_count};
        rendering->texture_count += 1;
    }

    return result;
}

TextureIndex white_texture_create(Arena* arena, RenderingHandle* rendering)
{
    BUSTER_UNUSED(arena);
    return rendering_texture_create(rendering, (TextureMemory){0});
}

void rendering_window_queue_rect_texture_update(RenderingHandle* rendering, RenderingWindowHandle* window, RectTextureSlot slot, TextureIndex texture_index)
{
    BUSTER_UNUSED(rendering);
    BUSTER_UNUSED(window);
    rendering_command_stream_set_texture_binding(window ? window->commands : 0, (u32)slot, texture_index);
}

FontTextureAtlas rendering_font_create(Arena* arena, RenderingHandle* rendering, FontTextureAtlasCreate create)
{
    BUSTER_UNUSED(arena);
    BUSTER_UNUSED(create);
    FontTextureAtlas result = {0};
    result.texture = rendering_texture_create(rendering, (TextureMemory){0});
    return result;
}

void rendering_queue_font_update(RenderingHandle* rendering, RenderingWindowHandle* window, RenderFontType type, FontTextureAtlas atlas)
{
    BUSTER_UNUSED(window);
    if (rendering && (u32)type < RENDER_FONT_TYPE_COUNT)
    {
        rendering->fonts[(u32)type] = atlas;
    }
}

void rendering_window_rect_texture_update_end(RenderingHandle* rendering, RenderingWindowHandle* window)
{
    BUSTER_UNUSED(rendering);
    BUSTER_UNUSED(window);
}

void rendering_window_frame_begin(RenderingHandle* rendering, RenderingWindowHandle* window)
{
    BUSTER_UNUSED(rendering);
    if (window)
    {
        rendering_command_stream_begin(window->commands, (RenderingWindowSize){.width = window->width, .height = window->height}, window->scale);
    }
}

void rendering_window_frame_end(RenderingHandle* rendering, RenderingWindowHandle* window)
{
    BUSTER_UNUSED(rendering);
    if (window && window->commands)
    {
        rendering_backend_trace_begin(window->commands, RENDERING_BACKEND_NULL);
        rendering_backend_trace_preflight(window->commands);
        rendering_backend_trace_finish(window->commands, false, false, !rendering_command_stream_is_valid(window->commands));
        rendering_frame_error_commit(&window->last_frame_error, !rendering_command_stream_is_valid(window->commands));
        window->commands->frame_active = false;
    }
}

bool rendering_window_has_rendering_error_internal(RenderingWindowHandle* window)
{
    return !window || rendering_frame_error_query(&window->last_frame_error);
}

void rendering_backend_trace_command(RenderingCommandStream* stream, u32 command_index, RenderingCommand command)
{
    if (!stream)
    {
        return;
    }
    rendering_backend_trace_record_command(stream, command_index);
    rendering_backend_trace_validate_common(stream, command_index, command);
}

RenderingBackendReplayResult rendering_backend_replay_for_test(RenderingCommandStream* stream, RenderingReplayEvent* events, u32 capacity)
{
    RenderingBackendReplayResult result = rendering_backend_replay_policy(stream, RENDERING_BACKEND_NULL, events, capacity);
    rendering_backend_trace_begin(stream, RENDERING_BACKEND_NULL);
    rendering_backend_trace_preflight(stream);
    rendering_backend_trace_finish(stream, false, false, !rendering_command_stream_is_valid(stream));
    rendering_backend_trace_copy_result(&result, stream);
    return result;
}

void rendering_window_deinitialize(RenderingHandle* rendering, RenderingWindowHandle* window)
{
    BUSTER_UNUSED(rendering);
    if (window)
    {
        if (window->commands && window->commands->vertex_cpu)
        {
            arena_destroy(window->commands->vertex_cpu, 1);
            window->commands->vertex_cpu = 0;
        }
        if (window->commands && window->commands->index_cpu)
        {
            arena_destroy(window->commands->index_cpu, 1);
            window->commands->index_cpu = 0;
        }
    }
}

void rendering_deinitialize(RenderingHandle* rendering)
{
    BUSTER_UNUSED(rendering);
}
