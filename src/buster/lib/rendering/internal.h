#pragma once

#include <string.h>

#include <buster/lib/rendering.h>
#include <buster/lib/string.h>
#include <buster/lib/os.h>
#include <buster/lib/arena.h>
#include <buster/lib/file.h>
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
BUSTER_TEST_F_DECL bool rendering_vulkan_device_is_better(RenderingVulkanDeviceCandidate* candidate, RenderingVulkanDeviceCandidate* current,
                                                          u64 candidate_score, u64 current_score);
BUSTER_TEST_F_DECL RenderingVulkanDeviceSelection rendering_vulkan_select_device(RenderingVulkanDeviceCandidateSlice candidates);

#define BUSTER_GPU_VALIDATION_ENABLED (!BUSTER_OPTIMIZE || BUSTER_SANITIZE)
