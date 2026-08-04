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
    RenderingCommandStream commands;
    RenderingScale scale;
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
    result->commands.vertex_cpu = arena_create((ArenaCreation){0});
    result->commands.index_cpu = arena_create((ArenaCreation){0});
    rendering_command_stream_bind_buffers(&result->commands, result->commands.vertex_cpu, result->commands.index_cpu);
    rendering_command_stream_begin(&result->commands, (RenderingWindowSize){.width = result->width, .height = result->height}, result->scale);
    return result;
}

RenderingCommandStream* rendering_window_command_stream(RenderingWindowHandle* window)
{
    return window ? &window->commands : 0;
}

void rendering_window_set_content_scale_internal(RenderingWindowHandle* window, RenderingScale scale)
{
    if (!window)
    {
        return;
    }
    window->scale = scale.x > 0.0f && scale.y > 0.0f ? scale : (RenderingScale){.x = 1.0f, .y = 1.0f};
    rendering_command_stream_set_scale(&window->commands, window->scale);
}

RenderingWindowSize rendering_window_get_size(RenderingWindowHandle* window)
{
    return window ? (RenderingWindowSize){
                       .width = window->width,
                       .height = window->height,
                   }
                 : (RenderingWindowSize){0};
}

void rendering_window_rect_texture_update_begin(RenderingWindowHandle* window)
{
    BUSTER_UNUSED(window);
}

TextureIndex rendering_texture_create(RenderingHandle* rendering, TextureMemory texture_memory)
{
    BUSTER_UNUSED(texture_memory);
    if (!rendering)
    {
        return (TextureIndex){.value = UINT32_MAX};
    }
    TextureIndex result = {.value = rendering->texture_count};
    rendering->texture_count += 1;
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
    BUSTER_UNUSED(slot);
    BUSTER_UNUSED(texture_index);
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
        rendering_command_stream_begin(&window->commands, (RenderingWindowSize){.width = window->width, .height = window->height}, window->scale);
    }
}

void rendering_window_frame_end(RenderingHandle* rendering, RenderingWindowHandle* window)
{
    BUSTER_UNUSED(rendering);
    BUSTER_UNUSED(window);
}

void rendering_window_deinitialize(RenderingHandle* rendering, RenderingWindowHandle* window)
{
    BUSTER_UNUSED(rendering);
    if (window)
    {
        if (window->commands.vertex_cpu)
        {
            arena_destroy(window->commands.vertex_cpu, 1);
            window->commands.vertex_cpu = 0;
        }
        if (window->commands.index_cpu)
        {
            arena_destroy(window->commands.index_cpu, 1);
            window->commands.index_cpu = 0;
        }
    }
}

void rendering_deinitialize(RenderingHandle* rendering)
{
    BUSTER_UNUSED(rendering);
}
