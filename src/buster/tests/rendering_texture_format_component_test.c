// Headless texture-format contract checks. The backend probe reads only its
// native format mapping and does not create a device or submit GPU commands.
#include <buster/lib/system_headers.h>
#include <buster/lib/os.h>
#include <buster/lib/rendering/internal.h>
#include <stdio.h>

BUSTER_V_IMPL OsState os_state;
BUSTER_GLOBAL_LOCAL ProgramState rendering_texture_format_program;
BUSTER_V_IMPL ProgramState* program_state = &rendering_texture_format_program;

WmRect wm_window_get_framebuffer_rect(WmHandle* windowing, WmWindowHandle* wm_window)
{
    BUSTER_UNUSED(windowing);
    BUSTER_UNUSED(wm_window);
    return (WmRect){0};
}

WmOffset offset_from_rect(WmRect rect)
{
    BUSTER_UNUSED(rect);
    return (WmOffset){0};
}

WmNativeSurface wm_window_get_native_surface(WmHandle* windowing, WmWindowHandle* window)
{
    BUSTER_UNUSED(windowing);
    BUSTER_UNUSED(window);
    return (WmNativeSurface){0};
}

FontTextureAtlasDescription font_texture_atlas_create(Arena* arena, FontTextureAtlasCreate create)
{
    BUSTER_UNUSED(arena);
    BUSTER_UNUSED(create);
    return (FontTextureAtlasDescription){0};
}

BUSTER_GLOBAL_LOCAL bool rendering_texture_format_check(bool condition, const char* description, u32* assertions, u32* failures)
{
    *assertions += 1;
    if (!condition)
    {
        *failures += 1;
        fprintf(stderr, "rendering_texture_format_component_tests: %s failed\n", description);
    }
    return condition;
}

int main(void)
{
    u32 assertions = 0;
    u32 failures = 0;
    RenderingTextureFormatProperties r8 = rendering_texture_format_properties(TEXTURE_FORMAT_R8_UNORM);
    RenderingTextureFormatProperties rgba8_srgb = rendering_texture_format_properties(TEXTURE_FORMAT_R8G8B8A8_SRGB);
    rendering_texture_format_check(r8.channel_count == 1 && r8.color_channel_count == 1 && !r8.color_channels_are_srgb && !r8.has_alpha,
                                   "R8_UNORM is one linear unorm channel", &assertions, &failures);
    rendering_texture_format_check(rgba8_srgb.channel_count == 4 && rgba8_srgb.color_channel_count == 3 && rgba8_srgb.color_channels_are_srgb &&
                                       rgba8_srgb.has_alpha && !rgba8_srgb.alpha_is_srgb,
                                   "RGBA8_SRGB decodes RGB and leaves alpha unorm", &assertions, &failures);

    RenderingTextureFormatBackendProbe probe = rendering_texture_format_backend_probe_for_test();
    rendering_texture_format_check(probe.r8_channel_count == 1 && probe.rgba8_channel_count == 4,
                                   "backend byte widths match the shared format contract", &assertions, &failures);
#if BUSTER_USE_VULKAN
    rendering_texture_format_check(probe.backend == RENDERING_BACKEND_VULKAN && probe.native_mapping_available,
                                   "Vulkan backend mapping is available", &assertions, &failures);
#elif defined(_WIN32) && BUSTER_USE_D3D12
    rendering_texture_format_check(probe.backend == RENDERING_BACKEND_D3D12 && probe.native_mapping_available,
                                   "D3D12 backend mapping is available", &assertions, &failures);
#elif defined(__APPLE__)
    rendering_texture_format_check(probe.backend == RENDERING_BACKEND_METAL && probe.native_mapping_available,
                                   "Metal backend mapping is available", &assertions, &failures);
#else
    rendering_texture_format_check(probe.backend == RENDERING_BACKEND_NULL && !probe.native_mapping_available,
                                   "null backend advertises no native texture mapping", &assertions, &failures);
#endif
    if (probe.native_mapping_available)
    {
        rendering_texture_format_check(probe.r8_native_format == probe.expected_r8_native_format,
                                       "R8_UNORM maps to the native unorm format", &assertions, &failures);
        rendering_texture_format_check(probe.rgba8_native_format == probe.expected_rgba8_srgb_native_format,
                                       "RGBA8_SRGB maps to the native sRGB format", &assertions, &failures);
        rendering_texture_format_check(probe.r8_native_format != probe.rgba8_native_format,
                                       "linear and sRGB texture formats remain distinct", &assertions, &failures);
    }

    printf("rendering_texture_format_component_tests: %u/%u assertions passed; backend=%u; native_mapping=%u\n",
           (unsigned)(assertions - failures), (unsigned)assertions, (unsigned)probe.backend, (unsigned)probe.native_mapping_available);
    return failures != 0;
}
