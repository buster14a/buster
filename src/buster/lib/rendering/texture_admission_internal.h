#pragma once

#include <buster/lib/base.h>

#define BUSTER_RENDERING_VULKAN_MAX_TEXTURE_COUNT (16u)

BUSTER_GLOBAL_LOCAL bool rendering_vulkan_texture_index_admit(u32 texture_count, u32* texture_index)
{
    bool result = texture_index && texture_count < BUSTER_RENDERING_VULKAN_MAX_TEXTURE_COUNT;
    if (result)
    {
        *texture_index = texture_count;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool rendering_vulkan_texture_index_is_valid(u32 texture_count, u32 texture_index)
{
    return texture_index < texture_count && texture_index < BUSTER_RENDERING_VULKAN_MAX_TEXTURE_COUNT;
}
