#include <buster/tests/rendering_test.h>

BUSTER_TEST_F_DECL UnitTestResult rendering_tests(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    BUSTER_UNUSED(arguments);

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

    return result;
}
