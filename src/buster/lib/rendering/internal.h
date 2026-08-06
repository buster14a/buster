#pragma once

#include <string.h>

#include <buster/lib/rendering.h>
#include <buster/lib/string.h>
#include <buster/lib/os.h>
#include <buster/lib/arena.h>
#include <buster/lib/file.h>
#include <buster/lib/float.h>
#include <buster/lib/font_provider.h>
#include <buster/lib/window.h>
#include <buster/lib/shaders/rect_shared.h>
#include <buster/lib/shaders/blur_shared.h>
#include <buster/lib/shaders/paths.h>

#ifndef BUSTER_USE_SLANG_SHADERS
#define BUSTER_USE_SLANG_SHADERS 0
#endif

#if defined(__APPLE__)
#include <buster/lib/apple_runtime.h>
#endif

#if BUSTER_USE_METAL && BUSTER_USE_SLANG_SHADERS
#include <buster/lib/shaders/metal.h>
#endif

#if BUSTER_USE_D3D12 && BUSTER_USE_SLANG_SHADERS
#include <buster/lib/shaders/d3d12.h>
#endif

typedef struct RectVertex RectVertex;
typedef struct BlurConstants BlurConstants;
typedef struct RenderingUvCoordinate RenderingUvCoordinate;
struct RenderingUvCoordinate
{
    f32 x;
    f32 y;
};

typedef enum BusterPipeline
{
    BUSTER_PIPELINE_RECT,
    BUSTER_PIPELINE_COUNT,
} BusterPipeline;

#define RENDERING_MAX_DRAW_COUNT (4096)
#define RENDERING_MAX_BATCH_COUNT (RENDERING_MAX_DRAW_COUNT)
#define RENDERING_MAX_CLIP_DEPTH (64)
#define RENDERING_MAX_VERTEX_COUNT (RENDERING_MAX_DRAW_COUNT * 4)
#define RENDERING_MAX_INDEX_COUNT (RENDERING_MAX_DRAW_COUNT * 6)
#define RENDERING_MAX_BLUR_RADIUS (BUSTER_BLUR_MAX_RADIUS)
#define RENDERING_MAX_BLUR_PIXELS (4 * 1024 * 1024)
#define RENDERING_MAX_BLUR_PASS_SET_COUNT (RENDERING_MAX_DRAW_COUNT * 3)
#define RENDERING_MAX_WINDOW_COUNT (8)
#define RENDERING_TARGET_BACKBUFFER (0)
#define RENDERING_RESOURCE_SLOT_COUNT RECT_TEXTURE_SLOT_COUNT

// The self-hosted C frontend does not yet constant-evaluate vector-backed
// sizeof/offsetof expressions; native C toolchains enforce the ABI here.
#if !defined(__BUSTER__)
BUSTER_CT_CHECK(sizeof(BlurConstants) == 64);
BUSTER_CT_CHECK(BUSTER_OFFSET_OF(BlurConstants, texel_step) == 0);
BUSTER_CT_CHECK(BUSTER_OFFSET_OF(BlurConstants, radius) == 8);
BUSTER_CT_CHECK(BUSTER_OFFSET_OF(BlurConstants, vertical) == 12);
BUSTER_CT_CHECK(BUSTER_OFFSET_OF(BlurConstants, mask_rect) == 16);
BUSTER_CT_CHECK(BUSTER_OFFSET_OF(BlurConstants, corner_radii) == 32);
BUSTER_CT_CHECK(BUSTER_OFFSET_OF(BlurConstants, target_size) == 48);
BUSTER_CT_CHECK(BUSTER_OFFSET_OF(BlurConstants, composite) == 56);
BUSTER_CT_CHECK(BUSTER_OFFSET_OF(BlurConstants, reserved) == 60);
#endif

typedef struct RenderingResourceBindings RenderingResourceBindings;
struct RenderingResourceBindings
{
    TextureIndex textures[RENDERING_RESOURCE_SLOT_COUNT];
};

typedef enum RenderingCommandKind
{
    RENDERING_COMMAND_RECT,
    RENDERING_COMMAND_CLIP_PUSH,
    RENDERING_COMMAND_CLIP_POP,
    RENDERING_COMMAND_FLUSH,
    RENDERING_COMMAND_RESOURCE,
    RENDERING_COMMAND_TARGET,
    RENDERING_COMMAND_BACKGROUND_BLUR,
    RENDERING_COMMAND_KIND_COUNT,
} RenderingCommandKind;

typedef enum RenderingBackendKind
{
    RENDERING_BACKEND_NULL,
    RENDERING_BACKEND_VULKAN,
    RENDERING_BACKEND_METAL,
    RENDERING_BACKEND_D3D12,
    RENDERING_BACKEND_KIND_COUNT,
} RenderingBackendKind;

typedef struct RenderingCommand RenderingCommand;
struct RenderingCommand
{
    RenderingCommandKind kind;
    BusterPipeline pipeline;
    u32 first_index;
    u32 index_count;
    TextureIndex texture;
    RenderingClipRect clip;
    RenderingResourceBindings resources;
    RenderingClipRect blur_rect;
    float4 blur_corner_radii;
    u32 batch_index;
    u32 resource_slot;
    u32 target;
    u32 blur_radius;
};

typedef struct RenderingBatch RenderingBatch;
struct RenderingBatch
{
    BusterPipeline pipeline;
    u32 first_index;
    u32 index_count;
    TextureIndex texture;
    RenderingClipRect clip;
    RenderingResourceBindings resources;
    u32 target;
    u8 reserved[4];
};

typedef struct RenderingBackendExecutionTrace RenderingBackendExecutionTrace;
struct RenderingBackendExecutionTrace
{
    RenderingBackendKind backend;
    u32 blur_occurrence;
    u32 blur_pass_count;
    u32 blur_capture_pass_count;
    u32 blur_horizontal_pass_count;
    u32 blur_vertical_pass_count;
    u32 descriptor_snapshot_count;
    u32 state_restore_count;
    u32 consumed_command_count;
    bool valid;
    bool backend_executed;
    bool consumed_order_preserved;
    bool resources_snapshot;
    bool target_boundaries;
    bool descriptor_snapshots;
    bool state_restored;
    bool submitted;
    bool presented;
    bool error_frame;
    bool failure_propagated;
};

typedef struct RenderingCommandStream RenderingCommandStream;
struct RenderingCommandStream
{
    Arena* vertex_cpu;
    Arena* index_cpu;
    RenderingCommand commands[RENDERING_MAX_DRAW_COUNT];
    RenderingBatch batches[RENDERING_MAX_BATCH_COUNT];
    RenderingClipRect clip_stack[RENDERING_MAX_CLIP_DEPTH];
    RenderingResourceBindings resources;
    RenderingScale scale;
    RenderingWindowSize target_size;
    u32 command_count;
    u32 batch_count;
    u32 clip_depth;
    u32 clip_overflow_depth;
    u32 vertex_count;
    u32 index_count;
    u32 target;
    bool frame_active;
    bool resources_initialized;
    bool force_new_batch;
    bool overflowed;
    bool render_failed;
    RenderingBackendExecutionTrace backend_trace;
};

typedef enum RenderingReplayEventKind
{
    RENDERING_REPLAY_DRAW,
    RENDERING_REPLAY_CLIP_PUSH,
    RENDERING_REPLAY_CLIP_POP,
    RENDERING_REPLAY_FLUSH,
    RENDERING_REPLAY_RESOURCE,
    RENDERING_REPLAY_TARGET,
    RENDERING_REPLAY_BACKGROUND_BLUR,
    RENDERING_REPLAY_EVENT_KIND_COUNT,
} RenderingReplayEventKind;

typedef struct RenderingReplayEvent RenderingReplayEvent;
struct RenderingReplayEvent
{
    RenderingReplayEventKind kind;
    BusterPipeline pipeline;
    u32 command_index;
    u32 batch_index;
    TextureIndex texture;
    RenderingClipRect clip;
    RenderingClipRect blur_rect;
    float4 blur_corner_radii;
    RenderingResourceBindings resources;
    u32 target;
    u32 radius;
};

typedef struct RenderingBackendReplayResult RenderingBackendReplayResult;
struct RenderingBackendReplayResult
{
    RenderingBackendKind backend;
    u32 event_count;
    u32 draw_count;
    u32 blur_pass_count;
    u32 blur_capture_pass_count;
    u32 blur_horizontal_pass_count;
    u32 blur_vertical_pass_count;
    u32 descriptor_snapshot_count;
    u32 state_restore_count;
    u32 consumed_command_count;
    bool valid;
    bool order_preserved;
    bool resources_snapshot;
    bool target_boundaries;
    bool state_restored;
    bool backend_executed;
    bool consumed_order_preserved;
    bool failure_propagated;
    bool submitted;
    bool presented;
    bool descriptor_snapshots;
    bool error_frame;
    u8 reserved[2];
};

typedef struct RenderingBlurPlan RenderingBlurPlan;
struct RenderingBlurPlan
{
    RenderingClipRect rect;
    u32 source_width;
    u32 source_height;
    u32 half_width;
    u32 half_height;
    u32 radius;
    u32 pass_count;
    bool captures_current_target;
    bool valid;
    u8 reserved[2];
};

typedef struct RenderingBlurDimensions RenderingBlurDimensions;
struct RenderingBlurDimensions
{
    u32 source_width;
    u32 source_height;
    u32 half_width;
    u32 half_height;
    bool valid;
    u8 reserved[3];
};

typedef struct RenderingBlurDescriptorBindings RenderingBlurDescriptorBindings;
struct RenderingBlurDescriptorBindings
{
    u32 horizontal;
    u32 vertical;
    u32 downsample;
    bool valid;
    bool stable;
    u8 reserved[2];
};

typedef struct RenderingDescriptorRange RenderingDescriptorRange;
struct RenderingDescriptorRange
{
    u32 base;
    u32 length;
    bool valid;
    u8 reserved[3];
};

BUSTER_F_DECL RenderingCommandStream* rendering_window_command_stream(RenderingWindowHandle* window);
BUSTER_F_DECL void rendering_window_set_content_scale_internal(RenderingWindowHandle* window, RenderingScale scale);
BUSTER_F_DECL void rendering_command_stream_bind_buffers(RenderingCommandStream* stream, Arena* vertex_cpu, Arena* index_cpu);
BUSTER_F_DECL void rendering_command_stream_begin(RenderingCommandStream* stream, RenderingWindowSize target_size, RenderingScale scale);
BUSTER_F_DECL void rendering_command_stream_set_scale(RenderingCommandStream* stream, RenderingScale scale);
BUSTER_F_DECL void rendering_command_stream_push_clip(RenderingCommandStream* stream, F32Interval2 rect);
BUSTER_F_DECL void rendering_command_stream_pop_clip(RenderingCommandStream* stream);
BUSTER_F_DECL void rendering_command_stream_reset_clip(RenderingCommandStream* stream);
BUSTER_F_DECL void rendering_command_stream_record_rect(RenderingCommandStream* stream, BusterPipeline pipeline, TextureIndex texture, u32 first_index,
                                                         u32 index_count);
BUSTER_F_DECL void rendering_command_stream_record_clip(RenderingCommandStream* stream, RenderingCommandKind kind, RenderingClipRect clip);
BUSTER_F_DECL void rendering_command_stream_record_flush(RenderingCommandStream* stream);
BUSTER_F_DECL bool rendering_command_stream_set_texture_binding(RenderingCommandStream* stream, u32 slot, TextureIndex texture);
BUSTER_F_DECL bool rendering_command_stream_record_target(RenderingCommandStream* stream, u32 target);
BUSTER_F_DECL bool rendering_command_stream_record_background_blur(RenderingCommandStream* stream, F32Interval2 rect, u32 radius);
BUSTER_F_DECL bool rendering_command_stream_record_background_blur_rounded(RenderingCommandStream* stream, F32Interval2 rect, u32 radius, float4 corner_radii);
BUSTER_F_DECL bool rendering_command_stream_command_ends_batch(RenderingCommandStream* stream, u32 command_index);
BUSTER_F_DECL void rendering_command_stream_mark_failure(RenderingCommandStream* stream);
BUSTER_F_DECL bool rendering_command_stream_is_valid(RenderingCommandStream* stream);
BUSTER_F_DECL void rendering_backend_trace_begin(RenderingCommandStream* stream, RenderingBackendKind backend);
BUSTER_F_DECL void rendering_backend_trace_record_command(RenderingCommandStream* stream, u32 command_index);
BUSTER_F_DECL void rendering_backend_trace_command(RenderingCommandStream* stream, u32 command_index, RenderingCommand command);
BUSTER_F_DECL bool rendering_backend_trace_preflight(RenderingCommandStream* stream);
BUSTER_F_DECL void rendering_backend_trace_finish(RenderingCommandStream* stream, bool submitted, bool presented, bool error_frame);
BUSTER_F_DECL bool rendering_backend_trace_validate_common(RenderingCommandStream* stream, u32 command_index, RenderingCommand command);
BUSTER_F_DECL void rendering_backend_trace_copy_result(RenderingBackendReplayResult* result, RenderingCommandStream* stream);
BUSTER_F_DECL void rendering_frame_error_commit(bool* last_frame_error, bool frame_error);
BUSTER_F_DECL bool rendering_frame_error_query(bool* last_frame_error);
BUSTER_F_DECL bool rendering_window_has_rendering_error_internal(RenderingWindowHandle* window);
BUSTER_F_DECL RenderingBlurDimensions rendering_blur_dimensions_make(RenderingWindowSize target_size);
BUSTER_F_DECL RenderingBlurDescriptorBindings rendering_blur_descriptor_bindings(u32 occurrence);
BUSTER_F_DECL RenderingDescriptorRange rendering_descriptor_range_make(u32 descriptor_base, u32 window_slot, u32 window_count, u32 window_length);
BUSTER_F_DECL bool rendering_arena_allocation_fits(Arena* arena, u64 size, u64 alignment);
BUSTER_F_DECL RenderingBlurPlan rendering_blur_plan_make(RenderingWindowSize target_size, RenderingClipRect rect, u32 radius);
BUSTER_F_DECL u32 rendering_command_stream_replay(RenderingCommandStream* stream, RenderingReplayEvent* events, u32 capacity);
BUSTER_F_DECL RenderingBackendReplayResult rendering_backend_replay_policy(RenderingCommandStream* stream, RenderingBackendKind backend,
                                                                                  RenderingReplayEvent* events, u32 capacity);
BUSTER_F_DECL RenderingBackendReplayResult rendering_backend_replay_for_test(RenderingCommandStream* stream, RenderingReplayEvent* events, u32 capacity);
BUSTER_F_DECL bool rendering_command_stream_add_vertices(RenderingCommandStream* stream, ByteSlice vertex_memory, u32 vertex_count);
BUSTER_F_DECL bool rendering_command_stream_add_indices(RenderingCommandStream* stream, Sliceu32 indices);
BUSTER_F_DECL bool rendering_command_stream_rect_allocation_fits(RenderingCommandStream* stream, BusterPipeline pipeline, TextureIndex texture,
                                                                       u32 first_index, u32 index_count, u64 vertex_bytes, u32 vertex_count);
BUSTER_F_DECL bool rendering_vulkan_device_functions_loaded_for_test(bool core_loaded, bool clear_attachments_loaded, bool blit_image_loaded);
BUSTER_F_DECL bool rendering_window_set_size_for_test(RenderingWindowHandle* window, RenderingWindowSize size);
BUSTER_F_DECL RenderingUvCoordinate rendering_rect_uv_for_quad(RectVertex vertex, u32 quad_vertex_index);
BUSTER_F_DECL bool rendering_scale_is_valid(RenderingScale scale);

typedef enum RenderingVulkanDeviceType
{
    RENDERING_VULKAN_DEVICE_TYPE_OTHER,
    RENDERING_VULKAN_DEVICE_TYPE_INTEGRATED,
    RENDERING_VULKAN_DEVICE_TYPE_DISCRETE,
    RENDERING_VULKAN_DEVICE_TYPE_VIRTUAL,
    RENDERING_VULKAN_DEVICE_TYPE_CPU,
    RENDERING_VULKAN_DEVICE_TYPE_COUNT,
} RenderingVulkanDeviceType;

typedef struct RenderingVulkanQueueFamilyCandidate RenderingVulkanQueueFamilyCandidate;
struct RenderingVulkanQueueFamilyCandidate
{
    u32 queue_count;
    bool graphics;
    bool present;
    u8 reserved[2];
};

typedef struct RenderingVulkanQueueFamilySelection RenderingVulkanQueueFamilySelection;
struct RenderingVulkanQueueFamilySelection
{
    u32 graphics_family_index;
    u32 present_family_index;
    bool eligible;
    u8 reserved[3];
};

typedef struct RenderingVulkanDeviceCandidate RenderingVulkanDeviceCandidate;
struct RenderingVulkanDeviceCandidate
{
    String8 name;
    u32 vendor_id;
    u32 device_id;
    u32 enumeration_index;
    RenderingVulkanDeviceType device_type;
    RenderingVulkanQueueFamilySelection queues;
    bool has_required_extension;
    bool has_required_features;
    bool has_surface_support;
    bool excluded;
    u8 reserved[4];
};

typedef struct RenderingVulkanDeviceSelection RenderingVulkanDeviceSelection;
struct RenderingVulkanDeviceSelection
{
    u32 candidate_index;
    u64 score;
    bool found;
    u8 reserved[3];
};

typedef struct RenderingVulkanQueueFamilyCandidateSlice RenderingVulkanQueueFamilyCandidateSlice;
struct RenderingVulkanQueueFamilyCandidateSlice
{
    RenderingVulkanQueueFamilyCandidate* pointer;
    u64 length;
};

typedef struct RenderingVulkanDeviceCandidateSlice RenderingVulkanDeviceCandidateSlice;
struct RenderingVulkanDeviceCandidateSlice
{
    RenderingVulkanDeviceCandidate* pointer;
    u64 length;
};

typedef struct RenderingVulkanSurfaceCompatibility RenderingVulkanSurfaceCompatibility;
struct RenderingVulkanSurfaceCompatibility
{
    bool queue_setup;
    bool present_queue;
    bool capabilities;
    bool format;
    bool present_modes;
    bool usage;
    bool image_count;
    bool composite_alpha;
};

BUSTER_F_DECL bool rendering_vulkan_window_requires_device_initialization(bool device_initialized);
BUSTER_F_DECL bool rendering_vulkan_existing_surface_is_compatible(RenderingVulkanSurfaceCompatibility compatibility);
BUSTER_F_DECL bool rendering_vulkan_surface_format_sentinel_is_compatible(u32 available_color_space, u32 selected_color_space);
BUSTER_F_DECL bool rendering_vulkan_enumeration_needs_retry(bool incomplete, u32 capacity, u32 reported_count);
BUSTER_F_DECL bool rendering_vulkan_queue_family_enumeration_needs_retry(u32 capacity, u32 reported_count, u32 available_count);
BUSTER_F_DECL RenderingVulkanQueueFamilySelection rendering_vulkan_select_queue_families(RenderingVulkanQueueFamilyCandidateSlice candidates);
BUSTER_F_DECL bool rendering_vulkan_device_candidate_is_eligible(RenderingVulkanDeviceCandidate candidate);
BUSTER_F_DECL u64 rendering_vulkan_device_score(RenderingVulkanDeviceCandidate candidate);
BUSTER_F_DECL RenderingVulkanDeviceSelection rendering_vulkan_select_device(RenderingVulkanDeviceCandidateSlice candidates);

#define BUSTER_GPU_VALIDATION_ENABLED (!BUSTER_OPTIMIZE || BUSTER_SANITIZE)
