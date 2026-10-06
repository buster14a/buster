// Dependency-free PNG decoder. image_png_process validates the complete PNG
// chunk stream first, including every CRC and the ordering constraints needed
// by the decoder. Probe mode stops after publishing bounded metadata. Decode
// mode then consumes the concatenated IDAT payload as one zlib stream, expands
// stored/fixed/dynamic DEFLATE blocks into the exact expected scanline image,
// validates Adler-32, reverses filters in place, and scatters ordinary or
// Adam7 samples into the public top-left RGBA8 output.
//
// Layout map (searchable symbols, not line numbers):
//   PngState, png_parse_*               chunk grammar, color and APNG metadata
//   PngCompressedInput                  split-IDAT byte stream
//   PngBitReader, PngHuffman            bounded DEFLATE machinery
//   png_inflate_zlib                    zlib header/body/Adler-32
//   png_filtered_size                   exact scanline allocation census
//   png_unfilter_row, png_expand_pixels filters, samples and Adam7 scatter
//   image_png_process                   public codec seam

#include <buster/lib/image/internal.h>

#define PNG_SIGNATURE_SIZE 8u
#define PNG_CHUNK_HEADER_SIZE 8u
#define PNG_CHUNK_CRC_SIZE 4u
#define PNG_U31_MAX UINT32_C(0x7fffffff)
#define PNG_CHUNK_MAX_LENGTH PNG_U31_MAX
#define PNG_DEFLATE_MAX_BITS 15u
#define PNG_DEFLATE_LITERAL_SYMBOL_COUNT 288u
#define PNG_DEFLATE_DISTANCE_SYMBOL_COUNT 32u
#define PNG_DEFLATE_CODE_LENGTH_SYMBOL_COUNT 19u
#define PNG_DEFLATE_DYNAMIC_LENGTH_COUNT 320u
#define PNG_ADLER_MODULUS 65521u

#define PNG_CHUNK_TYPE(a, b, c, d)                                                                                                                            \
    (((u32)(u8)(a) << 24u) | ((u32)(u8)(b) << 16u) | ((u32)(u8)(c) << 8u) | (u32)(u8)(d))

typedef struct PngChunk PngChunk;
struct PngChunk
{
    u32 type;
    u32 length;
    u64 header_offset;
    u64 data_offset;
    u64 crc_offset;
    u64 next_offset;
};

typedef struct PngState PngState;
struct PngState
{
    u32 width;
    u32 height;
    u8 bit_depth;
    u8 color_type;
    u8 compression_method;
    u8 filter_method;
    u8 interlace_method;
    u8 channel_count;
    bool seen_ihdr;
    bool seen_plte;
    bool seen_trns;
    bool seen_chrm;
    bool seen_gama;
    bool seen_iccp;
    bool seen_srgb;
    bool seen_cicp;
    bool seen_exif;
    bool seen_actl;
    bool seen_fctl;
    bool seen_idat;
    bool idat_finished;
    bool seen_iend;
    bool first_fctl_before_idat;
    bool current_frame_needs_fdat;
    bool current_frame_has_fdat;
    bool trns_has_alpha;
    u8 palette[256][4];
    u32 palette_count;
    u32 animation_frame_count;
    u32 frame_control_count;
    u16 transparent_gray;
    u16 transparent_red;
    u16 transparent_green;
    u16 transparent_blue;
    u64 ihdr_offset;
    u64 first_idat_header_offset;
    u64 idat_size;
    u64 next_animation_sequence;
    ImageColorMetadataFlags color_metadata_flags;
    ImageOrientation orientation;
};

typedef struct PngCompressedInput PngCompressedInput;
struct PngCompressedInput
{
    ImageDecodeContext* context;
    PngState const* png;
    u64 chunk_header_offset;
    u64 data_offset;
    u64 data_remaining;
    u64 next_chunk_offset;
    u64 consumed;
};

typedef struct PngBitReader PngBitReader;
struct PngBitReader
{
    PngCompressedInput* input;
    u32 bits;
    u32 bit_count;
    u64 last_byte_offset;
};

typedef struct PngHuffman PngHuffman;
struct PngHuffman
{
    u16 count[PNG_DEFLATE_MAX_BITS + 1u];
    u16 symbols[PNG_DEFLATE_LITERAL_SYMBOL_COUNT];
    u16 symbol_count;
};

typedef enum PngHuffmanKind
{
    PNG_HUFFMAN_CODE_LENGTH,
    PNG_HUFFMAN_LITERAL_LENGTH,
    PNG_HUFFMAN_DISTANCE,
} PngHuffmanKind;

typedef struct PngInflateOutput PngInflateOutput;
struct PngInflateOutput
{
    ImageDecodeContext* context;
    u8* bytes;
    u64 capacity;
    u64 count;
    u32 maximum_distance;
    u32 adler_s1;
    u32 adler_s2;
    u64 error_offset;
};

typedef struct PngPass PngPass;
struct PngPass
{
    u32 x_start;
    u32 y_start;
    u32 x_step;
    u32 y_step;
    u32 width;
    u32 height;
    u64 row_bytes;
};

BUSTER_GLOBAL_LOCAL bool png_type_character_is_valid(u8 value)
{
    bool result = (value >= 'A' && value <= 'Z') || (value >= 'a' && value <= 'z');
    return result;
}

BUSTER_GLOBAL_LOCAL bool png_type_is_critical(u32 type)
{
    bool result = ((type >> 24u) & UINT32_C(0x20)) == 0;
    return result;
}

BUSTER_GLOBAL_LOCAL u32 png_crc_update(u32 crc, u8 const* bytes, u64 size)
{
    for (u64 index = 0; index < size; index += 1)
    {
        crc ^= bytes[index];
        for (u32 bit = 0; bit < 8; bit += 1)
        {
            u32 mask = 0u - (crc & 1u);
            crc = (crc >> 1u) ^ (UINT32_C(0xedb88320) & mask);
        }
    }
    return crc;
}

BUSTER_GLOBAL_LOCAL bool png_chunk_crc_is_valid(ImageDecodeContext* context, PngChunk chunk)
{
    bool result = context->status == IMAGE_DECODE_SUCCESS;
    u64 checked_size = (u64)chunk.length + 4u;
    if (result)
    {
        // One unit per checked byte, like every other per-byte PNG charge. A
        // per-bit charge would let chunk bytes alone exhaust the default
        // budget (see BUSTER_IMAGE_MAX_WORK).
        result = image_decode_add_work(context, checked_size, chunk.header_offset + 4u);
    }
    if (result)
    {
        u32 crc = png_crc_update(UINT32_C(0xffffffff), context->encoded.pointer + chunk.header_offset + 4u, checked_size);
        crc = ~crc;
        u32 expected = image_decode_u32_be(context->encoded.pointer + chunk.crc_offset);
        result = crc == expected;
        if (!result)
        {
            image_decode_error(context, IMAGE_DECODE_CHECKSUM_MISMATCH, chunk.crc_offset);
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool png_chunk_read_header(ImageDecodeContext* context, u64 offset, PngChunk* chunk)
{
    bool result = context->status == IMAGE_DECODE_SUCCESS && chunk != 0;
    if (result && !image_decode_range(context->encoded, offset, PNG_CHUNK_HEADER_SIZE))
    {
        image_decode_error(context, IMAGE_DECODE_TRUNCATED, offset);
        result = false;
    }
    if (result)
    {
        u8 const* header = context->encoded.pointer + offset;
        u32 length = image_decode_u32_be(header);
        u8 const* type = header + 4;
        // A lowercase reserved bit is assigned to a future PNG revision. The
        // decoder must treat that name as unknown, not reject the chunk before
        // its ancillary/critical bit can select the ordinary unknown policy.
        bool type_valid = png_type_character_is_valid(type[0]) && png_type_character_is_valid(type[1]) &&
                          png_type_character_is_valid(type[2]) && png_type_character_is_valid(type[3]);
        if (length > PNG_CHUNK_MAX_LENGTH || !type_valid)
        {
            image_decode_error(context, IMAGE_DECODE_MALFORMED, offset);
            result = false;
        }
        else
        {
            u64 data_offset = offset + PNG_CHUNK_HEADER_SIZE;
            *chunk = (PngChunk){
                .type = image_decode_u32_be(type),
                .length = length,
                .header_offset = offset,
                .data_offset = data_offset,
            };
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool png_chunk_validate(ImageDecodeContext* context, PngChunk* chunk)
{
    bool result = context->status == IMAGE_DECODE_SUCCESS && chunk != 0;
    if (result)
    {
        u64 payload_size = (u64)chunk->length + PNG_CHUNK_CRC_SIZE;
        if (!image_decode_range(context->encoded, chunk->data_offset, payload_size))
        {
            image_decode_error(context, IMAGE_DECODE_TRUNCATED, chunk->data_offset);
            result = false;
        }
        else
        {
            chunk->crc_offset = chunk->data_offset + chunk->length;
            chunk->next_offset = chunk->data_offset + payload_size;
            result = png_chunk_crc_is_valid(context, *chunk);
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool png_color_type_is_valid(u8 color_type, u8 bit_depth, u8* channel_count)
{
    bool result = true;
    u8 channels = 0;
    switch (color_type)
    {
    case 0:
        channels = 1;
        result = bit_depth == 1 || bit_depth == 2 || bit_depth == 4 || bit_depth == 8 || bit_depth == 16;
        break;
    case 2:
        channels = 3;
        result = bit_depth == 8 || bit_depth == 16;
        break;
    case 3:
        channels = 1;
        result = bit_depth == 1 || bit_depth == 2 || bit_depth == 4 || bit_depth == 8;
        break;
    case 4:
        channels = 2;
        result = bit_depth == 8 || bit_depth == 16;
        break;
    case 6:
        channels = 4;
        result = bit_depth == 8 || bit_depth == 16;
        break;
    default:
        result = false;
        break;
    }
    if (result)
    {
        *channel_count = channels;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL ImageColorModel png_color_model(u8 color_type)
{
    ImageColorModel result = IMAGE_COLOR_MODEL_UNKNOWN;
    switch (color_type)
    {
    case 0:
        result = IMAGE_COLOR_MODEL_GRAYSCALE;
        break;
    case 2:
        result = IMAGE_COLOR_MODEL_RGB;
        break;
    case 3:
        result = IMAGE_COLOR_MODEL_INDEXED;
        break;
    case 4:
        result = IMAGE_COLOR_MODEL_GRAYSCALE_ALPHA;
        break;
    case 6:
        result = IMAGE_COLOR_MODEL_RGBA;
        break;
    default:
        break;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool png_profile_name_is_valid(u8 const* bytes, u32 length, u32* separator_index)
{
    bool result = bytes && separator_index && length >= 4;
    u32 separator = 0;
    bool previous_space = false;
    while (result && separator < length && bytes[separator])
    {
        u8 value = bytes[separator];
        bool printable = (value >= 32 && value <= 126) || value >= 161;
        bool space = value == 32;
        result = printable && !(space && (!separator || previous_space));
        previous_space = space;
        separator += 1;
    }
    if (result)
    {
        result = separator >= 1 && separator <= 79 && separator < length && !previous_space && separator + 2u < length;
    }
    if (result)
    {
        *separator_index = separator;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL void png_parse_ihdr(ImageDecodeContext* context, PngState* png, PngChunk chunk)
{
    if (png->seen_ihdr || chunk.length != 13 || chunk.header_offset != PNG_SIGNATURE_SIZE)
    {
        image_decode_error(context, IMAGE_DECODE_MALFORMED, chunk.header_offset);
    }
    else
    {
        u8 const* data = context->encoded.pointer + chunk.data_offset;
        png->width = image_decode_u32_be(data);
        png->height = image_decode_u32_be(data + 4);
        png->bit_depth = data[8];
        png->color_type = data[9];
        png->compression_method = data[10];
        png->filter_method = data[11];
        png->interlace_method = data[12];
        png->ihdr_offset = chunk.data_offset;
        if (!png->width || png->width > PNG_U31_MAX)
        {
            image_decode_error(context, IMAGE_DECODE_MALFORMED, chunk.data_offset);
        }
        else if (!png->height || png->height > PNG_U31_MAX)
        {
            image_decode_error(context, IMAGE_DECODE_MALFORMED, chunk.data_offset + 4u);
        }
        else if (!png_color_type_is_valid(png->color_type, png->bit_depth, &png->channel_count) ||
                 png->compression_method != 0 || png->filter_method != 0 || png->interlace_method > 1)
        {
            image_decode_error(context, IMAGE_DECODE_MALFORMED, chunk.data_offset + 8u);
        }
        else
        {
            png->seen_ihdr = true;
        }
    }
}

BUSTER_GLOBAL_LOCAL void png_parse_plte(ImageDecodeContext* context, PngState* png, PngChunk chunk)
{
    bool valid = png->seen_ihdr && !png->seen_plte && !png->seen_trns && !png->seen_idat && png->color_type != 0 && png->color_type != 4 &&
                 chunk.length >= 3 && chunk.length <= 768 && chunk.length % 3u == 0;
    u32 entry_count = chunk.length / 3u;
    if (valid && png->color_type == 3)
    {
        valid = entry_count <= (1u << png->bit_depth);
    }
    if (!valid)
    {
        image_decode_error(context, IMAGE_DECODE_MALFORMED, chunk.header_offset);
    }
    else
    {
        u8 const* data = context->encoded.pointer + chunk.data_offset;
        for (u32 entry = 0; entry < entry_count; entry += 1)
        {
            png->palette[entry][0] = data[(u64)entry * 3u + 0u];
            png->palette[entry][1] = data[(u64)entry * 3u + 1u];
            png->palette[entry][2] = data[(u64)entry * 3u + 2u];
            png->palette[entry][3] = 255;
        }
        png->palette_count = entry_count;
        png->seen_plte = true;
    }
}

BUSTER_GLOBAL_LOCAL void png_parse_trns(ImageDecodeContext* context, PngState* png, PngChunk chunk)
{
    bool valid = png->seen_ihdr && !png->seen_trns && !png->seen_idat;
    u8 const* data = context->encoded.pointer + chunk.data_offset;
    if (valid)
    {
        switch (png->color_type)
        {
        case 0:
            valid = chunk.length == 2;
            if (valid)
            {
                u16 mask = (u16)(png->bit_depth == 16 ? UINT16_MAX : ((1u << png->bit_depth) - 1u));
                png->transparent_gray = (u16)(image_decode_u16_be(data) & mask);
                png->trns_has_alpha = true;
            }
            break;
        case 2:
            valid = chunk.length == 6;
            if (valid)
            {
                u16 mask = (u16)(png->bit_depth == 16 ? UINT16_MAX : UINT8_MAX);
                png->transparent_red = (u16)(image_decode_u16_be(data) & mask);
                png->transparent_green = (u16)(image_decode_u16_be(data + 2) & mask);
                png->transparent_blue = (u16)(image_decode_u16_be(data + 4) & mask);
                png->trns_has_alpha = true;
            }
            break;
        case 3:
            valid = png->seen_plte && chunk.length <= png->palette_count;
            if (valid)
            {
                for (u32 entry = 0; entry < chunk.length; entry += 1)
                {
                    png->palette[entry][3] = data[entry];
                    if (data[entry] != 255)
                    {
                        png->trns_has_alpha = true;
                    }
                }
            }
            break;
        case 4:
        case 6:
        default:
            valid = false;
            break;
        }
    }
    if (!valid)
    {
        image_decode_error(context, IMAGE_DECODE_MALFORMED, chunk.header_offset);
    }
    else
    {
        png->seen_trns = true;
    }
}

BUSTER_GLOBAL_LOCAL void png_parse_chrm(ImageDecodeContext* context, PngState* png, PngChunk chunk)
{
    bool valid = png->seen_ihdr && !png->seen_chrm && !png->seen_plte && !png->seen_idat && chunk.length == 32;
    u32 component = 0;
    u8 const* data = context->encoded.pointer + chunk.data_offset;
    while (valid && component < 8u)
    {
        valid = image_decode_u32_be(data + component * 4u) <= PNG_U31_MAX;
        if (!valid)
        {
            image_decode_error(context, IMAGE_DECODE_MALFORMED, chunk.data_offset + component * 4u);
        }
        component += 1;
    }
    if (!valid)
    {
        if (context->status == IMAGE_DECODE_SUCCESS)
        {
            image_decode_error(context, IMAGE_DECODE_MALFORMED, chunk.header_offset);
        }
    }
    else
    {
        png->seen_chrm = true;
        png->color_metadata_flags |= IMAGE_COLOR_METADATA_CHROMATICITIES;
    }
}

BUSTER_GLOBAL_LOCAL void png_parse_gama(ImageDecodeContext* context, PngState* png, PngChunk chunk)
{
    u8 const* data = context->encoded.pointer + chunk.data_offset;
    u32 gamma = chunk.length == 4 ? image_decode_u32_be(data) : 0;
    bool structure_valid = png->seen_ihdr && !png->seen_gama && !png->seen_plte && !png->seen_idat && chunk.length == 4;
    if (!structure_valid)
    {
        image_decode_error(context, IMAGE_DECODE_MALFORMED, chunk.header_offset);
    }
    else if (!gamma || gamma > PNG_U31_MAX)
    {
        image_decode_error(context, IMAGE_DECODE_MALFORMED, chunk.data_offset);
    }
    else
    {
        png->seen_gama = true;
        png->color_metadata_flags |= IMAGE_COLOR_METADATA_GAMMA;
    }
}

BUSTER_GLOBAL_LOCAL void png_parse_iccp(ImageDecodeContext* context, PngState* png, PngChunk chunk)
{
    u8 const* data = context->encoded.pointer + chunk.data_offset;
    u32 separator = 0;
    bool valid = png->seen_ihdr && !png->seen_iccp && !png->seen_plte && !png->seen_idat &&
                 png_profile_name_is_valid(data, chunk.length, &separator);
    if (valid)
    {
        valid = data[separator + 1u] == 0;
    }
    if (!valid)
    {
        image_decode_error(context, IMAGE_DECODE_MALFORMED, chunk.header_offset);
    }
    else
    {
        png->seen_iccp = true;
        png->color_metadata_flags |= IMAGE_COLOR_METADATA_ICC_PROFILE;
    }
}

BUSTER_GLOBAL_LOCAL void png_parse_srgb(ImageDecodeContext* context, PngState* png, PngChunk chunk)
{
    u8 const* data = context->encoded.pointer + chunk.data_offset;
    bool valid = png->seen_ihdr && !png->seen_srgb && !png->seen_plte && !png->seen_idat &&
                 chunk.length == 1 && data[0] <= 3;
    if (!valid)
    {
        image_decode_error(context, IMAGE_DECODE_MALFORMED, chunk.header_offset);
    }
    else
    {
        png->seen_srgb = true;
        png->color_metadata_flags |= IMAGE_COLOR_METADATA_SRGB;
    }
}

BUSTER_GLOBAL_LOCAL void png_parse_cicp(ImageDecodeContext* context, PngState* png, PngChunk chunk)
{
    u8 const* data = context->encoded.pointer + chunk.data_offset;
    bool valid = png->seen_ihdr && !png->seen_cicp && !png->seen_plte && !png->seen_idat && chunk.length == 4 &&
                 data[2] == 0 && data[3] <= 1;
    if (!valid)
    {
        image_decode_error(context, IMAGE_DECODE_MALFORMED, chunk.header_offset);
    }
    else
    {
        png->seen_cicp = true;
        png->color_metadata_flags |= IMAGE_COLOR_METADATA_CICP;
    }
}

BUSTER_GLOBAL_LOCAL void png_parse_exif(ImageDecodeContext* context, PngState* png, PngChunk chunk)
{
    bool valid = png->seen_ihdr && !png->seen_exif && !png->seen_idat;
    ImageOrientation orientation = IMAGE_ORIENTATION_TOP_LEFT;
    bool orientation_found = false;
    u64 tiff_error_offset = 0;
    if (valid)
    {
        ByteSlice tiff = {.pointer = context->encoded.pointer + chunk.data_offset, .length = chunk.length};
        valid = image_decode_tiff_orientation(tiff, &orientation, &orientation_found, &tiff_error_offset);
    }
    if (!valid)
    {
        u64 error_offset = png->seen_ihdr && !png->seen_exif && !png->seen_idat ? chunk.data_offset + tiff_error_offset : chunk.header_offset;
        image_decode_error(context, IMAGE_DECODE_MALFORMED, error_offset);
    }
    else
    {
        png->seen_exif = true;
        png->color_metadata_flags |= IMAGE_COLOR_METADATA_EXIF;
        if (orientation_found)
        {
            png->orientation = orientation;
        }
    }
}

BUSTER_GLOBAL_LOCAL void png_parse_actl(ImageDecodeContext* context, PngState* png, PngChunk chunk)
{
    u8 const* data = context->encoded.pointer + chunk.data_offset;
    u32 frame_count = chunk.length == 8 ? image_decode_u32_be(data) : 0;
    u32 play_count = chunk.length == 8 ? image_decode_u32_be(data + 4) : 0;
    bool structure_valid = png->seen_ihdr && !png->seen_actl && !png->seen_idat && chunk.length == 8;
    if (!structure_valid)
    {
        image_decode_error(context, IMAGE_DECODE_MALFORMED, chunk.header_offset);
    }
    else if (!frame_count || frame_count > PNG_U31_MAX)
    {
        image_decode_error(context, IMAGE_DECODE_MALFORMED, chunk.data_offset);
    }
    else if (play_count > PNG_U31_MAX)
    {
        image_decode_error(context, IMAGE_DECODE_MALFORMED, chunk.data_offset + 4u);
    }
    else if (image_decode_check_limit(context, IMAGE_EXCEEDED_LIMIT_FRAMES, frame_count, chunk.data_offset))
    {
        png->seen_actl = true;
        png->animation_frame_count = frame_count;
    }
}

BUSTER_GLOBAL_LOCAL void png_parse_fctl(ImageDecodeContext* context, PngState* png, PngChunk chunk)
{
    u8 const* data = context->encoded.pointer + chunk.data_offset;
    bool before_idat = !png->seen_idat;
    bool structure_valid = png->seen_ihdr && (png->seen_actl || before_idat) && chunk.length == 26;
    if (!structure_valid)
    {
        image_decode_error(context, IMAGE_DECODE_MALFORMED, chunk.header_offset);
    }
    else
    {
        u32 sequence = image_decode_u32_be(data);
        u32 width = image_decode_u32_be(data + 4);
        u32 height = image_decode_u32_be(data + 8);
        u32 x_offset = image_decode_u32_be(data + 12);
        u32 y_offset = image_decode_u32_be(data + 16);
        u64 integer_error_offset = chunk.header_offset;
        if (sequence > PNG_U31_MAX)
        {
            integer_error_offset = chunk.data_offset;
        }
        else if (!width || width > PNG_U31_MAX)
        {
            integer_error_offset = chunk.data_offset + 4u;
        }
        else if (!height || height > PNG_U31_MAX)
        {
            integer_error_offset = chunk.data_offset + 8u;
        }
        else if (x_offset > PNG_U31_MAX)
        {
            integer_error_offset = chunk.data_offset + 12u;
        }
        else if (y_offset > PNG_U31_MAX)
        {
            integer_error_offset = chunk.data_offset + 16u;
        }
        if (integer_error_offset != chunk.header_offset)
        {
            image_decode_error(context, IMAGE_DECODE_MALFORMED, integer_error_offset);
        }
        else
        {
            bool geometry_valid = width <= png->width && height <= png->height &&
                                  x_offset <= png->width - width && y_offset <= png->height - height;
            bool prior_frame_complete = !png->current_frame_needs_fdat || png->current_frame_has_fdat;
            bool frame_count_valid = !png->seen_actl || png->frame_control_count < png->animation_frame_count;
            bool valid = png->next_animation_sequence <= PNG_U31_MAX && sequence == (u32)png->next_animation_sequence &&
                         frame_count_valid && geometry_valid && data[24] <= 2 && data[25] <= 1 &&
                         prior_frame_complete && (!before_idat || !png->seen_fctl);
            if (valid && before_idat)
            {
                valid = width == png->width && height == png->height && !x_offset && !y_offset;
            }
            if (!valid)
            {
                image_decode_error(context, IMAGE_DECODE_MALFORMED, chunk.header_offset);
            }
            else
            {
                png->seen_fctl = true;
                png->frame_control_count += 1;
                png->next_animation_sequence += 1;
                if (before_idat)
                {
                    png->first_fctl_before_idat = true;
                }
                else
                {
                    png->current_frame_needs_fdat = true;
                    png->current_frame_has_fdat = false;
                }
            }
        }
    }
}

BUSTER_GLOBAL_LOCAL void png_parse_fdat(ImageDecodeContext* context, PngState* png, PngChunk chunk)
{
    u8 const* data = context->encoded.pointer + chunk.data_offset;
    bool structure_valid = png->seen_ihdr && png->seen_actl && png->seen_idat && png->current_frame_needs_fdat && chunk.length >= 4;
    if (!structure_valid)
    {
        image_decode_error(context, IMAGE_DECODE_MALFORMED, chunk.header_offset);
    }
    else
    {
        u32 sequence = image_decode_u32_be(data);
        if (sequence > PNG_U31_MAX)
        {
            image_decode_error(context, IMAGE_DECODE_MALFORMED, chunk.data_offset);
        }
        else if (png->next_animation_sequence > PNG_U31_MAX || sequence != (u32)png->next_animation_sequence)
        {
            image_decode_error(context, IMAGE_DECODE_MALFORMED, chunk.header_offset);
        }
        else
        {
            png->next_animation_sequence += 1;
            png->current_frame_has_fdat = true;
        }
    }
}

BUSTER_GLOBAL_LOCAL void png_parse_idat(ImageDecodeContext* context, PngState* png, PngChunk chunk)
{
    // The default-image fcTL may precede acTL, but acTL itself must still be
    // resolved before the first IDAT chunk.
    bool valid = png->seen_ihdr && !png->idat_finished && (!png->seen_fctl || png->seen_actl);
    if (valid && png->color_type == 3)
    {
        valid = png->seen_plte;
    }
    if (valid)
    {
        valid = (u64)chunk.length <= UINT64_MAX - png->idat_size;
    }
    if (!valid)
    {
        image_decode_error(context, IMAGE_DECODE_MALFORMED, chunk.header_offset);
    }
    else
    {
        if (!png->seen_idat)
        {
            png->first_idat_header_offset = chunk.header_offset;
        }
        png->seen_idat = true;
        png->idat_size += chunk.length;
    }
}

BUSTER_GLOBAL_LOCAL void png_parse_iend(ImageDecodeContext* context, PngState* png, PngChunk chunk)
{
    bool valid = png->seen_ihdr && png->seen_idat && !png->seen_iend && chunk.length == 0 && chunk.next_offset == context->encoded.length;
    if (!valid)
    {
        image_decode_error(context, IMAGE_DECODE_MALFORMED, chunk.header_offset);
    }
    else
    {
        png->seen_iend = true;
    }
}

BUSTER_GLOBAL_LOCAL PngState png_parse(ImageDecodeContext* context)
{
    PngState result = {.orientation = IMAGE_ORIENTATION_TOP_LEFT};
    static u8 const signature[PNG_SIGNATURE_SIZE] = {137, 80, 78, 71, 13, 10, 26, 10};
    if (!image_decode_range(context->encoded, 0, sizeof(signature)))
    {
        image_decode_error(context, IMAGE_DECODE_TRUNCATED, 0);
    }
    else if (memcmp(context->encoded.pointer, signature, sizeof(signature)) != 0)
    {
        image_decode_error(context, IMAGE_DECODE_MALFORMED, 0);
    }
    else
    {
        image_decode_add_work(context, sizeof(signature), 0);
    }

    u64 position = PNG_SIGNATURE_SIZE;
    while (context->status == IMAGE_DECODE_SUCCESS && !result.seen_iend)
    {
        PngChunk chunk = {0};
        if (png_chunk_read_header(context, position, &chunk) &&
            image_decode_count_limit(context, IMAGE_EXCEEDED_LIMIT_CHUNKS, 1, chunk.header_offset) &&
            image_decode_add_work(context, 1, chunk.header_offset) && png_chunk_validate(context, &chunk))
        {
            if (!result.seen_ihdr && chunk.type != PNG_CHUNK_TYPE('I', 'H', 'D', 'R'))
            {
                image_decode_error(context, IMAGE_DECODE_MALFORMED, chunk.header_offset);
            }
            else
            {
                bool is_idat = chunk.type == PNG_CHUNK_TYPE('I', 'D', 'A', 'T');
                if (result.seen_idat && !is_idat)
                {
                    result.idat_finished = true;
                }
                switch (chunk.type)
                {
                case PNG_CHUNK_TYPE('I', 'H', 'D', 'R'):
                    png_parse_ihdr(context, &result, chunk);
                    break;
                case PNG_CHUNK_TYPE('P', 'L', 'T', 'E'):
                    png_parse_plte(context, &result, chunk);
                    break;
                case PNG_CHUNK_TYPE('t', 'R', 'N', 'S'):
                    png_parse_trns(context, &result, chunk);
                    break;
                case PNG_CHUNK_TYPE('c', 'H', 'R', 'M'):
                    png_parse_chrm(context, &result, chunk);
                    break;
                case PNG_CHUNK_TYPE('g', 'A', 'M', 'A'):
                    png_parse_gama(context, &result, chunk);
                    break;
                case PNG_CHUNK_TYPE('i', 'C', 'C', 'P'):
                    png_parse_iccp(context, &result, chunk);
                    break;
                case PNG_CHUNK_TYPE('s', 'R', 'G', 'B'):
                    png_parse_srgb(context, &result, chunk);
                    break;
                case PNG_CHUNK_TYPE('c', 'I', 'C', 'P'):
                    png_parse_cicp(context, &result, chunk);
                    break;
                case PNG_CHUNK_TYPE('e', 'X', 'I', 'f'):
                    png_parse_exif(context, &result, chunk);
                    break;
                case PNG_CHUNK_TYPE('a', 'c', 'T', 'L'):
                    png_parse_actl(context, &result, chunk);
                    break;
                case PNG_CHUNK_TYPE('f', 'c', 'T', 'L'):
                    png_parse_fctl(context, &result, chunk);
                    break;
                case PNG_CHUNK_TYPE('f', 'd', 'A', 'T'):
                    png_parse_fdat(context, &result, chunk);
                    break;
                case PNG_CHUNK_TYPE('I', 'D', 'A', 'T'):
                    png_parse_idat(context, &result, chunk);
                    break;
                case PNG_CHUNK_TYPE('I', 'E', 'N', 'D'):
                    png_parse_iend(context, &result, chunk);
                    break;
                default:
                    if (png_type_is_critical(chunk.type))
                    {
                        image_decode_set_unsupported_feature(context, IMAGE_UNSUPPORTED_FEATURE_CONTAINER_FEATURE,
                                                             chunk.header_offset + 4u);
                    }
                    break;
                }
            }
            position = chunk.next_offset;
        }
    }

    if (context->status == IMAGE_DECODE_SUCCESS && (!result.seen_ihdr || !result.seen_idat || !result.seen_iend ||
                                                     (result.color_type == 3 && !result.seen_plte)))
    {
        image_decode_error(context, IMAGE_DECODE_MALFORMED, position);
    }
    if (context->status == IMAGE_DECODE_SUCCESS && result.seen_actl &&
        (result.frame_control_count != result.animation_frame_count ||
         (result.current_frame_needs_fdat && !result.current_frame_has_fdat)))
    {
        image_decode_error(context, IMAGE_DECODE_MALFORMED, position);
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool png_compressed_open_next(PngCompressedInput* input)
{
    bool result = input->context->status == IMAGE_DECODE_SUCCESS && input->consumed < input->png->idat_size;
    while (result && input->data_remaining == 0)
    {
        if (!image_decode_add_work(input->context, 1, input->chunk_header_offset))
        {
            result = false;
        }
        else if (!image_decode_range(input->context->encoded, input->chunk_header_offset, PNG_CHUNK_HEADER_SIZE))
        {
            image_decode_error(input->context, IMAGE_DECODE_TRUNCATED, input->chunk_header_offset);
            result = false;
        }
        else
        {
            u8 const* header = input->context->encoded.pointer + input->chunk_header_offset;
            u32 length = image_decode_u32_be(header);
            u32 type = image_decode_u32_be(header + 4);
            u64 data_offset = input->chunk_header_offset + PNG_CHUNK_HEADER_SIZE;
            u64 next_offset = data_offset + (u64)length + PNG_CHUNK_CRC_SIZE;
            if (type != PNG_CHUNK_TYPE('I', 'D', 'A', 'T') || length > PNG_CHUNK_MAX_LENGTH ||
                !image_decode_range(input->context->encoded, data_offset, (u64)length + PNG_CHUNK_CRC_SIZE) ||
                (u64)length > input->png->idat_size - input->consumed)
            {
                image_decode_error(input->context, IMAGE_DECODE_MALFORMED, input->chunk_header_offset);
                result = false;
            }
            else
            {
                input->data_offset = data_offset;
                input->data_remaining = length;
                input->next_chunk_offset = next_offset;
                input->chunk_header_offset = next_offset;
            }
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL PngCompressedInput png_compressed_input_make(ImageDecodeContext* context, PngState const* png)
{
    PngCompressedInput result = {
        .context = context,
        .png = png,
        .chunk_header_offset = png->first_idat_header_offset,
    };
    return result;
}

BUSTER_GLOBAL_LOCAL bool png_compressed_u8(PngCompressedInput* input, u8* value)
{
    bool result = input->context->status == IMAGE_DECODE_SUCCESS && value != 0;
    if (result && input->data_remaining == 0)
    {
        result = png_compressed_open_next(input);
        if (!result && input->context->status == IMAGE_DECODE_SUCCESS)
        {
            image_decode_error(input->context, IMAGE_DECODE_TRUNCATED, input->chunk_header_offset);
        }
    }
    if (result && input->data_remaining != 0)
    {
        *value = input->context->encoded.pointer[input->data_offset];
        result = image_decode_add_work(input->context, 1, input->data_offset);
        if (result)
        {
            input->data_offset += 1;
            input->data_remaining -= 1;
            input->consumed += 1;
        }
    }
    else if (result)
    {
        image_decode_error(input->context, IMAGE_DECODE_TRUNCATED, input->chunk_header_offset);
        result = false;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL u64 png_compressed_offset(PngCompressedInput const* input)
{
    u64 result = input->data_remaining ? input->data_offset : input->chunk_header_offset;
    return result;
}

BUSTER_GLOBAL_LOCAL bool png_bits_read(PngBitReader* reader, u32 count, u32* value)
{
    bool result = reader && value && count <= 16 && reader->input->context->status == IMAGE_DECODE_SUCCESS;
    while (result && reader->bit_count < count)
    {
        u8 byte = 0;
        result = png_compressed_u8(reader->input, &byte);
        if (result)
        {
            reader->last_byte_offset = reader->input->data_offset - 1u;
            reader->bits |= (u32)byte << reader->bit_count;
            reader->bit_count += 8;
        }
    }
    if (result)
    {
        u32 mask = count == 16 ? UINT32_C(0xffff) : (count ? (1u << count) - 1u : 0u);
        *value = reader->bits & mask;
        reader->bits >>= count;
        reader->bit_count -= count;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL void png_bits_align(PngBitReader* reader)
{
    reader->bits = 0;
    reader->bit_count = 0;
}

BUSTER_GLOBAL_LOCAL bool png_huffman_build(ImageDecodeContext* context, PngHuffman* huffman, u8 const* lengths, u32 length_count,
                                            PngHuffmanKind kind, u64 error_offset)
{
    memset(huffman, 0, sizeof(*huffman));
    bool result = image_decode_add_work(context, length_count, error_offset);
    if (result)
    {
        for (u32 symbol = 0; symbol < length_count && result; symbol += 1)
        {
            u8 length = lengths[symbol];
            if (length > PNG_DEFLATE_MAX_BITS)
            {
                image_decode_error(context, IMAGE_DECODE_MALFORMED, error_offset);
                result = false;
            }
            else if (length != 0)
            {
                huffman->count[length] += 1;
                huffman->symbol_count += 1;
            }
        }
    }
    s32 remaining = 1;
    for (u32 length = 1; length <= PNG_DEFLATE_MAX_BITS && result; length += 1)
    {
        remaining = remaining * 2 - huffman->count[length];
        if (remaining < 0)
        {
            image_decode_error(context, IMAGE_DECODE_MALFORMED, error_offset);
            result = false;
        }
    }
    if (result)
    {
        bool complete = remaining == 0;
        bool single = huffman->symbol_count == 1 && huffman->count[1] == 1;
        bool empty_distance = kind == PNG_HUFFMAN_DISTANCE && length_count == 1 && huffman->symbol_count == 0;
        bool valid = kind == PNG_HUFFMAN_CODE_LENGTH ? complete : complete || single || empty_distance;
        if (!valid)
        {
            image_decode_error(context, IMAGE_DECODE_MALFORMED, error_offset);
            result = false;
        }
    }
    if (result && huffman->symbol_count)
    {
        u16 offsets[PNG_DEFLATE_MAX_BITS + 1u] = {0};
        for (u32 length = 1; length < PNG_DEFLATE_MAX_BITS; length += 1)
        {
            offsets[length + 1u] = (u16)(offsets[length] + huffman->count[length]);
        }
        for (u32 symbol = 0; symbol < length_count; symbol += 1)
        {
            u8 length = lengths[symbol];
            if (length)
            {
                huffman->symbols[offsets[length]] = (u16)symbol;
                offsets[length] += 1;
            }
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool png_huffman_decode(PngBitReader* reader, PngHuffman const* huffman, u32* symbol)
{
    bool result = huffman->symbol_count != 0 && reader->input->context->status == IMAGE_DECODE_SUCCESS;
    if (!huffman->symbol_count && reader->input->context->status == IMAGE_DECODE_SUCCESS)
    {
        image_decode_error(reader->input->context, IMAGE_DECODE_MALFORMED, png_compressed_offset(reader->input));
    }
    s32 code = 0;
    s32 first = 0;
    u32 index = 0;
    bool found = false;
    for (u32 length = 1; length <= PNG_DEFLATE_MAX_BITS && result && !found; length += 1)
    {
        u32 bit = 0;
        result = png_bits_read(reader, 1, &bit);
        if (result)
        {
            code |= (s32)bit;
            s32 count = huffman->count[length];
            if (code >= first && code - first < count)
            {
                u32 selected = index + (u32)(code - first);
                if (selected < huffman->symbol_count)
                {
                    *symbol = huffman->symbols[selected];
                    found = true;
                }
                else
                {
                    image_decode_error(reader->input->context, IMAGE_DECODE_MALFORMED, png_compressed_offset(reader->input));
                    result = false;
                }
            }
            index += (u32)count;
            first = (first + count) << 1;
            code <<= 1;
        }
    }
    if (result && !found)
    {
        image_decode_error(reader->input->context, IMAGE_DECODE_MALFORMED, png_compressed_offset(reader->input));
        result = false;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool png_inflate_emit(PngInflateOutput* output, u8 byte)
{
    bool result = output->context->status == IMAGE_DECODE_SUCCESS;
    if (result && output->count >= output->capacity)
    {
        image_decode_error(output->context, IMAGE_DECODE_MALFORMED, output->error_offset);
        result = false;
    }
    if (result)
    {
        result = image_decode_add_work(output->context, 1, output->error_offset);
    }
    if (result)
    {
        output->bytes[output->count] = byte;
        output->count += 1;
        output->adler_s1 += byte;
        if (output->adler_s1 >= PNG_ADLER_MODULUS)
        {
            output->adler_s1 -= PNG_ADLER_MODULUS;
        }
        output->adler_s2 += output->adler_s1;
        if (output->adler_s2 >= PNG_ADLER_MODULUS)
        {
            output->adler_s2 -= PNG_ADLER_MODULUS;
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool png_inflate_stored(PngBitReader* reader, PngInflateOutput* output)
{
    png_bits_align(reader);
    u8 bytes[4] = {0};
    bool result = true;
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(bytes) && result; index += 1)
    {
        result = png_compressed_u8(reader->input, &bytes[index]);
    }
    if (result)
    {
        u16 length = image_decode_u16_le(bytes);
        u16 complement = image_decode_u16_le(bytes + 2);
        if ((u16)(length ^ UINT16_MAX) != complement)
        {
            image_decode_error(output->context, IMAGE_DECODE_MALFORMED, png_compressed_offset(reader->input));
            result = false;
        }
        for (u32 index = 0; index < length && result; index += 1)
        {
            u8 byte = 0;
            result = png_compressed_u8(reader->input, &byte);
            if (result)
            {
                output->error_offset = png_compressed_offset(reader->input);
                result = png_inflate_emit(output, byte);
            }
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool png_huffman_fixed(ImageDecodeContext* context, PngHuffman* literals, PngHuffman* distances, u64 error_offset)
{
    u8 literal_lengths[PNG_DEFLATE_LITERAL_SYMBOL_COUNT];
    u8 distance_lengths[PNG_DEFLATE_DISTANCE_SYMBOL_COUNT];
    for (u32 symbol = 0; symbol <= 143; symbol += 1) literal_lengths[symbol] = 8;
    for (u32 symbol = 144; symbol <= 255; symbol += 1) literal_lengths[symbol] = 9;
    for (u32 symbol = 256; symbol <= 279; symbol += 1) literal_lengths[symbol] = 7;
    for (u32 symbol = 280; symbol < PNG_DEFLATE_LITERAL_SYMBOL_COUNT; symbol += 1) literal_lengths[symbol] = 8;
    memset(distance_lengths, 5, sizeof(distance_lengths));
    bool result = png_huffman_build(context, literals, literal_lengths, BUSTER_ARRAY_LENGTH(literal_lengths),
                                    PNG_HUFFMAN_LITERAL_LENGTH, error_offset);
    if (result)
    {
        result = png_huffman_build(context, distances, distance_lengths, BUSTER_ARRAY_LENGTH(distance_lengths),
                                   PNG_HUFFMAN_DISTANCE, error_offset);
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool png_huffman_dynamic(PngBitReader* reader, PngHuffman* literals, PngHuffman* distances)
{
    static u8 const order[PNG_DEFLATE_CODE_LENGTH_SYMBOL_COUNT] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
    ImageDecodeContext* context = reader->input->context;
    u64 error_offset = png_compressed_offset(reader->input);
    u32 hlit_bits = 0;
    u32 hdist_bits = 0;
    u32 hclen_bits = 0;
    bool result = png_bits_read(reader, 5, &hlit_bits) && png_bits_read(reader, 5, &hdist_bits) && png_bits_read(reader, 4, &hclen_bits);
    u32 literal_count = hlit_bits + 257u;
    u32 distance_count = hdist_bits + 1u;
    u32 code_length_count = hclen_bits + 4u;
    if (result && (literal_count > 286 || distance_count > PNG_DEFLATE_DISTANCE_SYMBOL_COUNT))
    {
        image_decode_error(context, IMAGE_DECODE_MALFORMED, error_offset);
        result = false;
    }

    u8 code_lengths[PNG_DEFLATE_CODE_LENGTH_SYMBOL_COUNT] = {0};
    for (u32 index = 0; index < code_length_count && result; index += 1)
    {
        u32 value = 0;
        result = png_bits_read(reader, 3, &value);
        if (result)
        {
            code_lengths[order[index]] = (u8)value;
        }
    }

    PngHuffman code_length_huffman;
    if (result)
    {
        result = png_huffman_build(context, &code_length_huffman, code_lengths, BUSTER_ARRAY_LENGTH(code_lengths),
                                   PNG_HUFFMAN_CODE_LENGTH, error_offset);
    }

    u8 lengths[PNG_DEFLATE_DYNAMIC_LENGTH_COUNT] = {0};
    u32 total = literal_count + distance_count;
    u32 index = 0;
    while (index < total && result)
    {
        u32 symbol = 0;
        result = png_huffman_decode(reader, &code_length_huffman, &symbol);
        if (result && symbol <= 15)
        {
            lengths[index] = (u8)symbol;
            index += 1;
        }
        else if (result)
        {
            u32 extra_bits = symbol == 16 ? 2u : symbol == 17 ? 3u : symbol == 18 ? 7u : 0u;
            u32 base = symbol == 16 ? 3u : symbol == 17 ? 3u : symbol == 18 ? 11u : 0u;
            u32 extra = 0;
            if (!extra_bits || (symbol == 16 && index == 0))
            {
                image_decode_error(context, IMAGE_DECODE_MALFORMED, png_compressed_offset(reader->input));
                result = false;
            }
            else
            {
                result = png_bits_read(reader, extra_bits, &extra);
            }
            u32 repeat = base + extra;
            if (result && repeat > total - index)
            {
                image_decode_error(context, IMAGE_DECODE_MALFORMED, png_compressed_offset(reader->input));
                result = false;
            }
            if (result)
            {
                u8 value = symbol == 16 ? lengths[index - 1u] : 0;
                for (u32 repeated = 0; repeated < repeat; repeated += 1)
                {
                    lengths[index] = value;
                    index += 1;
                }
            }
        }
    }
    if (result && lengths[256] == 0)
    {
        image_decode_error(context, IMAGE_DECODE_MALFORMED, error_offset);
        result = false;
    }
    if (result)
    {
        result = png_huffman_build(context, literals, lengths, literal_count, PNG_HUFFMAN_LITERAL_LENGTH, error_offset);
    }
    if (result)
    {
        result = png_huffman_build(context, distances, lengths + literal_count, distance_count, PNG_HUFFMAN_DISTANCE, error_offset);
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool png_inflate_compressed(PngBitReader* reader, PngInflateOutput* output, bool dynamic)
{
    static u16 const length_base[29] = {
        3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258,
    };
    static u8 const length_extra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
    static u16 const distance_base[30] = {
        1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289,
        16385, 24577,
    };
    static u8 const distance_extra[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

    PngHuffman literals;
    PngHuffman distances;
    u64 error_offset = png_compressed_offset(reader->input);
    bool result = dynamic ? png_huffman_dynamic(reader, &literals, &distances)
                          : png_huffman_fixed(output->context, &literals, &distances, error_offset);
    bool ended = false;
    while (result && !ended)
    {
        u32 symbol = 0;
        result = png_huffman_decode(reader, &literals, &symbol);
        if (result && symbol < 256)
        {
            output->error_offset = png_compressed_offset(reader->input);
            result = png_inflate_emit(output, (u8)symbol);
        }
        else if (result && symbol == 256)
        {
            ended = true;
        }
        else if (result && symbol >= 257 && symbol <= 285)
        {
            u32 length_index = symbol - 257u;
            u32 length_bits = 0;
            result = png_bits_read(reader, length_extra[length_index], &length_bits);
            u32 length = length_base[length_index] + length_bits;
            u32 distance_symbol = 0;
            if (result)
            {
                result = png_huffman_decode(reader, &distances, &distance_symbol);
            }
            if (result && distance_symbol >= BUSTER_ARRAY_LENGTH(distance_base))
            {
                image_decode_error(output->context, IMAGE_DECODE_MALFORMED, png_compressed_offset(reader->input));
                result = false;
            }
            u32 distance_bits = 0;
            if (result)
            {
                result = png_bits_read(reader, distance_extra[distance_symbol], &distance_bits);
            }
            u32 distance = result ? (u32)distance_base[distance_symbol] + distance_bits : 0;
            if (result && (!distance || distance > output->count || distance > output->maximum_distance))
            {
                image_decode_error(output->context, IMAGE_DECODE_MALFORMED, png_compressed_offset(reader->input));
                result = false;
            }
            for (u32 copied = 0; copied < length && result; copied += 1)
            {
                u8 byte = output->bytes[output->count - distance];
                output->error_offset = png_compressed_offset(reader->input);
                result = png_inflate_emit(output, byte);
            }
        }
        else if (result)
        {
            image_decode_error(output->context, IMAGE_DECODE_MALFORMED, png_compressed_offset(reader->input));
            result = false;
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool png_inflate_deflate(PngBitReader* reader, PngInflateOutput* output)
{
    bool result = true;
    bool final = false;
    while (result && !final)
    {
        // Huffman decoding consumes one bit at a time, so an EOB can leave at
        // most the rest of its current byte buffered. A following block header
        // starts in that byte rather than at the input's next unread byte. If
        // no bits remain, read the first header bit before selecting its offset:
        // opening a split IDAT advances from the chunk header to its data.
        bool block_buffered = reader->bit_count != 0;
        u64 block_offset = block_buffered ? reader->last_byte_offset : 0;
        u32 final_bit = 0;
        u32 block_type = 0;
        result = png_bits_read(reader, 1, &final_bit);
        if (result && !block_buffered)
        {
            block_offset = reader->last_byte_offset;
        }
        if (result)
        {
            result = png_bits_read(reader, 2, &block_type);
        }
        if (result)
        {
            result = image_decode_count_limit(output->context, IMAGE_EXCEEDED_LIMIT_BLOCKS, 1, block_offset);
        }
        if (result)
        {
            final = final_bit != 0;
            if (block_type == 0)
            {
                result = png_inflate_stored(reader, output);
            }
            else if (block_type == 1 || block_type == 2)
            {
                result = png_inflate_compressed(reader, output, block_type == 2);
            }
            else
            {
                image_decode_error(output->context, IMAGE_DECODE_MALFORMED, png_compressed_offset(reader->input));
                result = false;
            }
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool png_inflate_zlib(ImageDecodeContext* context, PngState const* png, u8* output_bytes, u64 output_size)
{
    PngCompressedInput input = png_compressed_input_make(context, png);
    u8 cmf = 0;
    u8 flg = 0;
    bool result = png_compressed_u8(&input, &cmf) && png_compressed_u8(&input, &flg);
    if (result && ((cmf & 15u) != 8u || (cmf >> 4u) > 7u || (((u32)cmf << 8u) | flg) % 31u != 0 || (flg & 0x20u) != 0))
    {
        image_decode_error(context, IMAGE_DECODE_MALFORMED, png->first_idat_header_offset);
        result = false;
    }
    u32 maximum_distance = result ? UINT32_C(1) << ((cmf >> 4u) + 8u) : 0;

    PngBitReader bits = {.input = &input};
    PngInflateOutput output = {
        .context = context,
        .bytes = output_bytes,
        .capacity = output_size,
        .maximum_distance = maximum_distance,
        .adler_s1 = 1,
        .error_offset = png->first_idat_header_offset,
    };
    if (result)
    {
        result = png_inflate_deflate(&bits, &output);
    }
    png_bits_align(&bits);

    u8 adler_bytes[4] = {0};
    u64 adler_offset = png_compressed_offset(&input);
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(adler_bytes) && result; index += 1)
    {
        result = png_compressed_u8(&input, &adler_bytes[index]);
    }
    if (result)
    {
        u32 expected = image_decode_u32_be(adler_bytes);
        u32 actual = output.adler_s2 << 16u | output.adler_s1;
        if (expected != actual)
        {
            image_decode_error(context, IMAGE_DECODE_CHECKSUM_MISMATCH, adler_offset);
            result = false;
        }
    }
    if (result && output.count != output.capacity)
    {
        image_decode_error(context, IMAGE_DECODE_MALFORMED, png->first_idat_header_offset);
        result = false;
    }
    // PNG permits unused bytes after the complete zlib stream. Their containing
    // IDAT chunks, ordering and CRCs were already validated by png_parse.
    return result;
}

BUSTER_GLOBAL_LOCAL u32 png_pass_extent(u32 extent, u32 start, u32 step)
{
    u32 result = extent <= start ? 0 : 1u + (extent - 1u - start) / step;
    return result;
}

BUSTER_GLOBAL_LOCAL PngPass png_pass_make(PngState const* png, u32 pass_index)
{
    static u8 const x_start[7] = {0, 4, 0, 2, 0, 1, 0};
    static u8 const y_start[7] = {0, 0, 4, 0, 2, 0, 1};
    static u8 const x_step[7] = {8, 8, 4, 4, 2, 2, 1};
    static u8 const y_step[7] = {8, 8, 8, 4, 4, 2, 2};
    PngPass result;
    if (png->interlace_method == 0)
    {
        result = (PngPass){.x_step = 1, .y_step = 1, .width = png->width, .height = png->height};
    }
    else
    {
        result = (PngPass){
            .x_start = x_start[pass_index],
            .y_start = y_start[pass_index],
            .x_step = x_step[pass_index],
            .y_step = y_step[pass_index],
        };
        result.width = png_pass_extent(png->width, result.x_start, result.x_step);
        result.height = png_pass_extent(png->height, result.y_start, result.y_step);
    }
    if (!result.width || !result.height)
    {
        result.width = 0;
        result.height = 0;
    }
    if (result.width)
    {
        u64 row_bits = (u64)result.width * png->channel_count * png->bit_depth;
        result.row_bytes = (row_bits + 7u) / 8u;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool png_filtered_size(ImageDecodeContext* context, PngState const* png, u64* size)
{
    bool result = true;
    u64 total = 0;
    u32 pass_count = png->interlace_method ? 7u : 1u;
    for (u32 pass_index = 0; pass_index < pass_count && result; pass_index += 1)
    {
        PngPass pass = png_pass_make(png, pass_index);
        if (pass.width && pass.height)
        {
            u64 row_size = pass.row_bytes + 1u;
            if ((u64)pass.height > (UINT64_MAX - total) / row_size)
            {
                // The canonical output allocation is admitted before this
                // calculation, so this is a working-storage capacity failure,
                // not an exceeded caller-configured limit.
                image_decode_error(context, IMAGE_DECODE_CAPACITY_EXCEEDED, png->ihdr_offset);
                result = false;
            }
            else
            {
                total += (u64)pass.height * row_size;
            }
        }
    }
    if (result)
    {
        *size = total;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL u8 png_paeth(u8 left, u8 above, u8 upper_left)
{
    s32 a = left;
    s32 b = above;
    s32 c = upper_left;
    s32 prediction = a + b - c;
    s32 distance_a = prediction - a;
    s32 distance_b = prediction - b;
    s32 distance_c = prediction - c;
    if (distance_a < 0) distance_a = -distance_a;
    if (distance_b < 0) distance_b = -distance_b;
    if (distance_c < 0) distance_c = -distance_c;
    u8 result = distance_a <= distance_b && distance_a <= distance_c ? left : distance_b <= distance_c ? above : upper_left;
    return result;
}

BUSTER_GLOBAL_LOCAL bool png_unfilter_row(ImageDecodeContext* context, u8* row, u8 const* previous, u64 row_bytes, u32 bytes_per_pixel,
                                          u8 filter, u64 error_offset)
{
    bool result = filter <= 4 && image_decode_add_work(context, row_bytes + 1u, error_offset);
    if (!result && context->status == IMAGE_DECODE_SUCCESS)
    {
        image_decode_error(context, IMAGE_DECODE_MALFORMED, error_offset);
    }
    for (u64 index = 0; index < row_bytes && result; index += 1)
    {
        u8 left = index >= bytes_per_pixel ? row[index - bytes_per_pixel] : 0;
        u8 above = previous ? previous[index] : 0;
        u8 upper_left = previous && index >= bytes_per_pixel ? previous[index - bytes_per_pixel] : 0;
        u8 predictor = 0;
        switch (filter)
        {
        case 0:
            predictor = 0;
            break;
        case 1:
            predictor = left;
            break;
        case 2:
            predictor = above;
            break;
        case 3:
            predictor = (u8)(((u32)left + above) / 2u);
            break;
        case 4:
            predictor = png_paeth(left, above, upper_left);
            break;
        default:
            break;
        }
        row[index] = (u8)(row[index] + predictor);
    }
    return result;
}

BUSTER_GLOBAL_LOCAL u16 png_sample(u8 const* row, u8 bit_depth, u32 sample_index)
{
    u16 result;
    if (bit_depth == 16)
    {
        result = image_decode_u16_be(row + (u64)sample_index * 2u);
    }
    else if (bit_depth == 8)
    {
        result = row[sample_index];
    }
    else
    {
        u32 bit_offset = sample_index * bit_depth;
        u32 shift = 8u - bit_depth - bit_offset % 8u;
        result = (u16)(((u32)row[bit_offset / 8u] >> shift) & ((1u << bit_depth) - 1u));
    }
    return result;
}

BUSTER_GLOBAL_LOCAL u8 png_sample_to_u8(u16 sample, u8 bit_depth)
{
    u8 result;
    if (bit_depth == 16)
    {
        result = (u8)(sample >> 8u);
    }
    else if (bit_depth == 8)
    {
        result = (u8)sample;
    }
    else
    {
        u32 maximum = (1u << bit_depth) - 1u;
        result = (u8)((u32)sample * 255u / maximum);
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool png_write_pixel(ImageDecodeContext* context, PngState const* png, u8 const* row, u32 source_x,
                                         u32 destination_x, u32 destination_y, u64 error_offset)
{
    bool result = context->status == IMAGE_DECODE_SUCCESS;
    u16 samples[4] = {0};
    u32 first_sample = source_x * png->channel_count;
    for (u32 channel = 0; channel < png->channel_count; channel += 1)
    {
        samples[channel] = png_sample(row, png->bit_depth, first_sample + channel);
    }
    u8 red = 0;
    u8 green = 0;
    u8 blue = 0;
    u8 alpha = 255;
    switch (png->color_type)
    {
    case 0:
        red = png_sample_to_u8(samples[0], png->bit_depth);
        green = red;
        blue = red;
        if (png->seen_trns && samples[0] == png->transparent_gray) alpha = 0;
        break;
    case 2:
        red = png_sample_to_u8(samples[0], png->bit_depth);
        green = png_sample_to_u8(samples[1], png->bit_depth);
        blue = png_sample_to_u8(samples[2], png->bit_depth);
        if (png->seen_trns && samples[0] == png->transparent_red && samples[1] == png->transparent_green && samples[2] == png->transparent_blue)
        {
            alpha = 0;
        }
        break;
    case 3:
        if (samples[0] >= png->palette_count)
        {
            // PNG requires recovery from an out-of-range palette index by
            // displaying opaque black rather than failing the image.
            red = 0;
            green = 0;
            blue = 0;
            alpha = 255;
        }
        else
        {
            red = png->palette[samples[0]][0];
            green = png->palette[samples[0]][1];
            blue = png->palette[samples[0]][2];
            alpha = png->palette[samples[0]][3];
        }
        break;
    case 4:
        red = png_sample_to_u8(samples[0], png->bit_depth);
        green = red;
        blue = red;
        alpha = png_sample_to_u8(samples[1], png->bit_depth);
        break;
    case 6:
        red = png_sample_to_u8(samples[0], png->bit_depth);
        green = png_sample_to_u8(samples[1], png->bit_depth);
        blue = png_sample_to_u8(samples[2], png->bit_depth);
        alpha = png_sample_to_u8(samples[3], png->bit_depth);
        break;
    default:
        image_decode_error(context, IMAGE_DECODE_MALFORMED, error_offset);
        result = false;
        break;
    }
    if (result)
    {
        u64 destination = (u64)destination_y * context->image.stride + (u64)destination_x * 4u;
        context->image.pixels.pointer[destination + 0u] = red;
        context->image.pixels.pointer[destination + 1u] = green;
        context->image.pixels.pointer[destination + 2u] = blue;
        context->image.pixels.pointer[destination + 3u] = alpha;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool png_expand_pixels(ImageDecodeContext* context, PngState const* png, u8* filtered, u64 filtered_size)
{
    bool result = true;
    u64 cursor = 0;
    u32 pass_count = png->interlace_method ? 7u : 1u;
    u32 bits_per_pixel = (u32)png->channel_count * png->bit_depth;
    u32 bytes_per_pixel = BUSTER_MAX(1u, (bits_per_pixel + 7u) / 8u);
    for (u32 pass_index = 0; pass_index < pass_count && result; pass_index += 1)
    {
        PngPass pass = png_pass_make(png, pass_index);
        u8 const* previous = 0;
        for (u32 row_index = 0; row_index < pass.height && result; row_index += 1)
        {
            bool range_valid = cursor < filtered_size && pass.row_bytes <= filtered_size - cursor - 1u;
            if (!range_valid)
            {
                image_decode_error(context, IMAGE_DECODE_MALFORMED, png->first_idat_header_offset);
                result = false;
            }
            else
            {
                u8 filter = filtered[cursor];
                u8* row = filtered + cursor + 1u;
                result = png_unfilter_row(context, row, previous, pass.row_bytes, bytes_per_pixel, filter, png->first_idat_header_offset);
                if (result)
                {
                    u64 pixel_work = (u64)pass.width * png->channel_count;
                    result = image_decode_add_work(context, pixel_work, png->first_idat_header_offset);
                }
                for (u32 source_x = 0; source_x < pass.width && result; source_x += 1)
                {
                    u32 destination_x = pass.x_start + source_x * pass.x_step;
                    u32 destination_y = pass.y_start + row_index * pass.y_step;
                    result = png_write_pixel(context, png, row, source_x, destination_x, destination_y, png->first_idat_header_offset);
                }
                previous = row;
                cursor += pass.row_bytes + 1u;
            }
        }
    }
    if (result && cursor != filtered_size)
    {
        image_decode_error(context, IMAGE_DECODE_MALFORMED, png->first_idat_header_offset);
        result = false;
    }
    return result;
}

void image_png_process(ImageDecodeContext* context)
{
    PngState png = png_parse(context);
    TemporalArena scratch = {0};
    if (context->status == IMAGE_DECODE_SUCCESS)
    {
        bool has_alpha = png.color_type == 4 || png.color_type == 6 || png.trns_has_alpha;
        u32 frame_count = png.seen_actl ? png.animation_frame_count : 1;
        if (image_decode_set_information(context, png.width, png.height, frame_count, png.channel_count, png.bit_depth, has_alpha,
                                         png.orientation, png.ihdr_offset))
        {
            context->information.source_color_model = png_color_model(png.color_type);
            context->information.color_metadata_flags = png.color_metadata_flags;
            context->information.is_animated = png.seen_actl;
            // In the second APNG layout the default IDAT image is a static
            // fallback outside acTL.num_frames, so even a one-frame animation
            // contains an image other than the returned default.
            context->information.has_more_images = png.seen_actl &&
                                                   (png.animation_frame_count > 1 || !png.first_fctl_before_idat);
        }
    }

    if (context->status == IMAGE_DECODE_SUCCESS && context->decode_pixels)
    {
        u64 filtered_size = 0;
        bool ready = image_decode_allocate_pixels(context, png.width, png.height, png.ihdr_offset);
        if (ready)
        {
            ready = png_filtered_size(context, &png, &filtered_size);
        }
        if (ready)
        {
            if (context->options.scratch_arena)
            {
                scratch = arena_begin_temporal(context->options.scratch_arena);
            }
            else if (thread_context_selected())
            {
                Arena* conflict = context->arena;
                scratch = scratch_begin(&conflict, 1);
                if (!image_decode_arena_can_allocate(scratch.arena, filtered_size, BUSTER_ALIGN_OF(u32)) &&
                    image_decode_arena_can_allocate(context->arena, filtered_size, BUSTER_ALIGN_OF(u32)))
                {
                    scratch = arena_begin_temporal(context->arena);
                }
            }
            else
            {
                scratch = arena_begin_temporal(context->arena);
            }
        }
        u8* filtered = ready ? image_decode_allocate(context, scratch.arena, filtered_size, BUSTER_ALIGN_OF(u32), png.first_idat_header_offset) : 0;
        ready = ready && filtered != 0;
        if (ready)
        {
            ready = png_inflate_zlib(context, &png, filtered, filtered_size);
        }
        if (ready)
        {
            png_expand_pixels(context, &png, filtered, filtered_size);
        }
    }
    if (scratch.arena)
    {
        scratch_end(scratch);
    }
}
