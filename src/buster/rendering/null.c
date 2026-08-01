#include <buster/rendering/internal.h>

struct RenderingHandle
{
    u32 texture_count;
    FontTextureAtlas fonts[RENDER_FONT_TYPE_COUNT];
};

struct RenderingWindowHandle
{
    u32 width;
    u32 height;
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
    WmRect rect = wm_window_get_framebuffer_rect(windowing, window);
    WmOffset size = offset_from_rect(rect);
    result->width = size.width;
    result->height = size.height;
    return result;
}

RenderingWindowSize rendering_window_get_size(RenderingWindowHandle* window)
{
    return (RenderingWindowSize){
        .width = window->width,
        .height = window->height,
    };
}

void rendering_window_rect_texture_update_begin(RenderingWindowHandle* window)
{
    BUSTER_UNUSED(window);
}

TextureIndex rendering_texture_create(RenderingHandle* rendering, TextureMemory texture_memory)
{
    BUSTER_UNUSED(texture_memory);
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
    rendering->fonts[(u32)type] = atlas;
}

void rendering_window_rect_texture_update_end(RenderingHandle* rendering, RenderingWindowHandle* window)
{
    BUSTER_UNUSED(rendering);
    BUSTER_UNUSED(window);
}

void rendering_window_frame_begin(RenderingHandle* rendering, RenderingWindowHandle* window)
{
    BUSTER_UNUSED(rendering);
    BUSTER_UNUSED(window);
}

void rendering_window_frame_end(RenderingHandle* rendering, RenderingWindowHandle* window)
{
    BUSTER_UNUSED(rendering);
    BUSTER_UNUSED(window);
}

void rendering_window_render_rect(RenderingWindowHandle* window, RectDraw draw)
{
    BUSTER_UNUSED(window);
    BUSTER_UNUSED(draw);
}

void rendering_window_render_text(RenderingHandle* rendering, RenderingWindowHandle* window, String8 string, float4 color, RenderFontType font_type,
                                  f32 x_offset, f32 y_offset)
{
    BUSTER_UNUSED(rendering);
    BUSTER_UNUSED(window);
    BUSTER_UNUSED(string);
    BUSTER_UNUSED(color);
    BUSTER_UNUSED(font_type);
    BUSTER_UNUSED(x_offset);
    BUSTER_UNUSED(y_offset);
}

void rendering_window_deinitialize(RenderingHandle* rendering, RenderingWindowHandle* window)
{
    BUSTER_UNUSED(rendering);
    BUSTER_UNUSED(window);
}

void rendering_deinitialize(RenderingHandle* rendering)
{
    BUSTER_UNUSED(rendering);
}
