// Quite OK Image decoder implementation.
//
// The file is a fixed big-endian header followed by one stateful byte stream.
// Probe and decode share the complete opcode walk; only decode writes pixels.
// Both charge the complete pixel count before expanding any operation.
//
// Layout map:
//   image_qoi_pixel_hash      QOI's 64-entry previous-pixel index
//   image_qoi_walk            opcode expansion and exact stream termination
//   image_qoi_process         header, metadata and public codec seam
#include <buster/lib/image/internal.h>

#define IMAGE_QOI_HEADER_SIZE 14u
#define IMAGE_QOI_END_SIZE 8u

typedef struct ImageQoiPixel ImageQoiPixel;
struct ImageQoiPixel
{
    u8 r;
    u8 g;
    u8 b;
    u8 a;
};

BUSTER_GLOBAL_LOCAL u32 image_qoi_pixel_hash(ImageQoiPixel pixel)
{
    u32 result = ((u32)pixel.r * 3u + (u32)pixel.g * 5u + (u32)pixel.b * 7u + (u32)pixel.a * 11u) & 63u;
    return result;
}

BUSTER_GLOBAL_LOCAL bool image_qoi_end_marker_equal(ByteSlice encoded, u64 offset)
{
    static u8 const marker[IMAGE_QOI_END_SIZE] = {0, 0, 0, 0, 0, 0, 0, 1};
    bool result = image_decode_range(encoded, offset, sizeof(marker));
    if (result)
    {
        result = memcmp(encoded.pointer + offset, marker, sizeof(marker)) == 0;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL void image_qoi_write_pixel(ImageDecodeContext* context, u64 index, ImageQoiPixel pixel)
{
    u8* destination = context->image.pixels.pointer + index * 4u;
    destination[0] = pixel.r;
    destination[1] = pixel.g;
    destination[2] = pixel.b;
    destination[3] = pixel.a;
}

BUSTER_GLOBAL_LOCAL bool image_qoi_walk(ImageDecodeContext* context, u64 pixel_count, bool* has_alpha)
{
    ImageByteReader reader = {.context = context, .position = IMAGE_QOI_HEADER_SIZE};
    ImageQoiPixel index[64] = {0};
    ImageQoiPixel pixel = {.a = 255};
    u64 output = 0;
    u32 run = 0;
    bool result = has_alpha && image_decode_add_work(context, pixel_count, reader.position);
    while (result && output < pixel_count)
    {
        if (run)
        {
            run -= 1;
        }
        else
        {
            u64 opcode_offset = reader.position;
            u8 first = 0;
            result = image_reader_u8(&reader, &first);
            if (result)
            {
                result = image_decode_count_limit(context, IMAGE_EXCEEDED_LIMIT_BLOCKS, 1, opcode_offset);
            }
            if (result && first == 0xfe)
            {
                result = image_reader_u8(&reader, &pixel.r) && image_reader_u8(&reader, &pixel.g) && image_reader_u8(&reader, &pixel.b);
            }
            else if (result && first == 0xff)
            {
                result = image_reader_u8(&reader, &pixel.r) && image_reader_u8(&reader, &pixel.g) && image_reader_u8(&reader, &pixel.b) &&
                         image_reader_u8(&reader, &pixel.a);
            }
            else if (result)
            {
                u8 tag = first & 0xc0u;
                if (tag == 0x00u)
                {
                    pixel = index[first & 63u];
                }
                else if (tag == 0x40u)
                {
                    pixel.r = (u8)(pixel.r + ((first >> 4u) & 3u) - 2u);
                    pixel.g = (u8)(pixel.g + ((first >> 2u) & 3u) - 2u);
                    pixel.b = (u8)(pixel.b + (first & 3u) - 2u);
                }
                else if (tag == 0x80u)
                {
                    u8 second = 0;
                    result = image_reader_u8(&reader, &second);
                    if (result)
                    {
                        s32 green_delta = (s32)(first & 63u) - 32;
                        pixel.r = (u8)((s32)pixel.r + green_delta + (s32)(second >> 4u) - 8);
                        pixel.g = (u8)((s32)pixel.g + green_delta);
                        pixel.b = (u8)((s32)pixel.b + green_delta + (s32)(second & 15u) - 8);
                    }
                }
                else
                {
                    run = (u32)(first & 63u);
                    if ((u64)run >= pixel_count - output)
                    {
                        image_decode_error(context, IMAGE_DECODE_MALFORMED, reader.position - 1u);
                        result = false;
                    }
                }
            }
        }
        if (result)
        {
            index[image_qoi_pixel_hash(pixel)] = pixel;
            *has_alpha = *has_alpha || pixel.a != 255;
            if (context->decode_pixels)
            {
                image_qoi_write_pixel(context, output, pixel);
            }
            output += 1;
        }
    }
    if (result && run)
    {
        image_decode_error(context, IMAGE_DECODE_MALFORMED, reader.position);
        result = false;
    }
    if (result && (!image_qoi_end_marker_equal(context->encoded, reader.position) || reader.position + IMAGE_QOI_END_SIZE != context->encoded.length))
    {
        image_decode_error(context, IMAGE_DECODE_MALFORMED, reader.position);
        result = false;
    }
    return result;
}

void image_qoi_process(ImageDecodeContext* context)
{
    bool valid = image_decode_range(context->encoded, 0, IMAGE_QOI_HEADER_SIZE + IMAGE_QOI_END_SIZE);
    if (!valid)
    {
        image_decode_error(context, IMAGE_DECODE_TRUNCATED, context->encoded.length);
    }
    if (valid && memcmp(context->encoded.pointer, "qoif", 4) != 0)
    {
        image_decode_error(context, IMAGE_DECODE_MALFORMED, 0);
        valid = false;
    }

    u32 width = valid ? image_decode_u32_be(context->encoded.pointer + 4) : 0;
    u32 height = valid ? image_decode_u32_be(context->encoded.pointer + 8) : 0;
    u8 channels = valid ? context->encoded.pointer[12] : 0;
    u8 colorspace = valid ? context->encoded.pointer[13] : 0;
    if (valid && (channels != 3 && channels != 4))
    {
        image_decode_error(context, IMAGE_DECODE_MALFORMED, 12);
        valid = false;
    }
    if (valid && colorspace > 1)
    {
        image_decode_error(context, IMAGE_DECODE_MALFORMED, 13);
        valid = false;
    }
    if (valid && !image_qoi_end_marker_equal(context->encoded, context->encoded.length - IMAGE_QOI_END_SIZE))
    {
        image_decode_error(context, IMAGE_DECODE_MALFORMED, context->encoded.length - IMAGE_QOI_END_SIZE);
        valid = false;
    }
    if (valid)
    {
        valid = image_decode_set_information(context, width, height, 1, channels, 8, channels == 4, IMAGE_ORIENTATION_TOP_LEFT, 4);
        if (valid)
        {
            context->information.source_color_model = channels == 4 ? IMAGE_COLOR_MODEL_RGBA : IMAGE_COLOR_MODEL_RGB;
            context->information.qoi_colorspace = colorspace ? IMAGE_QOI_COLORSPACE_ALL_CHANNELS_LINEAR
                                                             : IMAGE_QOI_COLORSPACE_SRGB_LINEAR_ALPHA;
            if (!colorspace)
            {
                context->information.color_metadata_flags |= IMAGE_COLOR_METADATA_SRGB;
            }
        }
    }
    bool has_alpha = channels == 4;
    if (valid && context->decode_pixels)
    {
        valid = image_decode_allocate_pixels(context, width, height, IMAGE_QOI_HEADER_SIZE);
    }
    if (valid)
    {
        valid = image_qoi_walk(context, (u64)width * height, &has_alpha);
    }
    if (valid)
    {
        context->information.has_alpha = has_alpha;
    }
}
