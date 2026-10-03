// BMP/DIB decoder implementation.
//
// Windows CORE/INFO/V2/V3/V4/V5 headers are admitted. Indexed scanlines,
// direct RGB/bitfields scanlines and the two BMP RLE machines all write the
// same top-left RGBA8 surface.
//
// Layout map:
//   ImageBmpHeader, image_bmp_parse_header  DIB/mask/palette source layout
//   image_bmp_decode_rows                  indexed/direct scanline expansion
//   image_bmp_decode_rle                   RLE4/RLE8 state machine
//   image_bmp_process                      public codec seam
#include <buster/lib/image/internal.h>

enum
{
    IMAGE_BMP_FILE_HEADER_SIZE = 14,
    IMAGE_BMP_RGB = 0,
    IMAGE_BMP_RLE8 = 1,
    IMAGE_BMP_RLE4 = 2,
    IMAGE_BMP_BITFIELDS = 3,
    IMAGE_BMP_ALPHA_BITFIELDS = 6,
};

typedef struct ImageBmpHeader ImageBmpHeader;
struct ImageBmpHeader
{
    u64 pixel_offset;
    u64 pixel_data_limit;
    u64 palette_offset;
    u64 row_stride;
    u32 width;
    u32 height;
    u32 compression;
    u32 palette_entries;
    u32 masks[4];
    u16 bits_per_pixel;
    u8 palette_stride;
    u8 source_channels;
    u8 source_bits;
    bool top_down;
    bool indexed;
    bool rle;
    bool has_alpha;
};

typedef struct ImageBmpPixel ImageBmpPixel;
struct ImageBmpPixel
{
    u8 r;
    u8 g;
    u8 b;
    u8 a;
};

BUSTER_GLOBAL_LOCAL u32 image_bmp_mask_shift(u32 mask)
{
    u32 result = 0;
    while (mask && !(mask & 1u))
    {
        mask >>= 1u;
        result += 1u;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL u32 image_bmp_mask_width(u32 mask)
{
    u32 result = 0;
    mask >>= image_bmp_mask_shift(mask);
    while (mask & 1u)
    {
        mask >>= 1u;
        result += 1u;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool image_bmp_mask_valid(u32 mask, u16 bits_per_pixel)
{
    u32 shift = image_bmp_mask_shift(mask);
    u32 shifted = mask >> shift;
    bool contiguous = shifted && !(shifted & (shifted + 1u));
    bool result = contiguous && (bits_per_pixel == 32 || !(mask & ~((1u << bits_per_pixel) - 1u)));
    return result;
}

BUSTER_GLOBAL_LOCAL u8 image_bmp_mask_sample(u32 value, u32 mask)
{
    u32 shift = image_bmp_mask_shift(mask);
    u32 shifted_mask = mask >> shift;
    u32 sample = (value & mask) >> shift;
    // image_bmp_validate_masks rejects zero masks; the guard keeps the
    // division visibly safe for callers that bypass header validation.
    u8 result = shifted_mask ? (u8)(((u64)sample * 255u + shifted_mask / 2u) / shifted_mask) : 0;
    return result;
}

BUSTER_GLOBAL_LOCAL bool image_bmp_palette_pixel(ImageDecodeContext* context, ImageBmpHeader const* header, u32 index, u64 error_offset,
                                                 ImageBmpPixel* pixel)
{
    bool result = index < header->palette_entries;
    if (!result)
    {
        image_decode_error(context, IMAGE_DECODE_MALFORMED, error_offset);
    }
    if (result)
    {
        u8 const* entry = context->encoded.pointer + header->palette_offset + (u64)index * header->palette_stride;
        *pixel = (ImageBmpPixel){.r = entry[2], .g = entry[1], .b = entry[0], .a = 255};
    }
    return result;
}

BUSTER_GLOBAL_LOCAL void image_bmp_store_pixel(ImageDecodeContext* context, ImageBmpHeader const* header, u32 file_x, u32 file_y, ImageBmpPixel pixel)
{
    u32 y = file_y;
    if (!header->top_down)
    {
        y = header->height - 1u - file_y;
    }
    u8* destination = context->image.pixels.pointer + ((u64)y * header->width + file_x) * 4u;
    destination[0] = pixel.r;
    destination[1] = pixel.g;
    destination[2] = pixel.b;
    destination[3] = pixel.a;
}

BUSTER_GLOBAL_LOCAL ImageBmpPixel image_bmp_direct_pixel(u8 const* source, ImageBmpHeader const* header)
{
    ImageBmpPixel result = {.a = 255};
    if (header->bits_per_pixel == 24)
    {
        result.r = source[2];
        result.g = source[1];
        result.b = source[0];
    }
    else
    {
        u32 value = header->bits_per_pixel == 16 ? image_decode_u16_le(source) : image_decode_u32_le(source);
        result.r = image_bmp_mask_sample(value, header->masks[0]);
        result.g = image_bmp_mask_sample(value, header->masks[1]);
        result.b = image_bmp_mask_sample(value, header->masks[2]);
        if (header->has_alpha)
        {
            result.a = image_bmp_mask_sample(value, header->masks[3]);
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool image_bmp_decode_rows(ImageDecodeContext* context, ImageBmpHeader const* header)
{
    u64 pixel_count = (u64)header->width * header->height;
    bool result = image_decode_add_work(context, pixel_count, header->pixel_offset);
    for (u32 y = 0; result && y < header->height; y += 1)
    {
        u8 const* row = context->encoded.pointer + header->pixel_offset + (u64)y * header->row_stride;
        for (u32 x = 0; result && x < header->width; x += 1)
        {
            ImageBmpPixel pixel = {0};
            if (header->indexed)
            {
                u32 index = 0;
                if (header->bits_per_pixel == 1)
                {
                    index = ((u32)row[x / 8u] >> (7u - (x & 7u))) & 1u;
                }
                else if (header->bits_per_pixel == 4)
                {
                    index = ((u32)row[x / 2u] >> ((x & 1u) ? 0u : 4u)) & 15u;
                }
                else
                {
                    index = row[x];
                }
                result = image_bmp_palette_pixel(context, header, index, header->pixel_offset + (u64)y * header->row_stride +
                                                                           ((u64)x * header->bits_per_pixel) / 8u,
                                                 &pixel);
            }
            else
            {
                u32 pixel_bytes = header->bits_per_pixel / 8u;
                pixel = image_bmp_direct_pixel(row + (u64)x * pixel_bytes, header);
            }
            if (result)
            {
                image_bmp_store_pixel(context, header, x, y, pixel);
            }
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool image_bmp_rle_write(ImageDecodeContext* context, ImageBmpHeader const* header, u32* x, u32 y, u32 index, u64 offset)
{
    ImageBmpPixel pixel = {0};
    bool result = *x < header->width && y < header->height && image_bmp_palette_pixel(context, header, index, offset, &pixel);
    if (!result && context->status == IMAGE_DECODE_SUCCESS)
    {
        image_decode_error(context, IMAGE_DECODE_MALFORMED, offset);
    }
    if (result)
    {
        image_bmp_store_pixel(context, header, *x, y, pixel);
        *x += 1u;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool image_bmp_decode_rle(ImageDecodeContext* context, ImageBmpHeader const* header)
{
    u64 pixel_count = (u64)header->width * header->height;
    bool result = image_decode_add_work(context, pixel_count, header->pixel_offset);
    if (result)
    {
        // RLE delta, EOL and early-EOB gaps are transparent rather than
        // palette index zero.
        memset(context->image.pixels.pointer, 0, (size_t)context->image.pixels.length);
    }

    ImageByteReader reader = {.context = context, .position = header->pixel_offset};
    u32 x = 0;
    u32 y = 0;
    bool ended = false;
    while (result && !ended)
    {
        u64 packet_offset = reader.position;
        u8 count = 0;
        u8 command = 0;
        if (reader.position > header->pixel_data_limit || 2u > header->pixel_data_limit - reader.position)
        {
            image_decode_error(context, IMAGE_DECODE_TRUNCATED, reader.position);
            result = false;
        }
        if (result)
        {
            result = image_reader_u8(&reader, &count) && image_reader_u8(&reader, &command);
        }
        if (result)
        {
            result = image_decode_count_limit(context, IMAGE_EXCEEDED_LIMIT_BLOCKS, 1, packet_offset);
        }
        if (result)
        {
            result = image_decode_add_work(context, 1, packet_offset);
        }
        if (result && count)
        {
            if (y >= header->height || (u32)count > header->width - x)
            {
                image_decode_error(context, IMAGE_DECODE_MALFORMED, packet_offset);
                result = false;
            }
            for (u32 index = 0; result && index < count; index += 1)
            {
                u32 palette_index = header->compression == IMAGE_BMP_RLE8 ? command : ((u32)command >> ((index & 1u) ? 0u : 4u)) & 15u;
                result = image_bmp_rle_write(context, header, &x, y, palette_index, packet_offset + 1u);
            }
        }
        else if (result && command == 0)
        {
            x = 0;
            y += 1u;
            if (y > header->height)
            {
                image_decode_error(context, IMAGE_DECODE_MALFORMED, packet_offset);
                result = false;
            }
        }
        else if (result && command == 1)
        {
            ended = true;
        }
        else if (result && command == 2)
        {
            u8 dx = 0;
            u8 dy = 0;
            if (reader.position > header->pixel_data_limit || 2u > header->pixel_data_limit - reader.position)
            {
                image_decode_error(context, IMAGE_DECODE_TRUNCATED, reader.position);
                result = false;
            }
            if (result)
            {
                result = image_reader_u8(&reader, &dx) && image_reader_u8(&reader, &dy);
            }
            if (result && (y >= header->height || dx > header->width - x || dy >= header->height - y || x + dx >= header->width))
            {
                image_decode_error(context, IMAGE_DECODE_MALFORMED, packet_offset);
                result = false;
            }
            if (result)
            {
                x += dx;
                y += dy;
            }
        }
        else if (result)
        {
            u32 absolute_count = command;
            u32 byte_count = header->compression == IMAGE_BMP_RLE8 ? absolute_count : (absolute_count + 1u) / 2u;
            u32 padded_count = (byte_count + 1u) & ~1u;
            ByteSlice bytes = {0};
            if (reader.position > header->pixel_data_limit || padded_count > header->pixel_data_limit - reader.position)
            {
                image_decode_error(context, IMAGE_DECODE_TRUNCATED, reader.position);
                result = false;
            }
            if (result)
            {
                bytes = image_reader_slice(&reader, padded_count);
                result = bytes.length == padded_count;
            }
            if (result && (y >= header->height || absolute_count > header->width - x))
            {
                image_decode_error(context, IMAGE_DECODE_MALFORMED, packet_offset);
                result = false;
            }
            for (u32 index = 0; result && index < absolute_count; index += 1)
            {
                u32 palette_index = header->compression == IMAGE_BMP_RLE8 ? bytes.pointer[index]
                                                                          : ((u32)bytes.pointer[index / 2u] >> ((index & 1u) ? 0u : 4u)) & 15u;
                result = image_bmp_rle_write(context, header, &x, y, palette_index, packet_offset + 2u + index / 2u);
            }
        }
    }
    if (result && !ended)
    {
        image_decode_error(context, IMAGE_DECODE_TRUNCATED, reader.position);
        result = false;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool image_bmp_validate_masks(ImageDecodeContext* context, ImageBmpHeader* header, u64 offset)
{
    bool result = image_bmp_mask_valid(header->masks[0], header->bits_per_pixel) && image_bmp_mask_valid(header->masks[1], header->bits_per_pixel) &&
                  image_bmp_mask_valid(header->masks[2], header->bits_per_pixel);
    u32 overlap = (header->masks[0] & header->masks[1]) | (header->masks[0] & header->masks[2]) | (header->masks[1] & header->masks[2]);
    result = result && !overlap;
    if (result && header->masks[3])
    {
        result = image_bmp_mask_valid(header->masks[3], header->bits_per_pixel) &&
                 !(header->masks[3] & (header->masks[0] | header->masks[1] | header->masks[2]));
    }
    if (!result)
    {
        image_decode_error(context, IMAGE_DECODE_MALFORMED, offset);
    }
    if (result)
    {
        header->has_alpha = header->masks[3] != 0;
        u32 bits = BUSTER_MAX(image_bmp_mask_width(header->masks[0]), image_bmp_mask_width(header->masks[1]));
        bits = BUSTER_MAX(bits, image_bmp_mask_width(header->masks[2]));
        bits = BUSTER_MAX(bits, image_bmp_mask_width(header->masks[3]));
        header->source_bits = (u8)bits;
        header->source_channels = (u8)(header->has_alpha ? 4 : 3);
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool image_bmp_parse_header(ImageDecodeContext* context, ImageBmpHeader* header)
{
    bool result = header && image_decode_range(context->encoded, 0, IMAGE_BMP_FILE_HEADER_SIZE + 4u);
    if (!result)
    {
        image_decode_error(context, IMAGE_DECODE_TRUNCATED, context->encoded.length);
    }
    u32 file_size = 0;
    u32 pixel_offset = 0;
    u32 dib_size = 0;
    if (result)
    {
        u8 const* bytes = context->encoded.pointer;
        file_size = image_decode_u32_le(bytes + 2);
        pixel_offset = image_decode_u32_le(bytes + 10);
        dib_size = image_decode_u32_le(bytes + 14);
        if (bytes[0] != 'B' || bytes[1] != 'M')
        {
            image_decode_error(context, IMAGE_DECODE_MALFORMED, 0);
            result = false;
        }
        u16 reserved1 = image_decode_u16_le(bytes + 6);
        u16 reserved2 = image_decode_u16_le(bytes + 8);
        if (result && (reserved1 || reserved2))
        {
            image_decode_error(context, IMAGE_DECODE_MALFORMED, reserved1 ? 6 : 8);
            result = false;
        }
        if (result && file_size && file_size > context->encoded.length)
        {
            image_decode_error(context, IMAGE_DECODE_TRUNCATED, context->encoded.length);
            result = false;
        }
        if (result && (pixel_offset > context->encoded.length || (file_size && pixel_offset > file_size)))
        {
            image_decode_error(context, pixel_offset > context->encoded.length ? IMAGE_DECODE_TRUNCATED : IMAGE_DECODE_MALFORMED, 10);
            result = false;
        }
    }

    bool core = dib_size == 12;
    bool info = dib_size == 40 || dib_size == 52 || dib_size == 56 || dib_size == 108 || dib_size == 124;
    if (result && !core && !info)
    {
        image_decode_set_unsupported_feature(context, IMAGE_UNSUPPORTED_FEATURE_PROFILE, 14);
        result = false;
    }
    if (result && !image_decode_range(context->encoded, IMAGE_BMP_FILE_HEADER_SIZE, dib_size))
    {
        image_decode_error(context, IMAGE_DECODE_TRUNCATED, context->encoded.length);
        result = false;
    }

    u32 width = 0;
    u32 height = 0;
    u16 planes = 0;
    u16 bits_per_pixel = 0;
    u32 compression = IMAGE_BMP_RGB;
    u32 image_size = 0;
    u32 colors_used = 0;
    bool top_down = false;
    if (result && core)
    {
        width = image_decode_u16_le(context->encoded.pointer + 18);
        height = image_decode_u16_le(context->encoded.pointer + 20);
        planes = image_decode_u16_le(context->encoded.pointer + 22);
        bits_per_pixel = image_decode_u16_le(context->encoded.pointer + 24);
    }
    else if (result)
    {
        u32 width_raw = image_decode_u32_le(context->encoded.pointer + 18);
        u32 height_raw = image_decode_u32_le(context->encoded.pointer + 22);
        planes = image_decode_u16_le(context->encoded.pointer + 26);
        bits_per_pixel = image_decode_u16_le(context->encoded.pointer + 28);
        compression = image_decode_u32_le(context->encoded.pointer + 30);
        image_size = image_decode_u32_le(context->encoded.pointer + 34);
        colors_used = image_decode_u32_le(context->encoded.pointer + 46);
        if (!width_raw || width_raw > INT32_MAX || !height_raw || height_raw == UINT32_C(0x80000000))
        {
            image_decode_error(context, IMAGE_DECODE_MALFORMED, !width_raw || width_raw > INT32_MAX ? 18 : 22);
            result = false;
        }
        if (result)
        {
            width = width_raw;
            top_down = (height_raw & UINT32_C(0x80000000)) != 0;
            height = top_down ? (~height_raw) + 1u : height_raw;
        }
    }
    if (result && planes != 1)
    {
        image_decode_error(context, IMAGE_DECODE_MALFORMED, core ? 22 : 26);
        result = false;
    }
    bool indexed = bits_per_pixel == 1 || bits_per_pixel == 4 || bits_per_pixel == 8;
    bool direct = bits_per_pixel == 16 || bits_per_pixel == 24 || bits_per_pixel == 32;
    bool rle = compression == IMAGE_BMP_RLE4 || compression == IMAGE_BMP_RLE8;
    if (result && !indexed && !direct)
    {
        image_decode_set_unsupported_feature(context, IMAGE_UNSUPPORTED_FEATURE_PRECISION, core ? 24 : 28);
        result = false;
    }
    if (result && ((compression == IMAGE_BMP_RLE8 && bits_per_pixel != 8) || (compression == IMAGE_BMP_RLE4 && bits_per_pixel != 4) ||
                   ((compression == IMAGE_BMP_BITFIELDS || compression == IMAGE_BMP_ALPHA_BITFIELDS) &&
                    (bits_per_pixel != 16 && bits_per_pixel != 32)) ||
                   (compression != IMAGE_BMP_RGB && compression != IMAGE_BMP_RLE8 && compression != IMAGE_BMP_RLE4 &&
                    compression != IMAGE_BMP_BITFIELDS && compression != IMAGE_BMP_ALPHA_BITFIELDS) ||
                   (bits_per_pixel == 24 && compression != IMAGE_BMP_RGB) || (indexed && compression != IMAGE_BMP_RGB && !rle)))
    {
        image_decode_set_unsupported_feature(context, IMAGE_UNSUPPORTED_FEATURE_CODING, 30);
        result = false;
    }
    if (result && core && compression != IMAGE_BMP_RGB)
    {
        image_decode_set_unsupported_feature(context, IMAGE_UNSUPPORTED_FEATURE_CODING, 14);
        result = false;
    }
    if (result && top_down && rle)
    {
        image_decode_error(context, IMAGE_DECODE_MALFORMED, 22);
        result = false;
    }

    u64 header_end = IMAGE_BMP_FILE_HEADER_SIZE + (u64)dib_size;
    u64 external_mask_bytes = 0;
    ImageBmpHeader parsed = {
        .pixel_offset = pixel_offset,
        .pixel_data_limit = file_size ? file_size : context->encoded.length,
        .width = width,
        .height = height,
        .compression = compression,
        .bits_per_pixel = bits_per_pixel,
        .top_down = top_down,
        .indexed = indexed,
        .rle = rle,
        .source_channels = (u8)(indexed ? 1 : 3),
        .source_bits = (u8)(indexed ? bits_per_pixel : bits_per_pixel >= 24 ? 8 : 5),
        .has_alpha = rle,
    };
    if (result && direct)
    {
        if (compression == IMAGE_BMP_RGB)
        {
            if (bits_per_pixel == 16)
            {
                parsed.masks[0] = 0x7c00u;
                parsed.masks[1] = 0x03e0u;
                parsed.masks[2] = 0x001fu;
            }
            else
            {
                parsed.masks[0] = 0x00ff0000u;
                parsed.masks[1] = 0x0000ff00u;
                parsed.masks[2] = 0x000000ffu;
            }
        }
        else if (dib_size >= 52)
        {
            parsed.masks[0] = image_decode_u32_le(context->encoded.pointer + 54);
            parsed.masks[1] = image_decode_u32_le(context->encoded.pointer + 58);
            parsed.masks[2] = image_decode_u32_le(context->encoded.pointer + 62);
            parsed.masks[3] = dib_size >= 56 ? image_decode_u32_le(context->encoded.pointer + 66) : 0;
        }
        else
        {
            external_mask_bytes = compression == IMAGE_BMP_ALPHA_BITFIELDS ? 16u : 12u;
            if (!image_decode_range(context->encoded, header_end, external_mask_bytes))
            {
                image_decode_error(context, IMAGE_DECODE_TRUNCATED, context->encoded.length);
                result = false;
            }
            if (result)
            {
                parsed.masks[0] = image_decode_u32_le(context->encoded.pointer + header_end);
                parsed.masks[1] = image_decode_u32_le(context->encoded.pointer + header_end + 4u);
                parsed.masks[2] = image_decode_u32_le(context->encoded.pointer + header_end + 8u);
                parsed.masks[3] = external_mask_bytes == 16 ? image_decode_u32_le(context->encoded.pointer + header_end + 12u) : 0;
            }
        }
        if (result && compression == IMAGE_BMP_ALPHA_BITFIELDS && !parsed.masks[3])
        {
            image_decode_error(context, IMAGE_DECODE_MALFORMED, header_end);
            result = false;
        }
        if (result)
        {
            result = image_bmp_validate_masks(context, &parsed, compression == IMAGE_BMP_RGB ? 28 : header_end);
        }
    }

    parsed.palette_offset = header_end + external_mask_bytes;
    parsed.palette_stride = (u8)(core ? 3 : 4);
    if (result && indexed)
    {
        u32 maximum_entries = 1u << bits_per_pixel;
        parsed.palette_entries = colors_used ? colors_used : maximum_entries;
        if (!parsed.palette_entries || parsed.palette_entries > maximum_entries)
        {
            image_decode_error(context, IMAGE_DECODE_MALFORMED, 46);
            result = false;
        }
        u64 palette_bytes = (u64)parsed.palette_entries * parsed.palette_stride;
        if (result && (!image_decode_range(context->encoded, parsed.palette_offset, palette_bytes) ||
                       parsed.palette_offset + palette_bytes > pixel_offset))
        {
            image_decode_error(context, parsed.palette_offset + palette_bytes > context->encoded.length ? IMAGE_DECODE_TRUNCATED : IMAGE_DECODE_MALFORMED,
                               parsed.palette_offset);
            result = false;
        }
    }
    if (result && parsed.palette_offset > pixel_offset)
    {
        image_decode_error(context, IMAGE_DECODE_MALFORMED, 10);
        result = false;
    }
    if (result && rle && image_size)
    {
        u64 image_end = (u64)pixel_offset + image_size;
        if (image_end > context->encoded.length)
        {
            image_decode_error(context, IMAGE_DECODE_TRUNCATED, context->encoded.length);
            result = false;
        }
        else if (file_size && image_end > file_size)
        {
            image_decode_error(context, IMAGE_DECODE_MALFORMED, 34);
            result = false;
        }
        else
        {
            parsed.pixel_data_limit = image_end;
        }
    }
    if (result && !rle)
    {
        parsed.row_stride = ((((u64)width * bits_per_pixel) + 31u) / 32u) * 4u;
        u64 image_bytes = parsed.row_stride * height;
        u64 data_limit = file_size ? file_size : context->encoded.length;
        if (!image_decode_range(context->encoded, pixel_offset, image_bytes) || image_bytes > data_limit - pixel_offset)
        {
            image_decode_error(context, pixel_offset + image_bytes > context->encoded.length ? IMAGE_DECODE_TRUNCATED : IMAGE_DECODE_MALFORMED, pixel_offset);
            result = false;
        }
    }
    if (result)
    {
        *header = parsed;
    }
    return result;
}

void image_bmp_process(ImageDecodeContext* context)
{
    ImageBmpHeader header = {0};
    bool valid = image_bmp_parse_header(context, &header);
    if (valid)
    {
        valid = image_decode_set_information(context, header.width, header.height, 1, header.source_channels, header.source_bits, header.has_alpha,
                                             IMAGE_ORIENTATION_TOP_LEFT, 18);
        if (valid)
        {
            context->information.source_color_model = header.indexed ? IMAGE_COLOR_MODEL_INDEXED
                                                                      : header.has_alpha ? IMAGE_COLOR_MODEL_RGBA : IMAGE_COLOR_MODEL_RGB;
        }
    }
    if (valid && context->decode_pixels)
    {
        valid = image_decode_allocate_pixels(context, header.width, header.height, header.pixel_offset);
        if (valid)
        {
            if (header.rle)
            {
                image_bmp_decode_rle(context, &header);
            }
            else
            {
                image_bmp_decode_rows(context, &header);
            }
        }
    }
}
