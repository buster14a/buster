#include <buster/tests/rendering_test.h>

BUSTER_TEST_F_DECL UnitTestResult rendering_tests(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    BUSTER_UNUSED(arguments);

    rendering_window_set_content_scale(0, (RenderingScale){.x = 2.0f, .y = 2.0f});
    rendering_window_clip_push(0, (F32Interval2){.x0 = 0, .y0 = 0, .x1 = 1, .y1 = 1});
    rendering_window_clip_pop(0);
    rendering_window_clip_reset(0);
    rendering_window_flush(0);
    rendering_window_render_rect(0, (RectDraw){0});
    rendering_window_render_text(0, 0, S8("null"), (float4){0}, RENDER_FONT_TYPE_MONOSPACE, 0, 0);

    BUSTER_TEST(arguments, rendering_vulkan_window_requires_device_initialization(false));
    BUSTER_TEST(arguments, !rendering_vulkan_window_requires_device_initialization(true));

    RenderingVulkanSurfaceCompatibility compatible_surface = {
        .queue_setup = true,
        .present_queue = true,
        .capabilities = true,
        .format = true,
        .present_modes = true,
        .usage = true,
        .image_count = true,
        .composite_alpha = true,
    };
    BUSTER_TEST(arguments, rendering_vulkan_existing_surface_is_compatible(compatible_surface));
    compatible_surface.format = false;
    BUSTER_TEST(arguments, !rendering_vulkan_existing_surface_is_compatible(compatible_surface));
    compatible_surface.format = true;
    compatible_surface.present_queue = false;
    BUSTER_TEST(arguments, !rendering_vulkan_existing_surface_is_compatible(compatible_surface));

    BUSTER_TEST(arguments, rendering_vulkan_surface_format_sentinel_is_compatible(1, 1));
    BUSTER_TEST(arguments, !rendering_vulkan_surface_format_sentinel_is_compatible(1, 2));

    BUSTER_TEST(arguments, !rendering_vulkan_enumeration_needs_retry(false, 4, 4));
    BUSTER_TEST(arguments, rendering_vulkan_enumeration_needs_retry(true, 4, 4));
    BUSTER_TEST(arguments, rendering_vulkan_enumeration_needs_retry(false, 4, 5));
    BUSTER_TEST(arguments, !rendering_vulkan_queue_family_enumeration_needs_retry(8, 8, 8));
    BUSTER_TEST(arguments, rendering_vulkan_queue_family_enumeration_needs_retry(8, 8, 9));
    BUSTER_TEST(arguments, rendering_vulkan_queue_family_enumeration_needs_retry(8, 9, 9));

    RenderingVulkanQueueFamilyCandidate same_queue_candidates[] = {
        {.queue_count = 1, .graphics = true, .present = false},
        {.queue_count = 1, .graphics = true, .present = true},
        {.queue_count = 1, .graphics = false, .present = true},
    };
    RenderingVulkanQueueFamilySelection same_queue_selection =
        rendering_vulkan_select_queue_families((RenderingVulkanQueueFamilyCandidateSlice)BUSTER_ARRAY_TO_SLICE(same_queue_candidates));
    BUSTER_TEST(arguments, same_queue_selection.eligible);
    BUSTER_TEST(arguments, same_queue_selection.graphics_family_index == 1);
    BUSTER_TEST(arguments, same_queue_selection.present_family_index == 1);

    RenderingVulkanQueueFamilyCandidate split_queue_candidates[] = {
        {.queue_count = 1, .graphics = true, .present = false},
        {.queue_count = 1, .graphics = false, .present = true},
    };
    RenderingVulkanQueueFamilySelection split_queue_selection =
        rendering_vulkan_select_queue_families((RenderingVulkanQueueFamilyCandidateSlice)BUSTER_ARRAY_TO_SLICE(split_queue_candidates));
    BUSTER_TEST(arguments, split_queue_selection.eligible);
    BUSTER_TEST(arguments, split_queue_selection.graphics_family_index == 0);
    BUSTER_TEST(arguments, split_queue_selection.present_family_index == 1);

    RenderingVulkanDeviceCandidate candidates[] = {
        {
            .name = S8("Rejected GPU"),
            .vendor_id = 1,
            .device_id = 1,
            .enumeration_index = 0,
            .device_type = RENDERING_VULKAN_DEVICE_TYPE_DISCRETE,
            .queues = {.graphics_family_index = 0, .present_family_index = 0, .eligible = true},
            .has_required_extension = false,
            .has_required_features = true,
            .has_surface_support = true,
        },
        {
            .name = S8("Integrated GPU"),
            .vendor_id = 2,
            .device_id = 2,
            .enumeration_index = 1,
            .device_type = RENDERING_VULKAN_DEVICE_TYPE_INTEGRATED,
            .queues = {.graphics_family_index = 0, .present_family_index = 1, .eligible = true},
            .has_required_extension = true,
            .has_required_features = true,
            .has_surface_support = true,
        },
        {
            .name = S8("Discrete GPU"),
            .vendor_id = 3,
            .device_id = 3,
            .enumeration_index = 2,
            .device_type = RENDERING_VULKAN_DEVICE_TYPE_DISCRETE,
            .queues = {.graphics_family_index = 0, .present_family_index = 0, .eligible = true},
            .has_required_extension = true,
            .has_required_features = true,
            .has_surface_support = true,
        },
    };
    RenderingVulkanDeviceSelection selection = rendering_vulkan_select_device((RenderingVulkanDeviceCandidateSlice)BUSTER_ARRAY_TO_SLICE(candidates));
    BUSTER_TEST(arguments, selection.found);
    BUSTER_TEST(arguments, selection.candidate_index == 2);
    BUSTER_TEST(arguments, selection.score == rendering_vulkan_device_score(candidates[2]));

    RenderingVulkanDeviceCandidate tie_candidates[] = {
        {
            .name = S8("Zeta GPU"),
            .vendor_id = 4,
            .device_id = 4,
            .enumeration_index = 0,
            .device_type = RENDERING_VULKAN_DEVICE_TYPE_DISCRETE,
            .queues = {.graphics_family_index = 0, .present_family_index = 0, .eligible = true},
            .has_required_extension = true,
            .has_required_features = true,
            .has_surface_support = true,
        },
        {
            .name = S8("Alpha GPU"),
            .vendor_id = 5,
            .device_id = 5,
            .enumeration_index = 1,
            .device_type = RENDERING_VULKAN_DEVICE_TYPE_DISCRETE,
            .queues = {.graphics_family_index = 0, .present_family_index = 0, .eligible = true},
            .has_required_extension = true,
            .has_required_features = true,
            .has_surface_support = true,
        },
    };
    selection = rendering_vulkan_select_device((RenderingVulkanDeviceCandidateSlice)BUSTER_ARRAY_TO_SLICE(tie_candidates));
    BUSTER_TEST(arguments, selection.found);
    BUSTER_TEST(arguments, selection.candidate_index == 1);

    RenderingVulkanDeviceCandidate no_eligible_candidates[] = {
        {
            .name = S8("No Extension"),
            .device_type = RENDERING_VULKAN_DEVICE_TYPE_DISCRETE,
            .queues = {.eligible = true},
            .has_required_extension = false,
            .has_required_features = true,
            .has_surface_support = true,
        },
        {
            .name = S8("No Features"),
            .device_type = RENDERING_VULKAN_DEVICE_TYPE_INTEGRATED,
            .queues = {.eligible = true},
            .has_required_extension = true,
            .has_required_features = false,
            .has_surface_support = true,
        },
        {
            .name = S8("No Surface"),
            .device_type = RENDERING_VULKAN_DEVICE_TYPE_INTEGRATED,
            .queues = {.eligible = true},
            .has_required_extension = true,
            .has_required_features = true,
            .has_surface_support = false,
        },
        {
            .name = S8("No Queue Pair"),
            .device_type = RENDERING_VULKAN_DEVICE_TYPE_INTEGRATED,
            .queues = {.eligible = false},
            .has_required_extension = true,
            .has_required_features = true,
            .has_surface_support = true,
        },
    };
    selection = rendering_vulkan_select_device((RenderingVulkanDeviceCandidateSlice)BUSTER_ARRAY_TO_SLICE(no_eligible_candidates));
    BUSTER_TEST(arguments, !selection.found);

    RenderingClipRect dpi_clip = rendering_clip_rect_from_f32((F32Interval2){.x0 = -2.25f, .y0 = 0.25f, .x1 = 5.1f, .y1 = 8.0f},
                                                               (RenderingScale){.x = 2.0f, .y = 1.5f}, (RenderingWindowSize){.width = 20, .height = 10});
    BUSTER_TEST(arguments, dpi_clip.x0 == 0 && dpi_clip.y0 == 0 && dpi_clip.x1 == 11 && dpi_clip.y1 == 10);
    BUSTER_TEST(arguments, rendering_clip_rect_is_empty(rendering_clip_rect_intersect((RenderingClipRect){.x0 = 4, .y0 = 4, .x1 = 4, .y1 = 8},
                                                                                       (RenderingClipRect){.x0 = 0, .y0 = 0, .x1 = 10, .y1 = 10})));

    Arena* stream_vertex_arena = arena_create((ArenaCreation){0});
    Arena* stream_index_arena = arena_create((ArenaCreation){0});
    RenderingCommandStream* stream = arena_allocate(arguments->arena, RenderingCommandStream, 1);
    *stream = (RenderingCommandStream){0};
    rendering_command_stream_bind_buffers(stream, stream_vertex_arena, stream_index_arena);
    rendering_command_stream_begin(stream, (RenderingWindowSize){.width = 100, .height = 100}, (RenderingScale){.x = 1.0f, .y = 1.0f});
    rendering_command_stream_record_rect(stream, BUSTER_PIPELINE_RECT, (TextureIndex){.value = 0}, 0, 6);
    rendering_command_stream_record_rect(stream, BUSTER_PIPELINE_RECT, (TextureIndex){.value = 0}, 6, 6);
    rendering_command_stream_record_rect(stream, BUSTER_PIPELINE_RECT, (TextureIndex){.value = 1}, 12, 6);
    rendering_command_stream_push_clip(stream, (F32Interval2){.x0 = 10, .y0 = 10, .x1 = 90, .y1 = 90});
    rendering_command_stream_push_clip(stream, (F32Interval2){.x0 = -5, .y0 = 20, .x1 = 80, .y1 = 100});
    BUSTER_TEST(arguments, stream->clip_stack[stream->clip_depth - 1].x0 == 10 && stream->clip_stack[stream->clip_depth - 1].y0 == 20 &&
                                  stream->clip_stack[stream->clip_depth - 1].x1 == 80 && stream->clip_stack[stream->clip_depth - 1].y1 == 90);
    rendering_command_stream_record_rect(stream, BUSTER_PIPELINE_RECT, (TextureIndex){.value = 1}, 18, 6);
    rendering_command_stream_pop_clip(stream);
    rendering_command_stream_record_rect(stream, BUSTER_PIPELINE_RECT, (TextureIndex){.value = 1}, 24, 6);
    rendering_command_stream_pop_clip(stream);
    rendering_command_stream_record_rect(stream, BUSTER_PIPELINE_RECT, (TextureIndex){.value = 1}, 30, 6);
    rendering_command_stream_push_clip(stream, (F32Interval2){.x0 = 0, .y0 = 0, .x1 = 0, .y1 = 100});
    rendering_command_stream_record_rect(stream, BUSTER_PIPELINE_RECT, (TextureIndex){.value = 0}, 36, 6);
    BUSTER_TEST(arguments, stream->batch_count == 5);
    rendering_command_stream_pop_clip(stream);
    rendering_command_stream_record_rect(stream, BUSTER_PIPELINE_RECT, (TextureIndex){.value = 0}, 42, 6);
    BUSTER_TEST(arguments, stream->batch_count == 6);
    BUSTER_TEST(arguments, stream->batches[0].index_count == 12);
    BUSTER_TEST(arguments, stream->batches[2].clip.x0 == 10 && stream->batches[2].clip.y0 == 20 && stream->batches[2].clip.x1 == 80 &&
                                  stream->batches[2].clip.y1 == 90);
    BUSTER_TEST(arguments, stream->commands[0].kind == RENDERING_COMMAND_RECT && stream->commands[3].kind == RENDERING_COMMAND_CLIP_PUSH);
    rendering_command_stream_record_flush(stream);
    BUSTER_TEST(arguments, stream->force_new_batch && stream->command_count != 0);
    stream->command_count = RENDERING_MAX_DRAW_COUNT;
    stream->overflowed = false;
    rendering_command_stream_record_flush(stream);
    BUSTER_TEST(arguments, stream->overflowed);

    u8 blur_pixels[] = {
        0, 0, 0, 255,
        255, 255, 255, 255,
        0, 0, 0, 255,
    };
    Arena* blur_scratch = arena_create((ArenaCreation){0});
    BUSTER_TEST(arguments, rendering_blur_rgba8(blur_scratch, blur_pixels, 3, 1, 12, 1));
    BUSTER_TEST(arguments, blur_pixels[0] == 85 && blur_pixels[4] == 85 && blur_pixels[8] == 85);
    BUSTER_TEST(arguments, blur_pixels[3] == 255 && blur_pixels[7] == 255 && blur_pixels[11] == 255);
    u8 clamped_blur_pixels[] = {
        0, 0, 0, 255,
        255, 255, 255, 255,
        0, 0, 0, 255,
    };
    u8 maximum_blur_pixels[] = {
        0, 0, 0, 255,
        255, 255, 255, 255,
        0, 0, 0, 255,
    };
    BUSTER_TEST(arguments, rendering_blur_rgba8(blur_scratch, clamped_blur_pixels, 3, 1, 12, RENDERING_MAX_BLUR_RADIUS + 1));
    BUSTER_TEST(arguments, rendering_blur_rgba8(blur_scratch, maximum_blur_pixels, 3, 1, 12, RENDERING_MAX_BLUR_RADIUS));
    BUSTER_TEST(arguments, memcmp(clamped_blur_pixels, maximum_blur_pixels, sizeof(clamped_blur_pixels)) == 0);
    BUSTER_TEST(arguments, !rendering_blur_rgba8(blur_scratch, blur_pixels, 3, 1, 11, 1));
    arena_destroy(blur_scratch, 1);
    arena_destroy(stream_vertex_arena, 1);
    arena_destroy(stream_index_arena, 1);

    return result;
}
