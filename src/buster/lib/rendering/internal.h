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
#define RENDERING_MAX_BLUR_RADIUS (32)
#define RENDERING_MAX_BLUR_PIXELS (4 * 1024 * 1024)

typedef enum RenderingCommandKind
{
    RENDERING_COMMAND_RECT,
    RENDERING_COMMAND_CLIP_PUSH,
    RENDERING_COMMAND_CLIP_POP,
    RENDERING_COMMAND_FLUSH,
    RENDERING_COMMAND_KIND_COUNT,
} RenderingCommandKind;

typedef struct RenderingCommand RenderingCommand;
struct RenderingCommand
{
    RenderingCommandKind kind;
    BusterPipeline pipeline;
    u32 first_index;
    u32 index_count;
    TextureIndex texture;
    RenderingClipRect clip;
};

typedef struct RenderingBatch RenderingBatch;
struct RenderingBatch
{
    BusterPipeline pipeline;
    u32 first_index;
    u32 index_count;
    TextureIndex texture;
    RenderingClipRect clip;
};

typedef struct RenderingCommandStream RenderingCommandStream;
struct RenderingCommandStream
{
    Arena* vertex_cpu;
    Arena* index_cpu;
    RenderingCommand commands[RENDERING_MAX_DRAW_COUNT];
    RenderingBatch batches[RENDERING_MAX_BATCH_COUNT];
    RenderingClipRect clip_stack[RENDERING_MAX_CLIP_DEPTH];
    RenderingScale scale;
    RenderingWindowSize target_size;
    u32 command_count;
    u32 batch_count;
    u32 clip_depth;
    u32 vertex_count;
    u32 index_count;
    bool force_new_batch;
    bool overflowed;
    u8 reserved[2];
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

BUSTER_TEST_F_DECL bool rendering_vulkan_window_requires_device_initialization(bool device_initialized);
BUSTER_TEST_F_DECL bool rendering_vulkan_existing_surface_is_compatible(RenderingVulkanSurfaceCompatibility compatibility);
BUSTER_TEST_F_DECL bool rendering_vulkan_surface_format_sentinel_is_compatible(u32 available_color_space, u32 selected_color_space);
BUSTER_TEST_F_DECL bool rendering_vulkan_enumeration_needs_retry(bool incomplete, u32 capacity, u32 reported_count);
BUSTER_TEST_F_DECL bool rendering_vulkan_queue_family_enumeration_needs_retry(u32 capacity, u32 reported_count, u32 available_count);
BUSTER_TEST_F_DECL RenderingVulkanQueueFamilySelection rendering_vulkan_select_queue_families(RenderingVulkanQueueFamilyCandidateSlice candidates);
BUSTER_TEST_F_DECL bool rendering_vulkan_device_candidate_is_eligible(RenderingVulkanDeviceCandidate candidate);
BUSTER_TEST_F_DECL u64 rendering_vulkan_device_score(RenderingVulkanDeviceCandidate candidate);
BUSTER_TEST_F_DECL RenderingVulkanDeviceSelection rendering_vulkan_select_device(RenderingVulkanDeviceCandidateSlice candidates);

#define BUSTER_GPU_VALIDATION_ENABLED (!BUSTER_OPTIMIZE || BUSTER_SANITIZE)
