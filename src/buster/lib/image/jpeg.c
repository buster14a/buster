// Dependency-free JPEG reader. image_jpeg_process owns marker/table parsing,
// baseline Huffman entropy decoding, inverse DCT, component upsampling, color
// conversion and report-only JFIF/Exif/ICC/Adobe metadata. The input is
// hostile: every byte range, table index, coefficient run, allocation and unit
// of decode work is bounded.
//
// Supported frame path: 8-bit Huffman sequential DCT (SOF0/SOF1), one or three
// components, interleaved or component scans, supported integral sampling-factor
// ratios (including the common 4:4:4, 4:2:2 and 4:2:0 shapes), restart markers
// and 8/16-bit quantization tables. Progressive, arithmetic, lossless and
// extended precision frames are classified as unsupported features rather than
// corrupt baseline data.
//
// Layout map:
//   ImageJpegState, image_jpeg_process       marker grammar and codec seam
//   image_jpeg_parse_*                       frame/table/scan/metadata records
//   image_jpeg_probe_skip_scan               bounded entropy-envelope traversal
//   ImageJpegEntropyReader                   stuffed-byte entropy input
//   image_jpeg_decode_scan                   MCU/restart traversal
//   image_jpeg_inverse_dct                   staged fixed-point 8x8 transform
//   image_jpeg_compose                       upsampling and color conversion

#include <buster/lib/image/internal.h>

#define IMAGE_JPEG_COMPONENT_CAPACITY 4u
#define IMAGE_JPEG_TABLE_CAPACITY 4u
#define IMAGE_JPEG_HUFFMAN_LENGTH_CAPACITY 17u
#define IMAGE_JPEG_HUFFMAN_SYMBOL_CAPACITY 256u
#define IMAGE_JPEG_BLOCK_COEFFICIENT_COUNT 64u
#define IMAGE_JPEG_MAX_BLOCKS_PER_MCU 10u
#define IMAGE_JPEG_BLOCK_WORK 256u

typedef struct ImageJpegQuantizationTable ImageJpegQuantizationTable;
struct ImageJpegQuantizationTable
{
    u16 values[IMAGE_JPEG_BLOCK_COEFFICIENT_COUNT];
    bool defined;
};

typedef struct ImageJpegHuffmanTable ImageJpegHuffmanTable;
struct ImageJpegHuffmanTable
{
    u16 counts[IMAGE_JPEG_HUFFMAN_LENGTH_CAPACITY];
    u16 first_code[IMAGE_JPEG_HUFFMAN_LENGTH_CAPACITY];
    u16 first_symbol[IMAGE_JPEG_HUFFMAN_LENGTH_CAPACITY];
    u8 symbols[IMAGE_JPEG_HUFFMAN_SYMBOL_CAPACITY];
    u16 symbol_count;
    bool defined;
};

typedef struct ImageJpegComponent ImageJpegComponent;
struct ImageJpegComponent
{
    u8 identifier;
    u8 horizontal_sampling;
    u8 vertical_sampling;
    u8 quantization_table;
    u8 dc_table;
    u8 ac_table;
    s32 dc_predictor;
    u8* samples;
    u32 sample_width;
    u32 sample_height;
    u32 block_columns;
    u32 block_rows;
    u32 stride;
    bool decoded;
};

typedef enum ImageJpegColorTransform
{
    IMAGE_JPEG_COLOR_GRAYSCALE,
    IMAGE_JPEG_COLOR_YCBCR,
    IMAGE_JPEG_COLOR_RGB,
} ImageJpegColorTransform;

typedef struct ImageJpegState ImageJpegState;
struct ImageJpegState
{
    ImageDecodeContext* context;
    TemporalArena scratch;
    ImageJpegQuantizationTable quantization_tables[IMAGE_JPEG_TABLE_CAPACITY];
    ImageJpegHuffmanTable huffman_tables[2][IMAGE_JPEG_TABLE_CAPACITY];
    ImageJpegComponent components[IMAGE_JPEG_COMPONENT_CAPACITY];
    u64 position;
    u64 frame_offset;
    u32 width;
    u32 height;
    u16 restart_interval;
    u8 component_count;
    u8 maximum_horizontal_sampling;
    u8 maximum_vertical_sampling;
    u8 precision;
    u8 icc_segment_count;
    u8 icc_seen[32];
    s8 adobe_transform;
    u32 icc_declared_size;
    u64 icc_profile_bytes;
    u64 adobe_offset;
    ImageOrientation orientation;
    bool frame_found;
    bool scan_found;
    bool end_found;
    bool planes_ready;
    bool jfif_found;
    bool adobe_found;
    bool adobe_conflict;
    bool exif_found;
    bool icc_found;
    bool icc_header_valid;
    bool icc_invalid;
    bool orientation_found;
};

typedef struct ImageJpegEntropyReader ImageJpegEntropyReader;
struct ImageJpegEntropyReader
{
    ImageJpegState* state;
    u64 position;
    u64 byte_offset;
    u64 marker_offset;
    u8 byte;
    u8 bit_count;
    u8 marker;
};

BUSTER_GLOBAL_LOCAL u8 const image_jpeg_zigzag[IMAGE_JPEG_BLOCK_COEFFICIENT_COUNT] = {
    0, 1, 8, 16, 9, 2, 3, 10,
    17, 24, 32, 25, 18, 11, 4, 5,
    12, 19, 26, 33, 40, 48, 41, 34,
    27, 20, 13, 6, 7, 14, 21, 28,
    35, 42, 49, 56, 57, 50, 43, 36,
    29, 22, 15, 23, 30, 37, 44, 51,
    58, 59, 52, 45, 38, 31, 39, 46,
    53, 60, 61, 54, 47, 55, 62, 63,
};

// Q13 constants for the accurate integer Loeffler IDCT. This is the transform
// used by the libjpeg reference path, including its staged rounding. s64
// intermediates also cover the module's accepted 16-bit quantization tables:
// maximum legal AC intermediates remain below 2^50, while the hostile s32 DC
// predictor path remains below 2^63 through both passes.
#define IMAGE_JPEG_FIX_0_298631336 INT64_C(2446)
#define IMAGE_JPEG_FIX_0_390180644 INT64_C(3196)
#define IMAGE_JPEG_FIX_0_541196100 INT64_C(4433)
#define IMAGE_JPEG_FIX_0_765366865 INT64_C(6270)
#define IMAGE_JPEG_FIX_0_899976223 INT64_C(7373)
#define IMAGE_JPEG_FIX_1_175875602 INT64_C(9633)
#define IMAGE_JPEG_FIX_1_501321110 INT64_C(12299)
#define IMAGE_JPEG_FIX_1_847759065 INT64_C(15137)
#define IMAGE_JPEG_FIX_1_961570560 INT64_C(16069)
#define IMAGE_JPEG_FIX_2_053119869 INT64_C(16819)
#define IMAGE_JPEG_FIX_2_562915447 INT64_C(20995)
#define IMAGE_JPEG_FIX_3_072711026 INT64_C(25172)
#define IMAGE_JPEG_IDCT_CONSTANT_BITS 13u
#define IMAGE_JPEG_IDCT_PASS1_BITS 2u

BUSTER_GLOBAL_LOCAL void image_jpeg_error(ImageJpegState* state, ImageDecodeStatus status, u64 offset)
{
    image_decode_error(state->context, status, offset);
}

BUSTER_GLOBAL_LOCAL void image_jpeg_unsupported(ImageJpegState* state, ImageUnsupportedFeature feature, u64 offset)
{
    image_decode_set_unsupported_feature(state->context, feature, offset);
}

BUSTER_GLOBAL_LOCAL bool image_jpeg_payload_range(ByteSlice payload, u64 offset, u64 size)
{
    bool result = image_decode_range(payload, offset, size);
    return result;
}

BUSTER_GLOBAL_LOCAL bool image_jpeg_next_marker(ImageJpegState* state, u8* marker, u64* marker_offset)
{
    bool result = state->context->status == IMAGE_DECODE_SUCCESS && marker && marker_offset;
    ByteSlice encoded = state->context->encoded;
    if (result && state->position >= encoded.length)
    {
        image_jpeg_error(state, IMAGE_DECODE_TRUNCATED, state->position);
        result = false;
    }
    if (result && encoded.pointer[state->position] != 0xffu)
    {
        image_jpeg_error(state, IMAGE_DECODE_MALFORMED, state->position);
        result = false;
    }
    if (result)
    {
        *marker_offset = state->position;
        while (result && state->position < encoded.length && encoded.pointer[state->position] == 0xffu)
        {
            u64 byte_offset = state->position;
            state->position += 1;
            result = image_decode_add_work(state->context, 1, byte_offset);
        }
        if (result && state->position >= encoded.length)
        {
            image_jpeg_error(state, IMAGE_DECODE_TRUNCATED, state->position);
            result = false;
        }
    }
    if (result)
    {
        *marker = encoded.pointer[state->position];
        state->position += 1;
        result = image_decode_add_work(state->context, 1, state->position - 1u);
        if (result && *marker == 0)
        {
            image_jpeg_error(state, IMAGE_DECODE_MALFORMED, *marker_offset);
            result = false;
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool image_jpeg_segment(ImageJpegState* state, ByteSlice* payload, u64* payload_offset)
{
    bool result = state->context->status == IMAGE_DECODE_SUCCESS && payload && payload_offset;
    ByteSlice encoded = state->context->encoded;
    if (result && !image_decode_range(encoded, state->position, 2))
    {
        image_jpeg_error(state, IMAGE_DECODE_TRUNCATED, state->position);
        result = false;
    }
    u16 length = 0;
    if (result)
    {
        length = image_decode_u16_be(encoded.pointer + state->position);
        if (length < 2)
        {
            image_jpeg_error(state, IMAGE_DECODE_MALFORMED, state->position);
            result = false;
        }
    }
    if (result && !image_decode_range(encoded, state->position + 2, (u64)length - 2))
    {
        image_jpeg_error(state, IMAGE_DECODE_TRUNCATED, state->position + 2);
        result = false;
    }
    if (result)
    {
        *payload_offset = state->position + 2;
        *payload = (ByteSlice){.pointer = encoded.pointer + *payload_offset, .length = (u64)length - 2};
        state->position += length;
        result = image_decode_add_work(state->context, length, *payload_offset);
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool image_jpeg_is_frame_marker(u8 marker)
{
    bool result = marker == 0xc0u || marker == 0xc1u || marker == 0xc2u || marker == 0xc3u ||
                  marker == 0xc5u || marker == 0xc6u || marker == 0xc7u || marker == 0xc9u ||
                  marker == 0xcau || marker == 0xcbu || marker == 0xcdu || marker == 0xceu || marker == 0xcfu;
    return result;
}

BUSTER_GLOBAL_LOCAL s32 image_jpeg_component_index(ImageJpegState const* state, u8 identifier)
{
    s32 result = -1;
    for (u32 index = 0; index < state->component_count && result < 0; index += 1)
    {
        if (state->components[index].identifier == identifier)
        {
            result = (s32)index;
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL void image_jpeg_parse_exif(ImageJpegState* state, ByteSlice payload)
{
    bool exif = payload.length >= 14 && memcmp(payload.pointer, "Exif\0\0", 6) == 0;
    if (exif)
    {
        ByteSlice tiff = {.pointer = payload.pointer + 6, .length = payload.length - 6};
        ImageOrientation orientation = IMAGE_ORIENTATION_TOP_LEFT;
        bool orientation_found = false;
        u64 error_offset = 0;
        bool valid = image_decode_tiff_orientation(tiff, &orientation, &orientation_found, &error_offset);
        if (valid)
        {
            state->exif_found = true;
        }
        if (valid && orientation_found && !state->orientation_found)
        {
            state->orientation = orientation;
            state->orientation_found = true;
        }
    }
}

BUSTER_GLOBAL_LOCAL void image_jpeg_parse_icc(ImageJpegState* state, ByteSlice payload)
{
    bool recognized = payload.length >= 14 && memcmp(payload.pointer, "ICC_PROFILE\0", 12) == 0;
    if (recognized && !state->icc_invalid)
    {
        u8 sequence = payload.pointer[12];
        u8 count = payload.pointer[13];
        bool valid = sequence >= 1u && count >= 1u && sequence <= count;
        if (valid && state->icc_found)
        {
            valid = count == state->icc_segment_count;
        }
        if (valid && !state->icc_found)
        {
            state->icc_found = true;
            state->icc_segment_count = count;
        }
        u32 byte_index = sequence ? (u32)(sequence - 1u) >> 3u : 0;
        u8 bit = 0;
        if (sequence)
        {
            bit = (u8)(1u << ((u32)(sequence - 1u) & 7u));
        }
        if (valid)
        {
            valid = !(state->icc_seen[byte_index] & bit);
        }
        u64 profile_bytes = payload.length - 14u;
        if (valid)
        {
            valid = profile_bytes <= UINT64_MAX - state->icc_profile_bytes;
        }
        if (valid && sequence == 1u)
        {
            ByteSlice profile = {.pointer = payload.pointer + 14, .length = profile_bytes};
            valid = profile.length >= 132u && memcmp(profile.pointer + 36u, "acsp", 4) == 0;
            if (valid)
            {
                state->icc_declared_size = image_decode_u32_be(profile.pointer);
                valid = state->icc_declared_size >= 132u;
            }
            state->icc_header_valid = valid;
        }
        if (valid)
        {
            state->icc_seen[byte_index] |= bit;
            state->icc_profile_bytes += profile_bytes;
        }
        else
        {
            state->icc_invalid = true;
        }
    }
}

BUSTER_GLOBAL_LOCAL bool image_jpeg_icc_complete(ImageJpegState const* state)
{
    bool result = state->icc_found && !state->icc_invalid && state->icc_header_valid &&
                  state->icc_profile_bytes == state->icc_declared_size;
    for (u32 sequence = 0; sequence < state->icc_segment_count && result; sequence += 1)
    {
        result = (state->icc_seen[sequence >> 3u] & (u8)(1u << (sequence & 7u))) != 0;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL void image_jpeg_parse_application(ImageJpegState* state, u8 marker, ByteSlice payload, u64 marker_offset)
{
    if (marker == 0xe0u && payload.length >= 14 && memcmp(payload.pointer, "JFIF\0", 5) == 0)
    {
        state->jfif_found = true;
    }
    else if (marker == 0xe1u)
    {
        image_jpeg_parse_exif(state, payload);
    }
    else if (marker == 0xe2u)
    {
        image_jpeg_parse_icc(state, payload);
    }
    else if (marker == 0xeeu && payload.length >= 12 && memcmp(payload.pointer, "Adobe", 5) == 0)
    {
        s8 transform = (s8)payload.pointer[11];
        if (!state->adobe_found)
        {
            state->adobe_found = true;
            state->adobe_transform = transform;
            state->adobe_offset = marker_offset;
        }
        else if (state->adobe_transform != transform)
        {
            state->adobe_conflict = true;
        }
    }
}

BUSTER_GLOBAL_LOCAL void image_jpeg_parse_quantization(ImageJpegState* state, ByteSlice payload, u64 payload_offset)
{
    u64 position = 0;
    if (!payload.length)
    {
        image_jpeg_error(state, IMAGE_DECODE_MALFORMED, payload_offset);
    }
    while (position < payload.length && state->context->status == IMAGE_DECODE_SUCCESS)
    {
        u64 table_offset = position;
        u8 description = payload.pointer[position];
        position += 1;
        u8 precision = description >> 4u;
        u8 index = description & 15u;
        u64 value_size = precision == 0 ? 1u : 2u;
        u64 values_size = IMAGE_JPEG_BLOCK_COEFFICIENT_COUNT * value_size;
        if (precision > 1 || index >= IMAGE_JPEG_TABLE_CAPACITY || !image_jpeg_payload_range(payload, position, values_size))
        {
            image_jpeg_error(state, IMAGE_DECODE_MALFORMED, payload_offset + table_offset);
        }
        else
        {
            ImageJpegQuantizationTable* table = &state->quantization_tables[index];
            bool valid = true;
            for (u32 value_index = 0; value_index < IMAGE_JPEG_BLOCK_COEFFICIENT_COUNT; value_index += 1)
            {
                u16 value = precision == 0 ? payload.pointer[position] : image_decode_u16_be(payload.pointer + position);
                position += value_size;
                if (!value)
                {
                    valid = false;
                }
                table->values[image_jpeg_zigzag[value_index]] = value;
            }
            if (valid)
            {
                table->defined = true;
            }
            else
            {
                image_jpeg_error(state, IMAGE_DECODE_MALFORMED, payload_offset + table_offset);
            }
        }
    }
}

BUSTER_GLOBAL_LOCAL bool image_jpeg_build_huffman(ImageJpegHuffmanTable* table)
{
    bool result = table && table->symbol_count;
    u32 code = 0;
    u32 symbol = 0;
    for (u32 length = 1; length <= 16 && result; length += 1)
    {
        u32 count = table->counts[length];
        u32 capacity = UINT32_C(1) << length;
        result = code <= capacity && count <= capacity - code;
        if (result)
        {
            table->first_code[length] = (u16)code;
            table->first_symbol[length] = (u16)symbol;
            symbol += count;
            if (length == 16)
            {
                // JPEG reserves the all-ones code for entropy padding.
                result = code + count < capacity;
            }
            else
            {
                code = (code + count) << 1u;
            }
        }
    }
    result = result && symbol == table->symbol_count;
    if (result)
    {
        table->defined = true;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL void image_jpeg_parse_huffman(ImageJpegState* state, ByteSlice payload, u64 payload_offset)
{
    u64 position = 0;
    if (!payload.length)
    {
        image_jpeg_error(state, IMAGE_DECODE_MALFORMED, payload_offset);
    }
    while (position < payload.length && state->context->status == IMAGE_DECODE_SUCCESS)
    {
        u64 table_offset = position;
        u8 description = payload.pointer[position];
        position += 1;
        u8 table_class = description >> 4u;
        u8 index = description & 15u;
        if (table_class > 1 || index >= IMAGE_JPEG_TABLE_CAPACITY || !image_jpeg_payload_range(payload, position, 16))
        {
            image_jpeg_error(state, IMAGE_DECODE_MALFORMED, payload_offset + table_offset);
        }
        else
        {
            ImageJpegHuffmanTable* table = &state->huffman_tables[table_class][index];
            memset(table, 0, sizeof(*table));
            u32 symbol_count = 0;
            for (u32 length = 1; length <= 16; length += 1)
            {
                table->counts[length] = payload.pointer[position];
                symbol_count += payload.pointer[position];
                position += 1;
            }
            if (symbol_count > IMAGE_JPEG_HUFFMAN_SYMBOL_CAPACITY || !image_jpeg_payload_range(payload, position, symbol_count))
            {
                image_jpeg_error(state, IMAGE_DECODE_MALFORMED, payload_offset + table_offset);
            }
            else
            {
                memcpy(table->symbols, payload.pointer + position, symbol_count);
                table->symbol_count = (u16)symbol_count;
                position += symbol_count;
                if (!image_jpeg_build_huffman(table))
                {
                    image_jpeg_error(state, IMAGE_DECODE_MALFORMED, payload_offset + table_offset);
                }
            }
        }
    }
}

BUSTER_GLOBAL_LOCAL void image_jpeg_parse_frame(ImageJpegState* state, u8 marker, ByteSlice payload, u64 payload_offset, u64 marker_offset)
{
    bool valid = state->context->status == IMAGE_DECODE_SUCCESS;
    if (valid && state->frame_found)
    {
        image_jpeg_error(state, IMAGE_DECODE_MALFORMED, marker_offset);
        valid = false;
    }
    if (valid && payload.length < 6)
    {
        image_jpeg_error(state, IMAGE_DECODE_MALFORMED, payload_offset);
        valid = false;
    }
    u8 component_count = valid ? payload.pointer[5] : 0;
    u64 expected_size = 6u + (u64)component_count * 3u;
    if (valid && (component_count < 1 || payload.length != expected_size))
    {
        image_jpeg_error(state, IMAGE_DECODE_MALFORMED, payload_offset + 5);
        valid = false;
    }
    if (valid && component_count > IMAGE_JPEG_COMPONENT_CAPACITY)
    {
        image_jpeg_unsupported(state, IMAGE_UNSUPPORTED_FEATURE_COMPONENT_MODEL, payload_offset + 5u);
        valid = false;
    }
    if (valid)
    {
        state->frame_found = true;
        state->frame_offset = marker_offset;
        state->precision = payload.pointer[0];
        state->height = image_decode_u16_be(payload.pointer + 1);
        state->width = image_decode_u16_be(payload.pointer + 3);
        state->component_count = component_count;
        u32 blocks_per_mcu = 0;
        for (u32 index = 0; index < component_count && valid; index += 1)
        {
            u64 component_offset = 6u + (u64)index * 3u;
            ImageJpegComponent* component = &state->components[index];
            component->identifier = payload.pointer[component_offset];
            component->horizontal_sampling = payload.pointer[component_offset + 1] >> 4u;
            component->vertical_sampling = payload.pointer[component_offset + 1] & 15u;
            component->quantization_table = payload.pointer[component_offset + 2];
            bool unique = image_jpeg_component_index(state, component->identifier) == (s32)index;
            valid = unique && component->horizontal_sampling >= 1 && component->horizontal_sampling <= 4 &&
                    component->vertical_sampling >= 1 && component->vertical_sampling <= 4 &&
                    component->quantization_table < IMAGE_JPEG_TABLE_CAPACITY;
            if (valid)
            {
                state->maximum_horizontal_sampling = (u8)BUSTER_MAX(state->maximum_horizontal_sampling, component->horizontal_sampling);
                state->maximum_vertical_sampling = (u8)BUSTER_MAX(state->maximum_vertical_sampling, component->vertical_sampling);
                blocks_per_mcu += (u32)component->horizontal_sampling * component->vertical_sampling;
                valid = blocks_per_mcu <= IMAGE_JPEG_MAX_BLOCKS_PER_MCU;
            }
            if (!valid)
            {
                image_jpeg_error(state, IMAGE_DECODE_MALFORMED, payload_offset + component_offset);
            }
        }
    }
    if (valid && marker != 0xc0u && marker != 0xc1u)
    {
        image_jpeg_unsupported(state, IMAGE_UNSUPPORTED_FEATURE_CODING, marker_offset);
        valid = false;
    }
    if (valid && state->precision != 8u)
    {
        image_jpeg_unsupported(state, IMAGE_UNSUPPORTED_FEATURE_PRECISION, payload_offset);
        valid = false;
    }
    if (valid && state->height == 0)
    {
        image_jpeg_unsupported(state, IMAGE_UNSUPPORTED_FEATURE_PROFILE, payload_offset + 1u);
        valid = false;
    }
    if (valid && state->component_count != 1 && state->component_count != 3)
    {
        image_jpeg_unsupported(state, IMAGE_UNSUPPORTED_FEATURE_COMPONENT_MODEL, payload_offset + 5u);
        valid = false;
    }
    if (valid && state->context->status == IMAGE_DECODE_SUCCESS)
    {
        for (u32 index = 0; index < state->component_count && valid; index += 1)
        {
            ImageJpegComponent const* component = &state->components[index];
            valid = state->maximum_horizontal_sampling % component->horizontal_sampling == 0 &&
                    state->maximum_vertical_sampling % component->vertical_sampling == 0;
            if (!valid)
            {
                image_jpeg_unsupported(state, IMAGE_UNSUPPORTED_FEATURE_PROFILE,
                                       payload_offset + 6u + (u64)index * 3u + 1u);
            }
        }
    }
}

BUSTER_GLOBAL_LOCAL void image_jpeg_parse_restart_interval(ImageJpegState* state, ByteSlice payload, u64 payload_offset)
{
    if (payload.length == 2)
    {
        state->restart_interval = image_decode_u16_be(payload.pointer);
    }
    else
    {
        image_jpeg_error(state, IMAGE_DECODE_MALFORMED, payload_offset);
    }
}

BUSTER_GLOBAL_LOCAL u8* image_jpeg_scratch_allocate(ImageJpegState* state, u64 size, u64 alignment, u64 offset)
{
    u8* result = 0;
    if (state->context->status == IMAGE_DECODE_SUCCESS)
    {
        u64 aligned_position = 0;
        Arena* scratch = state->scratch.arena;
        bool valid = scratch && size <= ARENA_MAX_RESERVATION && align_forward_checked(scratch->position, alignment, &aligned_position) &&
                     aligned_position <= scratch->reserved_size && size <= scratch->reserved_size - aligned_position;
        if (valid)
        {
            result = (u8*)arena_allocate_bytes(scratch, size, alignment);
        }
        else
        {
            image_jpeg_error(state, IMAGE_DECODE_CAPACITY_EXCEEDED, offset);
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool image_jpeg_prepare_planes(ImageJpegState* state, u64 offset)
{
    bool result = state->context->status == IMAGE_DECODE_SUCCESS;
    if (result && !state->planes_ready)
    {
        result = image_decode_allocate_pixels(state->context, state->context->information.width,
                                              state->context->information.height, offset);
        if (result && state->context->options.scratch_arena)
        {
            state->scratch = arena_begin_temporal(state->context->options.scratch_arena);
        }
        else if (result && thread_context_selected())
        {
            Arena* conflict = state->context->arena;
            state->scratch = scratch_begin(&conflict, 1);
        }
        else if (result)
        {
            state->scratch = arena_begin_temporal(state->context->arena);
        }
        u32 mcu_width = (u32)state->maximum_horizontal_sampling * 8u;
        u32 mcu_height = (u32)state->maximum_vertical_sampling * 8u;
        u32 mcu_columns = 1u + (state->width - 1u) / mcu_width;
        u32 mcu_rows = 1u + (state->height - 1u) / mcu_height;
        for (u32 index = 0; index < state->component_count && result; index += 1)
        {
            ImageJpegComponent* component = &state->components[index];
            u64 scaled_width = (u64)state->width * component->horizontal_sampling;
            u64 scaled_height = (u64)state->height * component->vertical_sampling;
            component->sample_width = (u32)((scaled_width + state->maximum_horizontal_sampling - 1u) / state->maximum_horizontal_sampling);
            component->sample_height = (u32)((scaled_height + state->maximum_vertical_sampling - 1u) / state->maximum_vertical_sampling);
            component->block_columns = mcu_columns * component->horizontal_sampling;
            component->block_rows = mcu_rows * component->vertical_sampling;
            component->stride = component->block_columns * 8u;
            u64 plane_size = (u64)component->stride * component->block_rows * 8u;
            component->samples = image_jpeg_scratch_allocate(state, plane_size, BUSTER_ALIGN_OF(u32), offset);
            result = component->samples != 0;
        }
        if (result)
        {
            state->planes_ready = true;
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool image_jpeg_entropy_byte(ImageJpegEntropyReader* reader, u8* value)
{
    bool result = reader && reader->state && value && reader->state->context->status == IMAGE_DECODE_SUCCESS;
    ByteSlice encoded = result ? reader->state->context->encoded : (ByteSlice){0};
    if (result && reader->position >= encoded.length)
    {
        image_jpeg_error(reader->state, IMAGE_DECODE_TRUNCATED, reader->position);
        result = false;
    }
    if (result)
    {
        u64 byte_offset = reader->position;
        reader->byte_offset = byte_offset;
        u8 byte = encoded.pointer[reader->position];
        reader->position += 1;
        result = image_decode_add_work(reader->state->context, 1, byte_offset);
        if (result && byte == 0xffu)
        {
            u64 marker_offset = reader->position - 1u;
            if (reader->position >= encoded.length)
            {
                image_jpeg_error(reader->state, IMAGE_DECODE_TRUNCATED, reader->position);
                result = false;
            }
            else
            {
                u8 following = encoded.pointer[reader->position];
                if (following == 0)
                {
                    reader->position += 1;
                    result = image_decode_add_work(reader->state->context, 1, reader->position - 1u);
                    if (result)
                    {
                        *value = 0xffu;
                    }
                }
                else
                {
                    reader->marker = following;
                    reader->marker_offset = marker_offset;
                    image_jpeg_error(reader->state, IMAGE_DECODE_MALFORMED, marker_offset);
                    result = false;
                }
            }
        }
        else if (result)
        {
            *value = byte;
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool image_jpeg_entropy_padding(ImageJpegEntropyReader* reader)
{
    bool result = reader && reader->state && reader->state->context->status == IMAGE_DECODE_SUCCESS;
    if (result && reader->bit_count)
    {
        u32 mask = (UINT32_C(1) << reader->bit_count) - 1u;
        if (((u32)reader->byte & mask) != mask)
        {
            image_jpeg_error(reader->state, IMAGE_DECODE_MALFORMED, reader->byte_offset);
            result = false;
        }
    }
    if (result)
    {
        reader->bit_count = 0;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool image_jpeg_entropy_bits(ImageJpegEntropyReader* reader, u32 count, u32* value)
{
    bool result = reader && value && count <= 16 && reader->state->context->status == IMAGE_DECODE_SUCCESS;
    u32 bits = 0;
    u32 remaining = count;
    while (remaining && result)
    {
        if (!reader->bit_count)
        {
            result = image_jpeg_entropy_byte(reader, &reader->byte);
            if (result)
            {
                reader->bit_count = 8;
            }
        }
        if (result)
        {
            u32 take = remaining < reader->bit_count ? remaining : reader->bit_count;
            u32 shift = (u32)reader->bit_count - take;
            u32 mask = (UINT32_C(1) << take) - 1u;
            bits = (bits << take) | ((u32)reader->byte >> shift & mask);
            reader->bit_count = (u8)((u32)reader->bit_count - take);
            remaining -= take;
        }
    }
    if (result)
    {
        *value = bits;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool image_jpeg_huffman_symbol(ImageJpegEntropyReader* reader, ImageJpegHuffmanTable const* table, u8* symbol)
{
    bool result = reader && table && symbol && table->defined && reader->state->context->status == IMAGE_DECODE_SUCCESS;
    u32 code = 0;
    bool found = false;
    for (u32 length = 1; length <= 16 && result && !found; length += 1)
    {
        u32 bit = 0;
        result = image_jpeg_entropy_bits(reader, 1, &bit);
        if (result)
        {
            code = code * 2u + bit;
            u32 first_code = table->first_code[length];
            u32 count = table->counts[length];
            if (count && code >= first_code && code - first_code < count)
            {
                u32 index = (u32)table->first_symbol[length] + code - first_code;
                if (index < table->symbol_count)
                {
                    *symbol = table->symbols[index];
                    found = true;
                }
                else
                {
                    result = false;
                }
            }
        }
    }
    if (result && !found)
    {
        image_jpeg_error(reader->state, IMAGE_DECODE_MALFORMED, reader->position);
        result = false;
    }
    if (!result && reader && reader->state && reader->state->context->status == IMAGE_DECODE_SUCCESS)
    {
        image_jpeg_error(reader->state, IMAGE_DECODE_MALFORMED, reader->position);
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool image_jpeg_receive(ImageJpegEntropyReader* reader, u32 size, s32* value)
{
    bool result = reader && value && size <= 16;
    u32 bits = 0;
    if (result && size)
    {
        result = image_jpeg_entropy_bits(reader, size, &bits);
    }
    if (result)
    {
        s32 signed_value = (s32)bits;
        if (size && bits < (UINT32_C(1) << (size - 1u)))
        {
            signed_value -= (s32)((UINT32_C(1) << size) - 1u);
        }
        *value = signed_value;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL s64 image_jpeg_round_scale(s64 value, s64 scale)
{
    s64 result = value >= 0 ? (value + scale / 2) / scale : -((-value + scale / 2) / scale);
    return result;
}

BUSTER_GLOBAL_LOCAL s64 image_jpeg_descale(s64 value, u32 bits)
{
    s64 scale = INT64_C(1) << bits;
    s64 half = scale / 2;
    // Match an arithmetic right shift after adding half, without depending on
    // implementation-defined right shift of a negative signed integer.
    s64 result = value >= 0 ? (value + half) / scale : -((-value + half - 1) / scale);
    return result;
}

BUSTER_GLOBAL_LOCAL u8 image_jpeg_clamp_sample(s64 value)
{
    u8 result = value <= 0 ? 0 : value >= 255 ? 255 : (u8)value;
    return result;
}

BUSTER_GLOBAL_LOCAL void image_jpeg_inverse_dct(ImageJpegState* state, ImageJpegComponent* component, s32 const coefficients[64], bool ac_nonzero,
                                                u32 block_x, u32 block_y)
{
    ImageJpegQuantizationTable const* quantization = &state->quantization_tables[component->quantization_table];
    u8* destination = component->samples + (u64)block_y * 8u * component->stride + (u64)block_x * 8u;
    if (!ac_nonzero)
    {
        s64 dc = (s64)coefficients[0] * quantization->values[0];
        u8 sample = image_jpeg_clamp_sample(128 + image_jpeg_descale(dc, 3));
        for (u32 y = 0; y < 8; y += 1)
        {
            memset(destination + (u64)y * component->stride, sample, 8);
        }
    }
    else
    {
        s64 dequantized[64];
        s64 workspace[64];
        for (u32 index = 0; index < 64; index += 1)
        {
            dequantized[index] = (s64)coefficients[index] * quantization->values[index];
        }
        for (u32 x = 0; x < 8; x += 1)
        {
            s64 z2 = dequantized[2u * 8u + x];
            s64 z3 = dequantized[6u * 8u + x];
            s64 z1 = (z2 + z3) * IMAGE_JPEG_FIX_0_541196100;
            s64 temporary2 = z1 - z3 * IMAGE_JPEG_FIX_1_847759065;
            s64 temporary3 = z1 + z2 * IMAGE_JPEG_FIX_0_765366865;

            z2 = dequantized[x];
            z3 = dequantized[4u * 8u + x];
            s64 temporary0 = (z2 + z3) * (INT64_C(1) << IMAGE_JPEG_IDCT_CONSTANT_BITS);
            s64 temporary1 = (z2 - z3) * (INT64_C(1) << IMAGE_JPEG_IDCT_CONSTANT_BITS);
            s64 temporary10 = temporary0 + temporary3;
            s64 temporary13 = temporary0 - temporary3;
            s64 temporary11 = temporary1 + temporary2;
            s64 temporary12 = temporary1 - temporary2;

            temporary0 = dequantized[7u * 8u + x];
            temporary1 = dequantized[5u * 8u + x];
            temporary2 = dequantized[3u * 8u + x];
            temporary3 = dequantized[1u * 8u + x];
            z1 = temporary0 + temporary3;
            z2 = temporary1 + temporary2;
            z3 = temporary0 + temporary2;
            s64 z4 = temporary1 + temporary3;
            s64 z5 = (z3 + z4) * IMAGE_JPEG_FIX_1_175875602;

            temporary0 *= IMAGE_JPEG_FIX_0_298631336;
            temporary1 *= IMAGE_JPEG_FIX_2_053119869;
            temporary2 *= IMAGE_JPEG_FIX_3_072711026;
            temporary3 *= IMAGE_JPEG_FIX_1_501321110;
            z1 *= -IMAGE_JPEG_FIX_0_899976223;
            z2 *= -IMAGE_JPEG_FIX_2_562915447;
            z3 *= -IMAGE_JPEG_FIX_1_961570560;
            z4 *= -IMAGE_JPEG_FIX_0_390180644;
            z3 += z5;
            z4 += z5;
            temporary0 += z1 + z3;
            temporary1 += z2 + z4;
            temporary2 += z2 + z3;
            temporary3 += z1 + z4;

            u32 first_shift = IMAGE_JPEG_IDCT_CONSTANT_BITS - IMAGE_JPEG_IDCT_PASS1_BITS;
            workspace[x] = image_jpeg_descale(temporary10 + temporary3, first_shift);
            workspace[7u * 8u + x] = image_jpeg_descale(temporary10 - temporary3, first_shift);
            workspace[1u * 8u + x] = image_jpeg_descale(temporary11 + temporary2, first_shift);
            workspace[6u * 8u + x] = image_jpeg_descale(temporary11 - temporary2, first_shift);
            workspace[2u * 8u + x] = image_jpeg_descale(temporary12 + temporary1, first_shift);
            workspace[5u * 8u + x] = image_jpeg_descale(temporary12 - temporary1, first_shift);
            workspace[3u * 8u + x] = image_jpeg_descale(temporary13 + temporary0, first_shift);
            workspace[4u * 8u + x] = image_jpeg_descale(temporary13 - temporary0, first_shift);
        }
        for (u32 y = 0; y < 8; y += 1)
        {
            s64* row = workspace + y * 8u;
            s64 z2 = row[2];
            s64 z3 = row[6];
            s64 z1 = (z2 + z3) * IMAGE_JPEG_FIX_0_541196100;
            s64 temporary2 = z1 - z3 * IMAGE_JPEG_FIX_1_847759065;
            s64 temporary3 = z1 + z2 * IMAGE_JPEG_FIX_0_765366865;

            s64 temporary0 = (row[0] + row[4]) * (INT64_C(1) << IMAGE_JPEG_IDCT_CONSTANT_BITS);
            s64 temporary1 = (row[0] - row[4]) * (INT64_C(1) << IMAGE_JPEG_IDCT_CONSTANT_BITS);
            s64 temporary10 = temporary0 + temporary3;
            s64 temporary13 = temporary0 - temporary3;
            s64 temporary11 = temporary1 + temporary2;
            s64 temporary12 = temporary1 - temporary2;

            temporary0 = row[7];
            temporary1 = row[5];
            temporary2 = row[3];
            temporary3 = row[1];
            z1 = temporary0 + temporary3;
            z2 = temporary1 + temporary2;
            z3 = temporary0 + temporary2;
            s64 z4 = temporary1 + temporary3;
            s64 z5 = (z3 + z4) * IMAGE_JPEG_FIX_1_175875602;

            temporary0 *= IMAGE_JPEG_FIX_0_298631336;
            temporary1 *= IMAGE_JPEG_FIX_2_053119869;
            temporary2 *= IMAGE_JPEG_FIX_3_072711026;
            temporary3 *= IMAGE_JPEG_FIX_1_501321110;
            z1 *= -IMAGE_JPEG_FIX_0_899976223;
            z2 *= -IMAGE_JPEG_FIX_2_562915447;
            z3 *= -IMAGE_JPEG_FIX_1_961570560;
            z4 *= -IMAGE_JPEG_FIX_0_390180644;
            z3 += z5;
            z4 += z5;
            temporary0 += z1 + z3;
            temporary1 += z2 + z4;
            temporary2 += z2 + z3;
            temporary3 += z1 + z4;

            u32 second_shift = IMAGE_JPEG_IDCT_CONSTANT_BITS + IMAGE_JPEG_IDCT_PASS1_BITS + 3u;
            destination[(u64)y * component->stride] = image_jpeg_clamp_sample(128 + image_jpeg_descale(temporary10 + temporary3, second_shift));
            destination[(u64)y * component->stride + 7u] = image_jpeg_clamp_sample(128 + image_jpeg_descale(temporary10 - temporary3, second_shift));
            destination[(u64)y * component->stride + 1u] = image_jpeg_clamp_sample(128 + image_jpeg_descale(temporary11 + temporary2, second_shift));
            destination[(u64)y * component->stride + 6u] = image_jpeg_clamp_sample(128 + image_jpeg_descale(temporary11 - temporary2, second_shift));
            destination[(u64)y * component->stride + 2u] = image_jpeg_clamp_sample(128 + image_jpeg_descale(temporary12 + temporary1, second_shift));
            destination[(u64)y * component->stride + 5u] = image_jpeg_clamp_sample(128 + image_jpeg_descale(temporary12 - temporary1, second_shift));
            destination[(u64)y * component->stride + 3u] = image_jpeg_clamp_sample(128 + image_jpeg_descale(temporary13 + temporary0, second_shift));
            destination[(u64)y * component->stride + 4u] = image_jpeg_clamp_sample(128 + image_jpeg_descale(temporary13 - temporary0, second_shift));
        }
    }
}

BUSTER_GLOBAL_LOCAL bool image_jpeg_decode_block(ImageJpegEntropyReader* reader, ImageJpegComponent* component, u32 block_x, u32 block_y)
{
    ImageJpegState* state = reader->state;
    u64 block_offset = reader->bit_count ? reader->byte_offset : reader->position;
    bool result = image_decode_count_limit(state->context, IMAGE_EXCEEDED_LIMIT_BLOCKS, 1, block_offset);
    if (result)
    {
        result = image_decode_add_work(state->context, IMAGE_JPEG_BLOCK_WORK, block_offset);
    }
    s32 coefficients[64] = {0};
    bool ac_nonzero = false;
    ImageJpegHuffmanTable const* dc_table = &state->huffman_tables[0][component->dc_table];
    ImageJpegHuffmanTable const* ac_table = &state->huffman_tables[1][component->ac_table];
    u8 symbol = 0;
    if (result)
    {
        result = image_jpeg_huffman_symbol(reader, dc_table, &symbol);
    }
    if (result && symbol > 11)
    {
        image_jpeg_error(state, IMAGE_DECODE_MALFORMED, reader->position);
        result = false;
    }
    s32 difference = 0;
    if (result)
    {
        result = image_jpeg_receive(reader, symbol, &difference);
    }
    if (result)
    {
        s64 predictor = (s64)component->dc_predictor + difference;
        if (predictor < INT32_MIN || predictor > INT32_MAX)
        {
            image_jpeg_error(state, IMAGE_DECODE_MALFORMED, reader->position);
            result = false;
        }
        else
        {
            component->dc_predictor = (s32)predictor;
            coefficients[0] = component->dc_predictor;
        }
    }
    u32 coefficient = 1;
    while (result && coefficient < 64)
    {
        result = image_jpeg_huffman_symbol(reader, ac_table, &symbol);
        if (result)
        {
            u32 run = symbol >> 4u;
            u32 size = symbol & 15u;
            if (!size)
            {
                if (!run)
                {
                    coefficient = 64;
                }
                else if (run == 15 && coefficient <= 48)
                {
                    coefficient += 16;
                }
                else
                {
                    image_jpeg_error(state, IMAGE_DECODE_MALFORMED, reader->position);
                    result = false;
                }
            }
            else if (size > 10 || run > 63u - coefficient)
            {
                image_jpeg_error(state, IMAGE_DECODE_MALFORMED, reader->position);
                result = false;
            }
            else
            {
                coefficient += run;
                s32 value = 0;
                result = image_jpeg_receive(reader, size, &value);
                if (result)
                {
                    coefficients[image_jpeg_zigzag[coefficient]] = value;
                    ac_nonzero = ac_nonzero || value != 0;
                    coefficient += 1;
                }
            }
        }
    }
    if (result)
    {
        image_jpeg_inverse_dct(state, component, coefficients, ac_nonzero, block_x, block_y);
    }
    return result;
}

BUSTER_GLOBAL_LOCAL void image_jpeg_reset_predictors(ImageJpegState* state)
{
    for (u32 index = 0; index < state->component_count; index += 1)
    {
        state->components[index].dc_predictor = 0;
    }
}

BUSTER_GLOBAL_LOCAL bool image_jpeg_restart(ImageJpegEntropyReader* reader, u8 expected_marker)
{
    bool result = reader && reader->state->context->status == IMAGE_DECODE_SUCCESS;
    if (result)
    {
        result = image_jpeg_entropy_padding(reader);
    }
    ByteSlice encoded = result ? reader->state->context->encoded : (ByteSlice){0};
    if (result && reader->position >= encoded.length)
    {
        image_jpeg_error(reader->state, IMAGE_DECODE_TRUNCATED, reader->position);
        result = false;
    }
    u64 marker_offset = reader->position;
    if (result && encoded.pointer[reader->position] != 0xffu)
    {
        image_jpeg_error(reader->state, IMAGE_DECODE_MALFORMED, reader->position);
        result = false;
    }
    if (result)
    {
        while (result && reader->position < encoded.length && encoded.pointer[reader->position] == 0xffu)
        {
            u64 byte_offset = reader->position;
            reader->position += 1;
            result = image_decode_add_work(reader->state->context, 1, byte_offset);
        }
        if (result && reader->position >= encoded.length)
        {
            image_jpeg_error(reader->state, IMAGE_DECODE_TRUNCATED, reader->position);
            result = false;
        }
    }
    if (result)
    {
        u8 marker = encoded.pointer[reader->position];
        reader->position += 1;
        result = image_decode_add_work(reader->state->context, 1, reader->position - 1u);
        if (result && marker != expected_marker)
        {
            image_jpeg_error(reader->state, IMAGE_DECODE_MALFORMED, marker_offset);
            result = false;
        }
    }
    if (result)
    {
        image_jpeg_reset_predictors(reader->state);
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool image_jpeg_decode_scan(ImageJpegState* state, u8 const scan_components[4], u32 scan_component_count)
{
    bool result = state->context->status == IMAGE_DECODE_SUCCESS && scan_component_count >= 1 && scan_component_count <= state->component_count;
    ImageJpegEntropyReader entropy = {.state = state, .position = state->position};
    image_jpeg_reset_predictors(state);
    bool interleaved = scan_component_count > 1;
    u32 mcu_columns = 0;
    u32 mcu_rows = 0;
    if (result && interleaved)
    {
        u32 mcu_width = (u32)state->maximum_horizontal_sampling * 8u;
        u32 mcu_height = (u32)state->maximum_vertical_sampling * 8u;
        mcu_columns = 1u + (state->width - 1u) / mcu_width;
        mcu_rows = 1u + (state->height - 1u) / mcu_height;
    }
    else if (result)
    {
        ImageJpegComponent const* component = &state->components[scan_components[0]];
        mcu_columns = 1u + (component->sample_width - 1u) / 8u;
        mcu_rows = 1u + (component->sample_height - 1u) / 8u;
    }
    u64 mcu_count = (u64)mcu_columns * mcu_rows;
    u64 mcu_index = 0;
    u8 restart_marker = 0xd0u;
    while (mcu_index < mcu_count && result)
    {
        u32 mcu_x = (u32)(mcu_index % mcu_columns);
        u32 mcu_y = (u32)(mcu_index / mcu_columns);
        for (u32 scan_index = 0; scan_index < scan_component_count && result; scan_index += 1)
        {
            ImageJpegComponent* component = &state->components[scan_components[scan_index]];
            u32 horizontal_blocks = interleaved ? component->horizontal_sampling : 1u;
            u32 vertical_blocks = interleaved ? component->vertical_sampling : 1u;
            for (u32 block_y = 0; block_y < vertical_blocks && result; block_y += 1)
            {
                for (u32 block_x = 0; block_x < horizontal_blocks && result; block_x += 1)
                {
                    u32 destination_x = interleaved ? mcu_x * component->horizontal_sampling + block_x : mcu_x;
                    u32 destination_y = interleaved ? mcu_y * component->vertical_sampling + block_y : mcu_y;
                    if (destination_x >= component->block_columns || destination_y >= component->block_rows)
                    {
                        image_jpeg_error(state, IMAGE_DECODE_MALFORMED, entropy.position);
                        result = false;
                    }
                    else
                    {
                        result = image_jpeg_decode_block(&entropy, component, destination_x, destination_y);
                    }
                }
            }
        }
        mcu_index += result ? 1u : 0u;
        if (result && state->restart_interval && mcu_index < mcu_count && mcu_index % state->restart_interval == 0)
        {
            result = image_jpeg_restart(&entropy, restart_marker);
            restart_marker = (u8)(0xd0u + ((restart_marker - 0xd0u + 1u) & 7u));
        }
    }
    if (result)
    {
        result = image_jpeg_entropy_padding(&entropy);
    }
    state->position = entropy.position;
    return result;
}

BUSTER_GLOBAL_LOCAL bool image_jpeg_component_indices(ImageJpegState* state, ImageJpegColorTransform* transform, u8 indices[3]);

BUSTER_GLOBAL_LOCAL bool image_jpeg_set_information(ImageJpegState* state, u64 offset)
{
    bool result = state->context->status == IMAGE_DECODE_SUCCESS && state->frame_found;
    ImageJpegColorTransform transform = IMAGE_JPEG_COLOR_GRAYSCALE;
    u8 indices[3] = {0};
    if (result)
    {
        result = image_jpeg_component_indices(state, &transform, indices);
    }
    if (result)
    {
        result = image_decode_set_information(state->context, state->width, state->height, 1, state->component_count,
                                              state->precision, false, state->orientation, offset);
    }
    if (result)
    {
        ImageColorMetadataFlags flags = IMAGE_COLOR_METADATA_NONE;
        ImageColorModel color_model = IMAGE_COLOR_MODEL_GRAYSCALE;
        flags |= state->exif_found ? IMAGE_COLOR_METADATA_EXIF : 0;
        flags |= image_jpeg_icc_complete(state) ? IMAGE_COLOR_METADATA_ICC_PROFILE : 0;
        flags |= state->adobe_found ? IMAGE_COLOR_METADATA_ADOBE_TRANSFORM : 0;
        if (transform == IMAGE_JPEG_COLOR_RGB)
        {
            color_model = IMAGE_COLOR_MODEL_RGB;
        }
        else if (transform == IMAGE_JPEG_COLOR_YCBCR)
        {
            color_model = IMAGE_COLOR_MODEL_YCBCR;
        }
        state->context->information.source_color_model = color_model;
        state->context->information.color_metadata_flags = flags;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool image_jpeg_probe_skip_scan(ImageJpegState* state)
{
    bool result = state && state->context->status == IMAGE_DECODE_SUCCESS;
    ByteSlice encoded = result ? state->context->encoded : (ByteSlice){0};
    bool marker_found = false;
    bool marker_fill = false;
    while (result && !marker_found && state->position < encoded.length)
    {
        u64 offset = state->position;
        if (encoded.pointer[offset] != 0xffu)
        {
            marker_fill = false;
            state->position += 1;
            result = image_decode_add_work(state->context, 1, offset);
        }
        else
        {
            if (offset + 1u == encoded.length)
            {
                state->position += 1;
                result = image_decode_add_work(state->context, 1, offset);
            }
            else
            {
                u8 marker = encoded.pointer[offset + 1u];
                bool stuffed = marker == 0;
                bool restart = marker >= 0xd0u && marker <= 0xd7u;
                if (stuffed && marker_fill)
                {
                    image_jpeg_error(state, IMAGE_DECODE_MALFORMED, offset);
                    result = false;
                }
                else if (stuffed || restart)
                {
                    marker_fill = false;
                    state->position += 2;
                    result = image_decode_add_work(state->context, 2, offset);
                }
                else if (marker == 0xffu)
                {
                    // Optional fill bytes precede marker codes. Consume one
                    // at a time so a low work budget interrupts a hostile run.
                    marker_fill = true;
                    state->position += 1;
                    result = image_decode_add_work(state->context, 1, offset);
                }
                else
                {
                    marker_found = true;
                }
            }
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL void image_jpeg_parse_scan(ImageJpegState* state, ByteSlice payload, u64 payload_offset, u64 marker_offset)
{
    bool valid = state->context->status == IMAGE_DECODE_SUCCESS && state->frame_found;
    if (!valid && state->context->status == IMAGE_DECODE_SUCCESS)
    {
        image_jpeg_error(state, IMAGE_DECODE_MALFORMED, marker_offset);
    }
    if (valid && payload.length < 4)
    {
        image_jpeg_error(state, IMAGE_DECODE_MALFORMED, payload_offset);
        valid = false;
    }
    u8 scan_component_count = valid ? payload.pointer[0] : 0;
    u64 expected_size = 1u + (u64)scan_component_count * 2u + 3u;
    if (valid && (scan_component_count < 1 || scan_component_count > state->component_count || payload.length != expected_size))
    {
        image_jpeg_error(state, IMAGE_DECODE_MALFORMED, payload_offset);
        valid = false;
    }
    u8 scan_components[IMAGE_JPEG_COMPONENT_CAPACITY] = {0};
    u8 seen_components = 0;
    for (u32 scan_index = 0; scan_index < scan_component_count && valid; scan_index += 1)
    {
        u64 scan_offset = 1u + (u64)scan_index * 2u;
        s32 component_index = image_jpeg_component_index(state, payload.pointer[scan_offset]);
        u8 tables = payload.pointer[scan_offset + 1u];
        if (component_index < 0 || component_index >= (s32)IMAGE_JPEG_COMPONENT_CAPACITY || tables >> 4u >= IMAGE_JPEG_TABLE_CAPACITY ||
            (tables & 15u) >= IMAGE_JPEG_TABLE_CAPACITY || (seen_components & (u8)(1u << (u32)BUSTER_MAX(component_index, 0))))
        {
            image_jpeg_error(state, IMAGE_DECODE_MALFORMED, payload_offset + scan_offset);
            valid = false;
        }
        else
        {
            ImageJpegComponent* component = &state->components[component_index];
            scan_components[scan_index] = (u8)component_index;
            seen_components |= (u8)(1u << (u32)component_index);
            component->dc_table = tables >> 4u;
            component->ac_table = tables & 15u;
            if (state->context->decode_pixels && component->decoded)
            {
                image_jpeg_error(state, IMAGE_DECODE_MALFORMED, payload_offset + scan_offset);
                valid = false;
            }
        }
    }
    u64 parameters_offset = 1u + (u64)scan_component_count * 2u;
    if (valid && (payload.pointer[parameters_offset] != 0 || payload.pointer[parameters_offset + 1u] != 63u ||
                  payload.pointer[parameters_offset + 2u] != 0))
    {
        image_jpeg_unsupported(state, IMAGE_UNSUPPORTED_FEATURE_CODING, payload_offset + parameters_offset);
        valid = false;
    }
    if (valid)
    {
        valid = image_jpeg_set_information(state, marker_offset);
    }
    if (valid && state->context->decode_pixels)
    {
        valid = image_jpeg_prepare_planes(state, marker_offset);
        for (u32 scan_index = 0; scan_index < scan_component_count && valid; scan_index += 1)
        {
            ImageJpegComponent const* component = &state->components[scan_components[scan_index]];
            valid = state->quantization_tables[component->quantization_table].defined &&
                    state->huffman_tables[0][component->dc_table].defined && state->huffman_tables[1][component->ac_table].defined;
            if (!valid)
            {
                image_jpeg_error(state, IMAGE_DECODE_MALFORMED, payload_offset + 1u + (u64)scan_index * 2u);
            }
        }
        if (valid)
        {
            valid = image_jpeg_decode_scan(state, scan_components, scan_component_count);
        }
        if (valid)
        {
            for (u32 scan_index = 0; scan_index < scan_component_count; scan_index += 1)
            {
                state->components[scan_components[scan_index]].decoded = true;
            }
        }
    }
    else if (valid)
    {
        valid = image_jpeg_probe_skip_scan(state);
    }
    if (valid)
    {
        state->scan_found = true;
    }
}

BUSTER_GLOBAL_LOCAL bool image_jpeg_component_indices(ImageJpegState* state, ImageJpegColorTransform* transform, u8 indices[3])
{
    bool result = state && transform && indices && state->context->status == IMAGE_DECODE_SUCCESS;
    if (result && state->component_count == 1)
    {
        if (state->adobe_conflict || (state->adobe_found && state->adobe_transform != 0))
        {
            image_jpeg_unsupported(state, IMAGE_UNSUPPORTED_FEATURE_COLOR_TRANSFORM,
                                   state->adobe_found ? state->adobe_offset : state->frame_offset);
            result = false;
        }
        else
        {
            *transform = IMAGE_JPEG_COLOR_GRAYSCALE;
            indices[0] = 0;
        }
    }
    else if (result && state->component_count == 3)
    {
        s32 red = image_jpeg_component_index(state, 'R');
        s32 green = image_jpeg_component_index(state, 'G');
        s32 blue = image_jpeg_component_index(state, 'B');
        s32 luminance = image_jpeg_component_index(state, 1);
        s32 chroma_blue = image_jpeg_component_index(state, 2);
        s32 chroma_red = image_jpeg_component_index(state, 3);
        bool explicit_rgb = red >= 0 && green >= 0 && blue >= 0;
        bool explicit_ycbcr = luminance >= 0 && chroma_blue >= 0 && chroma_red >= 0;
        if (state->adobe_conflict || (state->adobe_found && state->adobe_transform != 0 && state->adobe_transform != 1))
        {
            image_jpeg_unsupported(state, IMAGE_UNSUPPORTED_FEATURE_COLOR_TRANSFORM,
                                   state->adobe_found ? state->adobe_offset : state->frame_offset);
            result = false;
        }
        else if ((state->adobe_found && state->adobe_transform == 0) || (!state->jfif_found && explicit_rgb))
        {
            *transform = IMAGE_JPEG_COLOR_RGB;
            indices[0] = explicit_rgb ? (u8)red : 0;
            indices[1] = explicit_rgb ? (u8)green : 1;
            indices[2] = explicit_rgb ? (u8)blue : 2;
        }
        else
        {
            *transform = IMAGE_JPEG_COLOR_YCBCR;
            indices[0] = explicit_ycbcr ? (u8)luminance : 0;
            indices[1] = explicit_ycbcr ? (u8)chroma_blue : 1;
            indices[2] = explicit_ycbcr ? (u8)chroma_red : 2;
        }
    }
    else if (result)
    {
        image_jpeg_unsupported(state, IMAGE_UNSUPPORTED_FEATURE_COMPONENT_MODEL, state->frame_offset);
        result = false;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL u8 image_jpeg_sample(ImageJpegState const* state, ImageJpegComponent const* component, u32 x, u32 y)
{
    u32 horizontal_scale = state->maximum_horizontal_sampling / component->horizontal_sampling;
    u32 vertical_scale = state->maximum_vertical_sampling / component->vertical_sampling;
    bool fancy = horizontal_scale == 2u && (vertical_scale == 1u || vertical_scale == 2u);
    u8 result = 0;
    if (fancy && vertical_scale == 1u)
    {
        u32 sample_x = BUSTER_MIN(x / 2u, component->sample_width - 1u);
        u32 sample_y = BUSTER_MIN(y, component->sample_height - 1u);
        u8 const* row = component->samples + (u64)sample_y * component->stride;
        if (!(x & 1u))
        {
            u32 previous_x = sample_x ? sample_x - 1u : sample_x;
            result = sample_x ? (u8)((3u * row[sample_x] + row[previous_x] + 1u) / 4u) : row[sample_x];
        }
        else
        {
            u32 following_x = BUSTER_MIN(sample_x + 1u, component->sample_width - 1u);
            result = following_x != sample_x ? (u8)((3u * row[sample_x] + row[following_x] + 2u) / 4u) : row[sample_x];
        }
    }
    else if (fancy)
    {
        u32 sample_x = BUSTER_MIN(x / 2u, component->sample_width - 1u);
        u32 sample_y = BUSTER_MIN(y / 2u, component->sample_height - 1u);
        u32 adjacent_y = sample_y;
        if (!(y & 1u) && sample_y)
        {
            adjacent_y -= 1u;
        }
        else if ((y & 1u) && sample_y + 1u < component->sample_height)
        {
            adjacent_y += 1u;
        }
        u8 const* current = component->samples + (u64)sample_y * component->stride;
        u8 const* adjacent = component->samples + (u64)adjacent_y * component->stride;
        u32 current_sum = 3u * current[sample_x] + adjacent[sample_x];
        u32 numerator = 0;
        u32 bias = 0;
        if (!(x & 1u))
        {
            u32 previous_x = sample_x ? sample_x - 1u : sample_x;
            u32 previous_sum = 3u * current[previous_x] + adjacent[previous_x];
            numerator = sample_x ? 3u * current_sum + previous_sum : 4u * current_sum;
            bias = 8u;
        }
        else
        {
            u32 following_x = BUSTER_MIN(sample_x + 1u, component->sample_width - 1u);
            u32 following_sum = 3u * current[following_x] + adjacent[following_x];
            numerator = following_x != sample_x ? 3u * current_sum + following_sum : 4u * current_sum;
            bias = 7u;
        }
        result = (u8)((numerator + bias) / 16u);
    }
    else
    {
        u32 sample_x = BUSTER_MIN(x / horizontal_scale, component->sample_width - 1u);
        u32 sample_y = BUSTER_MIN(y / vertical_scale, component->sample_height - 1u);
        result = component->samples[(u64)sample_y * component->stride + sample_x];
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool image_jpeg_compose(ImageJpegState* state)
{
    bool result = state->context->status == IMAGE_DECODE_SUCCESS;
    for (u32 index = 0; index < state->component_count && result; index += 1)
    {
        if (!state->components[index].decoded)
        {
            image_jpeg_error(state, IMAGE_DECODE_MALFORMED, state->position);
            result = false;
        }
    }
    ImageJpegColorTransform transform = IMAGE_JPEG_COLOR_GRAYSCALE;
    u8 indices[3] = {0};
    if (result)
    {
        result = image_jpeg_component_indices(state, &transform, indices);
    }
    if (result)
    {
        result = image_jpeg_set_information(state, state->frame_offset);
    }
    if (result)
    {
        result = image_decode_add_work(state->context, (u64)state->width * state->height * 4u, state->frame_offset);
    }
    if (result && !state->context->image.pixels.pointer)
    {
        result = image_decode_allocate_pixels(state->context, state->context->information.width,
                                              state->context->information.height, state->frame_offset);
    }
    for (u32 y = 0; y < state->height && result; y += 1)
    {
        for (u32 x = 0; x < state->width; x += 1)
        {
            s32 first = image_jpeg_sample(state, &state->components[indices[0]], x, y);
            s32 red = first;
            s32 green = first;
            s32 blue = first;
            if (transform == IMAGE_JPEG_COLOR_RGB)
            {
                green = image_jpeg_sample(state, &state->components[indices[1]], x, y);
                blue = image_jpeg_sample(state, &state->components[indices[2]], x, y);
            }
            else if (transform == IMAGE_JPEG_COLOR_YCBCR)
            {
                s32 chroma_blue = (s32)image_jpeg_sample(state, &state->components[indices[1]], x, y) - 128;
                s32 chroma_red = (s32)image_jpeg_sample(state, &state->components[indices[2]], x, y) - 128;
                red = first + (s32)image_jpeg_round_scale((s64)chroma_red * 91881, 65536);
                green = first - (s32)image_jpeg_round_scale((s64)chroma_blue * 22554 + (s64)chroma_red * 46802, 65536);
                blue = first + (s32)image_jpeg_round_scale((s64)chroma_blue * 116130, 65536);
            }
            u8* pixel = state->context->image.pixels.pointer + (u64)y * state->context->image.stride + (u64)x * 4u;
            pixel[0] = image_decode_clamp_u8(red);
            pixel[1] = image_decode_clamp_u8(green);
            pixel[2] = image_decode_clamp_u8(blue);
            pixel[3] = 255;
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool image_jpeg_marker_has_segment(u8 marker)
{
    bool result = image_jpeg_is_frame_marker(marker) || marker == 0xc4u || marker == 0xc8u || marker == 0xccu ||
                  (marker >= 0xdau && marker <= 0xfeu && marker != 0xd8u && marker != 0xd9u);
    return result;
}

void image_jpeg_process(ImageDecodeContext* context)
{
    ImageJpegState state = {
        .context = context,
        .adobe_transform = -1,
        .orientation = IMAGE_ORIENTATION_TOP_LEFT,
    };
    bool valid = context && context->status == IMAGE_DECODE_SUCCESS;
    if (valid && (!context->encoded.pointer || context->encoded.length < 2))
    {
        image_decode_error(context, IMAGE_DECODE_TRUNCATED, context->encoded.length);
        valid = false;
    }
    if (valid && (context->encoded.pointer[0] != 0xffu || context->encoded.pointer[1] != 0xd8u))
    {
        image_decode_error(context, IMAGE_DECODE_MALFORMED, 0);
        valid = false;
    }
    if (valid)
    {
        state.position = 2;
        valid = image_decode_add_work(context, 2, 0);
    }
    while (valid && !state.end_found && (!state.scan_found || context->decode_pixels || state.position < context->encoded.length))
    {
        u8 marker = 0;
        u64 marker_offset = 0;
        valid = image_jpeg_next_marker(&state, &marker, &marker_offset);
        if (valid && marker == 0xd9u)
        {
            state.end_found = true;
        }
        else if (valid && (marker == 0xd8u || (marker >= 0xd0u && marker <= 0xd7u)))
        {
            image_jpeg_error(&state, IMAGE_DECODE_MALFORMED, marker_offset);
            valid = false;
        }
        else if (valid && marker == 0x01u)
        {
            // TEM carries no parameters and has no image-decoding effect.
        }
        else if (valid && !image_jpeg_marker_has_segment(marker))
        {
            image_jpeg_unsupported(&state, IMAGE_UNSUPPORTED_FEATURE_PROFILE, marker_offset);
            valid = false;
        }
        else if (valid)
        {
            ByteSlice payload = {0};
            u64 payload_offset = 0;
            valid = image_decode_count_limit(context, IMAGE_EXCEEDED_LIMIT_SEGMENTS, 1, marker_offset);
            if (valid && marker == 0xdau)
            {
                valid = image_decode_count_limit(context, IMAGE_EXCEEDED_LIMIT_SCANS, 1, marker_offset);
            }
            if (valid)
            {
                valid = image_jpeg_segment(&state, &payload, &payload_offset);
            }
            if (valid && image_jpeg_is_frame_marker(marker))
            {
                image_jpeg_parse_frame(&state, marker, payload, payload_offset, marker_offset);
            }
            else if (valid && marker == 0xc4u)
            {
                image_jpeg_parse_huffman(&state, payload, payload_offset);
            }
            else if (valid && marker == 0xdbu)
            {
                image_jpeg_parse_quantization(&state, payload, payload_offset);
            }
            else if (valid && marker == 0xddu)
            {
                image_jpeg_parse_restart_interval(&state, payload, payload_offset);
            }
            else if (valid && marker == 0xdau)
            {
                image_jpeg_parse_scan(&state, payload, payload_offset, marker_offset);
            }
            else if (valid && marker >= 0xe0u && marker <= 0xefu)
            {
                image_jpeg_parse_application(&state, marker, payload, marker_offset);
            }
            else if (valid && (marker == 0xc8u || marker == 0xccu || marker == 0xdcu || marker == 0xdeu || marker == 0xdfu ||
                               (marker >= 0xf0u && marker <= 0xfdu)))
            {
                ImageUnsupportedFeature feature = marker == 0xccu || marker == 0xdeu || marker == 0xdfu ?
                                                      IMAGE_UNSUPPORTED_FEATURE_CODING : IMAGE_UNSUPPORTED_FEATURE_PROFILE;
                image_jpeg_unsupported(&state, feature, marker_offset);
            }
            valid = context->status == IMAGE_DECODE_SUCCESS;
        }
    }
    if (valid && !state.frame_found)
    {
        image_jpeg_error(&state, IMAGE_DECODE_MALFORMED, state.position);
        valid = false;
    }
    if (valid && !state.scan_found)
    {
        image_jpeg_error(&state, IMAGE_DECODE_MALFORMED, state.position);
        valid = false;
    }
    if (valid && !state.end_found)
    {
        image_jpeg_error(&state, IMAGE_DECODE_TRUNCATED, state.position);
        valid = false;
    }
    if (valid && !context->decode_pixels)
    {
        ImageJpegColorTransform transform = IMAGE_JPEG_COLOR_GRAYSCALE;
        u8 indices[3] = {0};
        valid = image_jpeg_component_indices(&state, &transform, indices);
        if (valid)
        {
            valid = image_jpeg_set_information(&state, state.frame_offset);
        }
    }
    if (valid && context->decode_pixels)
    {
        valid = image_jpeg_compose(&state);
    }
    if (state.scratch.arena)
    {
        scratch_end(state.scratch);
    }
}
