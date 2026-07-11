#include <buster/rendering/internal.h>

#if BUSTER_USE_VULKAN
#include <buster/rendering/vulkan.c>
#elif defined(_WIN32) && BUSTER_USE_D3D12
#include <buster/rendering/d3d12.c>
#elif defined(__APPLE__)
#include <buster/rendering/metal.c>
#else
#include <buster/rendering/null.c>
#endif
#if BUSTER_USE_VULKAN || (defined(_WIN32) && BUSTER_USE_D3D12) || defined(__APPLE__)
RenderingWindowSize rendering_window_get_size(RenderingWindowHandle* window)
{
    return (RenderingWindowSize){
        .width = window->width,
        .height = window->height,
    };
}

void rendering_queue_font_update(RenderingHandle* rendering, RenderingWindowHandle* window, RenderFontType type, FontTextureAtlas atlas)
{
    RectTextureSlot slot = (RectTextureSlot)((u32)RECT_TEXTURE_SLOT_MONOSPACE_FONT + (u32)type);
    rendering_window_queue_rect_texture_update(rendering, window, slot, atlas.texture);
    rendering->fonts[(u32)type] = atlas;
}

TextureIndex white_texture_create(Arena* arena, RenderingHandle* rendering)
{
    u32 white_texture_width = 1024;
    u32 white_texture_height = white_texture_width;
    u32* white_texture_buffer = arena_allocate(arena, u32, white_texture_width * white_texture_height);
    memset(white_texture_buffer, 0xff, white_texture_width * white_texture_height * sizeof(u32));

    return rendering_texture_create(rendering, (TextureMemory) {
        .pointer = white_texture_buffer,
        .width = white_texture_width,
        .height = white_texture_height,
        .depth = 1,
        .format = TEXTURE_FORMAT_R8G8B8A8_SRGB,
    });
}

FontTextureAtlas rendering_font_create(Arena* arena, RenderingHandle* rendering, FontTextureAtlasCreate create)
{
    FontTextureAtlas result = {0};
    result.description = font_texture_atlas_create(arena, create);
    result.texture = rendering_texture_create(rendering, (TextureMemory) {
        .pointer = result.description.pointer,
        .width = result.description.width,
        .height = result.description.height,
        .depth = 1,
        .format = TEXTURE_FORMAT_R8G8B8A8_SRGB,
    });
    return result;
}

BUSTER_GLOBAL_LOCAL u32 rendering_window_pipeline_add_vertices(RenderingWindowHandle* window, BusterPipeline pipeline_index, ByteSlice vertex_memory, u32 vertex_count)
{
    WindowFrame* frame = rendering_window_frame(window);
    VertexBuffer* vertex_buffer = &frame->pipeline_instantiations[(u32)pipeline_index].vertex_buffer;
    u8* allocation = (u8*)arena_allocate_bytes(vertex_buffer->cpu, vertex_memory.length, 16);
    memcpy(allocation, vertex_memory.pointer, vertex_memory.length);
    u32 vertex_offset = vertex_buffer->count;
    vertex_buffer->count = vertex_offset + vertex_count;
    return vertex_offset;
}

BUSTER_GLOBAL_LOCAL void rendering_window_pipeline_add_indices(RenderingWindowHandle* window, BusterPipeline pipeline_index, Sliceu32 indices)
{
    WindowFrame* frame = rendering_window_frame(window);
    IndexBuffer* index_buffer = &frame->pipeline_instantiations[(u32)pipeline_index].index_buffer;
    u32* allocation = arena_allocate(index_buffer->cpu, u32, indices.length);
    memcpy(allocation, indices.pointer, indices.length * sizeof(*indices.pointer));
}

void rendering_window_render_rect(RenderingWindowHandle* window, RectDraw draw)
{
    float2 p0 = draw.vertex.p0;
    float2 uv0 = draw.texture.p0;

    if (float2_element(draw.texture.p1, 0) != 0)
    {
        BUSTER_CHECK(float2_element(draw.texture.p1, 0) - float2_element(draw.texture.p0, 0) == float2_element(draw.vertex.p1, 0) - float2_element(draw.vertex.p0, 0));
        BUSTER_CHECK(float2_element(draw.texture.p1, 1) - float2_element(draw.texture.p0, 1) == float2_element(draw.vertex.p1, 1) - float2_element(draw.vertex.p0, 1));
    }

    f32 corner_radius = 5.0f;
    float2 extent = float2_make(
        float2_element(draw.vertex.p1, 0) - float2_element(p0, 0),
        float2_element(draw.vertex.p1, 1) - float2_element(p0, 1)
    );
    RectVertex vertices[] = {
        {
            .p0 = p0,
            .uv0 = uv0,
            .extent = extent,
            .texture_index = draw.texture_index,
            .colors = { draw.colors[0], draw.colors[1], draw.colors[2], draw.colors[3] },
            .softness = 1.0,
            .corner_radius = corner_radius,
        },
        {
            .p0 = p0,
            .uv0 = uv0,
            .extent = extent,
            .texture_index = draw.texture_index,
            .colors = { draw.colors[0], draw.colors[1], draw.colors[2], draw.colors[3] },
            .softness = 1.0,
            .corner_radius = corner_radius,
        },
        {
            .p0 = p0,
            .uv0 = uv0,
            .extent = extent,
            .texture_index = draw.texture_index,
            .colors = { draw.colors[0], draw.colors[1], draw.colors[2], draw.colors[3] },
            .softness = 1.0,
            .corner_radius = corner_radius,
        },
        {
            .p0 = p0,
            .uv0 = uv0,
            .extent = extent,
            .texture_index = draw.texture_index,
            .colors = { draw.colors[0], draw.colors[1], draw.colors[2], draw.colors[3] },
            .softness = 1.0,
            .corner_radius = corner_radius,
        },
    };

    u32 vertex_offset = rendering_window_pipeline_add_vertices(window, BUSTER_PIPELINE_RECT, BUSTER_ARRAY_TO_BYTE_SLICE(vertices), BUSTER_ARRAY_LENGTH(vertices));
    u32 indices[] = {
        vertex_offset + 0,
        vertex_offset + 1,
        vertex_offset + 2,
        vertex_offset + 1,
        vertex_offset + 3,
        vertex_offset + 2,
    };
    rendering_window_pipeline_add_indices(window, BUSTER_PIPELINE_RECT, (Sliceu32)BUSTER_ARRAY_TO_SLICE(indices));
}

void rendering_window_render_text(RenderingHandle* rendering, RenderingWindowHandle* window, String8 string, float4 color, RenderFontType font_type, f32 x_offset, f32 y_offset)
{
    FontTextureAtlas* texture_atlas = &rendering->fonts[(u32)font_type];
    s32 height = texture_atlas->description.ascent - texture_atlas->description.descent;
    u32 texture_index = texture_atlas->texture.value;

    for (u64 i = 0; i < string.length; i += 1)
    {
        u32 ch = (u32)string.pointer[i];
        FontCharacter* character = &texture_atlas->description.characters[ch];
        vec2 p0 = float2_make(x_offset, y_offset + (f32)(character->y_offset + height + texture_atlas->description.descent));
        vec2 uv0 = float2_make((f32)character->x, (f32)character->y);
        vec2 extent = float2_make((f32)character->width, (f32)character->height);
        RectVertex vertices[] = {
            { .p0 = p0, .uv0 = uv0, .extent = extent, .texture_index = texture_index, .colors = { color, color, color, color }, .softness = 1.0 },
            { .p0 = p0, .uv0 = uv0, .extent = extent, .texture_index = texture_index, .colors = { color, color, color, color }, .softness = 1.0 },
            { .p0 = p0, .uv0 = uv0, .extent = extent, .texture_index = texture_index, .colors = { color, color, color, color }, .softness = 1.0 },
            { .p0 = p0, .uv0 = uv0, .extent = extent, .texture_index = texture_index, .colors = { color, color, color, color }, .softness = 1.0 },
        };
        u32 vertex_offset = rendering_window_pipeline_add_vertices(window, BUSTER_PIPELINE_RECT, BUSTER_ARRAY_TO_BYTE_SLICE(vertices), BUSTER_ARRAY_LENGTH(vertices));
        u32 indices[] = { vertex_offset + 0, vertex_offset + 1, vertex_offset + 2, vertex_offset + 1, vertex_offset + 3, vertex_offset + 2 };
        rendering_window_pipeline_add_indices(window, BUSTER_PIPELINE_RECT, (Sliceu32)BUSTER_ARRAY_TO_SLICE(indices));

        s32 kerning = 0;
        if (i + 1 < string.length)
        {
            kerning = (texture_atlas->description.kerning_tables + ch * 256)[(u32)string.pointer[i + 1]];
        }
        x_offset += (f32)character->advance + (f32)kerning;
    }
}
#endif
