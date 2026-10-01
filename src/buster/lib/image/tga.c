// TGA decoder implementation.
//
// Supported image records are color-mapped, true-color and grayscale, in
// their raw and packet-RLE forms. Both origin bits are normalized to the
// public top-left output convention.
//
// Layout map:
//   ImageTgaHeader, image_tga_parse_header  footer/header and source layout
//   image_tga_read_pixel                   palette/direct sample expansion
//   image_tga_decode_pixels                raw/RLE packet stream and origins
//   image_tga_process                      public codec seam
#include <buster/lib/image/internal.h>

#define IMAGE_TGA_HEADER_SIZE 18u
#define IMAGE_TGA_FOOTER_SIZE 26u
#define IMAGE_TGA_EXTENSION_SIZE 495u
#define IMAGE_TGA_COLOR_CORRECTION_SIZE 2048u
#define IMAGE_TGA_DEVELOPER_ENTRY_SIZE 10u
#define IMAGE_TGA_DETECT_MAX_RLE_PACKETS UINT64_C(1048576)

typedef struct ImageTgaHeader ImageTgaHeader;
struct ImageTgaHeader
{
    u64 color_map_offset;
    u64 pixel_offset;
    u64 pixel_data_limit;
    u64 attributes_type_offset;
    u32 color_map_first;
    u32 color_map_length;
    u32 width;
    u32 height;
    u8 image_type;
    u8 pixel_bits;
    u8 color_map_bits;
    u8 descriptor;
    u8 source_channels;
    u8 source_bits;
    bool rle;
    bool indexed;
    bool grayscale;
    bool has_alpha;
    bool premultiplied_alpha;
};

typedef struct ImageTgaFooter ImageTgaFooter;
struct ImageTgaFooter
{
    u64 data_limit;
    u64 attributes_type_offset;
    u64 error_offset;
    u8 attributes_type;
    bool version_two;
};

typedef struct ImageTgaRange ImageTgaRange;
struct ImageTgaRange
{
    u64 offset;
    u64 size;
};

typedef struct ImageTgaPixel ImageTgaPixel;
struct ImageTgaPixel
{
    u8 r;
    u8 g;
    u8 b;
    u8 a;
};

BUSTER_GLOBAL_LOCAL bool image_tga_validate_auxiliary_range(ImageTgaRange range, u64 footer_offset, u64* data_limit)
{
    bool result = data_limit && range.offset <= footer_offset &&
                  (!range.size ||
                   (range.offset >= IMAGE_TGA_HEADER_SIZE && range.size <= footer_offset - range.offset));
    if (result && range.size)
    {
        *data_limit = BUSTER_MIN(*data_limit, range.offset);
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool image_tga_inspect_footer(ByteSlice encoded, ImageDecodeContext* context, ImageTgaFooter* footer)
{
    static u8 const signature[18] = {'T', 'R', 'U', 'E', 'V', 'I', 'S', 'I', 'O', 'N', '-', 'X', 'F', 'I', 'L', 'E', '.', 0};
    ImageTgaFooter parsed = {.data_limit = encoded.length};
    bool result = footer != 0 && (!encoded.length || encoded.pointer);
    bool version_two = result && encoded.length >= IMAGE_TGA_FOOTER_SIZE &&
                       memcmp(encoded.pointer + encoded.length - sizeof(signature), signature, sizeof(signature)) == 0;
    if (version_two)
    {
        u64 footer_offset = encoded.length - IMAGE_TGA_FOOTER_SIZE;
        u64 extension_offset = image_decode_u32_le(encoded.pointer + footer_offset);
        u64 developer_offset = image_decode_u32_le(encoded.pointer + footer_offset + 4u);
        parsed.version_two = true;
        parsed.data_limit = footer_offset;
        parsed.error_offset = footer_offset;

        if (extension_offset)
        {
            result = extension_offset >= IMAGE_TGA_HEADER_SIZE && extension_offset <= footer_offset &&
                     IMAGE_TGA_EXTENSION_SIZE <= footer_offset - extension_offset &&
                     image_decode_u16_le(encoded.pointer + extension_offset) == IMAGE_TGA_EXTENSION_SIZE;
            if (result)
            {
                parsed.attributes_type_offset = extension_offset + IMAGE_TGA_EXTENSION_SIZE - 1u;
                parsed.attributes_type = encoded.pointer[parsed.attributes_type_offset];
                parsed.error_offset = parsed.attributes_type_offset;
                result = parsed.attributes_type <= 4;
                parsed.data_limit = BUSTER_MIN(parsed.data_limit, extension_offset);
            }
        }
        if (result && developer_offset)
        {
            parsed.error_offset = footer_offset + 4u;
            result = developer_offset >= IMAGE_TGA_HEADER_SIZE && developer_offset <= footer_offset &&
                     2u <= footer_offset - developer_offset;
            if (result)
            {
                u64 entry_count = image_decode_u16_le(encoded.pointer + developer_offset);
                u64 directory_size = 2u + entry_count * IMAGE_TGA_DEVELOPER_ENTRY_SIZE;
                result = directory_size <= footer_offset - developer_offset;
                if (result)
                {
                    parsed.data_limit = BUSTER_MIN(parsed.data_limit, developer_offset);
                }
            }
        }
        if (result && extension_offset)
        {
            u64 color_correction_offset = image_decode_u32_le(encoded.pointer + extension_offset + 482u);
            u64 postage_offset = image_decode_u32_le(encoded.pointer + extension_offset + 486u);
            u64 scan_line_offset = image_decode_u32_le(encoded.pointer + extension_offset + 490u);
            ImageTgaRange color_correction = {
                .offset = color_correction_offset,
                .size = color_correction_offset ? IMAGE_TGA_COLOR_CORRECTION_SIZE : 0,
            };
            ImageTgaRange postage = {.offset = postage_offset};
            ImageTgaRange scan_lines = {
                .offset = scan_line_offset,
                .size = scan_line_offset ? (u64)image_decode_u16_le(encoded.pointer + 14u) * 4u : 0,
            };

            if (color_correction_offset)
            {
                parsed.error_offset = extension_offset + 482u;
                result = image_tga_validate_auxiliary_range(color_correction, footer_offset, &parsed.data_limit);
            }
            if (result && postage_offset)
            {
                parsed.error_offset = extension_offset + 486u;
                result = postage_offset >= IMAGE_TGA_HEADER_SIZE && postage_offset <= footer_offset &&
                         2u <= footer_offset - postage_offset;
                if (result)
                {
                    u64 stamp_width = encoded.pointer[postage_offset];
                    u64 stamp_height = encoded.pointer[postage_offset + 1u];
                    u64 sample_bytes = ((u64)encoded.pointer[16] + 7u) / 8u;
                    postage.size = 2u + stamp_width * stamp_height * sample_bytes;
                    result = image_tga_validate_auxiliary_range(postage, footer_offset, &parsed.data_limit);
                }
            }
            if (result && scan_line_offset)
            {
                parsed.error_offset = extension_offset + 490u;
                result = image_tga_validate_auxiliary_range(scan_lines, footer_offset, &parsed.data_limit);
            }
            if (result && postage_offset && scan_line_offset)
            {
                parsed.error_offset = extension_offset + 486u;
                result = scan_lines.offset + scan_lines.size <= postage.offset;
            }
        }
        if (result && developer_offset)
        {
            u64 entry_count = image_decode_u16_le(encoded.pointer + developer_offset);
            for (u64 entry_index = 0; result && entry_index < entry_count; entry_index += 1u)
            {
                u64 entry_offset = developer_offset + 2u + entry_index * IMAGE_TGA_DEVELOPER_ENTRY_SIZE;
                if (context)
                {
                    result = image_decode_add_work(context, 1, entry_offset);
                }
                if (result)
                {
                    ImageTgaRange field = {
                        .offset = image_decode_u32_le(encoded.pointer + entry_offset + 2u),
                        .size = image_decode_u32_le(encoded.pointer + entry_offset + 6u),
                    };
                    parsed.error_offset = entry_offset + 2u;
                    result = image_tga_validate_auxiliary_range(field, footer_offset, &parsed.data_limit);
                }
            }
        }
    }
    if (footer)
    {
        *footer = parsed;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool image_tga_detect_rle(ByteSlice encoded, u64 offset, u64 limit, u64 pixel_count, u64 sample_bytes)
{
    u64 output = 0;
    u64 packet_count = 0;
    bool result = offset <= limit && limit <= encoded.length;
    while (result && output < pixel_count && packet_count < IMAGE_TGA_DETECT_MAX_RLE_PACKETS)
    {
        result = offset < limit;
        u8 packet = result ? encoded.pointer[offset] : 0;
        u64 count = (u64)(packet & 0x7fu) + 1u;
        u64 samples = (packet & 0x80u) ? 1u : count;
        if (result)
        {
            offset += 1u;
            result = count <= pixel_count - output && samples <= (limit - offset) / sample_bytes;
        }
        if (result)
        {
            offset += samples * sample_bytes;
            output += count;
            packet_count += 1u;
        }
    }
    result = result && output == pixel_count;
    return result;
}

BUSTER_GLOBAL_LOCAL bool image_tga_attributes_valid(ImageTgaFooter const* footer, bool alpha_capable, u8 expected_attributes,
                                                     u8 attribute_bits)
{
    bool result = footer != 0;
    if (result && footer->attributes_type_offset)
    {
        if (footer->attributes_type == 0)
        {
            result = attribute_bits == 0;
        }
        else if (footer->attributes_type == 1)
        {
            result = alpha_capable && (attribute_bits == 0 || attribute_bits == expected_attributes);
        }
        else
        {
            result = alpha_capable && attribute_bits == expected_attributes;
        }
    }
    else if (result)
    {
        result = alpha_capable ? attribute_bits == 0 || attribute_bits == expected_attributes : attribute_bits == 0;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool image_tga_attributes_have_alpha(ImageTgaFooter const* footer, u8 attribute_bits)
{
    bool result = attribute_bits != 0;
    if (footer && footer->attributes_type_offset)
    {
        result = footer->attributes_type >= 2;
    }
    return result;
}

bool image_tga_detect(ByteSlice encoded)
{
    ImageTgaFooter footer = {0};
    bool result = encoded.pointer && encoded.length >= IMAGE_TGA_HEADER_SIZE && image_tga_inspect_footer(encoded, 0, &footer);
    if (result)
    {
        u8 const* bytes = encoded.pointer;
        u8 id_length = bytes[0];
        u8 color_map_type = bytes[1];
        u8 image_type = bytes[2];
        u32 color_map_first = image_decode_u16_le(bytes + 3);
        u32 color_map_length = image_decode_u16_le(bytes + 5);
        u8 color_map_bits = bytes[7];
        u32 width = image_decode_u16_le(bytes + 12);
        u32 height = image_decode_u16_le(bytes + 14);
        u8 pixel_bits = bytes[16];
        u8 descriptor = bytes[17];
        bool indexed = image_type == 1 || image_type == 9;
        bool true_color = image_type == 2 || image_type == 10;
        bool grayscale = image_type == 3 || image_type == 11;
        bool rle = image_type == 9 || image_type == 10 || image_type == 11;
        bool alpha_capable = (indexed && (color_map_bits == 16 || color_map_bits == 32)) ||
                             (true_color && (pixel_bits == 16 || pixel_bits == 32)) || (grayscale && pixel_bits == 16);
        u8 expected_attributes = alpha_capable && (grayscale || pixel_bits == 32 || color_map_bits == 32) ? 8u : alpha_capable ? 1u : 0u;
        u8 attribute_bits = descriptor & 15u;

        result = (indexed || true_color || grayscale) && width && height && !(descriptor & 0xc0u) &&
                 ((indexed && color_map_type == 1) || (!indexed && color_map_type == 0));
        result = result && (!indexed || ((pixel_bits == 8 || pixel_bits == 16) && color_map_length &&
                                          (color_map_bits == 15 || color_map_bits == 16 || color_map_bits == 24 || color_map_bits == 32) &&
                                          color_map_first + color_map_length <= (pixel_bits == 8 ? 256u : 65536u)));
        result = result && (!true_color || pixel_bits == 15 || pixel_bits == 16 || pixel_bits == 24 || pixel_bits == 32);
        result = result && (!grayscale || pixel_bits == 8 || pixel_bits == 16);
        result = result && (indexed || (!color_map_first && !color_map_length && !color_map_bits));
        result = result && image_tga_attributes_valid(&footer, alpha_capable, expected_attributes, attribute_bits);
        if (result)
        {
            u64 color_map_offset = IMAGE_TGA_HEADER_SIZE + (u64)id_length;
            u64 color_map_bytes = indexed ? (u64)color_map_length * (((u64)color_map_bits + 7u) / 8u) : 0;
            u64 pixel_offset = color_map_offset + color_map_bytes;
            u64 sample_bytes = ((u64)pixel_bits + 7u) / 8u;
            u64 pixel_count = (u64)width * height;
            result = color_map_offset <= footer.data_limit && color_map_bytes <= footer.data_limit - color_map_offset &&
                     pixel_offset <= footer.data_limit;
            if (result && rle)
            {
                result = image_tga_detect_rle(encoded, pixel_offset, footer.data_limit, pixel_count, sample_bytes);
            }
            else if (result)
            {
                result = pixel_count <= (footer.data_limit - pixel_offset) / sample_bytes;
            }
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL u8 image_tga_expand_5(u32 value)
{
    u8 result = (u8)((value << 3u) | (value >> 2u));
    return result;
}

BUSTER_GLOBAL_LOCAL bool image_tga_decode_color(u8 const* bytes, u8 bits, bool alpha, ImageTgaPixel* pixel)
{
    bool result = pixel != 0;
    if (result && (bits == 15 || bits == 16))
    {
        u16 value = image_decode_u16_le(bytes);
        pixel->r = image_tga_expand_5((value >> 10u) & 31u);
        pixel->g = image_tga_expand_5((value >> 5u) & 31u);
        pixel->b = image_tga_expand_5(value & 31u);
        pixel->a = alpha ? ((value & 0x8000u) ? 255 : 0) : 255;
    }
    else if (result && bits == 24)
    {
        pixel->r = bytes[2];
        pixel->g = bytes[1];
        pixel->b = bytes[0];
        pixel->a = 255;
    }
    else if (result && bits == 32)
    {
        pixel->r = bytes[2];
        pixel->g = bytes[1];
        pixel->b = bytes[0];
        pixel->a = alpha ? bytes[3] : 255;
    }
    else
    {
        result = false;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool image_tga_read_pixel(ImageByteReader* reader, ImageTgaHeader const* header, ImageTgaPixel* pixel)
{
    bool result = reader && header && pixel;
    u64 sample_offset = result ? reader->position : 0;
    u64 sample_bytes = result ? ((u64)header->pixel_bits + 7u) / 8u : 0;
    if (result && (sample_offset > header->pixel_data_limit || sample_bytes > header->pixel_data_limit - sample_offset))
    {
        image_decode_error(reader->context, IMAGE_DECODE_TRUNCATED, header->pixel_data_limit);
        result = false;
    }
    if (result && header->indexed)
    {
        u32 index = 0;
        if (header->pixel_bits == 8)
        {
            u8 value = 0;
            result = image_reader_u8(reader, &value);
            index = value;
        }
        else
        {
            u16 value = 0;
            result = image_reader_u16_le(reader, &value);
            index = value;
        }
        if (result && (index < header->color_map_first || index - header->color_map_first >= header->color_map_length))
        {
            image_decode_error(reader->context, IMAGE_DECODE_MALFORMED, sample_offset);
            result = false;
        }
        if (result)
        {
            u64 entry_bytes = ((u64)header->color_map_bits + 7u) / 8u;
            u64 offset = header->color_map_offset + (u64)(index - header->color_map_first) * entry_bytes;
            result = image_tga_decode_color(reader->context->encoded.pointer + offset, header->color_map_bits, header->has_alpha, pixel);
        }
    }
    else if (result && header->grayscale)
    {
        result = image_reader_u8(reader, &pixel->r);
        if (result)
        {
            pixel->g = pixel->r;
            pixel->b = pixel->r;
            pixel->a = 255;
        }
        if (result && header->pixel_bits == 16)
        {
            u8 alpha = 0;
            result = image_reader_u8(reader, &alpha);
            if (result && header->has_alpha)
            {
                pixel->a = alpha;
            }
        }
    }
    else if (result)
    {
        u64 byte_count = ((u64)header->pixel_bits + 7u) / 8u;
        ByteSlice sample = image_reader_slice(reader, byte_count);
        result = sample.length == byte_count && image_tga_decode_color(sample.pointer, header->pixel_bits, header->has_alpha, pixel);
    }
    return result;
}

BUSTER_GLOBAL_LOCAL void image_tga_store_pixel(ImageDecodeContext* context, ImageTgaHeader const* header, u64 file_index, ImageTgaPixel pixel)
{
    u32 file_x = (u32)(file_index % header->width);
    u32 file_y = (u32)(file_index / header->width);
    u32 x = file_x;
    u32 y = file_y;
    if (header->descriptor & 0x10u)
    {
        x = header->width - 1u - file_x;
    }
    if (!(header->descriptor & 0x20u))
    {
        y = header->height - 1u - file_y;
    }
    u8* destination = context->image.pixels.pointer + ((u64)y * header->width + x) * 4u;
    destination[0] = pixel.r;
    destination[1] = pixel.g;
    destination[2] = pixel.b;
    destination[3] = pixel.a;
}

BUSTER_GLOBAL_LOCAL bool image_tga_decode_pixels(ImageDecodeContext* context, ImageTgaHeader const* header)
{
    ImageByteReader reader = {.context = context, .position = header->pixel_offset};
    u64 pixel_count = (u64)header->width * header->height;
    u64 output = 0;
    bool result = image_decode_add_work(context, pixel_count, header->pixel_offset);
    while (result && output < pixel_count)
    {
        u32 count = 1;
        bool repeated = false;
        u64 packet_offset = reader.position;
        if (header->rle)
        {
            u8 packet = 0;
            if (reader.position >= header->pixel_data_limit)
            {
                image_decode_error(context, IMAGE_DECODE_TRUNCATED, header->pixel_data_limit);
                result = false;
            }
            if (result)
            {
                result = image_reader_u8(&reader, &packet);
            }
            if (result)
            {
                result = image_decode_count_limit(context, IMAGE_EXCEEDED_LIMIT_BLOCKS, 1, packet_offset);
            }
            if (result)
            {
                repeated = (packet & 0x80u) != 0;
                count = (u32)(packet & 0x7fu) + 1u;
                if ((u64)count > pixel_count - output)
                {
                    image_decode_error(context, IMAGE_DECODE_MALFORMED, packet_offset);
                    result = false;
                }
            }
        }
        ImageTgaPixel repeated_pixel = {0};
        if (result && repeated)
        {
            result = image_tga_read_pixel(&reader, header, &repeated_pixel);
        }
        for (u32 index = 0; result && index < count; index += 1)
        {
            ImageTgaPixel pixel = repeated_pixel;
            if (!repeated)
            {
                result = image_tga_read_pixel(&reader, header, &pixel);
            }
            if (result)
            {
                image_tga_store_pixel(context, header, output, pixel);
                output += 1;
            }
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool image_tga_parse_header(ImageDecodeContext* context, ImageTgaHeader* header)
{
    ImageTgaFooter footer = {0};
    bool result = header && image_decode_range(context->encoded, 0, IMAGE_TGA_HEADER_SIZE);
    if (!result)
    {
        image_decode_error(context, IMAGE_DECODE_TRUNCATED, context->encoded.length);
    }
    if (result && !image_tga_inspect_footer(context->encoded, context, &footer))
    {
        if (context->status == IMAGE_DECODE_SUCCESS)
        {
            image_decode_error(context, IMAGE_DECODE_MALFORMED, footer.error_offset);
        }
        result = false;
    }
    if (result)
    {
        u8 const* bytes = context->encoded.pointer;
        u8 id_length = bytes[0];
        u8 color_map_type = bytes[1];
        u16 color_map_first = image_decode_u16_le(bytes + 3);
        u16 color_map_length = image_decode_u16_le(bytes + 5);
        u8 color_map_bits = bytes[7];
        u16 width = image_decode_u16_le(bytes + 12);
        u16 height = image_decode_u16_le(bytes + 14);
        u8 image_type = bytes[2];
        u8 pixel_bits = bytes[16];
        u8 descriptor = bytes[17];
        bool indexed = image_type == 1 || image_type == 9;
        bool true_color = image_type == 2 || image_type == 10;
        bool grayscale = image_type == 3 || image_type == 11;
        bool rle = image_type == 9 || image_type == 10 || image_type == 11;
        u8 attribute_bits = descriptor & 15u;

        if (!indexed && !true_color && !grayscale)
        {
            image_decode_set_unsupported_feature(context, IMAGE_UNSUPPORTED_FEATURE_CODING, 2);
            result = false;
        }
        if (result && descriptor & 0xc0u)
        {
            image_decode_set_unsupported_feature(context, IMAGE_UNSUPPORTED_FEATURE_INTERLACE, 17);
            result = false;
        }
        if (result && ((indexed && color_map_type != 1) || (!indexed && color_map_type != 0)))
        {
            image_decode_error(context, IMAGE_DECODE_MALFORMED, 1);
            result = false;
        }
        if (result && !indexed && (color_map_first || color_map_length || color_map_bits))
        {
            image_decode_error(context, IMAGE_DECODE_MALFORMED, 3);
            result = false;
        }
        if (result && indexed && ((pixel_bits != 8 && pixel_bits != 16) || !color_map_length ||
                                  (color_map_bits != 15 && color_map_bits != 16 && color_map_bits != 24 && color_map_bits != 32)))
        {
            image_decode_set_unsupported_feature(context, IMAGE_UNSUPPORTED_FEATURE_PRECISION, 7);
            result = false;
        }
        if (result && true_color && pixel_bits != 15 && pixel_bits != 16 && pixel_bits != 24 && pixel_bits != 32)
        {
            image_decode_set_unsupported_feature(context, IMAGE_UNSUPPORTED_FEATURE_PRECISION, 16);
            result = false;
        }
        if (result && grayscale && pixel_bits != 8 && pixel_bits != 16)
        {
            image_decode_set_unsupported_feature(context, IMAGE_UNSUPPORTED_FEATURE_PRECISION, 16);
            result = false;
        }

        bool alpha_capable = (indexed && (color_map_bits == 16 || color_map_bits == 32)) ||
                             (true_color && (pixel_bits == 16 || pixel_bits == 32)) || (grayscale && pixel_bits == 16);
        u8 expected_attributes = alpha_capable && (grayscale || pixel_bits == 32 || color_map_bits == 32) ? 8u : alpha_capable ? 1u : 0u;
        bool has_alpha = image_tga_attributes_have_alpha(&footer, attribute_bits);
        if (result && !image_tga_attributes_valid(&footer, alpha_capable, expected_attributes, attribute_bits))
        {
            if (footer.attributes_type_offset)
            {
                image_decode_error(context, IMAGE_DECODE_MALFORMED, 17);
            }
            else
            {
                image_decode_set_unsupported_feature(context, IMAGE_UNSUPPORTED_FEATURE_COLOR_TRANSFORM, 17);
            }
            result = false;
        }

        u64 color_map_offset = IMAGE_TGA_HEADER_SIZE + (u64)id_length;
        u64 color_map_bytes = indexed ? (u64)color_map_length * (((u64)color_map_bits + 7u) / 8u) : 0;
        u64 pixel_offset = color_map_offset + color_map_bytes;
        if (result && (color_map_offset > footer.data_limit || color_map_bytes > footer.data_limit - color_map_offset))
        {
            image_decode_error(context, IMAGE_DECODE_TRUNCATED, footer.data_limit);
            result = false;
        }
        if (result && indexed && (u32)color_map_first + (u32)color_map_length > (pixel_bits == 8 ? 256u : 65536u))
        {
            image_decode_error(context, IMAGE_DECODE_MALFORMED, 3);
            result = false;
        }

        if (result)
        {
            *header = (ImageTgaHeader){
                .color_map_offset = color_map_offset,
                .pixel_offset = pixel_offset,
                .pixel_data_limit = footer.data_limit,
                .attributes_type_offset = footer.attributes_type_offset,
                .color_map_first = color_map_first,
                .color_map_length = color_map_length,
                .width = width,
                .height = height,
                .image_type = image_type,
                .pixel_bits = pixel_bits,
                .color_map_bits = color_map_bits,
                .descriptor = descriptor,
                .source_channels = indexed ? 1u : grayscale ? (u8)(has_alpha ? 2 : 1) : (u8)(has_alpha ? 4 : 3),
                .source_bits = indexed ? pixel_bits
                                       : (u8)((pixel_bits == 15 || pixel_bits == 16) && !grayscale ? 5 : 8),
                .rle = rle,
                .indexed = indexed,
                .grayscale = grayscale,
                .has_alpha = has_alpha,
                .premultiplied_alpha = footer.attributes_type_offset && footer.attributes_type == 4,
            };
        }
    }
    return result;
}

void image_tga_process(ImageDecodeContext* context)
{
    ImageTgaHeader header = {0};
    bool valid = image_tga_parse_header(context, &header);
    if (valid && header.premultiplied_alpha)
    {
        image_decode_set_unsupported_feature(context, IMAGE_UNSUPPORTED_FEATURE_COLOR_TRANSFORM, header.attributes_type_offset);
        valid = false;
    }
    if (valid)
    {
        valid = image_decode_set_information(context, header.width, header.height, 1, header.source_channels, header.source_bits, header.has_alpha,
                                             IMAGE_ORIENTATION_TOP_LEFT, 12);
        if (valid)
        {
            context->information.source_color_model = header.indexed       ? IMAGE_COLOR_MODEL_INDEXED
                                                      : header.grayscale    ? header.has_alpha ? IMAGE_COLOR_MODEL_GRAYSCALE_ALPHA
                                                                                              : IMAGE_COLOR_MODEL_GRAYSCALE
                                                      : header.has_alpha    ? IMAGE_COLOR_MODEL_RGBA
                                                                            : IMAGE_COLOR_MODEL_RGB;
        }
    }
    if (valid && !header.rle)
    {
        u64 source_bytes = ((u64)header.pixel_bits + 7u) / 8u;
        u64 pixel_bytes = (u64)header.width * header.height * source_bytes;
        if (header.pixel_offset > header.pixel_data_limit || pixel_bytes > header.pixel_data_limit - header.pixel_offset)
        {
            image_decode_error(context, IMAGE_DECODE_TRUNCATED, header.pixel_data_limit);
            valid = false;
        }
    }
    if (valid && context->decode_pixels)
    {
        valid = image_decode_allocate_pixels(context, header.width, header.height, header.pixel_offset);
        if (valid)
        {
            image_tga_decode_pixels(context, &header);
        }
    }
}
