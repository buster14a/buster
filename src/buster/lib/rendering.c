// Platform-neutral front door of the rendering module: draw-list
// generation, font/texture orchestration, and lifecycle policy shared by
// every backend. Native API calls live in the backend implementation files
// under rendering/ (Vulkan, Metal, ...), which this module selects and
// includes; backends consume window handles only through WmNativeSurface
// (AGENTS.md). Retained as opt-in infrastructure — the default build runs
// headless.

#include <buster/lib/rendering/internal.h>

bool rendering_vulkan_window_requires_device_initialization(bool device_initialized)
{
    return !device_initialized;
}

bool rendering_vulkan_existing_surface_is_compatible(RenderingVulkanSurfaceCompatibility compatibility)
{
    return compatibility.queue_setup && compatibility.present_queue && compatibility.capabilities && compatibility.format && compatibility.present_modes &&
           compatibility.usage && compatibility.image_count && compatibility.composite_alpha;
}

bool rendering_vulkan_surface_format_sentinel_is_compatible(u32 available_color_space, u32 selected_color_space)
{
    return available_color_space == selected_color_space;
}

bool rendering_vulkan_enumeration_needs_retry(bool incomplete, u32 capacity, u32 reported_count)
{
    return incomplete || reported_count > capacity;
}

bool rendering_vulkan_queue_family_enumeration_needs_retry(u32 capacity, u32 reported_count, u32 available_count)
{
    return reported_count > capacity || available_count > capacity || available_count > reported_count;
}

RenderingVulkanQueueFamilySelection rendering_vulkan_select_queue_families(RenderingVulkanQueueFamilyCandidateSlice candidates)
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

bool rendering_vulkan_device_candidate_is_eligible(RenderingVulkanDeviceCandidate candidate)
{
    return !candidate.excluded && candidate.has_required_extension && candidate.has_required_features && candidate.has_surface_support &&
           candidate.queues.eligible;
}

u64 rendering_vulkan_device_score(RenderingVulkanDeviceCandidate candidate)
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

RenderingVulkanDeviceSelection rendering_vulkan_select_device(RenderingVulkanDeviceCandidateSlice candidates)
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

bool rendering_scale_is_valid(RenderingScale scale)
{
    f32 maximum_finite_f32 = 3.402823466e+38f;
    return scale.x > 0.0f && scale.y > 0.0f && scale.x == scale.x && scale.y == scale.y && scale.x <= maximum_finite_f32 && scale.y <= maximum_finite_f32;
}

bool rendering_vulkan_device_functions_loaded_for_test(bool core_loaded, bool clear_attachments_loaded, bool blit_image_loaded)
{
    return core_loaded && clear_attachments_loaded && blit_image_loaded;
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
    if (rendering_clip_rect_is_empty(a) || rendering_clip_rect_is_empty(b))
    {
        return (RenderingClipRect){0};
    }
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
    if (x0 != x0 || x1 != x1 || y0 != y0 || y1 != y1 || rect.x1 <= rect.x0 || rect.y1 <= rect.y0)
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

void rendering_command_stream_mark_failure(RenderingCommandStream* stream)
{
    if (stream)
    {
        stream->render_failed = true;
    }
}

bool rendering_command_stream_is_valid(RenderingCommandStream* stream)
{
    return stream && !stream->overflowed && !stream->render_failed;
}

void rendering_backend_trace_begin(RenderingCommandStream* stream, RenderingBackendKind backend)
{
    if (!stream)
    {
        return;
    }
    stream->backend_trace = (RenderingBackendExecutionTrace){
        .backend = backend,
        .valid = true,
        .consumed_order_preserved = true,
        .resources_snapshot = true,
        .target_boundaries = true,
        .descriptor_snapshots = backend == RENDERING_BACKEND_VULKAN || backend == RENDERING_BACKEND_D3D12,
    };
}

void rendering_backend_trace_record_command(RenderingCommandStream* stream, u32 command_index)
{
    if (!stream)
    {
        return;
    }
    RenderingBackendExecutionTrace* trace = &stream->backend_trace;
    if (command_index != trace->consumed_command_count)
    {
        trace->consumed_order_preserved = false;
    }
    trace->consumed_command_count += 1;
    trace->backend_executed = true;
}

bool rendering_backend_trace_preflight(RenderingCommandStream* stream)
{
    if (!stream || !rendering_command_stream_is_valid(stream))
    {
        if (stream)
        {
            stream->backend_trace.valid = false;
        }
        return false;
    }
    for (u32 command_index = 0; command_index < stream->command_count; command_index += 1)
    {
        rendering_backend_trace_command(stream, command_index, stream->commands[command_index]);
        if (!rendering_command_stream_is_valid(stream))
        {
            break;
        }
    }
    return rendering_command_stream_is_valid(stream) && stream->backend_trace.valid;
}

bool rendering_backend_trace_validate_common(RenderingCommandStream* stream, u32 command_index, RenderingCommand command)
{
    bool valid = stream && command_index < stream->command_count;
    if (valid)
    {
        switch (command.kind)
        {
        case RENDERING_COMMAND_RECT:
            valid = command.pipeline < BUSTER_PIPELINE_COUNT && command.target == RENDERING_TARGET_BACKBUFFER;
            if (valid && rendering_command_stream_command_ends_batch(stream, command_index))
            {
                valid = command.batch_index < stream->batch_count;
                if (valid)
                {
                    RenderingBatch batch = stream->batches[command.batch_index];
                    valid = batch.pipeline < BUSTER_PIPELINE_COUNT && batch.target == RENDERING_TARGET_BACKBUFFER &&
                            memcmp(&batch.resources, &command.resources, sizeof(batch.resources)) == 0 && rendering_clip_rect_equal(batch.clip, command.clip);
                }
            }
            break;
        case RENDERING_COMMAND_RESOURCE:
            valid = command.resource_slot < RENDERING_RESOURCE_SLOT_COUNT && command.target == RENDERING_TARGET_BACKBUFFER;
            break;
        case RENDERING_COMMAND_TARGET:
            valid = command.target == RENDERING_TARGET_BACKBUFFER;
            break;
        case RENDERING_COMMAND_BACKGROUND_BLUR:
        {
            RenderingBlurPlan plan = rendering_blur_plan_make(stream->target_size, command.blur_rect, command.blur_radius);
            // A clipped-out blur is a successful no-op, just like a rect under
            // an empty scissor.  Non-empty malformed plans remain failures.
            valid = command.target == RENDERING_TARGET_BACKBUFFER && (plan.valid || rendering_clip_rect_is_empty(command.blur_rect));
            if (valid && plan.valid && plan.radius)
            {
                stream->backend_trace.blur_occurrence += 1;
                stream->backend_trace.blur_pass_count += 3;
                stream->backend_trace.blur_capture_pass_count += 1;
                stream->backend_trace.blur_horizontal_pass_count += 1;
                stream->backend_trace.blur_vertical_pass_count += 1;
                stream->backend_trace.state_restore_count += 1;
            }
            break;
        }
        case RENDERING_COMMAND_CLIP_PUSH:
        case RENDERING_COMMAND_CLIP_POP:
        case RENDERING_COMMAND_FLUSH:
            break;
        case RENDERING_COMMAND_KIND_COUNT:
            valid = false;
            break;
        }
    }
    if (!valid && stream)
    {
        stream->backend_trace.valid = false;
        rendering_command_stream_mark_failure(stream);
    }
    return valid;
}

void rendering_backend_trace_copy_result(RenderingBackendReplayResult* result, RenderingCommandStream* stream)
{
    if (!result || !stream)
    {
        return;
    }
    RenderingBackendExecutionTrace trace = stream->backend_trace;
    result->backend = trace.backend;
    result->valid = result->valid && trace.valid;
    result->blur_pass_count = trace.blur_pass_count;
    result->blur_capture_pass_count = trace.blur_capture_pass_count;
    result->blur_horizontal_pass_count = trace.blur_horizontal_pass_count;
    result->blur_vertical_pass_count = trace.blur_vertical_pass_count;
    result->descriptor_snapshot_count = trace.descriptor_snapshot_count;
    result->state_restore_count = trace.state_restore_count;
    result->consumed_command_count = trace.consumed_command_count;
    result->resources_snapshot = result->resources_snapshot && trace.resources_snapshot;
    result->target_boundaries = result->target_boundaries && trace.target_boundaries;
    result->state_restored = result->state_restored && trace.state_restored;
    result->backend_executed = trace.backend_executed;
    result->consumed_order_preserved = trace.consumed_order_preserved;
    result->failure_propagated = trace.failure_propagated;
    result->submitted = trace.submitted;
    result->presented = trace.presented;
    result->descriptor_snapshots = trace.descriptor_snapshots;
    result->error_frame = trace.error_frame;
}

void rendering_backend_trace_finish(RenderingCommandStream* stream, bool submitted, bool presented, bool error_frame)
{
    if (!stream)
    {
        return;
    }
    RenderingBackendExecutionTrace* trace = &stream->backend_trace;
    // An invalid stream is never replayed. GPU backends submit one complete
    // magenta clear as the deterministic error frame; null records failure
    // without pretending that a submission or presentation happened.
    trace->valid = trace->valid && rendering_command_stream_is_valid(stream);
    trace->state_restored = trace->valid && trace->state_restore_count == trace->blur_occurrence;
    trace->submitted = submitted;
    trace->presented = presented;
    trace->error_frame = error_frame || !trace->valid;
    trace->failure_propagated = !trace->valid;
}

void rendering_frame_error_commit(bool* last_frame_error, bool frame_error)
{
    if (last_frame_error)
    {
        *last_frame_error = frame_error;
    }
}

bool rendering_frame_error_query(bool* last_frame_error)
{
    return last_frame_error && *last_frame_error;
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
    stream->clip_overflow_depth = 0;
    stream->vertex_count = 0;
    stream->index_count = 0;
    stream->target = RENDERING_TARGET_BACKBUFFER;
    stream->force_new_batch = true;
    stream->overflowed = false;
    stream->render_failed = false;
    stream->backend_trace = (RenderingBackendExecutionTrace){0};
    stream->frame_active = true;
    if (!stream->resources_initialized)
    {
        for (u32 slot = 0; slot < RENDERING_RESOURCE_SLOT_COUNT; slot += 1)
        {
            stream->resources.textures[slot] = (TextureIndex){.value = UINT32_MAX};
        }
        stream->resources_initialized = true;
    }
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
    stream->clip_overflow_depth = 0;
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
        if (stream->clip_overflow_depth != UINT32_MAX)
        {
            stream->clip_overflow_depth += 1;
        }
        rendering_command_stream_mark_overflow(stream);
        rendering_command_stream_record_clip(stream, RENDERING_COMMAND_CLIP_PUSH, stream->clip_stack[stream->clip_depth - 1]);
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
    if (stream->clip_overflow_depth)
    {
        stream->clip_overflow_depth -= 1;
        rendering_command_stream_record_clip(stream, RENDERING_COMMAND_CLIP_POP, stream->clip_stack[stream->clip_depth - 1]);
        return;
    }
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
    stream->clip_overflow_depth = 0;
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
        .resources = command.resources,
        .target = command.target,
    };
    stream->batch_count += 1;
    return true;
}

bool rendering_command_stream_rect_allocation_fits(RenderingCommandStream* stream, BusterPipeline pipeline, TextureIndex texture,
                                                                       u32 first_index, u32 index_count, u64 vertex_bytes, u32 vertex_count)
{
    if (!stream || !rendering_command_stream_is_valid(stream) || pipeline >= BUSTER_PIPELINE_COUNT || stream->command_count >= RENDERING_MAX_DRAW_COUNT ||
        stream->vertex_count > RENDERING_MAX_VERTEX_COUNT || vertex_count > RENDERING_MAX_VERTEX_COUNT - stream->vertex_count || !stream->vertex_cpu ||
        !stream->index_cpu || !rendering_arena_allocation_fits(stream->vertex_cpu, vertex_bytes, 16) ||
        stream->index_count > RENDERING_MAX_INDEX_COUNT || index_count > RENDERING_MAX_INDEX_COUNT - stream->index_count ||
        first_index > RENDERING_MAX_INDEX_COUNT || index_count > RENDERING_MAX_INDEX_COUNT - first_index ||
        !rendering_arena_allocation_fits(stream->index_cpu, (u64)index_count * sizeof(u32), BUSTER_ALIGN_OF(u32)))
    {
        return false;
    }

    rendering_command_stream_ensure_clip_root(stream);
    RenderingCommand command = {
        .kind = RENDERING_COMMAND_RECT,
        .pipeline = pipeline,
        .first_index = first_index,
        .index_count = index_count,
        .texture = texture,
        .clip = stream->clip_stack[stream->clip_depth - 1],
        .resources = stream->resources,
        .target = stream->target,
    };
    if (index_count == 0 || rendering_clip_rect_is_empty(command.clip))
    {
        return true;
    }

    bool compatible = !stream->force_new_batch && stream->batch_count != 0;
    if (compatible)
    {
        RenderingBatch* previous = &stream->batches[stream->batch_count - 1];
        compatible = previous->pipeline == command.pipeline && previous->texture.value == command.texture.value &&
                     rendering_clip_rect_equal(previous->clip, command.clip) && previous->target == command.target &&
                     memcmp(&previous->resources, &command.resources, sizeof(command.resources)) == 0 && previous->index_count <= UINT32_MAX - previous->first_index &&
                     previous->first_index + previous->index_count == command.first_index && index_count <= UINT32_MAX - previous->index_count;
    }
    return compatible || stream->batch_count < RENDERING_MAX_BATCH_COUNT;
}

void rendering_command_stream_record_rect(RenderingCommandStream* stream, BusterPipeline pipeline, TextureIndex texture, u32 first_index,
                                           u32 index_count)
{
    if (!stream)
    {
        return;
    }

    if (pipeline >= BUSTER_PIPELINE_COUNT)
    {
        rendering_command_stream_mark_overflow(stream);
        stream->force_new_batch = true;
        return;
    }

    if (first_index > RENDERING_MAX_INDEX_COUNT || index_count > RENDERING_MAX_INDEX_COUNT - first_index)
    {
        rendering_command_stream_mark_overflow(stream);
        stream->force_new_batch = true;
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
        .resources = stream->resources,
        .target = stream->target,
        .batch_index = UINT32_MAX,
    };
    if (index_count == 0 || rendering_clip_rect_is_empty(clip))
    {
        rendering_command_stream_push_command(stream, command);
        stream->force_new_batch = true;
        return;
    }

    bool compatible = !stream->force_new_batch && stream->batch_count != 0;
    if (compatible)
    {
        RenderingBatch* previous = &stream->batches[stream->batch_count - 1];
        compatible = previous->pipeline == command.pipeline && previous->texture.value == command.texture.value &&
                     rendering_clip_rect_equal(previous->clip, command.clip) && previous->target == command.target &&
                     memcmp(&previous->resources, &command.resources, sizeof(command.resources)) == 0 && previous->index_count <= UINT32_MAX - previous->first_index &&
                     previous->first_index + previous->index_count == command.first_index && index_count <= UINT32_MAX - previous->index_count;
    }
    if (!compatible && stream->batch_count >= RENDERING_MAX_BATCH_COUNT)
    {
        rendering_command_stream_mark_overflow(stream);
        stream->force_new_batch = true;
        return;
    }
    if (!rendering_command_stream_push_command(stream, command))
    {
        return;
    }
    bool batched = false;
    if (compatible)
    {
        stream->batches[stream->batch_count - 1].index_count += index_count;
        stream->commands[stream->command_count - 1].batch_index = stream->batch_count - 1;
        batched = true;
    }
    else
    {
        if (rendering_command_stream_push_batch(stream, command))
        {
            stream->commands[stream->command_count - 1].batch_index = stream->batch_count - 1;
            batched = true;
        }
    }
    stream->force_new_batch = !batched;
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

bool rendering_command_stream_set_texture_binding(RenderingCommandStream* stream, u32 slot, TextureIndex texture)
{
    if (!stream || slot >= RENDERING_RESOURCE_SLOT_COUNT)
    {
        if (stream)
        {
            rendering_command_stream_mark_overflow(stream);
        }
        return false;
    }

    stream->resources.textures[slot] = texture;
    stream->resources_initialized = true;
    if (stream->frame_active)
    {
        RenderingCommand command = {
            .kind = RENDERING_COMMAND_RESOURCE,
            .pipeline = BUSTER_PIPELINE_RECT,
            .texture = texture,
            .resources = stream->resources,
            .target = stream->target,
            .resource_slot = slot,
            .batch_index = UINT32_MAX,
        };
        if (!rendering_command_stream_push_command(stream, command))
        {
            return false;
        }
        stream->force_new_batch = true;
    }
    return true;
}

bool rendering_command_stream_record_target(RenderingCommandStream* stream, u32 target)
{
    if (!stream || target != RENDERING_TARGET_BACKBUFFER)
    {
        if (stream)
        {
            rendering_command_stream_mark_overflow(stream);
        }
        return false;
    }
    stream->target = target;
    if (!rendering_command_stream_push_command(stream, (RenderingCommand){
                                                         .kind = RENDERING_COMMAND_TARGET,
                                                         .pipeline = BUSTER_PIPELINE_RECT,
                                                         .target = target,
                                                         .resources = stream->resources,
                                                         .batch_index = UINT32_MAX,
                                                     }))
    {
        return false;
    }
    stream->force_new_batch = true;
    return true;
}

BUSTER_GLOBAL_LOCAL float4 rendering_blur_corner_radii_to_device(RenderingScale scale, float4 corner_radii)
{
    if (!rendering_scale_is_valid(scale))
    {
        scale = (RenderingScale){.x = 1.0f, .y = 1.0f};
    }
    f32 radius_scale = scale.x < scale.y ? scale.x : scale.y;
    float4 result = {0};
    for (u32 index = 0; index < 4; index += 1)
    {
        float4_element(result, index) = float4_element(corner_radii, index) * radius_scale;
    }
    return result;
}

bool rendering_command_stream_record_background_blur(RenderingCommandStream* stream, F32Interval2 rect, u32 radius)
{
    return rendering_command_stream_record_background_blur_rounded(stream, rect, radius, (float4){0});
}

bool rendering_command_stream_record_background_blur_rounded(RenderingCommandStream* stream, F32Interval2 rect, u32 radius, float4 corner_radii)
{
    if (!stream)
    {
        return false;
    }
    rendering_command_stream_ensure_clip_root(stream);
    RenderingClipRect requested = rendering_clip_rect_from_f32(rect, stream->scale, stream->target_size);
    RenderingClipRect blur_rect = rendering_clip_rect_intersect(stream->clip_stack[stream->clip_depth - 1], requested);
    if (rendering_clip_rect_is_empty(blur_rect))
    {
        return true;
    }
    if (radius > RENDERING_MAX_BLUR_RADIUS)
    {
        radius = RENDERING_MAX_BLUR_RADIUS;
    }
    RenderingCommand command = {
        .kind = RENDERING_COMMAND_BACKGROUND_BLUR,
        .pipeline = BUSTER_PIPELINE_RECT,
        .clip = stream->clip_stack[stream->clip_depth - 1],
        .blur_rect = blur_rect,
        .blur_corner_radii = rendering_blur_corner_radii_to_device(stream->scale, corner_radii),
        .blur_radius = radius,
        .resources = stream->resources,
        .target = stream->target,
        .batch_index = UINT32_MAX,
    };
    if (!rendering_command_stream_push_command(stream, command))
    {
        return false;
    }
    stream->force_new_batch = true;
    return true;
}

RenderingBlurDimensions rendering_blur_dimensions_make(RenderingWindowSize target_size)
{
    RenderingBlurDimensions result = {
        .source_width = target_size.width,
        .source_height = target_size.height,
        .half_width = target_size.width / 2 + target_size.width % 2,
        .half_height = target_size.height / 2 + target_size.height % 2,
        .valid = false,
    };
    if (!result.source_width || !result.source_height || result.source_width > (u32)INT32_MAX || result.source_height > (u32)INT32_MAX || !result.half_width ||
        !result.half_height || result.half_width > UINT32_MAX / result.half_height)
    {
        return result;
    }
    result.valid = true;
    return result;
}

RenderingBlurDescriptorBindings rendering_blur_descriptor_bindings(u32 occurrence)
{
    RenderingBlurDescriptorBindings result = {0};
    if (occurrence >= RENDERING_MAX_DRAW_COUNT || RENDERING_MAX_BLUR_PASS_SET_COUNT < 2)
    {
        return result;
    }
    result.horizontal = occurrence * 3;
    result.vertical = result.horizontal + 1;
    result.downsample = result.horizontal + 2;
    result.valid = true;
    result.stable = true;
    return result;
}

RenderingDescriptorRange rendering_descriptor_range_make(u32 descriptor_base, u32 window_slot, u32 window_count, u32 window_length)
{
    RenderingDescriptorRange result = {0};
    if (window_slot >= window_count || window_length > UINT32_MAX - descriptor_base ||
        window_slot > (UINT32_MAX - descriptor_base) / window_length)
    {
        return result;
    }
    result.base = descriptor_base + window_slot * window_length;
    result.length = window_length;
    result.valid = true;
    return result;
}

bool rendering_arena_allocation_fits(Arena* arena, u64 size, u64 alignment)
{
    if (!arena || !BUSTER_IS_POWER_OF_TWO(alignment) || arena->position < arena_minimum_position || arena->position > arena->reserved_size ||
        arena->granularity == 0 || !BUSTER_IS_POWER_OF_TWO(arena->granularity))
    {
        return false;
    }

    u64 alignment_mask = alignment - 1;
    if (arena->position > UINT64_MAX - alignment_mask)
    {
        return false;
    }
    u64 aligned_offset = (arena->position + alignment_mask) & ~alignment_mask;
    if (aligned_offset > arena->reserved_size || size > arena->reserved_size - aligned_offset || size > UINT64_MAX - aligned_offset)
    {
        return false;
    }
    u64 aligned_size_after = aligned_offset + size;
    u64 granularity_mask = arena->granularity - 1;
    if (aligned_size_after > UINT64_MAX - granularity_mask)
    {
        return false;
    }
    u64 target_committed_size = (aligned_size_after + granularity_mask) & ~granularity_mask;
    return target_committed_size <= arena->reserved_size;
}

RenderingBlurPlan rendering_blur_plan_make(RenderingWindowSize target_size, RenderingClipRect rect, u32 radius)
{
    RenderingBlurDimensions dimensions = rendering_blur_dimensions_make(target_size);
    RenderingBlurPlan result = {
        .rect = rect,
        .source_width = dimensions.source_width,
        .source_height = dimensions.source_height,
        .half_width = dimensions.half_width,
        .half_height = dimensions.half_height,
        .radius = radius > RENDERING_MAX_BLUR_RADIUS ? RENDERING_MAX_BLUR_RADIUS : radius,
        .pass_count = 0,
        .captures_current_target = false,
        .valid = false,
    };
    if (rendering_clip_rect_is_empty(rect) || !dimensions.valid)
    {
        return result;
    }
    result.captures_current_target = true;
    result.pass_count = result.radius ? 2 : 0;
    result.valid = true;
    return result;
}

bool rendering_command_stream_command_ends_batch(RenderingCommandStream* stream, u32 command_index)
{
    if (!stream || command_index >= stream->command_count || stream->commands[command_index].kind != RENDERING_COMMAND_RECT)
    {
        return false;
    }
    u32 batch_index = stream->commands[command_index].batch_index;
    if (batch_index == UINT32_MAX || batch_index >= stream->batch_count)
    {
        return false;
    }
    if (command_index + 1 < stream->command_count)
    {
        RenderingCommand next = stream->commands[command_index + 1];
        if (next.kind == RENDERING_COMMAND_RECT && next.batch_index == batch_index)
        {
            return false;
        }
    }
    return true;
}

u32 rendering_command_stream_replay(RenderingCommandStream* stream, RenderingReplayEvent* events, u32 capacity)
{
    if (!rendering_command_stream_is_valid(stream))
    {
        return 0;
    }
    u32 event_count = 0;
    for (u32 command_index = 0; command_index < stream->command_count; command_index += 1)
    {
        RenderingCommand command = stream->commands[command_index];
        RenderingReplayEvent event = {
            .kind = RENDERING_REPLAY_EVENT_KIND_COUNT,
            .pipeline = command.pipeline,
            .command_index = command_index,
            .batch_index = command.batch_index,
            .texture = command.texture,
            .clip = command.clip,
            .blur_rect = command.blur_rect,
            .blur_corner_radii = command.blur_corner_radii,
            .resources = command.resources,
            .target = command.target,
            .radius = command.blur_radius,
        };
        bool emit = true;
        switch (command.kind)
        {
        case RENDERING_COMMAND_RECT:
            if (rendering_command_stream_command_ends_batch(stream, command_index))
            {
                event.kind = RENDERING_REPLAY_DRAW;
                if (command.batch_index < stream->batch_count)
                {
                    RenderingBatch batch = stream->batches[command.batch_index];
                    event.pipeline = batch.pipeline;
                    event.batch_index = command.batch_index;
                    event.texture = batch.texture;
                    event.clip = batch.clip;
                    event.resources = batch.resources;
                    event.target = batch.target;
                }
            }
            else
            {
                emit = false;
            }
            break;
        case RENDERING_COMMAND_CLIP_PUSH:
            event.kind = RENDERING_REPLAY_CLIP_PUSH;
            break;
        case RENDERING_COMMAND_CLIP_POP:
            event.kind = RENDERING_REPLAY_CLIP_POP;
            break;
        case RENDERING_COMMAND_FLUSH:
            event.kind = RENDERING_REPLAY_FLUSH;
            break;
        case RENDERING_COMMAND_RESOURCE:
            event.kind = RENDERING_REPLAY_RESOURCE;
            break;
        case RENDERING_COMMAND_TARGET:
            event.kind = RENDERING_REPLAY_TARGET;
            break;
        case RENDERING_COMMAND_BACKGROUND_BLUR:
            event.kind = RENDERING_REPLAY_BACKGROUND_BLUR;
            break;
        case RENDERING_COMMAND_KIND_COUNT:
            emit = false;
            break;
        }
        if (emit)
        {
            if (event_count < capacity && events)
            {
                events[event_count] = event;
            }
            event_count += 1;
        }
    }
    return event_count;
}

RenderingBackendReplayResult rendering_backend_replay_policy(RenderingCommandStream* stream, RenderingBackendKind backend, RenderingReplayEvent* events,
                                                              u32 capacity)
{
    RenderingBackendReplayResult result = {
        .backend = backend,
        .valid = false,
        .order_preserved = false,
        .resources_snapshot = false,
        .target_boundaries = false,
        .state_restored = false,
    };
    switch (backend)
    {
    case RENDERING_BACKEND_NULL:
    case RENDERING_BACKEND_VULKAN:
    case RENDERING_BACKEND_METAL:
    case RENDERING_BACKEND_D3D12:
        break;
    case RENDERING_BACKEND_KIND_COUNT:
        return result;
    }
    if (!rendering_command_stream_is_valid(stream) || !events)
    {
        return result;
    }
    u32 event_count = rendering_command_stream_replay(stream, events, capacity);
    if (event_count > capacity)
    {
        return result;
    }
    result.valid = true;
    result.event_count = event_count;
    result.order_preserved = true;
    result.resources_snapshot = true;
    result.target_boundaries = true;
    result.state_restored = true;
    bool saw_blur = false;
    bool blur_followed_by_draw = false;
    u32 previous_command_index = 0;
    for (u32 event_index = 0; event_index < event_count; event_index += 1)
    {
        RenderingReplayEvent event = events[event_index];
        if (event_index && event.command_index <= previous_command_index)
        {
            result.order_preserved = false;
        }
        previous_command_index = event.command_index;
        switch (event.kind)
        {
        case RENDERING_REPLAY_DRAW:
            result.draw_count += 1;
            if (event.batch_index >= stream->batch_count ||
                memcmp(&event.resources, &stream->batches[event.batch_index].resources, sizeof(event.resources)) != 0 ||
                event.target != stream->batches[event.batch_index].target)
            {
                result.resources_snapshot = false;
            }
            if (saw_blur)
            {
                blur_followed_by_draw = true;
                saw_blur = false;
            }
            break;
        case RENDERING_REPLAY_BACKGROUND_BLUR:
            result.blur_pass_count += 3;
            saw_blur = true;
            break;
        case RENDERING_REPLAY_TARGET:
            if (event.target != RENDERING_TARGET_BACKBUFFER)
            {
                result.target_boundaries = false;
            }
            break;
        case RENDERING_REPLAY_CLIP_PUSH:
        case RENDERING_REPLAY_CLIP_POP:
        case RENDERING_REPLAY_FLUSH:
        case RENDERING_REPLAY_RESOURCE:
            break;
        case RENDERING_REPLAY_EVENT_KIND_COUNT:
            result.valid = false;
            break;
        }
    }
    if (saw_blur)
    {
        blur_followed_by_draw = true;
    }
    result.state_restored = !result.blur_pass_count || blur_followed_by_draw;
    return result;
}

bool rendering_window_has_rendering_error(RenderingWindowHandle* window)
{
    return rendering_window_has_rendering_error_internal(window);
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

bool rendering_window_set_render_target(RenderingWindowHandle* window, u32 target)
{
    RenderingCommandStream* stream = window ? rendering_window_command_stream(window) : 0;
    return rendering_command_stream_record_target(stream, target);
}

bool rendering_window_render_background_blur(RenderingWindowHandle* window, F32Interval2 rect, u32 radius)
{
    RenderingCommandStream* stream = window ? rendering_window_command_stream(window) : 0;
    return rendering_command_stream_record_background_blur(stream, rect, radius);
}

bool rendering_window_render_background_blur_rounded(RenderingWindowHandle* window, F32Interval2 rect, u32 radius, float4 corner_radii)
{
    RenderingCommandStream* stream = window ? rendering_window_command_stream(window) : 0;
    return rendering_command_stream_record_background_blur_rounded(stream, rect, radius, corner_radii);
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
    u64 white_texture_count = (u64)white_texture_width * white_texture_height;
    if (!rendering_arena_allocation_fits(arena, white_texture_count * sizeof(u32), BUSTER_ALIGN_OF(u32)))
    {
        return (TextureIndex){.value = UINT32_MAX};
    }
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

bool rendering_command_stream_add_vertices(RenderingCommandStream* stream, ByteSlice vertex_memory, u32 vertex_count)
{
    if (!stream || stream->vertex_count > RENDERING_MAX_VERTEX_COUNT || vertex_count > RENDERING_MAX_VERTEX_COUNT - stream->vertex_count || !stream->vertex_cpu ||
        (vertex_memory.length && !vertex_memory.pointer) || !rendering_arena_allocation_fits(stream->vertex_cpu, vertex_memory.length, 16))
    {
        if (stream)
        {
            rendering_command_stream_mark_overflow(stream);
        }
        return false;
    }
    if (vertex_memory.length)
    {
        u8* allocation = (u8*)arena_allocate_bytes(stream->vertex_cpu, vertex_memory.length, 16);
        memcpy(allocation, vertex_memory.pointer, vertex_memory.length);
    }
    stream->vertex_count += vertex_count;
    return true;
}

bool rendering_command_stream_add_indices(RenderingCommandStream* stream, Sliceu32 indices)
{
    if (!stream || stream->index_count > RENDERING_MAX_INDEX_COUNT || indices.length > RENDERING_MAX_INDEX_COUNT - stream->index_count || !stream->index_cpu ||
        (indices.length && !indices.pointer) || indices.length > UINT64_MAX / sizeof(*indices.pointer))
    {
        if (stream)
        {
            rendering_command_stream_mark_overflow(stream);
        }
        return false;
    }
    u64 byte_count = indices.length * sizeof(*indices.pointer);
    if (!rendering_arena_allocation_fits(stream->index_cpu, byte_count, BUSTER_ALIGN_OF(u32)))
    {
        rendering_command_stream_mark_overflow(stream);
        return false;
    }
    if (byte_count)
    {
        u32* allocation = (u32*)arena_allocate_bytes(stream->index_cpu, byte_count, BUSTER_ALIGN_OF(u32));
        memcpy(allocation, indices.pointer, byte_count);
    }
    stream->index_count += (u32)indices.length;
    return true;
}

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
    u32 vertex_offset = stream->vertex_count;
    if (!rendering_command_stream_add_vertices(stream, vertex_memory, vertex_count))
    {
        return UINT32_MAX;
    }
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
    if (!rendering_command_stream_add_indices(stream, indices))
    {
        return UINT32_MAX;
    }
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

    u32 first_index = stream->index_count;
    if (!rendering_command_stream_rect_allocation_fits(stream, BUSTER_PIPELINE_RECT, (TextureIndex){.value = draw.texture_index}, first_index, 6,
                                                        sizeof(vertices), BUSTER_ARRAY_LENGTH(vertices)))
    {
        rendering_command_stream_mark_overflow(stream);
        return;
    }
    u64 vertex_position = stream->vertex_cpu->position;
    u64 index_position = stream->index_cpu->position;
    u32 vertex_count = stream->vertex_count;
    u32 index_count = stream->index_count;
    u32 command_count = stream->command_count;
    u32 batch_count = stream->batch_count;
    bool force_new_batch = stream->force_new_batch;
    u32 previous_batch_index_count = batch_count ? stream->batches[batch_count - 1].index_count : 0;
    u32 vertex_offset =
        rendering_window_pipeline_add_vertices(window, BUSTER_PIPELINE_RECT, BUSTER_ARRAY_TO_BYTE_SLICE(vertices), BUSTER_ARRAY_LENGTH(vertices));
    if (vertex_offset == UINT32_MAX)
    {
        arena_set_position(stream->vertex_cpu, vertex_position);
        arena_set_position(stream->index_cpu, index_position);
        stream->vertex_count = vertex_count;
        stream->index_count = index_count;
        stream->force_new_batch = force_new_batch;
        return;
    }
    u32 indices[] = {
        vertex_offset + 0, vertex_offset + 1, vertex_offset + 2, vertex_offset + 1, vertex_offset + 3, vertex_offset + 2,
    };
    u32 recorded_first_index = rendering_window_pipeline_add_indices(window, BUSTER_PIPELINE_RECT, (Sliceu32)BUSTER_ARRAY_TO_SLICE(indices));
    if (recorded_first_index != UINT32_MAX)
    {
        rendering_command_stream_record_rect(stream, BUSTER_PIPELINE_RECT, (TextureIndex){.value = draw.texture_index}, recorded_first_index,
                                             BUSTER_ARRAY_LENGTH(indices));
        if (stream->command_count == command_count || stream->overflowed)
        {
            arena_set_position(stream->vertex_cpu, vertex_position);
            arena_set_position(stream->index_cpu, index_position);
            stream->vertex_count = vertex_count;
            stream->index_count = index_count;
            stream->command_count = command_count;
            stream->batch_count = batch_count;
            if (batch_count)
            {
                stream->batches[batch_count - 1].index_count = previous_batch_index_count;
            }
            stream->force_new_batch = force_new_batch;
            rendering_command_stream_mark_overflow(stream);
        }
    }
    else
    {
        arena_set_position(stream->vertex_cpu, vertex_position);
        stream->vertex_count = vertex_count;
        stream->force_new_batch = force_new_batch;
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
        u32 text_first_index = stream->index_count;
        if (!rendering_command_stream_rect_allocation_fits(stream, BUSTER_PIPELINE_RECT, (TextureIndex){.value = texture_index}, text_first_index, 6,
                                                            sizeof(vertices), BUSTER_ARRAY_LENGTH(vertices)))
        {
            rendering_command_stream_mark_overflow(stream);
            return;
        }
        u64 text_vertex_position = stream->vertex_cpu->position;
        u64 text_index_position = stream->index_cpu->position;
        u32 text_vertex_count = stream->vertex_count;
        u32 text_index_count = stream->index_count;
        u32 text_command_count = stream->command_count;
        u32 text_batch_count = stream->batch_count;
        bool text_force_new_batch = stream->force_new_batch;
        u32 text_previous_batch_index_count = text_batch_count ? stream->batches[text_batch_count - 1].index_count : 0;
        u32 vertex_offset =
            rendering_window_pipeline_add_vertices(window, BUSTER_PIPELINE_RECT, BUSTER_ARRAY_TO_BYTE_SLICE(vertices), BUSTER_ARRAY_LENGTH(vertices));
        if (vertex_offset == UINT32_MAX)
        {
            arena_set_position(stream->vertex_cpu, text_vertex_position);
            arena_set_position(stream->index_cpu, text_index_position);
            stream->vertex_count = text_vertex_count;
            stream->index_count = text_index_count;
            stream->force_new_batch = text_force_new_batch;
            return;
        }
        u32 indices[] = {vertex_offset + 0, vertex_offset + 1, vertex_offset + 2, vertex_offset + 1, vertex_offset + 3, vertex_offset + 2};
        u32 first_index = rendering_window_pipeline_add_indices(window, BUSTER_PIPELINE_RECT, (Sliceu32)BUSTER_ARRAY_TO_SLICE(indices));
        if (first_index != UINT32_MAX)
        {
            rendering_command_stream_record_rect(stream, BUSTER_PIPELINE_RECT, (TextureIndex){.value = texture_index}, first_index,
                                                 BUSTER_ARRAY_LENGTH(indices));
            if (stream->command_count == text_command_count || stream->overflowed)
            {
                arena_set_position(stream->vertex_cpu, text_vertex_position);
                arena_set_position(stream->index_cpu, text_index_position);
                stream->vertex_count = text_vertex_count;
                stream->index_count = text_index_count;
                stream->command_count = text_command_count;
                stream->batch_count = text_batch_count;
                if (text_batch_count)
                {
                    stream->batches[text_batch_count - 1].index_count = text_previous_batch_index_count;
                }
                stream->force_new_batch = text_force_new_batch;
                rendering_command_stream_mark_overflow(stream);
                return;
            }
        }
        else
        {
            arena_set_position(stream->vertex_cpu, text_vertex_position);
            arena_set_position(stream->index_cpu, text_index_position);
            stream->vertex_count = text_vertex_count;
            stream->index_count = text_index_count;
            stream->force_new_batch = text_force_new_batch;
            return;
        }

        s32 kerning = 0;
        if (i + 1 < string.length)
        {
            kerning = (texture_atlas->description.kerning_tables + ch * 256)[(u32)string.pointer[i + 1]];
        }
        x_offset += (f32)character->advance + (f32)kerning;
    }
}

RenderingUvCoordinate rendering_rect_uv_for_quad(RectVertex vertex, u32 quad_vertex_index)
{
    RenderingUvCoordinate result = {0};
    static const f32 quad_coordinates[4][2] = {
        {-1.0f, -1.0f},
        {1.0f, -1.0f},
        {-1.0f, 1.0f},
        {1.0f, 1.0f},
    };
    if (quad_vertex_index >= BUSTER_ARRAY_LENGTH(quad_coordinates))
    {
        return result;
    }
    f32 u = quad_coordinates[quad_vertex_index][0] * 0.5f + 0.5f;
    f32 v = quad_coordinates[quad_vertex_index][1] * 0.5f + 0.5f;
    result.x = float2_element(vertex.uv0, 0) + float2_element(vertex.uv_extent, 0) * u;
    result.y = float2_element(vertex.uv0, 1) + float2_element(vertex.uv_extent, 1) * v;
    return result;
}

BUSTER_GLOBAL_LOCAL f32 rendering_blur_kernel_raw_weight(u32 radius, s32 offset)
{
    f32 sigma = (f32)radius * (f32)BUSTER_BLUR_SIGMA_SCALE;
    if (sigma < (f32)BUSTER_BLUR_MIN_SIGMA)
    {
        sigma = (f32)BUSTER_BLUR_MIN_SIGMA;
    }
    f32 distance = (f32)offset;
    f32 exponent = -(distance * distance) / (2.0f * sigma * sigma);
    return pow_f32((f32)BUSTER_BLUR_EXP_BASE, exponent);
}

f32 rendering_blur_kernel_weight(u32 radius, s32 offset)
{
    radius = radius > RENDERING_MAX_BLUR_RADIUS ? RENDERING_MAX_BLUR_RADIUS : radius;
    if (offset < -(s32)radius || offset > (s32)radius)
    {
        return 0.0f;
    }

    f32 weight = rendering_blur_kernel_raw_weight(radius, offset);
    f32 normalization = 0.0f;
    for (s32 normalization_offset = -(s32)RENDERING_MAX_BLUR_RADIUS; normalization_offset <= (s32)RENDERING_MAX_BLUR_RADIUS; normalization_offset += 1)
    {
        if (normalization_offset < -(s32)radius || normalization_offset > (s32)radius)
        {
            continue;
        }
        normalization += rendering_blur_kernel_raw_weight(radius, normalization_offset);
    }
    return normalization > 0.0f ? weight / normalization : 0.0f;
}

u32 rendering_blur_kernel_weight_fixed16(u32 radius, s32 offset)
{
    f32 weight = rendering_blur_kernel_weight(radius, offset) * 65536.0f;
    return weight > 0.0f ? (u32)floor_f32(weight + 0.5f) : 0;
}

BUSTER_GLOBAL_LOCAL f32 rendering_rounded_rect_sdf(RenderingClipRect rect, float2 pixel_position, float4 corner_radii)
{
    if (rendering_clip_rect_is_empty(rect))
    {
        return 1.0f;
    }

    f32 rect_x0 = (f32)rect.x0;
    f32 rect_y0 = (f32)rect.y0;
    f32 rect_x1 = (f32)rect.x1;
    f32 rect_y1 = (f32)rect.y1;
    float2 half_size = float2_make((rect_x1 - rect_x0) * 0.5f, (rect_y1 - rect_y0) * 0.5f);
    float2 center = float2_make((rect_x1 + rect_x0) * 0.5f, (rect_y1 + rect_y0) * 0.5f);
    u32 corner_index = float2_element(pixel_position, 0) < float2_element(center, 0)
                            ? (float2_element(pixel_position, 1) < float2_element(center, 1) ? 0 : 1)
                            : (float2_element(pixel_position, 1) < float2_element(center, 1) ? 2 : 3);
    f32 radius = float4_element(corner_radii, corner_index);
    if (radius < 0.0f || radius != radius)
    {
        radius = 0.0f;
    }
    f32 maximum_radius = float2_element(half_size, 0) < float2_element(half_size, 1) ? float2_element(half_size, 0) : float2_element(half_size, 1);
    radius = radius > maximum_radius ? maximum_radius : radius;
    float2 distance_without_radius = float2_make(fabs_f32(float2_element(center, 0) - float2_element(pixel_position, 0)) - float2_element(half_size, 0),
                                                  fabs_f32(float2_element(center, 1) - float2_element(pixel_position, 1)) - float2_element(half_size, 1));
    float2 distance_with_radius = float2_make(float2_element(distance_without_radius, 0) + radius, float2_element(distance_without_radius, 1) + radius);
    f32 negative_distance = BUSTER_MIN(BUSTER_MAX(float2_element(distance_with_radius, 0), float2_element(distance_with_radius, 1)), 0.0f);
    f32 positive_x = BUSTER_MAX(float2_element(distance_with_radius, 0), 0.0f);
    f32 positive_y = BUSTER_MAX(float2_element(distance_with_radius, 1), 0.0f);
    f32 positive_distance = sqrt_f32(positive_x * positive_x + positive_y * positive_y);
    return negative_distance + positive_distance - radius;
}

f32 rendering_rounded_rect_mask_factor(RenderingClipRect rect, float2 pixel_position, float4 corner_radii)
{
    return rendering_rounded_rect_sdf(rect, pixel_position, corner_radii) <= 0.0f ? 1.0f : 0.0f;
}

BUSTER_GLOBAL_LOCAL bool rendering_blur_validate(Arena* scratch, u8* pixels, u32 width, u32 height, u32 stride, u32 channels, u32 radius)
{
    if (!width || !height || !pixels || !scratch || !channels)
    {
        return false;
    }
    u64 row_bytes = (u64)width * channels;
    if ((u64)stride < row_bytes)
    {
        return false;
    }
    u64 pixel_count = (u64)width * height;
    if (pixel_count > RENDERING_MAX_BLUR_PIXELS || pixel_count > UINT64_MAX / channels)
    {
        return false;
    }
    u64 byte_count = pixel_count * channels;
    return radius == 0 || rendering_arena_allocation_fits(scratch, byte_count, 16);
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

    f32 kernel_weights[RENDERING_MAX_BLUR_RADIUS * 2 + 1] = {0};
    for (s32 offset = -(s32)RENDERING_MAX_BLUR_RADIUS; offset <= (s32)RENDERING_MAX_BLUR_RADIUS; offset += 1)
    {
        if (offset < -(s32)radius || offset > (s32)radius)
        {
            continue;
        }
        kernel_weights[offset + RENDERING_MAX_BLUR_RADIUS] = rendering_blur_kernel_weight(radius, offset);
    }
    u64 pixel_count = (u64)width * height;
    u8* horizontal = (u8*)arena_allocate_bytes(scratch, pixel_count * channels, 16);
    for (u32 y = 0; y < height; y += 1)
    {
        for (u32 x = 0; x < width; x += 1)
        {
            for (u32 channel = 0; channel < channels; channel += 1)
            {
                f32 sum = 0.0f;
                f32 weight_sum = 0.0f;
                for (s32 offset = -(s32)RENDERING_MAX_BLUR_RADIUS; offset <= (s32)RENDERING_MAX_BLUR_RADIUS; offset += 1)
                {
                    if (offset < -(s32)radius || offset > (s32)radius)
                    {
                        continue;
                    }
                    s32 sample_x = (s32)x + offset;
                    if (sample_x < 0)
                    {
                        sample_x = 0;
                    }
                    if (sample_x >= (s32)width)
                    {
                        sample_x = (s32)width - 1;
                    }
                    f32 weight = kernel_weights[offset + RENDERING_MAX_BLUR_RADIUS];
                    sum += (f32)pixels[(u64)y * stride + (u64)sample_x * channels + channel] * weight;
                    weight_sum += weight;
                }
                f32 value = weight_sum > 0.0f ? sum / weight_sum : 0.0f;
                value = BUSTER_CLAMP(0.0f, value, 255.0f);
                horizontal[((u64)y * width + x) * channels + channel] = (u8)round_f32(value);
            }
        }
    }

    for (u32 y = 0; y < height; y += 1)
    {
        for (u32 x = 0; x < width; x += 1)
        {
            for (u32 channel = 0; channel < channels; channel += 1)
            {
                f32 sum = 0.0f;
                f32 weight_sum = 0.0f;
                for (s32 offset = -(s32)RENDERING_MAX_BLUR_RADIUS; offset <= (s32)RENDERING_MAX_BLUR_RADIUS; offset += 1)
                {
                    if (offset < -(s32)radius || offset > (s32)radius)
                    {
                        continue;
                    }
                    s32 sample_y = (s32)y + offset;
                    if (sample_y < 0)
                    {
                        sample_y = 0;
                    }
                    if (sample_y >= (s32)height)
                    {
                        sample_y = (s32)height - 1;
                    }
                    f32 weight = kernel_weights[offset + RENDERING_MAX_BLUR_RADIUS];
                    sum += (f32)horizontal[((u64)sample_y * width + x) * channels + channel] * weight;
                    weight_sum += weight;
                }
                f32 value = weight_sum > 0.0f ? sum / weight_sum : 0.0f;
                value = BUSTER_CLAMP(0.0f, value, 255.0f);
                pixels[(u64)y * stride + (u64)x * channels + channel] = (u8)round_f32(value);
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
    if (!rendering_arena_allocation_fits(arena, byte_count, 16))
    {
        return (TextureIndex){.value = UINT32_MAX};
    }
    u64 arena_position = arena->position;
    u8* copy = (u8*)arena_allocate_bytes(arena, byte_count, 16);
    memcpy(copy, source.pointer, byte_count);
    Arena* conflicts[] = {arena};
    TemporalArena scratch = scratch_begin(conflicts, BUSTER_ARRAY_LENGTH(conflicts));
    bool success = channels == 4 ? rendering_blur_rgba8(scratch.arena, copy, source.width, source.height, source.width * channels, radius)
                                 : rendering_blur_r8(scratch.arena, copy, source.width, source.height, source.width, radius);
    scratch_end(scratch);
    if (!success)
    {
        arena_set_position(arena, arena_position);
        return (TextureIndex){.value = UINT32_MAX};
    }
    source.pointer = copy;
    return rendering_texture_create(rendering, source);
}
