// Image reader front door and shared bounded-parser policy.
//
// Public entry points:
//   image_detect_format   signature-first encoded-format recognition
//   image_probe           allocation-free dimensions/source metadata
//   image_decode          transactional canonical RGBA8 decoding
//   image_apply_orientation       copy a decoded image into display order
//
// Shared parser seam:
//   image_decode_*                 limits, allocation and scalar readers
//   image_decode_tiff_orientation  bounded TIFF IFD0 orientation metadata
//
// Codec map:
//   image/png.c           PNG chunks, DEFLATE, filters and Adam7
//   image/jpeg.c          JFIF/Exif Huffman DCT JPEG
//   image/gif.c           GIF87a/89a first-frame compositing
//   image/bmp.c           BMP/DIB indexed, direct and RLE pixels
//   image/tga.c           TGA raw/RLE pixels and origin normalization
//   image/qoi.c           Quite OK Image
//   image/pnm.c           portable anymap family

#include <buster/lib/image/internal.h>

#define IMAGE_ISO_MAX_COMPATIBLE_BRANDS 256u
#define IMAGE_ICO_DIRECTORY_HEADER_SIZE 6u
#define IMAGE_ICO_DIRECTORY_ENTRY_SIZE 16u

BUSTER_GLOBAL_LOCAL bool image_bytes_equal(ByteSlice bytes, u64 offset, void const* expected, u64 expected_size)
{
    bool result = image_decode_range(bytes, offset, expected_size);
    if (result)
    {
        result = memcmp(bytes.pointer + offset, expected, expected_size) == 0;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool image_is_icon_directory(ByteSlice encoded)
{
    bool result = encoded.pointer && encoded.length >= IMAGE_ICO_DIRECTORY_HEADER_SIZE;
    if (result)
    {
        u16 reserved = image_decode_u16_le(encoded.pointer);
        u16 type = image_decode_u16_le(encoded.pointer + 2);
        u16 entry_count = image_decode_u16_le(encoded.pointer + 4);
        result = reserved == 0 && (type == 1 || type == 2) && entry_count &&
                 entry_count <= (encoded.length - IMAGE_ICO_DIRECTORY_HEADER_SIZE) / IMAGE_ICO_DIRECTORY_ENTRY_SIZE;
        if (result)
        {
            u64 directory_size = IMAGE_ICO_DIRECTORY_HEADER_SIZE + (u64)entry_count * IMAGE_ICO_DIRECTORY_ENTRY_SIZE;
            for (u32 entry_index = 0; entry_index < entry_count && result; entry_index += 1)
            {
                u64 entry_offset = IMAGE_ICO_DIRECTORY_HEADER_SIZE + (u64)entry_index * IMAGE_ICO_DIRECTORY_ENTRY_SIZE;
                u64 image_size = image_decode_u32_le(encoded.pointer + entry_offset + 8);
                u64 image_offset = image_decode_u32_le(encoded.pointer + entry_offset + 12);
                result = image_size && image_offset >= directory_size && image_decode_range(encoded, image_offset, image_size);
            }
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool image_is_pfm_header(ByteSlice encoded)
{
    bool result = encoded.pointer && encoded.length >= 3 && encoded.pointer[0] == 'P' &&
                  (encoded.pointer[1] == 'F' || encoded.pointer[1] == 'f');
    if (result)
    {
        result = encoded.pointer[2] == '\n' ||
                 (encoded.pointer[2] == '\r' && encoded.length >= 4 && encoded.pointer[3] == '\n');
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool image_is_iso_brand(u8 const* brand, ImageFormat* format)
{
    bool result = true;
    if (memcmp(brand, "avif", 4) == 0 || memcmp(brand, "avis", 4) == 0)
    {
        *format = IMAGE_FORMAT_AVIF;
    }
    else if (memcmp(brand, "heic", 4) == 0 || memcmp(brand, "heix", 4) == 0 || memcmp(brand, "hevc", 4) == 0 ||
             memcmp(brand, "hevx", 4) == 0 || memcmp(brand, "heim", 4) == 0 || memcmp(brand, "heis", 4) == 0 ||
             memcmp(brand, "mif1", 4) == 0 || memcmp(brand, "msf1", 4) == 0)
    {
        *format = IMAGE_FORMAT_HEIF;
    }
    else
    {
        result = false;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL ImageFormat image_detect_iso_base_media(ByteSlice encoded)
{
    ImageFormat result = IMAGE_FORMAT_UNKNOWN;
    if (encoded.pointer && encoded.length >= 16 && image_bytes_equal(encoded, 4, "ftyp", 4))
    {
        u32 box_size = image_decode_u32_be(encoded.pointer);
        u64 end = box_size;
        if (box_size >= 16 && end <= encoded.length && (end - 16) % 4 == 0)
        {
            // Format detection has no caller-supplied work budget. Bound a
            // hostile ftyp compatibility list while still covering far more
            // brands than ordinary AVIF/HEIF files carry.
            image_is_iso_brand(encoded.pointer + 8, &result);
            u64 brand_count = BUSTER_MIN((end - 16) / 4, IMAGE_ISO_MAX_COMPATIBLE_BRANDS);
            for (u64 brand_index = 0; brand_index < brand_count && result != IMAGE_FORMAT_AVIF; brand_index += 1)
            {
                ImageFormat compatible_format = IMAGE_FORMAT_UNKNOWN;
                u64 offset = 16 + brand_index * 4;
                if (image_is_iso_brand(encoded.pointer + offset, &compatible_format) &&
                    (result == IMAGE_FORMAT_UNKNOWN || compatible_format == IMAGE_FORMAT_AVIF))
                {
                    result = compatible_format;
                }
            }
        }
    }
    return result;
}

ImageFormat image_detect_format(ByteSlice encoded)
{
    ImageFormat result = IMAGE_FORMAT_UNKNOWN;
    if ((!encoded.length || encoded.pointer) && encoded.length >= 2)
    {
        static u8 const png_signature[8] = {137, 80, 78, 71, 13, 10, 26, 10};
        static u8 const big_tiff_le_signature[4] = {'I', 'I', 43, 0};
        static u8 const big_tiff_be_signature[4] = {'M', 'M', 0, 43};
        static u8 const jpeg_xl_codestream_signature[2] = {0xff, 0x0a};
        static u8 const jpeg_xl_container_signature[12] = {0, 0, 0, 12, 'J', 'X', 'L', ' ', 13, 10, 0x87, 10};
        static u8 const jpeg_2000_codestream_signature[4] = {0xff, 0x4f, 0xff, 0x51};
        static u8 const jp2_signature[12] = {0, 0, 0, 12, 'j', 'P', ' ', ' ', 13, 10, 0x87, 10};
        static u8 const openexr_signature[4] = {0x76, 0x2f, 0x31, 0x01};
        static u8 const ktx1_signature[12] = {0xab, 'K', 'T', 'X', ' ', '1', '1', 0xbb, 13, 10, 26, 10};
        static u8 const ktx2_signature[12] = {0xab, 'K', 'T', 'X', ' ', '2', '0', 0xbb, 13, 10, 26, 10};
        if (encoded.length >= sizeof(png_signature) && memcmp(encoded.pointer, png_signature, sizeof(png_signature)) == 0)
        {
            result = IMAGE_FORMAT_PNG;
        }
        else if (encoded.pointer[0] == 0xff && encoded.pointer[1] == 0xd8)
        {
            result = IMAGE_FORMAT_JPEG;
        }
        else if (image_bytes_equal(encoded, 0, "GIF87a", 6) || image_bytes_equal(encoded, 0, "GIF89a", 6))
        {
            result = IMAGE_FORMAT_GIF;
        }
        else if (image_bytes_equal(encoded, 0, "BM", 2))
        {
            result = IMAGE_FORMAT_BMP;
        }
        else if (image_bytes_equal(encoded, 0, "qoif", 4))
        {
            result = IMAGE_FORMAT_QOI;
        }
        else if (encoded.pointer[0] == 'P' && encoded.pointer[1] >= '1' && encoded.pointer[1] <= '7')
        {
            result = IMAGE_FORMAT_PNM;
        }
        else if (image_bytes_equal(encoded, 0, "RIFF", 4) && image_bytes_equal(encoded, 8, "WEBP", 4))
        {
            result = IMAGE_FORMAT_WEBP;
        }
        else if (image_bytes_equal(encoded, 0, "II\x2a\0", 4) || image_bytes_equal(encoded, 0, "MM\0\x2a", 4))
        {
            result = IMAGE_FORMAT_TIFF;
        }
        else if (image_bytes_equal(encoded, 0, big_tiff_le_signature, sizeof(big_tiff_le_signature)) ||
                 image_bytes_equal(encoded, 0, big_tiff_be_signature, sizeof(big_tiff_be_signature)))
        {
            result = IMAGE_FORMAT_BIG_TIFF;
        }
        else if (image_is_icon_directory(encoded))
        {
            result = IMAGE_FORMAT_ICO;
        }
        else if (image_bytes_equal(encoded, 0, jpeg_xl_codestream_signature, sizeof(jpeg_xl_codestream_signature)) ||
                 image_bytes_equal(encoded, 0, jpeg_xl_container_signature, sizeof(jpeg_xl_container_signature)))
        {
            result = IMAGE_FORMAT_JPEG_XL;
        }
        else if (image_bytes_equal(encoded, 0, jpeg_2000_codestream_signature, sizeof(jpeg_2000_codestream_signature)) ||
                 image_bytes_equal(encoded, 0, jp2_signature, sizeof(jp2_signature)))
        {
            result = IMAGE_FORMAT_JPEG_2000;
        }
        else if (image_bytes_equal(encoded, 0, "8BPS", 4))
        {
            result = IMAGE_FORMAT_PSD;
        }
        else if (image_bytes_equal(encoded, 0, openexr_signature, sizeof(openexr_signature)))
        {
            result = IMAGE_FORMAT_OPENEXR;
        }
        else if (image_bytes_equal(encoded, 0, "#?RADIANCE", 10) || image_bytes_equal(encoded, 0, "#?RGBE", 6))
        {
            result = IMAGE_FORMAT_HDR;
        }
        else if (image_bytes_equal(encoded, 0, "DDS ", 4))
        {
            result = IMAGE_FORMAT_DDS;
        }
        else if (image_bytes_equal(encoded, 0, ktx1_signature, sizeof(ktx1_signature)))
        {
            result = IMAGE_FORMAT_KTX1;
        }
        else if (image_bytes_equal(encoded, 0, ktx2_signature, sizeof(ktx2_signature)))
        {
            result = IMAGE_FORMAT_KTX2;
        }
        else if (image_is_pfm_header(encoded))
        {
            result = IMAGE_FORMAT_PFM;
        }
        else
        {
            result = image_detect_iso_base_media(encoded);
            if (result == IMAGE_FORMAT_UNKNOWN && image_tga_detect(encoded))
            {
                result = IMAGE_FORMAT_TGA;
            }
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL ImageDecodeOptions image_decode_options_normalize(ImageDecodeOptions options)
{
    if (!options.max_width)
    {
        options.max_width = BUSTER_IMAGE_MAX_WIDTH;
    }
    if (!options.max_height)
    {
        options.max_height = BUSTER_IMAGE_MAX_HEIGHT;
    }
    if (!options.max_pixels)
    {
        options.max_pixels = BUSTER_IMAGE_MAX_PIXELS;
    }
    if (!options.max_decoded_bytes)
    {
        options.max_decoded_bytes = BUSTER_IMAGE_MAX_DECODED_BYTES;
    }
    if (!options.max_work)
    {
        options.max_work = BUSTER_IMAGE_MAX_WORK;
    }
    if (!options.max_frames)
    {
        options.max_frames = BUSTER_IMAGE_MAX_FRAMES;
    }
    if (!options.max_chunks)
    {
        options.max_chunks = BUSTER_IMAGE_MAX_CHUNKS;
    }
    if (!options.max_segments)
    {
        options.max_segments = BUSTER_IMAGE_MAX_SEGMENTS;
    }
    if (!options.max_blocks)
    {
        options.max_blocks = BUSTER_IMAGE_MAX_BLOCKS;
    }
    if (!options.max_scans)
    {
        options.max_scans = BUSTER_IMAGE_MAX_SCANS;
    }
    if (!options.max_depth)
    {
        options.max_depth = BUSTER_IMAGE_MAX_DEPTH;
    }
    return options;
}

void image_decode_error(ImageDecodeContext* context, ImageDecodeStatus status, u64 offset)
{
    if (context->status == IMAGE_DECODE_SUCCESS)
    {
        context->status = status;
        context->error_offset = offset;
    }
}

void image_decode_set_unsupported_feature(ImageDecodeContext* context, ImageUnsupportedFeature feature, u64 offset)
{
    if (context->status == IMAGE_DECODE_SUCCESS)
    {
        context->unsupported_feature = feature;
        image_decode_error(context, IMAGE_DECODE_UNSUPPORTED_FEATURE, offset);
    }
}

void image_decode_set_exceeded_limit(ImageDecodeContext* context, ImageExceededLimit limit,
                                     u64 observed_value, u64 limit_value, u64 offset)
{
    if (context->status == IMAGE_DECODE_SUCCESS)
    {
        context->exceeded_limit = limit;
        context->observed_value = observed_value;
        context->limit_value = limit_value;
        image_decode_error(context, IMAGE_DECODE_LIMIT_EXCEEDED, offset);
    }
}

BUSTER_GLOBAL_LOCAL bool image_decode_structural_limit(ImageDecodeContext* context, ImageExceededLimit limit,
                                                        u64** counter, u64* limit_value)
{
    bool result = true;
    switch (limit)
    {
    case IMAGE_EXCEEDED_LIMIT_FRAMES:
        *counter = &context->limit_counters.frames;
        *limit_value = context->options.max_frames;
        break;
    case IMAGE_EXCEEDED_LIMIT_CHUNKS:
        *counter = &context->limit_counters.chunks;
        *limit_value = context->options.max_chunks;
        break;
    case IMAGE_EXCEEDED_LIMIT_SEGMENTS:
        *counter = &context->limit_counters.segments;
        *limit_value = context->options.max_segments;
        break;
    case IMAGE_EXCEEDED_LIMIT_BLOCKS:
        *counter = &context->limit_counters.blocks;
        *limit_value = context->options.max_blocks;
        break;
    case IMAGE_EXCEEDED_LIMIT_SCANS:
        *counter = &context->limit_counters.scans;
        *limit_value = context->options.max_scans;
        break;
    case IMAGE_EXCEEDED_LIMIT_DEPTH:
        *counter = &context->limit_counters.depth;
        *limit_value = context->options.max_depth;
        break;
    default:
        result = false;
        break;
    }
    return result;
}

bool image_decode_check_limit(ImageDecodeContext* context, ImageExceededLimit limit,
                              u64 observed_value, u64 offset)
{
    bool result = context->status == IMAGE_DECODE_SUCCESS;
    u64* counter = 0;
    u64 limit_value = 0;
    if (result && !image_decode_structural_limit(context, limit, &counter, &limit_value))
    {
        image_decode_error(context, IMAGE_DECODE_INVALID_ARGUMENT, offset);
        result = false;
    }
    if (result && observed_value > limit_value)
    {
        image_decode_set_exceeded_limit(context, limit, observed_value, limit_value, offset);
        result = false;
    }
    return result;
}

bool image_decode_count_limit(ImageDecodeContext* context, ImageExceededLimit limit,
                              u64 amount, u64 offset)
{
    bool result = context->status == IMAGE_DECODE_SUCCESS;
    u64* counter = 0;
    u64 limit_value = 0;
    if (result && !image_decode_structural_limit(context, limit, &counter, &limit_value))
    {
        image_decode_error(context, IMAGE_DECODE_INVALID_ARGUMENT, offset);
        result = false;
    }
    if (result)
    {
        bool overflow = amount > UINT64_MAX - *counter;
        u64 observed_value = overflow ? UINT64_MAX : *counter + amount;
        *counter = observed_value;
        if (overflow || observed_value > limit_value)
        {
            image_decode_set_exceeded_limit(context, limit, observed_value, limit_value, offset);
            result = false;
        }
    }
    return result;
}

bool image_decode_enter_depth(ImageDecodeContext* context, u64 offset)
{
    bool result = image_decode_count_limit(context, IMAGE_EXCEEDED_LIMIT_DEPTH, 1, offset);
    return result;
}

void image_decode_leave_depth(ImageDecodeContext* context)
{
    if (context->limit_counters.depth)
    {
        context->limit_counters.depth -= 1;
    }
}

bool image_decode_add_work(ImageDecodeContext* context, u64 amount, u64 offset)
{
    bool result = context->status == IMAGE_DECODE_SUCCESS;
    if (result)
    {
        result = context->work <= context->options.max_work && amount <= context->options.max_work - context->work;
        if (result)
        {
            context->work += amount;
        }
        else
        {
            u64 observed_value = amount <= UINT64_MAX - context->work ? context->work + amount : UINT64_MAX;
            image_decode_set_exceeded_limit(context, IMAGE_EXCEEDED_LIMIT_WORK, observed_value,
                                            context->options.max_work, offset);
        }
    }
    return result;
}

bool image_decode_set_information(ImageDecodeContext* context, u32 width, u32 height, u32 frame_count,
                                  u8 source_channel_count, u8 source_bits_per_channel, bool has_alpha,
                                  ImageOrientation orientation, u64 offset)
{
    bool result = context->status == IMAGE_DECODE_SUCCESS;
    u64 pixels = (u64)width * height;
    if (result && (!width || !height))
    {
        image_decode_error(context, IMAGE_DECODE_MALFORMED, offset);
        result = false;
    }
    if (result && width > UINT32_MAX / 4)
    {
        image_decode_set_exceeded_limit(context, IMAGE_EXCEEDED_LIMIT_WIDTH, width, UINT32_MAX / 4, offset);
        result = false;
    }
    if (result && width > context->options.max_width)
    {
        image_decode_set_exceeded_limit(context, IMAGE_EXCEEDED_LIMIT_WIDTH, width, context->options.max_width, offset);
        result = false;
    }
    if (result && height > context->options.max_height)
    {
        image_decode_set_exceeded_limit(context, IMAGE_EXCEEDED_LIMIT_HEIGHT, height, context->options.max_height, offset);
        result = false;
    }
    if (result && pixels > context->options.max_pixels)
    {
        image_decode_set_exceeded_limit(context, IMAGE_EXCEEDED_LIMIT_PIXELS, pixels, context->options.max_pixels, offset);
        result = false;
    }
    if (result && pixels > context->options.max_decoded_bytes / 4)
    {
        u64 decoded_bytes = pixels <= UINT64_MAX / 4 ? pixels * 4 : UINT64_MAX;
        image_decode_set_exceeded_limit(context, IMAGE_EXCEEDED_LIMIT_DECODED_BYTES, decoded_bytes,
                                        context->options.max_decoded_bytes, offset);
        result = false;
    }
    if (result)
    {
        result = image_decode_check_limit(context, IMAGE_EXCEEDED_LIMIT_FRAMES, frame_count, offset);
    }
    if (result && (orientation < IMAGE_ORIENTATION_TOP_LEFT || orientation > IMAGE_ORIENTATION_LEFT_BOTTOM))
    {
        image_decode_error(context, IMAGE_DECODE_MALFORMED, offset);
        result = false;
    }
    if (result)
    {
        context->information.width = width;
        context->information.height = height;
        context->information.frame_count = frame_count;
        context->information.source_channel_count = source_channel_count;
        context->information.source_bits_per_channel = source_bits_per_channel;
        context->information.has_alpha = has_alpha;
        context->information.orientation = orientation;
    }
    return result;
}

bool image_decode_arena_can_allocate(Arena* arena, u64 size, u64 alignment)
{
    u64 aligned_position = 0;
    bool result = arena && size <= ARENA_MAX_RESERVATION && align_forward_checked(arena->position, alignment, &aligned_position) &&
                  aligned_position <= arena->reserved_size && size <= arena->reserved_size - aligned_position;
    return result;
}

u8* image_decode_allocate(ImageDecodeContext* context, Arena* arena, u64 size, u64 alignment, u64 offset)
{
    u8* result = 0;
    if (context->status == IMAGE_DECODE_SUCCESS)
    {
        bool valid = image_decode_arena_can_allocate(arena, size, alignment);
        if (valid)
        {
            result = arena_allocate_bytes(arena, size, alignment);
        }
        else
        {
            image_decode_error(context, IMAGE_DECODE_CAPACITY_EXCEEDED, offset);
        }
    }
    return result;
}

bool image_decode_allocate_pixels(ImageDecodeContext* context, u32 width, u32 height, u64 offset)
{
    bool result = context->status == IMAGE_DECODE_SUCCESS && context->decode_pixels;
    if (result)
    {
        u64 size = (u64)width * height * 4;
        u8* pixels = image_decode_allocate(context, context->arena, size, BUSTER_ALIGN_OF(u32), offset);
        result = pixels != 0;
        if (result)
        {
            context->image.pixels = (ByteSlice){.pointer = pixels, .length = size};
            context->image.width = width;
            context->image.height = height;
            context->image.stride = width * 4;
        }
    }
    return result;
}

bool image_decode_range(ByteSlice bytes, u64 offset, u64 size)
{
    bool result = (!bytes.length || bytes.pointer) && offset <= bytes.length && size <= bytes.length - offset;
    return result;
}

u16 image_decode_u16_be(u8 const* bytes)
{
    u16 result = (u16)((u16)bytes[0] << 8u) | bytes[1];
    return result;
}

u16 image_decode_u16_le(u8 const* bytes)
{
    u16 result = (u16)((u16)bytes[1] << 8u) | bytes[0];
    return result;
}

u32 image_decode_u32_be(u8 const* bytes)
{
    u32 result = (u32)bytes[0] << 24u | (u32)bytes[1] << 16u | (u32)bytes[2] << 8u | bytes[3];
    return result;
}

u32 image_decode_u32_le(u8 const* bytes)
{
    u32 result = (u32)bytes[3] << 24u | (u32)bytes[2] << 16u | (u32)bytes[1] << 8u | bytes[0];
    return result;
}

u64 image_decode_u64_le(u8 const* bytes)
{
    u64 result = (u64)image_decode_u32_le(bytes + 4) << 32u | image_decode_u32_le(bytes);
    return result;
}

bool image_decode_tiff_orientation(ByteSlice tiff, ImageOrientation* orientation,
                                   bool* orientation_found, u64* error_offset)
{
    bool result = orientation && orientation_found && error_offset && (!tiff.length || tiff.pointer);
    bool found = false;
    bool little_endian = false;
    ImageOrientation value = IMAGE_ORIENTATION_TOP_LEFT;
    u64 failure_offset = 0;
    if (result)
    {
        bool little = tiff.length >= 8 && !memcmp(tiff.pointer, "II\x2a\0", 4);
        bool big = tiff.length >= 8 && !memcmp(tiff.pointer, "MM\0\x2a", 4);
        result = little || big;
        little_endian = little;
    }
    u32 ifd_offset = 0;
    if (result)
    {
        ifd_offset = little_endian ? image_decode_u32_le(tiff.pointer + 4) : image_decode_u32_be(tiff.pointer + 4);
        result = image_decode_range(tiff, ifd_offset, 2);
        if (!result)
        {
            failure_offset = 4;
        }
    }
    u16 entry_count = 0;
    if (result)
    {
        entry_count = little_endian ? image_decode_u16_le(tiff.pointer + ifd_offset) : image_decode_u16_be(tiff.pointer + ifd_offset);
        u64 entries_offset = (u64)ifd_offset + 2u;
        u64 entries_size = (u64)entry_count * 12u;
        result = image_decode_range(tiff, entries_offset, entries_size);
        if (!result)
        {
            failure_offset = ifd_offset;
        }
    }
    for (u32 entry_index = 0; result && entry_index < entry_count && !found; entry_index += 1)
    {
        u64 entry_offset = (u64)ifd_offset + 2u + (u64)entry_index * 12u;
        u8 const* entry = tiff.pointer + entry_offset;
        u16 tag = little_endian ? image_decode_u16_le(entry) : image_decode_u16_be(entry);
        u16 type = little_endian ? image_decode_u16_le(entry + 2) : image_decode_u16_be(entry + 2);
        u32 count = little_endian ? image_decode_u32_le(entry + 4) : image_decode_u32_be(entry + 4);
        if (tag == 0x0112u && type == 3u && count == 1u)
        {
            u16 encoded_orientation = little_endian ? image_decode_u16_le(entry + 8) : image_decode_u16_be(entry + 8);
            if (encoded_orientation >= IMAGE_ORIENTATION_TOP_LEFT && encoded_orientation <= IMAGE_ORIENTATION_LEFT_BOTTOM)
            {
                value = (ImageOrientation)encoded_orientation;
                found = true;
            }
        }
    }
    if (result)
    {
        *orientation = value;
        *orientation_found = found;
        *error_offset = 0;
    }
    else if (error_offset)
    {
        *error_offset = failure_offset;
    }
    return result;
}

u8 image_decode_clamp_u8(s32 value)
{
    u8 result = (u8)BUSTER_CLAMP(0, value, 255);
    return result;
}

BUSTER_GLOBAL_LOCAL bool image_reader_bytes(ImageByteReader* reader, u64 size, u8 const** bytes)
{
    bool result = reader && reader->context && bytes && image_decode_range(reader->context->encoded, reader->position, size);
    if (result)
    {
        *bytes = reader->context->encoded.pointer + reader->position;
        reader->position += size;
    }
    else if (reader && reader->context)
    {
        image_decode_error(reader->context, IMAGE_DECODE_TRUNCATED, reader->position);
    }
    return result;
}

bool image_reader_u8(ImageByteReader* reader, u8* value)
{
    u8 const* bytes = 0;
    bool result = value && image_reader_bytes(reader, 1, &bytes);
    if (result)
    {
        *value = bytes[0];
    }
    return result;
}

bool image_reader_u16_be(ImageByteReader* reader, u16* value)
{
    u8 const* bytes = 0;
    bool result = value && image_reader_bytes(reader, 2, &bytes);
    if (result)
    {
        *value = image_decode_u16_be(bytes);
    }
    return result;
}

bool image_reader_u16_le(ImageByteReader* reader, u16* value)
{
    u8 const* bytes = 0;
    bool result = value && image_reader_bytes(reader, 2, &bytes);
    if (result)
    {
        *value = image_decode_u16_le(bytes);
    }
    return result;
}

bool image_reader_u32_be(ImageByteReader* reader, u32* value)
{
    u8 const* bytes = 0;
    bool result = value && image_reader_bytes(reader, 4, &bytes);
    if (result)
    {
        *value = image_decode_u32_be(bytes);
    }
    return result;
}

bool image_reader_u32_le(ImageByteReader* reader, u32* value)
{
    u8 const* bytes = 0;
    bool result = value && image_reader_bytes(reader, 4, &bytes);
    if (result)
    {
        *value = image_decode_u32_le(bytes);
    }
    return result;
}

bool image_reader_skip(ImageByteReader* reader, u64 size)
{
    u8 const* bytes = 0;
    bool result = image_reader_bytes(reader, size, &bytes);
    return result;
}

ByteSlice image_reader_slice(ImageByteReader* reader, u64 size)
{
    ByteSlice result = {0};
    u8 const* bytes = 0;
    if (image_reader_bytes(reader, size, &bytes))
    {
        result.pointer = (u8*)bytes;
        result.length = size;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL void image_process(ImageDecodeContext* context)
{
    switch (context->information.format)
    {
    case IMAGE_FORMAT_PNG:
        image_png_process(context);
        break;
    case IMAGE_FORMAT_JPEG:
        image_jpeg_process(context);
        break;
    case IMAGE_FORMAT_GIF:
        image_gif_process(context);
        break;
    case IMAGE_FORMAT_BMP:
        image_bmp_process(context);
        break;
    case IMAGE_FORMAT_TGA:
        image_tga_process(context);
        break;
    case IMAGE_FORMAT_QOI:
        image_qoi_process(context);
        break;
    case IMAGE_FORMAT_PNM:
        image_pnm_process(context);
        break;
    case IMAGE_FORMAT_WEBP:
    case IMAGE_FORMAT_TIFF:
    case IMAGE_FORMAT_ICO:
    case IMAGE_FORMAT_AVIF:
    case IMAGE_FORMAT_HEIF:
    case IMAGE_FORMAT_BIG_TIFF:
    case IMAGE_FORMAT_JPEG_XL:
    case IMAGE_FORMAT_JPEG_2000:
    case IMAGE_FORMAT_PSD:
    case IMAGE_FORMAT_OPENEXR:
    case IMAGE_FORMAT_HDR:
    case IMAGE_FORMAT_DDS:
    case IMAGE_FORMAT_KTX1:
    case IMAGE_FORMAT_KTX2:
    case IMAGE_FORMAT_PFM:
        image_decode_error(context, IMAGE_DECODE_UNSUPPORTED_FORMAT, 0);
        break;
    case IMAGE_FORMAT_UNKNOWN:
        image_decode_error(context, IMAGE_DECODE_UNRECOGNIZED_FORMAT, 0);
        break;
    case IMAGE_FORMAT_COUNT:
    default:
        image_decode_error(context, IMAGE_DECODE_INVALID_ARGUMENT, 0);
        break;
    }
}

BUSTER_GLOBAL_LOCAL ImageDecodeContext image_context_make(Arena* arena, ByteSlice encoded, ImageDecodeOptions options, bool decode_pixels)
{
    ImageDecodeContext result = {0};
    result.arena = arena;
    result.encoded = encoded;
    result.options = image_decode_options_normalize(options);
    result.decode_pixels = decode_pixels;
    result.information.orientation = IMAGE_ORIENTATION_TOP_LEFT;
    bool valid_hint = options.format_hint < IMAGE_FORMAT_COUNT;
    if (!valid_hint || (encoded.length && !encoded.pointer) || (decode_pixels && !arena))
    {
        result.status = IMAGE_DECODE_INVALID_ARGUMENT;
    }
    else
    {
        result.information.format = options.format_hint == IMAGE_FORMAT_UNKNOWN ? image_detect_format(encoded) : options.format_hint;
        if (result.information.format == IMAGE_FORMAT_UNKNOWN)
        {
            result.status = IMAGE_DECODE_UNRECOGNIZED_FORMAT;
        }
    }
    return result;
}

ImageProbeResult image_probe(ByteSlice encoded, ImageDecodeOptions options)
{
    ImageDecodeContext context = image_context_make(0, encoded, options, false);
    if (context.status == IMAGE_DECODE_SUCCESS)
    {
        image_process(&context);
    }
    ImageProbeResult result = {
        .information = context.information,
        .status = context.status,
        .error_offset = context.error_offset,
        .unsupported_feature = context.unsupported_feature,
        .exceeded_limit = context.exceeded_limit,
        .observed_value = context.observed_value,
        .limit_value = context.limit_value,
    };
    return result;
}

ImageDecodeResult image_decode(Arena* arena, ByteSlice encoded, ImageDecodeOptions options)
{
    u64 arena_position = arena ? arena->position : 0;
    ImageDecodeContext context = image_context_make(arena, encoded, options, true);
    if (context.status == IMAGE_DECODE_SUCCESS)
    {
        image_process(&context);
    }
    if (context.status != IMAGE_DECODE_SUCCESS && arena)
    {
        arena_set_position(arena, arena_position);
        context.image = (Image){0};
    }
    ImageDecodeResult result = {
        .image = context.image,
        .information = context.information,
        .status = context.status,
        .error_offset = context.error_offset,
        .unsupported_feature = context.unsupported_feature,
        .exceeded_limit = context.exceeded_limit,
        .observed_value = context.observed_value,
        .limit_value = context.limit_value,
    };
    return result;
}

BUSTER_GLOBAL_LOCAL void image_orientation_destination(ImageOrientation orientation, u32 width, u32 height,
                                                        u32 x, u32 y, u32* destination_x, u32* destination_y)
{
    u32 right = width - 1u - x;
    u32 bottom = height - 1u - y;
    u32 output_x = x;
    u32 output_y = y;
    switch (orientation)
    {
    case IMAGE_ORIENTATION_TOP_RIGHT:
        output_x = right;
        break;
    case IMAGE_ORIENTATION_BOTTOM_RIGHT:
        output_x = right;
        output_y = bottom;
        break;
    case IMAGE_ORIENTATION_BOTTOM_LEFT:
        output_y = bottom;
        break;
    case IMAGE_ORIENTATION_LEFT_TOP:
        output_x = y;
        output_y = x;
        break;
    case IMAGE_ORIENTATION_RIGHT_TOP:
        output_x = height - 1u - y;
        output_y = x;
        break;
    case IMAGE_ORIENTATION_RIGHT_BOTTOM:
        output_x = height - 1u - y;
        output_y = right;
        break;
    case IMAGE_ORIENTATION_LEFT_BOTTOM:
        output_x = y;
        output_y = right;
        break;
    case IMAGE_ORIENTATION_TOP_LEFT:
    default:
        break;
    }
    *destination_x = output_x;
    *destination_y = output_y;
}

ImageTransformResult image_apply_orientation(Arena* arena, Image source, ImageOrientation orientation)
{
    u64 arena_position = arena ? arena->position : 0;
    ImageDecodeContext context = {.arena = arena};
    u64 source_row_bytes = (u64)source.width * 4u;
    u64 source_prefix_bytes = 0;
    bool valid = arena && source.pixels.pointer && source.width && source.height &&
                 source.width <= UINT32_MAX / 4u && source.stride >= source_row_bytes &&
                 orientation >= IMAGE_ORIENTATION_TOP_LEFT && orientation <= IMAGE_ORIENTATION_LEFT_BOTTOM;
    if (valid)
    {
        valid = source.height - 1u <= (UINT64_MAX - source_row_bytes) / source.stride;
        source_prefix_bytes = valid ? (u64)(source.height - 1u) * source.stride : 0;
        valid = valid && source_prefix_bytes + source_row_bytes <= source.pixels.length;
    }
    u32 output_width = orientation >= IMAGE_ORIENTATION_LEFT_TOP ? source.height : source.width;
    u32 output_height = orientation >= IMAGE_ORIENTATION_LEFT_TOP ? source.width : source.height;
    u64 output_pixels = (u64)output_width * output_height;
    if (valid)
    {
        valid = output_width <= UINT32_MAX / 4u && output_pixels <= ARENA_MAX_RESERVATION / 4u;
    }
    if (!valid)
    {
        context.status = IMAGE_DECODE_INVALID_ARGUMENT;
    }
    u8* output = valid ? image_decode_allocate(&context, arena, output_pixels * 4u, BUSTER_ALIGN_OF(u32), 0) : 0;
    if (output)
    {
        context.image = (Image){
            .pixels = {.pointer = output, .length = output_pixels * 4u},
            .width = output_width,
            .height = output_height,
            .stride = output_width * 4u,
        };
        for (u32 y = 0; y < source.height; y += 1)
        {
            for (u32 x = 0; x < source.width; x += 1)
            {
                u32 destination_x = 0;
                u32 destination_y = 0;
                image_orientation_destination(orientation, source.width, source.height, x, y,
                                              &destination_x, &destination_y);
                u8 const* source_pixel = source.pixels.pointer + (u64)y * source.stride + (u64)x * 4u;
                u8* destination_pixel = output + (u64)destination_y * context.image.stride + (u64)destination_x * 4u;
                memcpy(destination_pixel, source_pixel, 4);
            }
        }
    }
    if (context.status != IMAGE_DECODE_SUCCESS && arena)
    {
        arena_set_position(arena, arena_position);
        context.image = (Image){0};
    }
    ImageTransformResult result = {.image = context.image, .status = context.status};
    return result;
}

#if BUSTER_UNITY_BUILD
#include <buster/lib/image/png.c>
#include <buster/lib/image/jpeg.c>
#include <buster/lib/image/gif.c>
#include <buster/lib/image/bmp.c>
#include <buster/lib/image/tga.c>
#include <buster/lib/image/qoi.c>
#include <buster/lib/image/pnm.c>
#endif
