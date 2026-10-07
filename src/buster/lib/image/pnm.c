// Portable anymap decoder implementation.
//
// P1 through P6 share a comment-aware token scanner. PAM (P7) uses bounded
// header lines and the same integer rules, then maps the standard tuple types
// to the public RGBA8 representation.
//
// Layout map:
//   ImagePnmScanner, image_pnm_token       bounded ASCII tokens/comments
//   image_pnm_parse_pam                    P7 key/value header grammar
//   image_pnm_parse_header                 P1--P7 source metadata
//   image_pnm_bit, image_pnm_read_ascii    ASCII raster validation/expansion
//   image_pnm_decode_binary                packed and multi-byte raster decode
//   image_pnm_find_additional_image        concatenated-image summary
//   image_pnm_process                      public codec seam
#include <buster/lib/image/internal.h>

typedef struct ImagePnmScanner ImagePnmScanner;
struct ImagePnmScanner
{
    ImageDecodeContext* context;
    u64 position;
    bool last_separator;
};

typedef struct ImagePnmHeader ImagePnmHeader;
struct ImagePnmHeader
{
    u64 pixel_offset;
    u32 width;
    u32 height;
    u32 max_value;
    u8 variant;
    u8 channels;
    u8 source_bits;
    ImageColorModel color_model;
    bool ascii;
    bool pbm;
    bool has_alpha;
};

BUSTER_GLOBAL_LOCAL bool image_pnm_space(u8 byte)
{
    bool result = byte == ' ' || byte == '\t' || byte == '\r' || byte == '\n' || byte == '\v' || byte == '\f';
    return result;
}

BUSTER_GLOBAL_LOCAL bool image_pnm_advance(ImagePnmScanner* scanner, u64 amount)
{
    bool result = scanner && scanner->context && image_decode_range(scanner->context->encoded, scanner->position, amount);
    if (result)
    {
        result = image_decode_add_work(scanner->context, amount, scanner->position);
    }
    if (result)
    {
        scanner->position += amount;
    }
    else if (scanner && scanner->context && scanner->context->status == IMAGE_DECODE_SUCCESS)
    {
        image_decode_error(scanner->context, IMAGE_DECODE_TRUNCATED, scanner->position);
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool image_pnm_skip_comment(ImagePnmScanner* scanner)
{
    bool result = scanner && scanner->position < scanner->context->encoded.length && scanner->context->encoded.pointer[scanner->position] == '#';
    while (result && scanner->position < scanner->context->encoded.length && scanner->context->encoded.pointer[scanner->position] != '\n' &&
           scanner->context->encoded.pointer[scanner->position] != '\r')
    {
        result = image_pnm_advance(scanner, 1);
    }
    // Netpbm ends a comment at CR or LF. Only that one byte is consumed, so a
    // CR-terminated comment never swallows a following LF-valued sample.
    if (result && scanner->position < scanner->context->encoded.length)
    {
        result = image_pnm_advance(scanner, 1);
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool image_pnm_skip_space_and_comments(ImagePnmScanner* scanner)
{
    bool result = scanner && scanner->context;
    bool progress = true;
    while (result && progress)
    {
        progress = false;
        while (result && scanner->position < scanner->context->encoded.length &&
               image_pnm_space(scanner->context->encoded.pointer[scanner->position]))
        {
            result = image_pnm_advance(scanner, 1);
            progress = true;
        }
        if (result && scanner->position < scanner->context->encoded.length && scanner->context->encoded.pointer[scanner->position] == '#')
        {
            result = image_pnm_skip_comment(scanner);
            progress = true;
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL ByteSlice image_pnm_token(ImagePnmScanner* scanner)
{
    ByteSlice result = {0};
    scanner->last_separator = false;
    bool valid = image_pnm_skip_space_and_comments(scanner);
    u64 start = scanner->position;
    while (valid && scanner->position < scanner->context->encoded.length)
    {
        u8 byte = scanner->context->encoded.pointer[scanner->position];
        if (image_pnm_space(byte) || byte == '#')
        {
            break;
        }
        valid = image_pnm_advance(scanner, 1);
    }
    if (valid && scanner->position > start)
    {
        result = (ByteSlice){.pointer = scanner->context->encoded.pointer + start, .length = scanner->position - start};
        if (scanner->position < scanner->context->encoded.length)
        {
            scanner->last_separator = true;
            if (scanner->context->encoded.pointer[scanner->position] == '#')
            {
                valid = image_pnm_skip_comment(scanner);
            }
            else
            {
                // Netpbm allows exactly one whitespace byte between the last
                // binary header token and the raster, so a CR is never paired
                // with a following LF: that byte may be the first sample.
                valid = image_pnm_advance(scanner, 1);
            }
        }
    }
    else if (valid)
    {
        image_decode_error(scanner->context, IMAGE_DECODE_TRUNCATED, scanner->position);
    }
    if (!valid)
    {
        result = (ByteSlice){0};
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool image_pnm_parse_u32(ByteSlice token, u32* value)
{
    bool result = value && token.pointer && token.length;
    u32 parsed = 0;
    for (u64 index = 0; result && index < token.length; index += 1)
    {
        u8 byte = token.pointer[index];
        if (byte < '0' || byte > '9' || parsed > (UINT32_MAX - (u32)(byte - '0')) / 10u)
        {
            result = false;
        }
        else
        {
            parsed = parsed * 10u + (u32)(byte - '0');
        }
    }
    if (result)
    {
        *value = parsed;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool image_pnm_token_u32(ImagePnmScanner* scanner, u32* value)
{
    u64 offset = scanner->position;
    ByteSlice token = image_pnm_token(scanner);
    bool result = token.length && image_pnm_parse_u32(token, value);
    if (!result && scanner->context->status == IMAGE_DECODE_SUCCESS)
    {
        image_decode_error(scanner->context, token.length ? IMAGE_DECODE_MALFORMED : IMAGE_DECODE_TRUNCATED, offset);
    }
    return result;
}

BUSTER_GLOBAL_LOCAL ByteSlice image_pnm_trim(ByteSlice line)
{
    u64 start = 0;
    u64 end = line.length;
    while (start < end && image_pnm_space(line.pointer[start]))
    {
        start += 1;
    }
    while (end > start && image_pnm_space(line.pointer[end - 1]))
    {
        end -= 1;
    }
    ByteSlice result = {.pointer = line.pointer, .length = end - start};
    if (result.pointer)
    {
        result.pointer += start;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool image_pnm_equal(ByteSlice text, char const* expected, u64 length)
{
    bool result = text.length == length && (!length || memcmp(text.pointer, expected, length) == 0);
    return result;
}

BUSTER_GLOBAL_LOCAL ByteSlice image_pnm_line(ImagePnmScanner* scanner, bool* complete)
{
    ByteSlice result = {0};
    *complete = false;
    u64 start = scanner->position;
    bool valid = true;
    while (valid && scanner->position < scanner->context->encoded.length && scanner->context->encoded.pointer[scanner->position] != '\n')
    {
        valid = image_pnm_advance(scanner, 1);
    }
    u64 end = scanner->position;
    if (end > start && scanner->context->encoded.pointer[end - 1] == '\r')
    {
        end -= 1;
    }
    if (valid && scanner->position < scanner->context->encoded.length)
    {
        valid = image_pnm_advance(scanner, 1);
        *complete = valid;
    }
    if (valid)
    {
        result = (ByteSlice){.pointer = scanner->context->encoded.pointer + start, .length = end - start};
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool image_pnm_pam_tuple(ImageDecodeContext* context, ByteSlice tuple, u32 depth, u32 max_value, ImagePnmHeader* header, u64 offset)
{
    bool result = true;
    bool black_white = image_pnm_equal(tuple, "BLACKANDWHITE", 13);
    bool black_white_alpha = image_pnm_equal(tuple, "BLACKANDWHITE_ALPHA", 19);
    bool grayscale = image_pnm_equal(tuple, "GRAYSCALE", 9);
    bool grayscale_alpha = image_pnm_equal(tuple, "GRAYSCALE_ALPHA", 15);
    bool rgb = image_pnm_equal(tuple, "RGB", 3);
    bool rgb_alpha = image_pnm_equal(tuple, "RGB_ALPHA", 9);
    if (!tuple.length)
    {
        image_decode_set_unsupported_feature(context, IMAGE_UNSUPPORTED_FEATURE_COMPONENT_MODEL, offset);
        result = false;
    }
    else if (black_white || grayscale)
    {
        header->channels = 1;
        header->color_model = IMAGE_COLOR_MODEL_GRAYSCALE;
        result = depth == 1;
    }
    else if (black_white_alpha || grayscale_alpha)
    {
        header->channels = 2;
        header->has_alpha = true;
        header->color_model = IMAGE_COLOR_MODEL_GRAYSCALE_ALPHA;
        result = depth == 2;
    }
    else if (rgb)
    {
        header->channels = 3;
        header->color_model = IMAGE_COLOR_MODEL_RGB;
        result = depth == 3;
    }
    else if (rgb_alpha)
    {
        header->channels = 4;
        header->has_alpha = true;
        header->color_model = IMAGE_COLOR_MODEL_RGBA;
        result = depth == 4;
    }
    else
    {
        image_decode_set_unsupported_feature(context, IMAGE_UNSUPPORTED_FEATURE_COMPONENT_MODEL, offset);
        result = false;
    }
    if (result && (black_white || black_white_alpha) && max_value != 1)
    {
        image_decode_error(context, IMAGE_DECODE_MALFORMED, offset);
        result = false;
    }
    if (!result && context->status == IMAGE_DECODE_SUCCESS)
    {
        image_decode_error(context, IMAGE_DECODE_MALFORMED, offset);
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool image_pnm_parse_pam(ImageDecodeContext* context, ImagePnmScanner* scanner, ImagePnmHeader* header)
{
    bool complete = false;
    ByteSlice magic_tail = image_pnm_trim(image_pnm_line(scanner, &complete));
    bool result = complete && !magic_tail.length;
    if (!result && context->status == IMAGE_DECODE_SUCCESS)
    {
        image_decode_error(context, complete ? IMAGE_DECODE_MALFORMED : IMAGE_DECODE_TRUNCATED, 2);
    }

    u32 width = 0;
    u32 height = 0;
    u32 depth = 0;
    u32 max_value = 0;
    ByteSlice tuple = {0};
    u64 tuple_offset = 2;
    bool have_width = false;
    bool have_height = false;
    bool have_depth = false;
    bool have_max = false;
    bool have_tuple = false;
    bool ended = false;
    while (result && !ended)
    {
        u64 line_offset = scanner->position;
        ByteSlice line = image_pnm_line(scanner, &complete);
        if (!complete)
        {
            image_decode_error(context, IMAGE_DECODE_TRUNCATED, line_offset);
            result = false;
        }
        if (result)
        {
            // PAM comments are whole lines; '#' inside a value (TUPLTYPE
            // RGB#custom) is part of an opaque identifier, not a comment.
            line = image_pnm_trim(line);
            if (line.length && line.pointer[0] != '#')
            {
                u64 split = 0;
                while (split < line.length && !image_pnm_space(line.pointer[split]))
                {
                    split += 1;
                }
                ByteSlice key = {.pointer = line.pointer, .length = split};
                ByteSlice value = {.pointer = line.pointer + split, .length = line.length - split};
                value = image_pnm_trim(value);
                if (image_pnm_equal(key, "ENDHDR", 6))
                {
                    ended = !value.length;
                    if (!ended)
                    {
                        image_decode_error(context, IMAGE_DECODE_MALFORMED, line_offset);
                        result = false;
                    }
                }
                else if (image_pnm_equal(key, "WIDTH", 5))
                {
                    result = !have_width && image_pnm_parse_u32(value, &width);
                    have_width = result;
                }
                else if (image_pnm_equal(key, "HEIGHT", 6))
                {
                    result = !have_height && image_pnm_parse_u32(value, &height);
                    have_height = result;
                }
                else if (image_pnm_equal(key, "DEPTH", 5))
                {
                    result = !have_depth && image_pnm_parse_u32(value, &depth);
                    have_depth = result;
                }
                else if (image_pnm_equal(key, "MAXVAL", 6))
                {
                    result = !have_max && image_pnm_parse_u32(value, &max_value);
                    have_max = result;
                }
                else if (image_pnm_equal(key, "TUPLTYPE", 8))
                {
                    result = !have_tuple && value.length;
                    if (result)
                    {
                        tuple = value;
                        tuple_offset = line_offset + (u64)(value.pointer - line.pointer);
                        have_tuple = true;
                    }
                }
                else
                {
                    image_decode_set_unsupported_feature(context, IMAGE_UNSUPPORTED_FEATURE_CONTAINER_FEATURE, line_offset);
                    result = false;
                }
                if (!result && context->status == IMAGE_DECODE_SUCCESS)
                {
                    image_decode_error(context, IMAGE_DECODE_MALFORMED, line_offset);
                }
            }
        }
    }
    if (result && (!have_width || !have_height || !have_depth || !have_max || !max_value || max_value > 65535u))
    {
        image_decode_error(context, IMAGE_DECODE_MALFORMED, 2);
        result = false;
    }
    if (result)
    {
        header->pixel_offset = scanner->position;
        header->width = width;
        header->height = height;
        header->max_value = max_value;
        header->variant = 7;
        header->source_bits = (u8)(max_value <= 255u ? 8 : 16);
        result = image_pnm_pam_tuple(context, tuple, depth, max_value, header, tuple_offset);
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool image_pnm_parse_header(ImageDecodeContext* context, ImagePnmHeader* header)
{
    bool result = header && image_decode_range(context->encoded, 0, 2);
    if (!result)
    {
        image_decode_error(context, IMAGE_DECODE_TRUNCATED, context->encoded.length);
    }
    u8 variant = result ? (u8)(context->encoded.pointer[1] - '0') : 0;
    if (result && (context->encoded.pointer[0] != 'P' || variant < 1 || variant > 7))
    {
        image_decode_error(context, IMAGE_DECODE_MALFORMED, 0);
        result = false;
    }
    if (result && (context->encoded.length == 2 || !image_pnm_space(context->encoded.pointer[2])))
    {
        image_decode_error(context, context->encoded.length == 2 ? IMAGE_DECODE_TRUNCATED : IMAGE_DECODE_MALFORMED, 2);
        result = false;
    }
    ImagePnmScanner scanner = {.context = context, .position = 2};
    if (result && variant == 7)
    {
        result = image_pnm_parse_pam(context, &scanner, header);
    }
    else if (result)
    {
        u32 width = 0;
        u32 height = 0;
        u32 max_value = 1;
        result = image_pnm_token_u32(&scanner, &width) && image_pnm_token_u32(&scanner, &height);
        if (result && variant != 1 && variant != 4)
        {
            result = image_pnm_token_u32(&scanner, &max_value);
        }
        if (result && (!max_value || max_value > 65535u))
        {
            image_decode_error(context, IMAGE_DECODE_MALFORMED, scanner.position);
            result = false;
        }
        bool ascii = variant <= 3;
        if (result && !ascii && !scanner.last_separator)
        {
            image_decode_error(context, IMAGE_DECODE_TRUNCATED, scanner.position);
            result = false;
        }
        if (result)
        {
            *header = (ImagePnmHeader){
                .pixel_offset = scanner.position,
                .width = width,
                .height = height,
                .max_value = max_value,
                .variant = variant,
                .channels = (u8)(variant == 3 || variant == 6 ? 3 : 1),
                .source_bits = (u8)(variant == 1 || variant == 4 ? 1 : max_value <= 255u ? 8 : 16),
                .color_model = variant == 3 || variant == 6 ? IMAGE_COLOR_MODEL_RGB : IMAGE_COLOR_MODEL_GRAYSCALE,
                .ascii = ascii,
                .pbm = variant == 1 || variant == 4,
            };
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL u8 image_pnm_scale(u32 value, u32 max_value)
{
    u8 result = (u8)(((u64)value * 255u + max_value / 2u) / max_value);
    return result;
}

BUSTER_GLOBAL_LOCAL void image_pnm_store(ImageDecodeContext* context, ImagePnmHeader const* header, u64 pixel_index, u32 const* samples)
{
    u8* destination = context->image.pixels.pointer + pixel_index * 4u;
    if (header->channels <= 2)
    {
        u8 gray = header->pbm ? (samples[0] ? 0 : 255) : image_pnm_scale(samples[0], header->max_value);
        destination[0] = gray;
        destination[1] = gray;
        destination[2] = gray;
        destination[3] = header->has_alpha ? image_pnm_scale(samples[1], header->max_value) : 255;
    }
    else
    {
        destination[0] = image_pnm_scale(samples[0], header->max_value);
        destination[1] = image_pnm_scale(samples[1], header->max_value);
        destination[2] = image_pnm_scale(samples[2], header->max_value);
        destination[3] = header->has_alpha ? image_pnm_scale(samples[3], header->max_value) : 255;
    }
}

// Plain PBM samples are single 0/1 characters and need no separator between
// them, so "010" is three samples rather than the integer 10.
BUSTER_GLOBAL_LOCAL bool image_pnm_bit(ImagePnmScanner* scanner, u32* value)
{
    bool result = image_pnm_skip_space_and_comments(scanner);
    if (result && scanner->position >= scanner->context->encoded.length)
    {
        image_decode_error(scanner->context, IMAGE_DECODE_TRUNCATED, scanner->position);
        result = false;
    }
    u8 byte = result ? scanner->context->encoded.pointer[scanner->position] : 0;
    if (result && byte != '0' && byte != '1')
    {
        image_decode_error(scanner->context, IMAGE_DECODE_MALFORMED, scanner->position);
        result = false;
    }
    if (result)
    {
        *value = (u32)(byte - '0');
        result = image_pnm_advance(scanner, 1);
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool image_pnm_read_ascii(ImageDecodeContext* context, ImagePnmHeader const* header, bool store_pixels, u64* end)
{
    ImagePnmScanner scanner = {.context = context, .position = header->pixel_offset};
    u64 pixel_count = (u64)header->width * header->height;
    u64 sample_count = pixel_count * header->channels;
    bool result = end && image_decode_add_work(context, sample_count, header->pixel_offset);
    for (u64 pixel = 0; result && pixel < pixel_count; pixel += 1)
    {
        u32 samples[4] = {0};
        for (u32 channel = 0; result && channel < header->channels; channel += 1)
        {
            result = header->pbm ? image_pnm_bit(&scanner, samples + channel) : image_pnm_token_u32(&scanner, samples + channel);
            if (result && samples[channel] > header->max_value)
            {
                image_decode_error(context, IMAGE_DECODE_MALFORMED, scanner.position);
                result = false;
            }
        }
        if (result && store_pixels)
        {
            image_pnm_store(context, header, pixel, samples);
        }
    }
    if (result)
    {
        *end = scanner.position;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool image_pnm_decode_binary(ImageDecodeContext* context, ImagePnmHeader const* header)
{
    u64 pixel_count = (u64)header->width * header->height;
    u64 sample_count = pixel_count * header->channels;
    bool result = image_decode_add_work(context, sample_count, header->pixel_offset);
    if (result && header->variant == 4)
    {
        u64 row_bytes = ((u64)header->width + 7u) / 8u;
        u8 const* source = context->encoded.pointer + header->pixel_offset;
        for (u32 y = 0; y < header->height; y += 1)
        {
            for (u32 x = 0; x < header->width; x += 1)
            {
                u32 samples[4] = {((u32)source[(u64)y * row_bytes + x / 8u] >> (7u - (x & 7u))) & 1u};
                image_pnm_store(context, header, (u64)y * header->width + x, samples);
            }
        }
    }
    else if (result)
    {
        u64 source = header->pixel_offset;
        u32 sample_bytes = header->max_value <= 255u ? 1u : 2u;
        for (u64 pixel = 0; result && pixel < pixel_count; pixel += 1)
        {
            u32 samples[4] = {0};
            for (u32 channel = 0; result && channel < header->channels; channel += 1)
            {
                samples[channel] = sample_bytes == 1 ? context->encoded.pointer[source] : image_decode_u16_be(context->encoded.pointer + source);
                if (samples[channel] > header->max_value)
                {
                    image_decode_error(context, IMAGE_DECODE_MALFORMED, source);
                    result = false;
                }
                source += sample_bytes;
            }
            if (result)
            {
                image_pnm_store(context, header, pixel, samples);
            }
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool image_pnm_binary_size(ImageDecodeContext* context, ImagePnmHeader const* header, u64* size)
{
    u64 result_size = 0;
    bool result = context && header && size;
    u64 remaining = 0;
    if (result && header->pixel_offset <= context->encoded.length)
    {
        remaining = context->encoded.length - header->pixel_offset;
    }
    else if (result)
    {
        image_decode_error(context, IMAGE_DECODE_TRUNCATED, context->encoded.length);
        result = false;
    }
    if (result && header->variant == 4)
    {
        u64 row_bytes = ((u64)header->width + 7u) / 8u;
        if (row_bytes && (u64)header->height > remaining / row_bytes)
        {
            image_decode_error(context, IMAGE_DECODE_TRUNCATED, context->encoded.length);
            result = false;
        }
        else
        {
            result_size = row_bytes * header->height;
        }
    }
    else if (result)
    {
        u64 pixel_count = (u64)header->width * header->height;
        u64 sample_bytes = header->max_value <= 255u ? 1u : 2u;
        u64 pixel_bytes = (u64)header->channels * sample_bytes;
        if (pixel_bytes && pixel_count > remaining / pixel_bytes)
        {
            image_decode_error(context, IMAGE_DECODE_TRUNCATED, context->encoded.length);
            result = false;
        }
        else
        {
            result_size = pixel_count * pixel_bytes;
        }
    }
    if (result)
    {
        *size = result_size;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool image_pnm_find_additional_image(ImageDecodeContext* context, u64 offset, bool plain_pbm, bool* has_more_images)
{
    ImagePnmScanner scanner = {.context = context, .position = offset};
    bool result = has_more_images && image_pnm_skip_space_and_comments(&scanner);
    bool has_more = result && scanner.position < context->encoded.length;
    if (has_more)
    {
        result = image_decode_range(context->encoded, scanner.position, 3) &&
                 context->encoded.pointer[scanner.position] == 'P' &&
                 context->encoded.pointer[scanner.position + 1u] >= '1' &&
                 context->encoded.pointer[scanner.position + 1u] <= '7' &&
                 image_pnm_space(context->encoded.pointer[scanner.position + 2u]);
        // Plain PBM permits arbitrary material after the raster when it is
        // introduced by whitespace (or a comment): it is ignored, not a frame.
        if (!result && plain_pbm && scanner.position > offset)
        {
            has_more = false;
            result = true;
        }
        if (!result && context->status == IMAGE_DECODE_SUCCESS)
        {
            image_decode_error(context, IMAGE_DECODE_MALFORMED, scanner.position);
        }
        if (result && has_more)
        {
            result = image_decode_count_limit(context, IMAGE_EXCEEDED_LIMIT_FRAMES, 1, scanner.position);
        }
    }
    if (result)
    {
        *has_more_images = has_more;
    }
    return result;
}

void image_pnm_process(ImageDecodeContext* context)
{
    ImagePnmHeader header = {0};
    u64 raster_end = 0;
    bool valid = image_pnm_parse_header(context, &header);
    if (valid)
    {
        valid = image_decode_count_limit(context, IMAGE_EXCEEDED_LIMIT_FRAMES, 1, 0);
    }
    if (valid)
    {
        valid = image_decode_set_information(context, header.width, header.height, 1, header.channels, header.source_bits, header.has_alpha,
                                             IMAGE_ORIENTATION_TOP_LEFT, 2);
        if (valid)
        {
            context->information.source_color_model = header.color_model;
        }
    }
    if (valid && !header.ascii)
    {
        u64 required = 0;
        valid = image_pnm_binary_size(context, &header, &required);
        if (valid && !image_decode_range(context->encoded, header.pixel_offset, required))
        {
            image_decode_error(context, IMAGE_DECODE_TRUNCATED, context->encoded.length);
            valid = false;
        }
        if (valid)
        {
            raster_end = header.pixel_offset + required;
        }
    }
    if (valid && context->decode_pixels)
    {
        valid = image_decode_allocate_pixels(context, header.width, header.height, header.pixel_offset);
    }
    if (valid && header.ascii)
    {
        valid = image_pnm_read_ascii(context, &header, context->decode_pixels, &raster_end);
    }
    else if (valid && context->decode_pixels)
    {
        valid = image_pnm_decode_binary(context, &header);
    }
    if (valid)
    {
        bool has_more_images = false;
        valid = image_pnm_find_additional_image(context, raster_end, header.variant == 1, &has_more_images);
        if (valid)
        {
            context->information.has_more_images = has_more_images;
        }
    }
}
