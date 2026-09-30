#pragma once

// Private contract shared by the image front door and codec translation
// units. Every parser operates on ImageDecodeContext, latches its first error,
// accounts hostile-input work, and leaves allocation rollback to image.c.

#include <buster/lib/image.h>
#include <buster/lib/integer.h>

typedef struct ImageDecodeContext ImageDecodeContext;
typedef struct ImageDecodeLimitCounters ImageDecodeLimitCounters;
struct ImageDecodeLimitCounters
{
    u64 frames;
    u64 chunks;
    u64 segments;
    u64 blocks;
    u64 scans;
    u64 depth;
};

struct ImageDecodeContext
{
    Arena* arena;
    ByteSlice encoded;
    ImageDecodeOptions options;
    ImageInformation information;
    Image image;
    ImageDecodeStatus status;
    u64 error_offset;
    ImageUnsupportedFeature unsupported_feature;
    ImageExceededLimit exceeded_limit;
    u64 observed_value;
    u64 limit_value;
    u64 work;
    ImageDecodeLimitCounters limit_counters;
    bool decode_pixels;
};

typedef struct ImageByteReader ImageByteReader;
struct ImageByteReader
{
    ImageDecodeContext* context;
    u64 position;
};

BUSTER_F_DECL void image_decode_error(ImageDecodeContext* context, ImageDecodeStatus status, u64 offset);
BUSTER_F_DECL void image_decode_set_unsupported_feature(ImageDecodeContext* context, ImageUnsupportedFeature feature, u64 offset);
BUSTER_F_DECL void image_decode_set_exceeded_limit(ImageDecodeContext* context, ImageExceededLimit limit,
                                                   u64 observed_value, u64 limit_value, u64 offset);
BUSTER_F_DECL bool image_decode_add_work(ImageDecodeContext* context, u64 amount, u64 offset);
BUSTER_F_DECL bool image_decode_check_limit(ImageDecodeContext* context, ImageExceededLimit limit,
                                            u64 observed_value, u64 offset);
BUSTER_F_DECL bool image_decode_count_limit(ImageDecodeContext* context, ImageExceededLimit limit,
                                            u64 amount, u64 offset);
BUSTER_F_DECL bool image_decode_enter_depth(ImageDecodeContext* context, u64 offset);
BUSTER_F_DECL void image_decode_leave_depth(ImageDecodeContext* context);
BUSTER_F_DECL bool image_decode_set_information(ImageDecodeContext* context, u32 width, u32 height, u32 frame_count,
                                               u8 source_channel_count, u8 source_bits_per_channel, bool has_alpha,
                                               ImageOrientation orientation, u64 offset);
BUSTER_F_DECL bool image_decode_arena_can_allocate(Arena* arena, u64 size, u64 alignment);
BUSTER_F_DECL u8* image_decode_allocate(ImageDecodeContext* context, Arena* arena, u64 size, u64 alignment, u64 offset);
BUSTER_F_DECL bool image_decode_allocate_pixels(ImageDecodeContext* context, u32 width, u32 height, u64 offset);
BUSTER_F_DECL bool image_decode_range(ByteSlice bytes, u64 offset, u64 size);
BUSTER_F_DECL u16 image_decode_u16_be(u8 const* bytes);
BUSTER_F_DECL u16 image_decode_u16_le(u8 const* bytes);
BUSTER_F_DECL u32 image_decode_u32_be(u8 const* bytes);
BUSTER_F_DECL u32 image_decode_u32_le(u8 const* bytes);
BUSTER_F_DECL u64 image_decode_u64_le(u8 const* bytes);
BUSTER_F_DECL bool image_decode_tiff_orientation(ByteSlice tiff, ImageOrientation* orientation,
                                                 bool* orientation_found, u64* error_offset);
BUSTER_F_DECL u8 image_decode_clamp_u8(s32 value);

BUSTER_F_DECL bool image_reader_u8(ImageByteReader* reader, u8* value);
BUSTER_F_DECL bool image_reader_u16_be(ImageByteReader* reader, u16* value);
BUSTER_F_DECL bool image_reader_u16_le(ImageByteReader* reader, u16* value);
BUSTER_F_DECL bool image_reader_u32_be(ImageByteReader* reader, u32* value);
BUSTER_F_DECL bool image_reader_u32_le(ImageByteReader* reader, u32* value);
BUSTER_F_DECL bool image_reader_skip(ImageByteReader* reader, u64 size);
BUSTER_F_DECL ByteSlice image_reader_slice(ImageByteReader* reader, u64 size);

BUSTER_F_DECL void image_png_process(ImageDecodeContext* context);
BUSTER_F_DECL void image_jpeg_process(ImageDecodeContext* context);
BUSTER_F_DECL void image_gif_process(ImageDecodeContext* context);
BUSTER_F_DECL void image_bmp_process(ImageDecodeContext* context);
BUSTER_F_DECL bool image_tga_detect(ByteSlice encoded);
BUSTER_F_DECL void image_tga_process(ImageDecodeContext* context);
BUSTER_F_DECL void image_qoi_process(ImageDecodeContext* context);
BUSTER_F_DECL void image_pnm_process(ImageDecodeContext* context);
