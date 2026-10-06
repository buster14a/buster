// CPU raster front door: admission, orientation-aware nearest sampling, and
// straight-alpha checkerboard composition. raster_xcb.c owns native requests.
// API: rendering_raster_draw / initialize / present / deinitialize.
#include <buster/lib/rendering/raster_internal.h>

bool rendering_raster_canvas_is_valid(RenderingRasterCanvas canvas)
{
    u64 used_bytes = (u64)canvas.stride * canvas.height;
    bool result = canvas.pixels.pointer && canvas.width && canvas.height && canvas.width <= BUSTER_RASTER_MAX_DIMENSION &&
                  canvas.height <= BUSTER_RASTER_MAX_DIMENSION && canvas.stride >= (u64)canvas.width * 4 &&
                  used_bytes <= BUSTER_RASTER_MAX_CANVAS_BYTES && used_bytes <= canvas.pixels.length &&
                  used_bytes <= UINTPTR_MAX - (uintptr_t)canvas.pixels.pointer;
    return result;
}

BUSTER_GLOBAL_LOCAL bool rendering_raster_source_is_valid(RenderingRasterSource source)
{
    bool result;
    if (!source.width && !source.height)
    {
        result = !source.pixels.pointer && !source.pixels.length && !source.stride;
    }
    else
    {
        u64 used_bytes = (u64)source.stride * source.height;
        result = source.pixels.pointer && source.width && source.height && source.orientation >= 1 && source.orientation <= 8 &&
                 source.stride >= (u64)source.width * 4 && used_bytes <= source.pixels.length &&
                 used_bytes <= UINTPTR_MAX - (uintptr_t)source.pixels.pointer;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool rendering_raster_storage_is_disjoint(RenderingRasterCanvas canvas, RenderingRasterSource source)
{
    bool result = true;
    if (source.width)
    {
        uintptr_t canvas_begin = (uintptr_t)canvas.pixels.pointer;
        uintptr_t source_begin = (uintptr_t)source.pixels.pointer;
        uintptr_t canvas_end = canvas_begin + (uintptr_t)((u64)canvas.stride * canvas.height);
        uintptr_t source_end = source_begin + (uintptr_t)((u64)source.stride * source.height);
        result = canvas_end <= source_begin || source_end <= canvas_begin;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL u8 rendering_raster_composite_channel(u8 sample, u8 alpha, u8 background)
{
    u32 mixed = (u32)sample * alpha + (u32)background * (255u - alpha);
    return (u8)((mixed + 127u) / 255u);
}

bool rendering_raster_draw(RenderingRasterCanvas canvas, RenderingRasterSource source, RenderingRasterView view)
{
    bool result = rendering_raster_canvas_is_valid(canvas) && rendering_raster_source_is_valid(source) &&
                  view.x == view.x && view.y == view.y && view.zoom == view.zoom &&
                  view.x >= -1.0e12 && view.x <= 1.0e12 && view.y >= -1.0e12 && view.y <= 1.0e12 &&
                  view.zoom >= BUSTER_RASTER_MIN_ZOOM && view.zoom <= BUSTER_RASTER_MAX_ZOOM;
    if (result)
    {
        result = rendering_raster_storage_is_disjoint(canvas, source);
    }
    if (result)
    {
        u32 display_width = source.orientation >= 5 ? source.height : source.width;
        u32 display_height = source.orientation >= 5 ? source.width : source.height;
        for (u32 y = 0; y < canvas.height; y += 1)
        {
            u8* output = canvas.pixels.pointer + (u64)y * canvas.stride;
            f64 sample_y = ((f64)y + 0.5 - view.y) / view.zoom;
            for (u32 x = 0; x < canvas.width; x += 1)
            {
                u8 background = ((x / BUSTER_RASTER_CHECKER_SIZE + y / BUSTER_RASTER_CHECKER_SIZE) & 1u) ?
                                    BUSTER_RASTER_CHECKER_LIGHT : BUSTER_RASTER_CHECKER_DARK;
                u8 const* sample = 0;
                f64 sample_x = ((f64)x + 0.5 - view.x) / view.zoom;
                if (sample_x >= 0.0 && sample_y >= 0.0 && sample_x < display_width && sample_y < display_height)
                {
                    u32 dx = (u32)sample_x;
                    u32 dy = (u32)sample_y;
                    u32 sx = dx;
                    u32 sy = dy;
                    switch (source.orientation)
                    {
                    case 1: break;
                    case 2: sx = source.width - 1u - dx; break;
                    case 3: sx = source.width - 1u - dx; sy = source.height - 1u - dy; break;
                    case 4: sy = source.height - 1u - dy; break;
                    case 5: sx = dy; sy = dx; break;
                    case 6: sx = dy; sy = source.height - 1u - dx; break;
                    case 7: sx = source.width - 1u - dy; sy = source.height - 1u - dx; break;
                    case 8: sx = source.width - 1u - dy; sy = dx; break;
                    default: break;
                    }
                    sample = source.pixels.pointer + (u64)sy * source.stride + (u64)sx * 4;
                }
                for (u32 channel = 0; channel < 3; channel += 1)
                {
                    output[(u64)x * 4 + channel] = sample ? rendering_raster_composite_channel(sample[channel], sample[3], background) : background;
                }
                output[(u64)x * 4 + 3] = 255;
            }
        }
    }
    return result;
}

#if BUSTER_LINUX && !defined(BUSTER_RASTER_CPU_ONLY)
#include <buster/lib/rendering/raster_xcb.c>
#else
bool rendering_raster_initialize(RenderingRasterPresenter* presenter, WmNativeSurface surface)
{
    BUSTER_UNUSED(presenter);
    BUSTER_UNUSED(surface);
    return false;
}
bool rendering_raster_present(RenderingRasterPresenter* presenter, RenderingRasterCanvas canvas)
{
    BUSTER_UNUSED(presenter);
    BUSTER_UNUSED(canvas);
    return false;
}
bool rendering_raster_deinitialize(RenderingRasterPresenter* presenter)
{
    bool result = presenter != 0 && !presenter->initialized;
    return result;
}
#if BUSTER_INCLUDE_TESTS
bool rendering_raster_readback_matches_for_test(RenderingRasterPresenter* presenter, RenderingRasterCanvas canvas)
{
    BUSTER_UNUSED(presenter);
    BUSTER_UNUSED(canvas);
    return false;
}
#endif
#endif
