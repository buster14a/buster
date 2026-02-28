#pragma once

#include <buster/base.h>
#include <buster/window.h>

typedef struct RenderingHandle RenderingHandle;
typedef struct RenderingWindowHandle RenderingWindowHandle;

ENUM(RectTextureSlot,
    RECT_TEXTURE_SLOT_WHITE,
    RECT_TEXTURE_SLOT_MONOSPACE_FONT,
    // RECT_TEXTURE_SLOT_PROPORTIONAL_FONT,
    RECT_TEXTURE_SLOT_COUNT
);

ENUM(RenderFontType,
    RENDER_FONT_TYPE_MONOSPACE,
    RENDER_FONT_TYPE_PROPORTIONAL,
    RENDER_FONT_TYPE_COUNT
);

ENUM(TextureFormat,
    TEXTURE_FORMAT_R8_UNORM,
    TEXTURE_FORMAT_R8G8B8A8_SRGB,
);

STRUCT(TextureMemory)
{
    void* pointer;
    u32 width;
    u32 height;
    u32 depth;
    TextureFormat format;
};

STRUCT(RectDraw)
{
    F32Interval2 vertex;
    F32Interval2 texture;
    vec4 colors[4];
    u32 texture_index;
    u8 reserved[12];
};

BUSTER_DECL RenderingHandle* rendering_initialize(Arena* arena);
BUSTER_DECL RenderingWindowHandle* rendering_window_initialize(Arena* arena, OsWindowingHandle* windowing, RenderingHandle* rendering, OsWindowHandle* window);
BUSTER_DECL void rendering_window_rect_texture_update_begin(RenderingWindowHandle* window);
BUSTER_DECL TextureIndex white_texture_create(Arena* arena, RenderingHandle* rendering);
BUSTER_DECL void rendering_window_queue_rect_texture_update(RenderingHandle* rendering, RenderingWindowHandle* window, RectTextureSlot slot, TextureIndex texture_index);
BUSTER_DECL void rendering_queue_font_update(RenderingHandle* rendering, RenderingWindowHandle* window, RenderFontType type, FontTextureAtlas atlas);
BUSTER_DECL void rendering_window_rect_texture_update_end(RenderingHandle* rendering, RenderingWindowHandle* window);
BUSTER_DECL TextureIndex rendering_texture_create(RenderingHandle* rendering, TextureMemory texture_memory);
BUSTER_DECL FontTextureAtlas rendering_font_create(Arena* arena, RenderingHandle* rendering, FontTextureAtlasCreate create);
BUSTER_DECL void rendering_window_frame_begin(RenderingHandle* rendering, RenderingWindowHandle* window);
BUSTER_DECL void rendering_window_frame_end(RenderingHandle* rendering, RenderingWindowHandle* window);
BUSTER_DECL void rendering_window_render_rect(RenderingWindowHandle* window, RectDraw draw);
BUSTER_DECL void rendering_window_render_text(RenderingHandle* rendering, RenderingWindowHandle* window, String8 string, float4 color, RenderFontType font_type, f32 x_offset, f32 y_offset);
BUSTER_DECL void rendering_deinitialize(RenderingHandle* rendering);
BUSTER_DECL void rendering_window_deinitialize(RenderingHandle* rendering, RenderingWindowHandle* window);
