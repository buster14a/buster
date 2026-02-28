#pragma once
#include <buster/rendering.h>
#include <buster/string8.h>
#include <buster/assertion.h>
#include <buster/os.h>
#include <buster/arena.h>
#include <buster/file.h>
#include <buster/font_provider.h>
#include <buster/window.h>
#include <buster/shaders/rect.inc>

typedef struct RectVertex RectVertex;

#define BUSTER_VULKAN_FUNCTION_POINTER(n) PFN_ ## n n
#define BUSTER_GLOBAL_VULKAN_FUNCTION_POINTER(n) BUSTER_GLOBAL_LOCAL __attribute__((used)) BUSTER_VULKAN_FUNCTION_POINTER(n)
#define BUSTER_VULKAN_OS_LOAD_FUNCTION(vulkan_library, function) function = (typeof(function)) os_dynamic_library_function_load(vulkan_library, S8(#function));
#define BUSTER_VULKAN_FUNCTION_LOAD_GENERIC(context, load_function, function) function = (typeof(function)) load_function(context, #function)
#define BUSTER_VULKAN_LOAD_INSTANCE_FUNCTION(instance, function) BUSTER_VULKAN_FUNCTION_LOAD_GENERIC(instance, vkGetInstanceProcAddr, function)
#define BUSTER_VULKAN_LOAD_DEVICE_FUNCTION(device, function) BUSTER_VULKAN_FUNCTION_LOAD_GENERIC(device, vkGetDeviceProcAddr, function)

#ifndef VK_NO_PROTOTYPES
#define VK_NO_PROTOTYPES
#endif

#ifndef VULKAN_H_

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wpadded"
#endif

#define VK_USE_PLATFORM_XCB_KHR
#if defined(VK_USE_PLATFORM_WIN32_KHR)
#include <vulkan/vk_platform.h>
#include <vulkan/vulkan_core.h>

/* When VK_USE_PLATFORM_WIN32_KHR is defined, instead of including vulkan.h directly, we include individual parts of the SDK
* This is necessary to avoid including <windows.h> which is very heavy - it takes 200ms to parse without WIN32_LEAN_AND_MEAN
* and 100ms to parse with it. vulkan_win32.h only needs a few symbols that are easy to redefine ourselves.
*/
typedef unsigned long DWORD;
typedef const wchar_t* LPCWSTR;
typedef void* HANDLE;
typedef struct HINSTANCE__* HINSTANCE;
typedef struct HWND__* HWND;
typedef struct HMONITOR__* HMONITOR;
typedef struct _SECURITY_ATTRIBUTES SECURITY_ATTRIBUTES;


#include <vulkan/vulkan_win32.h>

#ifdef VK_ENABLE_BETA_EXTENSIONS
#include <vulkan/vulkan_beta.h>
#endif
#else
#include <vulkan/vulkan.h>
#endif
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
#endif


ENUM_T(ShaderStage, u8, 
    SHADER_STAGE_VERTEX,
    SHADER_STAGE_FRAGMENT,
);

STRUCT(PushConstantRange)
{
    u16 offset;
    u16 size;
    ShaderStage stage;
    u8 reserved;
};
SLICE(PushConstantRangeSlice, PushConstantRange);

ENUM_T(DescriptorType, u8, 
    DESCRIPTOR_TYPE_IMAGE_PLUS_SAMPLER,
    DESCRIPTOR_TYPE_COUNT,
);

STRUCT(DescriptorSetLayoutBinding)
{
    u8 binding;
    DescriptorType type;
    ShaderStage stage;
    u8 count;
};
SLICE(DescriptorSetLayoutBindingSlice, DescriptorSetLayoutBinding);

STRUCT(DescriptorSetLayoutCreate)
{
    DescriptorSetLayoutBindingSlice bindings;
};
SLICE(DescriptorSetLayoutCreateSlice, DescriptorSetLayoutCreate);


SLICE(PushConstantRangeSlice, PushConstantRange);
STRUCT(PipelineLayoutCreate)
{
    PushConstantRangeSlice push_constant_ranges;
    DescriptorSetLayoutCreateSlice descriptor_set_layouts;
};
SLICE(PipelineLayoutCreateSlice, PipelineLayoutCreate);

STRUCT(GPUDrawPushConstants)
{
    u64 vertex_buffer;
    f32 width;
    f32 height;
};


STRUCT(PipelineCreate)
{
    u16Slice shader_source_indices;
    u16 layout_index;
    u8 reserved[6];
};
SLICE(PipelineCreateSlice, PipelineCreate);
SLICE(String8Slice, String8);

STRUCT(GraphicsPipelinesCreate)
{
    String8Slice shader_binaries;
    PipelineLayoutCreateSlice layouts;
    PipelineCreateSlice pipelines;
};

ENUM(BusterPipeline,
    BUSTER_PIPELINE_RECT,
    BUSTER_PIPELINE_COUNT,
);

#define MAX_SHADER_MODULE_COUNT_PER_PIPELINE (16)

// INSTANCE FUNCTIONS START
// These functions require no instance
BUSTER_GLOBAL_VULKAN_FUNCTION_POINTER(vkGetInstanceProcAddr);
BUSTER_GLOBAL_VULKAN_FUNCTION_POINTER(vkEnumerateInstanceVersion);
BUSTER_GLOBAL_VULKAN_FUNCTION_POINTER(vkEnumerateInstanceLayerProperties);
BUSTER_GLOBAL_VULKAN_FUNCTION_POINTER(vkCreateInstance);

// These functions require an instance as a parameter
BUSTER_GLOBAL_VULKAN_FUNCTION_POINTER(vkGetDeviceProcAddr);
BUSTER_GLOBAL_VULKAN_FUNCTION_POINTER(vkCreateDebugUtilsMessengerEXT);
BUSTER_GLOBAL_VULKAN_FUNCTION_POINTER(vkDestroyDebugUtilsMessengerEXT);
BUSTER_GLOBAL_VULKAN_FUNCTION_POINTER(vkEnumeratePhysicalDevices);
BUSTER_GLOBAL_VULKAN_FUNCTION_POINTER(vkGetPhysicalDeviceMemoryProperties);
BUSTER_GLOBAL_VULKAN_FUNCTION_POINTER(vkGetPhysicalDeviceProperties);
BUSTER_GLOBAL_VULKAN_FUNCTION_POINTER(vkGetPhysicalDeviceQueueFamilyProperties);
BUSTER_GLOBAL_VULKAN_FUNCTION_POINTER(vkGetPhysicalDeviceSurfaceCapabilitiesKHR);
BUSTER_GLOBAL_VULKAN_FUNCTION_POINTER(vkGetPhysicalDeviceSurfacePresentModesKHR);
BUSTER_GLOBAL_VULKAN_FUNCTION_POINTER(vkGetPhysicalDeviceSurfaceSupportKHR);
BUSTER_GLOBAL_VULKAN_FUNCTION_POINTER(vkCreateDevice);

#if defined(VK_KHR_xcb_surface)
BUSTER_GLOBAL_VULKAN_FUNCTION_POINTER(vkCreateXcbSurfaceKHR);
#endif
#if defined(VK_KHR_win32_surface)
BUSTER_GLOBAL_VULKAN_FUNCTION_POINTER(vkCreateWin32SurfaceKHR);
#endif
#if defined(VK_EXT_metal_surface)
BUSTER_GLOBAL_VULKAN_FUNCTION_POINTER(vkCreateMetalSurfaceEXT);
#endif
// INSTANCE FUNCTIONS END

BUSTER_GLOBAL_VULKAN_FUNCTION_POINTER(vkCreateSwapchainKHR);
BUSTER_GLOBAL_VULKAN_FUNCTION_POINTER(vkCmdCopyBuffer2);
BUSTER_GLOBAL_VULKAN_FUNCTION_POINTER(vkAllocateMemory);
BUSTER_GLOBAL_VULKAN_FUNCTION_POINTER(vkCreateBuffer);
BUSTER_GLOBAL_VULKAN_FUNCTION_POINTER(vkGetBufferMemoryRequirements);
BUSTER_GLOBAL_VULKAN_FUNCTION_POINTER(vkBindBufferMemory);
BUSTER_GLOBAL_VULKAN_FUNCTION_POINTER(vkMapMemory);
BUSTER_GLOBAL_VULKAN_FUNCTION_POINTER(vkGetBufferDeviceAddress);
BUSTER_GLOBAL_VULKAN_FUNCTION_POINTER(vkResetFences);
BUSTER_GLOBAL_VULKAN_FUNCTION_POINTER(vkResetCommandBuffer);
BUSTER_GLOBAL_VULKAN_FUNCTION_POINTER(vkBeginCommandBuffer);
BUSTER_GLOBAL_VULKAN_FUNCTION_POINTER(vkEndCommandBuffer);
BUSTER_GLOBAL_VULKAN_FUNCTION_POINTER(vkQueueSubmit2);
BUSTER_GLOBAL_VULKAN_FUNCTION_POINTER(vkWaitForFences);
BUSTER_GLOBAL_VULKAN_FUNCTION_POINTER(vkCreateImage);
BUSTER_GLOBAL_VULKAN_FUNCTION_POINTER(vkGetImageMemoryRequirements);
BUSTER_GLOBAL_VULKAN_FUNCTION_POINTER(vkBindImageMemory);
BUSTER_GLOBAL_VULKAN_FUNCTION_POINTER(vkCreateImageView);
BUSTER_GLOBAL_VULKAN_FUNCTION_POINTER(vkCmdPipelineBarrier2);
BUSTER_GLOBAL_VULKAN_FUNCTION_POINTER(vkCmdBlitImage2);
BUSTER_GLOBAL_VULKAN_FUNCTION_POINTER(vkGetDeviceQueue);
BUSTER_GLOBAL_VULKAN_FUNCTION_POINTER(vkCreateCommandPool);
BUSTER_GLOBAL_VULKAN_FUNCTION_POINTER(vkAllocateCommandBuffers);
BUSTER_GLOBAL_VULKAN_FUNCTION_POINTER(vkCreateFence);
BUSTER_GLOBAL_VULKAN_FUNCTION_POINTER(vkCreateSampler);
BUSTER_GLOBAL_VULKAN_FUNCTION_POINTER(vkDestroySampler);
BUSTER_GLOBAL_VULKAN_FUNCTION_POINTER(vkCreateShaderModule);
BUSTER_GLOBAL_VULKAN_FUNCTION_POINTER(vkDestroyShaderModule);
BUSTER_GLOBAL_VULKAN_FUNCTION_POINTER(vkCreateDescriptorSetLayout);
BUSTER_GLOBAL_VULKAN_FUNCTION_POINTER(vkDestroyDescriptorSetLayout);
BUSTER_GLOBAL_VULKAN_FUNCTION_POINTER(vkCreatePipelineLayout);
BUSTER_GLOBAL_VULKAN_FUNCTION_POINTER(vkDestroyPipelineLayout);
BUSTER_GLOBAL_VULKAN_FUNCTION_POINTER(vkDestroyPipeline);
BUSTER_GLOBAL_VULKAN_FUNCTION_POINTER(vkCreateGraphicsPipelines);
BUSTER_GLOBAL_VULKAN_FUNCTION_POINTER(vkDestroyImageView);
BUSTER_GLOBAL_VULKAN_FUNCTION_POINTER(vkDestroyImage);
BUSTER_GLOBAL_VULKAN_FUNCTION_POINTER(vkDestroyInstance);
BUSTER_GLOBAL_VULKAN_FUNCTION_POINTER(vkDestroyDevice);
BUSTER_GLOBAL_VULKAN_FUNCTION_POINTER(vkDestroySurfaceKHR);
BUSTER_GLOBAL_VULKAN_FUNCTION_POINTER(vkFreeMemory);
BUSTER_GLOBAL_VULKAN_FUNCTION_POINTER(vkDeviceWaitIdle);
BUSTER_GLOBAL_VULKAN_FUNCTION_POINTER(vkDestroySwapchainKHR);
BUSTER_GLOBAL_VULKAN_FUNCTION_POINTER(vkGetSwapchainImagesKHR);
BUSTER_GLOBAL_VULKAN_FUNCTION_POINTER(vkCreateDescriptorPool);
BUSTER_GLOBAL_VULKAN_FUNCTION_POINTER(vkDestroyDescriptorPool);
BUSTER_GLOBAL_VULKAN_FUNCTION_POINTER(vkAllocateDescriptorSets);
BUSTER_GLOBAL_VULKAN_FUNCTION_POINTER(vkCreateSemaphore);
BUSTER_GLOBAL_VULKAN_FUNCTION_POINTER(vkAcquireNextImageKHR);
BUSTER_GLOBAL_VULKAN_FUNCTION_POINTER(vkDestroyBuffer);
BUSTER_GLOBAL_VULKAN_FUNCTION_POINTER(vkDestroyCommandPool);
BUSTER_GLOBAL_VULKAN_FUNCTION_POINTER(vkDestroySemaphore);
BUSTER_GLOBAL_VULKAN_FUNCTION_POINTER(vkDestroyFence);
BUSTER_GLOBAL_VULKAN_FUNCTION_POINTER(vkUnmapMemory);
BUSTER_GLOBAL_VULKAN_FUNCTION_POINTER(vkCmdSetViewport);
BUSTER_GLOBAL_VULKAN_FUNCTION_POINTER(vkCmdSetScissor);
BUSTER_GLOBAL_VULKAN_FUNCTION_POINTER(vkCmdBeginRendering);
BUSTER_GLOBAL_VULKAN_FUNCTION_POINTER(vkCmdBindPipeline);
BUSTER_GLOBAL_VULKAN_FUNCTION_POINTER(vkCmdBindDescriptorSets);
BUSTER_GLOBAL_VULKAN_FUNCTION_POINTER(vkCmdBindIndexBuffer);
BUSTER_GLOBAL_VULKAN_FUNCTION_POINTER(vkCmdPushConstants);
BUSTER_GLOBAL_VULKAN_FUNCTION_POINTER(vkCmdDrawIndexed);
BUSTER_GLOBAL_VULKAN_FUNCTION_POINTER(vkCmdEndRendering);
BUSTER_GLOBAL_VULKAN_FUNCTION_POINTER(vkQueuePresentKHR);
BUSTER_GLOBAL_VULKAN_FUNCTION_POINTER(vkCmdCopyBufferToImage);
BUSTER_GLOBAL_VULKAN_FUNCTION_POINTER(vkUpdateDescriptorSets);

BUSTER_GLOBAL_LOCAL VkBool32 buster_vulkan_debug_callback(VkDebugUtilsMessageSeverityFlagBitsEXT message_severity, VkDebugUtilsMessageTypeFlagsEXT message_type, const VkDebugUtilsMessengerCallbackDataEXT* callback_data, void* user_data)
{
    BUSTER_UNUSED(user_data);

    String8 severity_string;

    switch (message_severity)
    {
            break; case VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT: severity_string = S8("VERBOSE");
            break; case VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT: severity_string = S8("INFORMATION");
            break; case VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT: severity_string = S8("WARNING");
            break; case VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT: severity_string = S8("ERROR");
            break; default: severity_string = S8("UNKNOWN");
    }

    bool is_general = (message_type & (VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT)) != 0;
    bool is_validation = (message_type & (VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT)) != 0;
    bool is_performance = (message_type & (VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT)) != 0;
    bool is_device_address_binding = (message_type & (VK_DEBUG_UTILS_MESSAGE_TYPE_DEVICE_ADDRESS_BINDING_BIT_EXT)) != 0;
    BUSTER_CHECK(is_general || is_validation || is_performance || is_device_address_binding);

    let message_id = string8_from_pointer((char8*)callback_data->pMessageIdName);
    if (!message_id.pointer)
    {
        message_id = S8("Message ID not specified");
    }

    let message = string8_from_pointer((char8*)callback_data->pMessage);
    if (!message.pointer)
    {
        message = S8("Message not specified");
    }

    string8_print(S8("[{S8}][{S8}] {S8}\n"), severity_string, message_id, message);

    bool success = message_severity < VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;

    if (!success)
    {
        os_fail();
    }

    return success ? VK_TRUE : VK_FALSE;
}

#define MAX_DESCRIPTOR_SET_LAYOUT_BINDING_COUNT (16)

STRUCT(DescriptorSetLayoutBindings)
{
    VkDescriptorSetLayoutBinding buffer[MAX_DESCRIPTOR_SET_LAYOUT_BINDING_COUNT];
    u32 count;
    u8 reserved[4];
};
#define MAX_DESCRIPTOR_SET_COUNT (16)
#define MAX_PUSH_CONSTANT_RANGE_COUNT (16)


STRUCT(Pipeline)
{
    VkPipeline handle;
    VkPipelineLayout layout;
    u32 descriptor_set_count;
    u32 push_constant_range_count;
    DescriptorSetLayoutBindings descriptor_set_layout_bindings[MAX_DESCRIPTOR_SET_COUNT];
    VkDescriptorSetLayout descriptor_set_layouts[MAX_DESCRIPTOR_SET_COUNT];
    VkPushConstantRange push_constant_ranges[MAX_PUSH_CONSTANT_RANGE_COUNT];
};

#define MAX_DESCRIPTOR_SET_LAYOUT_COUNT (16)

BUSTER_GLOBAL_LOCAL VkDescriptorType vulkan_descriptor_type(DescriptorType type)
{
    VkDescriptorType result;

    switch (type)
    {
        case DESCRIPTOR_TYPE_IMAGE_PLUS_SAMPLER:
            result = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            break;
        case DESCRIPTOR_TYPE_COUNT:
            unreachable();
        }

    return result;
}

BUSTER_GLOBAL_LOCAL VkShaderStageFlags vulkan_shader_stage(ShaderStage shader_stage)
{
    VkShaderStageFlags result;

    switch (shader_stage)
    {
    case SHADER_STAGE_VERTEX:
        result = VK_SHADER_STAGE_VERTEX_BIT;
        break;
    case SHADER_STAGE_FRAGMENT:
        result = VK_SHADER_STAGE_FRAGMENT_BIT;
        break;
    }

    return result;
}

BUSTER_GLOBAL_LOCAL VkShaderStageFlags vulkan_shader_stage_from_path(String8 shader_binary_path)
{
    VkShaderStageFlags shader_stage;
    if (string8_ends_with_sequence(shader_binary_path, S8(".vert.spv")))
    {
        shader_stage = VK_SHADER_STAGE_VERTEX_BIT;
    }
    else if (string8_ends_with_sequence(shader_binary_path, S8(".frag.spv")))
    {
        shader_stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    }
    else
    {
        os_fail();
    }

    return shader_stage;
}

#define MAX_SWAPCHAIN_IMAGE_COUNT (16)

BUSTER_GLOBAL_LOCAL void destroy_image(VkDevice device, const VkAllocationCallbacks* allocator, VkImageView image_view, VkImage image, VkDeviceMemory memory)
{
    vkDestroyImageView(device, image_view, allocator);
    vkDestroyImage(device, image, allocator);
    vkFreeMemory(device, memory, allocator);
}

STRUCT(GPUMemory)
{
    VkDeviceMemory handle;
    u64 size;
};

STRUCT(VulkanImage)
{
    VkImage handle;
    VkImageView view;
    GPUMemory memory;
    VkFormat format;
    u8 reserved[4];
};

STRUCT(VulkanImageCreate)
{
    u32 width;
    u32 height;
    u32 mip_levels;
    VkFormat format;
    VkImageUsageFlags usage;
};

BUSTER_GLOBAL_LOCAL GPUMemory vk_allocate_memory(VkDevice device, const VkAllocationCallbacks* allocation_callbacks, const VkPhysicalDeviceMemoryProperties* memory_properties, VkMemoryRequirements memory_requirements, VkMemoryPropertyFlags flags, u8 use_device_address_bit)
{
    u32 memory_type_index;
    let memory_type_count = memory_properties->memoryTypeCount;
    for (memory_type_index = 0; memory_type_index < memory_type_count; memory_type_index += 1)
    {
        let memory_type = memory_properties->memoryTypes[memory_type_index];

        if ((memory_requirements.memoryTypeBits & (1 << memory_type_index)) != 0 && (memory_type.propertyFlags & flags) == flags)
        {
            break;
        }
    }

    if (memory_type_index == memory_properties->memoryTypeCount)
    {
        os_fail();
    }

    VkMemoryAllocateInfo allocate_info = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .pNext = 0,
        .allocationSize = memory_requirements.size,
        .memoryTypeIndex = memory_type_index,
    };

    VkMemoryAllocateFlagsInfo flags_info = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO,
        .pNext = 0,
        .flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT,
        .deviceMask = 1,
    };

    if (use_device_address_bit)
    {
        allocate_info.pNext = &flags_info;
    }

    VkDeviceMemory memory = 0;
    {
        VkDeviceMemory memory_buffer;
        if (vkAllocateMemory(device, &allocate_info, allocation_callbacks, &memory_buffer) == VK_SUCCESS)
        {
            memory = memory_buffer;
        }
    }

    return (GPUMemory) { .handle = memory, .size = allocate_info.allocationSize };
}

BUSTER_GLOBAL_LOCAL VulkanImage vk_image_create(VkDevice device, const VkAllocationCallbacks* allocation_callbacks, const VkPhysicalDeviceMemoryProperties* memory_properties, VulkanImageCreate create)
{
    BUSTER_CHECK(create.width);
    BUSTER_CHECK(create.height);
    VulkanImage result = {};
    result.format = create.format;

    VkImageCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .pNext = 0,
        .flags = 0,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = create.format,
        .extent = {
            .width = create.width,
            .height = create.height,
            .depth = 1,
        },
        .mipLevels = create.mip_levels,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = create.usage,
        .sharingMode = 0,
        .queueFamilyIndexCount = 0,
        .pQueueFamilyIndices = 0,
        .initialLayout = 0,
    };

    if (vkCreateImage(device, &create_info, allocation_callbacks, &result.handle) == VK_SUCCESS)
    {
        VkMemoryRequirements memory_requirements;
        vkGetImageMemoryRequirements(device, result.handle, &memory_requirements);

        VkMemoryPropertyFlags flags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
        u8 use_device_address_bit = 0;
        result.memory = vk_allocate_memory(device, allocation_callbacks, memory_properties, memory_requirements, flags, use_device_address_bit);

        if (result.memory.handle)
        {
            VkDeviceSize memory_offset = 0;

            if (vkBindImageMemory(device, result.handle, result.memory.handle, memory_offset) == VK_SUCCESS)
            {
                VkImageViewCreateInfo view_create_info = {
                    .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                    .image = result.handle,
                    .viewType = VK_IMAGE_VIEW_TYPE_2D,
                    .format = create_info.format,
                    .subresourceRange = {
                        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                        .baseMipLevel = 0,
                        .levelCount = create.mip_levels,
                        .baseArrayLayer = 0,
                        .layerCount = 1,
                    },
                };

                VkImageView image_view;
                if (vkCreateImageView(device, &view_create_info, allocation_callbacks, &image_view) == VK_SUCCESS)
                {
                    result.view = image_view;
                }
            }
        }
    }

    return result;
}

ENUM_T(BufferType, u8, 
    BUFFER_TYPE_VERTEX,
    BUFFER_TYPE_INDEX,
    BUFFER_TYPE_STAGING,
);

STRUCT(VulkanBuffer)
{
    VkBuffer handle;
    GPUMemory memory;
    u64 address;
    VkDeviceSize size;
    BufferType type;
    u8 reserved[7];
};

STRUCT(VertexBuffer)
{
    VulkanBuffer gpu;
    Arena* cpu;
    u32 count;
    u8 reserved[4];
};

STRUCT(IndexBuffer)
{
    VulkanBuffer gpu;
    Arena* cpu;
};

STRUCT(FramePipelineInstantiation)
{
    VertexBuffer vertex_buffer;
    IndexBuffer index_buffer;
    VulkanBuffer transient_buffer;
};

STRUCT(WindowFrame)
{
    VkCommandPool command_pool;
    VkCommandBuffer command_buffer;
    VkSemaphore swapchain_semaphore;
    VkFence render_fence;
    VkBuffer index_buffer;
    GPUDrawPushConstants push_constants;
    FramePipelineInstantiation pipeline_instantiations[BUSTER_PIPELINE_COUNT];
    BusterPipeline bound_pipeline;
    u8 reserved[4];
};

#define MAX_TEXTURE_UPDATE_COUNT (32)

STRUCT(PipelineInstantiation)
{
    VkWriteDescriptorSet descriptor_set_update;
    VkDescriptorSet descriptor_sets[MAX_DESCRIPTOR_SET_COUNT];
    VkDescriptorImageInfo texture_descriptors[MAX_TEXTURE_UPDATE_COUNT];
};

BUSTER_GLOBAL_LOCAL DescriptorType descriptor_type_from_vulkan(VkDescriptorType descriptor_type)
{
    DescriptorType result;

    switch (descriptor_type)
    {
        case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
            result = DESCRIPTOR_TYPE_IMAGE_PLUS_SAMPLER;
            break;
        default: unreachable();
    }

    return result;
}

STRUCT(ImmediateContext)
{
    VkDevice device;
    VkFence fence;
    VkCommandBuffer command_buffer;
    VkCommandPool command_pool;
    VkQueue queue;
};

STRUCT(VulkanTexture)
{
    VulkanImage image;
    VkSampler sampler;
    VulkanBuffer transfer_buffer;
};

#define MAX_TEXTURE_COUNT (16)

STRUCT(RenderingHandle)
{
    OsModule* vulkan_library;
    VkDevice device;
    VkQueue graphics_queue;
    VkPhysicalDevice physical_device;
    VkInstance instance;
    VkDebugUtilsMessengerEXT messenger;
    u32 graphics_queue_family_index;
    u32 texture_count;
    FontTextureAtlas fonts[RENDER_FONT_TYPE_COUNT];
    const VkAllocationCallbacks* allocator;
    VkPhysicalDeviceMemoryProperties device_memory_properties;
    VkSampler sampler;
    ImmediateContext immediate;
    Pipeline pipelines[BUSTER_PIPELINE_COUNT];
    VulkanTexture textures[MAX_TEXTURE_COUNT];
};

STRUCT(RenderingWindowHandle)
{
    VkSwapchainKHR swapchain;
    VkSurfaceKHR surface;
    VkFormat swapchain_image_format;
    u32 width;
    u32 height;
    u32 last_width;
    u32 last_height;
    u32 swapchain_image_index;
    u32 swapchain_image_count;
    u32 frame_index;
    u32 frame_count;
    u8 reserved[4];
    VulkanImage render_image;
    VkImage swapchain_images[MAX_SWAPCHAIN_IMAGE_COUNT];
    VkImageView swapchain_image_views[MAX_SWAPCHAIN_IMAGE_COUNT];
    VkSemaphore render_semaphores[MAX_SWAPCHAIN_IMAGE_COUNT];
    WindowFrame frames[MAX_SWAPCHAIN_IMAGE_COUNT];
    PipelineInstantiation pipeline_instantiations[BUSTER_PIPELINE_COUNT];
    VkDescriptorPool descriptor_pool;
};

BUSTER_GLOBAL_LOCAL RenderingHandle rendering_handle = {};

BUSTER_GLOBAL_LOCAL void rendering_window_texture_update_begin(RenderingWindowHandle* window, BusterPipeline pipeline_index, u32 descriptor_count)
{
    let pipeline_instantiation = &window->pipeline_instantiations[pipeline_index];
    BUSTER_CHECK(descriptor_count <= BUSTER_ARRAY_LENGTH(pipeline_instantiation->texture_descriptors));
    BUSTER_CHECK(descriptor_count);
    BUSTER_CHECK(pipeline_instantiation->descriptor_sets[0]);

    pipeline_instantiation->descriptor_set_update = (VkWriteDescriptorSet) {
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .pNext = 0,
        .dstSet = pipeline_instantiation->descriptor_sets[0],
        .dstBinding = 0,
        .dstArrayElement = 0,
        .descriptorCount = descriptor_count,
        .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .pImageInfo = pipeline_instantiation->texture_descriptors,
        .pBufferInfo = 0,
        .pTexelBufferView = 0,
    };
}

BUSTER_IMPL void rendering_window_texture_update_end(RenderingHandle* rendering, RenderingWindowHandle* window, BusterPipeline pipeline_index)
{
    let pipeline_instantiation = &window->pipeline_instantiations[pipeline_index];
    u32 descriptor_copy_count = 0;
    VkCopyDescriptorSet* descriptor_copies = 0;
    VkWriteDescriptorSet descriptor_set_writes[] = {
        pipeline_instantiation->descriptor_set_update,
    };
    vkUpdateDescriptorSets(rendering->device, BUSTER_ARRAY_LENGTH(descriptor_set_writes), descriptor_set_writes, descriptor_copy_count, descriptor_copies);
}

BUSTER_IMPL void rendering_window_rect_texture_update_begin(RenderingWindowHandle* window)
{
    rendering_window_texture_update_begin(window, BUSTER_PIPELINE_RECT, RECT_TEXTURE_SLOT_COUNT);
}

BUSTER_IMPL void window_rect_texture_update_end(RenderingHandle* rendering, RenderingWindowHandle* window)
{
    rendering_window_texture_update_end(rendering, window, BUSTER_PIPELINE_RECT);
}

BUSTER_IMPL __attribute__((noinline)) RenderingHandle* rendering_initialize(Arena* arena)
{
    RenderingHandle* rendering = 0;
    VkAllocationCallbacks* allocator = 0;
#if defined(__linux__)
    rendering_handle.vulkan_library = os_dynamic_library_load(SOs("libvulkan.so.1"));
#elif defined(_WIN32)
    rendering_handle.vulkan_library = os_dynamic_library_load(SOs("vulkan-1.dll"));
#elif defined(__APPLE__)
    rendering_handle.vulkan_library = os_dynamic_library_load("libvulkan.dylib");

    if (!os_library_is_valid(rendering_handle.vulkan_library))
    {
        rendering_handle.vulkan_library = os_dynamic_library_load("libvulkan.1.dylib");
    }

    if (!os_library_is_valid(rendering_handle.vulkan_library))
    {
        rendering_handle.vulkan_library = os_dynamic_library_load("libMoltenVK.dylib");
    }

    if (!os_library_is_valid(rendering_handle.vulkan_library))
    {
        rendering_handle.vulkan_library = os_dynamic_library_load("vulkan.framework/vulkan");
    }

    if (!os_library_is_valid(rendering_handle.vulkan_library))
    {
        vulkan_library = os_dynamic_library_load("MoltenVK.framework/MoltenVK");
    }
#endif
    bool enable_validation = true;
    if (rendering_handle.vulkan_library)
    {
        BUSTER_VULKAN_OS_LOAD_FUNCTION(rendering_handle.vulkan_library, vkGetInstanceProcAddr);
        if (vkGetInstanceProcAddr)
        {
            BUSTER_VULKAN_LOAD_INSTANCE_FUNCTION(0, vkEnumerateInstanceVersion);
            BUSTER_VULKAN_LOAD_INSTANCE_FUNCTION(0, vkCreateInstance);

            VkResult result = VK_SUCCESS;
            if (vkEnumerateInstanceVersion)
            {
                u32 api_version = 0;
                result = vkEnumerateInstanceVersion(&api_version);
                if (result == VK_SUCCESS && api_version >= VK_API_VERSION_1_3)
                {
                    BUSTER_VULKAN_LOAD_INSTANCE_FUNCTION(0, vkEnumerateInstanceLayerProperties);

                    const char8* instance_layer_names[] = {
                        "VK_LAYER_KHRONOS_validation"
                    };
                    const char* const* enabled_layer_names = 0;
                    u64 supported_instance_layer_count = 0;
                    u32 enabled_layer_count = 0;

                    if (enable_validation)
                    {
                        enabled_layer_names = instance_layer_names;
                        bool layer_supported[BUSTER_ARRAY_LENGTH(instance_layer_names)] = {};
                        enabled_layer_count = BUSTER_ARRAY_LENGTH(instance_layer_names);

                        u32 layer_count = 0;
                        result = vkEnumerateInstanceLayerProperties(&layer_count, 0);
                        if (result == VK_SUCCESS)
                        {
                            VkLayerProperties instance_layer_properties[256];
                            if (layer_count <= BUSTER_ARRAY_LENGTH(instance_layer_properties))
                            {
                                result = vkEnumerateInstanceLayerProperties(&layer_count, instance_layer_properties);
                                if (result == VK_SUCCESS)
                                {
                                    for (u32 layer_i = 0; layer_i < layer_count && supported_instance_layer_count < BUSTER_ARRAY_LENGTH(instance_layer_names); layer_i += 1)
                                    {
                                        let layer_properties = &instance_layer_properties[layer_i];
                                        let layer_name = string8_from_pointer(layer_properties->layerName);

                                        for (u64 requested_i = 0; requested_i < BUSTER_ARRAY_LENGTH(instance_layer_names); requested_i += 1)
                                        {
                                            let is_supported_pointer = &layer_supported[requested_i];
                                            let requested_name = string8_from_pointer((char8*)instance_layer_names[requested_i]);

                                            if (string_equal(layer_name, requested_name))
                                            {
                                                let is_supported = *is_supported_pointer;
                                                *is_supported_pointer = 1;
                                                supported_instance_layer_count += !is_supported;

                                                break;
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }

                    const char* enabled_extension_names[] = {
                        VK_EXT_DEBUG_UTILS_EXTENSION_NAME,
                        VK_KHR_SURFACE_EXTENSION_NAME,
#ifdef VK_USE_PLATFORM_WIN32_KHR
                        VK_KHR_WIN32_SURFACE_EXTENSION_NAME,
#endif
#ifdef VK_USE_PLATFORM_XLIB_KHR
                        VK_KHR_XLIB_SURFACE_EXTENSION_NAME,
#endif 
#ifdef VK_USE_PLATFORM_XCB_KHR
                        VK_KHR_XCB_SURFACE_EXTENSION_NAME,
#endif
#ifdef VK_USE_PLATFORM_WAYLAND_KHR
                        VK_KHR_WAYLAND_SURFACE_EXTENSION_NAME,
#endif
#ifdef VK_USE_PLATFORM_METAL_EXT
                        VK_EXT_METAL_SURFACE_EXTENSION_NAME,
                        VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME,
#endif
                    };

                    VkValidationFeatureEnableEXT enabled_validation_features[] = {
                        VK_VALIDATION_FEATURE_ENABLE_DEBUG_PRINTF_EXT,
                    };

                    VkDebugUtilsMessengerCreateInfoEXT messenger_create_info = {
                        .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
                        .pNext = 0,
                        .messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
                        .messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
                        .pfnUserCallback = &buster_vulkan_debug_callback,
                        .pUserData = 0,
                    };

                    bool enable_shader_debug_printf = enable_validation;

                    VkValidationFeaturesEXT validation_features = { 
                        .sType = VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT,
                        .enabledValidationFeatureCount = BUSTER_ARRAY_LENGTH(enabled_validation_features),
                        .pEnabledValidationFeatures = enabled_validation_features,
                        .pNext = &messenger_create_info,
                    };

                    void* p_next = enable_shader_debug_printf ? (void*)&validation_features : (enable_validation ? &messenger_create_info : 0);

                    VkApplicationInfo application_info = {
                        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
                        .apiVersion = api_version,
                    };
                    VkInstanceCreateFlags instance_create_flags = 0;
#if defined(__APPLE__)
                    instance_create_flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
#endif
                    if (enabled_layer_count == supported_instance_layer_count)
                    {
                        VkInstanceCreateInfo instance_create_info = {
                            .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
                            .pNext = p_next,
                            .flags = instance_create_flags,
                            .pApplicationInfo = &application_info,
                            .ppEnabledLayerNames = enabled_layer_names,
                            .enabledLayerCount = enabled_layer_count,
                            .ppEnabledExtensionNames = enabled_extension_names,
                            .enabledExtensionCount = BUSTER_ARRAY_LENGTH(enabled_extension_names),
                        };

                        result = vkCreateInstance(&instance_create_info, allocator, &rendering_handle.instance);

                        if (result == VK_SUCCESS)
                        {
                            BUSTER_VULKAN_LOAD_INSTANCE_FUNCTION(rendering_handle.instance, vkCreateDebugUtilsMessengerEXT);
                            BUSTER_VULKAN_LOAD_INSTANCE_FUNCTION(rendering_handle.instance, vkDestroyDebugUtilsMessengerEXT);
                            result = vkCreateDebugUtilsMessengerEXT(rendering_handle.instance, &messenger_create_info, allocator, &rendering_handle.messenger);
                        }
                    }
                    else
                    {
                        result = VK_ERROR_LAYER_NOT_PRESENT;
                    }

                    if (result == VK_SUCCESS)
                    {
                        BUSTER_VULKAN_LOAD_INSTANCE_FUNCTION(rendering_handle.instance, vkDestroyInstance);
                        BUSTER_VULKAN_LOAD_INSTANCE_FUNCTION(rendering_handle.instance, vkGetDeviceProcAddr);
                        BUSTER_VULKAN_LOAD_INSTANCE_FUNCTION(rendering_handle.instance, vkEnumeratePhysicalDevices);
                        BUSTER_VULKAN_LOAD_INSTANCE_FUNCTION(rendering_handle.instance, vkGetPhysicalDeviceMemoryProperties);
                        BUSTER_VULKAN_LOAD_INSTANCE_FUNCTION(rendering_handle.instance, vkGetPhysicalDeviceProperties);
                        BUSTER_VULKAN_LOAD_INSTANCE_FUNCTION(rendering_handle.instance, vkGetPhysicalDeviceQueueFamilyProperties);
                        BUSTER_VULKAN_LOAD_INSTANCE_FUNCTION(rendering_handle.instance, vkCreateDevice);
                        BUSTER_VULKAN_LOAD_INSTANCE_FUNCTION(rendering_handle.instance, vkDestroyDevice);
                        BUSTER_VULKAN_LOAD_INSTANCE_FUNCTION(rendering_handle.instance, vkGetPhysicalDeviceSurfaceCapabilitiesKHR);
                        BUSTER_VULKAN_LOAD_INSTANCE_FUNCTION(rendering_handle.instance, vkGetPhysicalDeviceSurfacePresentModesKHR);
#ifdef VK_USE_PLATFORM_XCB_KHR
                        BUSTER_VULKAN_LOAD_INSTANCE_FUNCTION(rendering_handle.instance, vkCreateXcbSurfaceKHR);
#endif
#ifdef VK_USE_PLATFORM_WIN32_KHR
                        BUSTER_VULKAN_LOAD_INSTANCE_FUNCTION(rendering_handle.instance, vkCreateWin32SurfaceKHR);
#endif
#ifdef VK_USE_PLATFORM_METAL_EXT
                        BUSTER_VULKAN_LOAD_INSTANCE_FUNCTION(rendering_handle.instance, vkCreateMetalSurfaceEXT);
#endif
                        BUSTER_VULKAN_LOAD_INSTANCE_FUNCTION(rendering_handle.instance, vkDestroySurfaceKHR);

                        // TODO: make physical device choosing logic more robust
                        VkPhysicalDevice physical_devices[256];
                        u32 physical_device_count = 0;
                        result = vkEnumeratePhysicalDevices(rendering_handle.instance, &physical_device_count, 0);

                        if (result == VK_SUCCESS)
                        {
                            if (physical_device_count && physical_device_count <= BUSTER_ARRAY_LENGTH(physical_devices))
                            {
                                result = vkEnumeratePhysicalDevices(rendering_handle.instance, &physical_device_count, physical_devices);
                                rendering_handle.physical_device = physical_devices[0];
                            }
                        }
                    }

                    if (rendering_handle.physical_device)
                    {
                        vkGetPhysicalDeviceMemoryProperties(rendering_handle.physical_device, &rendering_handle.device_memory_properties);

                        {
                            u32 present_queue_family_index;
                            VkPhysicalDeviceProperties properties;
                            vkGetPhysicalDeviceProperties(rendering_handle.physical_device, &properties);

                            u32 queue_count;
                            vkGetPhysicalDeviceQueueFamilyProperties(rendering_handle.physical_device, &queue_count, 0);

                            VkQueueFamilyProperties queue_family_property_buffer[64];
                            if (queue_count <= BUSTER_ARRAY_LENGTH(queue_family_property_buffer))
                            {
                                vkGetPhysicalDeviceQueueFamilyProperties(rendering_handle.physical_device, &queue_count, queue_family_property_buffer);

                                for (rendering_handle.graphics_queue_family_index = 0; rendering_handle.graphics_queue_family_index < queue_count; rendering_handle.graphics_queue_family_index += 1)
                                {
                                    VkQueueFamilyProperties* queue_family_properties = &queue_family_property_buffer[rendering_handle.graphics_queue_family_index];
                                    if (queue_family_properties->queueFlags & VK_QUEUE_GRAPHICS_BIT)
                                    {
                                        break;
                                    }
                                }

                                if (rendering_handle.graphics_queue_family_index != queue_count)
                                {
                                    present_queue_family_index = 0;

                                    // for (present_queue_family_index = 0; present_queue_family_index < queue_count; present_queue_family_index += 1)
                                    // {
                                    //     VkBool32 support;
                                    //     VkResult success = vkGetPhysicalDeviceSurfaceSupportKHR(physical_device, present_queue_family_index, surface, &support);
                                    //     if (support)
                                    //     {
                                    //         break;
                                    //     }
                                    // }

                                    if (present_queue_family_index != queue_count && present_queue_family_index == rendering_handle.graphics_queue_family_index)
                                    {
                                        f32 queue_priorities[] = { 1.0f };
                                        VkDeviceQueueCreateInfo queue_create_infos[] = {
                                            {
                                                .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
                                                .queueFamilyIndex = rendering_handle.graphics_queue_family_index,
                                                .queueCount = BUSTER_ARRAY_LENGTH(queue_priorities),
                                                .pQueuePriorities = queue_priorities,
                                            },
                                        };

                                        const char* extensions[] =
                                        {
                                            VK_KHR_SWAPCHAIN_EXTENSION_NAME,
#ifdef __APPLE__
                                            "VK_KHR_portability_subset",
                                            VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME,
#endif
                                        };

#ifdef __APPLE__
                                        VkPhysicalDeviceDynamicRenderingFeatures dynamic_rendering_features = {
                                            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES,
                                            .pNext = 0,
                                            .dynamicRendering = VK_TRUE,
                                        };
#else
                                        VkPhysicalDeviceVulkan13Features features13 = {
                                            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
                                            .dynamicRendering = 1,
                                            .synchronization2 = 1,
                                        };
#endif

                                        VkPhysicalDeviceVulkan12Features features12 = {
                                            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
                                            .bufferDeviceAddress = 1,
                                            .descriptorIndexing = 1,
                                            .runtimeDescriptorArray = 1,
                                            .shaderSampledImageArrayNonUniformIndexing = 1,
#ifdef __APPLE__
                                            .pNext = &dynamic_rendering_features,
#else
                                            .pNext = &features13,
#endif
                                        };

                                        VkPhysicalDeviceFeatures2 features = {
                                            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
                                            .features = {
                                            },
                                            .pNext = &features12,
                                        };

                                        VkDeviceCreateInfo ci = {
                                            .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
                                            .ppEnabledExtensionNames = extensions,
                                            .enabledExtensionCount = BUSTER_ARRAY_LENGTH(extensions),
                                            .pQueueCreateInfos = queue_create_infos,
                                            .queueCreateInfoCount = BUSTER_ARRAY_LENGTH(queue_create_infos),
                                            .pNext = &features,
                                        };

                                        result = vkCreateDevice(rendering_handle.physical_device, &ci, allocator, &rendering_handle.device);
                                    }
                                }
                            }
                        }
                    }

                    if (rendering_handle.device)
                    {
                        BUSTER_VULKAN_LOAD_DEVICE_FUNCTION(rendering_handle.device, vkCreateSwapchainKHR);
                        BUSTER_VULKAN_LOAD_DEVICE_FUNCTION(rendering_handle.device, vkDestroySwapchainKHR);
                        BUSTER_VULKAN_LOAD_DEVICE_FUNCTION(rendering_handle.device, vkGetSwapchainImagesKHR);
                        BUSTER_VULKAN_LOAD_DEVICE_FUNCTION(rendering_handle.device, vkGetImageMemoryRequirements);
                        BUSTER_VULKAN_LOAD_DEVICE_FUNCTION(rendering_handle.device, vkGetBufferMemoryRequirements);
                        BUSTER_VULKAN_LOAD_DEVICE_FUNCTION(rendering_handle.device, vkMapMemory);
                        BUSTER_VULKAN_LOAD_DEVICE_FUNCTION(rendering_handle.device, vkUnmapMemory);
                        BUSTER_VULKAN_LOAD_DEVICE_FUNCTION(rendering_handle.device, vkAllocateMemory);
                        BUSTER_VULKAN_LOAD_DEVICE_FUNCTION(rendering_handle.device, vkBindImageMemory);
                        BUSTER_VULKAN_LOAD_DEVICE_FUNCTION(rendering_handle.device, vkBindBufferMemory);
                        BUSTER_VULKAN_LOAD_DEVICE_FUNCTION(rendering_handle.device, vkBindBufferMemory);
                        BUSTER_VULKAN_LOAD_DEVICE_FUNCTION(rendering_handle.device, vkGetDeviceQueue);
                        BUSTER_VULKAN_LOAD_DEVICE_FUNCTION(rendering_handle.device, vkCreateCommandPool);
                        BUSTER_VULKAN_LOAD_DEVICE_FUNCTION(rendering_handle.device, vkDestroyCommandPool);
                        BUSTER_VULKAN_LOAD_DEVICE_FUNCTION(rendering_handle.device, vkAllocateCommandBuffers);
                        BUSTER_VULKAN_LOAD_DEVICE_FUNCTION(rendering_handle.device, vkCreateFence);
                        BUSTER_VULKAN_LOAD_DEVICE_FUNCTION(rendering_handle.device, vkCreateSemaphore);
                        BUSTER_VULKAN_LOAD_DEVICE_FUNCTION(rendering_handle.device, vkDestroyFence);
                        BUSTER_VULKAN_LOAD_DEVICE_FUNCTION(rendering_handle.device, vkDestroySemaphore);
                        BUSTER_VULKAN_LOAD_DEVICE_FUNCTION(rendering_handle.device, vkCreateSampler);
                        BUSTER_VULKAN_LOAD_DEVICE_FUNCTION(rendering_handle.device, vkDestroySampler);
                        BUSTER_VULKAN_LOAD_DEVICE_FUNCTION(rendering_handle.device, vkCreateShaderModule);
                        BUSTER_VULKAN_LOAD_DEVICE_FUNCTION(rendering_handle.device, vkDestroyShaderModule);
                        BUSTER_VULKAN_LOAD_DEVICE_FUNCTION(rendering_handle.device, vkCreateDescriptorSetLayout);
                        BUSTER_VULKAN_LOAD_DEVICE_FUNCTION(rendering_handle.device, vkDestroyDescriptorSetLayout);
                        BUSTER_VULKAN_LOAD_DEVICE_FUNCTION(rendering_handle.device, vkCreatePipelineLayout);
                        BUSTER_VULKAN_LOAD_DEVICE_FUNCTION(rendering_handle.device, vkDestroyPipelineLayout);
                        BUSTER_VULKAN_LOAD_DEVICE_FUNCTION(rendering_handle.device, vkDestroyPipeline);
                        BUSTER_VULKAN_LOAD_DEVICE_FUNCTION(rendering_handle.device, vkCreateGraphicsPipelines);
                        BUSTER_VULKAN_LOAD_DEVICE_FUNCTION(rendering_handle.device, vkCreateImage);
                        BUSTER_VULKAN_LOAD_DEVICE_FUNCTION(rendering_handle.device, vkCreateImageView);
                        BUSTER_VULKAN_LOAD_DEVICE_FUNCTION(rendering_handle.device, vkCreateBuffer);
                        BUSTER_VULKAN_LOAD_DEVICE_FUNCTION(rendering_handle.device, vkDestroyBuffer);
                        BUSTER_VULKAN_LOAD_DEVICE_FUNCTION(rendering_handle.device, vkCreateDescriptorPool);
                        BUSTER_VULKAN_LOAD_DEVICE_FUNCTION(rendering_handle.device, vkDestroyDescriptorPool);
                        BUSTER_VULKAN_LOAD_DEVICE_FUNCTION(rendering_handle.device, vkAllocateDescriptorSets);
                        BUSTER_VULKAN_LOAD_DEVICE_FUNCTION(rendering_handle.device, vkResetFences);
                        BUSTER_VULKAN_LOAD_DEVICE_FUNCTION(rendering_handle.device, vkResetCommandBuffer);
                        BUSTER_VULKAN_LOAD_DEVICE_FUNCTION(rendering_handle.device, vkBeginCommandBuffer);
                        BUSTER_VULKAN_LOAD_DEVICE_FUNCTION(rendering_handle.device, vkCmdPipelineBarrier2);
                        BUSTER_VULKAN_LOAD_DEVICE_FUNCTION(rendering_handle.device, vkCmdCopyBufferToImage);
                        BUSTER_VULKAN_LOAD_DEVICE_FUNCTION(rendering_handle.device, vkEndCommandBuffer);
                        BUSTER_VULKAN_LOAD_DEVICE_FUNCTION(rendering_handle.device, vkQueueSubmit2);
                        BUSTER_VULKAN_LOAD_DEVICE_FUNCTION(rendering_handle.device, vkWaitForFences);
                        BUSTER_VULKAN_LOAD_DEVICE_FUNCTION(rendering_handle.device, vkUpdateDescriptorSets);
                        BUSTER_VULKAN_LOAD_DEVICE_FUNCTION(rendering_handle.device, vkAcquireNextImageKHR);
                        BUSTER_VULKAN_LOAD_DEVICE_FUNCTION(rendering_handle.device, vkGetBufferDeviceAddress);
                        BUSTER_VULKAN_LOAD_DEVICE_FUNCTION(rendering_handle.device, vkCmdCopyBuffer2);
                        BUSTER_VULKAN_LOAD_DEVICE_FUNCTION(rendering_handle.device, vkCmdSetViewport);
                        BUSTER_VULKAN_LOAD_DEVICE_FUNCTION(rendering_handle.device, vkCmdSetScissor);
                        BUSTER_VULKAN_LOAD_DEVICE_FUNCTION(rendering_handle.device, vkCmdBeginRendering);
                        BUSTER_VULKAN_LOAD_DEVICE_FUNCTION(rendering_handle.device, vkCmdEndRendering);
                        BUSTER_VULKAN_LOAD_DEVICE_FUNCTION(rendering_handle.device, vkCmdBindPipeline);
                        BUSTER_VULKAN_LOAD_DEVICE_FUNCTION(rendering_handle.device, vkCmdBindDescriptorSets);
                        BUSTER_VULKAN_LOAD_DEVICE_FUNCTION(rendering_handle.device, vkCmdBindIndexBuffer);
                        BUSTER_VULKAN_LOAD_DEVICE_FUNCTION(rendering_handle.device, vkCmdPushConstants);
                        BUSTER_VULKAN_LOAD_DEVICE_FUNCTION(rendering_handle.device, vkCmdDrawIndexed);
                        BUSTER_VULKAN_LOAD_DEVICE_FUNCTION(rendering_handle.device, vkCmdBlitImage2);
                        BUSTER_VULKAN_LOAD_DEVICE_FUNCTION(rendering_handle.device, vkQueuePresentKHR);
                        BUSTER_VULKAN_LOAD_DEVICE_FUNCTION(rendering_handle.device, vkDeviceWaitIdle);
                        BUSTER_VULKAN_LOAD_DEVICE_FUNCTION(rendering_handle.device, vkDestroyImageView);
                        BUSTER_VULKAN_LOAD_DEVICE_FUNCTION(rendering_handle.device, vkDestroyImage);
                        BUSTER_VULKAN_LOAD_DEVICE_FUNCTION(rendering_handle.device, vkFreeMemory);

                        vkGetDeviceQueue(rendering_handle.device, rendering_handle.graphics_queue_family_index, 0, &rendering_handle.graphics_queue);

                        rendering_handle.immediate.device = rendering_handle.device;
                        rendering_handle.immediate.queue = rendering_handle.graphics_queue;

                        VkCommandPoolCreateInfo create_info = {
                            .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
                            .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
                            .queueFamilyIndex = rendering_handle.graphics_queue_family_index,
                        };
                        result = vkCreateCommandPool(rendering_handle.device, &create_info, allocator, &rendering_handle.immediate.command_pool);

                        if (result == VK_SUCCESS)
                        {
                            VkCommandBufferAllocateInfo command_buffer_allocate_info = {
                                .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
                                .commandPool = rendering_handle.immediate.command_pool,
                                .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
                                .commandBufferCount = 1,
                            };

                            result = vkAllocateCommandBuffers(rendering_handle.device, &command_buffer_allocate_info, &rendering_handle.immediate.command_buffer);

                            if (result == VK_SUCCESS)
                            {
                                VkFenceCreateInfo fence_create_info = {
                                    .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
                                    .flags = VK_FENCE_CREATE_SIGNALED_BIT,
                                };

                                result = vkCreateFence(rendering_handle.device, &fence_create_info, allocator, &rendering_handle.immediate.fence);
                            }
                        }

                        if (result == VK_SUCCESS)
                        {
                            VkFilter sampler_filter = VK_FILTER_LINEAR;
                            VkSamplerCreateInfo sampler_create_info = {
                                .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
                                .pNext = 0,
                                .flags = 0,
                                .magFilter = sampler_filter,
                                .minFilter = sampler_filter,
                                .mipmapMode = 0,
                                .addressModeU = 0,
                                .addressModeV = 0,
                                .addressModeW = 0,
                                .mipLodBias = 0,
                                .anisotropyEnable = 0,
                                .maxAnisotropy = 0,
                                .compareEnable = 0,
                                .compareOp = 0,
                                .minLod = 0,
                                .maxLod = 0,
                                .borderColor = 0,
                                .unnormalizedCoordinates = 0,
                            };

                            result = vkCreateSampler(rendering_handle.device, &sampler_create_info, allocator, &rendering_handle.sampler);
                        }
                    }

                    String8 shader_binaries[] = {
                        S8("build" "/" "rect.vert.spv"),
                        S8("build" "/" "rect.frag.spv"),
                    };

                    PipelineLayoutCreate pipeline_layouts[] = {
                        (PipelineLayoutCreate) {
                            .push_constant_ranges = BUSTER_ARRAY_TO_SLICE(((PushConstantRange[]){
                                        (PushConstantRange) {
                                        .offset = 0,
                                        .size = sizeof(GPUDrawPushConstants),
                                        .stage = SHADER_STAGE_VERTEX,
                                        },
                                        })),
                            .descriptor_set_layouts = BUSTER_ARRAY_TO_SLICE(((DescriptorSetLayoutCreate[]){
                                        (DescriptorSetLayoutCreate) {
                                        .bindings = BUSTER_ARRAY_TO_SLICE(((DescriptorSetLayoutBinding[]) {
                                                    {
                                                    .binding = 0,
                                                    .type = DESCRIPTOR_TYPE_IMAGE_PLUS_SAMPLER,
                                                    .stage = SHADER_STAGE_FRAGMENT,
                                                    .count = RECT_TEXTURE_SLOT_COUNT,
                                                    },
                                                    })),
                                        },
                                        })),
                        },
                    };
                    PipelineCreate pipeline_create[] = {
                        (PipelineCreate) {
                            .shader_source_indices = BUSTER_ARRAY_TO_SLICE(((u16[]){0, 1})),
                            .layout_index = 0,
                        },
                    };
                    GraphicsPipelinesCreate create_data = {
                        .layouts = BUSTER_ARRAY_TO_SLICE(pipeline_layouts),
                        .pipelines = BUSTER_ARRAY_TO_SLICE(pipeline_create),
                        .shader_binaries = BUSTER_ARRAY_TO_SLICE(shader_binaries),
                    };
                    let graphics_pipeline_count = create_data.pipelines.length;
                    BUSTER_CHECK(graphics_pipeline_count);
                    let pipeline_layout_count = create_data.layouts.length;
                    BUSTER_CHECK(pipeline_layout_count);
                    BUSTER_CHECK(pipeline_layout_count <= graphics_pipeline_count);
                    let shader_count = create_data.shader_binaries.length;

                    VkPipeline pipeline_handles[BUSTER_PIPELINE_COUNT];
                    VkPipelineShaderStageCreateInfo shader_create_infos[MAX_SHADER_MODULE_COUNT_PER_PIPELINE];
                    VkGraphicsPipelineCreateInfo graphics_pipeline_create_infos[BUSTER_PIPELINE_COUNT];

                    VkPipelineVertexInputStateCreateInfo vertex_input_state_create_info = {
                        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
                        .pNext = 0,
                        .flags = 0,
                        .vertexBindingDescriptionCount = 0,
                        .pVertexBindingDescriptions = 0,
                        .vertexAttributeDescriptionCount = 0,
                        .pVertexAttributeDescriptions = 0,
                    };

                    VkPipelineInputAssemblyStateCreateInfo input_assembly_state_create_info = {
                        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
                        .pNext = 0,
                        .flags = 0,
                        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
                        .primitiveRestartEnable = VK_FALSE,
                    };

                    VkPipelineViewportStateCreateInfo viewport_state_create_info = {
                        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
                        .pNext = 0,
                        .viewportCount = 1,
                        .scissorCount = 1,
                    };

                    VkPipelineRasterizationStateCreateInfo rasterization_state_create_info = {
                        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
                        .pNext = 0,
                        .flags = 0,
                        .depthClampEnable = 0,
                        .rasterizerDiscardEnable = 0,
                        .polygonMode = VK_POLYGON_MODE_FILL,
                        .cullMode = VK_CULL_MODE_NONE,
                        .frontFace = VK_FRONT_FACE_CLOCKWISE,
                        .depthBiasEnable = 0,
                        .depthBiasConstantFactor = 0,
                        .depthBiasClamp = 0,
                        .depthBiasSlopeFactor = 0,
                        .lineWidth = 1.0f,
                    };

                    VkPipelineMultisampleStateCreateInfo multisample_state_create_info = {
                        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
                        .pNext = 0,
                        .flags = 0,
                        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
                        .sampleShadingEnable = 0,
                        .minSampleShading = 1.0f,
                        .pSampleMask = 0,
                        .alphaToCoverageEnable = 0,
                        .alphaToOneEnable = 0,
                    };

                    VkPipelineDepthStencilStateCreateInfo depth_stencil_state_create_info = {
                        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
                        .pNext = 0,
                        .flags = 0,
                        .depthTestEnable = 0,
                        .depthWriteEnable = 0,
                        .depthCompareOp = VK_COMPARE_OP_NEVER,
                        .depthBoundsTestEnable = 0,
                        .stencilTestEnable = 0,
                        .front = {},
                        .back = {},
                        .minDepthBounds = 0.0f,
                        .maxDepthBounds = 1.0f,
                    };

                    VkPipelineColorBlendAttachmentState attachments[] = {
                        {
                            .blendEnable = 1,
                            .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
                            .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
                            .colorBlendOp = VK_BLEND_OP_ADD,
                            .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
                            .dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
                            .alphaBlendOp = VK_BLEND_OP_ADD,
                            .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
                        },
                    };

                    VkPipelineColorBlendStateCreateInfo color_blend_state_create_info = {
                        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
                        .pNext = 0,
                        .flags = 0,
                        .logicOpEnable = VK_FALSE,
                        .logicOp = VK_LOGIC_OP_COPY,
                        .attachmentCount = BUSTER_ARRAY_LENGTH(attachments),
                        .pAttachments = attachments,
                        .blendConstants = {},
                    };

                    VkDynamicState states[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };

                    VkPipelineDynamicStateCreateInfo dynamic_state_create_info = {
                        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
                        .pNext = 0,
                        .flags = 0,
                        .dynamicStateCount = BUSTER_ARRAY_LENGTH(states),
                        .pDynamicStates = states,
                    };

                    // TODO: abstract away
                    VkFormat common_image_format = VK_FORMAT_B8G8R8A8_UNORM;
                    VkFormat color_attachment_formats[] = {
                        common_image_format,
                    };

                    VkPipelineRenderingCreateInfo rendering_create_info = {
                        .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
                        .pNext = 0,
                        .viewMask = 0,
                        .colorAttachmentCount = BUSTER_ARRAY_LENGTH(color_attachment_formats),
                        .pColorAttachmentFormats = color_attachment_formats,
                        .depthAttachmentFormat = 0,
                        .stencilAttachmentFormat = 0,
                    };

                    let shader_modules = arena_allocate(arena, VkShaderModule, shader_count);

                    for (u64 i = 0; i < shader_count; i += 1)
                    {
                        let shader_binary_path = create_data.shader_binaries.pointer[i];

                        let binary = file_read(arena, shader_binary_path, (FileReadOptions){});

                        VkShaderModuleCreateInfo create_info = {
                            .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
                            .codeSize = binary.length,
                            .pCode = (u32*)binary.pointer,
                        };

                        result = vkCreateShaderModule(rendering_handle.device, &create_info, allocator, &shader_modules[i]);
                    }

                    for (u64 pipeline_index = 0; pipeline_index < pipeline_layout_count; pipeline_index += 1)
                    {
                        let create = create_data.layouts.pointer[pipeline_index];
                        let descriptor_set_layout_count = create.descriptor_set_layouts.length;
                        let push_constant_range_count = create.push_constant_ranges.length;
                        let pipeline = &rendering_handle.pipelines[pipeline_index];
                        pipeline->descriptor_set_count = (u32)descriptor_set_layout_count;
                        BUSTER_CHECK(pipeline->descriptor_set_count);
                        pipeline->push_constant_range_count = (u32)push_constant_range_count;

                        if (descriptor_set_layout_count > MAX_DESCRIPTOR_SET_LAYOUT_COUNT)
                        {
                            os_fail();
                        }

                        // u16 descriptor_type_counter[DESCRIPTOR_TYPE_COUNT] = {};

                        for (u64 descriptor_set_layout_index = 0; descriptor_set_layout_index < descriptor_set_layout_count; descriptor_set_layout_index += 1)
                        {
                            let set_layout_create = create.descriptor_set_layouts.pointer[descriptor_set_layout_index];
                            let binding_count = set_layout_create.bindings.length;
                            let descriptor_set_layout_bindings = &pipeline->descriptor_set_layout_bindings[descriptor_set_layout_index];
                            descriptor_set_layout_bindings->count = (u32)binding_count;

                            for (u64 binding_index = 0; binding_index < binding_count; binding_index += 1)
                            {
                                let binding_descriptor = set_layout_create.bindings.pointer[binding_index];

                                VkDescriptorType descriptor_type = vulkan_descriptor_type(binding_descriptor.type);

                                VkShaderStageFlags shader_stage = vulkan_shader_stage(binding_descriptor.stage);

                                descriptor_set_layout_bindings->buffer[binding_index] = (VkDescriptorSetLayoutBinding) {
                                    .binding = binding_descriptor.binding,
                                        .descriptorType = descriptor_type,
                                        .descriptorCount = binding_descriptor.count,
                                        .stageFlags = shader_stage,
                                        .pImmutableSamplers = 0,
                                };
                            }

                            VkDescriptorSetLayoutCreateInfo create_info = {
                                .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
                                .pNext = 0,
                                .flags = 0,
                                .bindingCount = (u32)binding_count,
                                .pBindings = descriptor_set_layout_bindings->buffer,
                            };

                            result = vkCreateDescriptorSetLayout(rendering_handle.device, &create_info, allocator, &pipeline->descriptor_set_layouts[descriptor_set_layout_index]);
                        }

                        if (push_constant_range_count > MAX_PUSH_CONSTANT_RANGE_COUNT)
                        {
                            os_fail();
                        }

                        for (u64 push_constant_index = 0; push_constant_index < push_constant_range_count; push_constant_index += 1)
                        {
                            let push_constant_descriptor = create.push_constant_ranges.pointer[push_constant_index];
                            pipeline->push_constant_ranges[push_constant_index] = (VkPushConstantRange) {
                                .stageFlags = vulkan_shader_stage(push_constant_descriptor.stage),
                                    .offset = push_constant_descriptor.offset,
                                    .size = push_constant_descriptor.size,
                            };
                        }

                        VkPipelineLayoutCreateInfo create_info = {
                            .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
                            .pNext = 0,
                            .flags = 0,
                            .setLayoutCount = (u32)descriptor_set_layout_count,
                            .pSetLayouts = pipeline->descriptor_set_layouts,
                            .pushConstantRangeCount = (u32)push_constant_range_count,
                            .pPushConstantRanges = pipeline->push_constant_ranges,
                        };

                        result = vkCreatePipelineLayout(rendering_handle.device, &create_info, allocator, &pipeline->layout);
                    }

                    for (u64 pipeline_i = 0; pipeline_i < graphics_pipeline_count; pipeline_i += 1)
                    {
                        let create = create_data.pipelines.pointer[pipeline_i];
                        let pipeline = &rendering_handle.pipelines[pipeline_i];
                        let pipeline_shader_count = create.shader_source_indices.length;
                        if (pipeline_shader_count > MAX_SHADER_MODULE_COUNT_PER_PIPELINE)
                        {
                            os_fail();
                        }

                        for (u64 shader_i = 0; shader_i < pipeline_shader_count; shader_i += 1)
                        {
                            let shader_index = create.shader_source_indices.pointer[shader_i];
                            let shader_source_path = create_data.shader_binaries.pointer[shader_index];

                            shader_create_infos[shader_i] = (VkPipelineShaderStageCreateInfo) {
                                .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                                    .pNext = 0,
                                    .flags = 0,
                                    .stage = vulkan_shader_stage_from_path(shader_source_path),
                                    .module = shader_modules[shader_i],
                                    .pName = "main",
                                    .pSpecializationInfo = 0,
                            };
                        }

                        graphics_pipeline_create_infos[pipeline_i] = (VkGraphicsPipelineCreateInfo)
                        {
                            .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
                                .pNext = &rendering_create_info,
                                .flags = 0,
                                .stageCount = (u32)shader_count,
                                .pStages = shader_create_infos,
                                .pVertexInputState = &vertex_input_state_create_info,
                                .pInputAssemblyState = &input_assembly_state_create_info,
                                .pTessellationState = 0,
                                .pViewportState = &viewport_state_create_info,
                                .pRasterizationState = &rasterization_state_create_info,
                                .pMultisampleState = &multisample_state_create_info,
                                .pDepthStencilState = &depth_stencil_state_create_info,
                                .pColorBlendState = &color_blend_state_create_info,
                                .pDynamicState = &dynamic_state_create_info,
                                .layout = pipeline->layout,
                                .renderPass = 0,
                                .subpass = 0,
                                .basePipelineHandle = 0,
                                .basePipelineIndex = 0,
                        };
                    }

                    VkPipelineCache pipeline_cache = 0;
                    result = vkCreateGraphicsPipelines(rendering_handle.device, pipeline_cache, (u32)graphics_pipeline_count, graphics_pipeline_create_infos, allocator, pipeline_handles);

                    if (result == VK_SUCCESS)
                    {
                        rendering = &rendering_handle;

                        for (u32 i = 0; i < graphics_pipeline_count; i += 1)
                        {
                            rendering_handle.pipelines[i].handle = pipeline_handles[i];
                        }

                        for (u32 i = 0; i < shader_count; i += 1)
                        {
                            vkDestroyShaderModule(rendering->device, shader_modules[i], rendering->allocator);
                        }
                    }
                }
            }
        }
    }

    return rendering;
}

BUSTER_GLOBAL_LOCAL void swapchain_recreate(RenderingHandle* rendering, RenderingWindowHandle* window)
{
    VkSurfaceCapabilitiesKHR surface_capabilities;

    if (vkGetPhysicalDeviceSurfaceCapabilitiesKHR(rendering->physical_device, window->surface, &surface_capabilities) == VK_SUCCESS)
    {
        VkSwapchainKHR old_swapchain = window->swapchain;
        VkImageView old_swapchain_image_views[MAX_SWAPCHAIN_IMAGE_COUNT];

        if (old_swapchain)
        {
            if (vkDeviceWaitIdle(rendering->device) == VK_SUCCESS)
            {
                for (u32 i = 0; i < window->swapchain_image_count; i += 1)
                {
                    old_swapchain_image_views[i] = window->swapchain_image_views[i];
                }
            }
        }

        u32 queue_family_indices[] = { rendering->graphics_queue_family_index };
        VkImageUsageFlags swapchain_image_usage_flags = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        window->swapchain_image_format = VK_FORMAT_B8G8R8A8_UNORM;
        window->last_width = window->width;
        window->last_height = window->height;
        window->width = surface_capabilities.currentExtent.width;
        window->height = surface_capabilities.currentExtent.height;

        VkPresentModeKHR preferred_present_mode = VK_PRESENT_MODE_IMMEDIATE_KHR;
        VkPresentModeKHR present_modes[16];
        u32 present_mode_count = BUSTER_ARRAY_LENGTH(present_modes);

        if (vkGetPhysicalDeviceSurfacePresentModesKHR(rendering->physical_device, window->surface, &present_mode_count, present_modes) == VK_SUCCESS)
        {
            for (u32 i = 0; i < present_mode_count; i += 1)
            {
                if (present_modes[i] == VK_PRESENT_MODE_MAILBOX_KHR)
                {
                    preferred_present_mode = VK_PRESENT_MODE_MAILBOX_KHR;
                    break;
                }
            }

            VkSwapchainCreateInfoKHR swapchain_create_info = {
                .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
                .pNext = 0,
                .flags = 0,
                .surface = window->surface,
                .minImageCount = surface_capabilities.minImageCount,
                .imageFormat = window->swapchain_image_format,
                .imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR,
                .imageExtent = surface_capabilities.currentExtent,
                .imageArrayLayers = 1,
                .imageUsage = swapchain_image_usage_flags,
                .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
                .queueFamilyIndexCount = BUSTER_ARRAY_LENGTH(queue_family_indices),
                .pQueueFamilyIndices = queue_family_indices,
                .preTransform = surface_capabilities.currentTransform,
                .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
                .presentMode = preferred_present_mode,
                .clipped = 0,
                .oldSwapchain = window->swapchain,
            };

            if (vkCreateSwapchainKHR(rendering->device, &swapchain_create_info, rendering->allocator, &window->swapchain) == VK_SUCCESS)
            {
                BUSTER_CHECK(window->swapchain != old_swapchain);

                if (old_swapchain)
                {
                    for (u32 i = 0; i < window->swapchain_image_count; i += 1)
                    {
                        vkDestroyImageView(rendering->device, old_swapchain_image_views[i], rendering->allocator);
                    }

                    vkDestroySwapchainKHR(rendering->device, old_swapchain, rendering->allocator);

                    destroy_image(rendering->device, rendering->allocator, window->render_image.view, window->render_image.handle, window->render_image.memory.handle);
                }

                if (vkGetSwapchainImagesKHR(rendering->device, window->swapchain, &window->swapchain_image_count, 0) == VK_SUCCESS)
                {
                    if (window->swapchain_image_count == 0)
                    {
                        os_fail();
                    }

                    if (window->swapchain_image_count > BUSTER_ARRAY_LENGTH(window->swapchain_images))
                    {
                        os_fail();
                    }

                    if (vkGetSwapchainImagesKHR(rendering->device, window->swapchain, &window->swapchain_image_count, window->swapchain_images) == VK_SUCCESS)
                    {
                        // VkImageViewUsageCreateInfo image_view_usage_create_info = {
                        //     .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_USAGE_CREATE_INFO,
                        //     .pNext = 0,
                        //     .usage = swapchain_create_info.imageUsage,
                        // };

                        for (u32 i = 0; i < window->swapchain_image_count; i += 1)
                        {
                            VkImageViewCreateInfo create_info = {
                                .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                                // .pNext = &image_view_usage_create_info,
                                .flags = 0,
                                .image = window->swapchain_images[i],
                                .viewType = VK_IMAGE_VIEW_TYPE_2D,
                                .format = swapchain_create_info.imageFormat,
                                .components = {
                                    .r = VK_COMPONENT_SWIZZLE_IDENTITY,
                                    .g = VK_COMPONENT_SWIZZLE_IDENTITY,
                                    .b = VK_COMPONENT_SWIZZLE_IDENTITY,
                                    .a = VK_COMPONENT_SWIZZLE_IDENTITY,
                                },
                                .subresourceRange = {
                                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                                    .baseMipLevel = 0,
                                    .levelCount = 1,
                                    .baseArrayLayer = 0,
                                    .layerCount = 1,
                                },
                            };

                            let image_view_creation = vkCreateImageView(rendering->device, &create_info, rendering->allocator, &window->swapchain_image_views[i]);
                            if (image_view_creation != VK_SUCCESS)
                            {
                                os_fail();
                            }
                        }
                    }
                }

                window->render_image = vk_image_create(rendering->device, rendering->allocator, &rendering->device_memory_properties, (VulkanImageCreate) {
                    .width = surface_capabilities.currentExtent.width,
                    .height = surface_capabilities.currentExtent.height,
                    .mip_levels = 1,
                    .format = window->swapchain_image_format,
                    .usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
                });
            }
        }
    }
}

BUSTER_IMPL RenderingWindowHandle* rendering_window_initialize(Arena* arena, OsWindowingHandle* windowing, RenderingHandle* rendering, OsWindowHandle* window)
{
    BUSTER_UNUSED(rendering);
    BUSTER_UNUSED(window);

    let result = arena_allocate(arena, RenderingWindowHandle, 1);
#if defined(VK_USE_PLATFORM_XCB_KHR)
    VkXcbSurfaceCreateInfoKHR surface_create_info = {
        .sType = VK_STRUCTURE_TYPE_XCB_SURFACE_CREATE_INFO_KHR,
        .pNext = 0,
        .flags = 0,
        .connection = (xcb_connection_t*)native_windowing_handle_from_os_windowing_handle(windowing),
        .window = (xcb_window_t)(u64)native_window_handle_from_os_window_handle(window),
    };
    if (vkCreateXcbSurfaceKHR(rendering->instance, &surface_create_info, rendering->allocator, &result->surface) != VK_SUCCESS)
    {
        os_fail();
    }
#endif

    result->frame_index = 0;
    result->frame_count = 2;

    swapchain_recreate(rendering, result);

    for (u64 frame_index = 0; frame_index < result->frame_count; frame_index += 1)
    {
        let frame = &result->frames[frame_index];

        for (u64 pipeline_index = 0; pipeline_index < BUSTER_PIPELINE_COUNT; pipeline_index += 1)
        {
            let pipeline = &frame->pipeline_instantiations[pipeline_index];
            pipeline->vertex_buffer.cpu = arena_create((ArenaCreation){});
            pipeline->index_buffer.cpu = arena_create((ArenaCreation){});
            pipeline->vertex_buffer.gpu.type = BUFFER_TYPE_VERTEX;
            pipeline->index_buffer.gpu.type = BUFFER_TYPE_INDEX;
            pipeline->transient_buffer.type = BUFFER_TYPE_STAGING;
        }
    }

    for (u64 pipeline_index = 0; pipeline_index < BUSTER_PIPELINE_COUNT; pipeline_index += 1)
    {
        let pipeline_descriptor = &rendering->pipelines[pipeline_index];
        BUSTER_CHECK(pipeline_descriptor->descriptor_set_count);
        let pipeline_instantiation = &result->pipeline_instantiations[pipeline_index];

        u16 descriptor_type_counter[DESCRIPTOR_TYPE_COUNT] = {};

        for (u64 descriptor_index = 0; descriptor_index < pipeline_descriptor->descriptor_set_count; descriptor_index += 1)
        {
            let descriptor_set_layout_bindings = &pipeline_descriptor->descriptor_set_layout_bindings[descriptor_index];

            for (u64 binding_index = 0; binding_index < descriptor_set_layout_bindings->count; binding_index += 1)
            {
                let binding_descriptor = &descriptor_set_layout_bindings->buffer[binding_index];
                let descriptor_type = descriptor_type_from_vulkan(binding_descriptor->descriptorType);
                let counter_ptr = &descriptor_type_counter[descriptor_type];
                let old_counter = *counter_ptr;
                *counter_ptr = old_counter + (u16)binding_descriptor->descriptorCount;
            }
        }

        VkDescriptorPoolSize pool_sizes[DESCRIPTOR_TYPE_COUNT];
        u32 pool_size_count = 0;

        for (DescriptorType i = 0; i < DESCRIPTOR_TYPE_COUNT; i += 1)
        {
            let count = descriptor_type_counter[i];
            if (count != 0)
            {
                let pool_size = &pool_sizes[pool_size_count];
                pool_size_count += 1;

                *pool_size = (VkDescriptorPoolSize) {
                    .type = vulkan_descriptor_type(i),
                        .descriptorCount = count,
                };
            }
        }

        VkDescriptorPoolCreateInfo create_info = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
            .pNext = 0,
            .flags = 0,
            .maxSets = pipeline_descriptor->descriptor_set_count,
            .poolSizeCount = pool_size_count,
            .pPoolSizes = pool_sizes,
        };

        if (vkCreateDescriptorPool(rendering->device, &create_info, rendering->allocator, &result->descriptor_pool) == VK_SUCCESS)
        {
            VkDescriptorSetAllocateInfo allocate_info = {
                .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
                .pNext = 0,
                .descriptorPool = result->descriptor_pool,
                .descriptorSetCount = pipeline_descriptor->descriptor_set_count,
                .pSetLayouts = pipeline_descriptor->descriptor_set_layouts,
            };

            let descriptor_set_allocation = vkAllocateDescriptorSets(rendering->device, &allocate_info, pipeline_instantiation->descriptor_sets);
            if (descriptor_set_allocation != VK_SUCCESS)
            {
                os_fail();
            }
            BUSTER_CHECK(pipeline_instantiation->descriptor_sets[0]);
        }
    }

    VkSemaphoreCreateInfo semaphore_create_info = {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
        .flags = 0,
    };

    for (u32 frame_i = 0; frame_i < result->frame_count; frame_i += 1)
    {
        VkCommandPoolCreateInfo command_pool_create_info = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
            .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
            .queueFamilyIndex = rendering->graphics_queue_family_index,
        };

        VkFenceCreateInfo fence_create_info = {
            .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
            .flags = VK_FENCE_CREATE_SIGNALED_BIT,
        };

        WindowFrame* frame = &result->frames[frame_i];
        if (vkCreateCommandPool(rendering->device, &command_pool_create_info, rendering->allocator, &frame->command_pool) == VK_SUCCESS)
        {
            VkCommandBufferAllocateInfo command_buffer_allocate_info = {
                .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
                .commandPool = frame->command_pool,
                .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
                .commandBufferCount = 1,
            };

            if (vkAllocateCommandBuffers(rendering->device, &command_buffer_allocate_info, &frame->command_buffer) != VK_SUCCESS)
            {
                os_fail();
            }

            if (vkCreateFence(rendering->device, &fence_create_info, rendering->allocator, &frame->render_fence) != VK_SUCCESS)
            {
                os_fail();
            }

            if (vkCreateSemaphore(rendering->device, &semaphore_create_info, rendering->allocator, &frame->swapchain_semaphore) != VK_SUCCESS)
            {
                os_fail();
            }
            frame->bound_pipeline = BUSTER_PIPELINE_COUNT;
        }
    }

    for (u32 image_i = 0; image_i < result->swapchain_image_count; image_i += 1)
    {
        if (vkCreateSemaphore(rendering->device, &semaphore_create_info, rendering->allocator, &result->render_semaphores[image_i]) != VK_SUCCESS)
        {
            os_fail();
        }
    }

    return result;
}

BUSTER_IMPL void rendering_window_queue_pipeline_texture_update(RenderingHandle* rendering, RenderingWindowHandle* window, BusterPipeline pipeline_index, u32 resource_slot, TextureIndex texture_index)
{
    let pipeline_instantiation = &window->pipeline_instantiations[pipeline_index];
    VkDescriptorImageInfo* descriptor_image = &pipeline_instantiation->texture_descriptors[resource_slot];
    VulkanTexture* texture = &rendering->textures[texture_index.value];
    *descriptor_image = (VkDescriptorImageInfo) {
        .sampler = texture->sampler,
        .imageView = texture->image.view,
        .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, // TODO: specify
    };
}

BUSTER_IMPL void rendering_window_queue_rect_texture_update(RenderingHandle* rendering, RenderingWindowHandle* window, RectTextureSlot slot, TextureIndex texture_index)
{
    rendering_window_queue_pipeline_texture_update(rendering, window, BUSTER_PIPELINE_RECT, slot, texture_index);
}

BUSTER_IMPL void rendering_queue_font_update(RenderingHandle* rendering, RenderingWindowHandle* window, RenderFontType type, FontTextureAtlas atlas)
{
    // static_assert(RECT_TEXTURE_SLOT_MONOSPACE_FONT < RECT_TEXTURE_SLOT_PROPORTIONAL_FONT);
    let slot = RECT_TEXTURE_SLOT_MONOSPACE_FONT + (RectTextureSlot)type;
    rendering_window_queue_rect_texture_update(rendering, window, slot, atlas.texture);
    rendering->fonts[type] = atlas;
}

BUSTER_IMPL void rendering_window_rect_texture_update_end(RenderingHandle* rendering, RenderingWindowHandle* window)
{
    rendering_window_texture_update_end(rendering, window, BUSTER_PIPELINE_RECT);
}

BUSTER_GLOBAL_LOCAL VkFormat vk_texture_format(TextureFormat format)
{
    VkFormat result;
    switch (format)
    {
    case TEXTURE_FORMAT_R8_UNORM:
        result = VK_FORMAT_R8_UNORM;
        break;
    case TEXTURE_FORMAT_R8G8B8A8_SRGB:
        result = VK_FORMAT_R8G8B8A8_SRGB;
        break;
    }

    return result;
}

BUSTER_GLOBAL_LOCAL u32 format_channel_count(TextureFormat format)
{
    switch (format)
    {
    case TEXTURE_FORMAT_R8_UNORM:
        return 1;
    case TEXTURE_FORMAT_R8G8B8A8_SRGB:
        return 4;
    }

    BUSTER_UNREACHABLE();
}

BUSTER_GLOBAL_LOCAL VulkanBuffer vk_buffer_create(VkDevice device, const VkAllocationCallbacks* allocation_callbacks, const VkPhysicalDeviceMemoryProperties* physical_device_memory_properties, VkDeviceSize buffer_size, VkBufferUsageFlags usage_flags, VkMemoryPropertyFlags memory_flags)
{
    VulkanBuffer result = {
        .size = buffer_size,
    };

    VkBufferCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .pNext = 0,
        .flags = 0,
        .size = buffer_size,
        .usage = usage_flags,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .queueFamilyIndexCount = 0,
        .pQueueFamilyIndices = 0,
    };
    if (vkCreateBuffer(device, &create_info, allocation_callbacks, &result.handle) == VK_SUCCESS)
    {
        VkMemoryRequirements memory_requirements;
        vkGetBufferMemoryRequirements(device, result.handle, &memory_requirements);

        u8 use_device_address_bit = !!(create_info.usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT);
        result.memory = vk_allocate_memory(device, allocation_callbacks, physical_device_memory_properties, memory_requirements, memory_flags, use_device_address_bit);

        VkDeviceSize memory_offset = 0;
        if (vkBindBufferMemory(device, result.handle, result.memory.handle, memory_offset) == VK_SUCCESS)
        {
            u8 map_memory = !!(memory_flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
            BUSTER_CHECK(((map_memory | use_device_address_bit) == 0) | (map_memory == !use_device_address_bit));
            if (map_memory)
            {
                void* data = 0;
                VkDeviceSize offset = 0;
                VkMemoryMapFlags map_flags = 0;
                if (vkMapMemory(device, result.memory.handle, offset, memory_requirements.size, map_flags, &data) == VK_SUCCESS)
                {
                    result.address = (u64)data;
                }
            }

            if (use_device_address_bit)
            {
                VkBufferDeviceAddressInfo device_address_info = {
                    .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
                    .pNext = 0,
                    .buffer = result.handle,
                };
                result.address = vkGetBufferDeviceAddress(device, &device_address_info);
            }
        }
    }

    return result;
}

BUSTER_GLOBAL_LOCAL bool immediate_start(ImmediateContext context)
{
    VkFence fences[] = { context.fence };
    VkCommandBufferResetFlags reset_flags = 0;

    let reset_fences = vkResetFences(context.device, BUSTER_ARRAY_LENGTH(fences), fences);
    let reset_command_buffer = vkResetCommandBuffer(context.command_buffer, reset_flags);

    VkCommandBufferBeginInfo command_buffer_begin_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    let begin_command_buffer = vkBeginCommandBuffer(context.command_buffer, &command_buffer_begin_info);

    return reset_fences == VK_SUCCESS && reset_command_buffer == VK_SUCCESS && begin_command_buffer == VK_SUCCESS;
}

BUSTER_GLOBAL_LOCAL bool immediate_end(ImmediateContext context)
{
    VkFence fences[] = { context.fence };

    bool result = {};

    if (vkEndCommandBuffer(context.command_buffer) == VK_SUCCESS)
    {
        VkCommandBufferSubmitInfo command_buffer_submit_infos[] = {
            {
                .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
                .pNext = 0,
                .deviceMask = 0,
                .commandBuffer = context.command_buffer,
            }
        };

        VkSubmitFlags submit_flags = 0;

        VkSubmitInfo2 submit_info[] = {
            {
                .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
                .pNext = 0,
                .flags = submit_flags,
                .waitSemaphoreInfoCount = 0,
                .pWaitSemaphoreInfos = 0,
                .commandBufferInfoCount = BUSTER_ARRAY_LENGTH(command_buffer_submit_infos),
                .pCommandBufferInfos = command_buffer_submit_infos,
                .signalSemaphoreInfoCount = 0,
                .pSignalSemaphoreInfos = 0,
            }
        };

        if (vkQueueSubmit2(context.queue, BUSTER_ARRAY_LENGTH(submit_info), submit_info, context.fence) == VK_SUCCESS)
        {
            VkBool32 wait_all = 1;
            let timeout = ~(u64)0;

            if (vkWaitForFences(context.device, BUSTER_ARRAY_LENGTH(fences), fences, wait_all, timeout) == VK_SUCCESS)
            {
                result = true;
            }
        }
    }

    return result;
}

BUSTER_GLOBAL_LOCAL void vk_image_transition(VkCommandBuffer command_buffer, VkImage image, VkImageLayout old_layout, VkImageLayout new_layout)
{
    VkImageMemoryBarrier2 image_barrier = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
        .srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
        .dstAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT | VK_ACCESS_2_MEMORY_READ_BIT,
        .oldLayout = old_layout,
        .newLayout = new_layout,
        .image = image,
        .subresourceRange = {
            .aspectMask = (new_layout == VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL) ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = VK_REMAINING_MIP_LEVELS,
            .baseArrayLayer = 0,
            .layerCount = VK_REMAINING_ARRAY_LAYERS,
        },
    };

    VkDependencyInfo dependency_info = {
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = &image_barrier,
    };

    vkCmdPipelineBarrier2(command_buffer, &dependency_info);
}

STRUCT(VulkanCopyImage)
{
    VkImage handle;
    VkExtent2D extent;
};

STRUCT(VulkanCopyImageArgs)
{
    VulkanCopyImage source;
    VulkanCopyImage destination;
};

BUSTER_GLOBAL_LOCAL void vk_image_copy(VkCommandBuffer command_buffer, VulkanCopyImageArgs args)
{
    VkImageSubresourceLayers subresource_layers = {
        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        .mipLevel = 0,
        .baseArrayLayer = 0,
        .layerCount = 1,
    };
    VkImageBlit2 blit_regions[] = {
        {
            .sType = VK_STRUCTURE_TYPE_IMAGE_BLIT_2,
            .pNext = 0,
            .srcSubresource = subresource_layers,
            .srcOffsets = {
                [1] = {
                    .x = (s32)args.source.extent.width,
                    .y = (s32)args.source.extent.height,
                    .z = 1,
                },
            },
            .dstSubresource = subresource_layers,
            .dstOffsets = {
                [1] = {
                    .x = (s32)args.destination.extent.width,
                    .y = (s32)args.destination.extent.height,
                    .z = 1,
                },
            },
        },
    };

    VkBlitImageInfo2 blit_info = {
        .sType = VK_STRUCTURE_TYPE_BLIT_IMAGE_INFO_2,
        .pNext = 0,
        .srcImage = args.source.handle,
        .srcImageLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        .dstImage = args.destination.handle,
        .dstImageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .regionCount = BUSTER_ARRAY_LENGTH(blit_regions),
        .pRegions = blit_regions,
        .filter = VK_FILTER_LINEAR,
    };

    vkCmdBlitImage2(command_buffer, &blit_info);
}

BUSTER_GLOBAL_LOCAL void vk_buffer_destroy(RenderingHandle* rendering, VulkanBuffer* buffer)
{
    if (buffer->memory.handle)
    {
        vkFreeMemory(rendering->device, buffer->memory.handle, rendering->allocator);
        buffer->memory.handle = 0;
    }

    if (buffer->handle)
    {
        vkDestroyBuffer(rendering->device, buffer->handle, rendering->allocator);
        buffer->handle = 0;
    }
}

BUSTER_IMPL TextureIndex rendering_texture_create(RenderingHandle* rendering, TextureMemory texture_memory)
{
    BUSTER_CHECK(texture_memory.depth == 1);

    let texture_index = rendering->texture_count;
    rendering->texture_count += 1;
    let texture = &rendering->textures[texture_index];
    texture->image = vk_image_create(rendering->device, rendering->allocator, &rendering->device_memory_properties, (VulkanImageCreate) {
        .width = texture_memory.width,
        .height = texture_memory.height,
        .mip_levels = 1,
        .format = vk_texture_format(texture_memory.format),
        .usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
    });
    texture->sampler = rendering->sampler;

    let image_size = (u64)texture_memory.depth * texture_memory.width * texture_memory.height * format_channel_count(texture_memory.format);
    VkBufferUsageFlags buffer_usage_flags = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    VkMemoryPropertyFlags buffer_memory_flags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    texture->transfer_buffer = vk_buffer_create(rendering->device, rendering->allocator, &rendering->device_memory_properties, image_size, buffer_usage_flags, buffer_memory_flags);
    memcpy((void*)texture->transfer_buffer.address, texture_memory.pointer, image_size);

    immediate_start(rendering->immediate);

    vk_image_transition(rendering->immediate.command_buffer, texture->image.handle, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

    VkBufferImageCopy copy_regions[] = {
        {
            .bufferOffset = 0,
            .bufferRowLength = 0,
            .bufferImageHeight = 0,
            .imageSubresource = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .mipLevel = 0,
                .baseArrayLayer = 0,
                .layerCount = 1,
            },
            .imageOffset = {
                .x = 0,
                .y = 0,
                .z = 0,
            },
            .imageExtent = {
                .width = texture_memory.width,
                .height = texture_memory.height,
                .depth = texture_memory.depth,
            },
        }
    };

    vkCmdCopyBufferToImage(rendering->immediate.command_buffer, texture->transfer_buffer.handle, texture->image.handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, BUSTER_ARRAY_LENGTH(copy_regions), copy_regions);

    vk_image_transition(rendering->immediate.command_buffer, texture->image.handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    // Swiftshader's JIT compiler allocates internal state on first queue submission
    // that it never frees on vkDestroyDevice. Disable LSAN for this call so those
    // driver-internal allocations aren't reported as leaks.
    BUSTER_LSAN_DISABLE();
    immediate_end(rendering->immediate);
    BUSTER_LSAN_ENABLE();

    return (TextureIndex) { .value = texture_index };
}

BUSTER_IMPL TextureIndex white_texture_create(Arena* arena, RenderingHandle* rendering)
{
    u32 white_texture_width = 1024;
    u32 white_texture_height = white_texture_width;
    let white_texture_buffer = arena_allocate(arena, u32, white_texture_width * white_texture_height);
    memset(white_texture_buffer, 0xff, white_texture_width * white_texture_height * sizeof(u32));

    auto white_texture = rendering_texture_create(rendering, (TextureMemory) {
        .pointer = white_texture_buffer,
        .width = white_texture_width,
        .height = white_texture_height,
        .depth = 1,
        .format = TEXTURE_FORMAT_R8G8B8A8_SRGB,
    });

    return white_texture;
}

BUSTER_IMPL FontTextureAtlas rendering_font_create(Arena* arena, RenderingHandle* rendering, FontTextureAtlasCreate create)
{
    FontTextureAtlas result = {};
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

BUSTER_GLOBAL_LOCAL WindowFrame* window_frame(RenderingWindowHandle* window)
{
    return &window->frames[window->frame_index % window->frame_count];
}

BUSTER_IMPL void rendering_window_frame_begin(RenderingHandle* rendering, RenderingWindowHandle* window)
{
    let frame = window_frame(window);
    let timeout = ~(u64)0;

    u32 fence_count = 1;
    VkBool32 wait_all = 1;
    if (vkWaitForFences(rendering->device, fence_count, &frame->render_fence, wait_all, timeout) == VK_SUCCESS)
    {
        VkFence image_fence = 0;
        VkResult next_image_result = vkAcquireNextImageKHR(rendering->device, window->swapchain, timeout, frame->swapchain_semaphore, image_fence, &window->swapchain_image_index);

        if (next_image_result == VK_ERROR_OUT_OF_DATE_KHR)
        {
            swapchain_recreate(rendering, window);
        }
        else if (next_image_result != VK_SUCCESS && next_image_result != VK_SUBOPTIMAL_KHR)
        {
            os_fail();
        }

        VkCommandBufferResetFlags reset_flags = 0;
        bool success = vkResetFences(rendering->device, fence_count, &frame->render_fence) == VK_SUCCESS && vkResetCommandBuffer(frame->command_buffer, reset_flags) == VK_SUCCESS;
        if (!success)
        {
            os_fail();
        }
    }
    else
    {
        os_fail();
    }

    // Reset frame data
    for (u32 i = 0; i < BUSTER_ARRAY_LENGTH(window->pipeline_instantiations); i += 1)
    {
        let pipeline_instantiation = &frame->pipeline_instantiations[i];
        arena_reset_to_start(pipeline_instantiation->vertex_buffer.cpu);
        pipeline_instantiation->vertex_buffer.count = 0;
        arena_reset_to_start(pipeline_instantiation->index_buffer.cpu);
    }
}

BUSTER_GLOBAL_LOCAL void buffer_destroy(RenderingHandle* rendering, VulkanBuffer buffer)
{
    if (buffer.handle)
    {
        vkDestroyBuffer(rendering->device, buffer.handle, rendering->allocator);
    }

    if (buffer.memory.handle)
    {
        if (buffer.type == BUFFER_TYPE_STAGING)
        {
            vkUnmapMemory(rendering->device, buffer.memory.handle);
        }

        vkFreeMemory(rendering->device, buffer.memory.handle, rendering->allocator);
    }
}

BUSTER_GLOBAL_LOCAL VulkanBuffer buffer_create(RenderingHandle* rendering, u64 size, BufferType type)
{
    u8 is_dst = (type == BUFFER_TYPE_VERTEX) | (type == BUFFER_TYPE_INDEX);
    u8 is_src = type == BUFFER_TYPE_STAGING;

    VkBufferUsageFlags usage = 
        (VK_BUFFER_USAGE_TRANSFER_DST_BIT * is_dst) |
        (VK_BUFFER_USAGE_TRANSFER_SRC_BIT * is_src) |
        ((VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT) * (type == BUFFER_TYPE_VERTEX)) |
        (VK_BUFFER_USAGE_INDEX_BUFFER_BIT * (type == BUFFER_TYPE_INDEX));
    VkMemoryPropertyFlags memory_flags =
        (VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT * is_dst) |
        ((VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) * is_src);
    let result = vk_buffer_create(rendering->device, rendering->allocator, &rendering->device_memory_properties, size, usage, memory_flags);
    result.type = type;
    return result;
}

BUSTER_GLOBAL_LOCAL void buffer_ensure_capacity(RenderingHandle* rendering, VulkanBuffer* buffer, u64 needed_size)
{
    if (needed_size > buffer->memory.size)
    {
        buffer_destroy(rendering, *buffer);
        *buffer = buffer_create(rendering, needed_size, buffer->type);
    }
}

STRUCT(HostBufferCopy)
{
    ByteSlice source;
    u64 destination_offset;
};

SLICE(HostBufferCopySlice, HostBufferCopy);

BUSTER_GLOBAL_LOCAL void buffer_copy_to_host(VulkanBuffer buffer, HostBufferCopySlice regions)
{
    BUSTER_CHECK(buffer.type == BUFFER_TYPE_STAGING);

    let buffer_pointer = (u8*)buffer.address;

    for (u64 i = 0; i < regions.length; i += 1)
    {
        let region = regions.pointer[i];
        let destination = buffer_pointer + region.destination_offset;
        BUSTER_CHECK(destination + region.source.length <= (u8*)buffer.address + buffer.size);
#if USE_MEMCPY
        memcpy(destination, region.source.pointer, region.source.length);
#else
        for (u64 source_i = 0; source_i < region.source.length; source_i += 1)
        {
            destination[source_i] = region.source.pointer[source_i];
        }
#endif
    }
}

STRUCT(LocalBufferCopyRegion)
{
    u64 source_offset;
    u64 destination_offset;
    u64 size;
};
SLICE(LocalBufferCopyRegionSlice, LocalBufferCopyRegion);

STRUCT(LocalBufferCopy)
{
    VulkanBuffer destination;
    VulkanBuffer source;
    LocalBufferCopyRegionSlice regions;
};
SLICE(LocalBufferCopySlice, LocalBufferCopy);

#define MAX_LOCAL_BUFFER_COPY_COUNT (16)

BUSTER_GLOBAL_LOCAL void buffer_copy_to_local_command(VkCommandBuffer command_buffer, LocalBufferCopySlice copies)
{
    for (u64 copy_i = 0; copy_i < copies.length; copy_i += 1)
    {
        let copy = copies.pointer[copy_i];
        let source_buffer = &copy.source;
        let destination_buffer = &copy.destination;

        VkBufferCopy2 buffer_copies[MAX_LOCAL_BUFFER_COPY_COUNT];

        for (u64 copy_region_i = 0; copy_region_i < copy.regions.length; copy_region_i += 1)
        {
            let copy_region = copy.regions.pointer[copy_region_i];
            buffer_copies[copy_region_i] = (VkBufferCopy2) {
                .sType = VK_STRUCTURE_TYPE_BUFFER_COPY_2,
                .pNext = 0,
                .srcOffset = copy_region.source_offset,
                .dstOffset = copy_region.destination_offset,
                .size = copy_region.size,
            };
        }

        VkCopyBufferInfo2 info = {
            .sType = VK_STRUCTURE_TYPE_COPY_BUFFER_INFO_2,
            .pNext = 0,
            .srcBuffer = source_buffer->handle,
            .dstBuffer = destination_buffer->handle,
            .regionCount = (u32)copy.regions.length,
            .pRegions = buffer_copies,
        };

        vkCmdCopyBuffer2(command_buffer, &info);
    }
}

BUSTER_IMPL void rendering_window_frame_end(RenderingHandle* rendering, RenderingWindowHandle* window)
{
    let frame = window_frame(window);

    VkCommandBufferBeginInfo command_buffer_begin_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };

    if (vkBeginCommandBuffer(frame->command_buffer, &command_buffer_begin_info) == VK_SUCCESS)
    {
        for (u32 i = 0; i < BUSTER_PIPELINE_COUNT; i += 1)
        {
            let frame_pipeline_instantiation = &frame->pipeline_instantiations[i];

            if (!arena_buffer_is_empty(frame_pipeline_instantiation->vertex_buffer.cpu))
            {
                let new_vertex_buffer_size = arena_buffer_size(frame_pipeline_instantiation->vertex_buffer.cpu);
                let new_index_buffer_size = arena_buffer_size(frame_pipeline_instantiation->index_buffer.cpu);
                let new_transient_buffer_size = new_vertex_buffer_size + new_index_buffer_size;

                buffer_ensure_capacity(rendering, &frame_pipeline_instantiation->transient_buffer, new_transient_buffer_size);
                buffer_ensure_capacity(rendering, &frame_pipeline_instantiation->vertex_buffer.gpu, new_vertex_buffer_size);
                buffer_ensure_capacity(rendering, &frame_pipeline_instantiation->index_buffer.gpu, new_index_buffer_size);

                buffer_copy_to_host(frame_pipeline_instantiation->transient_buffer, (HostBufferCopySlice) BUSTER_ARRAY_TO_SLICE(((HostBufferCopy[]) {
                                (HostBufferCopy) {
                                .source = (ByteSlice) {
                                .pointer = arena_buffer_start(frame_pipeline_instantiation->vertex_buffer.cpu),
                                .length = new_vertex_buffer_size,
                                },
                                .destination_offset = 0,
                                },
                                (HostBufferCopy) {
                                .source = (ByteSlice) {
                                .pointer = arena_buffer_start(frame_pipeline_instantiation->index_buffer.cpu),
                                .length = new_index_buffer_size,
                                },
                                .destination_offset = new_vertex_buffer_size,
                                },
                                })));

                buffer_copy_to_local_command(frame->command_buffer, (LocalBufferCopySlice) BUSTER_ARRAY_TO_SLICE(((LocalBufferCopy[]) {
                                {
                                .destination = frame_pipeline_instantiation->vertex_buffer.gpu,
                                .source = frame_pipeline_instantiation->transient_buffer,
                                .regions = BUSTER_ARRAY_TO_SLICE(((LocalBufferCopyRegion[]) {
                                            {
                                            .source_offset = 0,
                                            .destination_offset = 0,
                                            .size = new_vertex_buffer_size,
                                            },
                                            })),
                                },
                                {
                                .destination = frame_pipeline_instantiation->index_buffer.gpu,
                                .source = frame_pipeline_instantiation->transient_buffer,
                                .regions = BUSTER_ARRAY_TO_SLICE(((LocalBufferCopyRegion[]) {
                                            {
                                            .source_offset = new_vertex_buffer_size,
                                            .destination_offset = 0,
                                            .size = new_index_buffer_size,
                                            },
                                            })),
                                },
                })));
            }
        }

        vk_image_transition(frame->command_buffer, window->render_image.handle, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

        VkViewport viewports[] = {
            {
                .x = 0,
                .y = 0,
                .width = (f32)window->width,
                .height = (f32)window->height,
                .minDepth = 0.0f,
                .maxDepth = 1.0f,
            }
        };

        u32 first_viewport = 0;
        vkCmdSetViewport(frame->command_buffer, first_viewport, BUSTER_ARRAY_LENGTH(viewports), viewports);

        VkRect2D scissors[] = {
            {
                .offset = {
                    .x = 0,
                    .y = 0,
                },
                .extent = {
                    .width = window->width,
                    .height = window->height,
                },
            }
        };

        u32 first_scissor = 0;
        vkCmdSetScissor(frame->command_buffer, first_scissor, BUSTER_ARRAY_LENGTH(scissors), scissors);

        VkRenderingAttachmentInfo color_attachments[] = {
            {
                .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
                .imageView = window->render_image.view,
                .imageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
                .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
                .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
                .clearValue = { .color = { .float32 = { 255.0f, 0.0f, 255.0f, 1.0f } } },
            },
        };

        VkRenderingInfo rendering_info = {
            .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
            .renderArea = {
                .extent = {
                    .width = window->width,
                    .height = window->height,
                },
            },
            .layerCount = 1,
            .colorAttachmentCount = BUSTER_ARRAY_LENGTH(color_attachments),
            .pColorAttachments = color_attachments,
        };

        vkCmdBeginRendering(frame->command_buffer, &rendering_info);

        for (u32 i = 0; i < BUSTER_PIPELINE_COUNT; i += 1)
        {
            let pipeline = &rendering->pipelines[i];
            let pipeline_instantiation = &window->pipeline_instantiations[i];
            let frame_pipeline_instantiation = &frame->pipeline_instantiations[i];

            if (!arena_buffer_is_empty(frame_pipeline_instantiation->vertex_buffer.cpu))
            {
                // Bind pipeline and descriptor sets
                {
                    vkCmdBindPipeline(frame->command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->handle);
                    // print("Binding pipeline: 0x{u64}\n", pipeline->handle);
                    u32 dynamic_offset_count = 0;
                    u32* dynamic_offsets = 0;
                    u32 first_set = 0;
                    vkCmdBindDescriptorSets(frame->command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->layout, first_set, pipeline->descriptor_set_count, pipeline_instantiation->descriptor_sets, dynamic_offset_count, dynamic_offsets);
                    // print("Binding descriptor sets: 0x{u64}\n", pipeline_instantiation->descriptor_sets);
                    frame->bound_pipeline = i;
                }

                // Bind index buffer
                {
                    vkCmdBindIndexBuffer(frame->command_buffer, frame_pipeline_instantiation->index_buffer.gpu.handle, 0, VK_INDEX_TYPE_UINT32);
                    frame->index_buffer = frame_pipeline_instantiation->index_buffer.gpu.handle;
                    // print("Binding descriptor sets: 0x{u64}\n", frame->index_buffer);
                }

                // Send vertex buffer and screen dimensions to the shader
                GPUDrawPushConstants push_constants = {
                    .vertex_buffer = frame_pipeline_instantiation->vertex_buffer.gpu.address,
                    .width = (f32)window->width,
                    .height = (f32)window->height,
                };

                {
                    let push_constant_range = pipeline->push_constant_ranges[0];
                    vkCmdPushConstants(frame->command_buffer, pipeline->layout, push_constant_range.stageFlags, push_constant_range.offset, push_constant_range.size, &push_constants);
                    frame->push_constants = push_constants;
                }

                vkCmdDrawIndexed(frame->command_buffer, (u32)(arena_buffer_size(frame_pipeline_instantiation->index_buffer.cpu) / sizeof(u32)), 1, 0, 0, 0);
            }
        }

        vkCmdEndRendering(frame->command_buffer);

        vk_image_transition(frame->command_buffer, window->render_image.handle, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

        VkImage swapchain_image = window->swapchain_images[window->swapchain_image_index];
        vk_image_transition(frame->command_buffer, swapchain_image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

        vk_image_copy(frame->command_buffer, (VulkanCopyImageArgs) {
                .source = {
                .handle = window->render_image.handle,
                .extent = {
                .width = window->width,
                .height = window->height,
                },
                },
                .destination = {
                .handle = swapchain_image,
                .extent = {
                .width = window->width,
                .height = window->height,
                },
                },
                });

        vk_image_transition(frame->command_buffer, swapchain_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);

        if (vkEndCommandBuffer(frame->command_buffer) == VK_SUCCESS)
        {
            VkCommandBufferSubmitInfo command_buffer_submit_info[] = {
                {
                    .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
                    .commandBuffer = frame->command_buffer,
                    .deviceMask = 0,
                },
            };

            VkSemaphoreSubmitInfo wait_semaphore_submit_info[] = {
                {
                    .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
                    .semaphore = frame->swapchain_semaphore,
                    .stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                    .deviceIndex = 0,
                    .value = 0,
                },
            };

            let render_semaphore = window->render_semaphores[window->swapchain_image_index];

            VkSemaphoreSubmitInfo signal_semaphore_submit_info[] = {
                {
                    .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
                    .semaphore = render_semaphore,
                    .stageMask = VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT,
                    .deviceIndex = 0,
                    .value = 0,
                },
            };

            VkSubmitInfo2 submit_info[] = {
                {
                    .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
                    .flags = 0,
                    .waitSemaphoreInfoCount = BUSTER_ARRAY_LENGTH(wait_semaphore_submit_info),
                    .pWaitSemaphoreInfos = wait_semaphore_submit_info,
                    .signalSemaphoreInfoCount = BUSTER_ARRAY_LENGTH(signal_semaphore_submit_info),
                    .pSignalSemaphoreInfos = signal_semaphore_submit_info,
                    .commandBufferInfoCount = BUSTER_ARRAY_LENGTH(command_buffer_submit_info),
                    .pCommandBufferInfos = command_buffer_submit_info,
                },
            };

            if (vkQueueSubmit2(rendering->graphics_queue, BUSTER_ARRAY_LENGTH(submit_info), submit_info, frame->render_fence) == VK_SUCCESS)
            {
                const VkSwapchainKHR swapchains[] = { window->swapchain };
                const u32 swapchain_image_indices[] = { window->swapchain_image_index };
                const VkSemaphore wait_semaphores[] = { render_semaphore };
                VkResult results[BUSTER_ARRAY_LENGTH(swapchains)];

                VkPresentInfoKHR present_info = {
                    .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
                    .waitSemaphoreCount = BUSTER_ARRAY_LENGTH(wait_semaphores),
                    .pWaitSemaphores = wait_semaphores,
                    .swapchainCount = BUSTER_ARRAY_LENGTH(swapchains),
                    .pSwapchains = swapchains,
                    .pImageIndices = swapchain_image_indices,
                    .pResults = results,
                };

                VkResult present_result = vkQueuePresentKHR(rendering->graphics_queue, &present_info);

                if (present_result == VK_SUCCESS)
                {
                    for (u32 i = 0; i < BUSTER_ARRAY_LENGTH(results); i += 1)
                    {
                        let result = results[i];
                        if (result != VK_SUCCESS)
                        {
                            os_fail();
                        }
                    }
                }
                else if (present_result == VK_ERROR_OUT_OF_DATE_KHR || present_result == VK_SUBOPTIMAL_KHR)
                {
                    swapchain_recreate(rendering, window);
                }
                else
                {
                    os_fail();
                }
            }
        }
    }
}

BUSTER_GLOBAL_LOCAL u32 rendering_window_pipeline_add_vertices(RenderingWindowHandle* window, BusterPipeline pipeline_index, ByteSlice vertex_memory, u32 vertex_count)
{
    let frame = window_frame(window);
    let vertex_buffer = &frame->pipeline_instantiations[pipeline_index].vertex_buffer;
    let allocation = arena_allocate_bytes(vertex_buffer->cpu, vertex_memory.length, 16);
    memcpy(allocation, vertex_memory.pointer, vertex_memory.length);
    let vertex_offset = vertex_buffer->count;
    vertex_buffer->count = vertex_offset + vertex_count;
    return vertex_offset;
}

BUSTER_GLOBAL_LOCAL void rendering_window_pipeline_add_indices(RenderingWindowHandle* window, BusterPipeline pipeline_index, u32Slice indices)
{
    let frame = window_frame(window);
    let index_buffer = &frame->pipeline_instantiations[pipeline_index].index_buffer;
    let allocation = arena_allocate(index_buffer->cpu, typeof(indices.pointer[0]), indices.length);
    memcpy(allocation, indices.pointer, indices.length * sizeof(*indices.pointer));
}

BUSTER_IMPL void rendering_window_render_rect(RenderingWindowHandle* window, RectDraw draw)
{
    let p0 = draw.vertex.p0;
    let uv0 = draw.texture.p0;

    if (draw.texture.p1.x != 0)
    {
        BUSTER_CHECK(draw.texture.p1.x - draw.texture.p0.x == draw.vertex.p1.x - draw.vertex.p0.x);
        BUSTER_CHECK(draw.texture.p1.y - draw.texture.p0.y == draw.vertex.p1.y - draw.vertex.p0.y);
    }

    let corner_radius = 5.0f;

    let extent = draw.vertex.p1 - p0;
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

    let vertex_offset = rendering_window_pipeline_add_vertices(window, BUSTER_PIPELINE_RECT, BUSTER_ARRAY_TO_BYTE_SLICE(vertices), BUSTER_ARRAY_LENGTH(vertices));

    u32 indices[] = {
        vertex_offset + 0,
        vertex_offset + 1,
        vertex_offset + 2,
        vertex_offset + 1,
        vertex_offset + 3,
        vertex_offset + 2,
    };

    rendering_window_pipeline_add_indices(window, BUSTER_PIPELINE_RECT, (u32Slice)BUSTER_ARRAY_TO_SLICE(indices));
}

// TODO: support gradient
BUSTER_IMPL void rendering_window_render_text(RenderingHandle* rendering, RenderingWindowHandle* window, String8 string, float4 color, RenderFontType font_type, f32 x_offset, f32 y_offset)
{
    let texture_atlas = &rendering->fonts[font_type];
    let height = texture_atlas->description.ascent - texture_atlas->description.descent;
    let texture_index = texture_atlas->texture.value;

    for (u64 i = 0; i < string.length; i += 1)
    {
        let ch = (u32)string.pointer[i];
        let character = &texture_atlas->description.characters[ch];

        let uv_x = character->x;
        let uv_y = character->y;

        let char_width = character->width;
        let char_height = character->height;

        let pos_x = x_offset;
        let pos_y = (s32)y_offset + character->y_offset + height + texture_atlas->description.descent; // Offset of the height to render the character from the bottom (y + height) up (y)
        vec2 p0 = { (f32)pos_x, (f32)pos_y };
        vec2 uv0 = { (f32)uv_x, (f32)uv_y };
        vec2 extent = { (f32)char_width, (f32)char_height };
        // print("P0: ({u32}, {u32}). P1: ({u32}, {u32})\n", (u32)p0.x, (u32)p0.y, (u32)p1.x, (u32)p1.y);

        RectVertex vertices[] = {
            {
                .p0 = p0,
                .uv0 = uv0,
                .extent = extent,
                .texture_index = texture_index,
                .colors = { color, color, color, color },
                .softness = 1.0,
            },
            {
                .p0 = p0,
                .uv0 = uv0,
                .extent = extent,
                .texture_index = texture_index,
                .colors = { color, color, color, color },
                .softness = 1.0,
            },
            {
                .p0 = p0,
                .uv0 = uv0,
                .extent = extent,
                .texture_index = texture_index,
                .colors = { color, color, color, color },
                .softness = 1.0,
            },
            {
                .p0 = p0,
                .uv0 = uv0,
                .extent = extent,
                .colors = { color, color, color, color },
                .texture_index = texture_index,
                .softness = 1.0,
            },
        };

        let vertex_offset = rendering_window_pipeline_add_vertices(window, BUSTER_PIPELINE_RECT, BUSTER_ARRAY_TO_BYTE_SLICE(vertices), BUSTER_ARRAY_LENGTH(vertices));

        u32 indices[] = {
            vertex_offset + 0,
            vertex_offset + 1,
            vertex_offset + 2,
            vertex_offset + 1,
            vertex_offset + 3,
            vertex_offset + 2,
        };

        rendering_window_pipeline_add_indices(window, BUSTER_PIPELINE_RECT, (u32Slice)BUSTER_ARRAY_TO_SLICE(indices));

        let kerning = (texture_atlas->description.kerning_tables + ch * 256)[(u32)string.pointer[i + 1]];
        x_offset += (f32)character->advance + (f32)kerning;
    }
}

BUSTER_IMPL void rendering_window_deinitialize(RenderingHandle* rendering, RenderingWindowHandle* window)
{
    if (vkDeviceWaitIdle(rendering->device) == VK_SUCCESS)
    {
        for (u32 i = 0; i < window->frame_count; i += 1)
        {
            let frame = &window->frames[i];
            if (frame->swapchain_semaphore)
            {
                vkDestroySemaphore(rendering->device, frame->swapchain_semaphore, rendering->allocator);
                frame->swapchain_semaphore = 0;
            }

            if (frame->render_fence)
            {
                vkDestroyFence(rendering->device, frame->render_fence, rendering->allocator);
                frame->render_fence = 0;
            }

            if (frame->command_pool)
            {
                vkDestroyCommandPool(rendering->device, frame->command_pool, rendering->allocator);
                frame->command_pool = 0;
                frame->command_buffer = 0;
            }

            for (u32 p = 0; p < BUSTER_ARRAY_LENGTH(frame->pipeline_instantiations); p += 1)
            {
                let pipeline = &frame->pipeline_instantiations[p];
                vk_buffer_destroy(rendering, &pipeline->vertex_buffer.gpu);
                vk_buffer_destroy(rendering, &pipeline->index_buffer.gpu);
                vk_buffer_destroy(rendering, &pipeline->transient_buffer);
            }
        }

        vkDestroyDescriptorPool(rendering->device, window->descriptor_pool, rendering->allocator);

        destroy_image(rendering->device, rendering->allocator, window->render_image.view, window->render_image.handle, window->render_image.memory.handle);

        for (u32 i = 0; i < window->swapchain_image_count; i += 1)
        {
            vkDestroyImageView(rendering->device, window->swapchain_image_views[i], rendering->allocator);
            vkDestroySemaphore(rendering->device, window->render_semaphores[i], rendering->allocator);
        }

        if (window->swapchain)
        {
            vkDestroySwapchainKHR(rendering->device, window->swapchain, rendering->allocator);
            window->swapchain = 0;
        }

        if (window->surface)
        {
            vkDestroySurfaceKHR(rendering->instance, window->surface, rendering->allocator);
            window->surface = 0;
        }
    }
    else
    {
        string8_print(S8("Device failed to wait idle\n"));
    }
}

BUSTER_IMPL void rendering_deinitialize(RenderingHandle* rendering)
{
    if (vkDeviceWaitIdle(rendering->device) == VK_SUCCESS)
    {
        for (u32 i = 0; i < BUSTER_PIPELINE_COUNT; i += 1)
        {
            let pipeline = &rendering->pipelines[i];
            for (u32 d = 0; d < pipeline->descriptor_set_count; d += 1)
            {
                vkDestroyDescriptorSetLayout(rendering->device, pipeline->descriptor_set_layouts[d], rendering->allocator);
            }

            vkDestroyPipeline(rendering->device, pipeline->handle, rendering->allocator);
            vkDestroyPipelineLayout(rendering->device, pipeline->layout, rendering->allocator);
        }

        vkDestroySampler(rendering->device, rendering->sampler, rendering->allocator);

        for (u32 i = 0; i < rendering->texture_count; i += 1)
        {
            let texture = &rendering->textures[i];
            vk_buffer_destroy(rendering, &texture->transfer_buffer);
            destroy_image(rendering->device, rendering->allocator, texture->image.view, texture->image.handle, texture->image.memory.handle);
        }

        if (rendering->immediate.fence)
        {
            vkDestroyFence(rendering->device, rendering->immediate.fence, rendering->allocator);
            rendering->immediate.fence = 0;
        }

        if (rendering->immediate.command_pool)
        {
            vkDestroyCommandPool(rendering->device, rendering->immediate.command_pool, rendering->allocator);
            rendering->immediate.command_pool = 0;
            rendering->immediate.command_buffer = 0;
        }

        if (rendering->device)
        {
            vkDestroyDevice(rendering->device, rendering->allocator);
            rendering->device = 0;
        }

        if (rendering->messenger)
        {
            vkDestroyDebugUtilsMessengerEXT(rendering->instance, rendering->messenger, rendering->allocator);
            rendering->messenger = 0;
        }

        if (rendering->instance)
        {
            vkDestroyInstance(rendering->instance, rendering->allocator);
            rendering->instance = 0;
        }

        if (rendering->vulkan_library)
        {
            os_dynamic_library_unload(rendering_handle.vulkan_library);
        }
    }
    else
    {
        string8_print(S8("Device failed to wait idle\n"));
    }
}
