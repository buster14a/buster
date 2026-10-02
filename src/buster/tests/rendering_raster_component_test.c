// Headless CPU raster goldens; native APIs remain unavailable in this target.
// Pattern bytes are first-party synthetic data, authored for these checks.
#include <buster/lib/rendering_raster.h>
#include <stdio.h>
#if BUSTER_UNITY_BUILD
#include <buster/lib/rendering_raster.c>
#endif

BUSTER_GLOBAL_LOCAL u32 raster_assertions;
BUSTER_GLOBAL_LOCAL u32 raster_failures;

BUSTER_GLOBAL_LOCAL void raster_check(bool condition, char const* description)
{
    raster_assertions += 1;
    if (!condition)
    {
        raster_failures += 1;
        printf("FAIL: %s\n", description);
    }
}

int main(void)
{
    u8 source_pixels[24] = {0};
    for (u32 index = 0; index < 6; index += 1)
    {
        source_pixels[index * 4] = (u8)((index + 1u) * 30u);
        source_pixels[index * 4 + 1] = (u8)(255u - source_pixels[index * 4]);
        source_pixels[index * 4 + 2] = (u8)index;
        source_pixels[index * 4 + 3] = 255;
    }
    RenderingRasterSource source = {.pixels = {source_pixels, sizeof(source_pixels)}, .width = 3, .height = 2, .stride = 12, .orientation = 1};
    u8 output[512] = {0};
    u8 orientations[8][6] = {
        {1, 2, 3, 4, 5, 6},
        {3, 2, 1, 6, 5, 4},
        {6, 5, 4, 3, 2, 1},
        {4, 5, 6, 1, 2, 3},
        {1, 4, 2, 5, 3, 6},
        {4, 1, 5, 2, 6, 3},
        {6, 3, 5, 2, 4, 1},
        {3, 6, 2, 5, 1, 4},
    };
    RenderingRasterCanvas canvas = {.pixels = {output, sizeof(output)}, .width = 3, .height = 2, .stride = 12};
    RenderingRasterView view = {.zoom = 1.0};
    for (u32 orientation = 1; orientation <= 8; orientation += 1)
    {
        source.orientation = orientation;
        canvas.width = orientation >= 5 ? 2u : 3u;
        canvas.height = orientation >= 5 ? 3u : 2u;
        canvas.stride = canvas.width * 4;
        raster_check(rendering_raster_draw(canvas, source, view), "orientation accepted");
        for (u32 index = 0; index < 6; index += 1)
        {
            u32 source_index = (u32)orientations[orientation - 1u][index] - 1u;
            raster_check(memcmp(output + index * 4, source_pixels + source_index * 4, 4) == 0, "independent orientation golden");
        }
    }

    source.orientation = 1;
    canvas.width = 6;
    canvas.height = 4;
    canvas.stride = 24;
    view.zoom = 2.0;
    raster_check(rendering_raster_draw(canvas, source, view), "nearest zoom accepted");
    for (u32 y = 0; y < canvas.height; y += 1)
    {
        for (u32 x = 0; x < canvas.width; x += 1)
        {
            u32 index = (y / 2u) * 3u + x / 2u;
            raster_check(memcmp(output + y * canvas.stride + x * 4, source_pixels + index * 4, 4) == 0, "nearest zoom golden");
        }
    }
    view = (RenderingRasterView){.x = 1.0, .y = 1.0, .zoom = 1.0};
    raster_check(rendering_raster_draw(canvas, source, view), "pan accepted");
    raster_check(output[0] == BUSTER_RASTER_CHECKER_DARK && output[3] == 255, "pan reveals checkerboard");
    raster_check(memcmp(output + canvas.stride + 4, source_pixels, 4) == 0, "pan preserves sample");

    canvas.width = 32;
    canvas.height = 2;
    canvas.stride = 128;
    raster_check(rendering_raster_draw(canvas, (RenderingRasterSource){0}, (RenderingRasterView){.zoom = 1}), "empty source background");
    raster_check(output[0] == BUSTER_RASTER_CHECKER_DARK && output[16 * 4] == BUSTER_RASTER_CHECKER_LIGHT, "checkerboard cells");
    u8 alpha_pixel[4] = {255, 0, 0, 128};
    RenderingRasterSource alpha_source = {.pixels = {alpha_pixel, sizeof(alpha_pixel)}, .width = 1, .height = 1, .stride = 4, .orientation = 1};
    raster_check(rendering_raster_draw(canvas, alpha_source, (RenderingRasterView){.zoom = 1}), "straight alpha accepted");
    raster_check(output[0] == 152 && output[1] == 24 && output[2] == 24 && output[3] == 255, "straight alpha golden");

    memset(output, 0xa5, sizeof(output));
    u8 preserved[sizeof(output)];
    memcpy(preserved, output, sizeof(output));
    source.orientation = 0;
    raster_check(!rendering_raster_draw(canvas, source, (RenderingRasterView){.zoom = 1}), "bad orientation refused");
    source.orientation = 1;
    source.pixels.length = sizeof(source_pixels) - 1u;
    raster_check(!rendering_raster_draw(canvas, source, (RenderingRasterView){.zoom = 1}), "short source refused");
    source.pixels.length = sizeof(source_pixels);
    raster_check(!rendering_raster_draw(canvas, source, (RenderingRasterView){.zoom = 0}), "zero zoom refused");
    raster_check(!rendering_raster_draw(canvas, source, (RenderingRasterView){.zoom = BUSTER_RASTER_MAX_ZOOM * 2}), "large zoom refused");
    raster_check(!rendering_raster_draw(canvas, source, (RenderingRasterView){.x = 1.0e13, .zoom = 1}), "unbounded pan refused");
    union {u64 bits; f64 value;} nan = {.bits = UINT64_C(0x7ff8000000000000)};
    raster_check(!rendering_raster_draw(canvas, source, (RenderingRasterView){.zoom = nan.value}), "NaN zoom refused");
    raster_check(memcmp(output, preserved, sizeof(output)) == 0, "failed admission is transactional");
    RenderingRasterCanvas too_wide = canvas;
    too_wide.width = BUSTER_RASTER_MAX_DIMENSION + 1u;
    raster_check(!rendering_raster_canvas_is_valid(too_wide), "canvas dimension cap");
    RenderingRasterCanvas too_large = canvas;
    too_large.stride = UINT32_MAX;
    raster_check(!rendering_raster_canvas_is_valid(too_large), "canvas byte cap");
    RenderingRasterCanvas alias = {.pixels = {source_pixels, sizeof(source_pixels)}, .width = 3, .height = 2, .stride = 12};
    raster_check(!rendering_raster_draw(alias, source, (RenderingRasterView){.zoom = 1}), "source/canvas alias refused");
    RenderingRasterPresenter presenter = {0};
    raster_check(!rendering_raster_initialize(&presenter, (WmNativeSurface){0}), "unsupported native surface refused");
    printf("rendering_raster_component_tests: %u/%u assertions passed\n", (unsigned)(raster_assertions - raster_failures), (unsigned)raster_assertions);
    return raster_failures != 0;
}
