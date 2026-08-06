#pragma once

#include <buster/lib/base.h>
#include <buster/lib/window.h>

typedef struct RenderingHandle RenderingHandle;
typedef struct RenderingWindowHandle RenderingWindowHandle;
typedef struct RenderingWindowSize RenderingWindowSize;
struct RenderingWindowSize
{
    u32 width;
    u32 height;
};

typedef struct RenderingClipRect RenderingClipRect;
struct RenderingClipRect
{
    s32 x0;
    s32 y0;
    s32 x1;
    s32 y1;
};

typedef struct RenderingScale RenderingScale;
struct RenderingScale
{
    f32 x;
    f32 y;
};

typedef enum RectTextureSlot
{
    RECT_TEXTURE_SLOT_WHITE,
    RECT_TEXTURE_SLOT_MONOSPACE_FONT,
    // RECT_TEXTURE_SLOT_PROPORTIONAL_FONT,
    RECT_TEXTURE_SLOT_COUNT,
} RectTextureSlot;

typedef enum RenderFontType
{
    RENDER_FONT_TYPE_MONOSPACE,
    RENDER_FONT_TYPE_PROPORTIONAL,
    RENDER_FONT_TYPE_COUNT,
} RenderFontType;

typedef enum TextureFormat
{
    TEXTURE_FORMAT_R8_UNORM,
    TEXTURE_FORMAT_R8G8B8A8_SRGB,
    TEXTURE_FORMAT_COUNT,
} TextureFormat;

typedef struct TextureMemory TextureMemory;
struct TextureMemory
{
    void* pointer;
    u32 width;
    u32 height;
    u32 depth;
    TextureFormat format;
};

typedef struct RectDraw RectDraw;
struct RectDraw
{
    F32Interval2 vertex;
    F32Interval2 texture;
    vec4 colors[4];
    u32 texture_index;
    u8 reserved[12];
};

BUSTER_F_DECL RenderingHandle* rendering_initialize(Arena* arena);
BUSTER_F_DECL RenderingWindowHandle* rendering_window_initialize(Arena* arena, WmHandle* windowing, RenderingHandle* rendering, WmWindowHandle* window);
#if BUSTER_ANDROID
BUSTER_F_DECL void rendering_window_surface_recreate(RenderingHandle* rendering, WmHandle* windowing, RenderingWindowHandle* window, WmWindowHandle* wm_window);
#endif
BUSTER_F_DECL RenderingWindowSize rendering_window_get_size(RenderingWindowHandle* window);
BUSTER_F_DECL void rendering_window_rect_texture_update_begin(RenderingWindowHandle* window);
BUSTER_F_DECL TextureIndex white_texture_create(Arena* arena, RenderingHandle* rendering);
BUSTER_F_DECL void rendering_window_queue_rect_texture_update(RenderingHandle* rendering, RenderingWindowHandle* window, RectTextureSlot slot,
                                                              TextureIndex texture_index);
BUSTER_F_DECL void rendering_queue_font_update(RenderingHandle* rendering, RenderingWindowHandle* window, RenderFontType type, FontTextureAtlas atlas);
BUSTER_F_DECL void rendering_window_rect_texture_update_end(RenderingHandle* rendering, RenderingWindowHandle* window);
BUSTER_F_DECL TextureIndex rendering_texture_create(RenderingHandle* rendering, TextureMemory texture_memory);
BUSTER_F_DECL FontTextureAtlas rendering_font_create(Arena* arena, RenderingHandle* rendering, FontTextureAtlasCreate create);
BUSTER_F_DECL void rendering_window_frame_begin(RenderingHandle* rendering, RenderingWindowHandle* window);
BUSTER_F_DECL void rendering_window_frame_end(RenderingHandle* rendering, RenderingWindowHandle* window);
BUSTER_F_DECL bool rendering_window_has_rendering_error(RenderingWindowHandle* window);
BUSTER_F_DECL RenderingClipRect rendering_clip_rect_from_f32(F32Interval2 rect, RenderingScale scale, RenderingWindowSize target_size);
BUSTER_F_DECL RenderingClipRect rendering_clip_rect_intersect(RenderingClipRect a, RenderingClipRect b);
BUSTER_F_DECL bool rendering_clip_rect_is_empty(RenderingClipRect rect);
BUSTER_F_DECL void rendering_window_set_content_scale(RenderingWindowHandle* window, RenderingScale scale);
BUSTER_F_DECL void rendering_window_clip_push(RenderingWindowHandle* window, F32Interval2 rect);
BUSTER_F_DECL void rendering_window_clip_pop(RenderingWindowHandle* window);
BUSTER_F_DECL void rendering_window_clip_reset(RenderingWindowHandle* window);
BUSTER_F_DECL void rendering_window_flush(RenderingWindowHandle* window);
BUSTER_F_DECL bool rendering_window_set_render_target(RenderingWindowHandle* window, u32 target);
BUSTER_F_DECL void rendering_window_render_rect(RenderingWindowHandle* window, RectDraw draw);
BUSTER_F_DECL void rendering_window_render_text(RenderingHandle* rendering, RenderingWindowHandle* window, String8 string, float4 color,
                                                RenderFontType font_type, f32 x_offset, f32 y_offset);
BUSTER_F_DECL bool rendering_window_render_background_blur(RenderingWindowHandle* window, F32Interval2 rect, u32 radius);
BUSTER_F_DECL bool rendering_window_render_background_blur_rounded(RenderingWindowHandle* window, F32Interval2 rect, u32 radius, float4 corner_radii);
BUSTER_F_DECL bool rendering_blur_rgba8(Arena* scratch, u8* pixels, u32 width, u32 height, u32 stride, u32 radius);
BUSTER_F_DECL TextureIndex rendering_texture_create_blurred(Arena* arena, RenderingHandle* rendering, TextureMemory source, u32 radius);
BUSTER_F_DECL f32 rendering_blur_kernel_weight(u32 radius, s32 offset);
BUSTER_F_DECL u32 rendering_blur_kernel_weight_fixed16(u32 radius, s32 offset);
BUSTER_F_DECL f32 rendering_rounded_rect_mask_factor(RenderingClipRect rect, float2 pixel_position, float4 corner_radii);
BUSTER_F_DECL void rendering_deinitialize(RenderingHandle* rendering);
BUSTER_F_DECL void rendering_window_deinitialize(RenderingHandle* rendering, RenderingWindowHandle* window);
