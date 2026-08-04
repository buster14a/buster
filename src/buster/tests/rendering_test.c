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
    BUSTER_TEST(arguments, rendering_vulkan_device_functions_loaded_for_test(true, true, true));
    BUSTER_TEST(arguments, !rendering_vulkan_device_functions_loaded_for_test(true, true, false));
    BUSTER_TEST(arguments, !rendering_vulkan_device_functions_loaded_for_test(true, false, true));

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
    BUSTER_TEST(arguments, rendering_clip_rect_is_empty(rendering_clip_rect_from_f32((F32Interval2){.x0 = 5, .y0 = 1, .x1 = 4, .y1 = 9},
                                                                                          (RenderingScale){.x = 1, .y = 1},
                                                                                          (RenderingWindowSize){.width = 20, .height = 20})));
    union
    {
        u32 bits;
        f32 value;
    } positive_infinity = {.bits = 0x7f800000}, negative_infinity = {.bits = 0xff800000}, quiet_nan = {.bits = 0x7fc00000}, maximum_finite = {.bits = 0x7f7fffff};
    BUSTER_TEST(arguments, rendering_scale_is_valid((RenderingScale){.x = 1, .y = 1}));
    BUSTER_TEST(arguments, !rendering_scale_is_valid((RenderingScale){.x = quiet_nan.value, .y = 1}));
    BUSTER_TEST(arguments, !rendering_scale_is_valid((RenderingScale){.x = 1, .y = quiet_nan.value}));
    BUSTER_TEST(arguments, !rendering_scale_is_valid((RenderingScale){.x = positive_infinity.value, .y = 1}));
    BUSTER_TEST(arguments, !rendering_scale_is_valid((RenderingScale){.x = 1, .y = positive_infinity.value}));
    BUSTER_TEST(arguments, !rendering_scale_is_valid((RenderingScale){.x = negative_infinity.value, .y = 1}));
    BUSTER_TEST(arguments, !rendering_scale_is_valid((RenderingScale){.x = 1, .y = negative_infinity.value}));
    BUSTER_TEST(arguments, !rendering_scale_is_valid((RenderingScale){.x = 0, .y = 1}));
    BUSTER_TEST(arguments, !rendering_scale_is_valid((RenderingScale){.x = -1, .y = 1}));
    BUSTER_TEST(arguments, rendering_scale_is_valid((RenderingScale){.x = 3.0e+20f, .y = 2.0e+20f}));
    BUSTER_TEST(arguments, rendering_scale_is_valid((RenderingScale){.x = maximum_finite.value, .y = maximum_finite.value}));
    BUSTER_TEST(arguments, rendering_clip_rect_is_empty(rendering_clip_rect_intersect((RenderingClipRect){.x0 = 4, .y0 = 4, .x1 = 4, .y1 = 8},
                                                                                       (RenderingClipRect){.x0 = 0, .y0 = 0, .x1 = 10, .y1 = 10})));

    bool last_frame_error = false;
    rendering_frame_error_commit(&last_frame_error, true);
    BUSTER_TEST(arguments, rendering_frame_error_query(&last_frame_error));
    rendering_frame_error_commit(&last_frame_error, false);
    BUSTER_TEST(arguments, !rendering_frame_error_query(&last_frame_error));
    RenderingDescriptorRange descriptor_range0 = rendering_descriptor_range_make(16, 0, RENDERING_MAX_WINDOW_COUNT, 100);
    RenderingDescriptorRange descriptor_range1 = rendering_descriptor_range_make(16, 1, RENDERING_MAX_WINDOW_COUNT, 100);
    RenderingDescriptorRange descriptor_range_reused = rendering_descriptor_range_make(16, 0, RENDERING_MAX_WINDOW_COUNT, 100);
    RenderingDescriptorRange descriptor_range_invalid = rendering_descriptor_range_make(16, RENDERING_MAX_WINDOW_COUNT, RENDERING_MAX_WINDOW_COUNT, 100);
    BUSTER_TEST(arguments, descriptor_range0.valid && descriptor_range1.valid && descriptor_range0.base == 16 && descriptor_range1.base == 116 &&
                                  descriptor_range0.base != descriptor_range1.base && descriptor_range0.base == descriptor_range_reused.base &&
                                  !descriptor_range_invalid.valid);
    RenderingBlurDescriptorBindings blur_bindings0 = rendering_blur_descriptor_bindings(0);
    RenderingBlurDescriptorBindings blur_bindings1 = rendering_blur_descriptor_bindings(1);
    BUSTER_TEST(arguments, blur_bindings0.valid && blur_bindings0.stable && blur_bindings0.horizontal == 0 && blur_bindings0.vertical == 1 &&
                                  blur_bindings0.downsample == 2 && blur_bindings1.valid && blur_bindings1.horizontal == 3 && blur_bindings1.vertical == 4 &&
                                  blur_bindings1.downsample == 5);
    BUSTER_TEST(arguments, !rendering_blur_descriptor_bindings(RENDERING_MAX_DRAW_COUNT).valid);

    Arena* stream_vertex_arena = arena_create((ArenaCreation){0});
    Arena* stream_index_arena = arena_create((ArenaCreation){0});
    Arena* boundary_vertex_arena = arena_create((ArenaCreation){.reserved_size = 256, .granularity = 64, .initial_size = 256});
    Arena* boundary_index_arena = arena_create((ArenaCreation){.reserved_size = 256, .granularity = 64, .initial_size = 256});
    RenderingCommandStream* boundary_stream = arena_allocate(arguments->arena, RenderingCommandStream, 1);
    memset(boundary_stream, 0, sizeof(*boundary_stream));
    rendering_command_stream_bind_buffers(boundary_stream, boundary_vertex_arena, boundary_index_arena);
    u8 boundary_vertices[17] = {0};
    arena_set_position(boundary_vertex_arena, 240);
    BUSTER_TEST(arguments, rendering_command_stream_add_vertices(boundary_stream, (ByteSlice){.pointer = boundary_vertices, .length = 15}, 1) &&
                                  boundary_vertex_arena->position == 255 && boundary_stream->vertex_count == 1);
    boundary_stream->vertex_count = 0;
    boundary_stream->overflowed = false;
    arena_set_position(boundary_vertex_arena, 240);
    BUSTER_TEST(arguments, rendering_command_stream_add_vertices(boundary_stream, (ByteSlice){.pointer = boundary_vertices, .length = 16}, 1) &&
                                  boundary_vertex_arena->position == 256 && boundary_stream->vertex_count == 1);
    boundary_stream->vertex_count = 0;
    boundary_stream->overflowed = false;
    arena_set_position(boundary_vertex_arena, 240);
    BUSTER_TEST(arguments, !rendering_command_stream_add_vertices(boundary_stream, (ByteSlice){.pointer = boundary_vertices, .length = 17}, 1) &&
                                  boundary_vertex_arena->position == 240 && boundary_stream->vertex_count == 0 &&
                                  rendering_command_stream_replay(boundary_stream, 0, 0) == 0);
    u32 boundary_indices[3] = {0, 1, 2};
    boundary_stream->index_count = 0;
    boundary_stream->overflowed = false;
    arena_set_position(boundary_index_arena, 248);
    BUSTER_TEST(arguments, rendering_command_stream_add_indices(boundary_stream, (Sliceu32){.pointer = boundary_indices, .length = 1}) &&
                                  boundary_index_arena->position == 252 && boundary_stream->index_count == 1);
    boundary_stream->index_count = 0;
    boundary_stream->overflowed = false;
    arena_set_position(boundary_index_arena, 248);
    BUSTER_TEST(arguments, rendering_command_stream_add_indices(boundary_stream, (Sliceu32){.pointer = boundary_indices, .length = 2}) &&
                                  boundary_index_arena->position == 256 && boundary_stream->index_count == 2);
    boundary_stream->index_count = 0;
    boundary_stream->overflowed = false;
    arena_set_position(boundary_index_arena, 248);
    BUSTER_TEST(arguments, !rendering_command_stream_add_indices(boundary_stream, (Sliceu32){.pointer = boundary_indices, .length = 3}) &&
                                  boundary_index_arena->position == 248 && boundary_stream->index_count == 0 &&
                                  rendering_command_stream_replay(boundary_stream, 0, 0) == 0);
    RenderingCommandStream* combined_boundary_stream = arena_allocate(arguments->arena, RenderingCommandStream, 1);
    memset(combined_boundary_stream, 0, sizeof(*combined_boundary_stream));
    rendering_command_stream_bind_buffers(combined_boundary_stream, boundary_vertex_arena, boundary_index_arena);
    combined_boundary_stream->target_size = (RenderingWindowSize){.width = 20, .height = 20};
    combined_boundary_stream->scale = (RenderingScale){.x = 1, .y = 1};
    combined_boundary_stream->target = RENDERING_TARGET_BACKBUFFER;
    combined_boundary_stream->clip_depth = 1;
    combined_boundary_stream->clip_stack[0] = (RenderingClipRect){.x0 = 0, .y0 = 0, .x1 = 20, .y1 = 20};
    combined_boundary_stream->force_new_batch = true;
    arena_set_position(boundary_vertex_arena, 240);
    arena_set_position(boundary_index_arena, 248);
    BUSTER_TEST(arguments, rendering_command_stream_rect_allocation_fits(combined_boundary_stream, BUSTER_PIPELINE_RECT, (TextureIndex){.value = 0}, 0, 1, 16, 1));
    arena_set_position(boundary_vertex_arena, 240);
    arena_set_position(boundary_index_arena, 248);
    BUSTER_TEST(arguments, !rendering_command_stream_rect_allocation_fits(combined_boundary_stream, BUSTER_PIPELINE_RECT, (TextureIndex){.value = 0}, 0, 1, 17, 1) &&
                                  boundary_vertex_arena->position == 240 && boundary_index_arena->position == 248);
    BUSTER_TEST(arguments, !rendering_arena_allocation_fits(boundary_vertex_arena, 17, 16));
    arena_destroy(boundary_vertex_arena, 1);
    arena_destroy(boundary_index_arena, 1);
    RenderingCommandStream* stream = arena_allocate(arguments->arena, RenderingCommandStream, 1);
    memset(stream, 0, sizeof(*stream));
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

    RenderingCommandStream* overflow_stream = arena_allocate(arguments->arena, RenderingCommandStream, 1);
    memset(overflow_stream, 0, sizeof(*overflow_stream));
    rendering_command_stream_begin(overflow_stream, (RenderingWindowSize){.width = 100, .height = 100}, (RenderingScale){.x = 1, .y = 1});
    for (u32 clip_index = 1; clip_index < RENDERING_MAX_CLIP_DEPTH; clip_index += 1)
    {
        rendering_command_stream_push_clip(overflow_stream, (F32Interval2){.x0 = 1, .y0 = 1, .x1 = 99, .y1 = 99});
    }
    rendering_command_stream_push_clip(overflow_stream, (F32Interval2){.x0 = 2, .y0 = 2, .x1 = 98, .y1 = 98});
    rendering_command_stream_push_clip(overflow_stream, (F32Interval2){.x0 = 3, .y0 = 3, .x1 = 97, .y1 = 97});
    BUSTER_TEST(arguments, overflow_stream->clip_depth == RENDERING_MAX_CLIP_DEPTH && overflow_stream->clip_overflow_depth == 2);
    BUSTER_TEST(arguments, overflow_stream->overflowed && !rendering_command_stream_is_valid(overflow_stream) &&
                                  rendering_command_stream_replay(overflow_stream, 0, 0) == 0);
    rendering_command_stream_pop_clip(overflow_stream);
    rendering_command_stream_pop_clip(overflow_stream);
    BUSTER_TEST(arguments, overflow_stream->clip_depth == RENDERING_MAX_CLIP_DEPTH && overflow_stream->clip_overflow_depth == 0);
    rendering_command_stream_pop_clip(overflow_stream);
    BUSTER_TEST(arguments, overflow_stream->clip_depth == RENDERING_MAX_CLIP_DEPTH - 1);

    RenderingCommandStream* replay_stream = arena_allocate(arguments->arena, RenderingCommandStream, 1);
    memset(replay_stream, 0, sizeof(*replay_stream));
    rendering_command_stream_begin(replay_stream, (RenderingWindowSize){.width = 200, .height = 100}, (RenderingScale){.x = 2, .y = 1.5f});
    replay_stream->frame_active = false;
    rendering_command_stream_set_texture_binding(replay_stream, 0, (TextureIndex){.value = 10});
    rendering_command_stream_set_texture_binding(replay_stream, 1, (TextureIndex){.value = 11});
    replay_stream->frame_active = true;
    rendering_command_stream_record_rect(replay_stream, BUSTER_PIPELINE_RECT, (TextureIndex){.value = 0}, 0, 6);
    rendering_command_stream_record_rect(replay_stream, BUSTER_PIPELINE_RECT, (TextureIndex){.value = 1}, 6, 6);
    rendering_command_stream_push_clip(replay_stream, (F32Interval2){.x0 = 10, .y0 = 10, .x1 = 80, .y1 = 60});
    rendering_command_stream_record_rect(replay_stream, BUSTER_PIPELINE_RECT, (TextureIndex){.value = 0}, 12, 6);
    rendering_command_stream_pop_clip(replay_stream);
    rendering_command_stream_reset_clip(replay_stream);
    rendering_command_stream_set_texture_binding(replay_stream, 0, (TextureIndex){.value = 12});
    rendering_command_stream_record_flush(replay_stream);
    rendering_command_stream_record_rect(replay_stream, BUSTER_PIPELINE_RECT, (TextureIndex){.value = 0}, 18, 6);
    BUSTER_TEST(arguments, rendering_command_stream_record_target(replay_stream, RENDERING_TARGET_BACKBUFFER));
    BUSTER_TEST(arguments, rendering_command_stream_record_background_blur(replay_stream, (F32Interval2){.x0 = 10, .y0 = 10, .x1 = 80, .y1 = 70},
                                                                            RENDERING_MAX_BLUR_RADIUS + 5));
    BUSTER_TEST(arguments, rendering_command_stream_record_background_blur(replay_stream, (F32Interval2){.x0 = 20, .y0 = 20, .x1 = 60, .y1 = 60}, 2));
    rendering_command_stream_record_rect(replay_stream, BUSTER_PIPELINE_RECT, (TextureIndex){.value = 0}, 24, 6);
    RenderingReplayEvent replay_events[32] = {0};
    RenderingReplayEvent backend_events[32] = {0};
    u32 replay_count = rendering_command_stream_replay(replay_stream, replay_events, BUSTER_ARRAY_LENGTH(replay_events));
    RenderingBackendReplayResult backend_replay = rendering_backend_replay_for_test(replay_stream, backend_events, BUSTER_ARRAY_LENGTH(backend_events));
    BUSTER_TEST(arguments, replay_count == 13 && backend_replay.valid && backend_replay.event_count == replay_count);
#if BUSTER_USE_VULKAN
    BUSTER_TEST(arguments, backend_replay.backend == RENDERING_BACKEND_VULKAN);
#elif defined(_WIN32) && BUSTER_USE_D3D12
    BUSTER_TEST(arguments, backend_replay.backend == RENDERING_BACKEND_D3D12);
#elif defined(__APPLE__)
    BUSTER_TEST(arguments, backend_replay.backend == RENDERING_BACKEND_METAL);
#else
    BUSTER_TEST(arguments, backend_replay.backend == RENDERING_BACKEND_NULL);
#endif
    BUSTER_TEST(arguments, memcmp(replay_events, backend_events, replay_count * sizeof(replay_events[0])) == 0 && backend_replay.order_preserved &&
                                  backend_replay.resources_snapshot && backend_replay.target_boundaries && backend_replay.state_restored &&
                                  backend_replay.backend_executed && backend_replay.consumed_order_preserved &&
                                  backend_replay.consumed_command_count == replay_count && backend_replay.blur_pass_count == 6 &&
                                  backend_replay.blur_capture_pass_count == 2 && backend_replay.blur_horizontal_pass_count == 2 &&
                                  backend_replay.blur_vertical_pass_count == 2 && backend_replay.state_restore_count == 2);
#if defined(_WIN32) && BUSTER_USE_D3D12
    BUSTER_TEST(arguments, backend_replay.descriptor_snapshots && backend_replay.descriptor_snapshot_count == 6 && !backend_replay.presented &&
                                  !backend_replay.submitted && !backend_replay.error_frame);
#elif defined(__APPLE__)
    BUSTER_TEST(arguments, !backend_replay.descriptor_snapshots && backend_replay.descriptor_snapshot_count == 0 && !backend_replay.presented &&
                                  !backend_replay.submitted && !backend_replay.error_frame);
#elif BUSTER_USE_VULKAN
    BUSTER_TEST(arguments, backend_replay.descriptor_snapshots && backend_replay.descriptor_snapshot_count == 4 && !backend_replay.presented &&
                                  !backend_replay.submitted && !backend_replay.error_frame);
#else
    BUSTER_TEST(arguments, !backend_replay.descriptor_snapshots && backend_replay.descriptor_snapshot_count == 0 && !backend_replay.presented &&
                                  !backend_replay.submitted && !backend_replay.error_frame);
#endif
    BUSTER_TEST(arguments, replay_events[0].kind == RENDERING_REPLAY_DRAW && replay_events[0].pipeline == BUSTER_PIPELINE_RECT);
    BUSTER_TEST(arguments, replay_events[1].kind == RENDERING_REPLAY_DRAW && replay_events[1].pipeline == BUSTER_PIPELINE_RECT);
    BUSTER_TEST(arguments, replay_events[2].kind == RENDERING_REPLAY_CLIP_PUSH && replay_events[2].clip.x0 == 20 && replay_events[2].clip.y0 == 15 &&
                                  replay_events[2].clip.x1 == 160 && replay_events[2].clip.y1 == 90);
    BUSTER_TEST(arguments, replay_events[3].kind == RENDERING_REPLAY_DRAW && replay_events[3].resources.textures[0].value == 10 &&
                                  replay_events[3].clip.x0 == 20 && replay_events[3].clip.y0 == 15);
    BUSTER_TEST(arguments, replay_events[4].kind == RENDERING_REPLAY_CLIP_POP && replay_events[4].clip.x0 == 0 && replay_events[4].clip.y0 == 0 &&
                                  replay_events[4].clip.x1 == 200 && replay_events[4].clip.y1 == 100);
    BUSTER_TEST(arguments, replay_events[5].kind == RENDERING_REPLAY_FLUSH);
    BUSTER_TEST(arguments, replay_events[6].kind == RENDERING_REPLAY_RESOURCE && replay_events[6].resources.textures[0].value == 12);
    BUSTER_TEST(arguments, replay_events[7].kind == RENDERING_REPLAY_FLUSH);
    BUSTER_TEST(arguments, replay_events[8].kind == RENDERING_REPLAY_DRAW && replay_events[8].resources.textures[0].value == 12 &&
                                  replay_events[8].clip.x0 == 0 && replay_events[8].clip.y0 == 0);
    BUSTER_TEST(arguments, replay_events[9].kind == RENDERING_REPLAY_TARGET);
    BUSTER_TEST(arguments, replay_events[10].kind == RENDERING_REPLAY_BACKGROUND_BLUR && replay_events[10].radius == RENDERING_MAX_BLUR_RADIUS);
    BUSTER_TEST(arguments, replay_events[11].kind == RENDERING_REPLAY_BACKGROUND_BLUR && replay_events[11].radius == 2);
    BUSTER_TEST(arguments, replay_events[12].kind == RENDERING_REPLAY_DRAW && replay_events[12].resources.textures[0].value == 12 &&
                                  replay_events[12].batch_index != replay_events[8].batch_index);

    RenderingCommandStream* command_overflow_stream = arena_allocate(arguments->arena, RenderingCommandStream, 1);
    memset(command_overflow_stream, 0, sizeof(*command_overflow_stream));
    rendering_command_stream_begin(command_overflow_stream, (RenderingWindowSize){.width = 100, .height = 100}, (RenderingScale){.x = 1, .y = 1});
    for (u32 draw_index = 0; draw_index < RENDERING_MAX_DRAW_COUNT + 1; draw_index += 1)
    {
        rendering_command_stream_record_rect(command_overflow_stream, BUSTER_PIPELINE_RECT, (TextureIndex){.value = 0}, draw_index * 6, 6);
    }
    BUSTER_TEST(arguments, command_overflow_stream->overflowed && !rendering_command_stream_is_valid(command_overflow_stream) &&
                                  rendering_command_stream_replay(command_overflow_stream, 0, 0) == 0);
    RenderingReplayEvent invalid_backend_events[4] = {0};
    RenderingBackendReplayResult invalid_backend_replay =
        rendering_backend_replay_for_test(command_overflow_stream, invalid_backend_events, BUSTER_ARRAY_LENGTH(invalid_backend_events));
    BUSTER_TEST(arguments, !invalid_backend_replay.backend_executed && invalid_backend_replay.consumed_command_count == 0 && !invalid_backend_replay.valid &&
                                  invalid_backend_replay.failure_propagated && invalid_backend_replay.error_frame);
    BUSTER_TEST(arguments, !invalid_backend_replay.submitted && !invalid_backend_replay.presented);

    RenderingCommandStream* invalid_pipeline_stream = arena_allocate(arguments->arena, RenderingCommandStream, 1);
    memset(invalid_pipeline_stream, 0, sizeof(*invalid_pipeline_stream));
    rendering_command_stream_begin(invalid_pipeline_stream, (RenderingWindowSize){.width = 20, .height = 20}, (RenderingScale){.x = 1, .y = 1});
    rendering_command_stream_record_rect(invalid_pipeline_stream, (BusterPipeline)BUSTER_PIPELINE_COUNT, (TextureIndex){.value = 0}, 0, 6);
    BUSTER_TEST(arguments, invalid_pipeline_stream->overflowed && invalid_pipeline_stream->command_count == 0 &&
                                  !rendering_command_stream_is_valid(invalid_pipeline_stream));
    RenderingBackendReplayResult invalid_pipeline_replay = rendering_backend_replay_for_test(invalid_pipeline_stream, invalid_backend_events,
                                                                                               BUSTER_ARRAY_LENGTH(invalid_backend_events));
    BUSTER_TEST(arguments, !invalid_pipeline_replay.valid && invalid_pipeline_replay.failure_propagated && invalid_pipeline_replay.error_frame);
    RenderingCommandStream* backend_invalid_pipeline_stream = arena_allocate(arguments->arena, RenderingCommandStream, 1);
    memset(backend_invalid_pipeline_stream, 0, sizeof(*backend_invalid_pipeline_stream));
    rendering_command_stream_begin(backend_invalid_pipeline_stream, (RenderingWindowSize){.width = 20, .height = 20}, (RenderingScale){.x = 1, .y = 1});
    backend_invalid_pipeline_stream->commands[0] = (RenderingCommand){
        .kind = RENDERING_COMMAND_RECT,
        .pipeline = (BusterPipeline)BUSTER_PIPELINE_COUNT,
        .target = RENDERING_TARGET_BACKBUFFER,
        .batch_index = 0,
    };
    backend_invalid_pipeline_stream->batches[0] = (RenderingBatch){.pipeline = BUSTER_PIPELINE_RECT, .target = RENDERING_TARGET_BACKBUFFER};
    backend_invalid_pipeline_stream->command_count = 1;
    backend_invalid_pipeline_stream->batch_count = 1;
    RenderingBackendReplayResult backend_invalid_pipeline_replay =
        rendering_backend_replay_for_test(backend_invalid_pipeline_stream, invalid_backend_events, BUSTER_ARRAY_LENGTH(invalid_backend_events));
    BUSTER_TEST(arguments, !backend_invalid_pipeline_replay.valid && backend_invalid_pipeline_replay.failure_propagated &&
                                  backend_invalid_pipeline_stream->render_failed);

    RenderingCommandStream* late_invalid_stream = arena_allocate(arguments->arena, RenderingCommandStream, 1);
    memset(late_invalid_stream, 0, sizeof(*late_invalid_stream));
    rendering_command_stream_begin(late_invalid_stream, (RenderingWindowSize){.width = 20, .height = 20}, (RenderingScale){.x = 1, .y = 1});
    rendering_command_stream_record_rect(late_invalid_stream, BUSTER_PIPELINE_RECT, (TextureIndex){.value = 0}, 0, 6);
    late_invalid_stream->commands[late_invalid_stream->command_count] = (RenderingCommand){
        .kind = RENDERING_COMMAND_RECT,
        .pipeline = (BusterPipeline)BUSTER_PIPELINE_COUNT,
        .target = RENDERING_TARGET_BACKBUFFER,
        .batch_index = late_invalid_stream->batches[0].pipeline == BUSTER_PIPELINE_RECT ? 0 : UINT32_MAX,
    };
    late_invalid_stream->command_count += 1;
    RenderingBackendReplayResult late_invalid_replay =
        rendering_backend_replay_for_test(late_invalid_stream, invalid_backend_events, BUSTER_ARRAY_LENGTH(invalid_backend_events));
    BUSTER_TEST(arguments, !late_invalid_replay.valid && late_invalid_replay.backend_executed && late_invalid_replay.consumed_order_preserved &&
                                  late_invalid_replay.consumed_command_count == 2 && late_invalid_replay.failure_propagated && late_invalid_replay.error_frame &&
                                  !late_invalid_replay.submitted && !late_invalid_replay.presented);

    RenderingCommandStream* invalid_target_stream = arena_allocate(arguments->arena, RenderingCommandStream, 1);
    memset(invalid_target_stream, 0, sizeof(*invalid_target_stream));
    rendering_command_stream_begin(invalid_target_stream, (RenderingWindowSize){.width = 20, .height = 20}, (RenderingScale){.x = 1, .y = 1});
    BUSTER_TEST(arguments, !rendering_command_stream_record_target(invalid_target_stream, RENDERING_TARGET_BACKBUFFER + 1) && invalid_target_stream->overflowed);

    RenderingCommandStream* empty_clip_stream = arena_allocate(arguments->arena, RenderingCommandStream, 1);
    memset(empty_clip_stream, 0, sizeof(*empty_clip_stream));
    rendering_command_stream_begin(empty_clip_stream, (RenderingWindowSize){.width = 20, .height = 20}, (RenderingScale){.x = 1, .y = 1});
    rendering_command_stream_push_clip(empty_clip_stream, (F32Interval2){.x0 = 4, .y0 = 4, .x1 = 4, .y1 = 12});
    rendering_command_stream_record_rect(empty_clip_stream, BUSTER_PIPELINE_RECT, (TextureIndex){.value = 0}, 0, 6);
    rendering_command_stream_pop_clip(empty_clip_stream);
    RenderingReplayEvent empty_clip_events[4] = {0};
    u32 empty_clip_event_count = rendering_command_stream_replay(empty_clip_stream, empty_clip_events, BUSTER_ARRAY_LENGTH(empty_clip_events));
    BUSTER_TEST(arguments, empty_clip_event_count == 2 && empty_clip_events[0].kind == RENDERING_REPLAY_CLIP_PUSH &&
                                  empty_clip_events[1].kind == RENDERING_REPLAY_CLIP_POP && empty_clip_stream->batch_count == 0);

    RectVertex uv_vertex = {
        .p0 = float2_make(40, 20),
        .uv0 = float2_make(100, 200),
        .extent = float2_make(200, 100),
        .uv_extent = float2_make(10, 6),
    };
    RenderingUvCoordinate uv_top_left = rendering_rect_uv_for_quad(uv_vertex, 0);
    RenderingUvCoordinate uv_bottom_right = rendering_rect_uv_for_quad(uv_vertex, 3);
    BUSTER_TEST(arguments, uv_top_left.x == 100 && uv_top_left.y == 200 && uv_bottom_right.x == 110 && uv_bottom_right.y == 206);
    RenderingBlurPlan blur_plan = rendering_blur_plan_make((RenderingWindowSize){.width = 101, .height = 51},
                                                            (RenderingClipRect){.x0 = 2, .y0 = 3, .x1 = 90, .y1 = 40}, 100);
    BUSTER_TEST(arguments, blur_plan.valid && blur_plan.captures_current_target && blur_plan.half_width == 51 && blur_plan.half_height == 26 &&
                                  blur_plan.pass_count == 2 && blur_plan.radius == RENDERING_MAX_BLUR_RADIUS);
    BUSTER_TEST(arguments, !rendering_blur_plan_make((RenderingWindowSize){.width = 101, .height = 51}, (RenderingClipRect){0}, 1).valid);
    BUSTER_TEST(arguments, !rendering_blur_plan_make((RenderingWindowSize){.width = UINT32_MAX, .height = UINT32_MAX},
                                                     (RenderingClipRect){.x0 = 1, .y0 = 1, .x1 = 2, .y1 = 2}, 1)
                                      .valid);

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
    u8 edge_blur_pixels[] = {
        255, 0, 0, 255,
        0, 0, 0, 255,
        0, 0, 0, 255,
    };
    BUSTER_TEST(arguments, rendering_blur_rgba8(blur_scratch, edge_blur_pixels, 3, 1, 12, 1));
    BUSTER_TEST(arguments, edge_blur_pixels[0] == 170 && edge_blur_pixels[4] == 85 && edge_blur_pixels[8] == 0);
    BUSTER_TEST(arguments, !rendering_blur_rgba8(blur_scratch, blur_pixels, 3, 1, 11, 1));
    arena_destroy(blur_scratch, 1);

    Arena* blur_boundary_scratch = arena_create((ArenaCreation){.reserved_size = 128, .granularity = 64, .initial_size = 128});
    u8 one_pixel[] = {1, 2, 3, 4};
    arena_set_position(blur_boundary_scratch, 112);
    BUSTER_TEST(arguments, rendering_blur_rgba8(blur_boundary_scratch, one_pixel, 1, 1, 4, 1) && blur_boundary_scratch->position == 116);
    arena_set_position(blur_boundary_scratch, 113);
    BUSTER_TEST(arguments, !rendering_blur_rgba8(blur_boundary_scratch, one_pixel, 1, 1, 4, 1) && blur_boundary_scratch->position == 113);
    arena_set_position(blur_boundary_scratch, 112);
    BUSTER_TEST(arguments, !rendering_blur_rgba8(blur_boundary_scratch, one_pixel, UINT32_MAX, 1, UINT32_MAX, 1) &&
                                  blur_boundary_scratch->position == 112);
    arena_destroy(blur_boundary_scratch, 1);

#if !BUSTER_USE_VULKAN && !(defined(_WIN32) && BUSTER_USE_D3D12) && !defined(__APPLE__) && !BUSTER_ANDROID
    RenderingHandle* public_rendering = rendering_initialize(arguments->arena);
    RenderingWindowHandle* public_window = rendering_window_initialize(arguments->arena, 0, public_rendering, 0);
    BUSTER_TEST(arguments, public_rendering && public_window);
    if (public_window)
    {
        BUSTER_TEST(arguments, rendering_window_set_size_for_test(public_window, (RenderingWindowSize){.width = 64, .height = 64}));
        rendering_window_frame_begin(public_rendering, public_window);
        bool public_blur_recorded = rendering_window_render_background_blur(public_window, (F32Interval2){.x0 = 8, .y0 = 8, .x1 = 56, .y1 = 56}, 2);
        RenderingCommandStream* public_stream = rendering_window_command_stream(public_window);
        BUSTER_TEST(arguments, public_blur_recorded && public_stream && public_stream->command_count == 1);
        rendering_window_frame_end(public_rendering, public_window);
        BUSTER_TEST(arguments, public_stream->backend_trace.backend == RENDERING_BACKEND_NULL && public_stream->backend_trace.backend_executed &&
                                      public_stream->backend_trace.blur_pass_count == 3 && public_stream->backend_trace.error_frame == false &&
                                      !rendering_window_has_rendering_error(public_window));

        rendering_window_frame_begin(public_rendering, public_window);
        rendering_window_clip_push(public_window, (F32Interval2){.x0 = 0, .y0 = 0, .x1 = 0, .y1 = 64});
        bool empty_clip_blur_recorded = rendering_window_render_background_blur(public_window, (F32Interval2){.x0 = 0, .y0 = 0, .x1 = 64, .y1 = 64}, 2);
        rendering_window_clip_pop(public_window);
        public_stream = rendering_window_command_stream(public_window);
        rendering_window_frame_end(public_rendering, public_window);
        BUSTER_TEST(arguments, empty_clip_blur_recorded && public_stream->command_count == 2 && public_stream->backend_trace.blur_pass_count == 0 &&
                                      !public_stream->backend_trace.error_frame && !rendering_window_has_rendering_error(public_window));

        rendering_window_frame_begin(public_rendering, public_window);
        bool offscreen_blur_recorded = rendering_window_render_background_blur(public_window, (F32Interval2){.x0 = 128, .y0 = 128, .x1 = 160, .y1 = 160}, 2);
        public_stream = rendering_window_command_stream(public_window);
        rendering_window_frame_end(public_rendering, public_window);
        BUSTER_TEST(arguments, offscreen_blur_recorded && public_stream->command_count == 0 && public_stream->backend_trace.blur_pass_count == 0 &&
                                      !public_stream->backend_trace.error_frame && !rendering_window_has_rendering_error(public_window));

        rendering_window_frame_begin(public_rendering, public_window);
        bool empty_input_blur_recorded = rendering_window_render_background_blur(public_window, (F32Interval2){.x0 = 24, .y0 = 24, .x1 = 24, .y1 = 48}, 2);
        public_stream = rendering_window_command_stream(public_window);
        rendering_window_frame_end(public_rendering, public_window);
        BUSTER_TEST(arguments, empty_input_blur_recorded && public_stream->command_count == 0 && public_stream->backend_trace.blur_pass_count == 0 &&
                                      !public_stream->backend_trace.error_frame && !rendering_window_has_rendering_error(public_window));

        rendering_window_deinitialize(public_rendering, public_window);
    }
#endif
    arena_destroy(stream_vertex_arena, 1);
    arena_destroy(stream_index_arena, 1);

    return result;
}
