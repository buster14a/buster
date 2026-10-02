#pragma once

#include <buster/lib/base.h>
#include <buster/lib/arena.h>

// Dependency-free raster-image decoding from untrusted, bounded memory.
// image_probe inspects metadata without allocating pixels. image_decode returns
// tightly packed, arena-owned, top-left-origin RGBA8 with straight alpha.
// Recognized Exif orientation is reported but not applied;
// image_apply_orientation copies a decoded image into display order when
// requested.
// Format-required sample unpacking and JPEG component conversion are applied;
// ICC, gamma, gamut and other color-management conversion is outside this
// module.
//
// Public API map:
//   ImageDecodeOptions       resource limits and optional scratch storage
//   ImageInformation         encoded layout, color and image-set metadata
//   ImageDecodeResult        canonical pixels or an exact failure category
//   image_detect_format      signature-first format recognition
//   image_probe              allocation-free dimensions/source metadata
//   image_decode             transactional canonical RGBA8 decoding
//   image_apply_orientation  copy a decoded image into display order

typedef enum ImageFormat
{
    IMAGE_FORMAT_UNKNOWN,
    IMAGE_FORMAT_PNG,
    IMAGE_FORMAT_JPEG,
    IMAGE_FORMAT_GIF,
    IMAGE_FORMAT_BMP,
    IMAGE_FORMAT_TGA,
    IMAGE_FORMAT_QOI,
    IMAGE_FORMAT_PNM,
    IMAGE_FORMAT_WEBP,
    IMAGE_FORMAT_TIFF,
    IMAGE_FORMAT_ICO,
    IMAGE_FORMAT_AVIF,
    IMAGE_FORMAT_HEIF,
    IMAGE_FORMAT_BIG_TIFF,
    IMAGE_FORMAT_JPEG_XL,
    IMAGE_FORMAT_JPEG_2000,
    IMAGE_FORMAT_PSD,
    IMAGE_FORMAT_OPENEXR,
    IMAGE_FORMAT_HDR,
    IMAGE_FORMAT_DDS,
    IMAGE_FORMAT_KTX1,
    IMAGE_FORMAT_KTX2,
    IMAGE_FORMAT_PFM,
    IMAGE_FORMAT_COUNT,
} ImageFormat;

typedef enum ImageDecodeStatus
{
    IMAGE_DECODE_SUCCESS,
    IMAGE_DECODE_INVALID_ARGUMENT,
    IMAGE_DECODE_UNRECOGNIZED_FORMAT,
    IMAGE_DECODE_TRUNCATED,
    IMAGE_DECODE_MALFORMED,
    IMAGE_DECODE_UNSUPPORTED_FORMAT,
    IMAGE_DECODE_UNSUPPORTED_FEATURE,
    IMAGE_DECODE_CHECKSUM_MISMATCH,
    IMAGE_DECODE_LIMIT_EXCEEDED,
    IMAGE_DECODE_CAPACITY_EXCEEDED,
    IMAGE_DECODE_STATUS_COUNT,
} ImageDecodeStatus;

typedef enum ImageUnsupportedFeature
{
    IMAGE_UNSUPPORTED_FEATURE_NONE,
    IMAGE_UNSUPPORTED_FEATURE_PROFILE,
    IMAGE_UNSUPPORTED_FEATURE_CODING,
    IMAGE_UNSUPPORTED_FEATURE_PRECISION,
    IMAGE_UNSUPPORTED_FEATURE_COMPONENT_MODEL,
    IMAGE_UNSUPPORTED_FEATURE_COLOR_TRANSFORM,
    IMAGE_UNSUPPORTED_FEATURE_INTERLACE,
    IMAGE_UNSUPPORTED_FEATURE_CONTAINER_FEATURE,
    IMAGE_UNSUPPORTED_FEATURE_ANIMATION,
    IMAGE_UNSUPPORTED_FEATURE_COUNT,
} ImageUnsupportedFeature;

typedef enum ImageExceededLimit
{
    IMAGE_EXCEEDED_LIMIT_NONE,
    IMAGE_EXCEEDED_LIMIT_WIDTH,
    IMAGE_EXCEEDED_LIMIT_HEIGHT,
    IMAGE_EXCEEDED_LIMIT_PIXELS,
    IMAGE_EXCEEDED_LIMIT_DECODED_BYTES,
    IMAGE_EXCEEDED_LIMIT_WORK,
    IMAGE_EXCEEDED_LIMIT_FRAMES,
    IMAGE_EXCEEDED_LIMIT_CHUNKS,
    IMAGE_EXCEEDED_LIMIT_SEGMENTS,
    IMAGE_EXCEEDED_LIMIT_BLOCKS,
    IMAGE_EXCEEDED_LIMIT_SCANS,
    IMAGE_EXCEEDED_LIMIT_DEPTH,
    IMAGE_EXCEEDED_LIMIT_COUNT,
} ImageExceededLimit;

typedef enum ImageOrientation
{
    IMAGE_ORIENTATION_TOP_LEFT = 1,
    IMAGE_ORIENTATION_TOP_RIGHT,
    IMAGE_ORIENTATION_BOTTOM_RIGHT,
    IMAGE_ORIENTATION_BOTTOM_LEFT,
    IMAGE_ORIENTATION_LEFT_TOP,
    IMAGE_ORIENTATION_RIGHT_TOP,
    IMAGE_ORIENTATION_RIGHT_BOTTOM,
    IMAGE_ORIENTATION_LEFT_BOTTOM,
} ImageOrientation;

typedef enum ImageColorModel
{
    IMAGE_COLOR_MODEL_UNKNOWN,
    IMAGE_COLOR_MODEL_GRAYSCALE,
    IMAGE_COLOR_MODEL_GRAYSCALE_ALPHA,
    IMAGE_COLOR_MODEL_RGB,
    IMAGE_COLOR_MODEL_RGBA,
    IMAGE_COLOR_MODEL_INDEXED,
    IMAGE_COLOR_MODEL_CMYK,
    IMAGE_COLOR_MODEL_YCBCR,
    IMAGE_COLOR_MODEL_YCCK,
    IMAGE_COLOR_MODEL_COUNT,
} ImageColorModel;

typedef u32 ImageColorMetadataFlags;
enum
{
    IMAGE_COLOR_METADATA_NONE = 0,
    IMAGE_COLOR_METADATA_ICC_PROFILE = (1u << 0),
    IMAGE_COLOR_METADATA_SRGB = (1u << 1),
    IMAGE_COLOR_METADATA_GAMMA = (1u << 2),
    IMAGE_COLOR_METADATA_CHROMATICITIES = (1u << 3),
    IMAGE_COLOR_METADATA_CICP = (1u << 4),
    IMAGE_COLOR_METADATA_EXIF = (1u << 5),
    IMAGE_COLOR_METADATA_ADOBE_TRANSFORM = (1u << 6),
};

typedef enum ImageQoiColorspace
{
    IMAGE_QOI_COLORSPACE_UNKNOWN,
    IMAGE_QOI_COLORSPACE_SRGB_LINEAR_ALPHA,
    IMAGE_QOI_COLORSPACE_ALL_CHANNELS_LINEAR,
    IMAGE_QOI_COLORSPACE_COUNT,
} ImageQoiColorspace;

typedef struct ImageDecodeOptions ImageDecodeOptions;
struct ImageDecodeOptions
{
    // UNKNOWN asks image_detect_format to select the codec. A format hint is
    // chiefly useful for TGA, whose header has no signature.
    ImageFormat format_hint;
    u32 max_width;
    u32 max_height;
    u64 max_pixels;
    u64 max_decoded_bytes;
    u64 max_work;
    // Structural ceilings count format records encountered while probing or
    // decoding. Zero selects the corresponding BUSTER_IMAGE_MAX_* default.
    u64 max_frames;
    u64 max_chunks;
    u64 max_segments;
    u64 max_blocks;
    u64 max_scans;
    u64 max_depth;
    // Optional working-storage arena. When omitted, decode uses the selected
    // thread's conflict-aware scratch arena, or the output arena as a safe
    // fallback on threads without a Buster thread context.
    Arena* scratch_arena;
};

typedef struct ImageInformation ImageInformation;
struct ImageInformation
{
    ImageFormat format;
    u32 width;
    u32 height;
    // Static images and the returned first image of a concatenated PNM stream
    // report 1. GIF reports validated image descriptors. APNG reports the acTL
    // animation-frame count, which can exclude a separate default image.
    u32 frame_count;
    // Encoded sample layout, before palette expansion or RGBA normalization.
    // source_bits_per_channel is the greatest stored channel precision when
    // channel widths differ. Indexed sources report one channel at their
    // encoded index width; their has_alpha flag may still be true because of
    // palette transparency. QOI preserves its informative header channel
    // count here, while has_alpha also reflects nonopaque stream samples.
    u8 source_channel_count;
    u8 source_bits_per_channel;
    bool has_alpha;
    ImageOrientation orientation;
    // These fields describe the encoded samples and recognized color
    // metadata. Decoding does not apply a color-management conversion.
    ImageColorModel source_color_model;
    ImageColorMetadataFlags color_metadata_flags;
    ImageQoiColorspace qoi_colorspace;
    // is_animated means the format declares an animation timeline; a format
    // may legally declare a timeline containing one frame.
    // has_more_images means the API returned only the first selectable image.
    bool is_animated;
    bool has_more_images;
};

typedef struct Image Image;
struct Image
{
    ByteSlice pixels;
    u32 width;
    u32 height;
    u32 stride;
};

typedef struct ImageProbeResult ImageProbeResult;
struct ImageProbeResult
{
    ImageInformation information;
    ImageDecodeStatus status;
    u64 error_offset;
    // Detail fields are zero unless the status names their category.
    ImageUnsupportedFeature unsupported_feature;
    ImageExceededLimit exceeded_limit;
    u64 observed_value;
    u64 limit_value;
};

typedef struct ImageDecodeResult ImageDecodeResult;
struct ImageDecodeResult
{
    Image image;
    ImageInformation information;
    ImageDecodeStatus status;
    u64 error_offset;
    // Detail fields are zero unless the status names their category.
    ImageUnsupportedFeature unsupported_feature;
    ImageExceededLimit exceeded_limit;
    u64 observed_value;
    u64 limit_value;
};

typedef struct ImageTransformResult ImageTransformResult;
struct ImageTransformResult
{
    Image image;
    ImageDecodeStatus status;
};

// Defaults bound both memory and hostile-input work. Callers may choose lower
// or higher nonzero limits through ImageDecodeOptions.
#define BUSTER_IMAGE_MAX_WIDTH 16384u
#define BUSTER_IMAGE_MAX_HEIGHT 16384u
#define BUSTER_IMAGE_MAX_PIXELS UINT64_C(67108864)
#define BUSTER_IMAGE_MAX_DECODED_BYTES (BUSTER_IMAGE_MAX_PIXELS * UINT64_C(4))
#define BUSTER_IMAGE_MAX_WORK UINT64_C(1073741824)
#define BUSTER_IMAGE_MAX_FRAMES UINT64_C(4096)
#define BUSTER_IMAGE_MAX_CHUNKS UINT64_C(16384)
#define BUSTER_IMAGE_MAX_SEGMENTS UINT64_C(16384)
#define BUSTER_IMAGE_MAX_BLOCKS UINT64_C(16777216)
#define BUSTER_IMAGE_MAX_SCANS UINT64_C(4096)
#define BUSTER_IMAGE_MAX_DEPTH UINT64_C(64)

BUSTER_F_DECL ImageFormat image_detect_format(ByteSlice encoded);
BUSTER_F_DECL ImageProbeResult image_probe(ByteSlice encoded, ImageDecodeOptions options);
BUSTER_F_DECL ImageDecodeResult image_decode(Arena* arena, ByteSlice encoded, ImageDecodeOptions options);
BUSTER_F_DECL ImageTransformResult image_apply_orientation(Arena* arena, Image source, ImageOrientation orientation);
