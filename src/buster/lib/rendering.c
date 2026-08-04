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

BUSTER_GLOBAL_LOCAL int rendering_vulkan_device_name_compare(String8 left, String8 right)
{
    u64 common_length = left.length < right.length ? left.length : right.length;
    for (u64 i = 0; i < common_length; i += 1)
    {
        u8 left_code_unit = (u8)left.pointer[i];
        u8 right_code_unit = (u8)right.pointer[i];
        if (left_code_unit < right_code_unit)
        {
            return -1;
        }
        if (left_code_unit > right_code_unit)
        {
            return 1;
        }
    }
    if (left.length < right.length)
    {
        return -1;
    }
    if (left.length > right.length)
    {
        return 1;
    }
    return 0;
}

BUSTER_GLOBAL_LOCAL bool rendering_vulkan_device_is_better(RenderingVulkanDeviceCandidate candidate, RenderingVulkanDeviceCandidate current,
                                                           u64 candidate_score, u64 current_score)
{
    if (candidate_score != current_score)
    {
        return candidate_score > current_score;
    }

    int name_comparison = rendering_vulkan_device_name_compare(candidate.name, current.name);
    if (name_comparison != 0)
    {
        return name_comparison < 0;
    }
    if (candidate.vendor_id != current.vendor_id)
    {
        return candidate.vendor_id < current.vendor_id;
    }
    if (candidate.device_id != current.device_id)
    {
        return candidate.device_id < current.device_id;
    }
    return candidate.enumeration_index < current.enumeration_index;
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
        if (!result.found || rendering_vulkan_device_is_better(candidate, candidates.pointer[result.candidate_index], score, result.score))
        {
            result.candidate_index = i;
            result.score = score;
            result.found = true;
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

BUSTER_GLOBAL_LOCAL bool rendering_scale_is_valid(RenderingScale scale)
{
    return scale.x > 0.0f && scale.y > 0.0f;
}

BUSTER_GLOBAL_LOCAL s32 rendering_clip_coordinate(f64 value, bool round_up)
{
    if (value <= (f64)INT32_MIN)
    {
        return INT32_MIN;
    }
    if (value >= (f64)INT32_MAX)
    {
        return INT32_MAX;
    }
    f64 rounded = round_up ? ceil_f64(value) : floor_f64(value);
    if (rounded <= (f64)INT32_MIN)
    {
        return INT32_MIN;
    }
    if (rounded >= (f64)INT32_MAX)
    {
        return INT32_MAX;
    }
    return (s32)rounded;
}

BUSTER_GLOBAL_LOCAL s32 rendering_clip_target_extent(u32 extent)
{
    return extent > (u32)INT32_MAX ? INT32_MAX : (s32)extent;
}

BUSTER_GLOBAL_LOCAL RenderingClipRect rendering_clip_rect_target(RenderingWindowSize target_size)
{
    return (RenderingClipRect){
        .x0 = 0,
        .y0 = 0,
        .x1 = rendering_clip_target_extent(target_size.width),
        .y1 = rendering_clip_target_extent(target_size.height),
    };
}

bool rendering_clip_rect_is_empty(RenderingClipRect rect)
{
    return rect.x1 <= rect.x0 || rect.y1 <= rect.y0;
}

RenderingClipRect rendering_clip_rect_intersect(RenderingClipRect a, RenderingClipRect b)
{
    RenderingClipRect result = {
        .x0 = a.x0 > b.x0 ? a.x0 : b.x0,
        .y0 = a.y0 > b.y0 ? a.y0 : b.y0,
        .x1 = a.x1 < b.x1 ? a.x1 : b.x1,
        .y1 = a.y1 < b.y1 ? a.y1 : b.y1,
    };
    return result;
}

RenderingClipRect rendering_clip_rect_from_f32(F32Interval2 rect, RenderingScale scale, RenderingWindowSize target_size)
{
    RenderingClipRect result = {0};
    if (!rendering_scale_is_valid(scale))
    {
        scale = (RenderingScale){.x = 1.0f, .y = 1.0f};
    }

    f64 x0 = (f64)rect.x0 * (f64)scale.x;
    f64 x1 = (f64)rect.x1 * (f64)scale.x;
    f64 y0 = (f64)rect.y0 * (f64)scale.y;
    f64 y1 = (f64)rect.y1 * (f64)scale.y;
    if (x0 != x0 || x1 != x1 || y0 != y0 || y1 != y1)
    {
        return result;
    }

    f64 min_x = x0 < x1 ? x0 : x1;
    f64 max_x = x0 < x1 ? x1 : x0;
    f64 min_y = y0 < y1 ? y0 : y1;
    f64 max_y = y0 < y1 ? y1 : y0;
    result.x0 = rendering_clip_coordinate(min_x, false);
    result.y0 = rendering_clip_coordinate(min_y, false);
    result.x1 = rendering_clip_coordinate(max_x, true);
    result.y1 = rendering_clip_coordinate(max_y, true);
    result = rendering_clip_rect_intersect(result, rendering_clip_rect_target(target_size));
    return result;
}

BUSTER_GLOBAL_LOCAL bool rendering_clip_rect_equal(RenderingClipRect a, RenderingClipRect b)
{
    return a.x0 == b.x0 && a.y0 == b.y0 && a.x1 == b.x1 && a.y1 == b.y1;
}

BUSTER_GLOBAL_LOCAL void rendering_command_stream_mark_overflow(RenderingCommandStream* stream)
{
    if (stream)
    {
        stream->overflowed = true;
    }
}

BUSTER_GLOBAL_LOCAL void rendering_command_stream_ensure_clip_root(RenderingCommandStream* stream)
{
    if (stream && stream->clip_depth == 0)
    {
        if (!rendering_scale_is_valid(stream->scale))
        {
            stream->scale = (RenderingScale){.x = 1.0f, .y = 1.0f};
        }
        stream->clip_depth = 1;
        stream->clip_stack[0] = rendering_clip_rect_target(stream->target_size);
    }
}

void rendering_command_stream_bind_buffers(RenderingCommandStream* stream, Arena* vertex_cpu, Arena* index_cpu)
{
    if (!stream)
    {
        return;
    }
    stream->vertex_cpu = vertex_cpu;
    stream->index_cpu = index_cpu;
}

void rendering_command_stream_begin(RenderingCommandStream* stream, RenderingWindowSize target_size, RenderingScale scale)
{
    if (!stream)
    {
        return;
    }

    stream->target_size = target_size;
    stream->scale = rendering_scale_is_valid(scale) ? scale : (RenderingScale){.x = 1.0f, .y = 1.0f};
    stream->command_count = 0;
    stream->batch_count = 0;
    stream->clip_depth = 1;
    stream->vertex_count = 0;
    stream->index_count = 0;
    stream->force_new_batch = true;
    stream->overflowed = false;
    stream->clip_stack[0] = rendering_clip_rect_target(target_size);
    if (stream->vertex_cpu)
    {
        arena_reset_to_start(stream->vertex_cpu);
    }
    if (stream->index_cpu)
    {
        arena_reset_to_start(stream->index_cpu);
    }
}

void rendering_command_stream_set_scale(RenderingCommandStream* stream, RenderingScale scale)
{
    if (!stream)
    {
        return;
    }
    stream->scale = rendering_scale_is_valid(scale) ? scale : (RenderingScale){.x = 1.0f, .y = 1.0f};
    stream->clip_depth = 1;
    stream->clip_stack[0] = rendering_clip_rect_target(stream->target_size);
    stream->force_new_batch = true;
}

void rendering_command_stream_push_clip(RenderingCommandStream* stream, F32Interval2 rect)
{
    if (!stream)
    {
        return;
    }
    rendering_command_stream_ensure_clip_root(stream);
    if (stream->clip_depth >= RENDERING_MAX_CLIP_DEPTH)
    {
        rendering_command_stream_mark_overflow(stream);
        return;
    }
    RenderingClipRect requested = rendering_clip_rect_from_f32(rect, stream->scale, stream->target_size);
    RenderingClipRect parent = stream->clip_stack[stream->clip_depth - 1];
    RenderingClipRect clip = rendering_clip_rect_intersect(parent, requested);
    stream->clip_stack[stream->clip_depth] = clip;
    stream->clip_depth += 1;
    rendering_command_stream_record_clip(stream, RENDERING_COMMAND_CLIP_PUSH, clip);
}

void rendering_command_stream_pop_clip(RenderingCommandStream* stream)
{
    if (!stream)
    {
        return;
    }
    rendering_command_stream_ensure_clip_root(stream);
    if (stream->clip_depth > 1)
    {
        stream->clip_depth -= 1;
    }
    else
    {
        return;
    }
    RenderingClipRect clip = stream->clip_stack[stream->clip_depth - 1];
    rendering_command_stream_record_clip(stream, RENDERING_COMMAND_CLIP_POP, clip);
}

void rendering_command_stream_reset_clip(RenderingCommandStream* stream)
{
    if (!stream)
    {
        return;
    }
    stream->clip_depth = 1;
    stream->clip_stack[0] = rendering_clip_rect_target(stream->target_size);
    rendering_command_stream_record_flush(stream);
}

BUSTER_GLOBAL_LOCAL bool rendering_command_stream_push_command(RenderingCommandStream* stream, RenderingCommand command)
{
    if (!stream || stream->command_count >= RENDERING_MAX_DRAW_COUNT)
    {
        if (stream)
        {
            rendering_command_stream_mark_overflow(stream);
        }
        return false;
    }
    stream->commands[stream->command_count] = command;
    stream->command_count += 1;
    return true;
}

BUSTER_GLOBAL_LOCAL bool rendering_command_stream_push_batch(RenderingCommandStream* stream, RenderingCommand command)
{
    if (stream->batch_count >= RENDERING_MAX_BATCH_COUNT)
    {
        rendering_command_stream_mark_overflow(stream);
        return false;
    }

    RenderingBatch* batch = &stream->batches[stream->batch_count];
    *batch = (RenderingBatch){
        .pipeline = command.pipeline,
        .first_index = command.first_index,
        .index_count = command.index_count,
        .texture = command.texture,
        .clip = command.clip,
    };
    stream->batch_count += 1;
    return true;
}

void rendering_command_stream_record_rect(RenderingCommandStream* stream, BusterPipeline pipeline, TextureIndex texture, u32 first_index,
                                           u32 index_count)
{
    if (!stream)
    {
        return;
    }

    rendering_command_stream_ensure_clip_root(stream);
    RenderingClipRect clip = stream->clip_stack[stream->clip_depth - 1];
    RenderingCommand command = {
        .kind = RENDERING_COMMAND_RECT,
        .pipeline = pipeline,
        .first_index = first_index,
        .index_count = index_count,
        .texture = texture,
        .clip = clip,
    };
    if (!rendering_command_stream_push_command(stream, command))
    {
        return;
    }

    if (index_count == 0 || rendering_clip_rect_is_empty(clip))
    {
        stream->force_new_batch = true;
        return;
    }

    bool compatible = !stream->force_new_batch && stream->batch_count != 0;
    if (compatible)
    {
        RenderingBatch* previous = &stream->batches[stream->batch_count - 1];
        compatible = previous->pipeline == command.pipeline && previous->texture.value == command.texture.value &&
                     rendering_clip_rect_equal(previous->clip, command.clip) && previous->first_index + previous->index_count == command.first_index;
    }
    if (compatible)
    {
        stream->batches[stream->batch_count - 1].index_count += index_count;
    }
    else
    {
        rendering_command_stream_push_batch(stream, command);
    }
    stream->force_new_batch = false;
}

void rendering_command_stream_record_clip(RenderingCommandStream* stream, RenderingCommandKind kind, RenderingClipRect clip)
{
    if (!stream)
    {
        return;
    }
    rendering_command_stream_push_command(stream, (RenderingCommand){.kind = kind, .pipeline = BUSTER_PIPELINE_RECT, .clip = clip});
    stream->force_new_batch = true;
}

void rendering_command_stream_record_flush(RenderingCommandStream* stream)
{
    if (!stream)
    {
        return;
    }
    rendering_command_stream_push_command(stream, (RenderingCommand){.kind = RENDERING_COMMAND_FLUSH, .pipeline = BUSTER_PIPELINE_RECT});
    stream->force_new_batch = true;
}

void rendering_window_set_content_scale(RenderingWindowHandle* window, RenderingScale scale)
{
    if (window)
    {
        rendering_window_set_content_scale_internal(window, scale);
    }
}

void rendering_window_clip_push(RenderingWindowHandle* window, F32Interval2 rect)
{
    RenderingCommandStream* stream = window ? rendering_window_command_stream(window) : 0;
    rendering_command_stream_push_clip(stream, rect);
}

void rendering_window_clip_pop(RenderingWindowHandle* window)
{
    RenderingCommandStream* stream = window ? rendering_window_command_stream(window) : 0;
    rendering_command_stream_pop_clip(stream);
}

void rendering_window_clip_reset(RenderingWindowHandle* window)
{
    RenderingCommandStream* stream = window ? rendering_window_command_stream(window) : 0;
    rendering_command_stream_reset_clip(stream);
}

void rendering_window_flush(RenderingWindowHandle* window)
{
    if (window)
    {
        rendering_command_stream_record_flush(rendering_window_command_stream(window));
    }
}

#if BUSTER_USE_VULKAN || (defined(_WIN32) && BUSTER_USE_D3D12) || defined(__APPLE__)
RenderingWindowSize rendering_window_get_size(RenderingWindowHandle* window)
{
    return window ? (RenderingWindowSize){
                       .width = window->width,
                       .height = window->height,
                   }
                 : (RenderingWindowSize){0};
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

#endif

BUSTER_GLOBAL_LOCAL u32 rendering_window_pipeline_add_vertices(RenderingWindowHandle* window, BusterPipeline pipeline_index, ByteSlice vertex_memory,
                                                               u32 vertex_count)
{
    RenderingCommandStream* stream = rendering_window_command_stream(window);
    if (!stream || pipeline_index >= BUSTER_PIPELINE_COUNT || stream->vertex_count > RENDERING_MAX_VERTEX_COUNT ||
        vertex_count > RENDERING_MAX_VERTEX_COUNT - stream->vertex_count || !stream->vertex_cpu)
    {
        if (stream)
        {
            rendering_command_stream_mark_overflow(stream);
        }
        return UINT32_MAX;
    }
    u8* allocation = (u8*)arena_allocate_bytes(stream->vertex_cpu, vertex_memory.length, 16);
    memcpy(allocation, vertex_memory.pointer, vertex_memory.length);
    u32 vertex_offset = stream->vertex_count;
    stream->vertex_count += vertex_count;
    return vertex_offset;
}

BUSTER_GLOBAL_LOCAL u32 rendering_window_pipeline_add_indices(RenderingWindowHandle* window, BusterPipeline pipeline_index, Sliceu32 indices)
{
    RenderingCommandStream* stream = rendering_window_command_stream(window);
    if (!stream || pipeline_index >= BUSTER_PIPELINE_COUNT || stream->index_count > RENDERING_MAX_INDEX_COUNT ||
        indices.length > RENDERING_MAX_INDEX_COUNT - stream->index_count || !stream->index_cpu ||
        indices.length > UINT32_MAX)
    {
        if (stream)
        {
            rendering_command_stream_mark_overflow(stream);
        }
        return UINT32_MAX;
    }
    u32 first_index = stream->index_count;
    u32* allocation = arena_allocate(stream->index_cpu, u32, indices.length);
    memcpy(allocation, indices.pointer, indices.length * sizeof(*indices.pointer));
    stream->index_count += (u32)indices.length;
    return first_index;
}

void rendering_window_render_rect(RenderingWindowHandle* window, RectDraw draw)
{
    if (!window)
    {
        return;
    }
    RenderingCommandStream* stream = rendering_window_command_stream(window);
    if (!stream)
    {
        return;
    }

    f32 x0 = draw.vertex.x0;
    f32 x1 = draw.vertex.x1;
    f32 y0 = draw.vertex.y0;
    f32 y1 = draw.vertex.y1;
    f32 uv_x0 = draw.texture.x0;
    f32 uv_x1 = draw.texture.x1;
    f32 uv_y0 = draw.texture.y0;
    f32 uv_y1 = draw.texture.y1;
    if (x1 < x0)
    {
        f32 swap = x0;
        x0 = x1;
        x1 = swap;
        swap = uv_x0;
        uv_x0 = uv_x1;
        uv_x1 = swap;
    }
    if (y1 < y0)
    {
        f32 swap = y0;
        y0 = y1;
        y1 = swap;
        swap = uv_y0;
        uv_y0 = uv_y1;
        uv_y1 = swap;
    }
    f32 scale_x = stream->scale.x;
    f32 scale_y = stream->scale.y;
    float2 p0 = float2_make(x0 * scale_x, y0 * scale_y);
    float2 uv0 = float2_make(uv_x0, uv_y0);
    float2 extent = float2_make((x1 - x0) * scale_x, (y1 - y0) * scale_y);
    float2 uv_extent = float2_make(uv_x1 - uv_x0, uv_y1 - uv_y0);
    f32 corner_radius = 5.0f * (scale_x < scale_y ? scale_x : scale_y);
    RectVertex vertices[] = {
        {
            .p0 = p0,
            .uv0 = uv0,
            .extent = extent,
            .uv_extent = uv_extent,
            .texture_index = draw.texture_index,
            .colors = {draw.colors[0], draw.colors[1], draw.colors[2], draw.colors[3]},
            .softness = 1.0,
            .corner_radius = corner_radius,
        },
        {
            .p0 = p0,
            .uv0 = uv0,
            .extent = extent,
            .uv_extent = uv_extent,
            .texture_index = draw.texture_index,
            .colors = {draw.colors[0], draw.colors[1], draw.colors[2], draw.colors[3]},
            .softness = 1.0,
            .corner_radius = corner_radius,
        },
        {
            .p0 = p0,
            .uv0 = uv0,
            .extent = extent,
            .uv_extent = uv_extent,
            .texture_index = draw.texture_index,
            .colors = {draw.colors[0], draw.colors[1], draw.colors[2], draw.colors[3]},
            .softness = 1.0,
            .corner_radius = corner_radius,
        },
        {
            .p0 = p0,
            .uv0 = uv0,
            .extent = extent,
            .uv_extent = uv_extent,
            .texture_index = draw.texture_index,
            .colors = {draw.colors[0], draw.colors[1], draw.colors[2], draw.colors[3]},
            .softness = 1.0,
            .corner_radius = corner_radius,
        },
    };

    u32 vertex_offset =
        rendering_window_pipeline_add_vertices(window, BUSTER_PIPELINE_RECT, BUSTER_ARRAY_TO_BYTE_SLICE(vertices), BUSTER_ARRAY_LENGTH(vertices));
    if (vertex_offset == UINT32_MAX)
    {
        return;
    }
    u32 indices[] = {
        vertex_offset + 0, vertex_offset + 1, vertex_offset + 2, vertex_offset + 1, vertex_offset + 3, vertex_offset + 2,
    };
    u32 first_index = rendering_window_pipeline_add_indices(window, BUSTER_PIPELINE_RECT, (Sliceu32)BUSTER_ARRAY_TO_SLICE(indices));
    if (first_index != UINT32_MAX)
    {
        rendering_command_stream_record_rect(stream, BUSTER_PIPELINE_RECT, (TextureIndex){.value = draw.texture_index}, first_index, BUSTER_ARRAY_LENGTH(indices));
    }
}

void rendering_window_render_text(RenderingHandle* rendering, RenderingWindowHandle* window, String8 string, float4 color, RenderFontType font_type,
                                  f32 x_offset, f32 y_offset)
{
    if (!rendering || !window || (u32)font_type >= RENDER_FONT_TYPE_COUNT)
    {
        return;
    }
    FontTextureAtlas* texture_atlas = &rendering->fonts[(u32)font_type];
    if ((!string.pointer && string.length) || !texture_atlas->description.characters || !texture_atlas->description.kerning_tables)
    {
        return;
    }
    RenderingCommandStream* stream = rendering_window_command_stream(window);
    if (!stream)
    {
        return;
    }
    s32 height = texture_atlas->description.ascent - texture_atlas->description.descent;
    u32 texture_index = texture_atlas->texture.value;

    for (u64 i = 0; i < string.length; i += 1)
    {
        u32 ch = (u32)string.pointer[i];
        FontCharacter* character = &texture_atlas->description.characters[ch];
        f32 scale_x = stream->scale.x;
        f32 scale_y = stream->scale.y;
        vec2 p0 = float2_make(x_offset * scale_x, (y_offset + (f32)(character->y_offset + height + texture_atlas->description.descent)) * scale_y);
        vec2 uv0 = float2_make((f32)character->x, (f32)character->y);
        vec2 extent = float2_make((f32)character->width * scale_x, (f32)character->height * scale_y);
        vec2 uv_extent = float2_make((f32)character->width, (f32)character->height);
        RectVertex vertices[] = {
            {.p0 = p0, .uv0 = uv0, .extent = extent, .uv_extent = uv_extent, .texture_index = texture_index, .colors = {color, color, color, color}, .softness = 1.0},
            {.p0 = p0, .uv0 = uv0, .extent = extent, .uv_extent = uv_extent, .texture_index = texture_index, .colors = {color, color, color, color}, .softness = 1.0},
            {.p0 = p0, .uv0 = uv0, .extent = extent, .uv_extent = uv_extent, .texture_index = texture_index, .colors = {color, color, color, color}, .softness = 1.0},
            {.p0 = p0, .uv0 = uv0, .extent = extent, .uv_extent = uv_extent, .texture_index = texture_index, .colors = {color, color, color, color}, .softness = 1.0},
        };
        u32 vertex_offset =
            rendering_window_pipeline_add_vertices(window, BUSTER_PIPELINE_RECT, BUSTER_ARRAY_TO_BYTE_SLICE(vertices), BUSTER_ARRAY_LENGTH(vertices));
        if (vertex_offset == UINT32_MAX)
        {
            return;
        }
        u32 indices[] = {vertex_offset + 0, vertex_offset + 1, vertex_offset + 2, vertex_offset + 1, vertex_offset + 3, vertex_offset + 2};
        u32 first_index = rendering_window_pipeline_add_indices(window, BUSTER_PIPELINE_RECT, (Sliceu32)BUSTER_ARRAY_TO_SLICE(indices));
        if (first_index != UINT32_MAX)
        {
            rendering_command_stream_record_rect(stream, BUSTER_PIPELINE_RECT, (TextureIndex){.value = texture_index}, first_index,
                                                 BUSTER_ARRAY_LENGTH(indices));
        }

        s32 kerning = 0;
        if (i + 1 < string.length)
        {
            kerning = (texture_atlas->description.kerning_tables + ch * 256)[(u32)string.pointer[i + 1]];
        }
        x_offset += (f32)character->advance + (f32)kerning;
    }
}

BUSTER_GLOBAL_LOCAL bool rendering_blur_validate(Arena* scratch, u8* pixels, u32 width, u32 height, u32 stride, u32 channels, u32 radius)
{
    BUSTER_UNUSED(radius);
    if (!width || !height || !pixels || !scratch || stride < (u64)width * channels)
    {
        return false;
    }
    u64 pixel_count = (u64)width * height;
    return pixel_count <= RENDERING_MAX_BLUR_PIXELS && pixel_count <= UINT32_MAX / channels;
}

BUSTER_GLOBAL_LOCAL bool rendering_blur_bytes(Arena* scratch, u8* pixels, u32 width, u32 height, u32 stride, u32 channels, u32 radius)
{
    if (!rendering_blur_validate(scratch, pixels, width, height, stride, channels, radius))
    {
        return false;
    }
    if (radius == 0)
    {
        return true;
    }
    if (radius > RENDERING_MAX_BLUR_RADIUS)
    {
        radius = RENDERING_MAX_BLUR_RADIUS;
    }

    u64 pixel_count = (u64)width * height;
    u8* horizontal = (u8*)arena_allocate_bytes(scratch, pixel_count * channels, 16);
    u32 window_size = radius * 2 + 1;
    for (u32 y = 0; y < height; y += 1)
    {
        for (u32 x = 0; x < width; x += 1)
        {
            for (u32 channel = 0; channel < channels; channel += 1)
            {
                u32 sum = 0;
                for (u32 sample = 0; sample < window_size; sample += 1)
                {
                    s32 offset = (s32)sample - (s32)radius;
                    s32 sample_x = (s32)x + offset;
                    if (sample_x < 0)
                    {
                        sample_x = 0;
                    }
                    if (sample_x >= (s32)width)
                    {
                        sample_x = (s32)width - 1;
                    }
                    sum += pixels[(u64)y * stride + (u64)sample_x * channels + channel];
                }
                horizontal[((u64)y * width + x) * channels + channel] = (u8)((sum + window_size / 2) / window_size);
            }
        }
    }

    for (u32 y = 0; y < height; y += 1)
    {
        for (u32 x = 0; x < width; x += 1)
        {
            for (u32 channel = 0; channel < channels; channel += 1)
            {
                u32 sum = 0;
                for (u32 sample = 0; sample < window_size; sample += 1)
                {
                    s32 offset = (s32)sample - (s32)radius;
                    s32 sample_y = (s32)y + offset;
                    if (sample_y < 0)
                    {
                        sample_y = 0;
                    }
                    if (sample_y >= (s32)height)
                    {
                        sample_y = (s32)height - 1;
                    }
                    sum += horizontal[((u64)sample_y * width + x) * channels + channel];
                }
                pixels[(u64)y * stride + (u64)x * channels + channel] = (u8)((sum + window_size / 2) / window_size);
            }
        }
    }
    return true;
}

bool rendering_blur_rgba8(Arena* scratch, u8* pixels, u32 width, u32 height, u32 stride, u32 radius)
{
    return rendering_blur_bytes(scratch, pixels, width, height, stride, 4, radius);
}

BUSTER_GLOBAL_LOCAL bool rendering_blur_r8(Arena* scratch, u8* pixels, u32 width, u32 height, u32 stride, u32 radius)
{
    return rendering_blur_bytes(scratch, pixels, width, height, stride, 1, radius);
}

TextureIndex rendering_texture_create_blurred(Arena* arena, RenderingHandle* rendering, TextureMemory source, u32 radius)
{
    if (!arena || !rendering || !source.pointer || !source.width || !source.height || source.depth != 1)
    {
        return (TextureIndex){.value = UINT32_MAX};
    }
    u32 channels = source.format == TEXTURE_FORMAT_R8_UNORM ? 1 : source.format == TEXTURE_FORMAT_R8G8B8A8_SRGB ? 4 : 0;
    if (!channels || source.width > UINT32_MAX / channels || (u64)source.width * source.height > RENDERING_MAX_BLUR_PIXELS ||
        (u64)source.width * source.height > UINT32_MAX / channels)
    {
        return (TextureIndex){.value = UINT32_MAX};
    }
    if (radius == 0)
    {
        return rendering_texture_create(rendering, source);
    }
    u64 byte_count = (u64)source.width * source.height * channels;
    u8* copy = (u8*)arena_allocate_bytes(arena, byte_count, 16);
    memcpy(copy, source.pointer, byte_count);
    Arena* conflicts[] = {arena};
    TemporalArena scratch = scratch_begin(conflicts, BUSTER_ARRAY_LENGTH(conflicts));
    bool success = channels == 4 ? rendering_blur_rgba8(scratch.arena, copy, source.width, source.height, source.width * channels, radius)
                                 : rendering_blur_r8(scratch.arena, copy, source.width, source.height, source.width, radius);
    scratch_end(scratch);
    if (!success)
    {
        return (TextureIndex){.value = UINT32_MAX};
    }
    source.pointer = copy;
    return rendering_texture_create(rendering, source);
}
