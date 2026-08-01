#include <buster/rendering/internal.h>

#define BUSTER_VULKAN_FUNCTION_POINTER(n) PFN_##n n
#define BUSTER_GLOBAL_VULKAN_FUNCTION_POINTER(n) BUSTER_GLOBAL_LOCAL __attribute__((used)) BUSTER_VULKAN_FUNCTION_POINTER(n)
#define BUSTER_VULKAN_OS_LOAD_FUNCTION(vulkan_library, function)                                                                                               \
    function = (__typeof__(function))os_dynamic_library_function_load(vulkan_library, S8(#function));
#define BUSTER_VULKAN_FUNCTION_LOAD_GENERIC(context, load_function, function) function = (__typeof__(function))load_function(context, #function)
#define BUSTER_VULKAN_LOAD_INSTANCE_FUNCTION(instance, function) BUSTER_VULKAN_FUNCTION_LOAD_GENERIC(instance, vkGetInstanceProcAddr, function)
#define BUSTER_VULKAN_LOAD_DEVICE_FUNCTION(device, function) BUSTER_VULKAN_FUNCTION_LOAD_GENERIC(device, vkGetDeviceProcAddr, function)

#ifndef VK_NO_PROTOTYPES
#define VK_NO_PROTOTYPES
#endif

#ifndef VULKAN_H_

#if defined(__clang__)
#pragma clang diagnostic ignored "-Wpadded"
#endif

#if defined(_WIN32)
#define VK_USE_PLATFORM_WIN32_KHR
#elif BUSTER_ANDROID
#define VK_USE_PLATFORM_ANDROID_KHR
#elif BUSTER_LINUX
#define VK_USE_PLATFORM_XCB_KHR
#elif defined(__APPLE__)
#define VK_USE_PLATFORM_METAL_EXT
#endif
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

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wreserved-identifier"
#endif
typedef struct _SECURITY_ATTRIBUTES SECURITY_ATTRIBUTES;
#if defined(__clang__)
#pragma clang diagnostic pop
#endif

#include <vulkan/vulkan_win32.h>

#ifdef VK_ENABLE_BETA_EXTENSIONS
#include <vulkan/vulkan_beta.h>
#endif
#else
#include <vulkan/vulkan.h>
#endif
#endif

typedef enum ShaderStage
{
    SHADER_STAGE_VERTEX,
    SHADER_STAGE_FRAGMENT,
    SHADER_STAGE_COUNT,
} ShaderStage;

typedef struct PushConstantRange PushConstantRange;
struct PushConstantRange
{
    u16 offset;
    u16 size;
    ShaderStage stage;
    u8 reserved;
};

typedef struct SlicePushConstantRange SlicePushConstantRange;
struct SlicePushConstantRange
{
    PushConstantRange* pointer;
    u64 length;
};

typedef enum DescriptorType
{
    DESCRIPTOR_TYPE_IMAGE_PLUS_SAMPLER,
    DESCRIPTOR_TYPE_STORAGE_BUFFER,
    DESCRIPTOR_TYPE_COUNT,
} DescriptorType;

typedef struct DescriptorSetLayoutBinding DescriptorSetLayoutBinding;
struct DescriptorSetLayoutBinding
{
    u8 binding;
    DescriptorType type;
    ShaderStage stage;
    u8 count;
};

typedef struct SliceDescriptorSetLayoutBinding SliceDescriptorSetLayoutBinding;
struct SliceDescriptorSetLayoutBinding
{
    DescriptorSetLayoutBinding* pointer;
    u64 length;
};

typedef struct DescriptorSetLayoutCreate DescriptorSetLayoutCreate;
struct DescriptorSetLayoutCreate
{
    SliceDescriptorSetLayoutBinding bindings;
};

typedef struct SliceDescriptorSetLayoutCreate SliceDescriptorSetLayoutCreate;
struct SliceDescriptorSetLayoutCreate
{
    DescriptorSetLayoutCreate* pointer;
    u64 length;
};

typedef struct PipelineLayoutCreate PipelineLayoutCreate;
struct PipelineLayoutCreate
{
    SlicePushConstantRange push_constant_ranges;
    SliceDescriptorSetLayoutCreate descriptor_set_layouts;
};

typedef struct SlicePipelineLayoutCreate SlicePipelineLayoutCreate;
struct SlicePipelineLayoutCreate
{
    PipelineLayoutCreate* pointer;
    u64 length;
};

typedef struct GPUDrawPushConstants GPUDrawPushConstants;
struct GPUDrawPushConstants
{
    u64 vertex_buffer;
    f32 width;
    f32 height;
};

// Android pulls vertices from a storage buffer, so its push constant carries
// only the screen dimensions (no device-address pointer).
typedef struct DrawConstants DrawConstants;
struct DrawConstants
{
    f32 width;
    f32 height;
};

typedef struct PipelineCreate PipelineCreate;
struct PipelineCreate
{
    Sliceu16 shader_source_indices;
    u16 layout_index;
    u8 reserved[6];
};

typedef struct SlicePipelineCreate SlicePipelineCreate;
struct SlicePipelineCreate
{
    PipelineCreate* pointer;
    u64 length;
};

typedef struct GraphicsPipelinesCreate GraphicsPipelinesCreate;
struct GraphicsPipelinesCreate
{
    SliceString8 shader_binaries;
    SlicePipelineLayoutCreate layouts;
    SlicePipelineCreate pipelines;
};

#define MAX_SHADER_MODULE_COUNT_PER_PIPELINE (16)

// INSTANCE FUNCTIONS START
// These functions require no instance
BUSTER_GLOBAL_VULKAN_FUNCTION_POINTER(vkGetInstanceProcAddr);
BUSTER_GLOBAL_VULKAN_FUNCTION_POINTER(vkEnumerateInstanceVersion);
BUSTER_GLOBAL_VULKAN_FUNCTION_POINTER(vkEnumerateInstanceLayerProperties);
BUSTER_GLOBAL_VULKAN_FUNCTION_POINTER(vkEnumerateInstanceExtensionProperties);
BUSTER_GLOBAL_VULKAN_FUNCTION_POINTER(vkCreateInstance);

// These functions require an instance as a parameter
BUSTER_GLOBAL_VULKAN_FUNCTION_POINTER(vkGetDeviceProcAddr);
BUSTER_GLOBAL_VULKAN_FUNCTION_POINTER(vkCreateDebugUtilsMessengerEXT);
BUSTER_GLOBAL_VULKAN_FUNCTION_POINTER(vkDestroyDebugUtilsMessengerEXT);
BUSTER_GLOBAL_VULKAN_FUNCTION_POINTER(vkEnumeratePhysicalDevices);
BUSTER_GLOBAL_VULKAN_FUNCTION_POINTER(vkGetPhysicalDeviceMemoryProperties);
BUSTER_GLOBAL_VULKAN_FUNCTION_POINTER(vkGetPhysicalDeviceProperties);
BUSTER_GLOBAL_VULKAN_FUNCTION_POINTER(vkGetPhysicalDeviceQueueFamilyProperties);
BUSTER_GLOBAL_VULKAN_FUNCTION_POINTER(vkEnumerateDeviceExtensionProperties);
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
#if defined(VK_KHR_android_surface)
BUSTER_GLOBAL_VULKAN_FUNCTION_POINTER(vkCreateAndroidSurfaceKHR);
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

BUSTER_GLOBAL_LOCAL VkBool32 buster_vulkan_debug_callback(VkDebugUtilsMessageSeverityFlagBitsEXT message_severity, VkDebugUtilsMessageTypeFlagsEXT message_type,
                                                          const VkDebugUtilsMessengerCallbackDataEXT* callback_data, void* user_data)
{
    BUSTER_UNUSED(user_data);

    String8 severity_string;

    switch (message_severity)
    {
        break;
    case VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT:
        severity_string = S8("VERBOSE");
        break;
    case VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT:
        severity_string = S8("INFORMATION");
        break;
    case VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT:
        severity_string = S8("WARNING");
        break;
    case VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT:
        severity_string = S8("ERROR");
        break;
    default:
        severity_string = S8("UNKNOWN");
    }

    bool is_general = (message_type & (VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT)) != 0;
    bool is_validation = (message_type & (VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT)) != 0;
    bool is_performance = (message_type & (VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT)) != 0;
    bool is_device_address_binding = (message_type & (VK_DEBUG_UTILS_MESSAGE_TYPE_DEVICE_ADDRESS_BINDING_BIT_EXT)) != 0;
    BUSTER_CHECK(is_general || is_validation || is_performance || is_device_address_binding);

    String8 message_id = string_from_pointer((char8*)callback_data->pMessageIdName);
    if (!message_id.pointer)
    {
        message_id = S8("Message ID not specified");
    }

    String8 message = string_from_pointer((char8*)callback_data->pMessage);
    if (!message.pointer)
    {
        message = S8("Message not specified");
    }

    string_print(S8("[{S8}][{S8}] {S8}\n"), severity_string, message_id, message);

    if ((message_severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) != 0)
    {
        os_fail();
    }

    return VK_FALSE;
}

BUSTER_GLOBAL_LOCAL bool vulkan_instance_extension_supported(VkExtensionProperties* properties, u32 property_count, const char* name)
{
    bool result = false;
    String8 requested = string_from_pointer((char8*)name);
    for (u32 i = 0; i < property_count; i += 1)
    {
        String8 available = string_from_pointer((char8*)properties[i].extensionName);
        if (string_equal(available, requested))
        {
            result = true;
            break;
        }
    }

    return result;
}

BUSTER_GLOBAL_LOCAL bool vulkan_device_extension_supported(VkExtensionProperties* properties, u32 property_count, const char* name)
{
    return vulkan_instance_extension_supported(properties, property_count, name);
}

BUSTER_GLOBAL_LOCAL bool vulkan_instance_layer_supported(VkLayerProperties* properties, u32 property_count, const char* name)
{
    bool result = false;
    String8 requested = string_from_pointer((char8*)name);
    for (u32 i = 0; i < property_count; i += 1)
    {
        String8 available = string_from_pointer((char8*)properties[i].layerName);
        if (string_equal(available, requested))
        {
            result = true;
            break;
        }
    }

    return result;
}

#define MAX_DESCRIPTOR_SET_LAYOUT_BINDING_COUNT (16)

typedef struct DescriptorSetLayoutBindings DescriptorSetLayoutBindings;
struct DescriptorSetLayoutBindings
{
    VkDescriptorSetLayoutBinding buffer[MAX_DESCRIPTOR_SET_LAYOUT_BINDING_COUNT];
    u32 count;
    u8 reserved[4];
};
#define MAX_DESCRIPTOR_SET_COUNT (16)
#define MAX_PUSH_CONSTANT_RANGE_COUNT (16)

typedef struct Pipeline Pipeline;
struct Pipeline
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
    case DESCRIPTOR_TYPE_STORAGE_BUFFER:
        result = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        break;
    case DESCRIPTOR_TYPE_COUNT:
        BUSTER_UNREACHABLE();
    default:
        BUSTER_UNREACHABLE();
    }

    return result;
}

BUSTER_GLOBAL_LOCAL VkShaderStageFlags vulkan_shader_stage(ShaderStage shader_stage)
{
    VkShaderStageFlags result = {0};

    switch (shader_stage)
    {
        break;
    case SHADER_STAGE_VERTEX:
        result = VK_SHADER_STAGE_VERTEX_BIT;
        break;
    case SHADER_STAGE_FRAGMENT:
        result = VK_SHADER_STAGE_FRAGMENT_BIT;
        break;
    case SHADER_STAGE_COUNT:
        BUSTER_UNREACHABLE();
    }

    return result;
}

BUSTER_GLOBAL_LOCAL VkShaderStageFlagBits vulkan_shader_stage_from_path(String8 shader_binary_path)
{
    VkShaderStageFlagBits shader_stage;
    if (string_ends_with_sequence(shader_binary_path, S8(".vert.spv")))
    {
        shader_stage = VK_SHADER_STAGE_VERTEX_BIT;
    }
    else if (string_ends_with_sequence(shader_binary_path, S8(".frag.spv")))
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

typedef struct GPUMemory GPUMemory;
struct GPUMemory
{
    VkDeviceMemory handle;
    u64 size;
};

typedef struct VulkanImage VulkanImage;
struct VulkanImage
{
    VkImage handle;
    VkImageView view;
    GPUMemory memory;
    VkFormat format;
    u8 reserved[4];
};

typedef struct VulkanImageCreate VulkanImageCreate;
struct VulkanImageCreate
{
    u32 width;
    u32 height;
    u32 mip_levels;
    VkFormat format;
    VkImageUsageFlags usage;
};

BUSTER_GLOBAL_LOCAL GPUMemory vk_allocate_memory(VkDevice device, const VkAllocationCallbacks* allocation_callbacks,
                                                 const VkPhysicalDeviceMemoryProperties* memory_properties, VkMemoryRequirements memory_requirements,
                                                 VkMemoryPropertyFlags flags, u8 use_device_address_bit)
{
    u32 memory_type_index;
    u32 memory_type_count = memory_properties->memoryTypeCount;
    for (memory_type_index = 0; memory_type_index < memory_type_count; memory_type_index += 1)
    {
        VkMemoryType memory_type = memory_properties->memoryTypes[memory_type_index];

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

    return (GPUMemory){.handle = memory, .size = allocate_info.allocationSize};
}

BUSTER_GLOBAL_LOCAL VulkanImage vk_image_create(VkDevice device, const VkAllocationCallbacks* allocation_callbacks,
                                                const VkPhysicalDeviceMemoryProperties* memory_properties, VulkanImageCreate create)
{
    BUSTER_CHECK(create.width);
    BUSTER_CHECK(create.height);
    VulkanImage result = {0};
    result.format = create.format;

    VkImageCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .pNext = 0,
        .flags = 0,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = create.format,
        .extent =
            {
                .width = create.width,
                .height = create.height,
                .depth = 1,
            },
        .mipLevels = create.mip_levels,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = create.usage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .queueFamilyIndexCount = 0,
        .pQueueFamilyIndices = 0,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
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
                    .subresourceRange =
                        {
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

typedef enum BufferType
{
    BUFFER_TYPE_VERTEX,
    BUFFER_TYPE_INDEX,
    BUFFER_TYPE_STAGING,
    BUFFER_TYPE_COUNT,
} BufferType;

typedef struct VulkanBuffer VulkanBuffer;
struct VulkanBuffer
{
    VkBuffer handle;
    GPUMemory memory;
    u64 address;
    VkDeviceSize size;
    BufferType type;
    u8 reserved[7];
};

typedef struct VertexBuffer VertexBuffer;
struct VertexBuffer
{
    VulkanBuffer gpu;
    Arena* cpu;
    u32 count;
    u8 reserved[4];
};

typedef struct IndexBuffer IndexBuffer;
struct IndexBuffer
{
    VulkanBuffer gpu;
    Arena* cpu;
};

typedef struct FramePipelineInstantiation FramePipelineInstantiation;
struct FramePipelineInstantiation
{
    VertexBuffer vertex_buffer;
    IndexBuffer index_buffer;
    VulkanBuffer transient_buffer;
};

typedef struct WindowFrame WindowFrame;
struct WindowFrame
{
    VkCommandPool command_pool;
    VkCommandBuffer command_buffer;
    VkSemaphore swapchain_semaphore;
    VkFence render_fence;
    VkBuffer index_buffer;
    GPUDrawPushConstants push_constants;
    FramePipelineInstantiation pipeline_instantiations[(u64)BUSTER_PIPELINE_COUNT];
    BusterPipeline bound_pipeline;
    u8 reserved[4];
};

#define MAX_TEXTURE_UPDATE_COUNT (32)

typedef struct PipelineInstantiation PipelineInstantiation;
struct PipelineInstantiation
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
        break;
    case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
        result = DESCRIPTOR_TYPE_IMAGE_PLUS_SAMPLER;
        break;
    case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
        result = DESCRIPTOR_TYPE_STORAGE_BUFFER;
        break;
    default:
        BUSTER_UNREACHABLE();
    }

    return result;
}

typedef struct ImmediateContext ImmediateContext;
struct ImmediateContext
{
    VkDevice device;
    VkFence fence;
    VkCommandBuffer command_buffer;
    VkCommandPool command_pool;
    VkQueue queue;
};

typedef struct VulkanTexture VulkanTexture;
struct VulkanTexture
{
    VulkanImage image;
    VkSampler sampler;
    VulkanBuffer transfer_buffer;
};

#define MAX_TEXTURE_COUNT (16)

typedef struct RenderingHandle RenderingHandle;
struct RenderingHandle
{
    OsModuleHandle* vulkan_library;
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

typedef struct RenderingWindowHandle RenderingWindowHandle;
struct RenderingWindowHandle
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

BUSTER_GLOBAL_LOCAL RenderingHandle rendering_handle = {0};
BUSTER_GLOBAL_LOCAL u32 vulkan_frame_begin_log_count = 0;
BUSTER_GLOBAL_LOCAL u32 vulkan_frame_end_log_count = 0;

BUSTER_GLOBAL_LOCAL void rendering_window_texture_update_begin(RenderingWindowHandle* window, BusterPipeline pipeline_index, u32 descriptor_count)
{
    PipelineInstantiation* pipeline_instantiation = &window->pipeline_instantiations[(u64)pipeline_index];
    BUSTER_CHECK(descriptor_count <= BUSTER_ARRAY_LENGTH(pipeline_instantiation->texture_descriptors));
    BUSTER_CHECK(descriptor_count);
    BUSTER_CHECK(pipeline_instantiation->descriptor_sets[0]);

    pipeline_instantiation->descriptor_set_update = (VkWriteDescriptorSet){
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

void rendering_window_texture_update_end(RenderingHandle* rendering, RenderingWindowHandle* window, BusterPipeline pipeline_index)
{
    PipelineInstantiation* pipeline_instantiation = &window->pipeline_instantiations[(u64)pipeline_index];
    u32 descriptor_copy_count = 0;
    VkCopyDescriptorSet* descriptor_copies = 0;
    VkWriteDescriptorSet descriptor_set_writes[] = {
        pipeline_instantiation->descriptor_set_update,
    };
    vkUpdateDescriptorSets(rendering->device, BUSTER_ARRAY_LENGTH(descriptor_set_writes), descriptor_set_writes, descriptor_copy_count, descriptor_copies);
}

void rendering_window_rect_texture_update_begin(RenderingWindowHandle* window)
{
    rendering_window_texture_update_begin(window, BUSTER_PIPELINE_RECT, (u32)RECT_TEXTURE_SLOT_COUNT);
}

void window_rect_texture_update_end(RenderingHandle* rendering, RenderingWindowHandle* window)
{
    rendering_window_texture_update_end(rendering, window, BUSTER_PIPELINE_RECT);
}

__attribute__((noinline)) RenderingHandle* rendering_initialize(Arena* arena)
{
    RenderingHandle* rendering = 0;
    VkAllocationCallbacks* allocator = 0;
    vulkan_frame_begin_log_count = 0;
    vulkan_frame_end_log_count = 0;
#if BUSTER_ANDROID
    rendering_handle.vulkan_library = os_dynamic_library_load(S8("libvulkan.so"));
#elif BUSTER_LINUX
    rendering_handle.vulkan_library = os_dynamic_library_load(S8("libvulkan.so.1"));
#elif defined(_WIN32)
    rendering_handle.vulkan_library = os_dynamic_library_load(S8("vulkan-1.dll"));
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
        rendering_handle.vulkan_library = os_dynamic_library_load("MoltenVK.framework/MoltenVK");
    }
#endif
    // Android (incl. emulator) does not ship the Khronos validation layers, so
    // requesting them would make instance creation fail. Keep validation desktop-only.
    bool enable_validation = !BUSTER_ANDROID && BUSTER_GPU_VALIDATION_ENABLED;
    string_print(S8("Vulkan rendering initialization: library={u64:x}, validation={u32}\n"), (u64)rendering_handle.vulkan_library, (u32)enable_validation);
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
                string_print(S8("Vulkan instance version: vkEnumerateInstanceVersion={u64:x}, api_version={u32}\n"), (u64)(u32)result, api_version);

                if (result == VK_SUCCESS && api_version >= VK_API_VERSION_1_3)
                {
                    BUSTER_VULKAN_LOAD_INSTANCE_FUNCTION(0, vkEnumerateInstanceLayerProperties);
                    BUSTER_VULKAN_LOAD_INSTANCE_FUNCTION(0, vkEnumerateInstanceExtensionProperties);

                    const char* validation_layer_name = "VK_LAYER_KHRONOS_validation";
                    VkLayerProperties instance_layer_properties[256];
                    u32 instance_layer_property_count = 0;
                    bool validation_layer_supported = false;
                    if (enable_validation && vkEnumerateInstanceLayerProperties)
                    {
                        VkResult layer_result = vkEnumerateInstanceLayerProperties(&instance_layer_property_count, 0);
                        if (layer_result == VK_SUCCESS && instance_layer_property_count <= BUSTER_ARRAY_LENGTH(instance_layer_properties))
                        {
                            layer_result = vkEnumerateInstanceLayerProperties(&instance_layer_property_count, instance_layer_properties);
                            if (layer_result == VK_SUCCESS)
                            {
                                validation_layer_supported =
                                    vulkan_instance_layer_supported(instance_layer_properties, instance_layer_property_count, validation_layer_name);
                            }
                        }
                    }

                    bool enable_validation_layer = enable_validation && validation_layer_supported;
                    if (enable_validation && !enable_validation_layer)
                    {
                        string_print(S8("Vulkan validation layer unavailable; continuing without validation\n"));
                    }

                    const char* instance_layer_names[] = {"VK_LAYER_KHRONOS_validation"};
                    const char* const* enabled_layer_names = 0;
                    u32 enabled_layer_count = 0;
                    if (enable_validation_layer)
                    {
                        enabled_layer_names = instance_layer_names;
                        enabled_layer_count = BUSTER_ARRAY_LENGTH(instance_layer_names);
                    }

                    VkExtensionProperties instance_extension_properties[512];
                    u32 instance_extension_property_count = 0;
                    if (result == VK_SUCCESS && vkEnumerateInstanceExtensionProperties)
                    {
                        result = vkEnumerateInstanceExtensionProperties(0, &instance_extension_property_count, 0);
                        if (result == VK_SUCCESS && instance_extension_property_count <= BUSTER_ARRAY_LENGTH(instance_extension_properties))
                        {
                            result = vkEnumerateInstanceExtensionProperties(0, &instance_extension_property_count, instance_extension_properties);
                        }
                        else if (result == VK_SUCCESS)
                        {
                            result = VK_ERROR_EXTENSION_NOT_PRESENT;
                        }
                    }

                    const char* enabled_extension_names[16];
                    u32 enabled_extension_count = 0;
                    bool missing_required_instance_extension = false;
                    bool enable_debug_utils = false;
                    bool enable_shader_debug_printf = false;
                    VkInstanceCreateFlags instance_create_flags = 0;

#define BUSTER_VULKAN_ENABLE_REQUIRED_INSTANCE_EXTENSION(extension_name)                                                                                       \
    do                                                                                                                                                         \
    {                                                                                                                                                          \
        if (vulkan_instance_extension_supported(instance_extension_properties, instance_extension_property_count, (extension_name)))                           \
        {                                                                                                                                                      \
            enabled_extension_names[enabled_extension_count++] = (extension_name);                                                                             \
        }                                                                                                                                                      \
        else                                                                                                                                                   \
        {                                                                                                                                                      \
            missing_required_instance_extension = true;                                                                                                        \
            string_print(S8("Vulkan required instance extension unavailable: {S8}\n"), string_from_pointer((char8*)(extension_name)));                         \
        }                                                                                                                                                      \
    } while (0)

#define BUSTER_VULKAN_ENABLE_OPTIONAL_INSTANCE_EXTENSION(extension_name, enabled_variable)                                                                     \
    do                                                                                                                                                         \
    {                                                                                                                                                          \
        if (vulkan_instance_extension_supported(instance_extension_properties, instance_extension_property_count, (extension_name)))                           \
        {                                                                                                                                                      \
            enabled_extension_names[enabled_extension_count++] = (extension_name);                                                                             \
            (enabled_variable) = true;                                                                                                                         \
        }                                                                                                                                                      \
    } while (0)

                    if (result == VK_SUCCESS)
                    {
                        BUSTER_VULKAN_ENABLE_REQUIRED_INSTANCE_EXTENSION(VK_KHR_SURFACE_EXTENSION_NAME);
#ifdef VK_USE_PLATFORM_WIN32_KHR
                        BUSTER_VULKAN_ENABLE_REQUIRED_INSTANCE_EXTENSION(VK_KHR_WIN32_SURFACE_EXTENSION_NAME);
#endif
#ifdef VK_USE_PLATFORM_XLIB_KHR
                        BUSTER_VULKAN_ENABLE_REQUIRED_INSTANCE_EXTENSION(VK_KHR_XLIB_SURFACE_EXTENSION_NAME);
#endif
#ifdef VK_USE_PLATFORM_XCB_KHR
                        BUSTER_VULKAN_ENABLE_REQUIRED_INSTANCE_EXTENSION(VK_KHR_XCB_SURFACE_EXTENSION_NAME);
#endif
#ifdef VK_USE_PLATFORM_WAYLAND_KHR
                        BUSTER_VULKAN_ENABLE_REQUIRED_INSTANCE_EXTENSION(VK_KHR_WAYLAND_SURFACE_EXTENSION_NAME);
#endif
#ifdef VK_USE_PLATFORM_METAL_EXT
                        BUSTER_VULKAN_ENABLE_REQUIRED_INSTANCE_EXTENSION(VK_EXT_METAL_SURFACE_EXTENSION_NAME);
                        if (vulkan_instance_extension_supported(instance_extension_properties, instance_extension_property_count,
                                                                VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME))
                        {
                            enabled_extension_names[enabled_extension_count++] = VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME;
                            instance_create_flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
                        }
#endif
#ifdef VK_USE_PLATFORM_ANDROID_KHR
                        BUSTER_VULKAN_ENABLE_REQUIRED_INSTANCE_EXTENSION(VK_KHR_ANDROID_SURFACE_EXTENSION_NAME);
#endif

                        if (enable_validation_layer)
                        {
                            BUSTER_VULKAN_ENABLE_OPTIONAL_INSTANCE_EXTENSION(VK_EXT_DEBUG_UTILS_EXTENSION_NAME, enable_debug_utils);
                            BUSTER_VULKAN_ENABLE_OPTIONAL_INSTANCE_EXTENSION(VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME, enable_shader_debug_printf);
                            if (!enable_debug_utils)
                            {
                                string_print(S8("Vulkan debug utils extension unavailable; debug messenger disabled\n"));
                            }
                            if (!enable_shader_debug_printf)
                            {
                                string_print(S8("Vulkan validation features extension unavailable; shader debug printf disabled\n"));
                            }
                        }
                    }

#undef BUSTER_VULKAN_ENABLE_REQUIRED_INSTANCE_EXTENSION
#undef BUSTER_VULKAN_ENABLE_OPTIONAL_INSTANCE_EXTENSION

                    VkValidationFeatureEnableEXT enabled_validation_features[] = {
                        VK_VALIDATION_FEATURE_ENABLE_DEBUG_PRINTF_EXT,
                    };

                    VkDebugUtilsMessengerCreateInfoEXT messenger_create_info = {
                        .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
                        .pNext = 0,
                        .messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT |
                                           VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
                        .messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                                       VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
                        .pfnUserCallback = &buster_vulkan_debug_callback,
                        .pUserData = 0,
                    };

                    VkValidationFeaturesEXT validation_features = {
                        .sType = VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT,
                        .enabledValidationFeatureCount = BUSTER_ARRAY_LENGTH(enabled_validation_features),
                        .pEnabledValidationFeatures = enabled_validation_features,
                        .pNext = enable_debug_utils ? &messenger_create_info : 0,
                    };

                    void* p_next = enable_shader_debug_printf ? (void*)&validation_features : (enable_debug_utils ? (void*)&messenger_create_info : 0);

                    VkApplicationInfo application_info = {
                        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
                        .apiVersion = api_version,
                    };
                    if (result == VK_SUCCESS && !missing_required_instance_extension)
                    {
                        u32 portability_enabled = 0;
#if defined(VK_USE_PLATFORM_METAL_EXT)
                        portability_enabled = (u32)((instance_create_flags & VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR) != 0);
#endif
                        string_print(S8("Vulkan instance creation: extensions={u32}, layers_enabled={u32}, validation_layer_supported={u32}, "
                                        "debug_utils={u32}, validation_features={u32}, portability={u32}\n"),
                                     enabled_extension_count, enabled_layer_count, (u32)validation_layer_supported, (u32)enable_debug_utils,
                                     (u32)enable_shader_debug_printf, portability_enabled);

                        VkInstanceCreateInfo instance_create_info = {
                            .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
                            .pNext = p_next,
                            .flags = instance_create_flags,
                            .pApplicationInfo = &application_info,
                            .ppEnabledLayerNames = enabled_layer_names,
                            .enabledLayerCount = enabled_layer_count,
                            .ppEnabledExtensionNames = enabled_extension_names,
                            .enabledExtensionCount = enabled_extension_count,
                        };

                        result = vkCreateInstance(&instance_create_info, allocator, &rendering_handle.instance);
                        string_print(S8("Vulkan instance creation: vkCreateInstance={u64:x}, instance={u64:x}\n"), (u64)(u32)result,
                                     (u64)rendering_handle.instance);

                        if (result == VK_SUCCESS && enable_debug_utils)
                        {
                            BUSTER_VULKAN_LOAD_INSTANCE_FUNCTION(rendering_handle.instance, vkCreateDebugUtilsMessengerEXT);
                            BUSTER_VULKAN_LOAD_INSTANCE_FUNCTION(rendering_handle.instance, vkDestroyDebugUtilsMessengerEXT);
                            if (vkCreateDebugUtilsMessengerEXT)
                            {
                                VkResult messenger_result =
                                    vkCreateDebugUtilsMessengerEXT(rendering_handle.instance, &messenger_create_info, allocator, &rendering_handle.messenger);
                                string_print(S8("Vulkan debug messenger creation: vkCreateDebugUtilsMessengerEXT={u64:x}, messenger={u64:x}\n"),
                                             (u64)(u32)messenger_result, (u64)rendering_handle.messenger);
                            }
                        }
                    }
                    else
                    {
                        result = VK_ERROR_EXTENSION_NOT_PRESENT;
                        string_print(S8("Vulkan instance creation skipped: required instance extension support missing\n"));
                    }

                    if (result == VK_SUCCESS)
                    {
                        BUSTER_VULKAN_LOAD_INSTANCE_FUNCTION(rendering_handle.instance, vkDestroyInstance);
                        BUSTER_VULKAN_LOAD_INSTANCE_FUNCTION(rendering_handle.instance, vkGetDeviceProcAddr);
                        BUSTER_VULKAN_LOAD_INSTANCE_FUNCTION(rendering_handle.instance, vkEnumeratePhysicalDevices);
                        BUSTER_VULKAN_LOAD_INSTANCE_FUNCTION(rendering_handle.instance, vkGetPhysicalDeviceMemoryProperties);
                        BUSTER_VULKAN_LOAD_INSTANCE_FUNCTION(rendering_handle.instance, vkGetPhysicalDeviceProperties);
                        BUSTER_VULKAN_LOAD_INSTANCE_FUNCTION(rendering_handle.instance, vkGetPhysicalDeviceQueueFamilyProperties);
                        BUSTER_VULKAN_LOAD_INSTANCE_FUNCTION(rendering_handle.instance, vkEnumerateDeviceExtensionProperties);
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
#ifdef VK_USE_PLATFORM_ANDROID_KHR
                        BUSTER_VULKAN_LOAD_INSTANCE_FUNCTION(rendering_handle.instance, vkCreateAndroidSurfaceKHR);
#endif
                        BUSTER_VULKAN_LOAD_INSTANCE_FUNCTION(rendering_handle.instance, vkDestroySurfaceKHR);

                        // TODO: make physical device choosing logic more robust
                        VkPhysicalDevice physical_devices[256];
                        u32 physical_device_count = 0;
                        result = vkEnumeratePhysicalDevices(rendering_handle.instance, &physical_device_count, 0);
                        string_print(S8("Vulkan physical device enumeration: first={u64:x}, count={u32}\n"), (u64)(u32)result, physical_device_count);

                        if (result == VK_SUCCESS)
                        {
                            if (physical_device_count && physical_device_count <= BUSTER_ARRAY_LENGTH(physical_devices))
                            {
                                result = vkEnumeratePhysicalDevices(rendering_handle.instance, &physical_device_count, physical_devices);
                                string_print(S8("Vulkan physical device enumeration: second={u64:x}, count={u32}\n"), (u64)(u32)result, physical_device_count);
                                if (result == VK_SUCCESS)
                                {
                                    rendering_handle.physical_device = physical_devices[0];
                                }
                                else
                                {
                                    BUSTER_TODO();
                                }
                            }
                        }
                    }

                    string_print(S8("Vulkan physical device selected: handle={u64:x}\n"), rendering_handle.physical_device);

                    if (rendering_handle.physical_device)
                    {
                        vkGetPhysicalDeviceMemoryProperties(rendering_handle.physical_device, &rendering_handle.device_memory_properties);

                        {
                            u32 present_queue_family_index;
                            VkPhysicalDeviceProperties properties;
                            vkGetPhysicalDeviceProperties(rendering_handle.physical_device, &properties);

                            u32 queue_count;
                            vkGetPhysicalDeviceQueueFamilyProperties(rendering_handle.physical_device, &queue_count, 0);
                            string_print(S8("Vulkan physical device properties: name={S8}, queue_families={u32}\n"), string_from_pointer(properties.deviceName),
                                         queue_count);

                            VkQueueFamilyProperties queue_family_property_buffer[64];
                            if (queue_count <= BUSTER_ARRAY_LENGTH(queue_family_property_buffer))
                            {
                                vkGetPhysicalDeviceQueueFamilyProperties(rendering_handle.physical_device, &queue_count, queue_family_property_buffer);

                                for (rendering_handle.graphics_queue_family_index = 0; rendering_handle.graphics_queue_family_index < queue_count;
                                     rendering_handle.graphics_queue_family_index += 1)
                                {
                                    VkQueueFamilyProperties* queue_family_properties =
                                        &queue_family_property_buffer[rendering_handle.graphics_queue_family_index];
                                    if (queue_family_properties->queueFlags & VK_QUEUE_GRAPHICS_BIT)
                                    {
                                        break;
                                    }
                                }

                                if (rendering_handle.graphics_queue_family_index != queue_count)
                                {
                                    present_queue_family_index = 0;
                                    string_print(S8("Vulkan queue selection: graphics={u32}, present={u32}, queue_flags={u32}\n"),
                                                 rendering_handle.graphics_queue_family_index, present_queue_family_index,
                                                 queue_family_property_buffer[rendering_handle.graphics_queue_family_index].queueFlags);

                                    // for (present_queue_family_index = 0; present_queue_family_index < queue_count; present_queue_family_index += 1)
                                    // {
                                    //     VkBool32 support;
                                    //     VkResult success = vkGetPhysicalDeviceSurfaceSupportKHR(physical_device, present_queue_family_index, surface,
                                    //     &support); if (support)
                                    //     {
                                    //         break;
                                    //     }
                                    // }

                                    if (present_queue_family_index != queue_count && present_queue_family_index == rendering_handle.graphics_queue_family_index)
                                    {
                                        f32 queue_priorities[] = {1.0f};
                                        VkDeviceQueueCreateInfo queue_create_infos[] = {
                                            {
                                                .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
                                                .queueFamilyIndex = rendering_handle.graphics_queue_family_index,
                                                .queueCount = BUSTER_ARRAY_LENGTH(queue_priorities),
                                                .pQueuePriorities = queue_priorities,
                                            },
                                        };

                                        VkExtensionProperties device_extension_properties[512];
                                        u32 device_extension_property_count = 0;
                                        bool missing_required_device_extension = false;
                                        bool portability_subset_enabled = false;
                                        const char* extensions[8];
                                        u32 extension_count = 0;

                                        if (vkEnumerateDeviceExtensionProperties)
                                        {
                                            result =
                                                vkEnumerateDeviceExtensionProperties(rendering_handle.physical_device, 0, &device_extension_property_count, 0);
                                            if (result == VK_SUCCESS && device_extension_property_count <= BUSTER_ARRAY_LENGTH(device_extension_properties))
                                            {
                                                result = vkEnumerateDeviceExtensionProperties(rendering_handle.physical_device, 0,
                                                                                              &device_extension_property_count, device_extension_properties);
                                            }
                                            else if (result == VK_SUCCESS)
                                            {
                                                result = VK_ERROR_EXTENSION_NOT_PRESENT;
                                            }
                                        }
                                        else
                                        {
                                            result = VK_ERROR_EXTENSION_NOT_PRESENT;
                                        }

                                        if (result == VK_SUCCESS)
                                        {
                                            if (vulkan_device_extension_supported(device_extension_properties, device_extension_property_count,
                                                                                  VK_KHR_SWAPCHAIN_EXTENSION_NAME))
                                            {
                                                extensions[extension_count++] = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
                                            }
                                            else
                                            {
                                                missing_required_device_extension = true;
                                                string_print(S8("Vulkan required device extension unavailable: {S8}\n"), S8(VK_KHR_SWAPCHAIN_EXTENSION_NAME));
                                            }

#ifdef VK_USE_PLATFORM_METAL_EXT
                                            if (vulkan_device_extension_supported(device_extension_properties, device_extension_property_count,
                                                                                  "VK_KHR_portability_subset"))
                                            {
                                                extensions[extension_count++] = "VK_KHR_portability_subset";
                                                portability_subset_enabled = true;
                                            }
#endif
                                        }

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
                                            .features = {0},
                                            .pNext = &features12,
                                        };

                                        VkDeviceCreateInfo ci = {
                                            .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
                                            .ppEnabledExtensionNames = extensions,
                                            .enabledExtensionCount = extension_count,
                                            .pQueueCreateInfos = queue_create_infos,
                                            .queueCreateInfoCount = BUSTER_ARRAY_LENGTH(queue_create_infos),
                                            .pNext = &features,
                                        };

                                        if (missing_required_device_extension)
                                        {
                                            result = VK_ERROR_EXTENSION_NOT_PRESENT;
                                        }

                                        VkDevice device = 0;
                                        if (result == VK_SUCCESS)
                                        {
                                            result = vkCreateDevice(rendering_handle.physical_device, &ci, allocator, &device);
                                        }
                                        string_print(S8("Vulkan logical device creation: vkCreateDevice={u64:x}, extensions={u32}, portability_subset={u32}\n"),
                                                     (u64)(u32)result, extension_count, (u32)portability_subset_enabled);

                                        if (result == VK_SUCCESS)
                                        {
                                            rendering_handle.device = device;
                                        }
                                    }
                                }
                            }
                        }
                    }

                    string_print(S8("Vulkan device selected: device={u64:x}\n"), rendering_handle.device);

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
                        string_print(S8("Vulkan graphics queue: family={u32}, queue={u64:x}\n"), rendering_handle.graphics_queue_family_index,
                                     (u64)rendering_handle.graphics_queue);

                        rendering_handle.immediate.device = rendering_handle.device;
                        rendering_handle.immediate.queue = rendering_handle.graphics_queue;

                        VkCommandPoolCreateInfo create_info = {
                            .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
                            .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
                            .queueFamilyIndex = rendering_handle.graphics_queue_family_index,
                        };
                        result = vkCreateCommandPool(rendering_handle.device, &create_info, allocator, &rendering_handle.immediate.command_pool);
                        string_print(S8("Vulkan immediate command pool creation: vkCreateCommandPool={u64:x}, command_pool={u64:x}\n"), (u64)(u32)result,
                                     (u64)rendering_handle.immediate.command_pool);

                        if (result == VK_SUCCESS)
                        {
                            VkCommandBufferAllocateInfo command_buffer_allocate_info = {
                                .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
                                .commandPool = rendering_handle.immediate.command_pool,
                                .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
                                .commandBufferCount = 1,
                            };

                            result =
                                vkAllocateCommandBuffers(rendering_handle.device, &command_buffer_allocate_info, &rendering_handle.immediate.command_buffer);
                            string_print(S8("Vulkan immediate command buffer allocation: vkAllocateCommandBuffers={u64:x}, command_buffer={u64:x}\n"),
                                         (u64)(u32)result, (u64)rendering_handle.immediate.command_buffer);

                            if (result == VK_SUCCESS)
                            {
                                VkFenceCreateInfo fence_create_info = {
                                    .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
                                    .flags = VK_FENCE_CREATE_SIGNALED_BIT,
                                };

                                result = vkCreateFence(rendering_handle.device, &fence_create_info, allocator, &rendering_handle.immediate.fence);
                                string_print(S8("Vulkan immediate fence creation: vkCreateFence={u64:x}, fence={u64:x}\n"), (u64)(u32)result,
                                             (u64)rendering_handle.immediate.fence);
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
                                .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
                                .addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT,
                                .addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT,
                                .addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT,
                                .mipLodBias = 0,
                                .anisotropyEnable = 0,
                                .maxAnisotropy = 0,
                                .compareEnable = 0,
                                .compareOp = VK_COMPARE_OP_NEVER,
                                .minLod = 0,
                                .maxLod = 0,
                                .borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK,
                                .unnormalizedCoordinates = 0,
                            };

                            VkSampler sampler = 0;
                            result = vkCreateSampler(rendering_handle.device, &sampler_create_info, allocator, &sampler);
                            string_print(S8("Vulkan sampler creation: vkCreateSampler={u64:x}, sampler={u64:x}\n"), (u64)(u32)result, (u64)sampler);

                            if (result == VK_SUCCESS)
                            {
                                rendering_handle.sampler = sampler;
                            }
                        }
                    }

                    if (result != VK_SUCCESS || !rendering_handle.device)
                    {
                        string_print(S8("Vulkan rendering initialization failed before pipeline setup: result={u64:x}, device={u64:x}\n"), (u64)(u32)result,
                                     (u64)rendering_handle.device);
                        return rendering;
                    }

                    String8 shader_binaries[] = {
#if BUSTER_ANDROID
                        // Shaders are shipped inside the APK and read via AAssetManager.
                        S8("shaders/rect.vert.spv"),
                        S8("shaders/rect.frag.spv"),
#else
                        S8(BUSTER_SHADER_RECT_VERT_SPV),
                        S8(BUSTER_SHADER_RECT_FRAG_SPV),
#endif
                    };

                    PushConstantRange rect_push_constant_ranges[] = {
                        (PushConstantRange){
                            .offset = 0,
#if BUSTER_ANDROID
                            .size = sizeof(DrawConstants),
#else
                            .size = sizeof(GPUDrawPushConstants),
#endif
                            .stage = SHADER_STAGE_VERTEX,
                        },
                    };

                    DescriptorSetLayoutBinding rect_descriptor_set_layout_bindings[] = {
                        {
                            .binding = 0,
                            .type = DESCRIPTOR_TYPE_IMAGE_PLUS_SAMPLER,
                            .stage = SHADER_STAGE_FRAGMENT,
                            .count = (u8)RECT_TEXTURE_SLOT_COUNT,
                        },
#if BUSTER_ANDROID
                        {
                            .binding = 1,
                            .type = DESCRIPTOR_TYPE_STORAGE_BUFFER,
                            .stage = SHADER_STAGE_VERTEX,
                            .count = 1,
                        },
#endif
                    };

                    DescriptorSetLayoutCreate rect_descriptor_set_layouts[] = {
                        (DescriptorSetLayoutCreate){
                            .bindings = BUSTER_ARRAY_TO_SLICE(rect_descriptor_set_layout_bindings),
                        },
                    };

                    PipelineLayoutCreate pipeline_layouts[] = {
                        (PipelineLayoutCreate){
                            .push_constant_ranges = BUSTER_ARRAY_TO_SLICE(rect_push_constant_ranges),
                            .descriptor_set_layouts = BUSTER_ARRAY_TO_SLICE(rect_descriptor_set_layouts),
                        },
                    };

                    u16 rect_pipeline_shader_source_indices[] = {0, 1};
                    PipelineCreate pipeline_create[] = {
                        (PipelineCreate){
                            .shader_source_indices = BUSTER_ARRAY_TO_SLICE(rect_pipeline_shader_source_indices),
                            .layout_index = 0,
                        },
                    };
                    GraphicsPipelinesCreate create_data = {
                        .layouts = BUSTER_ARRAY_TO_SLICE(pipeline_layouts),
                        .pipelines = BUSTER_ARRAY_TO_SLICE(pipeline_create),
                        .shader_binaries = BUSTER_ARRAY_TO_SLICE(shader_binaries),
                    };
                    u64 graphics_pipeline_count = create_data.pipelines.length;
                    BUSTER_CHECK(graphics_pipeline_count);
                    u64 pipeline_layout_count = create_data.layouts.length;
                    BUSTER_CHECK(pipeline_layout_count);
                    BUSTER_CHECK(pipeline_layout_count <= graphics_pipeline_count);
                    u64 shader_count = create_data.shader_binaries.length;

                    VkPipeline pipeline_handles[BUSTER_PIPELINE_COUNT];
                    VkPipelineShaderStageCreateInfo shader_create_infos[MAX_SHADER_MODULE_COUNT_PER_PIPELINE];
                    VkGraphicsPipelineCreateInfo graphics_pipeline_create_infos[(u64)BUSTER_PIPELINE_COUNT];

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
                        .front = {0},
                        .back = {0},
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
                        .blendConstants = {0},
                    };

                    VkDynamicState states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};

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
                        .depthAttachmentFormat = VK_FORMAT_UNDEFINED,
                        .stencilAttachmentFormat = VK_FORMAT_UNDEFINED,
                    };

                    VkShaderModule* shader_modules = arena_allocate(arena, VkShaderModule, shader_count);

                    for (u64 i = 0; i < shader_count; i += 1)
                    {
                        String8 shader_binary_path = create_data.shader_binaries.pointer[i];

                        ByteSlice binary = file_read(arena, shader_binary_path, (FileReadOptions){0});

                        VkShaderModuleCreateInfo create_info = {
                            .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
                            .codeSize = binary.length,
                            .pCode = (u32*)binary.pointer,
                        };

                        VkShaderModule shader_module;
                        result = vkCreateShaderModule(rendering_handle.device, &create_info, allocator, &shader_module);

                        if (result == VK_SUCCESS)
                        {
                            shader_modules[i] = shader_module;
                            string_print(S8("Vulkan shader module creation {u32}: vkCreateShaderModule={u64:x}, module={u64:x}, bytes={u64}\n"), (u32)i,
                                         (u64)(u32)result, (u64)shader_module, binary.length);
                        }
                        else
                        {
                            string_print(S8("Vulkan shader module creation failed {u32}: vkCreateShaderModule={u64:x}, bytes={u64}\n"), (u32)i,
                                         (u64)(u32)result, binary.length);
                            return rendering;
                        }
                    }

                    for (u64 pipeline_index = 0; pipeline_index < pipeline_layout_count; pipeline_index += 1)
                    {
                        PipelineLayoutCreate create = create_data.layouts.pointer[pipeline_index];
                        u64 descriptor_set_layout_count = create.descriptor_set_layouts.length;
                        u64 push_constant_range_count = create.push_constant_ranges.length;
                        Pipeline* pipeline = &rendering_handle.pipelines[pipeline_index];
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
                            DescriptorSetLayoutCreate set_layout_create = create.descriptor_set_layouts.pointer[descriptor_set_layout_index];
                            u64 binding_count = set_layout_create.bindings.length;
                            DescriptorSetLayoutBindings* descriptor_set_layout_bindings =
                                &pipeline->descriptor_set_layout_bindings[descriptor_set_layout_index];
                            descriptor_set_layout_bindings->count = (u32)binding_count;

                            for (u64 binding_index = 0; binding_index < binding_count; binding_index += 1)
                            {
                                DescriptorSetLayoutBinding binding_descriptor = set_layout_create.bindings.pointer[binding_index];

                                VkDescriptorType descriptor_type = vulkan_descriptor_type(binding_descriptor.type);

                                VkShaderStageFlags shader_stage = vulkan_shader_stage(binding_descriptor.stage);

                                descriptor_set_layout_bindings->buffer[binding_index] = (VkDescriptorSetLayoutBinding){
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

                            VkDescriptorSetLayout layout;
                            result = vkCreateDescriptorSetLayout(rendering_handle.device, &create_info, allocator, &layout);
                            if (result == VK_SUCCESS)
                            {
                                pipeline->descriptor_set_layouts[descriptor_set_layout_index] = layout;
                                string_print(S8("Vulkan descriptor set layout creation {u32}:{u32}: vkCreateDescriptorSetLayout={u64:x}, layout={u64:x}, "
                                                "bindings={u32}\n"),
                                             (u32)pipeline_index, (u32)descriptor_set_layout_index, (u64)(u32)result, (u64)layout, (u32)binding_count);
                            }
                            else
                            {
                                string_print(
                                    S8("Vulkan descriptor set layout creation failed {u32}:{u32}: vkCreateDescriptorSetLayout={u64:x}, bindings={u32}\n"),
                                    (u32)pipeline_index, (u32)descriptor_set_layout_index, (u64)(u32)result, (u32)binding_count);
                                return rendering;
                            }
                        }

                        if (push_constant_range_count > MAX_PUSH_CONSTANT_RANGE_COUNT)
                        {
                            os_fail();
                        }

                        for (u64 push_constant_index = 0; push_constant_index < push_constant_range_count; push_constant_index += 1)
                        {
                            PushConstantRange push_constant_descriptor = create.push_constant_ranges.pointer[push_constant_index];
                            pipeline->push_constant_ranges[push_constant_index] = (VkPushConstantRange){
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

                        VkPipelineLayout layout;
                        result = vkCreatePipelineLayout(rendering_handle.device, &create_info, allocator, &layout);

                        if (result == VK_SUCCESS)
                        {
                            pipeline->layout = layout;
                            string_print(S8("Vulkan pipeline layout creation {u32}: vkCreatePipelineLayout={u64:x}, layout={u64:x}, descriptor_sets={u32}, "
                                            "push_constants={u32}\n"),
                                         (u32)pipeline_index, (u64)(u32)result, (u64)layout, (u32)descriptor_set_layout_count, (u32)push_constant_range_count);
                        }
                        else
                        {
                            string_print(S8("Vulkan pipeline layout creation failed {u32}: vkCreatePipelineLayout={u64:x}, descriptor_sets={u32}, "
                                            "push_constants={u32}\n"),
                                         (u32)pipeline_index, (u64)(u32)result, (u32)descriptor_set_layout_count, (u32)push_constant_range_count);
                            return rendering;
                        }
                    }

                    for (u64 pipeline_i = 0; pipeline_i < graphics_pipeline_count; pipeline_i += 1)
                    {
                        PipelineCreate create = create_data.pipelines.pointer[pipeline_i];
                        Pipeline* pipeline = &rendering_handle.pipelines[pipeline_i];
                        u64 pipeline_shader_count = create.shader_source_indices.length;
                        if (pipeline_shader_count > MAX_SHADER_MODULE_COUNT_PER_PIPELINE)
                        {
                            os_fail();
                        }

                        for (u64 shader_i = 0; shader_i < pipeline_shader_count; shader_i += 1)
                        {
                            u16 shader_index = create.shader_source_indices.pointer[shader_i];
                            String8 shader_source_path = create_data.shader_binaries.pointer[shader_index];

                            shader_create_infos[shader_i] = (VkPipelineShaderStageCreateInfo){
                                .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                                .pNext = 0,
                                .flags = 0,
                                .stage = vulkan_shader_stage_from_path(shader_source_path),
                                .module = shader_modules[shader_i],
                                .pName = "main",
                                .pSpecializationInfo = 0,
                            };
                        }

                        graphics_pipeline_create_infos[pipeline_i] = (VkGraphicsPipelineCreateInfo){
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
                    result = vkCreateGraphicsPipelines(rendering_handle.device, pipeline_cache, (u32)graphics_pipeline_count, graphics_pipeline_create_infos,
                                                       allocator, pipeline_handles);
                    string_print(S8("Vulkan graphics pipeline creation: vkCreateGraphicsPipelines={u64:x}, pipeline_count={u32}\n"), (u64)(u32)result,
                                 (u32)graphics_pipeline_count);

                    if (result == VK_SUCCESS)
                    {
                        rendering = &rendering_handle;

                        for (u32 i = 0; i < graphics_pipeline_count; i += 1)
                        {
                            rendering_handle.pipelines[i].handle = pipeline_handles[i];
                            string_print(S8("Vulkan graphics pipeline {u32}: handle={u64:x}\n"), i, (u64)pipeline_handles[i]);
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

    string_print(S8("Vulkan rendering initialization {S8}: rendering={u64:x}, instance={u64:x}, physical_device={u64:x}, device={u64:x}, queue={u64:x}\n"),
                 rendering ? S8("succeeded") : S8("failed"), (u64)rendering, (u64)rendering_handle.instance, (u64)rendering_handle.physical_device,
                 (u64)rendering_handle.device, (u64)rendering_handle.graphics_queue);

    return rendering;
}

BUSTER_GLOBAL_LOCAL void swapchain_recreate(RenderingHandle* rendering, RenderingWindowHandle* window)
{
    VkSurfaceCapabilitiesKHR surface_capabilities;
    VkResult capabilities_result = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(rendering->physical_device, window->surface, &surface_capabilities);
    string_print(S8("Vulkan swapchain recreate: surface={u64:x}, old_swapchain={u64:x}, vkGetPhysicalDeviceSurfaceCapabilitiesKHR={u64:x}\n"),
                 (u64)window->surface, (u64)window->swapchain, (u64)(u32)capabilities_result);

    if (capabilities_result == VK_SUCCESS)
    {
        VkSwapchainKHR old_swapchain = window->swapchain;
        VkImageView old_swapchain_image_views[MAX_SWAPCHAIN_IMAGE_COUNT] = {0};

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

        u32 queue_family_indices[] = {rendering->graphics_queue_family_index};
        VkImageUsageFlags swapchain_image_usage_flags = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
#if BUSTER_ANDROID
        window->swapchain_image_format = VK_FORMAT_R8G8B8A8_UNORM;
#else
        window->swapchain_image_format = VK_FORMAT_B8G8R8A8_UNORM;
#endif
        window->last_width = window->width;
        window->last_height = window->height;
        window->width = surface_capabilities.currentExtent.width;
        window->height = surface_capabilities.currentExtent.height;
        string_print(S8("Vulkan surface capabilities: current={u32}x{u32}, min_images={u32}, max_images={u32}, current_transform={u32}\n"), window->width,
                     window->height, surface_capabilities.minImageCount, surface_capabilities.maxImageCount, surface_capabilities.currentTransform);

        VkPresentModeKHR preferred_present_mode = VK_PRESENT_MODE_IMMEDIATE_KHR;
        VkPresentModeKHR present_modes[16];
        u32 present_mode_count = BUSTER_ARRAY_LENGTH(present_modes);

        VkResult present_modes_result =
            vkGetPhysicalDeviceSurfacePresentModesKHR(rendering->physical_device, window->surface, &present_mode_count, present_modes);
        string_print(S8("Vulkan surface present modes: vkGetPhysicalDeviceSurfacePresentModesKHR={u64:x}, count={u32}\n"), (u64)(u32)present_modes_result,
                     present_mode_count);
        if (present_modes_result == VK_SUCCESS)
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

            VkResult create_swapchain_result = vkCreateSwapchainKHR(rendering->device, &swapchain_create_info, rendering->allocator, &window->swapchain);
            string_print(
                S8("Vulkan swapchain creation: vkCreateSwapchainKHR={u64:x}, swapchain={u64:x}, extent={u32}x{u32}, min_images={u32}, present_mode={u32}\n"),
                (u64)(u32)create_swapchain_result, (u64)window->swapchain, swapchain_create_info.imageExtent.width, swapchain_create_info.imageExtent.height,
                swapchain_create_info.minImageCount, swapchain_create_info.presentMode);
            if (create_swapchain_result == VK_SUCCESS)
            {
                BUSTER_CHECK(window->swapchain != old_swapchain);

                if (old_swapchain)
                {
                    for (u32 i = 0; i < window->swapchain_image_count; i += 1)
                    {
                        vkDestroyImageView(rendering->device, old_swapchain_image_views[i], rendering->allocator);
                    }

                    vkDestroySwapchainKHR(rendering->device, old_swapchain, rendering->allocator);

                    destroy_image(rendering->device, rendering->allocator, window->render_image.view, window->render_image.handle,
                                  window->render_image.memory.handle);
                }

                VkResult get_swapchain_image_count_result = vkGetSwapchainImagesKHR(rendering->device, window->swapchain, &window->swapchain_image_count, 0);
                string_print(S8("Vulkan swapchain image count: vkGetSwapchainImagesKHR={u64:x}, count={u32}\n"), (u64)(u32)get_swapchain_image_count_result,
                             window->swapchain_image_count);
                if (get_swapchain_image_count_result == VK_SUCCESS)
                {
                    if (window->swapchain_image_count == 0)
                    {
                        os_fail();
                    }

                    if (window->swapchain_image_count > BUSTER_ARRAY_LENGTH(window->swapchain_images))
                    {
                        os_fail();
                    }

                    VkResult get_swapchain_images_result =
                        vkGetSwapchainImagesKHR(rendering->device, window->swapchain, &window->swapchain_image_count, window->swapchain_images);
                    string_print(S8("Vulkan swapchain images: vkGetSwapchainImagesKHR={u64:x}, count={u32}\n"), (u64)(u32)get_swapchain_images_result,
                                 window->swapchain_image_count);
                    if (get_swapchain_images_result == VK_SUCCESS)
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
                                .components =
                                    {
                                        .r = VK_COMPONENT_SWIZZLE_IDENTITY,
                                        .g = VK_COMPONENT_SWIZZLE_IDENTITY,
                                        .b = VK_COMPONENT_SWIZZLE_IDENTITY,
                                        .a = VK_COMPONENT_SWIZZLE_IDENTITY,
                                    },
                                .subresourceRange =
                                    {
                                        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                                        .baseMipLevel = 0,
                                        .levelCount = 1,
                                        .baseArrayLayer = 0,
                                        .layerCount = 1,
                                    },
                            };

                            VkResult image_view_creation =
                                vkCreateImageView(rendering->device, &create_info, rendering->allocator, &window->swapchain_image_views[i]);
                            string_print(S8("Vulkan swapchain image view {u32}: vkCreateImageView={u64:x}, image={u64:x}, view={u64:x}\n"), i,
                                         (u64)(u32)image_view_creation, (u64)window->swapchain_images[i], (u64)window->swapchain_image_views[i]);
                            if (image_view_creation != VK_SUCCESS)
                            {
                                os_fail();
                            }
                        }
                    }
                }

                window->render_image =
                    vk_image_create(rendering->device, rendering->allocator, &rendering->device_memory_properties,
                                    (VulkanImageCreate){
                                        .width = surface_capabilities.currentExtent.width,
                                        .height = surface_capabilities.currentExtent.height,
                                        .mip_levels = 1,
                                        .format = window->swapchain_image_format,
                                        .usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
                                    });
                string_print(S8("Vulkan render image creation: image={u64:x}, view={u64:x}, memory={u64:x}, extent={u32}x{u32}\n"),
                             (u64)window->render_image.handle, (u64)window->render_image.view, (u64)window->render_image.memory.handle, window->width,
                             window->height);
            }
        }
    }
}

RenderingWindowHandle* rendering_window_initialize(Arena* arena, WmHandle* windowing, RenderingHandle* rendering, WmWindowHandle* window)
{
    BUSTER_UNUSED(rendering);

    RenderingWindowHandle* result = arena_allocate(arena, RenderingWindowHandle, 1);
    WmNativeSurface native_surface = wm_window_get_native_surface(windowing, window);
    BUSTER_UNUSED(native_surface);
#if defined(VK_USE_PLATFORM_XCB_KHR)
    BUSTER_CHECK(native_surface.kind == WM_NATIVE_SURFACE_XCB);
    xcb_connection_t* native_connection = (xcb_connection_t*)native_surface.display;
    xcb_window_t native_window = (xcb_window_t)(u64)native_surface.window;
    string_print(S8("Vulkan render window initialization: platform=xcb, native_window={u64:x}, connection={u64:x}\n"), (u64)native_window,
                 (u64)native_connection);
    VkXcbSurfaceCreateInfoKHR surface_create_info = {
        .sType = VK_STRUCTURE_TYPE_XCB_SURFACE_CREATE_INFO_KHR,
        .pNext = 0,
        .flags = 0,
        .connection = native_connection,
        .window = native_window,
    };

    VkResult create_surface_result = vkCreateXcbSurfaceKHR(rendering->instance, &surface_create_info, rendering->allocator, &result->surface);
    string_print(S8("Vulkan surface creation: vkCreateXcbSurfaceKHR={u64:x}, surface={u64:x}\n"), (u64)(u32)create_surface_result, (u64)result->surface);
    if (create_surface_result != VK_SUCCESS)
    {
        os_fail();
    }
#elif defined(VK_USE_PLATFORM_WIN32_KHR)
    BUSTER_CHECK(native_surface.kind == WM_NATIVE_SURFACE_WIN32);
    HINSTANCE native_instance = (HINSTANCE)native_surface.display;
    HWND native_window = (HWND)native_surface.window;
    string_print(S8("Vulkan render window initialization: platform=win32, hwnd={u64:x}, hinstance={u64:x}\n"), (u64)native_window, (u64)native_instance);
    VkWin32SurfaceCreateInfoKHR surface_create_info = {
        .sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR,
        .pNext = 0,
        .flags = 0,
        .hinstance = native_instance,
        .hwnd = native_window,
    };

    VkResult create_surface_result = vkCreateWin32SurfaceKHR(rendering->instance, &surface_create_info, rendering->allocator, &result->surface);
    string_print(S8("Vulkan surface creation: vkCreateWin32SurfaceKHR={u64:x}, surface={u64:x}\n"), (u64)(u32)create_surface_result, (u64)result->surface);
    if (create_surface_result != VK_SUCCESS)
    {
        os_fail();
    }
#elif defined(VK_USE_PLATFORM_ANDROID_KHR)
    BUSTER_CHECK(native_surface.kind == WM_NATIVE_SURFACE_ANDROID);
    struct ANativeWindow* native_window = (struct ANativeWindow*)native_surface.window;
    string_print(S8("Vulkan render window initialization: platform=android, native_window={u64:x}\n"), (u64)native_window);
    VkAndroidSurfaceCreateInfoKHR surface_create_info = {
        .sType = VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR,
        .pNext = 0,
        .flags = 0,
        .window = native_window,
    };

    VkResult create_surface_result = vkCreateAndroidSurfaceKHR(rendering->instance, &surface_create_info, rendering->allocator, &result->surface);
    string_print(S8("Vulkan surface creation: vkCreateAndroidSurfaceKHR={u64:x}, surface={u64:x}\n"), (u64)(u32)create_surface_result, (u64)result->surface);
    if (create_surface_result != VK_SUCCESS)
    {
        os_fail();
    }
#endif
    string_print(S8("Vulkan surface ready: surface={u64:x}\n"), result->surface);

    result->frame_index = 0;
#if BUSTER_ANDROID
    // Single frame in flight so the per-frame vertex storage buffer can be bound
    // into the shared rect descriptor set each frame without an in-flight hazard.
    result->frame_count = 1;
#else
    result->frame_count = 2;
#endif

    swapchain_recreate(rendering, result);
    string_print(S8("Vulkan swapchain ready: swapchain={u64:x}, extent={u32}x{u32}, images={u32}\n"), result->swapchain, result->width, result->height,
                 result->swapchain_image_count);

    for (u64 frame_index = 0; frame_index < result->frame_count; frame_index += 1)
    {
        WindowFrame* frame = &result->frames[frame_index];

        for (BusterPipeline pipeline_index = 0; pipeline_index < BUSTER_PIPELINE_COUNT; pipeline_index += 1)
        {
            FramePipelineInstantiation* pipeline = &frame->pipeline_instantiations[pipeline_index];
            pipeline->vertex_buffer.cpu = arena_create((ArenaCreation){0});
            pipeline->index_buffer.cpu = arena_create((ArenaCreation){0});
            pipeline->vertex_buffer.gpu.type = BUFFER_TYPE_VERTEX;
            pipeline->index_buffer.gpu.type = BUFFER_TYPE_INDEX;
            pipeline->transient_buffer.type = BUFFER_TYPE_STAGING;
        }
    }

    for (BusterPipeline pipeline_index = 0; pipeline_index < BUSTER_PIPELINE_COUNT; pipeline_index += 1)
    {
        Pipeline* pipeline_descriptor = &rendering->pipelines[pipeline_index];
        BUSTER_CHECK(pipeline_descriptor->descriptor_set_count);
        PipelineInstantiation* pipeline_instantiation = &result->pipeline_instantiations[pipeline_index];

        u16 descriptor_type_counter[(u64)DESCRIPTOR_TYPE_COUNT] = {0};

        for (u64 descriptor_index = 0; descriptor_index < pipeline_descriptor->descriptor_set_count; descriptor_index += 1)
        {
            DescriptorSetLayoutBindings* descriptor_set_layout_bindings = &pipeline_descriptor->descriptor_set_layout_bindings[descriptor_index];

            for (u64 binding_index = 0; binding_index < descriptor_set_layout_bindings->count; binding_index += 1)
            {
                VkDescriptorSetLayoutBinding* binding_descriptor = &descriptor_set_layout_bindings->buffer[binding_index];
                DescriptorType descriptor_type = descriptor_type_from_vulkan(binding_descriptor->descriptorType);
                u16* counter_ptr = &descriptor_type_counter[(u64)descriptor_type];
                u16 old_counter = *counter_ptr;
                *counter_ptr = old_counter + (u16)binding_descriptor->descriptorCount;
            }
        }

        VkDescriptorPoolSize pool_sizes[(u64)DESCRIPTOR_TYPE_COUNT];
        u32 pool_size_count = 0;

        for (DescriptorType i = 0; i < DESCRIPTOR_TYPE_COUNT; i += 1)
        {
            u16 count = descriptor_type_counter[i];
            if (count != 0)
            {
                VkDescriptorPoolSize* pool_size = &pool_sizes[pool_size_count];
                pool_size_count += 1;

                *pool_size = (VkDescriptorPoolSize){
                    .type = vulkan_descriptor_type((DescriptorType)i),
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

            VkResult descriptor_set_allocation = vkAllocateDescriptorSets(rendering->device, &allocate_info, pipeline_instantiation->descriptor_sets);
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
            string_print(S8("Vulkan frame resources {u32}: command_pool={u64:x}, command_buffer={u64:x}, render_fence={u64:x}, swapchain_semaphore={u64:x}\n"),
                         frame_i, (u64)frame->command_pool, (u64)frame->command_buffer, (u64)frame->render_fence, (u64)frame->swapchain_semaphore);
        }
    }

    for (u32 image_i = 0; image_i < result->swapchain_image_count; image_i += 1)
    {
        if (vkCreateSemaphore(rendering->device, &semaphore_create_info, rendering->allocator, &result->render_semaphores[image_i]) != VK_SUCCESS)
        {
            os_fail();
        }
        string_print(S8("Vulkan render semaphore {u32}: semaphore={u64:x}\n"), image_i, (u64)result->render_semaphores[image_i]);
    }

    string_print(S8("Vulkan render window initialization succeeded: surface={u64:x}, swapchain={u64:x}, frame_count={u32}, image_count={u32}\n"),
                 (u64)result->surface, (u64)result->swapchain, result->frame_count, result->swapchain_image_count);

    return result;
}

#if BUSTER_ANDROID
// On resume the old ANativeWindow (and thus the VkSurfaceKHR + swapchain) is
// dead. Rebuild just the surface and swapchain for the new native window; the
// RenderingWindowHandle, its descriptor sets, textures and frames are kept.
void rendering_window_surface_recreate(RenderingHandle* rendering, WmHandle* windowing, RenderingWindowHandle* window, WmWindowHandle* wm_window)
{
    vkDeviceWaitIdle(rendering->device);

    for (u32 i = 0; i < window->swapchain_image_count; i += 1)
    {
        if (window->swapchain_image_views[i])
        {
            vkDestroyImageView(rendering->device, window->swapchain_image_views[i], rendering->allocator);
            window->swapchain_image_views[i] = 0;
        }
    }
    if (window->render_image.handle)
    {
        destroy_image(rendering->device, rendering->allocator, window->render_image.view, window->render_image.handle, window->render_image.memory.handle);
        window->render_image = (VulkanImage){0};
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

    WmNativeSurface native_surface = wm_window_get_native_surface(windowing, wm_window);
    BUSTER_CHECK(native_surface.kind == WM_NATIVE_SURFACE_ANDROID);
    struct ANativeWindow* native_window = (struct ANativeWindow*)native_surface.window;
    VkAndroidSurfaceCreateInfoKHR surface_create_info = {
        .sType = VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR,
        .pNext = 0,
        .flags = 0,
        .window = native_window,
    };
    VkResult surface_result = vkCreateAndroidSurfaceKHR(rendering->instance, &surface_create_info, rendering->allocator, &window->surface);
    string_print(S8("Vulkan surface recreate: vkCreateAndroidSurfaceKHR={u64:x}, surface={u64:x}\n"), (u64)(u32)surface_result, (u64)window->surface);

    swapchain_recreate(rendering, window);
}
#endif

void rendering_window_queue_pipeline_texture_update(RenderingHandle* rendering, RenderingWindowHandle* window, BusterPipeline pipeline_index, u32 resource_slot,
                                                    TextureIndex texture_index)
{
    PipelineInstantiation* pipeline_instantiation = &window->pipeline_instantiations[(u64)pipeline_index];
    VkDescriptorImageInfo* descriptor_image = &pipeline_instantiation->texture_descriptors[resource_slot];
    VulkanTexture* texture = &rendering->textures[texture_index.value];
    *descriptor_image = (VkDescriptorImageInfo){
        .sampler = texture->sampler,
        .imageView = texture->image.view,
        .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, // TODO: specify
    };
}

void rendering_window_queue_rect_texture_update(RenderingHandle* rendering, RenderingWindowHandle* window, RectTextureSlot slot, TextureIndex texture_index)
{
    rendering_window_queue_pipeline_texture_update(rendering, window, BUSTER_PIPELINE_RECT, (u32)slot, texture_index);
}

void rendering_window_rect_texture_update_end(RenderingHandle* rendering, RenderingWindowHandle* window)
{
    rendering_window_texture_update_end(rendering, window, BUSTER_PIPELINE_RECT);
}

BUSTER_GLOBAL_LOCAL VkFormat vk_texture_format(TextureFormat format)
{
    VkFormat result = {0};
    switch (format)
    {
        break;
    case TEXTURE_FORMAT_R8_UNORM:
        result = VK_FORMAT_R8_UNORM;
        break;
    case TEXTURE_FORMAT_R8G8B8A8_SRGB:
        result = VK_FORMAT_R8G8B8A8_SRGB;
        break;
    case TEXTURE_FORMAT_COUNT:
        BUSTER_UNREACHABLE();
    }

    return result;
}

BUSTER_GLOBAL_LOCAL u32 format_channel_count(TextureFormat format)
{
    switch (format)
    {
        break;
    case TEXTURE_FORMAT_R8_UNORM:
        return 1;
        break;
    case TEXTURE_FORMAT_R8G8B8A8_SRGB:
        return 4;
        break;
    case TEXTURE_FORMAT_COUNT:
        BUSTER_UNREACHABLE();
    }

    BUSTER_UNREACHABLE();
}

BUSTER_GLOBAL_LOCAL VulkanBuffer vk_buffer_create(VkDevice device, const VkAllocationCallbacks* allocation_callbacks,
                                                  const VkPhysicalDeviceMemoryProperties* physical_device_memory_properties, VkDeviceSize buffer_size,
                                                  VkBufferUsageFlags usage_flags, VkMemoryPropertyFlags memory_flags)
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
        result.memory =
            vk_allocate_memory(device, allocation_callbacks, physical_device_memory_properties, memory_requirements, memory_flags, use_device_address_bit);

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
    VkFence fences[] = {context.fence};
    VkCommandBufferResetFlags reset_flags = 0;

    VkResult reset_fences = vkResetFences(context.device, BUSTER_ARRAY_LENGTH(fences), fences);
    VkResult reset_command_buffer = vkResetCommandBuffer(context.command_buffer, reset_flags);

    VkCommandBufferBeginInfo command_buffer_begin_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    VkResult begin_command_buffer = vkBeginCommandBuffer(context.command_buffer, &command_buffer_begin_info);

    return reset_fences == VK_SUCCESS && reset_command_buffer == VK_SUCCESS && begin_command_buffer == VK_SUCCESS;
}

BUSTER_GLOBAL_LOCAL bool immediate_end(ImmediateContext context)
{
    VkFence fences[] = {context.fence};

    bool result = false;

    if (vkEndCommandBuffer(context.command_buffer) == VK_SUCCESS)
    {
        VkCommandBufferSubmitInfo command_buffer_submit_infos[] = {{
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
            .pNext = 0,
            .deviceMask = 0,
            .commandBuffer = context.command_buffer,
        }};

        VkSubmitFlags submit_flags = 0;

        VkSubmitInfo2 submit_info[] = {{
            .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
            .pNext = 0,
            .flags = submit_flags,
            .waitSemaphoreInfoCount = 0,
            .pWaitSemaphoreInfos = 0,
            .commandBufferInfoCount = BUSTER_ARRAY_LENGTH(command_buffer_submit_infos),
            .pCommandBufferInfos = command_buffer_submit_infos,
            .signalSemaphoreInfoCount = 0,
            .pSignalSemaphoreInfos = 0,
        }};

        VkResult submit_result = vkQueueSubmit2(context.queue, BUSTER_ARRAY_LENGTH(submit_info), submit_info, context.fence);
        if (submit_result == VK_SUCCESS)
        {
            VkBool32 wait_all = 1;
            u64 timeout = ~(u64)0;

            VkResult wait_result = vkWaitForFences(context.device, BUSTER_ARRAY_LENGTH(fences), fences, wait_all, timeout);
            result = wait_result == VK_SUCCESS;
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
        .subresourceRange =
            {
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

typedef struct VulkanCopyImage VulkanCopyImage;
struct VulkanCopyImage
{
    VkImage handle;
    VkExtent2D extent;
};

typedef struct VulkanCopyImageArgs VulkanCopyImageArgs;
struct VulkanCopyImageArgs
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
            .srcOffsets =
                {
                    [1] =
                        {
                            .x = (s32)args.source.extent.width,
                            .y = (s32)args.source.extent.height,
                            .z = 1,
                        },
                },
            .dstSubresource = subresource_layers,
            .dstOffsets =
                {
                    [1] =
                        {
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

TextureIndex rendering_texture_create(RenderingHandle* rendering, TextureMemory texture_memory)
{
    BUSTER_CHECK(texture_memory.depth == 1);

    u32 texture_index = rendering->texture_count;
    rendering->texture_count += 1;
    VulkanTexture* texture = &rendering->textures[texture_index];
    texture->image = vk_image_create(rendering->device, rendering->allocator, &rendering->device_memory_properties,
                                     (VulkanImageCreate){
                                         .width = texture_memory.width,
                                         .height = texture_memory.height,
                                         .mip_levels = 1,
                                         .format = vk_texture_format(texture_memory.format),
                                         .usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                                     });
    texture->sampler = rendering->sampler;

    u64 image_size = (u64)texture_memory.depth * texture_memory.width * texture_memory.height * format_channel_count(texture_memory.format);
    VkBufferUsageFlags buffer_usage_flags = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    VkMemoryPropertyFlags buffer_memory_flags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    texture->transfer_buffer =
        vk_buffer_create(rendering->device, rendering->allocator, &rendering->device_memory_properties, image_size, buffer_usage_flags, buffer_memory_flags);
    memcpy((void*)texture->transfer_buffer.address, texture_memory.pointer, image_size);

    immediate_start(rendering->immediate);

    vk_image_transition(rendering->immediate.command_buffer, texture->image.handle, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

    VkBufferImageCopy copy_regions[] = {{
        .bufferOffset = 0,
        .bufferRowLength = 0,
        .bufferImageHeight = 0,
        .imageSubresource =
            {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .mipLevel = 0,
                .baseArrayLayer = 0,
                .layerCount = 1,
            },
        .imageOffset =
            {
                .x = 0,
                .y = 0,
                .z = 0,
            },
        .imageExtent =
            {
                .width = texture_memory.width,
                .height = texture_memory.height,
                .depth = texture_memory.depth,
            },
    }};

    vkCmdCopyBufferToImage(rendering->immediate.command_buffer, texture->transfer_buffer.handle, texture->image.handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           BUSTER_ARRAY_LENGTH(copy_regions), copy_regions);

    vk_image_transition(rendering->immediate.command_buffer, texture->image.handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    // Swiftshader's JIT compiler allocates internal state on first queue submission
    // that it never frees on vkDestroyDevice. Disable LSAN for this call so those
    // driver-internal allocations aren't reported as leaks.
    BUSTER_LSAN_DISABLE();
    immediate_end(rendering->immediate);
    BUSTER_LSAN_ENABLE();

    return (TextureIndex){.value = texture_index};
}

BUSTER_GLOBAL_LOCAL WindowFrame* rendering_window_frame(RenderingWindowHandle* window)
{
    return &window->frames[window->frame_index % window->frame_count];
}

void rendering_window_frame_begin(RenderingHandle* rendering, RenderingWindowHandle* window)
{
    WindowFrame* frame = rendering_window_frame(window);
    u64 timeout = ~(u64)0;
    bool log_frame_begin = vulkan_frame_begin_log_count < 3;
    if (log_frame_begin)
    {
        string_print(S8("Vulkan frame begin {u32}: swapchain={u64:x}, extent={u32}x{u32}, frame_index={u32}, swapchain_image_index={u32}\n"),
                     vulkan_frame_begin_log_count, (u64)window->swapchain, window->width, window->height, window->frame_index, window->swapchain_image_index);
        vulkan_frame_begin_log_count += 1;
    }

    VkSurfaceCapabilitiesKHR surface_capabilities = {0};
    VkResult capabilities_result = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(rendering->physical_device, window->surface, &surface_capabilities);
    if (capabilities_result == VK_SUCCESS)
    {
        u32 surface_width = surface_capabilities.currentExtent.width;
        u32 surface_height = surface_capabilities.currentExtent.height;
        if (surface_width && surface_height && (surface_width != window->width || surface_height != window->height))
        {
            string_print(S8("Vulkan frame begin detected surface resize: stored={u32}x{u32}, current={u32}x{u32}\n"), window->width, window->height,
                         surface_width, surface_height);
            swapchain_recreate(rendering, window);
            frame = rendering_window_frame(window);
        }
    }

    u32 fence_count = 1;
    VkBool32 wait_all = 1;
    VkResult wait_result = vkWaitForFences(rendering->device, fence_count, &frame->render_fence, wait_all, timeout);

    if (wait_result == VK_SUCCESS)
    {
        VkFence image_fence = 0;
        VkResult next_image_result =
            vkAcquireNextImageKHR(rendering->device, window->swapchain, timeout, frame->swapchain_semaphore, image_fence, &window->swapchain_image_index);
        u32 acquired_image_index = window->swapchain_image_index;

        if (next_image_result == VK_ERROR_OUT_OF_DATE_KHR)
        {
            string_print(S8("Vulkan frame begin acquire out of date: vkAcquireNextImageKHR={u64:x}\n"), (u64)(u32)next_image_result);
            swapchain_recreate(rendering, window);
            frame = rendering_window_frame(window);
            next_image_result =
                vkAcquireNextImageKHR(rendering->device, window->swapchain, timeout, frame->swapchain_semaphore, image_fence, &window->swapchain_image_index);
            acquired_image_index = window->swapchain_image_index;
        }

        if (next_image_result != VK_SUCCESS && next_image_result != VK_SUBOPTIMAL_KHR)
        {
            string_print(S8("Vulkan frame begin acquire failed: vkAcquireNextImageKHR={u64:x}\n"), (u64)(u32)next_image_result);
            os_fail();
        }

        VkCommandBufferResetFlags reset_flags = 0;
        VkResult reset_fence_result = vkResetFences(rendering->device, fence_count, &frame->render_fence);
        VkResult reset_command_buffer_result = vkResetCommandBuffer(frame->command_buffer, reset_flags);
        bool success = reset_fence_result == VK_SUCCESS && reset_command_buffer_result == VK_SUCCESS;
        if (!success)
        {
            string_print(S8("Vulkan frame begin reset failed: vkResetFences={u64:x}, vkResetCommandBuffer={u64:x}\n"), (u64)(u32)reset_fence_result,
                         (u64)(u32)reset_command_buffer_result);
            os_fail();
        }

        if (log_frame_begin)
        {
            string_print(S8("Vulkan frame begin acquire: vkWaitForFences={u64:x}, vkAcquireNextImageKHR={u64:x}, image_index={u32}\n"), (u64)(u32)wait_result,
                         (u64)(u32)next_image_result, acquired_image_index);
        }
    }
    else
    {
        string_print(S8("Vulkan frame begin wait failed: vkWaitForFences={u64:x}\n"), (u64)(u32)wait_result);
        os_fail();
    }

    // Reset frame data
    for (u32 i = 0; i < BUSTER_ARRAY_LENGTH(window->pipeline_instantiations); i += 1)
    {
        FramePipelineInstantiation* pipeline_instantiation = &frame->pipeline_instantiations[i];
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

    VkBufferUsageFlags usage = (VK_BUFFER_USAGE_TRANSFER_DST_BIT * is_dst) | (VK_BUFFER_USAGE_TRANSFER_SRC_BIT * is_src) |
                               ((VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT) * (type == BUFFER_TYPE_VERTEX)) |
                               (VK_BUFFER_USAGE_INDEX_BUFFER_BIT * (type == BUFFER_TYPE_INDEX));
    VkMemoryPropertyFlags memory_flags =
        (VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT * is_dst) | ((VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) * is_src);
    VulkanBuffer result = vk_buffer_create(rendering->device, rendering->allocator, &rendering->device_memory_properties, size, usage, memory_flags);
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

typedef struct HostBufferCopy HostBufferCopy;
struct HostBufferCopy
{
    ByteSlice source;
    u64 destination_offset;
};

typedef struct SliceHostBufferCopy SliceHostBufferCopy;
struct SliceHostBufferCopy
{
    HostBufferCopy* pointer;
    u64 length;
};

BUSTER_GLOBAL_LOCAL void buffer_copy_to_host(VulkanBuffer buffer, SliceHostBufferCopy regions)
{
    BUSTER_CHECK(buffer.type == BUFFER_TYPE_STAGING);

    u8* buffer_pointer = (u8*)buffer.address;

    for (u64 i = 0; i < regions.length; i += 1)
    {
        HostBufferCopy region = regions.pointer[i];
        u8* destination = buffer_pointer + region.destination_offset;
        BUSTER_CHECK(destination + region.source.length <= (u8*)buffer.address + buffer.size);
        memcpy(destination, region.source.pointer, region.source.length);
    }
}

typedef struct LocalBufferCopyRegion LocalBufferCopyRegion;
struct LocalBufferCopyRegion
{
    u64 source_offset;
    u64 destination_offset;
    u64 size;
};

typedef struct SliceLocalBufferCopyRegion SliceLocalBufferCopyRegion;
struct SliceLocalBufferCopyRegion
{
    LocalBufferCopyRegion* pointer;
    u64 length;
};

typedef struct LocalBufferCopy LocalBufferCopy;
struct LocalBufferCopy
{
    VulkanBuffer destination;
    VulkanBuffer source;
    SliceLocalBufferCopyRegion regions;
};

typedef struct SliceLocalBufferCopy SliceLocalBufferCopy;
struct SliceLocalBufferCopy
{
    LocalBufferCopy* pointer;
    u64 length;
};

#define MAX_LOCAL_BUFFER_COPY_COUNT (16)

BUSTER_GLOBAL_LOCAL void buffer_copy_to_local_command(VkCommandBuffer command_buffer, SliceLocalBufferCopy copies)
{
    for (u64 copy_i = 0; copy_i < copies.length; copy_i += 1)
    {
        LocalBufferCopy copy = copies.pointer[copy_i];
        VulkanBuffer* source_buffer = &copy.source;
        VulkanBuffer* destination_buffer = &copy.destination;

        VkBufferCopy2 buffer_copies[MAX_LOCAL_BUFFER_COPY_COUNT];

        for (u64 copy_region_i = 0; copy_region_i < copy.regions.length; copy_region_i += 1)
        {
            LocalBufferCopyRegion copy_region = copy.regions.pointer[copy_region_i];
            buffer_copies[copy_region_i] = (VkBufferCopy2){
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

void rendering_window_frame_end(RenderingHandle* rendering, RenderingWindowHandle* window)
{
    WindowFrame* frame = rendering_window_frame(window);
    if (vulkan_frame_end_log_count < 3)
    {
        string_print(S8("Vulkan frame end {u32}: swapchain={u64:x}, render_image={u64:x}, image_index={u32}, extent={u32}x{u32}\n"), vulkan_frame_end_log_count,
                     (u64)window->swapchain, (u64)window->render_image.handle, window->swapchain_image_index, window->width, window->height);
    }

    VkCommandBufferBeginInfo command_buffer_begin_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };

    if (vkBeginCommandBuffer(frame->command_buffer, &command_buffer_begin_info) == VK_SUCCESS)
    {
        for (BusterPipeline pipeline_index = 0; pipeline_index < BUSTER_PIPELINE_COUNT; pipeline_index += 1)
        {
            FramePipelineInstantiation* frame_pipeline_instantiation = &frame->pipeline_instantiations[pipeline_index];

            if (!arena_buffer_is_empty(frame_pipeline_instantiation->vertex_buffer.cpu))
            {
                u64 new_vertex_buffer_size = arena_buffer_size(frame_pipeline_instantiation->vertex_buffer.cpu);
                u64 new_index_buffer_size = arena_buffer_size(frame_pipeline_instantiation->index_buffer.cpu);
                u64 new_transient_buffer_size = new_vertex_buffer_size + new_index_buffer_size;

                buffer_ensure_capacity(rendering, &frame_pipeline_instantiation->transient_buffer, new_transient_buffer_size);
                buffer_ensure_capacity(rendering, &frame_pipeline_instantiation->vertex_buffer.gpu, new_vertex_buffer_size);
                buffer_ensure_capacity(rendering, &frame_pipeline_instantiation->index_buffer.gpu, new_index_buffer_size);

                buffer_copy_to_host(frame_pipeline_instantiation->transient_buffer,
                                    (SliceHostBufferCopy)BUSTER_ARRAY_TO_SLICE(((HostBufferCopy[]){
                                        (HostBufferCopy){
                                            .source =
                                                (ByteSlice){
                                                    .pointer = arena_buffer_start(frame_pipeline_instantiation->vertex_buffer.cpu),
                                                    .length = new_vertex_buffer_size,
                                                },
                                            .destination_offset = 0,
                                        },
                                        (HostBufferCopy){
                                            .source =
                                                (ByteSlice){
                                                    .pointer = arena_buffer_start(frame_pipeline_instantiation->index_buffer.cpu),
                                                    .length = new_index_buffer_size,
                                                },
                                            .destination_offset = new_vertex_buffer_size,
                                        },
                                    })));

                buffer_copy_to_local_command(frame->command_buffer, (SliceLocalBufferCopy)BUSTER_ARRAY_TO_SLICE(((LocalBufferCopy[]){
                                                                        {
                                                                            .destination = frame_pipeline_instantiation->vertex_buffer.gpu,
                                                                            .source = frame_pipeline_instantiation->transient_buffer,
                                                                            .regions = BUSTER_ARRAY_TO_SLICE(((LocalBufferCopyRegion[]){
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
                                                                            .regions = BUSTER_ARRAY_TO_SLICE(((LocalBufferCopyRegion[]){
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

        VkViewport viewports[] = {{
            .x = 0,
            .y = 0,
            .width = (f32)window->width,
            .height = (f32)window->height,
            .minDepth = 0.0f,
            .maxDepth = 1.0f,
        }};

        u32 first_viewport = 0;
        vkCmdSetViewport(frame->command_buffer, first_viewport, BUSTER_ARRAY_LENGTH(viewports), viewports);

        VkRect2D scissors[] = {{
            .offset =
                {
                    .x = 0,
                    .y = 0,
                },
            .extent =
                {
                    .width = window->width,
                    .height = window->height,
                },
        }};

        u32 first_scissor = 0;
        vkCmdSetScissor(frame->command_buffer, first_scissor, BUSTER_ARRAY_LENGTH(scissors), scissors);

        VkRenderingAttachmentInfo color_attachments[] = {
            {
                .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
                .imageView = window->render_image.view,
                .imageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
                .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
                .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
                .clearValue = {.color = {.float32 = {255.0f, 0.0f, 255.0f, 1.0f}}},
            },
        };

        VkRenderingInfo rendering_info = {
            .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
            .renderArea =
                {
                    .extent =
                        {
                            .width = window->width,
                            .height = window->height,
                        },
                },
            .layerCount = 1,
            .colorAttachmentCount = BUSTER_ARRAY_LENGTH(color_attachments),
            .pColorAttachments = color_attachments,
        };

        vkCmdBeginRendering(frame->command_buffer, &rendering_info);

        for (BusterPipeline pipeline_index = 0; pipeline_index < BUSTER_PIPELINE_COUNT; pipeline_index += 1)
        {
            Pipeline* pipeline = &rendering->pipelines[pipeline_index];
            PipelineInstantiation* pipeline_instantiation = &window->pipeline_instantiations[pipeline_index];
            FramePipelineInstantiation* frame_pipeline_instantiation = &frame->pipeline_instantiations[pipeline_index];

            if (!arena_buffer_is_empty(frame_pipeline_instantiation->vertex_buffer.cpu))
            {
                // Bind pipeline and descriptor sets
                {
                    vkCmdBindPipeline(frame->command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->handle);
                    // print("Binding pipeline: 0x{u64}\n", pipeline->handle);
                    u32 dynamic_offset_count = 0;
                    u32* dynamic_offsets = 0;
                    u32 first_set = 0;
                    vkCmdBindDescriptorSets(frame->command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->layout, first_set, pipeline->descriptor_set_count,
                                            pipeline_instantiation->descriptor_sets, dynamic_offset_count, dynamic_offsets);
                    // print("Binding descriptor sets: 0x{u64}\n", pipeline_instantiation->descriptor_sets);
                    frame->bound_pipeline = (BusterPipeline)pipeline_index;
                }

                // Bind index buffer
                {
                    vkCmdBindIndexBuffer(frame->command_buffer, frame_pipeline_instantiation->index_buffer.gpu.handle, 0, VK_INDEX_TYPE_UINT32);
                    frame->index_buffer = frame_pipeline_instantiation->index_buffer.gpu.handle;
                    // print("Binding descriptor sets: 0x{u64}\n", frame->index_buffer);
                }

#if BUSTER_ANDROID
                // Point the rect descriptor set (set 0, binding 1) at this frame's
                // vertex storage buffer. Safe to update mid-recording because Android
                // keeps a single frame in flight, so no submission is using the set.
                {
                    VkDescriptorBufferInfo vertex_buffer_info = {
                        .buffer = frame_pipeline_instantiation->vertex_buffer.gpu.handle,
                        .offset = 0,
                        .range = VK_WHOLE_SIZE,
                    };
                    VkWriteDescriptorSet vertex_buffer_write = {
                        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                        .dstSet = pipeline_instantiation->descriptor_sets[0],
                        .dstBinding = 1,
                        .dstArrayElement = 0,
                        .descriptorCount = 1,
                        .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                        .pBufferInfo = &vertex_buffer_info,
                    };
                    vkUpdateDescriptorSets(rendering->device, 1, &vertex_buffer_write, 0, 0);
                }

                // Vertices come from the descriptor above; only dimensions are pushed.
                DrawConstants push_constants = {
                    .width = (f32)window->width,
                    .height = (f32)window->height,
                };

                {
                    VkPushConstantRange push_constant_range = pipeline->push_constant_ranges[0];
                    vkCmdPushConstants(frame->command_buffer, pipeline->layout, push_constant_range.stageFlags, push_constant_range.offset,
                                       push_constant_range.size, &push_constants);
                }
#else
                // Send vertex buffer and screen dimensions to the shader
                GPUDrawPushConstants push_constants = {
                    .vertex_buffer = frame_pipeline_instantiation->vertex_buffer.gpu.address,
                    .width = (f32)window->width,
                    .height = (f32)window->height,
                };

                {
                    VkPushConstantRange push_constant_range = pipeline->push_constant_ranges[0];
                    vkCmdPushConstants(frame->command_buffer, pipeline->layout, push_constant_range.stageFlags, push_constant_range.offset,
                                       push_constant_range.size, &push_constants);
                    frame->push_constants = push_constants;
                }
#endif

                vkCmdDrawIndexed(frame->command_buffer, (u32)(arena_buffer_size(frame_pipeline_instantiation->index_buffer.cpu) / sizeof(u32)), 1, 0, 0, 0);
            }
        }

        vkCmdEndRendering(frame->command_buffer);

        vk_image_transition(frame->command_buffer, window->render_image.handle, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

        VkImage swapchain_image = window->swapchain_images[window->swapchain_image_index];
        vk_image_transition(frame->command_buffer, swapchain_image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

        vk_image_copy(frame->command_buffer, (VulkanCopyImageArgs){
                                                 .source =
                                                     {
                                                         .handle = window->render_image.handle,
                                                         .extent =
                                                             {
                                                                 .width = window->width,
                                                                 .height = window->height,
                                                             },
                                                     },
                                                 .destination =
                                                     {
                                                         .handle = swapchain_image,
                                                         .extent =
                                                             {
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

            VkSemaphore render_semaphore = window->render_semaphores[window->swapchain_image_index];

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

            VkResult submit_result = vkQueueSubmit2(rendering->graphics_queue, BUSTER_ARRAY_LENGTH(submit_info), submit_info, frame->render_fence);
            if (submit_result == VK_SUCCESS)
            {
                const VkSwapchainKHR swapchains[] = {window->swapchain};
                const u32 swapchain_image_indices[] = {window->swapchain_image_index};
                const VkSemaphore wait_semaphores[] = {render_semaphore};
                VkResult results[BUSTER_ARRAY_LENGTH(swapchains)] = {VK_SUCCESS};

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

                if (vulkan_frame_end_log_count < 3)
                {
                    string_print(
                        S8("Vulkan frame end present {u32}: vkQueueSubmit2={u64:x}, vkQueuePresentKHR={u64:x}, result0={u64:x}, render_semaphore={u64:x}\n"),
                        vulkan_frame_end_log_count, (u64)(u32)submit_result, (u64)(u32)present_result, (u64)(u32)results[0], (u64)render_semaphore);
                    vulkan_frame_end_log_count += 1;
                }

                if (present_result == VK_SUCCESS)
                {
                    for (u32 i = 0; i < BUSTER_ARRAY_LENGTH(results); i += 1)
                    {
                        VkResult result = results[i];
                        if (result != VK_SUCCESS)
                        {
                            os_fail();
                        }
                    }
                }
                else if (present_result == VK_ERROR_OUT_OF_DATE_KHR || present_result == VK_SUBOPTIMAL_KHR)
                {
                    string_print(S8("Vulkan frame end present requires swapchain recreate: vkQueuePresentKHR={u64:x}\n"), (u64)(u32)present_result);
                    swapchain_recreate(rendering, window);
                }
                else
                {
                    string_print(S8("Vulkan frame end present failed: vkQueuePresentKHR={u64:x}\n"), (u64)(u32)present_result);
                    os_fail();
                }
            }
            else
            {
                string_print(S8("Vulkan frame end submit failed: vkQueueSubmit2={u64:x}\n"), (u64)(u32)submit_result);
                os_fail();
            }
        }
        else
        {
            string_print(S8("Vulkan frame end command buffer failed: vkEndCommandBuffer failed\n"));
            os_fail();
        }
    }
    else
    {
        string_print(S8("Vulkan frame end command buffer failed: vkBeginCommandBuffer failed\n"));
        os_fail();
    }
}

// TODO: support gradient
void rendering_window_deinitialize(RenderingHandle* rendering, RenderingWindowHandle* window)
{
    string_print(S8("Vulkan render window deinitialize: surface={u64:x}, swapchain={u64:x}, frame_count={u32}, image_count={u32}\n"), (u64)window->surface,
                 (u64)window->swapchain, window->frame_count, window->swapchain_image_count);
    if (vkDeviceWaitIdle(rendering->device) == VK_SUCCESS)
    {
        for (u32 i = 0; i < window->frame_count; i += 1)
        {
            WindowFrame* frame = &window->frames[i];
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
                FramePipelineInstantiation* pipeline = &frame->pipeline_instantiations[p];
                if (pipeline->vertex_buffer.cpu)
                {
                    arena_destroy(pipeline->vertex_buffer.cpu, 1);
                    pipeline->vertex_buffer.cpu = 0;
                }

                if (pipeline->index_buffer.cpu)
                {
                    arena_destroy(pipeline->index_buffer.cpu, 1);
                    pipeline->index_buffer.cpu = 0;
                }

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
        string_print(S8("Device failed to wait idle\n"));
    }
}

void rendering_deinitialize(RenderingHandle* rendering)
{
    string_print(S8("Vulkan rendering deinitialize: texture_count={u32}, device={u64:x}, instance={u64:x}\n"), rendering->texture_count, (u64)rendering->device,
                 (u64)rendering->instance);
    if (vkDeviceWaitIdle(rendering->device) == VK_SUCCESS)
    {
        for (BusterPipeline pipeline_index = 0; pipeline_index < BUSTER_PIPELINE_COUNT; pipeline_index += 1)
        {
            Pipeline* pipeline = &rendering->pipelines[pipeline_index];
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
            VulkanTexture* texture = &rendering->textures[i];
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
        string_print(S8("Device failed to wait idle\n"));
    }
}
