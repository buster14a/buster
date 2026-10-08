// Headless Vulkan texture-slot admission policy regression. Native GPU work is
// deliberately outside this capacity-only test.
#include <buster/lib/rendering/texture_admission_internal.h>
#include <stdio.h>

BUSTER_GLOBAL_LOCAL u32 texture_admission_assertions;
BUSTER_GLOBAL_LOCAL u32 texture_admission_failures;

BUSTER_GLOBAL_LOCAL void texture_admission_check(bool condition, char const* description)
{
    texture_admission_assertions += 1;
    if (!condition)
    {
        texture_admission_failures += 1;
        printf("FAIL: %s\n", description);
    }
}

int main(void)
{
    for (u32 count = 0; count < BUSTER_RENDERING_VULKAN_MAX_TEXTURE_COUNT; count += 1)
    {
        u32 texture_index = UINT32_MAX;
        bool admitted = rendering_vulkan_texture_index_admit(count, &texture_index);
        texture_admission_check(admitted, "available Vulkan texture slot admitted");
        texture_admission_check(texture_index == count, "admitted slot retains its stable table index");
    }

    u32 rejected_index = UINT32_C(0xa5a55a5a);
    texture_admission_check(!rendering_vulkan_texture_index_admit(BUSTER_RENDERING_VULKAN_MAX_TEXTURE_COUNT, &rejected_index),
                            "exact capacity refuses another texture");
    texture_admission_check(rejected_index == UINT32_C(0xa5a55a5a), "rejected admission leaves the caller's index untouched");
    texture_admission_check(!rendering_vulkan_texture_index_admit(UINT32_MAX, &rejected_index), "out-of-range count refuses admission");
    texture_admission_check(rejected_index == UINT32_C(0xa5a55a5a), "overflow refusal leaves the caller's index untouched");
    texture_admission_check(!rendering_vulkan_texture_index_admit(0, 0), "missing index output refuses admission");

    texture_admission_check(rendering_vulkan_texture_index_is_valid(BUSTER_RENDERING_VULKAN_MAX_TEXTURE_COUNT, BUSTER_RENDERING_VULKAN_MAX_TEXTURE_COUNT - 1u),
                            "last admitted texture remains bindable");
    texture_admission_check(!rendering_vulkan_texture_index_is_valid(BUSTER_RENDERING_VULKAN_MAX_TEXTURE_COUNT, BUSTER_RENDERING_VULKAN_MAX_TEXTURE_COUNT),
                            "first index beyond the table cannot be bound");
    texture_admission_check(!rendering_vulkan_texture_index_is_valid(BUSTER_RENDERING_VULKAN_MAX_TEXTURE_COUNT, UINT32_MAX),
                            "invalid creation sentinel cannot be bound");
    texture_admission_check(!rendering_vulkan_texture_index_is_valid(0, 0), "uncreated texture cannot be bound");

    printf("rendering_texture_admission_component_tests: %u/%u assertions passed\n", (unsigned)(texture_admission_assertions - texture_admission_failures),
           (unsigned)texture_admission_assertions);
    return texture_admission_failures != 0;
}
