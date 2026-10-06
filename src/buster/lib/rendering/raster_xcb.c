// Native XCB raster presenter. No decoded/canvas pointer is retained.
// Setup inspects the actual drawable visual/depth/scanline representation;
// present marshals checked request chunks and ends with a server round trip.
#include <buster/lib/rendering/raster_internal.h>
#define BUSTER_RASTER_XCB_CHUNK_BYTES ((u32)65536)
#define BUSTER_RASTER_XCB_REQUEST_OVERHEAD ((u64)64)

BUSTER_GLOBAL_LOCAL bool rendering_raster_xcb_mask_is_contiguous(u32 mask)
{
    u32 remaining = mask;
    bool result = remaining != 0;
    if (result)
    {
        while (!(remaining & 1u))
        {
            remaining >>= 1;
        }
        result = (remaining & (remaining + 1u)) == 0;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL u32 rendering_raster_xcb_channel(u8 sample, u32 mask)
{
    u32 shift = 0;
    u32 maximum = mask;
    while (!(maximum & 1u))
    {
        maximum >>= 1;
        shift += 1;
    }
    u32 scaled = (u32)(((u64)sample * maximum + 127u) / 255u);
    return (scaled << shift) & mask;
}

BUSTER_GLOBAL_LOCAL u32 rendering_raster_xcb_pixel(RenderingRasterPresenter* presenter, u8 const* rgba)
{
    u32 pixel = rendering_raster_xcb_channel(rgba[0], presenter->red_mask) |
                rendering_raster_xcb_channel(rgba[1], presenter->green_mask) |
                rendering_raster_xcb_channel(rgba[2], presenter->blue_mask);
    return pixel;
}

BUSTER_GLOBAL_LOCAL u32 rendering_raster_xcb_row_bytes(RenderingRasterPresenter* presenter, u32 width)
{
    u32 bits = width * presenter->bits_per_pixel;
    u32 pad = presenter->scanline_pad;
    return ((bits + pad - 1u) / pad) * (pad / 8u);
}

bool rendering_raster_initialize(RenderingRasterPresenter* presenter, WmNativeSurface surface)
{
    bool result = false;
    RenderingRasterPresenter candidate = {0};
    xcb_get_window_attributes_reply_t* attributes = 0;
    xcb_get_geometry_reply_t* geometry = 0;
    xcb_generic_error_t* error = 0;
    xcb_connection_t* connection = (xcb_connection_t*)surface.display;
    bool admitted = presenter && !presenter->initialized && surface.kind == WM_NATIVE_SURFACE_XCB && connection &&
                    (u64)(uintptr_t)surface.window <= UINT32_MAX && surface.window && !xcb_connection_has_error(connection);
    if (admitted)
    {
        xcb_window_t window = (xcb_window_t)(uintptr_t)surface.window;
        attributes = xcb_get_window_attributes_reply(connection, xcb_get_window_attributes(connection, window), &error);
        admitted = attributes != 0 && error == 0;
        free(error);
        error = 0;
        if (admitted)
        {
            geometry = xcb_get_geometry_reply(connection, xcb_get_geometry(connection, window), &error);
            admitted = geometry != 0 && error == 0;
            free(error);
            error = 0;
        }
        if (admitted)
        {
            const xcb_setup_t* setup = xcb_get_setup(connection);
            candidate.surface = surface;
            candidate.depth = geometry->depth;
            candidate.byte_order = setup->image_byte_order;
            bool visual_found = false;
            for (xcb_screen_iterator_t screen = xcb_setup_roots_iterator(setup); screen.rem && !visual_found; xcb_screen_next(&screen))
            {
                for (xcb_depth_iterator_t depth = xcb_screen_allowed_depths_iterator(screen.data); depth.rem && !visual_found; xcb_depth_next(&depth))
                {
                    for (xcb_visualtype_iterator_t visual = xcb_depth_visuals_iterator(depth.data); visual.rem && !visual_found; xcb_visualtype_next(&visual))
                    {
                        if (visual.data->visual_id == attributes->visual && visual.data->_class == XCB_VISUAL_CLASS_TRUE_COLOR)
                        {
                            candidate.red_mask = visual.data->red_mask;
                            candidate.green_mask = visual.data->green_mask;
                            candidate.blue_mask = visual.data->blue_mask;
                            visual_found = depth.data->depth == geometry->depth;
                        }
                    }
                }
            }
            for (xcb_format_iterator_t format = xcb_setup_pixmap_formats_iterator(setup); format.rem; xcb_format_next(&format))
            {
                if (format.data->depth == candidate.depth)
                {
                    candidate.bits_per_pixel = format.data->bits_per_pixel;
                    candidate.scanline_pad = format.data->scanline_pad;
                }
            }
            u64 masks = (u64)candidate.red_mask | candidate.green_mask | candidate.blue_mask;
            admitted = visual_found && (candidate.bits_per_pixel == 16 || candidate.bits_per_pixel == 24 || candidate.bits_per_pixel == 32) &&
                       (candidate.scanline_pad == 8 || candidate.scanline_pad == 16 || candidate.scanline_pad == 32) &&
                       candidate.byte_order <= XCB_IMAGE_ORDER_MSB_FIRST && rendering_raster_xcb_mask_is_contiguous(candidate.red_mask) &&
                       rendering_raster_xcb_mask_is_contiguous(candidate.green_mask) && rendering_raster_xcb_mask_is_contiguous(candidate.blue_mask) &&
                       !(candidate.red_mask & candidate.green_mask) && !(candidate.red_mask & candidate.blue_mask) &&
                       !(candidate.green_mask & candidate.blue_mask) && masks < ((u64)1 << candidate.bits_per_pixel);
        }
        if (admitted)
        {
            candidate.gc = xcb_generate_id(connection);
            admitted = candidate.gc != 0;
            if (admitted)
            {
                error = xcb_request_check(connection, xcb_create_gc_checked(connection, candidate.gc, window, 0, 0));
                admitted = error == 0 && !xcb_connection_has_error(connection);
                free(error);
                error = 0;
            }
        }
        if (admitted)
        {
            candidate.initialized = true;
            *presenter = candidate;
            result = true;
        }
    }
    free(attributes);
    free(geometry);
    return result;
}

bool rendering_raster_present(RenderingRasterPresenter* presenter, RenderingRasterCanvas canvas)
{
    bool result = presenter && presenter->initialized && rendering_raster_canvas_is_valid(canvas);
    if (result)
    {
        xcb_connection_t* connection = (xcb_connection_t*)presenter->surface.display;
        xcb_window_t window = (xcb_window_t)(uintptr_t)presenter->surface.window;
        u32 row_bytes = rendering_raster_xcb_row_bytes(presenter, canvas.width);
        u64 request_bytes = (u64)xcb_get_maximum_request_length(connection) * 4;
        u64 payload_bytes = request_bytes > BUSTER_RASTER_XCB_REQUEST_OVERHEAD ? request_bytes - BUSTER_RASTER_XCB_REQUEST_OVERHEAD : 0;
        if (payload_bytes > BUSTER_RASTER_XCB_CHUNK_BYTES)
        {
            payload_bytes = BUSTER_RASTER_XCB_CHUNK_BYTES;
        }
        result = !xcb_connection_has_error(connection) && payload_bytes >= row_bytes;
        u32 rows_per_request = result ? (u32)(payload_bytes / row_bytes) : 0;
        u32 bytes_per_pixel = presenter->bits_per_pixel / 8u;
        u8 native_pixels[BUSTER_RASTER_XCB_CHUNK_BYTES];
        xcb_void_cookie_t cookies[BUSTER_RASTER_MAX_DIMENSION];
        u32 cookie_count = 0;
        for (u32 y = 0; result && y < canvas.height;)
        {
            u32 rows = canvas.height - y;
            if (rows > rows_per_request)
            {
                rows = rows_per_request;
            }
            u32 chunk_bytes = rows * row_bytes;
            memset(native_pixels, 0, chunk_bytes);
            for (u32 row = 0; row < rows; row += 1)
            {
                u8 const* input = canvas.pixels.pointer + (u64)(y + row) * canvas.stride;
                u8* output = native_pixels + (u64)row * row_bytes;
                for (u32 x = 0; x < canvas.width; x += 1)
                {
                    u32 pixel = rendering_raster_xcb_pixel(presenter, input + (u64)x * 4);
                    for (u32 byte = 0; byte < bytes_per_pixel; byte += 1)
                    {
                        u32 shift = presenter->byte_order == XCB_IMAGE_ORDER_LSB_FIRST ? byte * 8u : (bytes_per_pixel - 1u - byte) * 8u;
                        output[(u64)x * bytes_per_pixel + byte] = (u8)(pixel >> shift);
                    }
                }
            }
            cookies[cookie_count] = xcb_put_image_checked(connection, XCB_IMAGE_FORMAT_Z_PIXMAP, window, presenter->gc,
                                                        (u16)canvas.width, (u16)rows, 0, (s16)y, 0, presenter->depth,
                                                        chunk_bytes, native_pixels);
            cookie_count += 1;
            y += rows;
            result = !xcb_connection_has_error(connection);
        }
        // All checked requests precede this round trip. Their errors are then
        // available without a round trip for each chunk.
        if (cookie_count && result)
        {
            xcb_generic_error_t* error = 0;
            xcb_get_input_focus_reply_t* sync = xcb_get_input_focus_reply(connection, xcb_get_input_focus(connection), &error);
            result = sync != 0 && error == 0 && !xcb_connection_has_error(connection);
            free(error);
            free(sync);
        }
        for (u32 index = 0; index < cookie_count; index += 1)
        {
            xcb_generic_error_t* error = xcb_request_check(connection, cookies[index]);
            result = result && error == 0 && !xcb_connection_has_error(connection);
            free(error);
        }
    }
    return result;
}

bool rendering_raster_deinitialize(RenderingRasterPresenter* presenter)
{
    bool result = presenter != 0;
    if (presenter && presenter->initialized)
    {
        xcb_connection_t* connection = (xcb_connection_t*)presenter->surface.display;
        xcb_generic_error_t* error = xcb_request_check(connection, xcb_free_gc_checked(connection, presenter->gc));
        result = error == 0 && !xcb_connection_has_error(connection);
        free(error);
        *presenter = (RenderingRasterPresenter){0};
    }
    return result;
}

#if BUSTER_INCLUDE_TESTS
bool rendering_raster_readback_matches_for_test(RenderingRasterPresenter* presenter, RenderingRasterCanvas canvas)
{
    bool result = presenter && presenter->initialized && rendering_raster_canvas_is_valid(canvas) && canvas.width <= 256 && canvas.height <= 256;
    if (result)
    {
        xcb_connection_t* connection = (xcb_connection_t*)presenter->surface.display;
        xcb_window_t window = (xcb_window_t)(uintptr_t)presenter->surface.window;
        xcb_generic_error_t* error = 0;
        xcb_get_image_reply_t* reply = xcb_get_image_reply(connection,
            xcb_get_image(connection, XCB_IMAGE_FORMAT_Z_PIXMAP, window, 0, 0, (u16)canvas.width, (u16)canvas.height, UINT32_MAX), &error);
        result = reply && !error;
        u32 masks[3] = {0};
        u32 bits_per_pixel = 0;
        u32 scanline_pad = 0;
        u32 byte_order = 0;
        u32 row_bytes = 0;
        if (result)
        {
            // The readback oracle obtains the server's visual/format again; it
            // deliberately does not reuse the presenter's cached masks or packer.
            const xcb_setup_t* setup = xcb_get_setup(connection);
            byte_order = setup->image_byte_order;
            bool found = false;
            for (xcb_screen_iterator_t screen = xcb_setup_roots_iterator(setup); screen.rem && !found; xcb_screen_next(&screen))
            {
                for (xcb_depth_iterator_t depth = xcb_screen_allowed_depths_iterator(screen.data); depth.rem && !found; xcb_depth_next(&depth))
                {
                    for (xcb_visualtype_iterator_t visual = xcb_depth_visuals_iterator(depth.data); visual.rem && !found; xcb_visualtype_next(&visual))
                    {
                        if (visual.data->visual_id == reply->visual && visual.data->_class == XCB_VISUAL_CLASS_TRUE_COLOR)
                        {
                            masks[0] = visual.data->red_mask;
                            masks[1] = visual.data->green_mask;
                            masks[2] = visual.data->blue_mask;
                            found = masks[0] && masks[1] && masks[2];
                        }
                    }
                }
            }
            for (xcb_format_iterator_t format = xcb_setup_pixmap_formats_iterator(setup); format.rem; xcb_format_next(&format))
            {
                if (format.data->depth == reply->depth)
                {
                    bits_per_pixel = format.data->bits_per_pixel;
                    scanline_pad = format.data->scanline_pad;
                }
            }
            result = found && (bits_per_pixel == 16 || bits_per_pixel == 24 || bits_per_pixel == 32) &&
                     (scanline_pad == 8 || scanline_pad == 16 || scanline_pad == 32) && byte_order <= XCB_IMAGE_ORDER_MSB_FIRST;
            if (result)
            {
                row_bytes = ((canvas.width * bits_per_pixel + scanline_pad - 1u) / scanline_pad) * (scanline_pad / 8u);
                int length = xcb_get_image_data_length(reply);
                result = length >= 0 && (u64)length >= (u64)row_bytes * canvas.height;
            }
        }
        if (result)
        {
            u8 const* pixels = xcb_get_image_data(reply);
            u32 bytes_per_pixel = bits_per_pixel / 8u;
            for (u32 y = 0; y < canvas.height && result; y += 1)
            {
                for (u32 x = 0; x < canvas.width && result; x += 1)
                {
                    u32 actual = 0;
                    for (u32 byte = 0; byte < bytes_per_pixel; byte += 1)
                    {
                        u32 shift = byte_order == XCB_IMAGE_ORDER_LSB_FIRST ? byte * 8u : (bytes_per_pixel - 1u - byte) * 8u;
                        actual |= (u32)pixels[(u64)y * row_bytes + (u64)x * bytes_per_pixel + byte] << shift;
                    }
                    u8 const* expected = canvas.pixels.pointer + (u64)y * canvas.stride + (u64)x * 4;
                    for (u32 channel = 0; channel < 3 && result; channel += 1)
                    {
                        u32 shift = 0;
                        while (!((masks[channel] >> shift) & 1u))
                        {
                            shift += 1;
                        }
                        u32 maximum = masks[channel] >> shift;
                        u32 observed_level = (actual & masks[channel]) >> shift;
                        u32 expected_level = (u32)(((u64)expected[channel] * maximum + 127u) / 255u);
                        result = observed_level == expected_level;
                    }
                }
            }
        }
        free(error);
        free(reply);
    }
    return result;
}
#endif
