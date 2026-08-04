#include <buster/lib/rendering/internal.h>

BUSTER_TEST_F_DECL bool rendering_vulkan_window_requires_device_initialization(bool device_initialized)
{
    return !device_initialized;
}

BUSTER_TEST_F_DECL bool rendering_vulkan_existing_surface_is_compatible(RenderingVulkanSurfaceCompatibility compatibility)
{
    return compatibility.queue_setup && compatibility.present_queue && compatibility.capabilities && compatibility.format && compatibility.present_modes &&
           compatibility.usage && compatibility.image_count && compatibility.composite_alpha;
}

BUSTER_TEST_F_DECL bool rendering_vulkan_surface_format_sentinel_is_compatible(u32 available_color_space, u32 selected_color_space)
{
    return available_color_space == selected_color_space;
}

BUSTER_TEST_F_DECL bool rendering_vulkan_enumeration_needs_retry(bool incomplete, u32 capacity, u32 reported_count)
{
    return incomplete || reported_count > capacity;
}

BUSTER_TEST_F_DECL bool rendering_vulkan_queue_family_enumeration_needs_retry(u32 capacity, u32 reported_count, u32 available_count)
{
    return reported_count > capacity || available_count > capacity || available_count > reported_count;
}

BUSTER_TEST_F_DECL RenderingVulkanQueueFamilySelection rendering_vulkan_select_queue_families(RenderingVulkanQueueFamilyCandidateSlice candidates)
{
    RenderingVulkanQueueFamilySelection result = {
        .graphics_family_index = 0,
        .present_family_index = 0,
        .eligible = false,
    };

    for (u32 i = 0; i < candidates.length; i += 1)
    {
        RenderingVulkanQueueFamilyCandidate candidate = candidates.pointer[i];
        if (candidate.queue_count && candidate.graphics && candidate.present)
        {
            result.graphics_family_index = i;
            result.present_family_index = i;
            result.eligible = true;
            return result;
        }
    }

    u32 graphics_family_index = 0;
    u32 present_family_index = 0;
    bool has_graphics = false;
    bool has_present = false;
    for (u32 i = 0; i < candidates.length; i += 1)
    {
        RenderingVulkanQueueFamilyCandidate candidate = candidates.pointer[i];
        if (candidate.queue_count && candidate.graphics && !has_graphics)
        {
            graphics_family_index = i;
            has_graphics = true;
        }
        if (candidate.queue_count && candidate.present && !has_present)
        {
            present_family_index = i;
            has_present = true;
        }
    }

    if (has_graphics && has_present)
    {
        result.graphics_family_index = graphics_family_index;
        result.present_family_index = present_family_index;
        result.eligible = true;
    }
    return result;
}

BUSTER_TEST_F_DECL bool rendering_vulkan_device_candidate_is_eligible(RenderingVulkanDeviceCandidate candidate)
{
    return !candidate.excluded && candidate.has_required_extension && candidate.has_required_features && candidate.has_surface_support &&
           candidate.queues.eligible;
}

BUSTER_TEST_F_DECL u64 rendering_vulkan_device_score(RenderingVulkanDeviceCandidate candidate)
{
    u64 score = 0;
    switch (candidate.device_type)
    {
    case RENDERING_VULKAN_DEVICE_TYPE_DISCRETE:
        score = 1000000;
        break;
    case RENDERING_VULKAN_DEVICE_TYPE_INTEGRATED:
        score = 500000;
        break;
    case RENDERING_VULKAN_DEVICE_TYPE_VIRTUAL:
        score = 250000;
        break;
    case RENDERING_VULKAN_DEVICE_TYPE_CPU:
        score = 100000;
        break;
    case RENDERING_VULKAN_DEVICE_TYPE_OTHER:
        score = 0;
        break;
    case RENDERING_VULKAN_DEVICE_TYPE_COUNT:
        BUSTER_UNREACHABLE();
    }

    if (candidate.queues.graphics_family_index == candidate.queues.present_family_index)
    {
        score += 10000;
    }
    return score;
}

BUSTER_TEST_F_DECL bool rendering_vulkan_device_is_better(RenderingVulkanDeviceCandidate* candidate, RenderingVulkanDeviceCandidate* current,
                                                          u64 candidate_score, u64 current_score)
{
    if (candidate_score != current_score)
    {
        return candidate_score > current_score;
    }

    u64 common_name_length = candidate->name.length < current->name.length ? candidate->name.length : current->name.length;
    for (u64 name_index = 0; name_index < common_name_length; name_index += 1)
    {
        u8 candidate_code_unit = (u8)candidate->name.pointer[name_index];
        u8 current_code_unit = (u8)current->name.pointer[name_index];
        if (candidate_code_unit < current_code_unit)
        {
            return true;
        }
        if (candidate_code_unit > current_code_unit)
        {
            return false;
        }
    }
    if (candidate->name.length != current->name.length)
    {
        return candidate->name.length < current->name.length;
    }
    if (candidate->vendor_id != current->vendor_id)
    {
        return candidate->vendor_id < current->vendor_id;
    }
    if (candidate->device_id != current->device_id)
    {
        return candidate->device_id < current->device_id;
    }
    return candidate->enumeration_index < current->enumeration_index;
}

BUSTER_TEST_F_DECL RenderingVulkanDeviceSelection rendering_vulkan_select_device(RenderingVulkanDeviceCandidateSlice candidates)
{
    RenderingVulkanDeviceSelection result = {
        .candidate_index = 0,
        .score = 0,
        .found = false,
    };

    for (u32 i = 0; i < candidates.length; i += 1)
    {
        RenderingVulkanDeviceCandidate candidate = candidates.pointer[i];
        if (!rendering_vulkan_device_candidate_is_eligible(candidate))
        {
            continue;
        }

        u64 score = rendering_vulkan_device_score(candidate);
        if (!result.found)
        {
            result.candidate_index = i;
            result.score = score;
            result.found = true;
        }
        else
        {
            RenderingVulkanDeviceCandidate current = candidates.pointer[result.candidate_index];
            bool candidate_is_better = false;
            candidate_is_better = rendering_vulkan_device_is_better(&candidate, &current, score, result.score);
            if (candidate_is_better)
            {
                result.candidate_index = i;
                result.score = score;
                result.found = true;
            }
        }
    }
    return result;
}

#if BUSTER_USE_VULKAN
#include <buster/lib/rendering/vulkan.c>
#elif defined(_WIN32) && BUSTER_USE_D3D12
#include <buster/lib/rendering/d3d12.c>
#elif defined(__APPLE__)
#include <buster/lib/rendering/metal.c>
#else
#include <buster/lib/rendering/null.c>
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

    return rendering_texture_create(rendering, (TextureMemory){
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
    result.texture = rendering_texture_create(rendering, (TextureMemory){
                                                             .pointer = result.description.pointer,
                                                             .width = result.description.width,
                                                             .height = result.description.height,
                                                             .depth = 1,
                                                             .format = TEXTURE_FORMAT_R8G8B8A8_SRGB,
                                                         });
    return result;
}

BUSTER_GLOBAL_LOCAL u32 rendering_window_pipeline_add_vertices(RenderingWindowHandle* window, BusterPipeline pipeline_index, ByteSlice vertex_memory,
                                                               u32 vertex_count)
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
        BUSTER_CHECK(float2_element(draw.texture.p1, 0) - float2_element(draw.texture.p0, 0) ==
                     float2_element(draw.vertex.p1, 0) - float2_element(draw.vertex.p0, 0));
        BUSTER_CHECK(float2_element(draw.texture.p1, 1) - float2_element(draw.texture.p0, 1) ==
                     float2_element(draw.vertex.p1, 1) - float2_element(draw.vertex.p0, 1));
    }

    f32 corner_radius = 5.0f;
    float2 extent = float2_make(float2_element(draw.vertex.p1, 0) - float2_element(p0, 0), float2_element(draw.vertex.p1, 1) - float2_element(p0, 1));
    RectVertex vertices[] = {
        {
            .p0 = p0,
            .uv0 = uv0,
            .extent = extent,
            .texture_index = draw.texture_index,
            .colors = {draw.colors[0], draw.colors[1], draw.colors[2], draw.colors[3]},
            .softness = 1.0,
            .corner_radius = corner_radius,
        },
        {
            .p0 = p0,
            .uv0 = uv0,
            .extent = extent,
            .texture_index = draw.texture_index,
            .colors = {draw.colors[0], draw.colors[1], draw.colors[2], draw.colors[3]},
            .softness = 1.0,
            .corner_radius = corner_radius,
        },
        {
            .p0 = p0,
            .uv0 = uv0,
            .extent = extent,
            .texture_index = draw.texture_index,
            .colors = {draw.colors[0], draw.colors[1], draw.colors[2], draw.colors[3]},
            .softness = 1.0,
            .corner_radius = corner_radius,
        },
        {
            .p0 = p0,
            .uv0 = uv0,
            .extent = extent,
            .texture_index = draw.texture_index,
            .colors = {draw.colors[0], draw.colors[1], draw.colors[2], draw.colors[3]},
            .softness = 1.0,
            .corner_radius = corner_radius,
        },
    };

    u32 vertex_offset =
        rendering_window_pipeline_add_vertices(window, BUSTER_PIPELINE_RECT, BUSTER_ARRAY_TO_BYTE_SLICE(vertices), BUSTER_ARRAY_LENGTH(vertices));
    u32 indices[] = {
        vertex_offset + 0, vertex_offset + 1, vertex_offset + 2, vertex_offset + 1, vertex_offset + 3, vertex_offset + 2,
    };
    rendering_window_pipeline_add_indices(window, BUSTER_PIPELINE_RECT, (Sliceu32)BUSTER_ARRAY_TO_SLICE(indices));
}

void rendering_window_render_text(RenderingHandle* rendering, RenderingWindowHandle* window, String8 string, float4 color, RenderFontType font_type,
                                  f32 x_offset, f32 y_offset)
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
            {.p0 = p0, .uv0 = uv0, .extent = extent, .texture_index = texture_index, .colors = {color, color, color, color}, .softness = 1.0},
            {.p0 = p0, .uv0 = uv0, .extent = extent, .texture_index = texture_index, .colors = {color, color, color, color}, .softness = 1.0},
            {.p0 = p0, .uv0 = uv0, .extent = extent, .texture_index = texture_index, .colors = {color, color, color, color}, .softness = 1.0},
            {.p0 = p0, .uv0 = uv0, .extent = extent, .texture_index = texture_index, .colors = {color, color, color, color}, .softness = 1.0},
        };
        u32 vertex_offset =
            rendering_window_pipeline_add_vertices(window, BUSTER_PIPELINE_RECT, BUSTER_ARRAY_TO_BYTE_SLICE(vertices), BUSTER_ARRAY_LENGTH(vertices));
        u32 indices[] = {vertex_offset + 0, vertex_offset + 1, vertex_offset + 2, vertex_offset + 1, vertex_offset + 3, vertex_offset + 2};
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
