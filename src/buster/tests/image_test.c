// Headless image-reader contract and codec golden tests. This module owns its
// embedded encoded bytes, pixel oracles, and adversarial mutations; production
// codec behavior remains under lib/image/. The public test entry point is
// image_tests.
//
// Orientation:
// - image_test_fixtures and image_test_advanced_fixtures define the successful
//   encoded corpus and its canonical RGBA/metadata expectations.
// - image_test_information_matches_fixture and image_test_pixels_match compare
//   probe/decode results with those expectations.
// - ImageTestPngBuilder and image_test_png_append_chunk construct bounded
//   structural-limit inputs without adding opaque fixture blobs.
// - image_tests covers detection, orientation, limits, rollback, and malformed
//   derivatives in addition to the successful corpus.
//
// Fixture producers, independent pixel references, encoded-byte hashes, and
// the verification recipe are recorded in docs/image-test-fixtures.md.
#include <buster/tests/image_test.h>

#if BUSTER_INCLUDE_TESTS
#include <buster/lib/image.h>
#include <string.h>

typedef struct ImageDetectionCase ImageDetectionCase;
struct ImageDetectionCase
{
    ByteSlice bytes;
    ImageFormat format;
};

typedef struct ImageTestFixture ImageTestFixture;
struct ImageTestFixture
{
    u8 const* encoded;
    u64 encoded_size;
    u8 const* expected_rgba;
    u64 expected_rgba_size;
    ImageFormat format;
    u32 width;
    u32 height;
    u32 frame_count;
    u8 source_channel_count;
    u8 source_bits_per_channel;
    bool has_alpha;
    ImageColorModel source_color_model;
    bool repeated_pixel;
};

typedef struct ImageStructuralLimitCase ImageStructuralLimitCase;
struct ImageStructuralLimitCase
{
    u8 const* encoded;
    u64 encoded_size;
    ImageDecodeOptions options;
    ImageExceededLimit exceeded_limit;
    u64 error_offset;
    bool probe_limited;
};

BUSTER_GLOBAL_LOCAL u8 const image_test_expected_rgba_alpha[] = {
    255, 0, 0, 255, 0, 255, 0, 128,
    0, 0, 255, 0, 255, 255, 0, 255,
};

BUSTER_GLOBAL_LOCAL u8 const image_test_expected_rgba_opaque[] = {
    255, 0, 0, 255, 0, 255, 0, 255,
    0, 0, 255, 255, 255, 255, 0, 255,
};

BUSTER_GLOBAL_LOCAL u8 const image_test_expected_jpeg_pixel[] = {73, 73, 73, 255};
BUSTER_GLOBAL_LOCAL u8 const image_test_expected_black_opaque_pixel[] = {0, 0, 0, 255};
BUSTER_GLOBAL_LOCAL u8 const image_test_expected_black_transparent_pixel[] = {0, 0, 0, 0};
BUSTER_GLOBAL_LOCAL u8 const image_test_expected_jpeg_color_pixel[] = {180, 90, 30, 255};

// Pillow 12.3.0/libjpeg-turbo 6.2 reference output for the nonuniform 4:2:0
// fixture below. It exercises both staged IDCT rounding and centered fancy
// chroma upsampling, including odd image edges.
BUSTER_GLOBAL_LOCAL u8 const image_test_expected_jpeg_precision[] = {
    0x0f, 0x49, 0x9d, 0xff, 0x39, 0x53, 0xc2, 0xff, 0x5a, 0x35, 0xdb, 0xff, 0x97, 0x4b, 0xeb, 0xff,
    0x9b, 0x41, 0x9b, 0xff, 0xbc, 0x76, 0x81, 0xff, 0x91, 0x7d, 0x34, 0xff, 0x79, 0x92, 0x2d, 0xff,
    0x23, 0x60, 0x1d, 0xff, 0x0b, 0x60, 0x99, 0xff, 0x2f, 0x63, 0xb6, 0xff, 0x5e, 0x52, 0xda, 0xff,
    0xad, 0x7a, 0xfb, 0xff, 0xb8, 0x73, 0xb4, 0xff, 0xc3, 0x91, 0x86, 0xff, 0x99, 0x98, 0x3b, 0xff,
    0x8e, 0xb7, 0x41, 0xff, 0x34, 0x7f, 0x2c, 0xff, 0x1b, 0xa2, 0xa5, 0xff, 0x2d, 0x95, 0xae, 0xff,
    0x55, 0x7b, 0xc6, 0xff, 0x94, 0x91, 0xd8, 0xff, 0x9b, 0x85, 0x91, 0xff, 0x9c, 0x93, 0x5a, 0xff,
    0x81, 0xa7, 0x20, 0xff, 0x85, 0xd0, 0x35, 0xff, 0x2f, 0x98, 0x21, 0xff, 0x00, 0xa5, 0x83, 0xff,
    0x27, 0xae, 0xa0, 0xff, 0x60, 0xa5, 0xc6, 0xff, 0x90, 0xaa, 0xc5, 0xff, 0xa8, 0xac, 0x93, 0xff,
    0xb8, 0xc6, 0x71, 0xff, 0xac, 0xe2, 0x4c, 0xff, 0x9f, 0xf8, 0x50, 0xff, 0x49, 0xbd, 0x38, 0xff,
    0x00, 0xae, 0x7d, 0xff, 0x46, 0xd6, 0xb5, 0xff, 0x8b, 0xd8, 0xde, 0xff, 0x91, 0xb3, 0xb4, 0xff,
    0xa9, 0xb2, 0x85, 0xff, 0xb7, 0xc8, 0x6a, 0xff, 0x98, 0xcc, 0x3b, 0xff, 0x57, 0xab, 0x0f, 0xff,
    0x55, 0xc2, 0x45, 0xff, 0x36, 0xcc, 0xa9, 0xff, 0x79, 0xf0, 0xda, 0xff, 0xb0, 0xe7, 0xec, 0xff,
    0x92, 0x9e, 0x9e, 0xff, 0xbd, 0xaf, 0x8c, 0xff, 0xe7, 0xdf, 0x94, 0xff, 0xe2, 0xfd, 0x8a, 0xff,
    0x98, 0xcf, 0x58, 0xff, 0x00, 0x3a, 0x00, 0xff, 0x77, 0xd3, 0xde, 0xff, 0x92, 0xcf, 0xe1, 0xff,
    0x9f, 0xa2, 0xc1, 0xff, 0x58, 0x30, 0x4a, 0xff, 0x6d, 0x2c, 0x2a, 0xff, 0x77, 0x3b, 0x1f, 0xff,
    0x69, 0x50, 0x18, 0xff, 0x19, 0x1e, 0x00, 0xff, 0x1c, 0x3b, 0x1c, 0xff, 0x15, 0x3b, 0x6a, 0xff,
    0x38, 0x43, 0x71, 0xff, 0x75, 0x47, 0x76, 0xff, 0x69, 0x14, 0x3b, 0xff, 0xa6, 0x3a, 0x51, 0xff,
    0xad, 0x48, 0x4e, 0xff, 0x9e, 0x5b, 0x53, 0xff, 0x51, 0x30, 0x2b, 0xff, 0x3e, 0x3a, 0x48, 0xff,
    0x4a, 0x40, 0x89, 0xff, 0x4a, 0x27, 0x69, 0xff, 0x81, 0x2a, 0x61, 0xff, 0xad, 0x32, 0x5e, 0xff,
    0xc1, 0x31, 0x54, 0xff, 0xd9, 0x53, 0x6e, 0xff, 0xbe, 0x60, 0x7a, 0xff, 0x6d, 0x35, 0x52, 0xff,
    0x5f, 0x48, 0x72, 0xff,
};

BUSTER_GLOBAL_LOCAL u8 const image_test_expected_png_indexed_adam7[] = {
    255, 0, 0, 255, 0, 255, 0, 128, 0, 0, 255, 0, 255, 255, 0, 255,
    255, 255, 0, 255, 0, 0, 255, 0, 0, 255, 0, 128, 255, 0, 0, 255,
    0, 255, 0, 128, 255, 0, 0, 255, 255, 255, 0, 255, 0, 0, 255, 0,
    0, 0, 255, 0, 255, 255, 0, 255, 255, 0, 0, 255, 0, 255, 0, 128,
};

BUSTER_GLOBAL_LOCAL u8 const image_test_expected_gif_animation[] = {
    255, 0, 0, 255, 0, 255, 0, 255,
    0, 0, 0, 0, 255, 255, 0, 255,
};

BUSTER_GLOBAL_LOCAL u8 const image_test_expected_bmp_rle8[] = {
    255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 0, 255,
    255, 255, 0, 255, 0, 0, 255, 255, 0, 255, 0, 255, 255, 0, 0, 255,
};

BUSTER_GLOBAL_LOCAL u8 const image_test_expected_tga_rle_top_right[] = {
    255, 0, 0, 255, 0, 255, 0, 128, 0, 255, 0, 128,
    0, 0, 255, 0, 255, 255, 0, 255, 255, 255, 0, 255,
};

BUSTER_GLOBAL_LOCAL u8 const image_test_expected_qoi_mixed_ops[] = {
    16, 32, 48, 255, 17, 31, 48, 255, 20, 36, 54, 255, 20, 36, 54, 255,
    20, 36, 54, 255, 20, 36, 54, 255, 16, 32, 48, 255, 17, 33, 49, 255,
};

BUSTER_GLOBAL_LOCAL u8 const image_test_expected_gif_clipped[] = {
    0, 0, 0, 0, 0, 0, 0, 0, 255, 0, 0, 255,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 255, 255,
};

BUSTER_GLOBAL_LOCAL u8 const image_test_png[] = {
    0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44, 0x52,
    0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x02, 0x08, 0x06, 0x00, 0x00, 0x00, 0x72, 0xb6, 0x0d,
    0x24, 0x00, 0x00, 0x00, 0x14, 0x49, 0x44, 0x41, 0x54, 0x78, 0xda, 0x63, 0xf8, 0xcf, 0xc0, 0xf0,
    0x1f, 0x08, 0x1b, 0x18, 0xc0, 0x34, 0x10, 0x01, 0x00, 0x3d, 0xd9, 0x07, 0x7a, 0x2a, 0x9d, 0x16,
    0xe0, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82,
};

// One grayscale scanline split across two stored DEFLATE blocks. The block
// headers are at encoded offsets 43 and 49, independently of bit buffering.
BUSTER_GLOBAL_LOCAL u8 const image_test_png_two_deflate_blocks[] = {
    0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44, 0x52,
    0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x08, 0x00, 0x00, 0x00, 0x00, 0x3a, 0x7e, 0x9b,
    0x55, 0x00, 0x00, 0x00, 0x12, 0x49, 0x44, 0x41, 0x54, 0x78, 0x01, 0x00, 0x01, 0x00, 0xfe, 0xff,
    0x00, 0x01, 0x01, 0x00, 0xfe, 0xff, 0x00, 0x00, 0x02, 0x00, 0x01, 0xa7, 0x13, 0xa7, 0x59, 0x00,
    0x00, 0x00, 0x00, 0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82,
};

// The maximum legal PNG height and largest width compatible with the public
// RGBA stride keep byte arithmetic in u64 while making both output and 16-bit
// filtered scanlines exceed the arena reservation ceiling. Empty IDAT is
// sufficient for a metadata probe; decode rejects capacity before inflation.
BUSTER_GLOBAL_LOCAL u8 const image_test_png_unallocatable_filtered_size[] = {
    0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44, 0x52,
    0x3f, 0xff, 0xff, 0xff, 0x7f, 0xff, 0xff, 0xff, 0x10, 0x06, 0x00, 0x00, 0x00, 0xf9, 0xd4, 0x46,
    0x8e, 0x00, 0x00, 0x00, 0x00, 0x49, 0x44, 0x41, 0x54, 0x35, 0xaf, 0x06, 0x1e, 0x00, 0x00, 0x00,
    0x00, 0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82,
};

// Generated with Python's stdlib zlib.compressobj using Z_FIXED and Z_BLOCK.
// SHA-256: 92e350b757e286466e1d73f7925cb1cc45272973791943c30a8e668f8d60b9e4.
// The first block header starts at encoded offset 43; its EOB ends at bit 2 of
// offset 45, where the second block header begins without byte alignment.
BUSTER_GLOBAL_LOCAL u8 const image_test_png_unaligned_deflate_blocks[] = {
    0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44, 0x52,
    0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x08, 0x00, 0x00, 0x00, 0x00, 0x3a, 0x7e, 0x9b,
    0x55, 0x00, 0x00, 0x00, 0x0b, 0x49, 0x44, 0x41, 0x54, 0x78, 0x01, 0x62, 0x00, 0x8c, 0x01, 0x00,
    0x00, 0x02, 0x00, 0x01, 0x45, 0xf6, 0xf8, 0x33, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4e, 0x44,
    0xae, 0x42, 0x60, 0x82,
};

// Generated with Python's stdlib struct and zlib modules.
// SHA-256: 7ebbc450fde741b2963c39874862c234da619ddf2da1546e1f8e24a317e0c626.
// The sole PLTE entry is red, but the two-bit sample selects index three. PNG
// decoder recovery requires the unavailable palette entry to be opaque black.
BUSTER_GLOBAL_LOCAL u8 const image_test_png_out_of_range_index[] = {
    0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44, 0x52,
    0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x02, 0x03, 0x00, 0x00, 0x00, 0x62, 0x7b, 0x2c,
    0x1a, 0x00, 0x00, 0x00, 0x03, 0x50, 0x4c, 0x54, 0x45, 0xff, 0x00, 0x00, 0x19, 0xe2, 0x09, 0x37,
    0x00, 0x00, 0x00, 0x0a, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9c, 0x63, 0x38, 0x00, 0x00, 0x00, 0xc2,
    0x00, 0xc1, 0x52, 0x5e, 0x57, 0x51, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4e, 0x44, 0xae, 0x42,
    0x60, 0x82,
};

// Decoder-compatibility fixtures generated independently with Python's
// binascii/zlib modules. The reserved-bit ancillary chunk must be ignored;
// the unused high tRNS bits must be masked before comparing the sample.
BUSTER_GLOBAL_LOCAL u8 const image_test_png_reserved_ancillary[] = {
    0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44, 0x52,
    0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x02, 0x08, 0x00, 0x00, 0x00, 0x00, 0x57, 0xdd, 0x52,
    0xf8, 0x00, 0x00, 0x00, 0x00, 0x61, 0x62, 0x63, 0x64, 0xed, 0x82, 0xcd, 0x11, 0x00, 0x00, 0x00,
    0x0b, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9c, 0x63, 0x60, 0x00, 0x01, 0x00, 0x00, 0x06, 0x00, 0x01,
    0xfe, 0x8c, 0x67, 0xc8, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82,
};

BUSTER_GLOBAL_LOCAL u8 const image_test_png_trns_high_bits[] = {
    0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44, 0x52,
    0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x02, 0x08, 0x00, 0x00, 0x00, 0x00, 0x57, 0xdd, 0x52,
    0xf8, 0x00, 0x00, 0x00, 0x02, 0x74, 0x52, 0x4e, 0x53, 0x01, 0x00, 0x6f, 0x88, 0xfc, 0x79, 0x00,
    0x00, 0x00, 0x0b, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9c, 0x63, 0x60, 0x00, 0x01, 0x00, 0x00, 0x06,
    0x00, 0x01, 0xfe, 0x8c, 0x67, 0xc8, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4e, 0x44, 0xae, 0x42,
    0x60, 0x82,
};

BUSTER_GLOBAL_LOCAL u8 const image_test_png_reserved_critical[] = {
    0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44, 0x52,
    0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x08, 0x00, 0x00, 0x00, 0x00, 0x3a, 0x7e, 0x9b,
    0x55, 0x00, 0x00, 0x00, 0x00, 0x41, 0x62, 0x63, 0x64, 0x4d, 0xb0, 0x62, 0x2f, 0x00, 0x00, 0x00,
    0x0a, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9c, 0x63, 0x60, 0x00, 0x00, 0x00, 0x02, 0x00, 0x01, 0x48,
    0xaf, 0xa4, 0x71, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82,
};

// The first stream has an incomplete code-length alphabet. The next two have
// incomplete literal/length and distance alphabets. All three streams use
// only assigned codes and produced two zero bytes in the formerly permissive
// decoder, but RFC 1951 permits an incomplete data alphabet only when its one
// used symbol has bit length one.
BUSTER_GLOBAL_LOCAL u8 const image_test_png_incomplete_code_lengths[] = {
    0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44, 0x52,
    0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x08, 0x00, 0x00, 0x00, 0x00, 0x3a, 0x7e, 0x9b,
    0x55, 0x00, 0x00, 0x00, 0x50, 0x49, 0x44, 0x41, 0x54, 0x78, 0x01, 0x05, 0xc0, 0x01, 0x08, 0x00,
    0x00, 0x00, 0x00, 0x20, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x21, 0x00, 0x02, 0x00, 0x01, 0x66, 0x17, 0xbc, 0x60, 0x00, 0x00, 0x00,
    0x00, 0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82,
};

BUSTER_GLOBAL_LOCAL u8 const image_test_png_incomplete_literals[] = {
    0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44, 0x52,
    0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x08, 0x00, 0x00, 0x00, 0x00, 0x3a, 0x7e, 0x9b,
    0x55, 0x00, 0x00, 0x00, 0x12, 0x49, 0x44, 0x41, 0x54, 0x78, 0x01, 0x05, 0x80, 0x81, 0x08, 0x00,
    0x00, 0x00, 0x80, 0xf6, 0xa7, 0x3e, 0x10, 0x00, 0x02, 0x00, 0x01, 0x13, 0xd9, 0x3c, 0xab, 0x00,
    0x00, 0x00, 0x00, 0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82,
};

BUSTER_GLOBAL_LOCAL u8 const image_test_png_incomplete_distances[] = {
    0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44, 0x52,
    0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x08, 0x00, 0x00, 0x00, 0x00, 0x3a, 0x7e, 0x9b,
    0x55, 0x00, 0x00, 0x00, 0x13, 0x49, 0x44, 0x41, 0x54, 0x78, 0x01, 0x05, 0xc0, 0x01, 0x09, 0x00,
    0x00, 0x00, 0x80, 0x20, 0xff, 0xaf, 0x36, 0x02, 0x00, 0x02, 0x00, 0x01, 0xaa, 0x26, 0xae, 0xd7,
    0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82,
};

// A no-distance block is represented by one zero-length distance entry, not
// by a longer HDIST alphabet in which every entry has length zero.
BUSTER_GLOBAL_LOCAL u8 const image_test_png_multiple_empty_distances[] = {
    0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44, 0x52,
    0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x08, 0x00, 0x00, 0x00, 0x00, 0x3a, 0x7e, 0x9b,
    0x55, 0x00, 0x00, 0x00, 0x13, 0x49, 0x44, 0x41, 0x54, 0x78, 0x01, 0x05, 0xc1, 0x81, 0x08, 0x00,
    0x00, 0x00, 0x00, 0xa0, 0xfd, 0xa9, 0x2f, 0x02, 0x00, 0x02, 0x00, 0x01, 0x52, 0xba, 0x46, 0xd1,
    0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82,
};

// This zlib stream advertises a 256-byte window, builds 259 bytes of history
// with distance one, then illegally copies from distance 257.
BUSTER_GLOBAL_LOCAL u8 const image_test_png_small_window_distance[] = {
    0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44, 0x52,
    0x00, 0x00, 0x01, 0x05, 0x00, 0x00, 0x00, 0x01, 0x08, 0x00, 0x00, 0x00, 0x00, 0xf2, 0x1b, 0xe4,
    0xef, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x44, 0x41, 0x54, 0x08, 0x1d, 0x63, 0x18, 0x05, 0xc0, 0x00,
    0x00, 0x00, 0x01, 0x06, 0x00, 0x01, 0x8c, 0x0b, 0x69, 0x99, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45,
    0x4e, 0x44, 0xae, 0x42, 0x60, 0x82,
};

BUSTER_GLOBAL_LOCAL u8 const image_test_jpeg[] = {
    0xff, 0xd8, 0xff, 0xe0, 0x00, 0x10, 0x4a, 0x46, 0x49, 0x46, 0x00, 0x01, 0x01, 0x00, 0x00, 0x01,
    0x00, 0x01, 0x00, 0x00, 0xff, 0xdb, 0x00, 0x43, 0x00, 0x02, 0x01, 0x01, 0x01, 0x01, 0x01, 0x02,
    0x01, 0x01, 0x01, 0x02, 0x02, 0x02, 0x02, 0x02, 0x04, 0x03, 0x02, 0x02, 0x02, 0x02, 0x05, 0x04,
    0x04, 0x03, 0x04, 0x06, 0x05, 0x06, 0x06, 0x06, 0x05, 0x06, 0x06, 0x06, 0x07, 0x09, 0x08, 0x06,
    0x07, 0x09, 0x07, 0x06, 0x06, 0x08, 0x0b, 0x08, 0x09, 0x0a, 0x0a, 0x0a, 0x0a, 0x0a, 0x06, 0x08,
    0x0b, 0x0c, 0x0b, 0x0a, 0x0c, 0x09, 0x0a, 0x0a, 0x0a, 0xff, 0xc0, 0x00, 0x0b, 0x08, 0x00, 0x08,
    0x00, 0x08, 0x01, 0x01, 0x11, 0x00, 0xff, 0xc4, 0x00, 0x1f, 0x00, 0x00, 0x01, 0x05, 0x01, 0x01,
    0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x02, 0x03, 0x04,
    0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0xff, 0xc4, 0x00, 0xb5, 0x10, 0x00, 0x02, 0x01, 0x03,
    0x03, 0x02, 0x04, 0x03, 0x05, 0x05, 0x04, 0x04, 0x00, 0x00, 0x01, 0x7d, 0x01, 0x02, 0x03, 0x00,
    0x04, 0x11, 0x05, 0x12, 0x21, 0x31, 0x41, 0x06, 0x13, 0x51, 0x61, 0x07, 0x22, 0x71, 0x14, 0x32,
    0x81, 0x91, 0xa1, 0x08, 0x23, 0x42, 0xb1, 0xc1, 0x15, 0x52, 0xd1, 0xf0, 0x24, 0x33, 0x62, 0x72,
    0x82, 0x09, 0x0a, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2a, 0x34, 0x35,
    0x36, 0x37, 0x38, 0x39, 0x3a, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4a, 0x53, 0x54, 0x55,
    0x56, 0x57, 0x58, 0x59, 0x5a, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6a, 0x73, 0x74, 0x75,
    0x76, 0x77, 0x78, 0x79, 0x7a, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8a, 0x92, 0x93, 0x94,
    0x95, 0x96, 0x97, 0x98, 0x99, 0x9a, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7, 0xa8, 0xa9, 0xaa, 0xb2,
    0xb3, 0xb4, 0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba, 0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7, 0xc8, 0xc9,
    0xca, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda, 0xe1, 0xe2, 0xe3, 0xe4, 0xe5, 0xe6,
    0xe7, 0xe8, 0xe9, 0xea, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8, 0xf9, 0xfa, 0xff, 0xda,
    0x00, 0x08, 0x01, 0x01, 0x00, 0x00, 0x3f, 0x00, 0xf8, 0x8e, 0xbf, 0xff, 0xd9,
};

BUSTER_GLOBAL_LOCAL u8 const image_test_jpeg_precision[] = {
    0xff, 0xd8, 0xff, 0xe0, 0x00, 0x10, 0x4a, 0x46, 0x49, 0x46, 0x00, 0x01, 0x01, 0x00, 0x00, 0x01,
    0x00, 0x01, 0x00, 0x00, 0xff, 0xdb, 0x00, 0x43, 0x00, 0x10, 0x0b, 0x0c, 0x0e, 0x0c, 0x0a, 0x10,
    0x0e, 0x0d, 0x0e, 0x12, 0x11, 0x10, 0x13, 0x18, 0x28, 0x1a, 0x18, 0x16, 0x16, 0x18, 0x31, 0x23,
    0x25, 0x1d, 0x28, 0x3a, 0x33, 0x3d, 0x3c, 0x39, 0x33, 0x38, 0x37, 0x40, 0x48, 0x5c, 0x4e, 0x40,
    0x44, 0x57, 0x45, 0x37, 0x38, 0x50, 0x6d, 0x51, 0x57, 0x5f, 0x62, 0x67, 0x68, 0x67, 0x3e, 0x4d,
    0x71, 0x79, 0x70, 0x64, 0x78, 0x5c, 0x65, 0x67, 0x63, 0xff, 0xdb, 0x00, 0x43, 0x01, 0x11, 0x12,
    0x12, 0x18, 0x15, 0x18, 0x2f, 0x1a, 0x1a, 0x2f, 0x63, 0x42, 0x38, 0x42, 0x63, 0x63, 0x63, 0x63,
    0x63, 0x63, 0x63, 0x63, 0x63, 0x63, 0x63, 0x63, 0x63, 0x63, 0x63, 0x63, 0x63, 0x63, 0x63, 0x63,
    0x63, 0x63, 0x63, 0x63, 0x63, 0x63, 0x63, 0x63, 0x63, 0x63, 0x63, 0x63, 0x63, 0x63, 0x63, 0x63,
    0x63, 0x63, 0x63, 0x63, 0x63, 0x63, 0x63, 0x63, 0x63, 0x63, 0x63, 0x63, 0x63, 0x63, 0xff, 0xc0,
    0x00, 0x11, 0x08, 0x00, 0x09, 0x00, 0x09, 0x03, 0x01, 0x22, 0x00, 0x02, 0x11, 0x01, 0x03, 0x11,
    0x01, 0xff, 0xc4, 0x00, 0x1f, 0x00, 0x00, 0x01, 0x05, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09,
    0x0a, 0x0b, 0xff, 0xc4, 0x00, 0xb5, 0x10, 0x00, 0x02, 0x01, 0x03, 0x03, 0x02, 0x04, 0x03, 0x05,
    0x05, 0x04, 0x04, 0x00, 0x00, 0x01, 0x7d, 0x01, 0x02, 0x03, 0x00, 0x04, 0x11, 0x05, 0x12, 0x21,
    0x31, 0x41, 0x06, 0x13, 0x51, 0x61, 0x07, 0x22, 0x71, 0x14, 0x32, 0x81, 0x91, 0xa1, 0x08, 0x23,
    0x42, 0xb1, 0xc1, 0x15, 0x52, 0xd1, 0xf0, 0x24, 0x33, 0x62, 0x72, 0x82, 0x09, 0x0a, 0x16, 0x17,
    0x18, 0x19, 0x1a, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2a, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3a,
    0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4a, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5a,
    0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6a, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7a,
    0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8a, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99,
    0x9a, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7, 0xa8, 0xa9, 0xaa, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7,
    0xb8, 0xb9, 0xba, 0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7, 0xc8, 0xc9, 0xca, 0xd2, 0xd3, 0xd4, 0xd5,
    0xd6, 0xd7, 0xd8, 0xd9, 0xda, 0xe1, 0xe2, 0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9, 0xea, 0xf1,
    0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8, 0xf9, 0xfa, 0xff, 0xc4, 0x00, 0x1f, 0x01, 0x00, 0x03,
    0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01,
    0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0xff, 0xc4, 0x00, 0xb5, 0x11, 0x00,
    0x02, 0x01, 0x02, 0x04, 0x04, 0x03, 0x04, 0x07, 0x05, 0x04, 0x04, 0x00, 0x01, 0x02, 0x77, 0x00,
    0x01, 0x02, 0x03, 0x11, 0x04, 0x05, 0x21, 0x31, 0x06, 0x12, 0x41, 0x51, 0x07, 0x61, 0x71, 0x13,
    0x22, 0x32, 0x81, 0x08, 0x14, 0x42, 0x91, 0xa1, 0xb1, 0xc1, 0x09, 0x23, 0x33, 0x52, 0xf0, 0x15,
    0x62, 0x72, 0xd1, 0x0a, 0x16, 0x24, 0x34, 0xe1, 0x25, 0xf1, 0x17, 0x18, 0x19, 0x1a, 0x26, 0x27,
    0x28, 0x29, 0x2a, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3a, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49,
    0x4a, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5a, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69,
    0x6a, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7a, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88,
    0x89, 0x8a, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9a, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6,
    0xa7, 0xa8, 0xa9, 0xaa, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba, 0xc2, 0xc3, 0xc4,
    0xc5, 0xc6, 0xc7, 0xc8, 0xc9, 0xca, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda, 0xe2,
    0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9, 0xea, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8, 0xf9,
    0xfa, 0xff, 0xda, 0x00, 0x0c, 0x03, 0x01, 0x00, 0x02, 0x11, 0x03, 0x11, 0x00, 0x3f, 0x00, 0x7c,
    0x4d, 0xa7, 0x5a, 0xa6, 0xd6, 0x9d, 0x18, 0x84, 0xde, 0x76, 0x02, 0xc3, 0xe9, 0x90, 0x0f, 0xa5,
    0x63, 0xfd, 0xae, 0xc7, 0xfe, 0x7d, 0xee, 0x3f, 0xef, 0x95, 0xff, 0x00, 0x1a, 0x75, 0xa7, 0xfa,
    0x99, 0xbf, 0xdd, 0x6a, 0x65, 0x54, 0x60, 0xaa, 0xd4, 0x94, 0xa4, 0xdf, 0x6f, 0xb8, 0x8c, 0x46,
    0x1e, 0x31, 0x84, 0x13, 0x6d, 0xe8, 0x7f, 0xff, 0xd9,
};

// Pillow restart_marker_blocks=1 fixture. Byte 351 terminates the first MCU
// entropy segment and its low padding bits are the required all-ones fill.
BUSTER_GLOBAL_LOCAL u8 const image_test_jpeg_restart[] = {
    0xff, 0xd8, 0xff, 0xe0, 0x00, 0x10, 0x4a, 0x46, 0x49, 0x46, 0x00, 0x01, 0x01, 0x00, 0x00, 0x01,
    0x00, 0x01, 0x00, 0x00, 0xff, 0xdb, 0x00, 0x43, 0x00, 0x10, 0x0b, 0x0c, 0x0e, 0x0c, 0x0a, 0x10,
    0x0e, 0x0d, 0x0e, 0x12, 0x11, 0x10, 0x13, 0x18, 0x28, 0x1a, 0x18, 0x16, 0x16, 0x18, 0x31, 0x23,
    0x25, 0x1d, 0x28, 0x3a, 0x33, 0x3d, 0x3c, 0x39, 0x33, 0x38, 0x37, 0x40, 0x48, 0x5c, 0x4e, 0x40,
    0x44, 0x57, 0x45, 0x37, 0x38, 0x50, 0x6d, 0x51, 0x57, 0x5f, 0x62, 0x67, 0x68, 0x67, 0x3e, 0x4d,
    0x71, 0x79, 0x70, 0x64, 0x78, 0x5c, 0x65, 0x67, 0x63, 0xff, 0xc0, 0x00, 0x0b, 0x08, 0x00, 0x08,
    0x00, 0x10, 0x01, 0x01, 0x11, 0x00, 0xff, 0xc4, 0x00, 0x1f, 0x00, 0x00, 0x01, 0x05, 0x01, 0x01,
    0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x02, 0x03, 0x04,
    0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0xff, 0xc4, 0x00, 0xb5, 0x10, 0x00, 0x02, 0x01, 0x03,
    0x03, 0x02, 0x04, 0x03, 0x05, 0x05, 0x04, 0x04, 0x00, 0x00, 0x01, 0x7d, 0x01, 0x02, 0x03, 0x00,
    0x04, 0x11, 0x05, 0x12, 0x21, 0x31, 0x41, 0x06, 0x13, 0x51, 0x61, 0x07, 0x22, 0x71, 0x14, 0x32,
    0x81, 0x91, 0xa1, 0x08, 0x23, 0x42, 0xb1, 0xc1, 0x15, 0x52, 0xd1, 0xf0, 0x24, 0x33, 0x62, 0x72,
    0x82, 0x09, 0x0a, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2a, 0x34, 0x35,
    0x36, 0x37, 0x38, 0x39, 0x3a, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4a, 0x53, 0x54, 0x55,
    0x56, 0x57, 0x58, 0x59, 0x5a, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6a, 0x73, 0x74, 0x75,
    0x76, 0x77, 0x78, 0x79, 0x7a, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8a, 0x92, 0x93, 0x94,
    0x95, 0x96, 0x97, 0x98, 0x99, 0x9a, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7, 0xa8, 0xa9, 0xaa, 0xb2,
    0xb3, 0xb4, 0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba, 0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7, 0xc8, 0xc9,
    0xca, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda, 0xe1, 0xe2, 0xe3, 0xe4, 0xe5, 0xe6,
    0xe7, 0xe8, 0xe9, 0xea, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8, 0xf9, 0xfa, 0xff, 0xdd,
    0x00, 0x04, 0x00, 0x01, 0xff, 0xda, 0x00, 0x08, 0x01, 0x01, 0x00, 0x00, 0x3f, 0x00, 0x34, 0xed,
    0x1e, 0x0b, 0x1b, 0x56, 0xb9, 0xba, 0x65, 0x8a, 0x18, 0xc6, 0x59, 0xdb, 0xb7, 0xf9, 0xf4, 0xaf,
    0xff, 0xd0, 0x8e, 0xeb, 0x57, 0xb9, 0xbd, 0x91, 0xad, 0x74, 0x9d, 0xd6, 0xf6, 0xa0, 0x8c, 0x4e,
    0xb9, 0x59, 0x1f, 0x1e, 0x87, 0xf8, 0x41, 0xe3, 0xdf, 0x8f, 0x72, 0x2b, 0xff, 0xd9,
};
#define IMAGE_TEST_JPEG_RESTART_PADDING_OFFSET 351u
BUSTER_CT_CHECK(IMAGE_TEST_JPEG_RESTART_PADDING_OFFSET < sizeof(image_test_jpeg_restart));

BUSTER_GLOBAL_LOCAL u8 const image_test_gif[] = {
    0x47, 0x49, 0x46, 0x38, 0x37, 0x61, 0x02, 0x00, 0x02, 0x00, 0x81, 0x00, 0x00, 0xff, 0x00, 0x00,
    0x00, 0xff, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0x00, 0x2c, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00,
    0x02, 0x00, 0x00, 0x08, 0x07, 0x00, 0x01, 0x04, 0x10, 0x30, 0x20, 0x20, 0x00, 0x3b,
};

// The five 9-bit LSB-first codes are the four literal palette indices and
// end-of-information: 0, 1, 2, 3, 257. A leading clear code is permitted by
// GIF but not required by the image-reader contract.
BUSTER_GLOBAL_LOCAL u8 const image_test_gif_no_initial_clear[] = {
    0x47, 0x49, 0x46, 0x38, 0x37, 0x61, 0x02, 0x00, 0x02, 0x00, 0x81, 0x00, 0x00, 0xff, 0x00, 0x00,
    0x00, 0xff, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0x00, 0x2c, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00,
    0x02, 0x00, 0x00, 0x08, 0x06, 0x00, 0x02, 0x08, 0x18, 0x10, 0x10, 0x00, 0x3b,
};

BUSTER_GLOBAL_LOCAL u8 const image_test_gif_transparent_sentinel[] = {
    0x47, 0x49, 0x46, 0x38, 0x39, 0x61, 0x01, 0x00, 0x01, 0x00, 0x80, 0xff, 0x00, 0xff, 0x00, 0x00,
    0x00, 0xff, 0x00, 0x21, 0xf9, 0x04, 0x01, 0x00, 0x00, 0xff, 0x00, 0x2c, 0x00, 0x00, 0x00, 0x00,
    0x01, 0x00, 0x01, 0x00, 0x00, 0x08, 0x04, 0x00, 0xff, 0x05, 0x04, 0x00, 0x3b,
};

// With a three-bit code size, Clear/literal/EOI consumes nine bits from these
// two bytes and leaves seven zero padding bits in the final byte.
BUSTER_GLOBAL_LOCAL u8 const image_test_gif_minimum_code_size_two[] = {
    0x47, 0x49, 0x46, 0x38, 0x39, 0x61, 0x01, 0x00, 0x01, 0x00, 0x80, 0x00, 0x00,
    0xff, 0x00, 0x00, 0x00, 0xff, 0x00,
    0x2c, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00,
    0x02, 0x02, 0x44, 0x01, 0x00, 0x3b,
};

BUSTER_GLOBAL_LOCAL u8 const image_test_bmp[] = {
    0x42, 0x4d, 0x46, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x36, 0x00, 0x00, 0x00, 0x28, 0x00,
    0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x01, 0x00, 0x18, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x13, 0x0b, 0x00, 0x00, 0x13, 0x0b, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0x00, 0x00, 0x00, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00,
    0xff, 0x00, 0xff, 0x00, 0x00, 0x00,
};

// One-pixel BITMAPV3INFOHEADER with explicit RGBA masks. This exercises the
// distinction between source alpha and transparent gaps synthesized by RLE.
BUSTER_GLOBAL_LOCAL u8 const image_test_bmp_rgba[] = {
    0x42, 0x4d, 0x4a, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46, 0x00, 0x00, 0x00,
    0x38, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00,
    0x20, 0x00, 0x06, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0xff, 0x00, 0x00, 0xff, 0x00, 0x00, 0xff, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0xff, 0x03, 0x02, 0x01, 0x80,
};

BUSTER_GLOBAL_LOCAL u8 const image_test_tga[] = {
    0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x02, 0x00,
    0x20, 0x08, 0xff, 0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0x00, 0x00, 0xff, 0xff, 0x00, 0xff,
    0x00, 0x80,
};

BUSTER_GLOBAL_LOCAL u8 const image_test_qoi[] = {
    0x71, 0x6f, 0x69, 0x66, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x02, 0x04, 0x00, 0xff, 0xff,
    0x00, 0x00, 0xff, 0xff, 0x00, 0xff, 0x00, 0x80, 0xff, 0x00, 0x00, 0xff, 0x00, 0xff, 0xff, 0xff,
    0x00, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01,
};

// Independently assembled sequential grayscale JPEG.
// SHA-256: 74fed809afb112c7e018e6f6f0bad8e7df88dbde1715187178ae26cf18d648af.
// Its one-bit DC0 and EOB codes pack both 8-by-8 blocks into entropy byte 134:
// the second block begins at bit two rather than at the next unread byte.
BUSTER_GLOBAL_LOCAL u8 const image_test_jpeg_buffered_blocks[] = {
    0xff, 0xd8, 0xff, 0xdb, 0x00, 0x43, 0x00, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
    0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
    0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
    0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
    0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0xff, 0xc0, 0x00, 0x0b, 0x08, 0x00, 0x08, 0x00, 0x10,
    0x01, 0x01, 0x11, 0x00, 0xff, 0xc4, 0x00, 0x26, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x01, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0xda, 0x00, 0x08,
    0x01, 0x01, 0x00, 0x00, 0x3f, 0x00, 0x0f, 0xff, 0xd9,
};
#define IMAGE_TEST_JPEG_BUFFERED_BLOCK_OFFSET 134u
BUSTER_CT_CHECK(IMAGE_TEST_JPEG_BUFFERED_BLOCK_OFFSET < sizeof(image_test_jpeg_buffered_blocks));

// Independently assembled legal sequential JPEG with one component per scan.
// Its deliberately minimal DC/EOB Huffman tables produce neutral gray and put
// the three SOS markers at offsets 130, 141 and 152.
BUSTER_GLOBAL_LOCAL u8 const image_test_jpeg_three_scans[] = {
    0xff, 0xd8, 0xff, 0xdb, 0x00, 0x43, 0x00, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
    0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
    0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
    0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
    0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0xff, 0xc0, 0x00, 0x11, 0x08, 0x00, 0x01, 0x00, 0x01,
    0x03, 0x01, 0x11, 0x00, 0x02, 0x11, 0x00, 0x03, 0x11, 0x00, 0xff, 0xc4, 0x00, 0x26, 0x00, 0x01,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x10, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0xff, 0xda, 0x00, 0x08, 0x01, 0x01, 0x00, 0x00, 0x3f, 0x00, 0x3f, 0xff, 0xda, 0x00,
    0x08, 0x01, 0x02, 0x00, 0x00, 0x3f, 0x00, 0x3f, 0xff, 0xda, 0x00, 0x08, 0x01, 0x03, 0x00, 0x00,
    0x3f, 0x00, 0x3f, 0xff, 0xd9,
};

BUSTER_GLOBAL_LOCAL u8 const image_test_pnm[] = {
    0x50, 0x36, 0x0a, 0x23, 0x20, 0x62, 0x75, 0x73, 0x74, 0x65, 0x72, 0x20, 0x69, 0x6d, 0x61, 0x67,
    0x65, 0x20, 0x67, 0x6f, 0x6c, 0x64, 0x65, 0x6e, 0x0a, 0x32, 0x20, 0x32, 0x0a, 0x32, 0x35, 0x35,
    0x0a, 0xff, 0x00, 0x00, 0x00, 0xff, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0x00,
};

BUSTER_GLOBAL_LOCAL u8 const image_test_png_indexed_adam7[] = {
    0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44, 0x52,
    0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x04, 0x02, 0x03, 0x00, 0x00, 0x01, 0xa3, 0x98, 0x46,
    0x7b, 0x00, 0x00, 0x00, 0x0c, 0x50, 0x4c, 0x54, 0x45, 0xff, 0x00, 0x00, 0x00, 0xff, 0x00, 0x00,
    0x00, 0xff, 0xff, 0xff, 0x00, 0xd6, 0x02, 0x8f, 0x7b, 0x00, 0x00, 0x00, 0x04, 0x74, 0x52, 0x4e,
    0x53, 0xff, 0x80, 0x00, 0xff, 0xa1, 0xa1, 0x94, 0x66, 0x00, 0x00, 0x00, 0x14, 0x49, 0x44, 0x41,
    0x54, 0x78, 0xda, 0x63, 0x60, 0x60, 0x68, 0x60, 0x28, 0x00, 0x42, 0x05, 0x86, 0x27, 0x0c, 0x1b,
    0x01, 0x10, 0x8b, 0x03, 0x16, 0x98, 0x30, 0x88, 0xfc, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4e,
    0x44, 0xae, 0x42, 0x60, 0x82,
};

BUSTER_GLOBAL_LOCAL u8 const image_test_jpeg_color_420[] = {
    0xff, 0xd8, 0xff, 0xe0, 0x00, 0x10, 0x4a, 0x46, 0x49, 0x46, 0x00, 0x01, 0x01, 0x00, 0x00, 0x01,
    0x00, 0x01, 0x00, 0x00, 0xff, 0xdb, 0x00, 0x43, 0x00, 0x03, 0x02, 0x02, 0x03, 0x02, 0x02, 0x03,
    0x03, 0x03, 0x03, 0x04, 0x03, 0x03, 0x04, 0x05, 0x08, 0x05, 0x05, 0x04, 0x04, 0x05, 0x0a, 0x07,
    0x07, 0x06, 0x08, 0x0c, 0x0a, 0x0c, 0x0c, 0x0b, 0x0a, 0x0b, 0x0b, 0x0d, 0x0e, 0x12, 0x10, 0x0d,
    0x0e, 0x11, 0x0e, 0x0b, 0x0b, 0x10, 0x16, 0x10, 0x11, 0x13, 0x14, 0x15, 0x15, 0x15, 0x0c, 0x0f,
    0x17, 0x18, 0x16, 0x14, 0x18, 0x12, 0x14, 0x15, 0x14, 0xff, 0xdb, 0x00, 0x43, 0x01, 0x03, 0x04,
    0x04, 0x05, 0x04, 0x05, 0x09, 0x05, 0x05, 0x09, 0x14, 0x0d, 0x0b, 0x0d, 0x14, 0x14, 0x14, 0x14,
    0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14,
    0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14,
    0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0xff, 0xc0,
    0x00, 0x11, 0x08, 0x00, 0x10, 0x00, 0x10, 0x03, 0x01, 0x22, 0x00, 0x02, 0x11, 0x01, 0x03, 0x11,
    0x01, 0xff, 0xc4, 0x00, 0x15, 0x00, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x06, 0xff, 0xc4, 0x00, 0x14, 0x10, 0x01, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0xc4,
    0x00, 0x15, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x07, 0x08, 0xff, 0xc4, 0x00, 0x14, 0x11, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0xda, 0x00, 0x0c, 0x03,
    0x01, 0x00, 0x02, 0x11, 0x03, 0x11, 0x00, 0x3f, 0x00, 0x8f, 0x00, 0x0e, 0xa1, 0x5f, 0xff, 0xd9,
};

BUSTER_GLOBAL_LOCAL u8 const image_test_jpeg_progressive[] = {
    0xff, 0xd8, 0xff, 0xe0, 0x00, 0x10, 0x4a, 0x46, 0x49, 0x46, 0x00, 0x01, 0x01, 0x00, 0x00, 0x01,
    0x00, 0x01, 0x00, 0x00, 0xff, 0xdb, 0x00, 0x43, 0x00, 0x03, 0x02, 0x02, 0x03, 0x02, 0x02, 0x03,
    0x03, 0x03, 0x03, 0x04, 0x03, 0x03, 0x04, 0x05, 0x08, 0x05, 0x05, 0x04, 0x04, 0x05, 0x0a, 0x07,
    0x07, 0x06, 0x08, 0x0c, 0x0a, 0x0c, 0x0c, 0x0b, 0x0a, 0x0b, 0x0b, 0x0d, 0x0e, 0x12, 0x10, 0x0d,
    0x0e, 0x11, 0x0e, 0x0b, 0x0b, 0x10, 0x16, 0x10, 0x11, 0x13, 0x14, 0x15, 0x15, 0x15, 0x0c, 0x0f,
    0x17, 0x18, 0x16, 0x14, 0x18, 0x12, 0x14, 0x15, 0x14, 0xff, 0xdb, 0x00, 0x43, 0x01, 0x03, 0x04,
    0x04, 0x05, 0x04, 0x05, 0x09, 0x05, 0x05, 0x09, 0x14, 0x0d, 0x0b, 0x0d, 0x14, 0x14, 0x14, 0x14,
    0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14,
    0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14,
    0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0xff, 0xc2,
    0x00, 0x11, 0x08, 0x00, 0x08, 0x00, 0x08, 0x03, 0x01, 0x22, 0x00, 0x02, 0x11, 0x01, 0x03, 0x11,
    0x01, 0xff, 0xc4, 0x00, 0x15, 0x00, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x05, 0xff, 0xc4, 0x00, 0x15, 0x01, 0x01, 0x01, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x06, 0x07, 0xff,
    0xda, 0x00, 0x0c, 0x03, 0x01, 0x00, 0x02, 0x10, 0x03, 0x10, 0x00, 0x00, 0x01, 0x8e, 0x01, 0xd0,
    0xbf, 0xff, 0xc4, 0x00, 0x14, 0x10, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0xda, 0x00, 0x08, 0x01, 0x01, 0x00, 0x01, 0x05,
    0x02, 0x7f, 0xff, 0xc4, 0x00, 0x14, 0x11, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0xda, 0x00, 0x08, 0x01, 0x03, 0x01, 0x01,
    0x3f, 0x01, 0x7f, 0xff, 0xc4, 0x00, 0x14, 0x11, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0xda, 0x00, 0x08, 0x01, 0x02, 0x01,
    0x01, 0x3f, 0x01, 0x7f, 0xff, 0xc4, 0x00, 0x14, 0x10, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0xda, 0x00, 0x08, 0x01, 0x01,
    0x00, 0x06, 0x3f, 0x02, 0x7f, 0xff, 0xc4, 0x00, 0x14, 0x10, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0xda, 0x00, 0x08, 0x01,
    0x01, 0x00, 0x01, 0x3f, 0x21, 0x7f, 0xff, 0xda, 0x00, 0x0c, 0x03, 0x01, 0x00, 0x02, 0x00, 0x03,
    0x00, 0x00, 0x00, 0x10, 0x07, 0xff, 0xc4, 0x00, 0x14, 0x11, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0xda, 0x00, 0x08, 0x01,
    0x03, 0x01, 0x01, 0x3f, 0x10, 0x7f, 0xff, 0xc4, 0x00, 0x14, 0x11, 0x01, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0xda, 0x00, 0x08,
    0x01, 0x02, 0x01, 0x01, 0x3f, 0x10, 0x7f, 0xff, 0xc4, 0x00, 0x14, 0x10, 0x01, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0xda, 0x00,
    0x08, 0x01, 0x01, 0x00, 0x01, 0x3f, 0x10, 0x7f, 0xff, 0xd9,
};

BUSTER_GLOBAL_LOCAL u8 const image_test_gif_animation[] = {
    0x47, 0x49, 0x46, 0x38, 0x39, 0x61, 0x02, 0x00, 0x02, 0x00, 0x81, 0x00, 0x00, 0xff, 0x00, 0x00,
    0x00, 0xff, 0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0x00, 0x21, 0xff, 0x0b, 0x4e, 0x45, 0x54, 0x53,
    0x43, 0x41, 0x50, 0x45, 0x32, 0x2e, 0x30, 0x03, 0x01, 0x00, 0x00, 0x00, 0x21, 0xf9, 0x04, 0x09,
    0x01, 0x00, 0x02, 0x00, 0x2c, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x02, 0x00, 0x00, 0x08, 0x07,
    0x00, 0x01, 0x04, 0x10, 0x30, 0x20, 0x20, 0x00, 0x21, 0xf9, 0x04, 0x09, 0x02, 0x00, 0x02, 0x00,
    0x2c, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x02, 0x00, 0x81, 0xff, 0x00, 0x00, 0x00, 0xff, 0x00,
    0x00, 0x00, 0xff, 0xff, 0xff, 0x00, 0x08, 0x07, 0x00, 0x07, 0x08, 0x08, 0x00, 0x20, 0x20, 0x00,
    0x3b,
};

BUSTER_GLOBAL_LOCAL u8 const image_test_bmp_rle8[] = {
    0x42, 0x4d, 0x58, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46, 0x00, 0x00, 0x00, 0x28, 0x00,
    0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x01, 0x00, 0x08, 0x00, 0x01, 0x00,
    0x00, 0x00, 0x12, 0x00, 0x00, 0x00, 0x13, 0x0b, 0x00, 0x00, 0x13, 0x0b, 0x00, 0x00, 0x04, 0x00,
    0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0x00, 0x00, 0xff, 0x00, 0x00, 0xff, 0x00,
    0x00, 0x00, 0x00, 0xff, 0xff, 0x00, 0x00, 0x04, 0x03, 0x02, 0x01, 0x00, 0x00, 0x00, 0x00, 0x04,
    0x00, 0x01, 0x02, 0x03, 0x00, 0x00, 0x00, 0x01,
};

BUSTER_GLOBAL_LOCAL u8 const image_test_tga_rle_top_right[] = {
    0x00, 0x00, 0x0a, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x02, 0x00,
    0x20, 0x38, 0x81, 0x00, 0xff, 0x00, 0x80, 0x00, 0x00, 0x00, 0xff, 0xff, 0x81, 0x00, 0xff, 0xff,
    0xff, 0x00, 0xff, 0x00, 0x00, 0x00,
};

BUSTER_GLOBAL_LOCAL u8 const image_test_qoi_mixed_ops[] = {
    0x71, 0x6f, 0x69, 0x66, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x01, 0x03, 0x00, 0xfe, 0x10,
    0x20, 0x30, 0x76, 0xa5, 0x69, 0xc2, 0x15, 0x7f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01,
};

BUSTER_GLOBAL_LOCAL u8 const image_test_pam_rgba16[] = {
    0x50, 0x37, 0x0a, 0x57, 0x49, 0x44, 0x54, 0x48, 0x20, 0x32, 0x0a, 0x48, 0x45, 0x49, 0x47, 0x48,
    0x54, 0x20, 0x32, 0x0a, 0x44, 0x45, 0x50, 0x54, 0x48, 0x20, 0x34, 0x0a, 0x4d, 0x41, 0x58, 0x56,
    0x41, 0x4c, 0x20, 0x36, 0x35, 0x35, 0x33, 0x35, 0x0a, 0x54, 0x55, 0x50, 0x4c, 0x54, 0x59, 0x50,
    0x45, 0x20, 0x52, 0x47, 0x42, 0x5f, 0x41, 0x4c, 0x50, 0x48, 0x41, 0x0a, 0x45, 0x4e, 0x44, 0x48,
    0x44, 0x52, 0x0a, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0x00, 0x00, 0xff, 0xff, 0x00,
    0x00, 0x80, 0x80, 0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff, 0x00,
    0x00, 0xff, 0xff,
};

BUSTER_GLOBAL_LOCAL u8 const image_test_gif_clipped[] = {
    0x47, 0x49, 0x46, 0x38, 0x37, 0x61, 0x03, 0x00, 0x02, 0x00, 0x81, 0x03, 0x00, 0xff, 0x00, 0x00,
    0x00, 0xff, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0x00, 0x2c, 0x02, 0x00, 0x00, 0x00, 0x02, 0x00,
    0x02, 0x00, 0x00, 0x08, 0x07, 0x00, 0x01, 0x04, 0x10, 0x30, 0x20, 0x20, 0x00, 0x3b,
};

BUSTER_GLOBAL_LOCAL ImageTestFixture const image_test_fixtures[] = {
    {image_test_png, sizeof(image_test_png), image_test_expected_rgba_alpha, sizeof(image_test_expected_rgba_alpha),
     IMAGE_FORMAT_PNG, 2, 2, 1, 4, 8, true, IMAGE_COLOR_MODEL_RGBA, false},
    {image_test_png_reserved_ancillary, sizeof(image_test_png_reserved_ancillary), image_test_expected_black_opaque_pixel,
     sizeof(image_test_expected_black_opaque_pixel), IMAGE_FORMAT_PNG, 2, 2, 1, 1, 8, false, IMAGE_COLOR_MODEL_GRAYSCALE, true},
    {image_test_png_trns_high_bits, sizeof(image_test_png_trns_high_bits), image_test_expected_black_transparent_pixel,
     sizeof(image_test_expected_black_transparent_pixel), IMAGE_FORMAT_PNG, 2, 2, 1, 1, 8, true, IMAGE_COLOR_MODEL_GRAYSCALE, true},
    {image_test_jpeg, sizeof(image_test_jpeg), image_test_expected_jpeg_pixel, sizeof(image_test_expected_jpeg_pixel),
     IMAGE_FORMAT_JPEG, 8, 8, 1, 1, 8, false, IMAGE_COLOR_MODEL_GRAYSCALE, true},
    {image_test_gif, sizeof(image_test_gif), image_test_expected_rgba_opaque, sizeof(image_test_expected_rgba_opaque),
     IMAGE_FORMAT_GIF, 2, 2, 1, 1, 2, false, IMAGE_COLOR_MODEL_INDEXED, false},
    {image_test_bmp, sizeof(image_test_bmp), image_test_expected_rgba_opaque, sizeof(image_test_expected_rgba_opaque),
     IMAGE_FORMAT_BMP, 2, 2, 1, 3, 8, false, IMAGE_COLOR_MODEL_RGB, false},
    {image_test_tga, sizeof(image_test_tga), image_test_expected_rgba_alpha, sizeof(image_test_expected_rgba_alpha),
     IMAGE_FORMAT_TGA, 2, 2, 1, 4, 8, true, IMAGE_COLOR_MODEL_RGBA, false},
    {image_test_qoi, sizeof(image_test_qoi), image_test_expected_rgba_alpha, sizeof(image_test_expected_rgba_alpha),
     IMAGE_FORMAT_QOI, 2, 2, 1, 4, 8, true, IMAGE_COLOR_MODEL_RGBA, false},
    {image_test_pnm, sizeof(image_test_pnm), image_test_expected_rgba_opaque, sizeof(image_test_expected_rgba_opaque),
     IMAGE_FORMAT_PNM, 2, 2, 1, 3, 8, false, IMAGE_COLOR_MODEL_RGB, false},
};

BUSTER_GLOBAL_LOCAL ImageTestFixture const image_test_advanced_fixtures[] = {
    {image_test_png_indexed_adam7, sizeof(image_test_png_indexed_adam7), image_test_expected_png_indexed_adam7,
     sizeof(image_test_expected_png_indexed_adam7), IMAGE_FORMAT_PNG, 4, 4, 1, 1, 2, true, IMAGE_COLOR_MODEL_INDEXED, false},
    {image_test_jpeg_color_420, sizeof(image_test_jpeg_color_420), image_test_expected_jpeg_color_pixel,
     sizeof(image_test_expected_jpeg_color_pixel), IMAGE_FORMAT_JPEG, 16, 16, 1, 3, 8, false, IMAGE_COLOR_MODEL_YCBCR, true},
    {image_test_jpeg_precision, sizeof(image_test_jpeg_precision), image_test_expected_jpeg_precision,
     sizeof(image_test_expected_jpeg_precision), IMAGE_FORMAT_JPEG, 9, 9, 1, 3, 8, false, IMAGE_COLOR_MODEL_YCBCR, false},
    {image_test_gif_animation, sizeof(image_test_gif_animation), image_test_expected_gif_animation,
     sizeof(image_test_expected_gif_animation), IMAGE_FORMAT_GIF, 2, 2, 2, 1, 2, true, IMAGE_COLOR_MODEL_INDEXED, false},
    {image_test_gif_clipped, sizeof(image_test_gif_clipped), image_test_expected_gif_clipped,
     sizeof(image_test_expected_gif_clipped), IMAGE_FORMAT_GIF, 3, 2, 1, 1, 2, true, IMAGE_COLOR_MODEL_INDEXED, false},
    {image_test_gif_no_initial_clear, sizeof(image_test_gif_no_initial_clear), image_test_expected_rgba_opaque,
     sizeof(image_test_expected_rgba_opaque), IMAGE_FORMAT_GIF, 2, 2, 1, 1, 2, false, IMAGE_COLOR_MODEL_INDEXED, false},
    {image_test_bmp_rle8, sizeof(image_test_bmp_rle8), image_test_expected_bmp_rle8,
     sizeof(image_test_expected_bmp_rle8), IMAGE_FORMAT_BMP, 4, 2, 1, 1, 8, true, IMAGE_COLOR_MODEL_INDEXED, false},
    {image_test_tga_rle_top_right, sizeof(image_test_tga_rle_top_right), image_test_expected_tga_rle_top_right,
     sizeof(image_test_expected_tga_rle_top_right), IMAGE_FORMAT_TGA, 3, 2, 1, 4, 8, true, IMAGE_COLOR_MODEL_RGBA, false},
    {image_test_qoi_mixed_ops, sizeof(image_test_qoi_mixed_ops), image_test_expected_qoi_mixed_ops,
     sizeof(image_test_expected_qoi_mixed_ops), IMAGE_FORMAT_QOI, 8, 1, 1, 3, 8, false, IMAGE_COLOR_MODEL_RGB, false},
    {image_test_pam_rgba16, sizeof(image_test_pam_rgba16), image_test_expected_rgba_alpha,
     sizeof(image_test_expected_rgba_alpha), IMAGE_FORMAT_PNM, 2, 2, 1, 4, 16, true, IMAGE_COLOR_MODEL_RGBA, false},
};

BUSTER_GLOBAL_LOCAL ByteSlice image_test_encoded(ImageTestFixture const* fixture, u64 length)
{
    ByteSlice result = {.pointer = (u8*)fixture->encoded, .length = length};
    return result;
}

BUSTER_GLOBAL_LOCAL bool image_test_information_matches_fixture(ImageInformation information, ImageTestFixture const* fixture)
{
    ImageColorMetadataFlags expected_color_metadata = IMAGE_COLOR_METADATA_NONE;
    ImageQoiColorspace expected_qoi_colorspace = IMAGE_QOI_COLORSPACE_UNKNOWN;
    bool expected_is_animated = false;
    bool expected_has_more_images = false;
    if (fixture->format == IMAGE_FORMAT_GIF)
    {
        expected_is_animated = fixture->frame_count > 1;
        expected_has_more_images = fixture->frame_count > 1;
    }
    else if (fixture->format == IMAGE_FORMAT_QOI)
    {
        expected_color_metadata = IMAGE_COLOR_METADATA_SRGB;
        expected_qoi_colorspace = IMAGE_QOI_COLORSPACE_SRGB_LINEAR_ALPHA;
    }
    bool result = information.format == fixture->format && information.width == fixture->width && information.height == fixture->height &&
                   information.frame_count == fixture->frame_count && information.source_channel_count == fixture->source_channel_count &&
                   information.source_bits_per_channel == fixture->source_bits_per_channel && information.has_alpha == fixture->has_alpha &&
                   information.orientation == IMAGE_ORIENTATION_TOP_LEFT && information.source_color_model == fixture->source_color_model &&
                   information.color_metadata_flags == expected_color_metadata &&
                   information.qoi_colorspace == expected_qoi_colorspace && information.is_animated == expected_is_animated &&
                   information.has_more_images == expected_has_more_images;
    return result;
}

BUSTER_GLOBAL_LOCAL bool image_test_information_equal(ImageInformation a, ImageInformation b)
{
    bool result = a.format == b.format && a.width == b.width && a.height == b.height && a.frame_count == b.frame_count &&
                  a.source_channel_count == b.source_channel_count && a.source_bits_per_channel == b.source_bits_per_channel &&
                  a.has_alpha == b.has_alpha && a.orientation == b.orientation && a.source_color_model == b.source_color_model &&
                  a.color_metadata_flags == b.color_metadata_flags && a.qoi_colorspace == b.qoi_colorspace &&
                  a.is_animated == b.is_animated && a.has_more_images == b.has_more_images;
    return result;
}

BUSTER_GLOBAL_LOCAL bool image_test_pixels_match(Image image, ImageTestFixture const* fixture)
{
    u64 expected_size = (u64)fixture->width * fixture->height * 4;
    bool result = image.pixels.pointer && image.pixels.length == expected_size && image.width == fixture->width &&
                  image.height == fixture->height && image.stride == fixture->width * 4;
    if (result && fixture->repeated_pixel)
    {
        for (u64 offset = 0; offset < image.pixels.length && result; offset += fixture->expected_rgba_size)
        {
            result = !memcmp(image.pixels.pointer + offset, fixture->expected_rgba, fixture->expected_rgba_size);
        }
    }
    else if (result)
    {
        result = fixture->expected_rgba_size == expected_size && !memcmp(image.pixels.pointer, fixture->expected_rgba, expected_size);
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool image_test_image_empty(Image image)
{
    bool result = !image.pixels.pointer && !image.pixels.length && !image.width && !image.height && !image.stride;
    return result;
}

BUSTER_GLOBAL_LOCAL bool image_test_rejected_without_allocation(Arena* arena, ByteSlice encoded, ImageDecodeOptions options,
                                                                ImageDecodeStatus expected_status)
{
    u64 position = arena->position;
    ImageDecodeResult decoded = image_decode(arena, encoded, options);
    bool result = decoded.status == expected_status && image_test_image_empty(decoded.image) && arena->position == position;
    return result;
}

BUSTER_GLOBAL_LOCAL bool image_test_rejected_at_without_allocation(Arena* arena, ByteSlice encoded, ImageDecodeOptions options,
                                                                   ImageDecodeStatus expected_status, u64 expected_offset)
{
    ImageProbeResult probe = image_probe(encoded, options);
    u64 position = arena->position;
    ImageDecodeResult decoded = image_decode(arena, encoded, options);
    bool result = probe.status == expected_status && probe.error_offset == expected_offset &&
                  decoded.status == expected_status && decoded.error_offset == expected_offset &&
                  image_test_image_empty(decoded.image) && arena->position == position;
    return result;
}

BUSTER_GLOBAL_LOCAL bool image_test_pnm_decodes_to(Arena* arena, char const* text, u64 length, u8 const* expected, u64 expected_length)
{
    ImageDecodeOptions options = {.format_hint = IMAGE_FORMAT_PNM};
    ByteSlice encoded = {.pointer = (u8*)text, .length = length};
    ImageProbeResult probe = image_probe(encoded, options);
    u64 position = arena->position;
    ImageDecodeResult decoded = image_decode(arena, encoded, options);
    bool result = probe.status == IMAGE_DECODE_SUCCESS && decoded.status == IMAGE_DECODE_SUCCESS &&
                  decoded.image.pixels.length == expected_length && !memcmp(decoded.image.pixels.pointer, expected, expected_length);
    arena_set_position(arena, position);
    return result;
}

BUSTER_GLOBAL_LOCAL bool image_test_pam_tuple_unsupported(Arena* arena, char const* tuple)
{
    static char const prefix[] = "P7\nWIDTH 1\nHEIGHT 1\nDEPTH 3\nMAXVAL 255\nTUPLTYPE ";
    static char const suffix[] = "\nENDHDR\n\x11\x22\x33";
    u8 encoded_bytes[160];
    u64 length = sizeof(prefix) - 1u;
    memcpy(encoded_bytes, prefix, length);
    for (u64 index = 0; tuple[index]; index += 1)
    {
        encoded_bytes[length] = (u8)tuple[index];
        length += 1;
    }
    memcpy(encoded_bytes + length, suffix, sizeof(suffix) - 1u);
    length += sizeof(suffix) - 1u;
    ImageDecodeOptions options = {.format_hint = IMAGE_FORMAT_PNM};
    ByteSlice encoded = {.pointer = encoded_bytes, .length = length};
    ImageProbeResult probe = image_probe(encoded, options);
    u64 position = arena->position;
    ImageDecodeResult decoded = image_decode(arena, encoded, options);
    bool result = probe.status == IMAGE_DECODE_UNSUPPORTED_FEATURE &&
                  probe.unsupported_feature == IMAGE_UNSUPPORTED_FEATURE_COMPONENT_MODEL &&
                  decoded.status == IMAGE_DECODE_UNSUPPORTED_FEATURE &&
                  decoded.unsupported_feature == IMAGE_UNSUPPORTED_FEATURE_COMPONENT_MODEL &&
                  image_test_image_empty(decoded.image) && arena->position == position;
    return result;
}

#define IMAGE_TEST_TEXT(text) (text), sizeof(text) - 1u

typedef struct ImageTestPngBuilder ImageTestPngBuilder;
struct ImageTestPngBuilder
{
    u8* bytes;
    u64 capacity;
    u64 position;
    bool valid;
};

#define IMAGE_TEST_PNG_CHUNK_TYPE(a, b, c, d)                                                                                                                 \
    (((u32)(u8)(a) << 24u) | ((u32)(u8)(b) << 16u) | ((u32)(u8)(c) << 8u) | (u32)(u8)(d))

BUSTER_GLOBAL_LOCAL void image_test_u32_be(u8* bytes, u32 value)
{
    bytes[0] = (u8)(value >> 24u);
    bytes[1] = (u8)(value >> 16u);
    bytes[2] = (u8)(value >> 8u);
    bytes[3] = (u8)value;
}

BUSTER_GLOBAL_LOCAL void image_test_u32_le(u8* bytes, u32 value)
{
    bytes[0] = (u8)value;
    bytes[1] = (u8)(value >> 8u);
    bytes[2] = (u8)(value >> 16u);
    bytes[3] = (u8)(value >> 24u);
}

BUSTER_GLOBAL_LOCAL u32 image_test_png_crc(u8 const* bytes, u64 size)
{
    u32 result = UINT32_C(0xffffffff);
    for (u64 index = 0; index < size; index += 1)
    {
        result ^= bytes[index];
        for (u32 bit = 0; bit < 8; bit += 1)
        {
            u32 mask = 0u - (result & 1u);
            result = (result >> 1u) ^ (UINT32_C(0xedb88320) & mask);
        }
    }
    result = ~result;
    return result;
}

BUSTER_GLOBAL_LOCAL bool image_test_png_append_chunk(ImageTestPngBuilder* builder, u32 type, u8 const* data, u32 length)
{
    bool result = builder && builder->valid && builder->position <= builder->capacity &&
                  (u64)length + 12u <= builder->capacity - builder->position && (data || !length);
    if (result)
    {
        u8* destination = builder->bytes + builder->position;
        image_test_u32_be(destination, length);
        image_test_u32_be(destination + 4, type);
        if (length)
        {
            memcpy(destination + 8, data, length);
        }
        image_test_u32_be(destination + 8u + length, image_test_png_crc(destination + 4, (u64)length + 4u));
        builder->position += (u64)length + 12u;
    }
    if (builder)
    {
        builder->valid = result;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool image_test_probe_limit(ByteSlice encoded, ImageDecodeOptions options,
                                                ImageExceededLimit exceeded_limit, u64 error_offset)
{
    ImageProbeResult probe = image_probe(encoded, options);
    bool result = probe.status == IMAGE_DECODE_LIMIT_EXCEEDED && probe.exceeded_limit == exceeded_limit &&
                  probe.observed_value == 2 && probe.limit_value == 1 && probe.error_offset == error_offset &&
                  probe.unsupported_feature == IMAGE_UNSUPPORTED_FEATURE_NONE;
    return result;
}

BUSTER_GLOBAL_LOCAL bool image_test_decode_limit(Arena* arena, ByteSlice encoded, ImageDecodeOptions options,
                                                 ImageExceededLimit exceeded_limit, u64 error_offset)
{
    u64 position = arena->position;
    ImageDecodeResult decoded = image_decode(arena, encoded, options);
    bool result = decoded.status == IMAGE_DECODE_LIMIT_EXCEEDED && decoded.exceeded_limit == exceeded_limit &&
                  decoded.observed_value == 2 && decoded.limit_value == 1 && decoded.error_offset == error_offset &&
                  decoded.unsupported_feature == IMAGE_UNSUPPORTED_FEATURE_NONE && image_test_image_empty(decoded.image) &&
                  arena->position == position;
    return result;
}

// PNG work accounting. These fixtures are generated in memory: a stored-deflate
// writer produces the incompressible worst case, and a fixed-Huffman writer
// produces a decompression bomb. A full-size decode (64 Mpx of 16-bit RGBA)
// needs about 1.3 GiB of memory, so the budget is verified at 1/256 scale with
// the same ratios as the defaults: max_decoded_bytes is 4 per pixel and
// max_work is BUSTER_IMAGE_MAX_WORK_PER_PIXEL per pixel.
#define IMAGE_TEST_PNG_WORK_SIDE 512u
#define IMAGE_TEST_PNG_WORK_WORST_UNITS_PER_PIXEL 36u

typedef struct ImageTestBitWriter ImageTestBitWriter;
struct ImageTestBitWriter
{
    u8* bytes;
    u64 position;
    u64 bit_buffer;
    u32 bit_count;
};

BUSTER_GLOBAL_LOCAL void image_test_bits_write(ImageTestBitWriter* writer, u32 value, u32 count)
{
    writer->bit_buffer |= (u64)value << writer->bit_count;
    writer->bit_count += count;
    while (writer->bit_count >= 8u)
    {
        writer->bytes[writer->position] = (u8)writer->bit_buffer;
        writer->position += 1;
        writer->bit_buffer >>= 8u;
        writer->bit_count -= 8u;
    }
}

// Huffman codes are packed most-significant bit first.
BUSTER_GLOBAL_LOCAL void image_test_bits_write_code(ImageTestBitWriter* writer, u32 code, u32 count)
{
    u32 reversed = 0;
    for (u32 bit = 0; bit < count; bit += 1)
    {
        reversed |= ((code >> bit) & 1u) << (count - 1u - bit);
    }
    image_test_bits_write(writer, reversed, count);
}

BUSTER_GLOBAL_LOCAL u32 image_test_adler32(u8 const* bytes, u64 size)
{
    u32 s1 = 1;
    u32 s2 = 0;
    for (u64 index = 0; index < size; index += 1)
    {
        s1 = (s1 + bytes[index]) % 65521u;
        s2 = (s2 + s1) % 65521u;
    }
    return s2 << 16u | s1;
}

typedef struct ImageTestPngSpec ImageTestPngSpec;
struct ImageTestPngSpec
{
    u32 width;
    u32 height;
    u8 bit_depth;
    u8 color_type;
    u8 channel_count;
    // Compress zero-filled scanlines with fixed-Huffman matches instead of
    // storing pseudo-random scanlines.
    bool bomb;
    // Size of an ancillary private chunk placed before IDAT; zero omits it.
    u32 ancillary_size;
};

BUSTER_GLOBAL_LOCAL ByteSlice image_test_png_make(Arena* arena, ImageTestPngSpec spec)
{
    ByteSlice result = {0};
    u64 row_bytes = ((u64)spec.width * spec.channel_count * spec.bit_depth + 7u) / 8u;
    u64 filtered_size = (u64)spec.height * (row_bytes + 1u);
    u8* filtered = arena_allocate_zeroed(arena, u8, filtered_size);
    u64 zlib_capacity = spec.bomb ? filtered_size / 128u + 1024u : filtered_size + filtered_size / 65535u * 5u + 64u;
    u8* zlib = arena_allocate_zeroed(arena, u8, zlib_capacity);
    u64 png_capacity = 8u + 25u + (u64)spec.ancillary_size + 12u + zlib_capacity + 12u + 12u + 12u;
    u8* png = arena_allocate_zeroed(arena, u8, png_capacity);
    u8* ancillary = spec.ancillary_size ? arena_allocate_zeroed(arena, u8, spec.ancillary_size) : 0;
    if (filtered && zlib && png && (ancillary || !spec.ancillary_size))
    {
        u32 random = 1;
        for (u64 row = 0; row < spec.height && !spec.bomb; row += 1)
        {
            for (u64 index = 0; index < row_bytes; index += 1)
            {
                random = random * UINT32_C(1664525) + UINT32_C(1013904223);
                filtered[row * (row_bytes + 1u) + 1u + index] = (u8)(random >> 24u);
            }
        }
        u32 adler = image_test_adler32(filtered, filtered_size);
        ImageTestBitWriter bits = {.bytes = zlib};
        image_test_bits_write(&bits, 0x78u, 8);
        image_test_bits_write(&bits, 0x01u, 8);
        if (spec.bomb)
        {
            image_test_bits_write(&bits, 1u, 1);
            image_test_bits_write(&bits, 1u, 2);
            // Literal zero, then maximum-length matches at distance one.
            image_test_bits_write_code(&bits, 0x30u, 8);
            u64 remaining = filtered_size - 1u;
            while (remaining >= 258u)
            {
                image_test_bits_write_code(&bits, 0xc5u, 8);
                image_test_bits_write_code(&bits, 0u, 5);
                remaining -= 258u;
            }
            for (; remaining; remaining -= 1u)
            {
                image_test_bits_write_code(&bits, 0x30u, 8);
            }
            image_test_bits_write_code(&bits, 0u, 7);
            image_test_bits_write(&bits, 0u, (8u - bits.bit_count) % 8u);
        }
        else
        {
            for (u64 offset = 0; offset < filtered_size; offset += 65535u)
            {
                u64 length = BUSTER_MIN((u64)65535u, filtered_size - offset);
                bool final = offset + length >= filtered_size;
                image_test_bits_write(&bits, final ? 1u : 0u, 8);
                image_test_bits_write(&bits, (u32)length, 16);
                image_test_bits_write(&bits, (u32)length ^ 0xffffu, 16);
                memcpy(zlib + bits.position, filtered + offset, length);
                bits.position += length;
                if (final)
                {
                    break;
                }
            }
        }
        u64 zlib_size = bits.position;
        image_test_u32_be(zlib + zlib_size, adler);
        zlib_size += 4u;

        u8 ihdr[13] = {0};
        image_test_u32_be(ihdr, spec.width);
        image_test_u32_be(ihdr + 4, spec.height);
        ihdr[8] = spec.bit_depth;
        ihdr[9] = spec.color_type;
        static u8 const signature[8] = {137, 80, 78, 71, 13, 10, 26, 10};
        memcpy(png, signature, sizeof(signature));
        ImageTestPngBuilder builder = {.bytes = png, .capacity = png_capacity, .position = sizeof(signature), .valid = true};
        image_test_png_append_chunk(&builder, IMAGE_TEST_PNG_CHUNK_TYPE('I', 'H', 'D', 'R'), ihdr, sizeof(ihdr));
        if (spec.ancillary_size)
        {
            image_test_png_append_chunk(&builder, IMAGE_TEST_PNG_CHUNK_TYPE('p', 'r', 'V', 't'), ancillary, spec.ancillary_size);
        }
        image_test_png_append_chunk(&builder, IMAGE_TEST_PNG_CHUNK_TYPE('I', 'D', 'A', 'T'), zlib, (u32)zlib_size);
        image_test_png_append_chunk(&builder, IMAGE_TEST_PNG_CHUNK_TYPE('I', 'E', 'N', 'D'), 0, 0);
        if (builder.valid)
        {
            result = (ByteSlice){.pointer = png, .length = builder.position};
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL UnitTestResult image_test_png_work_budget(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};

    // The default budget must cover the worst valid PNG at the default pixel
    // limit: 36 units per pixel of 16-bit RGBA, plus framing slack.
    BUSTER_TEST(arguments, BUSTER_IMAGE_MAX_WORK == BUSTER_IMAGE_MAX_PIXELS * BUSTER_IMAGE_MAX_WORK_PER_PIXEL);
    BUSTER_TEST(arguments, BUSTER_IMAGE_MAX_WORK_PER_PIXEL > IMAGE_TEST_PNG_WORK_WORST_UNITS_PER_PIXEL);
    BUSTER_TEST(arguments, BUSTER_IMAGE_MAX_DECODED_BYTES == BUSTER_IMAGE_MAX_PIXELS * 4u);

    Arena* arena = arena_create((ArenaCreation){.reserved_size = BUSTER_MB(64), .flags = {.no_pool = true}});
    if (BUSTER_REQUIRE(arguments, arena != 0))
    {
        u64 pixels = (u64)IMAGE_TEST_PNG_WORK_SIDE * IMAGE_TEST_PNG_WORK_SIDE;
        u64 position = arena->position;
        // Worst valid case: 16-bit RGBA, incompressible (stored) scanlines.
        ByteSlice worst = image_test_png_make(arena, (ImageTestPngSpec){.width = IMAGE_TEST_PNG_WORK_SIDE,
                                                                        .height = IMAGE_TEST_PNG_WORK_SIDE,
                                                                        .bit_depth = 16,
                                                                        .color_type = 6,
                                                                        .channel_count = 4});
        u64 worst_position = arena->position;
        if (BUSTER_REQUIRE(arguments, worst.length != 0))
        {
            ImageDecodeOptions scaled = {
                .format_hint = IMAGE_FORMAT_PNG,
                .max_pixels = pixels,
                .max_decoded_bytes = pixels * 4u,
                .max_work = pixels * BUSTER_IMAGE_MAX_WORK_PER_PIXEL,
            };
            ImageDecodeResult accepted = image_decode(arena, worst, scaled);
            BUSTER_TEST(arguments, accepted.status == IMAGE_DECODE_SUCCESS && accepted.image.width == IMAGE_TEST_PNG_WORK_SIDE &&
                                       accepted.image.height == IMAGE_TEST_PNG_WORK_SIDE &&
                                       accepted.image.pixels.length == pixels * 4u);
            arena_set_position(arena, worst_position);

            // The accounting is tight enough to be meaningful: 36 units per
            // pixel plus a small framing allowance suffice, and 30 do not.
            scaled.max_work = pixels * IMAGE_TEST_PNG_WORK_WORST_UNITS_PER_PIXEL + pixels / 16u;
            ImageDecodeResult tight = image_decode(arena, worst, scaled);
            BUSTER_TEST(arguments, tight.status == IMAGE_DECODE_SUCCESS);
            arena_set_position(arena, worst_position);
            scaled.max_work = pixels * 30u;
            BUSTER_TEST(arguments, image_test_rejected_without_allocation(arena, worst, scaled, IMAGE_DECODE_LIMIT_EXCEEDED));
            ImageDecodeResult rejected = image_decode(arena, worst, scaled);
            BUSTER_TEST(arguments, rejected.exceeded_limit == IMAGE_EXCEEDED_LIMIT_WORK && rejected.limit_value == scaled.max_work &&
                                       rejected.observed_value > scaled.max_work);
            arena_set_position(arena, worst_position);

            // A decompression bomb: a few KiB of fixed-Huffman matches expand
            // to the same scanlines. The work budget, not the compressed size,
            // bounds it.
            arena_set_position(arena, position);
            ByteSlice bomb = image_test_png_make(arena, (ImageTestPngSpec){.width = IMAGE_TEST_PNG_WORK_SIDE,
                                                                           .height = IMAGE_TEST_PNG_WORK_SIDE,
                                                                           .bit_depth = 16,
                                                                           .color_type = 6,
                                                                           .channel_count = 4,
                                                                           .bomb = true});
            u64 bomb_position = arena->position;
            if (BUSTER_REQUIRE(arguments, bomb.length != 0 && bomb.length < pixels / 8u))
            {
                scaled.max_work = pixels * BUSTER_IMAGE_MAX_WORK_PER_PIXEL;
                ImageDecodeResult bomb_accepted = image_decode(arena, bomb, scaled);
                BUSTER_TEST(arguments, bomb_accepted.status == IMAGE_DECODE_SUCCESS);
                arena_set_position(arena, bomb_position);
                // Fewer units than the inflated size alone.
                scaled.max_work = pixels * 4u;
                ImageDecodeResult bomb_rejected = image_decode(arena, bomb, scaled);
                BUSTER_TEST(arguments, bomb_rejected.status == IMAGE_DECODE_LIMIT_EXCEEDED &&
                                           bomb_rejected.exceeded_limit == IMAGE_EXCEEDED_LIMIT_WORK &&
                                           bomb_rejected.limit_value == scaled.max_work);
                arena_set_position(arena, bomb_position);
            }
        }

        // Chunk bytes cost one unit each (CRC), so a valid PNG whose chunks
        // exceed 1/8 of the budget is not rejected, and one exceeding the
        // whole budget is.
        arena_set_position(arena, position);
        u32 ancillary_size = (u32)BUSTER_MB(1);
        ByteSlice heavy = image_test_png_make(arena, (ImageTestPngSpec){.width = 1,
                                                                        .height = 1,
                                                                        .bit_depth = 8,
                                                                        .color_type = 0,
                                                                        .channel_count = 1,
                                                                        .ancillary_size = ancillary_size});
        if (BUSTER_REQUIRE(arguments, heavy.length > ancillary_size))
        {
            ImageDecodeOptions options = {.format_hint = IMAGE_FORMAT_PNG, .max_work = ancillary_size + 256u};
            ImageDecodeResult heavy_accepted = image_decode(arena, heavy, options);
            BUSTER_TEST(arguments, heavy_accepted.status == IMAGE_DECODE_SUCCESS && heavy_accepted.image.width == 1);
            options.max_work = ancillary_size / 2u;
            ImageDecodeResult heavy_rejected = image_decode(arena, heavy, options);
            BUSTER_TEST(arguments, heavy_rejected.status == IMAGE_DECODE_LIMIT_EXCEEDED &&
                                       heavy_rejected.exceeded_limit == IMAGE_EXCEEDED_LIMIT_WORK &&
                                       heavy_rejected.limit_value == options.max_work);
        }
        BUSTER_TEST(arguments, arena_destroy(arena, 1));
    }
    return result;
}

#define IMAGE_TEST_PNG_PATHS_WIDTH 37u
#define IMAGE_TEST_PNG_PATHS_HEIGHT 19u
#define IMAGE_TEST_PNG_PATHS_ROW_BYTES (IMAGE_TEST_PNG_PATHS_WIDTH * 4u)
#define IMAGE_TEST_PNG_PATHS_FILTERED_SIZE (IMAGE_TEST_PNG_PATHS_HEIGHT * (IMAGE_TEST_PNG_PATHS_ROW_BYTES + 1u))

// Greedy fixed-Huffman encoder over a fixed distance menu. It emits 7-, 8- and
// 9-bit literal/length codes, overlapping matches (distance below length) at
// every copy width the decoder distinguishes, and whole-row matches.
BUSTER_GLOBAL_LOCAL void image_test_png_fixed_encode(ImageTestBitWriter* bits, u8 const* data, u32 size)
{
    static u16 const length_base[29] = {
        3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258,
    };
    static u8 const length_extra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
    static u16 const distance_base[16] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193};
    static u8 const distance_extra[16] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6};
    static u32 const distances[] = {1, 2, 3, 5, 8, 11, IMAGE_TEST_PNG_PATHS_ROW_BYTES + 1u};
    image_test_bits_write(bits, 1u, 1);
    image_test_bits_write(bits, 1u, 2);
    u32 position = 0;
    while (position < size)
    {
        u32 best_length = 0;
        u32 best_distance = 0;
        for (u32 choice = 0; choice < BUSTER_ARRAY_LENGTH(distances); choice += 1)
        {
            u32 distance = distances[choice];
            u32 length = 0;
            while (distance <= position && position + length < size && length < 258u &&
                   data[position + length] == data[position + length - distance])
            {
                length += 1;
            }
            if (length > best_length)
            {
                best_length = length;
                best_distance = distance;
            }
        }
        if (best_length >= 3u)
        {
            u32 length_index = BUSTER_ARRAY_LENGTH(length_base) - 1u;
            while (length_base[length_index] > best_length)
            {
                length_index -= 1u;
            }
            u32 symbol = 257u + length_index;
            if (symbol <= 279u)
            {
                image_test_bits_write_code(bits, symbol - 256u, 7);
            }
            else
            {
                image_test_bits_write_code(bits, 0xc0u + symbol - 280u, 8);
            }
            image_test_bits_write(bits, best_length - length_base[length_index], length_extra[length_index]);
            u32 distance_index = BUSTER_ARRAY_LENGTH(distance_base) - 1u;
            while (distance_base[distance_index] > best_distance)
            {
                distance_index -= 1u;
            }
            image_test_bits_write_code(bits, distance_index, 5);
            image_test_bits_write(bits, best_distance - distance_base[distance_index], distance_extra[distance_index]);
            position += best_length;
        }
        else
        {
            u8 literal = data[position];
            if (literal < 144u)
            {
                image_test_bits_write_code(bits, 0x30u + literal, 8);
            }
            else
            {
                image_test_bits_write_code(bits, 0x190u + literal - 144u, 9);
            }
            position += 1;
        }
    }
    image_test_bits_write_code(bits, 0u, 7);
    image_test_bits_write(bits, 0u, (8u - bits->bit_count) % 8u);
}

// Bulk inflate paths: table-driven Huffman decode, bulk literal and match
// emission, stored-block copies and deferred Adler-32. Each stream is also
// split into tiny IDAT chunks so that every refill and table peek meets a
// chunk boundary and falls back to the byte-serial reader.
BUSTER_GLOBAL_LOCAL UnitTestResult image_test_png_inflate_paths(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    Arena* arena = arena_create((ArenaCreation){.reserved_size = BUSTER_MB(4), .flags = {.no_pool = true}});
    if (BUSTER_REQUIRE(arguments, arena != 0))
    {
        u8 filtered[IMAGE_TEST_PNG_PATHS_FILTERED_SIZE] = {0};
        u8 expected[IMAGE_TEST_PNG_PATHS_HEIGHT * IMAGE_TEST_PNG_PATHS_ROW_BYTES];
        u32 random = 7;
        for (u32 row = 0; row < IMAGE_TEST_PNG_PATHS_HEIGHT; row += 1)
        {
            u8* line = filtered + row * (IMAGE_TEST_PNG_PATHS_ROW_BYTES + 1u) + 1u;
            u8 const* above = line - (IMAGE_TEST_PNG_PATHS_ROW_BYTES + 1u);
            for (u32 index = 0; index < IMAGE_TEST_PNG_PATHS_ROW_BYTES; index += 1)
            {
                random = random * UINT32_C(1664525) + UINT32_C(1013904223);
                u8 noise = (u8)(random >> 24u);
                u32 period = row % 5u == 0 ? 1u : row % 5u == 1 ? 3u : row % 5u == 2 ? 11u : 0;
                u8 value = period ? (u8)(150u + (index % period) * 9u) : noise;
                // Every fifth row repeats the row above, a distance of one
                // filtered row that is longer than the match.
                line[index] = row % 5u == 4u ? above[index] : value;
            }
            memcpy(expected + row * IMAGE_TEST_PNG_PATHS_ROW_BYTES, line, IMAGE_TEST_PNG_PATHS_ROW_BYTES);
        }
        u32 adler = image_test_adler32(filtered, sizeof(filtered));

        for (u32 stored = 0; stored < 2u; stored += 1)
        {
            u8 zlib[IMAGE_TEST_PNG_PATHS_FILTERED_SIZE * 2u] = {0};
            ImageTestBitWriter bits = {.bytes = zlib};
            image_test_bits_write(&bits, 0x78u, 8);
            image_test_bits_write(&bits, 0x01u, 8);
            if (stored)
            {
                image_test_bits_write(&bits, 1u, 8);
                image_test_bits_write(&bits, (u32)sizeof(filtered), 16);
                image_test_bits_write(&bits, (u32)sizeof(filtered) ^ 0xffffu, 16);
                memcpy(zlib + bits.position, filtered, sizeof(filtered));
                bits.position += sizeof(filtered);
            }
            else
            {
                image_test_png_fixed_encode(&bits, filtered, (u32)sizeof(filtered));
            }
            image_test_u32_be(zlib + bits.position, adler);
            u64 zlib_size = bits.position + 4u;

            // Splits of 1, 3 and 7 bytes put chunk boundaries inside codes,
            // matches and stored runs; UINT32_MAX keeps one IDAT. The second
            // pass flips an Adler-32 bit, which the deferred checksum must
            // still report at the trailer.
            static u32 const splits[] = {1, 3, 7, UINT32_MAX};
            for (u32 variant = 0; variant < BUSTER_ARRAY_LENGTH(splits) * 2u; variant += 1)
            {
                u32 split = splits[variant % BUSTER_ARRAY_LENGTH(splits)];
                bool corrupt = variant >= BUSTER_ARRAY_LENGTH(splits);
                zlib[zlib_size - 1u] = (u8)(adler ^ (corrupt ? 1u : 0u));
                u64 chunk_count = split == UINT32_MAX ? 1u : (zlib_size + split - 1u) / split;
                u64 capacity = 8u + 25u + zlib_size + chunk_count * 12u + 12u;
                u64 position = arena->position;
                u8* png = arena_allocate_zeroed(arena, u8, capacity);
                if (BUSTER_REQUIRE(arguments, png != 0))
                {
                    u8 ihdr[13] = {0};
                    image_test_u32_be(ihdr, IMAGE_TEST_PNG_PATHS_WIDTH);
                    image_test_u32_be(ihdr + 4, IMAGE_TEST_PNG_PATHS_HEIGHT);
                    ihdr[8] = 8;
                    ihdr[9] = 6;
                    static u8 const signature[8] = {137, 80, 78, 71, 13, 10, 26, 10};
                    memcpy(png, signature, sizeof(signature));
                    ImageTestPngBuilder builder = {.bytes = png, .capacity = capacity, .position = sizeof(signature), .valid = true};
                    image_test_png_append_chunk(&builder, IMAGE_TEST_PNG_CHUNK_TYPE('I', 'H', 'D', 'R'), ihdr, sizeof(ihdr));
                    for (u64 offset = 0; offset < zlib_size; offset += BUSTER_MIN((u64)split, zlib_size - offset))
                    {
                        u32 length = (u32)BUSTER_MIN((u64)split, zlib_size - offset);
                        image_test_png_append_chunk(&builder, IMAGE_TEST_PNG_CHUNK_TYPE('I', 'D', 'A', 'T'), zlib + offset, length);
                    }
                    image_test_png_append_chunk(&builder, IMAGE_TEST_PNG_CHUNK_TYPE('I', 'E', 'N', 'D'), 0, 0);
                    if (BUSTER_REQUIRE(arguments, builder.valid))
                    {
                        ByteSlice encoded = {.pointer = png, .length = builder.position};
                        ImageDecodeResult decoded = image_decode(arena, encoded, (ImageDecodeOptions){.format_hint = IMAGE_FORMAT_PNG});
                        if (corrupt)
                        {
                            BUSTER_TEST(arguments, decoded.status == IMAGE_DECODE_CHECKSUM_MISMATCH &&
                                                       image_test_image_empty(decoded.image));
                        }
                        else
                        {
                            BUSTER_TEST(arguments, decoded.status == IMAGE_DECODE_SUCCESS &&
                                                       decoded.image.width == IMAGE_TEST_PNG_PATHS_WIDTH &&
                                                       decoded.image.height == IMAGE_TEST_PNG_PATHS_HEIGHT &&
                                                       decoded.image.pixels.length == sizeof(expected) &&
                                                       !memcmp(decoded.image.pixels.pointer, expected, sizeof(expected)));
                        }
                    }
                }
                arena_set_position(arena, position);
            }
        }
        BUSTER_TEST(arguments, arena_destroy(arena, 1));
    }
    return result;
}

UnitTestResult image_tests(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    BUSTER_TEST_FIXTURE(arguments, image_test_png_work_budget);
    BUSTER_TEST_FIXTURE(arguments, image_test_png_inflate_paths);
    u8 png[] = {137, 80, 78, 71, 13, 10, 26, 10};
    u8 jpeg[] = {0xff, 0xd8, 0xff};
    u8 gif[] = {'G', 'I', 'F', '8', '9', 'a'};
    u8 bmp[] = {'B', 'M'};
    u8 qoi[] = {'q', 'o', 'i', 'f'};
    u8 pnm[] = {'P', '6'};
    u8 webp[] = {'R', 'I', 'F', 'F', 4, 0, 0, 0, 'W', 'E', 'B', 'P'};
    u8 tiff[] = {'I', 'I', 42, 0};
    u8 big_tiff_le[] = {'I', 'I', 43, 0};
    u8 big_tiff_be[] = {'M', 'M', 0, 43};
    u8 ico[] = {0, 0, 1, 0, 1, 0, 1, 1, 0, 0, 1, 0, 32, 0, 1, 0, 0, 0, 22, 0, 0, 0, 0};
    u8 cur[] = {0, 0, 2, 0, 1, 0, 1, 1, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 22, 0, 0, 0, 0};
    u8 avif[] = {0, 0, 0, 16, 'f', 't', 'y', 'p', 'a', 'v', 'i', 'f', 0, 0, 0, 0};
    u8 heif[] = {0, 0, 0, 16, 'f', 't', 'y', 'p', 'h', 'e', 'i', 'c', 0, 0, 0, 0};
    u8 mif1_avif[] = {0, 0, 0, 20, 'f', 't', 'y', 'p', 'm', 'i', 'f', '1', 0, 0, 0, 0, 'a', 'v', 'i', 'f'};
    u8 misaligned_avif[] = {0, 0, 0, 21, 'f', 't', 'y', 'p', 'n', 'o', 'n', 'e', 0, 0, 0, 0, 0, 'a', 'v', 'i', 'f'};
    u8 jpeg_xl_codestream[] = {0xff, 0x0a};
    u8 jpeg_xl_container[] = {0, 0, 0, 12, 'J', 'X', 'L', ' ', 13, 10, 0x87, 10};
    u8 jpeg_2000_codestream[] = {0xff, 0x4f, 0xff, 0x51};
    u8 jp2[] = {0, 0, 0, 12, 'j', 'P', ' ', ' ', 13, 10, 0x87, 10};
    u8 psd[] = {'8', 'B', 'P', 'S'};
    u8 openexr[] = {0x76, 0x2f, 0x31, 0x01};
    u8 hdr[] = {'#', '?', 'R', 'A', 'D', 'I', 'A', 'N', 'C', 'E'};
    u8 rgbe[] = {'#', '?', 'R', 'G', 'B', 'E'};
    u8 dds[] = {'D', 'D', 'S', ' '};
    u8 ktx1[] = {0xab, 'K', 'T', 'X', ' ', '1', '1', 0xbb, 13, 10, 26, 10};
    u8 ktx2[] = {0xab, 'K', 'T', 'X', ' ', '2', '0', 0xbb, 13, 10, 26, 10};
    u8 pfm_rgb[] = {'P', 'F', '\n'};
    u8 pfm_grayscale[] = {'P', 'f', '\r', '\n'};
    u8 tga[] = {0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 1, 0, 24, 0, 0, 0, 0};
    u8 truncated_tga[] = {0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 1, 0, 24, 0};
    ImageDetectionCase cases[] = {
        {BUSTER_ARRAY_TO_BYTE_SLICE(png), IMAGE_FORMAT_PNG},
        {BUSTER_ARRAY_TO_BYTE_SLICE(jpeg), IMAGE_FORMAT_JPEG},
        {BUSTER_ARRAY_TO_BYTE_SLICE(gif), IMAGE_FORMAT_GIF},
        {BUSTER_ARRAY_TO_BYTE_SLICE(bmp), IMAGE_FORMAT_BMP},
        {BUSTER_ARRAY_TO_BYTE_SLICE(qoi), IMAGE_FORMAT_QOI},
        {BUSTER_ARRAY_TO_BYTE_SLICE(pnm), IMAGE_FORMAT_PNM},
        {BUSTER_ARRAY_TO_BYTE_SLICE(webp), IMAGE_FORMAT_WEBP},
        {BUSTER_ARRAY_TO_BYTE_SLICE(tiff), IMAGE_FORMAT_TIFF},
        {BUSTER_ARRAY_TO_BYTE_SLICE(big_tiff_le), IMAGE_FORMAT_BIG_TIFF},
        {BUSTER_ARRAY_TO_BYTE_SLICE(big_tiff_be), IMAGE_FORMAT_BIG_TIFF},
        {BUSTER_ARRAY_TO_BYTE_SLICE(ico), IMAGE_FORMAT_ICO},
        {BUSTER_ARRAY_TO_BYTE_SLICE(cur), IMAGE_FORMAT_ICO},
        {BUSTER_ARRAY_TO_BYTE_SLICE(avif), IMAGE_FORMAT_AVIF},
        {BUSTER_ARRAY_TO_BYTE_SLICE(heif), IMAGE_FORMAT_HEIF},
        {BUSTER_ARRAY_TO_BYTE_SLICE(mif1_avif), IMAGE_FORMAT_AVIF},
        {BUSTER_ARRAY_TO_BYTE_SLICE(misaligned_avif), IMAGE_FORMAT_UNKNOWN},
        {BUSTER_ARRAY_TO_BYTE_SLICE(jpeg_xl_codestream), IMAGE_FORMAT_JPEG_XL},
        {BUSTER_ARRAY_TO_BYTE_SLICE(jpeg_xl_container), IMAGE_FORMAT_JPEG_XL},
        {BUSTER_ARRAY_TO_BYTE_SLICE(jpeg_2000_codestream), IMAGE_FORMAT_JPEG_2000},
        {BUSTER_ARRAY_TO_BYTE_SLICE(jp2), IMAGE_FORMAT_JPEG_2000},
        {BUSTER_ARRAY_TO_BYTE_SLICE(psd), IMAGE_FORMAT_PSD},
        {BUSTER_ARRAY_TO_BYTE_SLICE(openexr), IMAGE_FORMAT_OPENEXR},
        {BUSTER_ARRAY_TO_BYTE_SLICE(hdr), IMAGE_FORMAT_HDR},
        {BUSTER_ARRAY_TO_BYTE_SLICE(rgbe), IMAGE_FORMAT_HDR},
        {BUSTER_ARRAY_TO_BYTE_SLICE(dds), IMAGE_FORMAT_DDS},
        {BUSTER_ARRAY_TO_BYTE_SLICE(ktx1), IMAGE_FORMAT_KTX1},
        {BUSTER_ARRAY_TO_BYTE_SLICE(ktx2), IMAGE_FORMAT_KTX2},
        {BUSTER_ARRAY_TO_BYTE_SLICE(pfm_rgb), IMAGE_FORMAT_PFM},
        {BUSTER_ARRAY_TO_BYTE_SLICE(pfm_grayscale), IMAGE_FORMAT_PFM},
        {BUSTER_ARRAY_TO_BYTE_SLICE(tga), IMAGE_FORMAT_TGA},
    };
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(cases); index += 1)
    {
        ImageFormat detected = image_detect_format(cases[index].bytes);
        if (detected != cases[index].format)
        {
            arguments->show(arguments, S8("IMAGE_DETECTION_CASE index={u32} expected={u32} actual={u32}\n"), index,
                            (u32)cases[index].format, (u32)detected);
        }
        BUSTER_TEST(arguments, detected == cases[index].format);
    }
    BUSTER_TEST(arguments, image_detect_format((ByteSlice){0}) == IMAGE_FORMAT_UNKNOWN);
    BUSTER_TEST(arguments, image_detect_format((ByteSlice){.pointer = 0, .length = 1}) == IMAGE_FORMAT_UNKNOWN);
    BUSTER_TEST(arguments, image_detect_format(BUSTER_ARRAY_TO_BYTE_SLICE(truncated_tga)) == IMAGE_FORMAT_UNKNOWN);

    // ICO/CUR recognition validates every resource range, including nonzero
    // sizes and offsets beyond the complete directory rather than just the
    // directory byte count.
    u8 ico_zero_size[sizeof(ico)];
    memcpy(ico_zero_size, ico, sizeof(ico));
    memset(ico_zero_size + 14, 0, 4);
    BUSTER_TEST(arguments, image_detect_format(BUSTER_ARRAY_TO_BYTE_SLICE(ico_zero_size)) == IMAGE_FORMAT_UNKNOWN);
    u8 ico_directory_offset[sizeof(ico)];
    memcpy(ico_directory_offset, ico, sizeof(ico));
    memset(ico_directory_offset + 18, 0, 4);
    BUSTER_TEST(arguments, image_detect_format(BUSTER_ARRAY_TO_BYTE_SLICE(ico_directory_offset)) == IMAGE_FORMAT_UNKNOWN);
    u8 ico_out_of_range[sizeof(ico)];
    memcpy(ico_out_of_range, ico, sizeof(ico));
    ico_out_of_range[18] = (u8)sizeof(ico);
    BUSTER_TEST(arguments, image_detect_format(BUSTER_ARRAY_TO_BYTE_SLICE(ico_out_of_range)) == IMAGE_FORMAT_UNKNOWN);
    u8 ico_two_entries[] = {
        0, 0, 1, 0, 2, 0,
        1, 1, 0, 0, 1, 0, 32, 0, 1, 0, 0, 0, 38, 0, 0, 0,
        1, 1, 0, 0, 1, 0, 32, 0, 1, 0, 0, 0, 40, 0, 0, 0,
        0, 0,
    };
    BUSTER_TEST(arguments, image_detect_format(BUSTER_ARRAY_TO_BYTE_SLICE(ico_two_entries)) == IMAGE_FORMAT_UNKNOWN);

    u8 pfm_missing_line_end[] = {'P', 'F'};
    u8 pfm_space_separator[] = {'P', 'F', ' '};
    u8 pfm_bad_line_end[] = {'P', 'f', '\r', 'x'};
    BUSTER_TEST(arguments, image_detect_format(BUSTER_ARRAY_TO_BYTE_SLICE(pfm_missing_line_end)) == IMAGE_FORMAT_UNKNOWN);
    BUSTER_TEST(arguments, image_detect_format(BUSTER_ARRAY_TO_BYTE_SLICE(pfm_space_separator)) == IMAGE_FORMAT_UNKNOWN);
    BUSTER_TEST(arguments, image_detect_format(BUSTER_ARRAY_TO_BYTE_SLICE(pfm_bad_line_end)) == IMAGE_FORMAT_UNKNOWN);

    // Detection examines at most 256 compatible brands. A recognized brand
    // immediately beyond that cap cannot turn an otherwise unknown file into
    // AVIF and makes the hostile-list work bound observable.
    u8 avif_beyond_scan[1044] = {0};
    avif_beyond_scan[2] = 4;
    avif_beyond_scan[3] = 0x14;
    memcpy(avif_beyond_scan + 4, "ftyp", 4);
    memcpy(avif_beyond_scan + 1040, "avif", 4);
    BUSTER_TEST(arguments, image_detect_format(BUSTER_ARRAY_TO_BYTE_SLICE(avif_beyond_scan)) == IMAGE_FORMAT_UNKNOWN);

    u64 position = arguments->arena->position;
    ImageFormat unsupported_formats[] = {
        IMAGE_FORMAT_WEBP, IMAGE_FORMAT_TIFF, IMAGE_FORMAT_ICO, IMAGE_FORMAT_AVIF, IMAGE_FORMAT_HEIF,
        IMAGE_FORMAT_BIG_TIFF, IMAGE_FORMAT_JPEG_XL, IMAGE_FORMAT_JPEG_2000, IMAGE_FORMAT_PSD, IMAGE_FORMAT_OPENEXR,
        IMAGE_FORMAT_HDR, IMAGE_FORMAT_DDS, IMAGE_FORMAT_KTX1, IMAGE_FORMAT_KTX2, IMAGE_FORMAT_PFM,
    };
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(unsupported_formats); index += 1)
    {
        ImageDecodeResult decoded = image_decode(arguments->arena, (ByteSlice){0},
                                                 (ImageDecodeOptions){.format_hint = unsupported_formats[index]});
        BUSTER_TEST(arguments, decoded.status == IMAGE_DECODE_UNSUPPORTED_FORMAT && !decoded.image.pixels.pointer &&
                                   decoded.unsupported_feature == IMAGE_UNSUPPORTED_FEATURE_NONE &&
                                   decoded.exceeded_limit == IMAGE_EXCEEDED_LIMIT_NONE && !decoded.observed_value &&
                                   !decoded.limit_value && arguments->arena->position == position);
    }

    ImageDecodeResult invalid = image_decode(0, BUSTER_ARRAY_TO_BYTE_SLICE(png), (ImageDecodeOptions){0});
    BUSTER_TEST(arguments, invalid.status == IMAGE_DECODE_INVALID_ARGUMENT);
    ImageProbeResult unrecognized = image_probe((ByteSlice){0}, (ImageDecodeOptions){0});
    BUSTER_TEST(arguments, unrecognized.status == IMAGE_DECODE_UNRECOGNIZED_FORMAT);

    // Exif orientation is metadata, not an implicit decode transform. The
    // explicit helper copies a non-square RGBA image into each display order.
    u8 orientation_pixels[] = {
        1, 0, 0, 255, 2, 0, 0, 255,
        3, 0, 0, 255, 4, 0, 0, 255,
        5, 0, 0, 255, 6, 0, 0, 255,
    };
    u8 orientation_expected[][6] = {
        {1, 2, 3, 4, 5, 6}, {2, 1, 4, 3, 6, 5}, {6, 5, 4, 3, 2, 1}, {5, 6, 3, 4, 1, 2},
        {1, 3, 5, 2, 4, 6}, {5, 3, 1, 6, 4, 2}, {6, 4, 2, 5, 3, 1}, {2, 4, 6, 1, 3, 5},
    };
    Image orientation_source = {
        .pixels = BUSTER_ARRAY_TO_BYTE_SLICE(orientation_pixels),
        .width = 2,
        .height = 3,
        .stride = 8,
    };
    for (u32 orientation_index = 0; orientation_index < BUSTER_ARRAY_LENGTH(orientation_expected); orientation_index += 1)
    {
        u64 orientation_position = arguments->arena->position;
        ImageOrientation orientation = (ImageOrientation)(orientation_index + 1u);
        ImageTransformResult transformed = image_apply_orientation(arguments->arena, orientation_source, orientation);
        u32 expected_width = orientation >= IMAGE_ORIENTATION_LEFT_TOP ? 3u : 2u;
        u32 expected_height = orientation >= IMAGE_ORIENTATION_LEFT_TOP ? 2u : 3u;
        BUSTER_REQUIRE(arguments, transformed.status == IMAGE_DECODE_SUCCESS);
        BUSTER_TEST(arguments, transformed.image.width == expected_width && transformed.image.height == expected_height &&
                               transformed.image.stride == expected_width * 4u);
        for (u32 pixel_index = 0; pixel_index < 6; pixel_index += 1)
        {
            u8 const* pixel = transformed.image.pixels.pointer + (u64)pixel_index * 4u;
            BUSTER_TEST(arguments, pixel[0] == orientation_expected[orientation_index][pixel_index] &&
                                   pixel[1] == 0 && pixel[2] == 0 && pixel[3] == 255);
        }
        arena_set_position(arguments->arena, orientation_position);
    }

    // Each complete fixture must take the same signature-selected codec in
    // probe and decode, publish identical metadata, and return exact RGBA8.
    for (u32 fixture_index = 0; fixture_index < BUSTER_ARRAY_LENGTH(image_test_fixtures); fixture_index += 1)
    {
        ImageTestFixture const* fixture = &image_test_fixtures[fixture_index];
        ByteSlice encoded = image_test_encoded(fixture, fixture->encoded_size);
        BUSTER_TEST(arguments, image_detect_format(encoded) == fixture->format);

        ImageProbeResult probe = image_probe(encoded, (ImageDecodeOptions){0});
        if (BUSTER_REQUIRE(arguments, probe.status == IMAGE_DECODE_SUCCESS))
        {
            BUSTER_TEST(arguments, image_test_information_matches_fixture(probe.information, fixture));
        }

        u64 fixture_position = arguments->arena->position;
        ImageDecodeResult decoded = image_decode(arguments->arena, encoded, (ImageDecodeOptions){0});
        if (BUSTER_REQUIRE(arguments, decoded.status == IMAGE_DECODE_SUCCESS))
        {
            BUSTER_TEST(arguments, image_test_information_matches_fixture(decoded.information, fixture));
            BUSTER_TEST(arguments, image_test_information_equal(decoded.information, probe.information));
            BUSTER_TEST(arguments, image_test_pixels_match(decoded.image, fixture));
        }
        arena_set_position(arguments->arena, fixture_position);
    }

    // A second compact wave covers representative format features without
    // multiplying the exhaustive prefix and limit matrices below.
    for (u32 fixture_index = 0; fixture_index < BUSTER_ARRAY_LENGTH(image_test_advanced_fixtures); fixture_index += 1)
    {
        ImageTestFixture const* fixture = &image_test_advanced_fixtures[fixture_index];
        ByteSlice encoded = image_test_encoded(fixture, fixture->encoded_size);
        BUSTER_TEST(arguments, image_detect_format(encoded) == fixture->format);

        ImageProbeResult probe = image_probe(encoded, (ImageDecodeOptions){0});
        if (BUSTER_REQUIRE(arguments, probe.status == IMAGE_DECODE_SUCCESS))
        {
            BUSTER_TEST(arguments, image_test_information_matches_fixture(probe.information, fixture));
        }

        u64 fixture_position = arguments->arena->position;
        ImageDecodeResult decoded = image_decode(arguments->arena, encoded, (ImageDecodeOptions){0});
        if (BUSTER_REQUIRE(arguments, decoded.status == IMAGE_DECODE_SUCCESS))
        {
            BUSTER_TEST(arguments, image_test_information_matches_fixture(decoded.information, fixture));
            BUSTER_TEST(arguments, image_test_information_equal(decoded.information, probe.information));
            BUSTER_TEST(arguments, image_test_pixels_match(decoded.image, fixture));
        }
        arena_set_position(arguments->arena, fixture_position);
    }

    // BMP metadata distinguishes encoded palette/RGB/RGBA samples from alpha
    // introduced by transparent RLE gaps. Unsupported header profiles,
    // precisions and coding modes also retain their public detail category.
    ByteSlice rgba_bmp = BUSTER_ARRAY_TO_BYTE_SLICE(image_test_bmp_rgba);
    ImageProbeResult rgba_bmp_probe = image_probe(rgba_bmp, (ImageDecodeOptions){.format_hint = IMAGE_FORMAT_BMP});
    BUSTER_TEST(arguments, rgba_bmp_probe.status == IMAGE_DECODE_SUCCESS &&
                           rgba_bmp_probe.information.source_color_model == IMAGE_COLOR_MODEL_RGBA &&
                           rgba_bmp_probe.information.source_channel_count == 4 && rgba_bmp_probe.information.has_alpha);
    u64 rgba_bmp_position = arguments->arena->position;
    ImageDecodeResult rgba_bmp_decoded = image_decode(arguments->arena, rgba_bmp,
                                                      (ImageDecodeOptions){.format_hint = IMAGE_FORMAT_BMP});
    if (BUSTER_REQUIRE(arguments, rgba_bmp_decoded.status == IMAGE_DECODE_SUCCESS))
    {
        u8 rgba_bmp_pixel[] = {1, 2, 3, 128};
        BUSTER_TEST(arguments, rgba_bmp_decoded.information.source_color_model == IMAGE_COLOR_MODEL_RGBA &&
                               rgba_bmp_decoded.information.has_alpha &&
                               rgba_bmp_decoded.image.pixels.length == sizeof(rgba_bmp_pixel) &&
                               !memcmp(rgba_bmp_decoded.image.pixels.pointer, rgba_bmp_pixel, sizeof(rgba_bmp_pixel)));
    }
    arena_set_position(arguments->arena, rgba_bmp_position);

    // Accepted bitfields may assign more precision to alpha than to any color
    // channel. source_bits_per_channel reports the greatest stored channel
    // precision, not only the greatest RGB mask width.
    u8 wide_alpha_bmp[sizeof(image_test_bmp_rgba)];
    u8 wide_alpha_masks[] = {
        0x0f, 0x00, 0x00, 0x00,
        0xf0, 0x00, 0x00, 0x00,
        0x00, 0x0f, 0x00, 0x00,
        0x00, 0xf0, 0xff, 0xff,
    };
    memcpy(wide_alpha_bmp, image_test_bmp_rgba, sizeof(wide_alpha_bmp));
    memcpy(wide_alpha_bmp + 54, wide_alpha_masks, sizeof(wide_alpha_masks));
    ByteSlice wide_alpha_bmp_bytes = BUSTER_ARRAY_TO_BYTE_SLICE(wide_alpha_bmp);
    ImageProbeResult wide_alpha_bmp_probe = image_probe(wide_alpha_bmp_bytes,
                                                        (ImageDecodeOptions){.format_hint = IMAGE_FORMAT_BMP});
    BUSTER_TEST(arguments, wide_alpha_bmp_probe.status == IMAGE_DECODE_SUCCESS &&
                           wide_alpha_bmp_probe.information.source_channel_count == 4 &&
                           wide_alpha_bmp_probe.information.source_bits_per_channel == 20 &&
                           wide_alpha_bmp_probe.information.has_alpha);
    rgba_bmp_position = arguments->arena->position;
    ImageDecodeResult wide_alpha_bmp_decoded = image_decode(arguments->arena, wide_alpha_bmp_bytes,
                                                            (ImageDecodeOptions){.format_hint = IMAGE_FORMAT_BMP});
    BUSTER_TEST(arguments, wide_alpha_bmp_decoded.status == IMAGE_DECODE_SUCCESS &&
                           wide_alpha_bmp_decoded.information.source_channel_count == 4 &&
                           wide_alpha_bmp_decoded.information.source_bits_per_channel == 20 &&
                           wide_alpha_bmp_decoded.information.has_alpha);
    arena_set_position(arguments->arena, rgba_bmp_position);

    u8 bmp_unsupported_profile[sizeof(image_test_bmp)];
    u8 bmp_unsupported_precision[sizeof(image_test_bmp)];
    u8 bmp_unsupported_coding[sizeof(image_test_bmp)];
    memcpy(bmp_unsupported_profile, image_test_bmp, sizeof(image_test_bmp));
    memcpy(bmp_unsupported_precision, image_test_bmp, sizeof(image_test_bmp));
    memcpy(bmp_unsupported_coding, image_test_bmp, sizeof(image_test_bmp));
    bmp_unsupported_profile[14] = 41;
    bmp_unsupported_precision[28] = 2;
    bmp_unsupported_coding[30] = 4;
    ByteSlice unsupported_bmps[] = {
        BUSTER_ARRAY_TO_BYTE_SLICE(bmp_unsupported_profile),
        BUSTER_ARRAY_TO_BYTE_SLICE(bmp_unsupported_precision),
        BUSTER_ARRAY_TO_BYTE_SLICE(bmp_unsupported_coding),
    };
    ImageUnsupportedFeature bmp_features[] = {
        IMAGE_UNSUPPORTED_FEATURE_PROFILE,
        IMAGE_UNSUPPORTED_FEATURE_PRECISION,
        IMAGE_UNSUPPORTED_FEATURE_CODING,
    };
    for (u32 bmp_index = 0; bmp_index < BUSTER_ARRAY_LENGTH(unsupported_bmps); bmp_index += 1)
    {
        ImageDecodeOptions bmp_options = {.format_hint = IMAGE_FORMAT_BMP};
        ImageProbeResult bmp_probe = image_probe(unsupported_bmps[bmp_index], bmp_options);
        BUSTER_TEST(arguments, bmp_probe.status == IMAGE_DECODE_UNSUPPORTED_FEATURE &&
                               bmp_probe.unsupported_feature == bmp_features[bmp_index]);
        u64 bmp_position = arguments->arena->position;
        ImageDecodeResult bmp_decoded = image_decode(arguments->arena, unsupported_bmps[bmp_index], bmp_options);
        BUSTER_TEST(arguments, bmp_decoded.status == IMAGE_DECODE_UNSUPPORTED_FEATURE &&
                               bmp_decoded.unsupported_feature == bmp_features[bmp_index] &&
                               image_test_image_empty(bmp_decoded.image) && arguments->arena->position == bmp_position);
    }

    // Both BITMAPFILEHEADER reserved words are required to be zero.
    u8 bmp_reserved[sizeof(image_test_bmp)];
    for (u32 reserved_index = 0; reserved_index < 2; reserved_index += 1)
    {
        memcpy(bmp_reserved, image_test_bmp, sizeof(bmp_reserved));
        u64 reserved_offset = 6u + reserved_index * 2u;
        bmp_reserved[reserved_offset] = 1;
        ByteSlice bmp_reserved_bytes = BUSTER_ARRAY_TO_BYTE_SLICE(bmp_reserved);
        ImageDecodeOptions bmp_reserved_options = {.format_hint = IMAGE_FORMAT_BMP};
        ImageProbeResult bmp_reserved_probe = image_probe(bmp_reserved_bytes, bmp_reserved_options);
        BUSTER_TEST(arguments, bmp_reserved_probe.status == IMAGE_DECODE_MALFORMED &&
                               bmp_reserved_probe.error_offset == reserved_offset);
        BUSTER_TEST(arguments, image_test_rejected_without_allocation(arguments->arena, bmp_reserved_bytes,
                                                                      bmp_reserved_options, IMAGE_DECODE_MALFORMED));
    }

    // A nonzero BMP file size is the logical end of the file even when the
    // caller's ByteSlice contains additional bytes and biSizeImage is omitted.
    // The first RLE packet fits, but the following packet must not read past it.
    u8 bmp_short_rle[sizeof(image_test_bmp_rle8)];
    memcpy(bmp_short_rle, image_test_bmp_rle8, sizeof(bmp_short_rle));
    bmp_short_rle[2] = 72;
    bmp_short_rle[3] = 0;
    bmp_short_rle[4] = 0;
    bmp_short_rle[5] = 0;
    bmp_short_rle[34] = 0;
    bmp_short_rle[35] = 0;
    bmp_short_rle[36] = 0;
    bmp_short_rle[37] = 0;
    ByteSlice bmp_short_rle_bytes = BUSTER_ARRAY_TO_BYTE_SLICE(bmp_short_rle);
    ImageDecodeOptions bmp_short_rle_options = {.format_hint = IMAGE_FORMAT_BMP};
    ImageProbeResult bmp_short_rle_probe = image_probe(bmp_short_rle_bytes, bmp_short_rle_options);
    BUSTER_TEST(arguments, bmp_short_rle_probe.status == IMAGE_DECODE_SUCCESS);
    u64 bmp_short_rle_position = arguments->arena->position;
    ImageDecodeResult bmp_short_rle_decode = image_decode(arguments->arena, bmp_short_rle_bytes, bmp_short_rle_options);
    BUSTER_TEST(arguments, bmp_short_rle_decode.status == IMAGE_DECODE_TRUNCATED &&
                           bmp_short_rle_decode.error_offset == 72 && image_test_image_empty(bmp_short_rle_decode.image) &&
                           arguments->arena->position == bmp_short_rle_position);

    // A nonzero biSizeImage independently bounds the compressed raster. Bytes
    // after its declared end remain part of the file but cannot supply packets.
    u8 bmp_short_image_rle[sizeof(image_test_bmp_rle8)];
    memcpy(bmp_short_image_rle, image_test_bmp_rle8, sizeof(bmp_short_image_rle));
    bmp_short_image_rle[34] = 2;
    bmp_short_image_rle[35] = 0;
    bmp_short_image_rle[36] = 0;
    bmp_short_image_rle[37] = 0;
    ByteSlice bmp_short_image_rle_bytes = BUSTER_ARRAY_TO_BYTE_SLICE(bmp_short_image_rle);
    ImageProbeResult bmp_short_image_rle_probe = image_probe(bmp_short_image_rle_bytes, bmp_short_rle_options);
    BUSTER_TEST(arguments, bmp_short_image_rle_probe.status == IMAGE_DECODE_SUCCESS);
    bmp_short_rle_position = arguments->arena->position;
    ImageDecodeResult bmp_short_image_rle_decode = image_decode(arguments->arena, bmp_short_image_rle_bytes,
                                                                bmp_short_rle_options);
    BUSTER_TEST(arguments, bmp_short_image_rle_decode.status == IMAGE_DECODE_TRUNCATED &&
                           bmp_short_image_rle_decode.error_offset == 72 &&
                           image_test_image_empty(bmp_short_image_rle_decode.image) &&
                           arguments->arena->position == bmp_short_rle_position);

    // A physically unavailable declared raster is truncated. A raster extent
    // available in the ByteSlice but extending past bfSize is internally
    // contradictory and therefore malformed.
    u8 bmp_oversized_image_rle[sizeof(image_test_bmp_rle8)];
    memcpy(bmp_oversized_image_rle, image_test_bmp_rle8, sizeof(bmp_oversized_image_rle));
    bmp_oversized_image_rle[34] = 19;
    ByteSlice bmp_oversized_image_rle_bytes = BUSTER_ARRAY_TO_BYTE_SLICE(bmp_oversized_image_rle);
    ImageProbeResult bmp_oversized_image_rle_probe = image_probe(bmp_oversized_image_rle_bytes, bmp_short_rle_options);
    BUSTER_TEST(arguments, bmp_oversized_image_rle_probe.status == IMAGE_DECODE_TRUNCATED &&
                           bmp_oversized_image_rle_probe.error_offset == sizeof(bmp_oversized_image_rle));
    BUSTER_TEST(arguments, image_test_rejected_without_allocation(arguments->arena, bmp_oversized_image_rle_bytes,
                                                                  bmp_short_rle_options, IMAGE_DECODE_TRUNCATED));

    u8 bmp_inconsistent_image_rle[sizeof(image_test_bmp_rle8)];
    memcpy(bmp_inconsistent_image_rle, image_test_bmp_rle8, sizeof(bmp_inconsistent_image_rle));
    bmp_inconsistent_image_rle[2] = 72;
    bmp_inconsistent_image_rle[3] = 0;
    bmp_inconsistent_image_rle[4] = 0;
    bmp_inconsistent_image_rle[5] = 0;
    ByteSlice bmp_inconsistent_image_rle_bytes = BUSTER_ARRAY_TO_BYTE_SLICE(bmp_inconsistent_image_rle);
    ImageProbeResult bmp_inconsistent_image_rle_probe = image_probe(bmp_inconsistent_image_rle_bytes,
                                                                    bmp_short_rle_options);
    BUSTER_TEST(arguments, bmp_inconsistent_image_rle_probe.status == IMAGE_DECODE_MALFORMED &&
                           bmp_inconsistent_image_rle_probe.error_offset == 34);
    BUSTER_TEST(arguments, image_test_rejected_without_allocation(arguments->arena, bmp_inconsistent_image_rle_bytes,
                                                                  bmp_short_rle_options, IMAGE_DECODE_MALFORMED));

    // SOF1 is the extended sequential marker; at eight-bit precision its
    // Huffman scan is identical to SOF0. Higher precision remains unsupported.
    u64 jpeg_frame_offset = UINT64_MAX;
    for (u64 offset = 0; offset + 4u < sizeof(image_test_jpeg) && jpeg_frame_offset == UINT64_MAX; offset += 1)
    {
        if (image_test_jpeg[offset] == 0xffu && image_test_jpeg[offset + 1u] == 0xc0u)
        {
            jpeg_frame_offset = offset;
        }
    }
    if (BUSTER_REQUIRE(arguments, jpeg_frame_offset != UINT64_MAX))
    {
        u8 jpeg_sof1[sizeof(image_test_jpeg)];
        memcpy(jpeg_sof1, image_test_jpeg, sizeof(jpeg_sof1));
        jpeg_sof1[jpeg_frame_offset + 1u] = 0xc1u;
        ByteSlice sof1 = BUSTER_ARRAY_TO_BYTE_SLICE(jpeg_sof1);
        ImageProbeResult sof1_probe = image_probe(sof1, (ImageDecodeOptions){.format_hint = IMAGE_FORMAT_JPEG});
        BUSTER_TEST(arguments, sof1_probe.status == IMAGE_DECODE_SUCCESS);
        BUSTER_TEST(arguments, image_test_information_matches_fixture(sof1_probe.information, &image_test_fixtures[3]));

        u64 sof1_position = arguments->arena->position;
        ImageDecodeResult sof1_decoded = image_decode(arguments->arena, sof1, (ImageDecodeOptions){.format_hint = IMAGE_FORMAT_JPEG});
        if (BUSTER_REQUIRE(arguments, sof1_decoded.status == IMAGE_DECODE_SUCCESS))
        {
            BUSTER_TEST(arguments, image_test_information_matches_fixture(sof1_decoded.information, &image_test_fixtures[3]));
            BUSTER_TEST(arguments, image_test_pixels_match(sof1_decoded.image, &image_test_fixtures[3]));
        }
        arena_set_position(arguments->arena, sof1_position);

        jpeg_sof1[jpeg_frame_offset + 4u] = 12u;
        ByteSlice sof1_12_bit = BUSTER_ARRAY_TO_BYTE_SLICE(jpeg_sof1);
        ImageDecodeOptions jpeg_options = {.format_hint = IMAGE_FORMAT_JPEG};
        ImageProbeResult sof1_12_bit_probe = image_probe(sof1_12_bit, jpeg_options);
        BUSTER_TEST(arguments, sof1_12_bit_probe.status == IMAGE_DECODE_UNSUPPORTED_FEATURE &&
                               sof1_12_bit_probe.unsupported_feature == IMAGE_UNSUPPORTED_FEATURE_PRECISION);
        BUSTER_TEST(arguments, image_test_rejected_without_allocation(arguments->arena, sof1_12_bit, jpeg_options,
                                                                       IMAGE_DECODE_UNSUPPORTED_FEATURE));
    }

    // Entropy-coded segments are padded to a byte boundary with one bits.
    // Validate both the end-of-scan boundary and an in-scan restart boundary.
    ImageDecodeOptions padding_options = {.format_hint = IMAGE_FORMAT_JPEG};
    u64 restart_position = arguments->arena->position;
    ImageDecodeResult restart_decoded = image_decode(arguments->arena, BUSTER_ARRAY_TO_BYTE_SLICE(image_test_jpeg_restart), padding_options);
    BUSTER_TEST(arguments, restart_decoded.status == IMAGE_DECODE_SUCCESS);
    arena_set_position(arguments->arena, restart_position);

    u8 invalid_scan_padding[sizeof(image_test_jpeg)];
    memcpy(invalid_scan_padding, image_test_jpeg, sizeof(invalid_scan_padding));
    u64 scan_padding_offset = sizeof(invalid_scan_padding) - 3u;
    invalid_scan_padding[scan_padding_offset] &= 0xfeu;
    u64 padding_position = arguments->arena->position;
    ImageDecodeResult invalid_scan_result = image_decode(arguments->arena, BUSTER_ARRAY_TO_BYTE_SLICE(invalid_scan_padding), padding_options);
    BUSTER_TEST(arguments, invalid_scan_result.status == IMAGE_DECODE_MALFORMED &&
                           invalid_scan_result.error_offset == scan_padding_offset && image_test_image_empty(invalid_scan_result.image) &&
                           arguments->arena->position == padding_position);

    u8 invalid_restart_padding[sizeof(image_test_jpeg_restart)];
    memcpy(invalid_restart_padding, image_test_jpeg_restart, sizeof(invalid_restart_padding));
    invalid_restart_padding[IMAGE_TEST_JPEG_RESTART_PADDING_OFFSET] &= 0xfeu;
    ImageDecodeResult invalid_restart_result = image_decode(arguments->arena, BUSTER_ARRAY_TO_BYTE_SLICE(invalid_restart_padding), padding_options);
    BUSTER_TEST(arguments, invalid_restart_result.status == IMAGE_DECODE_MALFORMED &&
                           invalid_restart_result.error_offset == IMAGE_TEST_JPEG_RESTART_PADDING_OFFSET &&
                           image_test_image_empty(invalid_restart_result.image) && arguments->arena->position == padding_position);

    // Structurally valid sampling factors that imply a fractional expansion
    // are a profile the decoder does not implement, not malformed JPEG syntax.
    u8 jpeg_fractional_sampling[] = {
        0xff, 0xd8, 0xff, 0xc0, 0x00, 0x11, 0x08, 0x00, 0x08, 0x00, 0x08,
        0x03, 0x01, 0x31, 0x00, 0x02, 0x21, 0x00, 0x03, 0x11, 0x00,
    };
    ByteSlice fractional_sampling = BUSTER_ARRAY_TO_BYTE_SLICE(jpeg_fractional_sampling);
    ImageDecodeOptions jpeg_options = {.format_hint = IMAGE_FORMAT_JPEG};
    ImageProbeResult fractional_sampling_probe = image_probe(fractional_sampling, jpeg_options);
    BUSTER_TEST(arguments, fractional_sampling_probe.status == IMAGE_DECODE_UNSUPPORTED_FEATURE &&
                           fractional_sampling_probe.unsupported_feature == IMAGE_UNSUPPORTED_FEATURE_PROFILE);
    BUSTER_TEST(arguments, image_test_rejected_without_allocation(arguments->arena, fractional_sampling, jpeg_options,
                                                                   IMAGE_DECODE_UNSUPPORTED_FEATURE));

    // JPEG decoding reports Exif orientation without rotating stored pixels;
    // image_apply_orientation remains the explicit display-order operation.
    u8 jpeg_exif_orientation[] = {
        0xff, 0xd8, 0xff, 0xe1, 0x00, 0x1e,
        'E', 'x', 'i', 'f', 0x00, 0x00, 'I', 'I', 0x2a, 0x00, 0x08, 0x00, 0x00, 0x00,
        0x01, 0x00, 0x12, 0x01, 0x03, 0x00, 0x01, 0x00, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00,
        0xff, 0xc0, 0x00, 0x0b, 0x08, 0x00, 0x08, 0x00, 0x04, 0x01, 0x01, 0x11, 0x00,
        0xff, 0xda, 0x00, 0x08, 0x01, 0x01, 0x00, 0x00, 0x3f, 0x00,
        0xff, 0xd9,
    };
    ImageProbeResult orientation_probe = image_probe(BUSTER_ARRAY_TO_BYTE_SLICE(jpeg_exif_orientation), jpeg_options);
    BUSTER_TEST(arguments, orientation_probe.status == IMAGE_DECODE_SUCCESS && orientation_probe.information.width == 4u &&
                               orientation_probe.information.height == 8u &&
                               orientation_probe.information.orientation == IMAGE_ORIENTATION_RIGHT_TOP &&
                               orientation_probe.information.source_color_model == IMAGE_COLOR_MODEL_GRAYSCALE &&
                               orientation_probe.information.color_metadata_flags == IMAGE_COLOR_METADATA_EXIF);

    ByteSlice truncated_orientation = {
        .pointer = jpeg_exif_orientation,
        .length = sizeof(jpeg_exif_orientation) - 2u,
    };
    ImageProbeResult truncated_orientation_probe = image_probe(truncated_orientation, jpeg_options);
    BUSTER_TEST(arguments, truncated_orientation_probe.status == IMAGE_DECODE_TRUNCATED &&
                           truncated_orientation_probe.error_offset == truncated_orientation.length);

    // Probe traverses stuffed bytes and restart markers without interpreting
    // entropy, so marker metadata after a scan remains visible. The complete
    // three-scan fixture also verifies that decode retains the same metadata.
    u8 probe_envelope_tail[] = {0xff, 0x00, 0xff, 0xff, 0xd0, 0xff, 0xff, 0xd9};
    u8 jpeg_probe_envelope[sizeof(jpeg_exif_orientation) - 2u + sizeof(probe_envelope_tail)];
    memcpy(jpeg_probe_envelope, jpeg_exif_orientation, sizeof(jpeg_exif_orientation) - 2u);
    memcpy(jpeg_probe_envelope + sizeof(jpeg_exif_orientation) - 2u, probe_envelope_tail, sizeof(probe_envelope_tail));
    ImageProbeResult envelope_probe = image_probe(BUSTER_ARRAY_TO_BYTE_SLICE(jpeg_probe_envelope), jpeg_options);
    BUSTER_TEST(arguments, envelope_probe.status == IMAGE_DECODE_SUCCESS &&
                           envelope_probe.information.orientation == IMAGE_ORIENTATION_RIGHT_TOP &&
                           envelope_probe.information.color_metadata_flags == IMAGE_COLOR_METADATA_EXIF);

    u8 invalid_fill_tail[] = {0xff, 0xff, 0x00, 0xff, 0xd9};
    u8 jpeg_invalid_fill[sizeof(jpeg_exif_orientation) - 2u + sizeof(invalid_fill_tail)];
    memcpy(jpeg_invalid_fill, jpeg_exif_orientation, sizeof(jpeg_exif_orientation) - 2u);
    memcpy(jpeg_invalid_fill + sizeof(jpeg_exif_orientation) - 2u, invalid_fill_tail, sizeof(invalid_fill_tail));
    ImageProbeResult invalid_fill_probe = image_probe(BUSTER_ARRAY_TO_BYTE_SLICE(jpeg_invalid_fill), jpeg_options);
    BUSTER_TEST(arguments, invalid_fill_probe.status == IMAGE_DECODE_MALFORMED &&
                           invalid_fill_probe.error_offset == sizeof(jpeg_exif_orientation) - 1u);

    u8 jpeg_post_scan_exif_segment[] = {
        0xff, 0xe1, 0x00, 0x22, 'E', 'x', 'i', 'f', 0, 0,
        'M', 'M', 0, 0x2a, 0, 0, 0, 8,
        0, 1, 0x01, 0x12, 0, 3, 0, 0, 0, 1, 0, IMAGE_ORIENTATION_RIGHT_TOP, 0, 0,
        0, 0, 0, 0,
    };
    u8 jpeg_post_scan_exif[sizeof(image_test_jpeg_three_scans) + sizeof(jpeg_post_scan_exif_segment)];
    memcpy(jpeg_post_scan_exif, image_test_jpeg_three_scans, 141);
    memcpy(jpeg_post_scan_exif + 141, jpeg_post_scan_exif_segment, sizeof(jpeg_post_scan_exif_segment));
    memcpy(jpeg_post_scan_exif + 141 + sizeof(jpeg_post_scan_exif_segment), image_test_jpeg_three_scans + 141,
           sizeof(image_test_jpeg_three_scans) - 141u);
    ByteSlice jpeg_post_scan_exif_bytes = BUSTER_ARRAY_TO_BYTE_SLICE(jpeg_post_scan_exif);
    ImageProbeResult post_scan_probe = image_probe(jpeg_post_scan_exif_bytes, jpeg_options);
    BUSTER_TEST(arguments, post_scan_probe.status == IMAGE_DECODE_SUCCESS &&
                           post_scan_probe.information.orientation == IMAGE_ORIENTATION_RIGHT_TOP &&
                           post_scan_probe.information.color_metadata_flags == IMAGE_COLOR_METADATA_EXIF);
    u64 post_scan_position = arguments->arena->position;
    ImageDecodeResult post_scan_decode = image_decode(arguments->arena, jpeg_post_scan_exif_bytes, jpeg_options);
    u8 post_scan_expected[] = {128, 128, 128, 255};
    BUSTER_TEST(arguments, post_scan_decode.status == IMAGE_DECODE_SUCCESS &&
                           post_scan_decode.information.orientation == IMAGE_ORIENTATION_RIGHT_TOP &&
                           post_scan_decode.information.color_metadata_flags == IMAGE_COLOR_METADATA_EXIF &&
                           post_scan_decode.image.pixels.length == sizeof(post_scan_expected) &&
                           !memcmp(post_scan_decode.image.pixels.pointer, post_scan_expected, sizeof(post_scan_expected)));
    arena_set_position(arguments->arena, post_scan_position);

    // Three-component interpretation follows the marker contract used for
    // conversion: explicit component IDs select RGB without JFIF, while Adobe
    // transform 0/1 explicitly selects RGB/YCbCr.
    u8 jpeg_rgb_components[] = {
        0xff, 0xd8,
        0xff, 0xc0, 0x00, 0x11, 0x08, 0x00, 0x01, 0x00, 0x01, 0x03,
        'R', 0x11, 0x00, 'G', 0x11, 0x00, 'B', 0x11, 0x00,
        0xff, 0xda, 0x00, 0x0c, 0x03, 'R', 0x00, 'G', 0x00, 'B', 0x00, 0x00, 0x3f, 0x00,
        0xff, 0xd9,
    };
    ImageProbeResult rgb_probe = image_probe(BUSTER_ARRAY_TO_BYTE_SLICE(jpeg_rgb_components), jpeg_options);
    BUSTER_TEST(arguments, rgb_probe.status == IMAGE_DECODE_SUCCESS &&
                           rgb_probe.information.source_color_model == IMAGE_COLOR_MODEL_RGB &&
                           rgb_probe.information.color_metadata_flags == IMAGE_COLOR_METADATA_NONE);

    u8 jpeg_adobe_components[] = {
        0xff, 0xd8,
        0xff, 0xee, 0x00, 0x0e, 'A', 'd', 'o', 'b', 'e', 0x00, 0x64, 0x00, 0x00, 0x00, 0x00, 0x00,
        0xff, 0xc0, 0x00, 0x11, 0x08, 0x00, 0x01, 0x00, 0x01, 0x03,
        0x01, 0x11, 0x00, 0x02, 0x11, 0x00, 0x03, 0x11, 0x00,
        0xff, 0xda, 0x00, 0x0c, 0x03, 0x01, 0x00, 0x02, 0x00, 0x03, 0x00, 0x00, 0x3f, 0x00,
        0xff, 0xd9,
    };
    ImageProbeResult adobe_rgb_probe = image_probe(BUSTER_ARRAY_TO_BYTE_SLICE(jpeg_adobe_components), jpeg_options);
    BUSTER_TEST(arguments, adobe_rgb_probe.status == IMAGE_DECODE_SUCCESS &&
                           adobe_rgb_probe.information.source_color_model == IMAGE_COLOR_MODEL_RGB &&
                           adobe_rgb_probe.information.color_metadata_flags == IMAGE_COLOR_METADATA_ADOBE_TRANSFORM);
    jpeg_adobe_components[17] = 1u;
    ImageProbeResult adobe_ycbcr_probe = image_probe(BUSTER_ARRAY_TO_BYTE_SLICE(jpeg_adobe_components), jpeg_options);
    BUSTER_TEST(arguments, adobe_ycbcr_probe.status == IMAGE_DECODE_SUCCESS &&
                           adobe_ycbcr_probe.information.source_color_model == IMAGE_COLOR_MODEL_YCBCR &&
                           adobe_ycbcr_probe.information.color_metadata_flags == IMAGE_COLOR_METADATA_ADOBE_TRANSFORM);
    jpeg_adobe_components[17] = 2u;
    ImageProbeResult adobe_ycck_probe = image_probe(BUSTER_ARRAY_TO_BYTE_SLICE(jpeg_adobe_components), jpeg_options);
    BUSTER_TEST(arguments, adobe_ycck_probe.status == IMAGE_DECODE_UNSUPPORTED_FEATURE &&
                           adobe_ycck_probe.unsupported_feature == IMAGE_UNSUPPORTED_FEATURE_COLOR_TRANSFORM);

    // ICC APP2 is reported only for a complete, uniquely numbered profile
    // whose header size and signature match the marker payload.
    u8 jpeg_icc[177] = {0};
    u8 jpeg_icc_prefix[] = {
        0xff, 0xd8, 0xff, 0xe2, 0x00, 0x94,
        'I', 'C', 'C', '_', 'P', 'R', 'O', 'F', 'I', 'L', 'E', 0x00, 0x01, 0x01,
    };
    u8 jpeg_grayscale_tail[] = {
        0xff, 0xc0, 0x00, 0x0b, 0x08, 0x00, 0x01, 0x00, 0x01, 0x01, 0x01, 0x11, 0x00,
        0xff, 0xda, 0x00, 0x08, 0x01, 0x01, 0x00, 0x00, 0x3f, 0x00,
        0xff, 0xd9,
    };
    memcpy(jpeg_icc, jpeg_icc_prefix, sizeof(jpeg_icc_prefix));
    u8* icc_profile = jpeg_icc + sizeof(jpeg_icc_prefix);
    icc_profile[3] = 132u;
    memcpy(icc_profile + 36u, "acsp", 4);
    memcpy(icc_profile + 132u, jpeg_grayscale_tail, sizeof(jpeg_grayscale_tail));
    ImageProbeResult icc_probe = image_probe(BUSTER_ARRAY_TO_BYTE_SLICE(jpeg_icc), jpeg_options);
    BUSTER_TEST(arguments, icc_probe.status == IMAGE_DECODE_SUCCESS &&
                           icc_probe.information.source_color_model == IMAGE_COLOR_MODEL_GRAYSCALE &&
                           icc_probe.information.color_metadata_flags == IMAGE_COLOR_METADATA_ICC_PROFILE);
    icc_profile[36] = 'x';
    ImageProbeResult invalid_icc_probe = image_probe(BUSTER_ARRAY_TO_BYTE_SLICE(jpeg_icc), jpeg_options);
    BUSTER_TEST(arguments, invalid_icc_probe.status == IMAGE_DECODE_SUCCESS &&
                           invalid_icc_probe.information.color_metadata_flags == IMAGE_COLOR_METADATA_NONE);
    icc_profile[36] = 'a';
    u8 jpeg_with_icc[sizeof(image_test_jpeg) + 150u];
    memcpy(jpeg_with_icc, image_test_jpeg, 2);
    memcpy(jpeg_with_icc + 2u, jpeg_icc + 2u, 150u);
    memcpy(jpeg_with_icc + 152u, image_test_jpeg + 2u, sizeof(image_test_jpeg) - 2u);
    u64 jpeg_icc_position = arguments->arena->position;
    ImageDecodeResult icc_decoded = image_decode(arguments->arena, BUSTER_ARRAY_TO_BYTE_SLICE(jpeg_with_icc), jpeg_options);
    if (BUSTER_REQUIRE(arguments, icc_decoded.status == IMAGE_DECODE_SUCCESS))
    {
        BUSTER_TEST(arguments, icc_decoded.information.source_color_model == IMAGE_COLOR_MODEL_GRAYSCALE &&
                               icc_decoded.information.color_metadata_flags == IMAGE_COLOR_METADATA_ICC_PROFILE);
        BUSTER_TEST(arguments, image_test_pixels_match(icc_decoded.image, &image_test_fixtures[3]));
    }
    arena_set_position(arguments->arena, jpeg_icc_position);

    u8 jpeg_four_components[] = {
        0xff, 0xd8, 0xff, 0xc0, 0x00, 0x14, 0x08, 0x00, 0x01, 0x00, 0x01, 0x04,
        0x01, 0x11, 0x00, 0x02, 0x11, 0x00, 0x03, 0x11, 0x00, 0x04, 0x11, 0x00,
    };
    ImageProbeResult four_component_probe = image_probe(BUSTER_ARRAY_TO_BYTE_SLICE(jpeg_four_components), jpeg_options);
    BUSTER_TEST(arguments, four_component_probe.status == IMAGE_DECODE_UNSUPPORTED_FEATURE &&
                           four_component_probe.unsupported_feature == IMAGE_UNSUPPORTED_FEATURE_COMPONENT_MODEL);

    // Every strict proper prefix is rejected. A codec may allocate temporary
    // tables or output before discovering the cut, but image_decode must rewind
    // the caller's arena and clear the partial image transactionally.
    for (u32 fixture_index = 0; fixture_index < BUSTER_ARRAY_LENGTH(image_test_fixtures); fixture_index += 1)
    {
        ImageTestFixture const* fixture = &image_test_fixtures[fixture_index];
        ImageDecodeOptions options = {.format_hint = fixture->format};
        for (u64 length = 0; length < fixture->encoded_size; length += 1)
        {
            u64 fixture_position = arguments->arena->position;
            ImageDecodeResult decoded = image_decode(arguments->arena, image_test_encoded(fixture, length), options);
            BUSTER_TEST(arguments, decoded.status != IMAGE_DECODE_SUCCESS);
            BUSTER_TEST(arguments, image_test_image_empty(decoded.image));
            BUSTER_TEST(arguments, arguments->arena->position == fixture_position);
        }
    }

    // Metadata limits apply identically to allocation-free probes and full
    // decoding. All fixtures are at least 2 by 2, so each lowered nonzero limit
    // is distinct from the API's zero-means-default convention.
    for (u32 fixture_index = 0; fixture_index < BUSTER_ARRAY_LENGTH(image_test_fixtures); fixture_index += 1)
    {
        ImageTestFixture const* fixture = &image_test_fixtures[fixture_index];
        ByteSlice encoded = image_test_encoded(fixture, fixture->encoded_size);
        u64 pixel_count = (u64)fixture->width * fixture->height;
        ImageDecodeOptions limits[] = {
            {.format_hint = fixture->format, .max_width = fixture->width - 1},
            {.format_hint = fixture->format, .max_height = fixture->height - 1},
            {.format_hint = fixture->format, .max_pixels = pixel_count - 1},
            {.format_hint = fixture->format, .max_decoded_bytes = pixel_count * 4 - 1},
        };
        ImageExceededLimit exceeded_limits[] = {
            IMAGE_EXCEEDED_LIMIT_WIDTH,
            IMAGE_EXCEEDED_LIMIT_HEIGHT,
            IMAGE_EXCEEDED_LIMIT_PIXELS,
            IMAGE_EXCEEDED_LIMIT_DECODED_BYTES,
        };
        u64 observed_values[] = {fixture->width, fixture->height, pixel_count, pixel_count * 4};
        u64 limit_values[] = {fixture->width - 1, fixture->height - 1, pixel_count - 1, pixel_count * 4 - 1};
        for (u32 limit_index = 0; limit_index < BUSTER_ARRAY_LENGTH(limits); limit_index += 1)
        {
            ImageProbeResult probe = image_probe(encoded, limits[limit_index]);
            BUSTER_TEST(arguments, probe.status == IMAGE_DECODE_LIMIT_EXCEEDED);
            BUSTER_TEST(arguments, probe.exceeded_limit == exceeded_limits[limit_index] &&
                                   probe.observed_value == observed_values[limit_index] &&
                                   probe.limit_value == limit_values[limit_index] &&
                                   probe.unsupported_feature == IMAGE_UNSUPPORTED_FEATURE_NONE);
            u64 limit_position = arguments->arena->position;
            ImageDecodeResult decoded = image_decode(arguments->arena, encoded, limits[limit_index]);
            BUSTER_TEST(arguments, decoded.status == IMAGE_DECODE_LIMIT_EXCEEDED &&
                                   decoded.exceeded_limit == exceeded_limits[limit_index] &&
                                   decoded.observed_value == observed_values[limit_index] &&
                                   decoded.limit_value == limit_values[limit_index] &&
                                   decoded.unsupported_feature == IMAGE_UNSUPPORTED_FEATURE_NONE &&
                                   image_test_image_empty(decoded.image) && arguments->arena->position == limit_position);
        }
    }

    // Structural ceilings fail on the first record beyond the configured
    // count and publish that record's encoded offset. PNG probes do not walk
    // DEFLATE, while QOI probes walk every opcode so actual alpha and limits
    // match decode. JPEG probes traverse entropy envelopes to discover later
    // marker metadata without decoding entropy blocks. Current codec grammars
    // are iterative, so max_depth has no natural input-dependent nesting to
    // count.
    u8 split_block_png[128] = {0};
    memcpy(split_block_png, image_test_png_two_deflate_blocks, 33);
    ImageTestPngBuilder split_block_builder = {
        .bytes = split_block_png,
        .capacity = sizeof(split_block_png),
        .position = 33,
        .valid = true,
    };
    bool split_block_valid = image_test_png_append_chunk(&split_block_builder,
                                                         IMAGE_TEST_PNG_CHUNK_TYPE('I', 'D', 'A', 'T'),
                                                         image_test_png_two_deflate_blocks + 41, 8);
    u64 split_block_offset = split_block_builder.position + 8u;
    split_block_valid = split_block_valid && image_test_png_append_chunk(&split_block_builder,
                                                                          IMAGE_TEST_PNG_CHUNK_TYPE('I', 'D', 'A', 'T'),
                                                                          image_test_png_two_deflate_blocks + 49, 10);
    split_block_valid = split_block_valid && image_test_png_append_chunk(
                                                   &split_block_builder, IMAGE_TEST_PNG_CHUNK_TYPE('I', 'E', 'N', 'D'), 0, 0);
    BUSTER_TEST(arguments, split_block_valid && split_block_offset == 61u);
    ByteSlice two_block_png = BUSTER_ARRAY_TO_BYTE_SLICE(image_test_png_two_deflate_blocks);
    ByteSlice unaligned_block_png = BUSTER_ARRAY_TO_BYTE_SLICE(image_test_png_unaligned_deflate_blocks);
    ByteSlice split_block_bytes = {.pointer = split_block_png, .length = split_block_builder.position};
    ByteSlice buffered_block_jpeg = BUSTER_ARRAY_TO_BYTE_SLICE(image_test_jpeg_buffered_blocks);
    ByteSlice three_scan_jpeg = BUSTER_ARRAY_TO_BYTE_SLICE(image_test_jpeg_three_scans);
    ImageProbeResult two_block_probe = image_probe(two_block_png, (ImageDecodeOptions){.format_hint = IMAGE_FORMAT_PNG});
    ImageProbeResult unaligned_block_probe = image_probe(unaligned_block_png, (ImageDecodeOptions){.format_hint = IMAGE_FORMAT_PNG});
    ImageProbeResult split_block_probe = image_probe(split_block_bytes, (ImageDecodeOptions){.format_hint = IMAGE_FORMAT_PNG});
    ImageProbeResult buffered_block_probe = image_probe(buffered_block_jpeg,
                                                        (ImageDecodeOptions){.format_hint = IMAGE_FORMAT_JPEG});
    ImageProbeResult three_scan_probe = image_probe(three_scan_jpeg, (ImageDecodeOptions){.format_hint = IMAGE_FORMAT_JPEG});
    BUSTER_TEST(arguments, two_block_probe.status == IMAGE_DECODE_SUCCESS);
    BUSTER_TEST(arguments, unaligned_block_probe.status == IMAGE_DECODE_SUCCESS);
    BUSTER_TEST(arguments, split_block_probe.status == IMAGE_DECODE_SUCCESS);
    BUSTER_TEST(arguments, buffered_block_probe.status == IMAGE_DECODE_SUCCESS);
    BUSTER_TEST(arguments, three_scan_probe.status == IMAGE_DECODE_SUCCESS);
    u64 structural_fixture_position = arguments->arena->position;
    u8 expected_black[] = {0, 0, 0, 255};
    u8 expected_gray[] = {128, 128, 128, 255};
    ImageDecodeResult two_block_decode = image_decode(arguments->arena, two_block_png,
                                                      (ImageDecodeOptions){.format_hint = IMAGE_FORMAT_PNG});
    BUSTER_TEST(arguments, two_block_decode.status == IMAGE_DECODE_SUCCESS &&
                           two_block_decode.image.pixels.length == sizeof(expected_black) &&
                           !memcmp(two_block_decode.image.pixels.pointer, expected_black, sizeof(expected_black)));
    arena_set_position(arguments->arena, structural_fixture_position);
    ImageDecodeResult unaligned_block_decode = image_decode(arguments->arena, unaligned_block_png,
                                                            (ImageDecodeOptions){.format_hint = IMAGE_FORMAT_PNG});
    BUSTER_TEST(arguments, unaligned_block_decode.status == IMAGE_DECODE_SUCCESS &&
                           unaligned_block_decode.image.pixels.length == sizeof(expected_black) &&
                           !memcmp(unaligned_block_decode.image.pixels.pointer, expected_black, sizeof(expected_black)));
    arena_set_position(arguments->arena, structural_fixture_position);
    ImageDecodeResult split_block_decode = image_decode(arguments->arena, split_block_bytes,
                                                        (ImageDecodeOptions){.format_hint = IMAGE_FORMAT_PNG});
    BUSTER_TEST(arguments, split_block_decode.status == IMAGE_DECODE_SUCCESS &&
                           split_block_decode.image.pixels.length == sizeof(expected_black) &&
                           !memcmp(split_block_decode.image.pixels.pointer, expected_black, sizeof(expected_black)));
    arena_set_position(arguments->arena, structural_fixture_position);
    ImageDecodeResult buffered_block_decode = image_decode(arguments->arena, buffered_block_jpeg,
                                                           (ImageDecodeOptions){.format_hint = IMAGE_FORMAT_JPEG});
    bool buffered_pixels_match = buffered_block_decode.status == IMAGE_DECODE_SUCCESS &&
                                 buffered_block_decode.image.width == 16 && buffered_block_decode.image.height == 8 &&
                                 buffered_block_decode.image.pixels.length == 16u * 8u * sizeof(expected_gray);
    for (u64 offset = 0; offset < buffered_block_decode.image.pixels.length && buffered_pixels_match;
         offset += sizeof(expected_gray))
    {
        buffered_pixels_match = !memcmp(buffered_block_decode.image.pixels.pointer + offset, expected_gray,
                                        sizeof(expected_gray));
    }
    BUSTER_TEST(arguments, buffered_pixels_match);
    arena_set_position(arguments->arena, structural_fixture_position);
    ImageDecodeResult three_scan_decode = image_decode(arguments->arena, three_scan_jpeg,
                                                       (ImageDecodeOptions){.format_hint = IMAGE_FORMAT_JPEG});
    BUSTER_TEST(arguments, three_scan_decode.status == IMAGE_DECODE_SUCCESS &&
                           three_scan_decode.image.pixels.length == sizeof(expected_gray) &&
                           !memcmp(three_scan_decode.image.pixels.pointer, expected_gray, sizeof(expected_gray)));
    arena_set_position(arguments->arena, structural_fixture_position);

    // After the signature, one loop step and the IHDR CRC consume exactly this
    // budget. The second chunk must exceed max_chunks before its own work is
    // charged, and decode must still roll back its output arena.
    u64 png_first_chunk_work = 8u + 1u + (13u + 4u);
    ImageStructuralLimitCase structural_limits[] = {
        {image_test_png, sizeof(image_test_png),
         {.format_hint = IMAGE_FORMAT_PNG, .max_work = png_first_chunk_work, .max_chunks = 1},
         IMAGE_EXCEEDED_LIMIT_CHUNKS, 33, true},
        {image_test_png_two_deflate_blocks, sizeof(image_test_png_two_deflate_blocks),
         {.format_hint = IMAGE_FORMAT_PNG, .max_blocks = 1}, IMAGE_EXCEEDED_LIMIT_BLOCKS, 49, false},
        {image_test_png_unaligned_deflate_blocks, sizeof(image_test_png_unaligned_deflate_blocks),
         {.format_hint = IMAGE_FORMAT_PNG, .max_blocks = 1}, IMAGE_EXCEEDED_LIMIT_BLOCKS, 45, false},
        {split_block_png, split_block_builder.position, {.format_hint = IMAGE_FORMAT_PNG, .max_blocks = 1},
         IMAGE_EXCEEDED_LIMIT_BLOCKS, split_block_offset, false},
        {image_test_jpeg, sizeof(image_test_jpeg), {.format_hint = IMAGE_FORMAT_JPEG, .max_segments = 1},
         IMAGE_EXCEEDED_LIMIT_SEGMENTS, 20, true},
        {image_test_jpeg_buffered_blocks, sizeof(image_test_jpeg_buffered_blocks),
         {.format_hint = IMAGE_FORMAT_JPEG, .max_blocks = 1}, IMAGE_EXCEEDED_LIMIT_BLOCKS,
         IMAGE_TEST_JPEG_BUFFERED_BLOCK_OFFSET, false},
        {image_test_jpeg_three_scans, sizeof(image_test_jpeg_three_scans),
         {.format_hint = IMAGE_FORMAT_JPEG, .max_scans = 1}, IMAGE_EXCEEDED_LIMIT_SCANS, 141, true},
        {image_test_jpeg_three_scans, sizeof(image_test_jpeg_three_scans),
         {.format_hint = IMAGE_FORMAT_JPEG, .max_blocks = 1}, IMAGE_EXCEEDED_LIMIT_BLOCKS, 151, false},
        {image_test_gif_animation, sizeof(image_test_gif_animation),
         {.format_hint = IMAGE_FORMAT_GIF, .max_frames = 1}, IMAGE_EXCEEDED_LIMIT_FRAMES, 80, true},
        {image_test_gif_animation, sizeof(image_test_gif_animation),
         {.format_hint = IMAGE_FORMAT_GIF, .max_blocks = 1}, IMAGE_EXCEEDED_LIMIT_BLOCKS, 39, true},
        {image_test_bmp_rle8, sizeof(image_test_bmp_rle8), {.format_hint = IMAGE_FORMAT_BMP, .max_blocks = 1},
         IMAGE_EXCEEDED_LIMIT_BLOCKS, 76, false},
        {image_test_tga_rle_top_right, sizeof(image_test_tga_rle_top_right),
         {.format_hint = IMAGE_FORMAT_TGA, .max_blocks = 1}, IMAGE_EXCEEDED_LIMIT_BLOCKS, 23, false},
        {image_test_qoi_mixed_ops, sizeof(image_test_qoi_mixed_ops),
         {.format_hint = IMAGE_FORMAT_QOI, .max_blocks = 1}, IMAGE_EXCEEDED_LIMIT_BLOCKS, 18, true},
    };
    for (u32 limit_index = 0; limit_index < BUSTER_ARRAY_LENGTH(structural_limits); limit_index += 1)
    {
        ImageStructuralLimitCase const* limit = &structural_limits[limit_index];
        ByteSlice encoded = {.pointer = (u8*)limit->encoded, .length = limit->encoded_size};
        if (limit->probe_limited)
        {
            BUSTER_TEST(arguments, image_test_probe_limit(encoded, limit->options, limit->exceeded_limit,
                                                          limit->error_offset));
        }
        else
        {
            ImageProbeResult probe = image_probe(encoded, limit->options);
            BUSTER_TEST(arguments, probe.status == IMAGE_DECODE_SUCCESS &&
                                   probe.exceeded_limit == IMAGE_EXCEEDED_LIMIT_NONE);
        }
        BUSTER_TEST(arguments, image_test_decode_limit(arguments->arena, encoded, limit->options,
                                                       limit->exceeded_limit, limit->error_offset));
    }

    ImageDecodeOptions huge_png_options = {
        .format_hint = IMAGE_FORMAT_PNG,
        .max_width = UINT32_C(0x3fffffff),
        .max_height = UINT32_C(0x7fffffff),
        .max_pixels = UINT64_MAX,
        .max_decoded_bytes = UINT64_MAX,
    };
    ByteSlice huge_png = BUSTER_ARRAY_TO_BYTE_SLICE(image_test_png_unallocatable_filtered_size);
    ImageProbeResult huge_png_probe = image_probe(huge_png, huge_png_options);
    BUSTER_TEST(arguments, huge_png_probe.status == IMAGE_DECODE_SUCCESS &&
                           huge_png_probe.information.width == UINT32_C(0x3fffffff) &&
                           huge_png_probe.information.height == UINT32_C(0x7fffffff));
    u64 huge_png_position = arguments->arena->position;
    ImageDecodeResult huge_png_decode = image_decode(arguments->arena, huge_png, huge_png_options);
    BUSTER_TEST(arguments, huge_png_decode.status == IMAGE_DECODE_CAPACITY_EXCEEDED &&
                           huge_png_decode.error_offset == 16 &&
                           huge_png_decode.exceeded_limit == IMAGE_EXCEEDED_LIMIT_NONE &&
                           !huge_png_decode.observed_value && !huge_png_decode.limit_value &&
                           image_test_image_empty(huge_png_decode.image) &&
                           arguments->arena->position == huge_png_position);

    // A deliberately tiny work budget must fail transactionally in every
    // implemented codec, including formats whose output allocation succeeds
    // before their parser exhausts the budget.
    for (u32 fixture_index = 0; fixture_index < BUSTER_ARRAY_LENGTH(image_test_fixtures); fixture_index += 1)
    {
        ImageTestFixture const* fixture = &image_test_fixtures[fixture_index];
        ImageDecodeOptions options = {.format_hint = fixture->format, .max_work = 1};
        BUSTER_TEST(arguments, image_test_rejected_without_allocation(arguments->arena,
                                                                       image_test_encoded(fixture, fixture->encoded_size), options,
                                                                       IMAGE_DECODE_LIMIT_EXCEEDED));
    }

    // The JPEG output alone cannot fit in this arena. A capacity failure must
    // still leave both the image and the caller-owned cursor untouched.
    Arena* small_arena = arena_create((ArenaCreation){.reserved_size = 128, .granularity = 64, .initial_size = 64,
                                                       .flags = {.no_pool = true}});
    if (BUSTER_REQUIRE(arguments, small_arena != 0))
    {
        u64 small_position = small_arena->position;
        ImageDecodeResult small_result = image_decode(small_arena, BUSTER_ARRAY_TO_BYTE_SLICE(image_test_jpeg),
                                                      (ImageDecodeOptions){.format_hint = IMAGE_FORMAT_JPEG});
        BUSTER_TEST(arguments, small_result.status == IMAGE_DECODE_CAPACITY_EXCEEDED);
        BUSTER_TEST(arguments, image_test_image_empty(small_result.image));
        BUSTER_TEST(arguments, small_arena->position == small_position);
        BUSTER_TEST(arguments, arena_destroy(small_arena, 1));
    }

    // PNG's filtered scanlines are scratch state. Constrain the caller arena
    // to exactly the aligned RGBA result: decode must succeed and retain only
    // those pixels, proving the temporary buffer does not consume caller space.
    u64 png_position = arguments->arena->position;
    u64 png_pixel_position = 0;
    if (BUSTER_REQUIRE(arguments, align_forward_checked(png_position, BUSTER_ALIGN_OF(u32), &png_pixel_position)))
    {
        u64 png_reserved_size = arguments->arena->reserved_size;
        u64 png_exact_end = png_pixel_position + sizeof(image_test_expected_rgba_alpha);
        arguments->arena->reserved_size = png_exact_end;
        ImageDecodeResult exact_png = image_decode(arguments->arena, BUSTER_ARRAY_TO_BYTE_SLICE(image_test_png),
                                                   (ImageDecodeOptions){.format_hint = IMAGE_FORMAT_PNG});
        arguments->arena->reserved_size = png_reserved_size;
        if (BUSTER_REQUIRE(arguments, exact_png.status == IMAGE_DECODE_SUCCESS))
        {
            BUSTER_TEST(arguments, image_test_pixels_match(exact_png.image, &image_test_fixtures[0]));
            BUSTER_TEST(arguments, arguments->arena->position == png_exact_end);
        }
        arena_set_position(arguments->arena, png_position);
    }

    // Progressive DCT JPEG is detected and parsed far enough to identify the
    // unsupported coding mode, rather than being mislabeled as malformed.
    ByteSlice progressive_jpeg = BUSTER_ARRAY_TO_BYTE_SLICE(image_test_jpeg_progressive);
    ImageDecodeOptions progressive_options = {.format_hint = IMAGE_FORMAT_JPEG};
    BUSTER_TEST(arguments, image_detect_format(progressive_jpeg) == IMAGE_FORMAT_JPEG);
    ImageProbeResult progressive_probe = image_probe(progressive_jpeg, progressive_options);
    BUSTER_TEST(arguments, progressive_probe.status == IMAGE_DECODE_UNSUPPORTED_FEATURE &&
                           progressive_probe.unsupported_feature == IMAGE_UNSUPPORTED_FEATURE_CODING);
    BUSTER_TEST(arguments, image_test_rejected_without_allocation(arguments->arena, progressive_jpeg, progressive_options,
                                                                   IMAGE_DECODE_UNSUPPORTED_FEATURE));

    u64 work_limit_position = arguments->arena->position;
    ImageDecodeOptions work_limit = {.format_hint = IMAGE_FORMAT_QOI, .max_work = 1};
    ImageProbeResult work_limited_probe = image_probe(BUSTER_ARRAY_TO_BYTE_SLICE(image_test_qoi), work_limit);
    BUSTER_TEST(arguments, work_limited_probe.status == IMAGE_DECODE_LIMIT_EXCEEDED &&
                           work_limited_probe.exceeded_limit == IMAGE_EXCEEDED_LIMIT_WORK &&
                           work_limited_probe.observed_value == 4 && work_limited_probe.limit_value == 1 &&
                           work_limited_probe.error_offset == 14);
    ImageDecodeResult work_limited = image_decode(arguments->arena, BUSTER_ARRAY_TO_BYTE_SLICE(image_test_qoi), work_limit);
    BUSTER_TEST(arguments, work_limited.status == IMAGE_DECODE_LIMIT_EXCEEDED &&
                           work_limited.exceeded_limit == IMAGE_EXCEEDED_LIMIT_WORK &&
                           work_limited.observed_value == 4 && work_limited.limit_value == 1 &&
                           work_limited.error_offset == 14 && image_test_image_empty(work_limited.image) &&
                           arguments->arena->position == work_limit_position);

    // PNG protects both structural and compressed bytes with per-chunk CRCs.
    // Byte 32 is in the independently generated IHDR CRC, not in its payload.
    u8 png_bad_crc[sizeof(image_test_png)];
    memcpy(png_bad_crc, image_test_png, sizeof(png_bad_crc));
    png_bad_crc[32] ^= 1u;
    ByteSlice bad_png = {.pointer = png_bad_crc, .length = sizeof(png_bad_crc)};
    ImageDecodeOptions png_options = {.format_hint = IMAGE_FORMAT_PNG};
    ImageProbeResult png_probe = image_probe(bad_png, png_options);
    BUSTER_TEST(arguments, png_probe.status == IMAGE_DECODE_CHECKSUM_MISMATCH);
    BUSTER_TEST(arguments,
                image_test_rejected_without_allocation(arguments->arena, bad_png, png_options, IMAGE_DECODE_CHECKSUM_MISMATCH));

    // A future-version reserved bit changes only how an otherwise unknown
    // chunk is classified. Ancillary coverage is in the golden table above;
    // an unknown critical spelling must retain the unsupported status.
    ByteSlice reserved_critical_png = BUSTER_ARRAY_TO_BYTE_SLICE(image_test_png_reserved_critical);
    ImageProbeResult reserved_critical_probe = image_probe(reserved_critical_png, png_options);
    BUSTER_TEST(arguments, reserved_critical_probe.status == IMAGE_DECODE_UNSUPPORTED_FEATURE &&
                           reserved_critical_probe.unsupported_feature == IMAGE_UNSUPPORTED_FEATURE_CONTAINER_FEATURE);
    BUSTER_TEST(arguments, image_test_rejected_without_allocation(arguments->arena, reserved_critical_png, png_options,
                                                                   IMAGE_DECODE_UNSUPPORTED_FEATURE));

    // An unavailable indexed-color palette entry is a recoverable sample error:
    // probe retains the indexed source model and decode paints opaque black.
    ByteSlice out_of_range_index_png = BUSTER_ARRAY_TO_BYTE_SLICE(image_test_png_out_of_range_index);
    ImageProbeResult out_of_range_index_probe = image_probe(out_of_range_index_png, png_options);
    BUSTER_TEST(arguments, out_of_range_index_probe.status == IMAGE_DECODE_SUCCESS &&
                           out_of_range_index_probe.information.source_color_model == IMAGE_COLOR_MODEL_INDEXED &&
                           out_of_range_index_probe.information.source_bits_per_channel == 2);
    u64 png_recovery_position = arguments->arena->position;
    ImageDecodeResult out_of_range_index_decode = image_decode(arguments->arena, out_of_range_index_png, png_options);
    if (BUSTER_REQUIRE(arguments, out_of_range_index_decode.status == IMAGE_DECODE_SUCCESS))
    {
        BUSTER_TEST(arguments, out_of_range_index_decode.image.pixels.length == sizeof(expected_black) &&
                               !memcmp(out_of_range_index_decode.image.pixels.pointer, expected_black, sizeof(expected_black)));
    }
    arena_set_position(arguments->arena, png_recovery_position);

    // An indexed tRNS chunk may contain fewer alpha values than PLTE has
    // entries, including none. Empty and all-255 tables leave the palette
    // opaque, so has_alpha describes actual palette transparency rather than
    // the mere presence of a tRNS chunk.
    u8 opaque_trns_pngs[2][128] = {0};
    u8 indexed_zero_idat[] = {0x78, 0x9c, 0x63, 0x60, 0x00, 0x00, 0x00, 0x02, 0x00, 0x01};
    u8 opaque_alpha = 255;
    for (u32 trns_case = 0; trns_case < BUSTER_ARRAY_LENGTH(opaque_trns_pngs); trns_case += 1)
    {
        memcpy(opaque_trns_pngs[trns_case], image_test_png_out_of_range_index, 48);
        ImageTestPngBuilder opaque_trns_builder = {
            .bytes = opaque_trns_pngs[trns_case],
            .capacity = sizeof(opaque_trns_pngs[trns_case]),
            .position = 48,
            .valid = true,
        };
        image_test_png_append_chunk(&opaque_trns_builder, IMAGE_TEST_PNG_CHUNK_TYPE('t', 'R', 'N', 'S'),
                                    trns_case ? &opaque_alpha : 0, trns_case);
        image_test_png_append_chunk(&opaque_trns_builder, IMAGE_TEST_PNG_CHUNK_TYPE('I', 'D', 'A', 'T'),
                                    indexed_zero_idat, sizeof(indexed_zero_idat));
        image_test_png_append_chunk(&opaque_trns_builder, IMAGE_TEST_PNG_CHUNK_TYPE('I', 'E', 'N', 'D'), 0, 0);
        BUSTER_REQUIRE(arguments, opaque_trns_builder.valid);
        ByteSlice opaque_trns_bytes = {.pointer = opaque_trns_pngs[trns_case], .length = opaque_trns_builder.position};
        ImageProbeResult opaque_trns_probe = image_probe(opaque_trns_bytes, png_options);
        BUSTER_TEST(arguments, opaque_trns_probe.status == IMAGE_DECODE_SUCCESS &&
                               opaque_trns_probe.information.source_color_model == IMAGE_COLOR_MODEL_INDEXED &&
                               !opaque_trns_probe.information.has_alpha);
        png_recovery_position = arguments->arena->position;
        ImageDecodeResult opaque_trns_decode = image_decode(arguments->arena, opaque_trns_bytes, png_options);
        if (BUSTER_REQUIRE(arguments, opaque_trns_decode.status == IMAGE_DECODE_SUCCESS))
        {
            u8 expected_red[] = {255, 0, 0, 255};
            BUSTER_TEST(arguments, !opaque_trns_decode.information.has_alpha &&
                                   opaque_trns_decode.image.pixels.length == sizeof(expected_red) &&
                                   !memcmp(opaque_trns_decode.image.pixels.pointer, expected_red, sizeof(expected_red)));
        }
        arena_set_position(arguments->arena, png_recovery_position);
    }

    // Bytes after a complete zlib datastream are unused image data, but remain
    // covered by their IDAT CRC. Keep the ignored tail small and prove that its
    // corruption is still detected before decode reaches the zlib stream.
    u8 trailing_idat_data[24];
    memcpy(trailing_idat_data, image_test_png + 41, 20);
    u8 unused_idat_bytes[] = {0xde, 0xad, 0xbe, 0xef};
    memcpy(trailing_idat_data + 20, unused_idat_bytes, sizeof(unused_idat_bytes));
    u8 trailing_idat_png[128] = {0};
    memcpy(trailing_idat_png, image_test_png, 33);
    ImageTestPngBuilder trailing_idat_builder = {
        .bytes = trailing_idat_png,
        .capacity = sizeof(trailing_idat_png),
        .position = 33,
        .valid = true,
    };
    u64 unused_idat_offset = trailing_idat_builder.position + 8u + 20u;
    image_test_png_append_chunk(&trailing_idat_builder, IMAGE_TEST_PNG_CHUNK_TYPE('I', 'D', 'A', 'T'),
                                trailing_idat_data, sizeof(trailing_idat_data));
    image_test_png_append_chunk(&trailing_idat_builder, IMAGE_TEST_PNG_CHUNK_TYPE('I', 'E', 'N', 'D'), 0, 0);
    BUSTER_REQUIRE(arguments, trailing_idat_builder.valid);
    ByteSlice trailing_idat_bytes = {.pointer = trailing_idat_png, .length = trailing_idat_builder.position};
    ImageProbeResult trailing_idat_probe = image_probe(trailing_idat_bytes, png_options);
    BUSTER_TEST(arguments, trailing_idat_probe.status == IMAGE_DECODE_SUCCESS);
    png_recovery_position = arguments->arena->position;
    ImageDecodeResult trailing_idat_decode = image_decode(arguments->arena, trailing_idat_bytes, png_options);
    if (BUSTER_REQUIRE(arguments, trailing_idat_decode.status == IMAGE_DECODE_SUCCESS))
    {
        BUSTER_TEST(arguments, image_test_pixels_match(trailing_idat_decode.image, &image_test_fixtures[0]));
    }
    arena_set_position(arguments->arena, png_recovery_position);

    u8 bad_trailing_crc_png[sizeof(trailing_idat_png)];
    memcpy(bad_trailing_crc_png, trailing_idat_png, sizeof(bad_trailing_crc_png));
    bad_trailing_crc_png[unused_idat_offset] ^= 1u;
    ByteSlice bad_trailing_crc = {.pointer = bad_trailing_crc_png, .length = trailing_idat_builder.position};
    ImageProbeResult bad_trailing_crc_probe = image_probe(bad_trailing_crc, png_options);
    BUSTER_TEST(arguments, bad_trailing_crc_probe.status == IMAGE_DECODE_CHECKSUM_MISMATCH);
    BUSTER_TEST(arguments, image_test_rejected_without_allocation(arguments->arena, bad_trailing_crc, png_options,
                                                                   IMAGE_DECODE_CHECKSUM_MISMATCH));

    // PNG color chunks are metadata only: decoding preserves encoded samples
    // while probe and decode report every recognized, structurally valid tag.
    // This APNG includes the default IDAT image as frame zero; later fdAT data
    // is validated structurally but deliberately not decoded by this API.
    u8 chrm[32] = {0};
    u32 srgb_chromaticities[] = {31270, 32900, 64000, 33000, 30000, 60000, 15000, 6000};
    for (u32 component = 0; component < BUSTER_ARRAY_LENGTH(srgb_chromaticities); component += 1)
    {
        image_test_u32_be(chrm + component * 4u, srgb_chromaticities[component]);
    }
    u8 gama[4];
    image_test_u32_be(gama, 45455);
    u8 iccp[] = {'p', 0, 0, 0x78, 0x9c, 0x03, 0, 0, 0, 0, 1};
    u8 srgb[] = {0};
    u8 cicp[] = {1, 1, 0, 1};
    u8 exif[] = {
        'M', 'M', 0, 0x2a, 0, 0, 0, 8,
        0, 1, 0x01, 0x12, 0, 3, 0, 0, 0, 1, 0, IMAGE_ORIENTATION_RIGHT_TOP, 0, 0,
        0, 0, 0, 0,
    };
    u8 actl_two[] = {0, 0, 0, 2, 0, 0, 0, 0};
    u8 actl_one[] = {0, 0, 0, 1, 0, 0, 0, 0};
    u8 fctl[26] = {0};
    image_test_u32_be(fctl + 4, 2);
    image_test_u32_be(fctl + 8, 2);
    u8 second_fctl[26];
    memcpy(second_fctl, fctl, sizeof(second_fctl));
    image_test_u32_be(second_fctl, 1);
    u8 second_fdat[24];
    image_test_u32_be(second_fdat, 2);
    memcpy(second_fdat + 4, image_test_png + 41, 20);

    u8 metadata_png[512] = {0};
    memcpy(metadata_png, image_test_png, 33);
    ImageTestPngBuilder metadata_builder = {
        .bytes = metadata_png,
        .capacity = sizeof(metadata_png),
        .position = 33,
        .valid = true,
    };
    u64 chrm_header_offset = metadata_builder.position;
    image_test_png_append_chunk(&metadata_builder, IMAGE_TEST_PNG_CHUNK_TYPE('c', 'H', 'R', 'M'), chrm, sizeof(chrm));
    u64 gama_header_offset = metadata_builder.position;
    image_test_png_append_chunk(&metadata_builder, IMAGE_TEST_PNG_CHUNK_TYPE('g', 'A', 'M', 'A'), gama, sizeof(gama));
    image_test_png_append_chunk(&metadata_builder, IMAGE_TEST_PNG_CHUNK_TYPE('i', 'C', 'C', 'P'), iccp, sizeof(iccp));
    image_test_png_append_chunk(&metadata_builder, IMAGE_TEST_PNG_CHUNK_TYPE('c', 'I', 'C', 'P'), cicp, sizeof(cicp));
    image_test_png_append_chunk(&metadata_builder, IMAGE_TEST_PNG_CHUNK_TYPE('e', 'X', 'I', 'f'), exif, sizeof(exif));
    u64 actl_header_offset = metadata_builder.position;
    image_test_png_append_chunk(&metadata_builder, IMAGE_TEST_PNG_CHUNK_TYPE('a', 'c', 'T', 'L'), actl_two, sizeof(actl_two));
    u64 fctl_header_offset = metadata_builder.position;
    image_test_png_append_chunk(&metadata_builder, IMAGE_TEST_PNG_CHUNK_TYPE('f', 'c', 'T', 'L'), fctl, sizeof(fctl));
    image_test_png_append_chunk(&metadata_builder, IMAGE_TEST_PNG_CHUNK_TYPE('I', 'D', 'A', 'T'), image_test_png + 41, 20);
    image_test_png_append_chunk(&metadata_builder, IMAGE_TEST_PNG_CHUNK_TYPE('f', 'c', 'T', 'L'), second_fctl, sizeof(second_fctl));
    u64 fdat_header_offset = metadata_builder.position;
    image_test_png_append_chunk(&metadata_builder, IMAGE_TEST_PNG_CHUNK_TYPE('f', 'd', 'A', 'T'), second_fdat, sizeof(second_fdat));
    image_test_png_append_chunk(&metadata_builder, IMAGE_TEST_PNG_CHUNK_TYPE('I', 'E', 'N', 'D'), 0, 0);
    BUSTER_REQUIRE(arguments, metadata_builder.valid);
    ByteSlice metadata_png_bytes = {.pointer = metadata_png, .length = metadata_builder.position};
    ImageColorMetadataFlags all_png_metadata = IMAGE_COLOR_METADATA_CHROMATICITIES | IMAGE_COLOR_METADATA_GAMMA |
                                               IMAGE_COLOR_METADATA_ICC_PROFILE | IMAGE_COLOR_METADATA_CICP |
                                               IMAGE_COLOR_METADATA_EXIF;
    ImageProbeResult metadata_probe = image_probe(metadata_png_bytes, png_options);
    BUSTER_TEST(arguments, metadata_probe.status == IMAGE_DECODE_SUCCESS && metadata_probe.information.frame_count == 2 &&
                           metadata_probe.information.source_color_model == IMAGE_COLOR_MODEL_RGBA &&
                           metadata_probe.information.color_metadata_flags == all_png_metadata &&
                           metadata_probe.information.orientation == IMAGE_ORIENTATION_RIGHT_TOP &&
                           metadata_probe.information.is_animated && metadata_probe.information.has_more_images);
    u64 metadata_position = arguments->arena->position;
    ImageDecodeResult metadata_decoded = image_decode(arguments->arena, metadata_png_bytes, png_options);
    if (BUSTER_REQUIRE(arguments, metadata_decoded.status == IMAGE_DECODE_SUCCESS))
    {
        BUSTER_TEST(arguments, image_test_information_equal(metadata_probe.information, metadata_decoded.information));
        BUSTER_TEST(arguments, image_test_pixels_match(metadata_decoded.image, &image_test_fixtures[0]));
    }
    arena_set_position(arguments->arena, metadata_position);

    // PNG four-byte integers stop at 2^31-1 even though they are stored in
    // u32. Every recognized field rejects the first out-of-range value at its
    // own byte, during both metadata probing and transactional decoding.
    struct ImageTestPngIntegerCase
    {
        u64 header_offset;
        u32 chunk_length;
        u32 field_offset;
    };
    struct ImageTestPngIntegerCase png_integer_cases[] = {
        {8, 13, 0},
        {8, 13, 4},
        {chrm_header_offset, sizeof(chrm), 0},
        {chrm_header_offset, sizeof(chrm), 4},
        {chrm_header_offset, sizeof(chrm), 8},
        {chrm_header_offset, sizeof(chrm), 12},
        {chrm_header_offset, sizeof(chrm), 16},
        {chrm_header_offset, sizeof(chrm), 20},
        {chrm_header_offset, sizeof(chrm), 24},
        {chrm_header_offset, sizeof(chrm), 28},
        {gama_header_offset, sizeof(gama), 0},
        {actl_header_offset, sizeof(actl_two), 0},
        {actl_header_offset, sizeof(actl_two), 4},
        {fctl_header_offset, sizeof(fctl), 0},
        {fctl_header_offset, sizeof(fctl), 4},
        {fctl_header_offset, sizeof(fctl), 8},
        {fctl_header_offset, sizeof(fctl), 12},
        {fctl_header_offset, sizeof(fctl), 16},
        {fdat_header_offset, sizeof(second_fdat), 0},
    };
    u8 malformed_integer_png[sizeof(metadata_png)];
    for (u32 integer_case = 0; integer_case < BUSTER_ARRAY_LENGTH(png_integer_cases); integer_case += 1)
    {
        struct ImageTestPngIntegerCase const* test = &png_integer_cases[integer_case];
        memcpy(malformed_integer_png, metadata_png, metadata_builder.position);
        u64 data_offset = test->header_offset + 8u;
        u64 field_offset = data_offset + test->field_offset;
        image_test_u32_be(malformed_integer_png + field_offset, UINT32_C(0x80000000));
        u64 crc_offset = data_offset + test->chunk_length;
        image_test_u32_be(malformed_integer_png + crc_offset,
                          image_test_png_crc(malformed_integer_png + test->header_offset + 4u,
                                             (u64)test->chunk_length + 4u));
        ByteSlice malformed_integer_bytes = {.pointer = malformed_integer_png, .length = metadata_builder.position};
        BUSTER_TEST(arguments, image_test_rejected_at_without_allocation(arguments->arena, malformed_integer_bytes,
                                                                          png_options, IMAGE_DECODE_MALFORMED, field_offset));
    }

    // The adjacent maximum remains legal for dimensions and metadata values.
    // num_plays is not otherwise surfaced, so a successful complete APNG also
    // proves that its upper boundary was admitted.
    u8 maximum_integer_png[sizeof(metadata_png)];
    memcpy(maximum_integer_png, metadata_png, metadata_builder.position);
    for (u32 component = 0; component < 8u; component += 1)
    {
        image_test_u32_be(maximum_integer_png + chrm_header_offset + 8u + component * 4u, UINT32_C(0x7fffffff));
    }
    image_test_u32_be(maximum_integer_png + gama_header_offset + 8u, UINT32_C(0x7fffffff));
    image_test_u32_be(maximum_integer_png + actl_header_offset + 12u, UINT32_C(0x7fffffff));
    u64 maximum_headers[] = {chrm_header_offset, gama_header_offset, actl_header_offset};
    u32 maximum_lengths[] = {sizeof(chrm), sizeof(gama), sizeof(actl_two)};
    for (u32 chunk_index = 0; chunk_index < BUSTER_ARRAY_LENGTH(maximum_headers); chunk_index += 1)
    {
        u64 header_offset = maximum_headers[chunk_index];
        u32 length = maximum_lengths[chunk_index];
        image_test_u32_be(maximum_integer_png + header_offset + 8u + length,
                          image_test_png_crc(maximum_integer_png + header_offset + 4u, (u64)length + 4u));
    }
    ByteSlice maximum_integer_bytes = {.pointer = maximum_integer_png, .length = metadata_builder.position};
    ImageProbeResult maximum_integer_probe = image_probe(maximum_integer_bytes, png_options);
    BUSTER_TEST(arguments, maximum_integer_probe.status == IMAGE_DECODE_SUCCESS &&
                           maximum_integer_probe.information.color_metadata_flags == all_png_metadata);
    metadata_position = arguments->arena->position;
    ImageDecodeResult maximum_integer_decode = image_decode(arguments->arena, maximum_integer_bytes, png_options);
    if (BUSTER_REQUIRE(arguments, maximum_integer_decode.status == IMAGE_DECODE_SUCCESS))
    {
        BUSTER_TEST(arguments, image_test_pixels_match(maximum_integer_decode.image, &image_test_fixtures[0]));
    }
    arena_set_position(arguments->arena, metadata_position);

    // eXIf uses a TIFF-relative IFD0 offset. Invalid offsets are rejected at
    // the pointer field by both metadata probing and transactional decoding.
    u8 invalid_exif[] = {'I', 'I', 0x2a, 0, 0xff, 0xff, 0xff, 0xff};
    u8 invalid_exif_png[128] = {0};
    memcpy(invalid_exif_png, image_test_png, 33);
    ImageTestPngBuilder invalid_exif_builder = {
        .bytes = invalid_exif_png,
        .capacity = sizeof(invalid_exif_png),
        .position = 33,
        .valid = true,
    };
    u64 invalid_exif_data_offset = invalid_exif_builder.position + 8u;
    image_test_png_append_chunk(&invalid_exif_builder, IMAGE_TEST_PNG_CHUNK_TYPE('e', 'X', 'I', 'f'),
                                invalid_exif, sizeof(invalid_exif));
    image_test_png_append_chunk(&invalid_exif_builder, IMAGE_TEST_PNG_CHUNK_TYPE('I', 'D', 'A', 'T'), image_test_png + 41, 20);
    image_test_png_append_chunk(&invalid_exif_builder, IMAGE_TEST_PNG_CHUNK_TYPE('I', 'E', 'N', 'D'), 0, 0);
    BUSTER_REQUIRE(arguments, invalid_exif_builder.valid);
    ByteSlice invalid_exif_bytes = {.pointer = invalid_exif_png, .length = invalid_exif_builder.position};
    ImageProbeResult invalid_exif_probe = image_probe(invalid_exif_bytes, png_options);
    BUSTER_TEST(arguments, invalid_exif_probe.status == IMAGE_DECODE_MALFORMED &&
                           invalid_exif_probe.error_offset == invalid_exif_data_offset + 4u);
    metadata_position = arguments->arena->position;
    ImageDecodeResult invalid_exif_decode = image_decode(arguments->arena, invalid_exif_bytes, png_options);
    BUSTER_TEST(arguments, invalid_exif_decode.status == IMAGE_DECODE_MALFORMED &&
                           invalid_exif_decode.error_offset == invalid_exif_data_offset + 4u &&
                           image_test_image_empty(invalid_exif_decode.image) && arguments->arena->position == metadata_position);

    u8 srgb_png[128] = {0};
    memcpy(srgb_png, image_test_png, 33);
    ImageTestPngBuilder srgb_builder = {
        .bytes = srgb_png,
        .capacity = sizeof(srgb_png),
        .position = 33,
        .valid = true,
    };
    image_test_png_append_chunk(&srgb_builder, IMAGE_TEST_PNG_CHUNK_TYPE('s', 'R', 'G', 'B'), srgb, sizeof(srgb));
    image_test_png_append_chunk(&srgb_builder, IMAGE_TEST_PNG_CHUNK_TYPE('I', 'D', 'A', 'T'), image_test_png + 41, 20);
    image_test_png_append_chunk(&srgb_builder, IMAGE_TEST_PNG_CHUNK_TYPE('I', 'E', 'N', 'D'), 0, 0);
    BUSTER_REQUIRE(arguments, srgb_builder.valid);
    ImageProbeResult srgb_probe = image_probe((ByteSlice){.pointer = srgb_png, .length = srgb_builder.position}, png_options);
    BUSTER_TEST(arguments, srgb_probe.status == IMAGE_DECODE_SUCCESS &&
                           srgb_probe.information.color_metadata_flags == IMAGE_COLOR_METADATA_SRGB &&
                           srgb_probe.information.source_color_model == IMAGE_COLOR_MODEL_RGBA);

    // Both profile chunks are discouraged in one stream, but their coexistence
    // is not forbidden. Presence reporting is independent of chunk order because
    // this decoder deliberately leaves PNG color precedence to its caller.
    u8 coexisting_profiles_png[160] = {0};
    memcpy(coexisting_profiles_png, image_test_png, 33);
    ImageTestPngBuilder coexisting_profiles = {
        .bytes = coexisting_profiles_png,
        .capacity = sizeof(coexisting_profiles_png),
        .position = 33,
        .valid = true,
    };
    image_test_png_append_chunk(&coexisting_profiles, IMAGE_TEST_PNG_CHUNK_TYPE('i', 'C', 'C', 'P'), iccp, sizeof(iccp));
    image_test_png_append_chunk(&coexisting_profiles, IMAGE_TEST_PNG_CHUNK_TYPE('s', 'R', 'G', 'B'), srgb, sizeof(srgb));
    image_test_png_append_chunk(&coexisting_profiles, IMAGE_TEST_PNG_CHUNK_TYPE('I', 'D', 'A', 'T'), image_test_png + 41, 20);
    image_test_png_append_chunk(&coexisting_profiles, IMAGE_TEST_PNG_CHUNK_TYPE('I', 'E', 'N', 'D'), 0, 0);

    u8 reversed_profiles_png[160] = {0};
    memcpy(reversed_profiles_png, image_test_png, 33);
    ImageTestPngBuilder reversed_profiles = {
        .bytes = reversed_profiles_png,
        .capacity = sizeof(reversed_profiles_png),
        .position = 33,
        .valid = true,
    };
    image_test_png_append_chunk(&reversed_profiles, IMAGE_TEST_PNG_CHUNK_TYPE('s', 'R', 'G', 'B'), srgb, sizeof(srgb));
    image_test_png_append_chunk(&reversed_profiles, IMAGE_TEST_PNG_CHUNK_TYPE('i', 'C', 'C', 'P'), iccp, sizeof(iccp));
    image_test_png_append_chunk(&reversed_profiles, IMAGE_TEST_PNG_CHUNK_TYPE('I', 'D', 'A', 'T'), image_test_png + 41, 20);
    image_test_png_append_chunk(&reversed_profiles, IMAGE_TEST_PNG_CHUNK_TYPE('I', 'E', 'N', 'D'), 0, 0);
    BUSTER_REQUIRE(arguments, coexisting_profiles.valid && reversed_profiles.valid);
    ByteSlice profile_orders[] = {
        {.pointer = coexisting_profiles_png, .length = coexisting_profiles.position},
        {.pointer = reversed_profiles_png, .length = reversed_profiles.position},
    };
    ImageColorMetadataFlags profile_flags = IMAGE_COLOR_METADATA_ICC_PROFILE | IMAGE_COLOR_METADATA_SRGB;
    for (u32 profile_order = 0; profile_order < BUSTER_ARRAY_LENGTH(profile_orders); profile_order += 1)
    {
        ImageProbeResult profile_probe = image_probe(profile_orders[profile_order], png_options);
        BUSTER_TEST(arguments, profile_probe.status == IMAGE_DECODE_SUCCESS &&
                               profile_probe.information.color_metadata_flags == profile_flags);
    }
    metadata_position = arguments->arena->position;
    ImageDecodeResult profile_decoded = image_decode(arguments->arena, profile_orders[0], png_options);
    if (BUSTER_REQUIRE(arguments, profile_decoded.status == IMAGE_DECODE_SUCCESS))
    {
        BUSTER_TEST(arguments, profile_decoded.information.color_metadata_flags == profile_flags &&
                               image_test_pixels_match(profile_decoded.image, &image_test_fixtures[0]));
    }
    arena_set_position(arguments->arena, metadata_position);

    // If no fcTL precedes IDAT, the default is only a static fallback and is
    // outside acTL.num_frames. Thus a one-frame animation still has another
    // image even though frame_count reports the one animation frame.
    u8 fallback_png[256] = {0};
    memcpy(fallback_png, image_test_png, 33);
    ImageTestPngBuilder fallback_builder = {
        .bytes = fallback_png,
        .capacity = sizeof(fallback_png),
        .position = 33,
        .valid = true,
    };
    u8 fallback_fdat[24];
    image_test_u32_be(fallback_fdat, 1);
    memcpy(fallback_fdat + 4, image_test_png + 41, 20);
    image_test_png_append_chunk(&fallback_builder, IMAGE_TEST_PNG_CHUNK_TYPE('a', 'c', 'T', 'L'), actl_one, sizeof(actl_one));
    image_test_png_append_chunk(&fallback_builder, IMAGE_TEST_PNG_CHUNK_TYPE('I', 'D', 'A', 'T'), image_test_png + 41, 20);
    image_test_png_append_chunk(&fallback_builder, IMAGE_TEST_PNG_CHUNK_TYPE('f', 'c', 'T', 'L'), fctl, sizeof(fctl));
    image_test_png_append_chunk(&fallback_builder, IMAGE_TEST_PNG_CHUNK_TYPE('f', 'd', 'A', 'T'), fallback_fdat, sizeof(fallback_fdat));
    image_test_png_append_chunk(&fallback_builder, IMAGE_TEST_PNG_CHUNK_TYPE('I', 'E', 'N', 'D'), 0, 0);
    BUSTER_REQUIRE(arguments, fallback_builder.valid);
    ByteSlice fallback_png_bytes = {.pointer = fallback_png, .length = fallback_builder.position};
    ImageProbeResult fallback_probe = image_probe(fallback_png_bytes, png_options);
    BUSTER_TEST(arguments, fallback_probe.status == IMAGE_DECODE_SUCCESS && fallback_probe.information.frame_count == 1 &&
                           fallback_probe.information.is_animated && fallback_probe.information.has_more_images);
    metadata_position = arguments->arena->position;
    ImageDecodeResult fallback_decoded = image_decode(arguments->arena, fallback_png_bytes, png_options);
    if (BUSTER_REQUIRE(arguments, fallback_decoded.status == IMAGE_DECODE_SUCCESS))
    {
        BUSTER_TEST(arguments, image_test_pixels_match(fallback_decoded.image, &image_test_fixtures[0]));
    }
    arena_set_position(arguments->arena, metadata_position);

    // The default-image fcTL and acTL chunks are both constrained to precede
    // IDAT, but have no ordering constraint relative to one another.
    u8 reordered_apng[192] = {0};
    memcpy(reordered_apng, image_test_png, 33);
    ImageTestPngBuilder reordered_builder = {
        .bytes = reordered_apng,
        .capacity = sizeof(reordered_apng),
        .position = 33,
        .valid = true,
    };
    image_test_png_append_chunk(&reordered_builder, IMAGE_TEST_PNG_CHUNK_TYPE('f', 'c', 'T', 'L'), fctl, sizeof(fctl));
    image_test_png_append_chunk(&reordered_builder, IMAGE_TEST_PNG_CHUNK_TYPE('a', 'c', 'T', 'L'), actl_one, sizeof(actl_one));
    image_test_png_append_chunk(&reordered_builder, IMAGE_TEST_PNG_CHUNK_TYPE('I', 'D', 'A', 'T'), image_test_png + 41, 20);
    image_test_png_append_chunk(&reordered_builder, IMAGE_TEST_PNG_CHUNK_TYPE('I', 'E', 'N', 'D'), 0, 0);
    BUSTER_REQUIRE(arguments, reordered_builder.valid);
    ByteSlice reordered_bytes = {.pointer = reordered_apng, .length = reordered_builder.position};
    ImageProbeResult reordered_probe = image_probe(reordered_bytes, png_options);
    BUSTER_TEST(arguments, reordered_probe.status == IMAGE_DECODE_SUCCESS && reordered_probe.information.frame_count == 1 &&
                           reordered_probe.information.is_animated && !reordered_probe.information.has_more_images);
    metadata_position = arguments->arena->position;
    ImageDecodeResult reordered_decoded = image_decode(arguments->arena, reordered_bytes, png_options);
    if (BUSTER_REQUIRE(arguments, reordered_decoded.status == IMAGE_DECODE_SUCCESS))
    {
        BUSTER_TEST(arguments, image_test_pixels_match(reordered_decoded.image, &image_test_fixtures[0]));
    }
    arena_set_position(arguments->arena, metadata_position);

    // Reject an unresolved provisional fcTL and retain sequence validation for
    // the legal pre-acTL placement.
    u8 unresolved_fctl_png[160] = {0};
    memcpy(unresolved_fctl_png, image_test_png, 33);
    ImageTestPngBuilder unresolved_fctl = {
        .bytes = unresolved_fctl_png,
        .capacity = sizeof(unresolved_fctl_png),
        .position = 33,
        .valid = true,
    };
    image_test_png_append_chunk(&unresolved_fctl, IMAGE_TEST_PNG_CHUNK_TYPE('f', 'c', 'T', 'L'), fctl, sizeof(fctl));
    image_test_png_append_chunk(&unresolved_fctl, IMAGE_TEST_PNG_CHUNK_TYPE('I', 'D', 'A', 'T'), image_test_png + 41, 20);
    image_test_png_append_chunk(&unresolved_fctl, IMAGE_TEST_PNG_CHUNK_TYPE('I', 'E', 'N', 'D'), 0, 0);

    u8 bad_reordered_sequence_png[192] = {0};
    memcpy(bad_reordered_sequence_png, image_test_png, 33);
    ImageTestPngBuilder bad_reordered_sequence = {
        .bytes = bad_reordered_sequence_png,
        .capacity = sizeof(bad_reordered_sequence_png),
        .position = 33,
        .valid = true,
    };
    u8 bad_first_fctl[sizeof(fctl)];
    memcpy(bad_first_fctl, fctl, sizeof(bad_first_fctl));
    image_test_u32_be(bad_first_fctl, 1);
    image_test_png_append_chunk(&bad_reordered_sequence, IMAGE_TEST_PNG_CHUNK_TYPE('f', 'c', 'T', 'L'),
                                bad_first_fctl, sizeof(bad_first_fctl));
    image_test_png_append_chunk(&bad_reordered_sequence, IMAGE_TEST_PNG_CHUNK_TYPE('a', 'c', 'T', 'L'), actl_one, sizeof(actl_one));
    image_test_png_append_chunk(&bad_reordered_sequence, IMAGE_TEST_PNG_CHUNK_TYPE('I', 'D', 'A', 'T'), image_test_png + 41, 20);
    image_test_png_append_chunk(&bad_reordered_sequence, IMAGE_TEST_PNG_CHUNK_TYPE('I', 'E', 'N', 'D'), 0, 0);

    // A declared frame count is admitted at acTL, before the parser needs the
    // rest of the stream. The diagnostic points at num_frames itself.
    u8 frame_limited_apng[64] = {0};
    memcpy(frame_limited_apng, image_test_png, 33);
    ImageTestPngBuilder frame_limit_builder = {
        .bytes = frame_limited_apng,
        .capacity = sizeof(frame_limited_apng),
        .position = 33,
        .valid = true,
    };
    u64 frame_count_offset = frame_limit_builder.position + 8u;
    image_test_png_append_chunk(&frame_limit_builder, IMAGE_TEST_PNG_CHUNK_TYPE('a', 'c', 'T', 'L'), actl_two, sizeof(actl_two));
    BUSTER_REQUIRE(arguments, unresolved_fctl.valid && bad_reordered_sequence.valid && frame_limit_builder.valid);
    ByteSlice frame_limited_bytes = {.pointer = frame_limited_apng, .length = frame_limit_builder.position};
    ImageDecodeOptions frame_limit_options = {.format_hint = IMAGE_FORMAT_PNG, .max_frames = 1};
    BUSTER_TEST(arguments, image_test_probe_limit(frame_limited_bytes, frame_limit_options,
                                                  IMAGE_EXCEEDED_LIMIT_FRAMES, frame_count_offset));
    BUSTER_TEST(arguments, image_test_decode_limit(arguments->arena, frame_limited_bytes, frame_limit_options,
                                                   IMAGE_EXCEEDED_LIMIT_FRAMES, frame_count_offset));

    // Known metadata has fixed placement, uniqueness and payload syntax. A
    // declared animation must also account for every frame and fdAT stream.
    u8 gama_after_idat_png[128] = {0};
    memcpy(gama_after_idat_png, image_test_png, 33);
    ImageTestPngBuilder gama_after_idat = {
        .bytes = gama_after_idat_png,
        .capacity = sizeof(gama_after_idat_png),
        .position = 33,
        .valid = true,
    };
    image_test_png_append_chunk(&gama_after_idat, IMAGE_TEST_PNG_CHUNK_TYPE('I', 'D', 'A', 'T'), image_test_png + 41, 20);
    image_test_png_append_chunk(&gama_after_idat, IMAGE_TEST_PNG_CHUNK_TYPE('g', 'A', 'M', 'A'), gama, sizeof(gama));
    image_test_png_append_chunk(&gama_after_idat, IMAGE_TEST_PNG_CHUNK_TYPE('I', 'E', 'N', 'D'), 0, 0);

    u8 duplicate_cicp_png[160] = {0};
    memcpy(duplicate_cicp_png, image_test_png, 33);
    ImageTestPngBuilder duplicate_cicp = {
        .bytes = duplicate_cicp_png,
        .capacity = sizeof(duplicate_cicp_png),
        .position = 33,
        .valid = true,
    };
    image_test_png_append_chunk(&duplicate_cicp, IMAGE_TEST_PNG_CHUNK_TYPE('c', 'I', 'C', 'P'), cicp, sizeof(cicp));
    image_test_png_append_chunk(&duplicate_cicp, IMAGE_TEST_PNG_CHUNK_TYPE('c', 'I', 'C', 'P'), cicp, sizeof(cicp));
    image_test_png_append_chunk(&duplicate_cicp, IMAGE_TEST_PNG_CHUNK_TYPE('I', 'D', 'A', 'T'), image_test_png + 41, 20);
    image_test_png_append_chunk(&duplicate_cicp, IMAGE_TEST_PNG_CHUNK_TYPE('I', 'E', 'N', 'D'), 0, 0);

    u8 incomplete_apng[192] = {0};
    memcpy(incomplete_apng, image_test_png, 33);
    ImageTestPngBuilder incomplete_animation = {
        .bytes = incomplete_apng,
        .capacity = sizeof(incomplete_apng),
        .position = 33,
        .valid = true,
    };
    image_test_png_append_chunk(&incomplete_animation, IMAGE_TEST_PNG_CHUNK_TYPE('a', 'c', 'T', 'L'), actl_two, sizeof(actl_two));
    image_test_png_append_chunk(&incomplete_animation, IMAGE_TEST_PNG_CHUNK_TYPE('f', 'c', 'T', 'L'), fctl, sizeof(fctl));
    image_test_png_append_chunk(&incomplete_animation, IMAGE_TEST_PNG_CHUNK_TYPE('I', 'D', 'A', 'T'), image_test_png + 41, 20);
    image_test_png_append_chunk(&incomplete_animation, IMAGE_TEST_PNG_CHUNK_TYPE('I', 'E', 'N', 'D'), 0, 0);

    BUSTER_REQUIRE(arguments, gama_after_idat.valid && duplicate_cicp.valid && incomplete_animation.valid);
    ByteSlice malformed_metadata_pngs[] = {
        {.pointer = gama_after_idat_png, .length = gama_after_idat.position},
        {.pointer = duplicate_cicp_png, .length = duplicate_cicp.position},
        {.pointer = incomplete_apng, .length = incomplete_animation.position},
        {.pointer = unresolved_fctl_png, .length = unresolved_fctl.position},
        {.pointer = bad_reordered_sequence_png, .length = bad_reordered_sequence.position},
    };
    for (u32 metadata_index = 0; metadata_index < BUSTER_ARRAY_LENGTH(malformed_metadata_pngs); metadata_index += 1)
    {
        ImageProbeResult invalid_metadata_probe = image_probe(malformed_metadata_pngs[metadata_index], png_options);
        BUSTER_TEST(arguments, invalid_metadata_probe.status == IMAGE_DECODE_MALFORMED);
        BUSTER_TEST(arguments, image_test_rejected_without_allocation(arguments->arena, malformed_metadata_pngs[metadata_index],
                                                                      png_options, IMAGE_DECODE_MALFORMED));
    }

    // Each stream has valid PNG CRCs, a valid Adler-32 and the exact expected
    // scanline byte count. Their isolated DEFLATE faults are, respectively,
    // incomplete code-length/literal/distance trees, a multi-entry empty
    // distance alphabet, and a distance beyond the zlib-advertised window.
    ByteSlice malformed_deflate_pngs[] = {
        BUSTER_ARRAY_TO_BYTE_SLICE(image_test_png_incomplete_code_lengths),
        BUSTER_ARRAY_TO_BYTE_SLICE(image_test_png_incomplete_literals),
        BUSTER_ARRAY_TO_BYTE_SLICE(image_test_png_incomplete_distances),
        BUSTER_ARRAY_TO_BYTE_SLICE(image_test_png_multiple_empty_distances),
        BUSTER_ARRAY_TO_BYTE_SLICE(image_test_png_small_window_distance),
    };
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(malformed_deflate_pngs); index += 1)
    {
        BUSTER_TEST(arguments, image_test_rejected_without_allocation(arguments->arena, malformed_deflate_pngs[index], png_options,
                                                                       IMAGE_DECODE_MALFORMED));
    }

    ImageDecodeOptions gif_options = {.format_hint = IMAGE_FORMAT_GIF};

    // Animation counts every validated image descriptor, while alpha describes
    // only the returned first-frame canvas. Make the first animation frame
    // opaque and retain transparency on the second to keep those facts apart.
    u8 gif_later_alpha[sizeof(image_test_gif_animation)];
    memcpy(gif_later_alpha, image_test_gif_animation, sizeof(image_test_gif_animation));
    bool first_control_found = false;
    for (u64 offset = 0; offset + 3u < sizeof(gif_later_alpha) && !first_control_found; offset += 1)
    {
        if (gif_later_alpha[offset] == 0x21 && gif_later_alpha[offset + 1u] == 0xf9 &&
            gif_later_alpha[offset + 2u] == 4)
        {
            gif_later_alpha[offset + 3u] &= 0xfeu;
            first_control_found = true;
        }
    }
    if (BUSTER_REQUIRE(arguments, first_control_found))
    {
        ImageProbeResult later_alpha_probe = image_probe(BUSTER_ARRAY_TO_BYTE_SLICE(gif_later_alpha), gif_options);
        BUSTER_TEST(arguments, later_alpha_probe.status == IMAGE_DECODE_SUCCESS &&
                               later_alpha_probe.information.frame_count == 2 &&
                               later_alpha_probe.information.source_color_model == IMAGE_COLOR_MODEL_INDEXED &&
                               later_alpha_probe.information.is_animated && later_alpha_probe.information.has_more_images &&
                               !later_alpha_probe.information.has_alpha);
    }

    // The initial dictionary is usable before a Clear code. This no-Clear
    // stream contains four literal colors followed directly by EOI.
    u8 gif_no_clear[sizeof(image_test_gif)];
    memcpy(gif_no_clear, image_test_gif, sizeof(gif_no_clear));
    gif_no_clear[36] = 6;
    u8 no_clear_data[] = {0x00, 0x02, 0x08, 0x18, 0x10, 0x10};
    memcpy(gif_no_clear + 37, no_clear_data, sizeof(no_clear_data));
    gif_no_clear[43] = 0;
    gif_no_clear[44] = 0x3b;
    ByteSlice no_clear_gif = {.pointer = gif_no_clear, .length = sizeof(gif_no_clear) - 1};
    ImageProbeResult no_clear_probe = image_probe(no_clear_gif, gif_options);
    BUSTER_TEST(arguments, no_clear_probe.status == IMAGE_DECODE_SUCCESS && !no_clear_probe.information.has_alpha);
    u64 gif_position = arguments->arena->position;
    ImageDecodeResult no_clear_decoded = image_decode(arguments->arena, no_clear_gif, gif_options);
    if (BUSTER_REQUIRE(arguments, no_clear_decoded.status == IMAGE_DECODE_SUCCESS))
    {
        BUSTER_TEST(arguments, no_clear_decoded.image.pixels.length == sizeof(image_test_expected_rgba_opaque) &&
                               !memcmp(no_clear_decoded.image.pixels.pointer, image_test_expected_rgba_opaque,
                                       sizeof(image_test_expected_rgba_opaque)));
    }
    arena_set_position(arguments->arena, gif_position);

    // A nonliteral first code remains invalid even though Clear is optional.
    u8 gif_bad_first_code[sizeof(image_test_gif)];
    memcpy(gif_bad_first_code, image_test_gif, sizeof(gif_bad_first_code));
    gif_bad_first_code[37] = 2;
    ByteSlice bad_first_code = BUSTER_ARRAY_TO_BYTE_SLICE(gif_bad_first_code);
    ImageProbeResult bad_first_probe = image_probe(bad_first_code, gif_options);
    BUSTER_TEST(arguments, bad_first_probe.status == IMAGE_DECODE_MALFORMED);
    BUSTER_TEST(arguments, image_test_rejected_without_allocation(arguments->arena, bad_first_code, gif_options,
                                                                  IMAGE_DECODE_MALFORMED));

    // EOI may leave only sub-byte padding before the zero terminator. Reject
    // both an unread byte in its current subblock and another nonempty subblock.
    u8 gif_unread_byte[sizeof(image_test_gif) + 1];
    memcpy(gif_unread_byte, image_test_gif, 44);
    gif_unread_byte[36] = 8;
    gif_unread_byte[44] = 0xaa;
    gif_unread_byte[45] = 0;
    gif_unread_byte[46] = 0x3b;
    u8 gif_extra_subblock[sizeof(image_test_gif) + 2];
    memcpy(gif_extra_subblock, image_test_gif, 44);
    gif_extra_subblock[44] = 1;
    gif_extra_subblock[45] = 0xaa;
    gif_extra_subblock[46] = 0;
    gif_extra_subblock[47] = 0x3b;
    ByteSlice gif_trailing_data[] = {
        BUSTER_ARRAY_TO_BYTE_SLICE(gif_unread_byte),
        BUSTER_ARRAY_TO_BYTE_SLICE(gif_extra_subblock),
    };
    for (u32 trailing_index = 0; trailing_index < BUSTER_ARRAY_LENGTH(gif_trailing_data); trailing_index += 1)
    {
        ImageProbeResult trailing_probe = image_probe(gif_trailing_data[trailing_index], gif_options);
        BUSTER_TEST(arguments, trailing_probe.status == IMAGE_DECODE_MALFORMED);
        BUSTER_TEST(arguments, image_test_rejected_without_allocation(arguments->arena, gif_trailing_data[trailing_index],
                                                                      gif_options, IMAGE_DECODE_MALFORMED));
    }

    // EOI ends the code stream even when the final byte retains seven zero
    // padding bits. This canonical minimum-code-size-two stream is accepted by
    // independent GIF readers and must not be mistaken for another code.
    ByteSlice gif_minimum_code_size_two = BUSTER_ARRAY_TO_BYTE_SLICE(image_test_gif_minimum_code_size_two);
    ImageProbeResult minimum_code_probe = image_probe(gif_minimum_code_size_two, gif_options);
    BUSTER_TEST(arguments, minimum_code_probe.status == IMAGE_DECODE_SUCCESS &&
                           minimum_code_probe.information.source_color_model == IMAGE_COLOR_MODEL_INDEXED &&
                           minimum_code_probe.information.source_channel_count == 1 &&
                           !minimum_code_probe.information.has_alpha);
    gif_position = arguments->arena->position;
    ImageDecodeResult minimum_code_decoded = image_decode(arguments->arena, gif_minimum_code_size_two, gif_options);
    if (BUSTER_REQUIRE(arguments, minimum_code_decoded.status == IMAGE_DECODE_SUCCESS))
    {
        u8 opaque_red[] = {255, 0, 0, 255};
        BUSTER_TEST(arguments, minimum_code_decoded.image.pixels.length == sizeof(opaque_red) &&
                               !memcmp(minimum_code_decoded.image.pixels.pointer, opaque_red, sizeof(opaque_red)));
    }
    arena_set_position(arguments->arena, gif_position);

    // A transparent index is a compositing no-op even when it is only a
    // sentinel outside the active palette. The invalid background index is
    // deliberately ignored, and the untouched canvas remains transparent black.
    ByteSlice transparent_sentinel_gif = BUSTER_ARRAY_TO_BYTE_SLICE(image_test_gif_transparent_sentinel);
    ImageProbeResult transparent_probe = image_probe(transparent_sentinel_gif, gif_options);
    BUSTER_TEST(arguments, transparent_probe.status == IMAGE_DECODE_SUCCESS && transparent_probe.information.has_alpha);
    gif_position = arguments->arena->position;
    ImageDecodeResult transparent_decoded = image_decode(arguments->arena, transparent_sentinel_gif, gif_options);
    if (BUSTER_REQUIRE(arguments, transparent_decoded.status == IMAGE_DECODE_SUCCESS))
    {
        u8 transparent_black[] = {0, 0, 0, 0};
        BUSTER_TEST(arguments, transparent_decoded.information.has_alpha &&
                               transparent_decoded.image.pixels.length == sizeof(transparent_black) &&
                               !memcmp(transparent_decoded.image.pixels.pointer, transparent_black, sizeof(transparent_black)));
    }
    arena_set_position(arguments->arena, gif_position);

    // A transparency declaration does not add alpha when its index is absent
    // from the fully covering first image. Rewrite the literal to palette zero
    // while retaining the out-of-palette transparency sentinel.
    u8 gif_unused_transparency[sizeof(image_test_gif_transparent_sentinel)];
    memcpy(gif_unused_transparency, image_test_gif_transparent_sentinel, sizeof(image_test_gif_transparent_sentinel));
    gif_unused_transparency[40] = 0x01;
    gif_unused_transparency[41] = 0x04;
    ByteSlice unused_transparency_gif = BUSTER_ARRAY_TO_BYTE_SLICE(gif_unused_transparency);
    ImageProbeResult unused_transparency_probe = image_probe(unused_transparency_gif, gif_options);
    BUSTER_TEST(arguments, unused_transparency_probe.status == IMAGE_DECODE_SUCCESS &&
                           !unused_transparency_probe.information.has_alpha);
    gif_position = arguments->arena->position;
    ImageDecodeResult unused_transparency_decoded = image_decode(arguments->arena, unused_transparency_gif, gif_options);
    if (BUSTER_REQUIRE(arguments, unused_transparency_decoded.status == IMAGE_DECODE_SUCCESS))
    {
        u8 opaque_red[] = {255, 0, 0, 255};
        BUSTER_TEST(arguments, !unused_transparency_decoded.information.has_alpha &&
                               unused_transparency_decoded.image.pixels.length == sizeof(opaque_red) &&
                               !memcmp(unused_transparency_decoded.image.pixels.pointer, opaque_red, sizeof(opaque_red)));
    }
    arena_set_position(arguments->arena, gif_position);

    // The first image may overrun or miss the logical screen. It is fully
    // decoded for validation, while only in-canvas opaque samples composite.
    u8 gif_clipped[sizeof(image_test_gif)];
    memcpy(gif_clipped, image_test_gif, sizeof(gif_clipped));
    gif_clipped[26] = 1;
    gif_clipped[28] = 1;
    ByteSlice clipped_gif = BUSTER_ARRAY_TO_BYTE_SLICE(gif_clipped);
    ImageProbeResult clipped_probe = image_probe(clipped_gif, gif_options);
    BUSTER_TEST(arguments, clipped_probe.status == IMAGE_DECODE_SUCCESS && clipped_probe.information.has_alpha);
    gif_position = arguments->arena->position;
    ImageDecodeResult clipped_decoded = image_decode(arguments->arena, clipped_gif, gif_options);
    if (BUSTER_REQUIRE(arguments, clipped_decoded.status == IMAGE_DECODE_SUCCESS))
    {
        u8 clipped_expected[] = {
            0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 255, 0, 0, 255,
        };
        BUSTER_TEST(arguments, clipped_decoded.information.has_alpha &&
                               clipped_decoded.image.pixels.length == sizeof(clipped_expected) &&
                               !memcmp(clipped_decoded.image.pixels.pointer, clipped_expected, sizeof(clipped_expected)));
    }
    arena_set_position(arguments->arena, gif_position);

    gif_clipped[26] = 2;
    gif_clipped[28] = 2;
    ByteSlice offscreen_gif = BUSTER_ARRAY_TO_BYTE_SLICE(gif_clipped);
    ImageProbeResult offscreen_probe = image_probe(offscreen_gif, gif_options);
    BUSTER_TEST(arguments, offscreen_probe.status == IMAGE_DECODE_SUCCESS && offscreen_probe.information.has_alpha);
    gif_position = arguments->arena->position;
    ImageDecodeResult offscreen_decoded = image_decode(arguments->arena, offscreen_gif, gif_options);
    if (BUSTER_REQUIRE(arguments, offscreen_decoded.status == IMAGE_DECODE_SUCCESS))
    {
        u8 empty_canvas[sizeof(image_test_expected_rgba_opaque)] = {0};
        BUSTER_TEST(arguments, offscreen_decoded.information.has_alpha &&
                               offscreen_decoded.image.pixels.length == sizeof(empty_canvas) &&
                               !memcmp(offscreen_decoded.image.pixels.pointer, empty_canvas, sizeof(empty_canvas)));
    }
    arena_set_position(arguments->arena, gif_position);

    // Plain Text and Application extensions have fixed header-block sizes,
    // unlike the variable data subblocks which follow those headers.
    u8 gif_bad_plain_text[sizeof(image_test_gif) + 15];
    memcpy(gif_bad_plain_text, image_test_gif, 25);
    gif_bad_plain_text[25] = 0x21;
    gif_bad_plain_text[26] = 0x01;
    gif_bad_plain_text[27] = 11;
    memset(gif_bad_plain_text + 28, 0, 12);
    memcpy(gif_bad_plain_text + 40, image_test_gif + 25, sizeof(image_test_gif) - 25);
    u8 gif_bad_application[sizeof(image_test_gif) + 14];
    memcpy(gif_bad_application, image_test_gif, 25);
    gif_bad_application[25] = 0x21;
    gif_bad_application[26] = 0xff;
    gif_bad_application[27] = 10;
    memset(gif_bad_application + 28, 0, 11);
    memcpy(gif_bad_application + 39, image_test_gif + 25, sizeof(image_test_gif) - 25);
    ByteSlice bad_fixed_extensions[] = {
        BUSTER_ARRAY_TO_BYTE_SLICE(gif_bad_plain_text),
        BUSTER_ARRAY_TO_BYTE_SLICE(gif_bad_application),
    };
    for (u32 extension_index = 0; extension_index < BUSTER_ARRAY_LENGTH(bad_fixed_extensions); extension_index += 1)
    {
        ImageProbeResult extension_probe = image_probe(bad_fixed_extensions[extension_index], gif_options);
        BUSTER_TEST(arguments, extension_probe.status == IMAGE_DECODE_MALFORMED);
        BUSTER_TEST(arguments, image_test_rejected_without_allocation(arguments->arena, bad_fixed_extensions[extension_index],
                                                                      gif_options, IMAGE_DECODE_MALFORMED));
    }

    // Packed logical-screen sort and color-resolution fields are data, while
    // GCE and image-descriptor reserved bits must remain zero. The trailer is
    // the exact end of the GIF data stream.
    u8 gif_logical_flags[sizeof(image_test_gif)];
    memcpy(gif_logical_flags, image_test_gif, sizeof(gif_logical_flags));
    gif_logical_flags[10] = 0xf9;
    ByteSlice logical_flags_gif = BUSTER_ARRAY_TO_BYTE_SLICE(gif_logical_flags);
    ImageProbeResult logical_flags_probe = image_probe(logical_flags_gif, gif_options);
    BUSTER_TEST(arguments, logical_flags_probe.status == IMAGE_DECODE_SUCCESS);
    gif_position = arguments->arena->position;
    ImageDecodeResult logical_flags_decoded = image_decode(arguments->arena, logical_flags_gif, gif_options);
    BUSTER_TEST(arguments, logical_flags_decoded.status == IMAGE_DECODE_SUCCESS);
    arena_set_position(arguments->arena, gif_position);

    u8 gif_gce_reserved[sizeof(image_test_gif_transparent_sentinel)];
    memcpy(gif_gce_reserved, image_test_gif_transparent_sentinel, sizeof(gif_gce_reserved));
    gif_gce_reserved[22] |= 0x20u;
    u8 gif_gce_reserved_disposal[sizeof(image_test_gif_transparent_sentinel)];
    memcpy(gif_gce_reserved_disposal, image_test_gif_transparent_sentinel, sizeof(gif_gce_reserved_disposal));
    gif_gce_reserved_disposal[22] |= 0x10u;
    u8 gif_image_reserved[sizeof(image_test_gif)];
    memcpy(gif_image_reserved, image_test_gif, sizeof(gif_image_reserved));
    gif_image_reserved[34] = 0x08;
    u8 gif_after_trailer[sizeof(image_test_gif) + 1];
    memcpy(gif_after_trailer, image_test_gif, sizeof(image_test_gif));
    gif_after_trailer[sizeof(image_test_gif)] = 0;
    ByteSlice bad_gif_framing[] = {
        BUSTER_ARRAY_TO_BYTE_SLICE(gif_gce_reserved),
        BUSTER_ARRAY_TO_BYTE_SLICE(gif_gce_reserved_disposal),
        BUSTER_ARRAY_TO_BYTE_SLICE(gif_image_reserved),
        BUSTER_ARRAY_TO_BYTE_SLICE(gif_after_trailer),
    };
    for (u32 framing_index = 0; framing_index < BUSTER_ARRAY_LENGTH(bad_gif_framing); framing_index += 1)
    {
        ImageProbeResult framing_probe = image_probe(bad_gif_framing[framing_index], gif_options);
        BUSTER_TEST(arguments, framing_probe.status == IMAGE_DECODE_MALFORMED);
        BUSTER_TEST(arguments, image_test_rejected_without_allocation(arguments->arena, bad_gif_framing[framing_index],
                                                                      gif_options, IMAGE_DECODE_MALFORMED));
    }

    // QOI requires the exact eight-byte terminal marker and no trailing data.
    u8 qoi_bad_end[sizeof(image_test_qoi)];
    memcpy(qoi_bad_end, image_test_qoi, sizeof(qoi_bad_end));
    qoi_bad_end[sizeof(qoi_bad_end) - 1] = 0;
    ByteSlice bad_qoi = {.pointer = qoi_bad_end, .length = sizeof(qoi_bad_end)};
    ImageDecodeOptions qoi_options = {.format_hint = IMAGE_FORMAT_QOI};
    ImageProbeResult qoi_probe = image_probe(bad_qoi, qoi_options);
    BUSTER_TEST(arguments, qoi_probe.status == IMAGE_DECODE_MALFORMED);
    BUSTER_TEST(arguments, image_test_rejected_without_allocation(arguments->arena, bad_qoi, qoi_options, IMAGE_DECODE_MALFORMED));
    u8 linear_qoi[sizeof(image_test_qoi)];
    memcpy(linear_qoi, image_test_qoi, sizeof(linear_qoi));
    linear_qoi[13] = 1;
    ImageProbeResult linear_qoi_probe = image_probe(BUSTER_ARRAY_TO_BYTE_SLICE(linear_qoi), qoi_options);
    BUSTER_TEST(arguments, linear_qoi_probe.status == IMAGE_DECODE_SUCCESS &&
                           linear_qoi_probe.information.qoi_colorspace == IMAGE_QOI_COLORSPACE_ALL_CHANNELS_LINEAR &&
                           linear_qoi_probe.information.color_metadata_flags == IMAGE_COLOR_METADATA_NONE);

    // QOI's RGB channel header is purely informative. An explicit RGBA opcode
    // and an index reference to QOI's initially transparent table both retain
    // their alpha, while source layout metadata preserves the header hint.
    u8 qoi_rgb_with_rgba[] = {
        'q', 'o', 'i', 'f', 0, 0, 0, 1, 0, 0, 0, 1, 3, 0,
        0xff, 1, 2, 3, 0,
        0, 0, 0, 0, 0, 0, 0, 1,
    };
    u8 qoi_rgb_with_transparent_index[] = {
        'q', 'o', 'i', 'f', 0, 0, 0, 1, 0, 0, 0, 1, 3, 0,
        0,
        0, 0, 0, 0, 0, 0, 0, 1,
    };
    ByteSlice qoi_rgb_alpha_cases[] = {
        BUSTER_ARRAY_TO_BYTE_SLICE(qoi_rgb_with_rgba),
        BUSTER_ARRAY_TO_BYTE_SLICE(qoi_rgb_with_transparent_index),
    };
    u8 qoi_rgb_alpha_expected[][4] = {
        {1, 2, 3, 0},
        {0, 0, 0, 0},
    };
    for (u32 alpha_case = 0; alpha_case < BUSTER_ARRAY_LENGTH(qoi_rgb_alpha_cases); alpha_case += 1)
    {
        ImageProbeResult alpha_probe = image_probe(qoi_rgb_alpha_cases[alpha_case], qoi_options);
        BUSTER_TEST(arguments, alpha_probe.status == IMAGE_DECODE_SUCCESS && alpha_probe.information.has_alpha &&
                               alpha_probe.information.source_channel_count == 3 &&
                               alpha_probe.information.source_color_model == IMAGE_COLOR_MODEL_RGB);
        u64 alpha_position = arguments->arena->position;
        ImageDecodeResult alpha_decoded = image_decode(arguments->arena, qoi_rgb_alpha_cases[alpha_case], qoi_options);
        BUSTER_TEST(arguments, alpha_decoded.status == IMAGE_DECODE_SUCCESS &&
                               image_test_information_equal(alpha_decoded.information, alpha_probe.information) &&
                               alpha_decoded.image.pixels.length == sizeof(qoi_rgb_alpha_expected[alpha_case]) &&
                               !memcmp(alpha_decoded.image.pixels.pointer, qoi_rgb_alpha_expected[alpha_case],
                                       sizeof(qoi_rgb_alpha_expected[alpha_case])));
        arena_set_position(arguments->arena, alpha_position);
    }

    // A valid end marker cannot substitute for missing opcode bytes. Probe
    // now walks the same stream as decode, and decode failure rolls back the
    // pixel allocation made before that walk.
    u8 qoi_short_alpha_stream[] = {
        'q', 'o', 'i', 'f', 0, 0, 0, 2, 0, 0, 0, 1, 3, 0,
        0xff, 1, 2, 3, 0,
        0, 0, 0, 0, 0, 0, 0, 1,
    };
    ByteSlice short_alpha_qoi = BUSTER_ARRAY_TO_BYTE_SLICE(qoi_short_alpha_stream);
    ImageProbeResult short_alpha_probe = image_probe(short_alpha_qoi, qoi_options);
    BUSTER_TEST(arguments, short_alpha_probe.status == IMAGE_DECODE_MALFORMED &&
                           short_alpha_probe.error_offset == 20);
    u64 short_alpha_position = arguments->arena->position;
    ImageDecodeResult short_alpha_decoded = image_decode(arguments->arena, short_alpha_qoi, qoi_options);
    BUSTER_TEST(arguments, short_alpha_decoded.status == IMAGE_DECODE_MALFORMED &&
                           short_alpha_decoded.error_offset == 20 && image_test_image_empty(short_alpha_decoded.image) &&
                           arguments->arena->position == short_alpha_position);

    // TGA is a last-resort heuristic: type-specific color-map fields and the
    // complete pixel envelope must agree before an unsigned file is claimed.
    u8 inconsistent_tga[] = {0, 0, 1, 0, 0, 1, 0, 24, 0, 0, 0, 0, 1, 0, 1, 0, 8, 0, 0};
    BUSTER_TEST(arguments, image_detect_format(BUSTER_ARRAY_TO_BYTE_SLICE(inconsistent_tga)) == IMAGE_FORMAT_UNKNOWN);

    // The TGA 2.0 extension distinguishes absent, ignorable, retained, useful
    // and premultiplied attribute data. Only the last cannot satisfy the
    // public straight-alpha output contract.
    static u8 const tga_footer_signature[] = "TRUEVISION-XFILE.";
    u8 attributes_tga[543] = {0};
    attributes_tga[2] = 2;
    attributes_tga[12] = 1;
    attributes_tga[14] = 1;
    attributes_tga[16] = 32;
    attributes_tga[17] = 8;
    attributes_tga[18] = 10;
    attributes_tga[19] = 20;
    attributes_tga[20] = 30;
    attributes_tga[21] = 40;
    attributes_tga[22] = 0xef;
    attributes_tga[23] = 1;
    attributes_tga[516] = 4;
    attributes_tga[517] = 22;
    memcpy(attributes_tga + 525, tga_footer_signature, sizeof(tga_footer_signature));
    ByteSlice attributes_tga_bytes = BUSTER_ARRAY_TO_BYTE_SLICE(attributes_tga);
    ImageDecodeOptions tga_options = {.format_hint = IMAGE_FORMAT_TGA};
    u8 straight_attribute_types[] = {0, 1, 1, 2, 3};
    u8 straight_attribute_bits[] = {0, 0, 8, 8, 8};
    bool straight_has_alpha[] = {false, false, false, true, true};
    u64 tga_position = arguments->arena->position;
    for (u32 attribute_index = 0; attribute_index < BUSTER_ARRAY_LENGTH(straight_attribute_types); attribute_index += 1)
    {
        attributes_tga[17] = straight_attribute_bits[attribute_index];
        attributes_tga[516] = straight_attribute_types[attribute_index];
        BUSTER_TEST(arguments, image_detect_format(attributes_tga_bytes) == IMAGE_FORMAT_TGA);
        ImageProbeResult attribute_probe = image_probe(attributes_tga_bytes, tga_options);
        BUSTER_TEST(arguments, attribute_probe.status == IMAGE_DECODE_SUCCESS &&
                               attribute_probe.information.source_channel_count == (straight_has_alpha[attribute_index] ? 4u : 3u) &&
                               attribute_probe.information.has_alpha == straight_has_alpha[attribute_index] &&
                               attribute_probe.information.source_color_model ==
                                   (straight_has_alpha[attribute_index] ? IMAGE_COLOR_MODEL_RGBA : IMAGE_COLOR_MODEL_RGB));
        ImageDecodeResult attribute_decode = image_decode(arguments->arena, attributes_tga_bytes, tga_options);
        if (BUSTER_REQUIRE(arguments, attribute_decode.status == IMAGE_DECODE_SUCCESS))
        {
            u8 expected[] = {30, 20, 10, straight_has_alpha[attribute_index] ? 40u : 255u};
            BUSTER_TEST(arguments, attribute_decode.image.pixels.length == sizeof(expected) &&
                                   !memcmp(attribute_decode.image.pixels.pointer, expected, sizeof(expected)));
        }
        arena_set_position(arguments->arena, tga_position);
    }

    u8 contradictory_attribute_types[] = {0, 3};
    u8 contradictory_attribute_bits[] = {8, 0};
    for (u32 contradiction_index = 0; contradiction_index < BUSTER_ARRAY_LENGTH(contradictory_attribute_types);
         contradiction_index += 1)
    {
        attributes_tga[17] = contradictory_attribute_bits[contradiction_index];
        attributes_tga[516] = contradictory_attribute_types[contradiction_index];
        BUSTER_TEST(arguments, image_detect_format(attributes_tga_bytes) == IMAGE_FORMAT_UNKNOWN);
        ImageProbeResult contradiction_probe = image_probe(attributes_tga_bytes, tga_options);
        BUSTER_TEST(arguments, contradiction_probe.status == IMAGE_DECODE_MALFORMED && contradiction_probe.error_offset == 17);
        BUSTER_TEST(arguments, image_test_rejected_without_allocation(arguments->arena, attributes_tga_bytes, tga_options,
                                                                      IMAGE_DECODE_MALFORMED));
    }

    attributes_tga[17] = 8;
    attributes_tga[516] = 4;
    BUSTER_TEST(arguments, image_detect_format(attributes_tga_bytes) == IMAGE_FORMAT_TGA);
    ImageProbeResult premultiplied_probe = image_probe(attributes_tga_bytes, tga_options);
    BUSTER_TEST(arguments, premultiplied_probe.status == IMAGE_DECODE_UNSUPPORTED_FEATURE &&
                           premultiplied_probe.unsupported_feature == IMAGE_UNSUPPORTED_FEATURE_COLOR_TRANSFORM &&
                           premultiplied_probe.error_offset == 516);
    BUSTER_TEST(arguments, image_test_rejected_without_allocation(arguments->arena, attributes_tga_bytes, tga_options,
                                                                   IMAGE_DECODE_UNSUPPORTED_FEATURE));
    attributes_tga[22] = 0xee;
    BUSTER_TEST(arguments, image_detect_format(attributes_tga_bytes) == IMAGE_FORMAT_UNKNOWN);
    ImageProbeResult bad_footer_probe = image_probe(attributes_tga_bytes, tga_options);
    BUSTER_TEST(arguments, bad_footer_probe.status == IMAGE_DECODE_MALFORMED);

    // TGA 2.0 does not require offset-addressed areas to be disjoint. A
    // zero-entry Developer Directory inside the fixed Extension Area is
    // independently bounded and must not hide the complete preceding raster.
    enum
    {
        tga_alias_extension_offset = 21,
        tga_alias_developer_offset = 23,
        tga_alias_footer_offset = tga_alias_extension_offset + 495,
        tga_alias_size = tga_alias_footer_offset + 26,
    };
    u8 tga_alias[tga_alias_size] = {0};
    tga_alias[2] = 2;
    tga_alias[12] = 1;
    tga_alias[14] = 1;
    tga_alias[16] = 24;
    tga_alias[18] = 30;
    tga_alias[19] = 20;
    tga_alias[20] = 10;
    tga_alias[tga_alias_extension_offset] = 0xef;
    tga_alias[tga_alias_extension_offset + 1] = 1;
    image_test_u32_le(tga_alias + tga_alias_footer_offset, tga_alias_extension_offset);
    image_test_u32_le(tga_alias + tga_alias_footer_offset + 4, tga_alias_developer_offset);
    memcpy(tga_alias + tga_alias_footer_offset + 8, tga_footer_signature, sizeof(tga_footer_signature));
    ByteSlice tga_alias_bytes = BUSTER_ARRAY_TO_BYTE_SLICE(tga_alias);
    BUSTER_TEST(arguments, image_detect_format(tga_alias_bytes) == IMAGE_FORMAT_TGA);
    ImageProbeResult tga_alias_probe = image_probe(tga_alias_bytes, tga_options);
    BUSTER_TEST(arguments, tga_alias_probe.status == IMAGE_DECODE_SUCCESS);
    u64 tga_alias_position = arguments->arena->position;
    ImageDecodeResult tga_alias_decode = image_decode(arguments->arena, tga_alias_bytes, tga_options);
    u8 expected_tga_alias_pixel[] = {10, 20, 30, 255};
    BUSTER_TEST(arguments, tga_alias_decode.status == IMAGE_DECODE_SUCCESS &&
                           tga_alias_decode.image.pixels.length == sizeof(expected_tga_alias_pixel) &&
                           !memcmp(tga_alias_decode.image.pixels.pointer, expected_tga_alias_pixel,
                                   sizeof(expected_tga_alias_pixel)));
    arena_set_position(arguments->arena, tga_alias_position);

    // Every nonempty TGA 2.0 auxiliary payload is a logical end of the image
    // stream. A valid table after the raster must not be mistaken for pixels,
    // while a table that starts before the raster is complete exposes a
    // truncated image at that exact boundary.
    enum
    {
        tga_scan_pixel_offset = 18,
        tga_scan_table_offset = 24,
        tga_scan_extension_offset = 28,
        tga_scan_footer_offset = tga_scan_extension_offset + 495,
        tga_scan_size = tga_scan_footer_offset + 26,
    };
    u8 tga_scan_table[tga_scan_size] = {0};
    tga_scan_table[2] = 2;
    tga_scan_table[12] = 2;
    tga_scan_table[14] = 1;
    tga_scan_table[16] = 24;
    tga_scan_table[18] = 30;
    tga_scan_table[19] = 20;
    tga_scan_table[20] = 10;
    tga_scan_table[21] = 60;
    tga_scan_table[22] = 50;
    tga_scan_table[23] = 40;
    image_test_u32_le(tga_scan_table + tga_scan_table_offset, tga_scan_pixel_offset);
    tga_scan_table[tga_scan_extension_offset] = 0xef;
    tga_scan_table[tga_scan_extension_offset + 1] = 1;
    image_test_u32_le(tga_scan_table + tga_scan_extension_offset + 490, tga_scan_table_offset);
    image_test_u32_le(tga_scan_table + tga_scan_footer_offset, tga_scan_extension_offset);
    memcpy(tga_scan_table + tga_scan_footer_offset + 8, tga_footer_signature, sizeof(tga_footer_signature));
    ByteSlice tga_scan_bytes = BUSTER_ARRAY_TO_BYTE_SLICE(tga_scan_table);
    BUSTER_TEST(arguments, image_detect_format(tga_scan_bytes) == IMAGE_FORMAT_TGA);
    ImageProbeResult tga_scan_probe = image_probe(tga_scan_bytes, tga_options);
    BUSTER_TEST(arguments, tga_scan_probe.status == IMAGE_DECODE_SUCCESS);
    u64 tga_auxiliary_position = arguments->arena->position;
    ImageDecodeResult tga_scan_decode = image_decode(arguments->arena, tga_scan_bytes, tga_options);
    u8 expected_tga_scan_pixels[] = {10, 20, 30, 255, 40, 50, 60, 255};
    BUSTER_TEST(arguments, tga_scan_decode.status == IMAGE_DECODE_SUCCESS &&
                           tga_scan_decode.image.pixels.length == sizeof(expected_tga_scan_pixels) &&
                           !memcmp(tga_scan_decode.image.pixels.pointer, expected_tga_scan_pixels,
                                   sizeof(expected_tga_scan_pixels)));
    arena_set_position(arguments->arena, tga_auxiliary_position);

    image_test_u32_le(tga_scan_table + tga_scan_extension_offset + 490, 21);
    BUSTER_TEST(arguments, image_detect_format(tga_scan_bytes) == IMAGE_FORMAT_UNKNOWN);
    tga_scan_probe = image_probe(tga_scan_bytes, tga_options);
    BUSTER_TEST(arguments, tga_scan_probe.status == IMAGE_DECODE_TRUNCATED && tga_scan_probe.error_offset == 21);
    BUSTER_TEST(arguments, image_test_rejected_without_allocation(arguments->arena, tga_scan_bytes, tga_options,
                                                                   IMAGE_DECODE_TRUNCATED));
    image_test_u32_le(tga_scan_table + tga_scan_extension_offset + 490, tga_scan_table_offset);
    image_test_u32_le(tga_scan_table + tga_scan_extension_offset + 486, tga_scan_table_offset);
    tga_scan_probe = image_probe(tga_scan_bytes, tga_options);
    BUSTER_TEST(arguments, tga_scan_probe.status == IMAGE_DECODE_MALFORMED &&
                           tga_scan_probe.error_offset == tga_scan_extension_offset + 486);
    image_test_u32_le(tga_scan_table + tga_scan_extension_offset + 486, 0);
    image_test_u32_le(tga_scan_table + tga_scan_extension_offset + 490, tga_scan_footer_offset - 3);
    tga_scan_probe = image_probe(tga_scan_bytes, tga_options);
    BUSTER_TEST(arguments, tga_scan_probe.status == IMAGE_DECODE_MALFORMED &&
                           tga_scan_probe.error_offset == tga_scan_extension_offset + 490);
    BUSTER_TEST(arguments, image_test_rejected_without_allocation(arguments->arena, tga_scan_bytes, tga_options,
                                                                   IMAGE_DECODE_MALFORMED));

    enum
    {
        tga_correction_offset = 21,
        tga_correction_extension_offset = tga_correction_offset + 2048,
        tga_correction_footer_offset = tga_correction_extension_offset + 495,
        tga_correction_size = tga_correction_footer_offset + 26,
    };
    u8 tga_color_correction[tga_correction_size] = {0};
    tga_color_correction[2] = 2;
    tga_color_correction[12] = 1;
    tga_color_correction[14] = 1;
    tga_color_correction[16] = 24;
    tga_color_correction[18] = 30;
    tga_color_correction[19] = 20;
    tga_color_correction[20] = 10;
    tga_color_correction[tga_correction_extension_offset] = 0xef;
    tga_color_correction[tga_correction_extension_offset + 1] = 1;
    image_test_u32_le(tga_color_correction + tga_correction_extension_offset + 482, tga_correction_offset);
    image_test_u32_le(tga_color_correction + tga_correction_footer_offset, tga_correction_extension_offset);
    memcpy(tga_color_correction + tga_correction_footer_offset + 8, tga_footer_signature, sizeof(tga_footer_signature));
    ByteSlice tga_color_correction_bytes = BUSTER_ARRAY_TO_BYTE_SLICE(tga_color_correction);
    BUSTER_TEST(arguments, image_detect_format(tga_color_correction_bytes) == IMAGE_FORMAT_TGA);
    ImageProbeResult tga_correction_probe = image_probe(tga_color_correction_bytes, tga_options);
    BUSTER_TEST(arguments, tga_correction_probe.status == IMAGE_DECODE_SUCCESS);
    image_test_u32_le(tga_color_correction + tga_correction_extension_offset + 482, 20);
    tga_correction_probe = image_probe(tga_color_correction_bytes, tga_options);
    BUSTER_TEST(arguments, tga_correction_probe.status == IMAGE_DECODE_TRUNCATED && tga_correction_probe.error_offset == 20);
    BUSTER_TEST(arguments, image_test_rejected_without_allocation(arguments->arena, tga_color_correction_bytes, tga_options,
                                                                   IMAGE_DECODE_TRUNCATED));
    image_test_u32_le(tga_color_correction + tga_correction_extension_offset + 482,
                      tga_correction_footer_offset - 2047);
    tga_correction_probe = image_probe(tga_color_correction_bytes, tga_options);
    BUSTER_TEST(arguments, tga_correction_probe.status == IMAGE_DECODE_MALFORMED &&
                           tga_correction_probe.error_offset == tga_correction_extension_offset + 482);

    enum
    {
        tga_stamp_offset = 21,
        tga_stamp_extension_offset = 26,
        tga_stamp_footer_offset = tga_stamp_extension_offset + 495,
        tga_stamp_size = tga_stamp_footer_offset + 26,
    };
    u8 tga_postage_stamp[tga_stamp_size] = {0};
    tga_postage_stamp[2] = 2;
    tga_postage_stamp[12] = 1;
    tga_postage_stamp[14] = 1;
    tga_postage_stamp[16] = 24;
    tga_postage_stamp[18] = 30;
    tga_postage_stamp[19] = 20;
    tga_postage_stamp[20] = 1;
    tga_postage_stamp[21] = 1;
    tga_postage_stamp[22] = 1;
    tga_postage_stamp[23] = 3;
    tga_postage_stamp[24] = 2;
    tga_postage_stamp[25] = 1;
    tga_postage_stamp[tga_stamp_extension_offset] = 0xef;
    tga_postage_stamp[tga_stamp_extension_offset + 1] = 1;
    image_test_u32_le(tga_postage_stamp + tga_stamp_extension_offset + 486, tga_stamp_offset);
    image_test_u32_le(tga_postage_stamp + tga_stamp_footer_offset, tga_stamp_extension_offset);
    memcpy(tga_postage_stamp + tga_stamp_footer_offset + 8, tga_footer_signature, sizeof(tga_footer_signature));
    ByteSlice tga_postage_stamp_bytes = BUSTER_ARRAY_TO_BYTE_SLICE(tga_postage_stamp);
    BUSTER_TEST(arguments, image_detect_format(tga_postage_stamp_bytes) == IMAGE_FORMAT_TGA);
    ImageProbeResult tga_stamp_probe = image_probe(tga_postage_stamp_bytes, tga_options);
    BUSTER_TEST(arguments, tga_stamp_probe.status == IMAGE_DECODE_SUCCESS);
    tga_postage_stamp[tga_stamp_offset] = 0;
    tga_postage_stamp[tga_stamp_offset + 1] = 0;
    tga_stamp_probe = image_probe(tga_postage_stamp_bytes, tga_options);
    BUSTER_TEST(arguments, tga_stamp_probe.status == IMAGE_DECODE_SUCCESS);
    tga_postage_stamp[tga_stamp_offset] = 1;
    tga_postage_stamp[tga_stamp_offset + 1] = 1;
    image_test_u32_le(tga_postage_stamp + tga_stamp_extension_offset + 486, 20);
    tga_stamp_probe = image_probe(tga_postage_stamp_bytes, tga_options);
    BUSTER_TEST(arguments, tga_stamp_probe.status == IMAGE_DECODE_TRUNCATED && tga_stamp_probe.error_offset == 20);
    BUSTER_TEST(arguments, image_test_rejected_without_allocation(arguments->arena, tga_postage_stamp_bytes, tga_options,
                                                                   IMAGE_DECODE_TRUNCATED));
    image_test_u32_le(tga_postage_stamp + tga_stamp_extension_offset + 486, tga_stamp_footer_offset - 1);
    tga_stamp_probe = image_probe(tga_postage_stamp_bytes, tga_options);
    BUSTER_TEST(arguments, tga_stamp_probe.status == IMAGE_DECODE_MALFORMED &&
                           tga_stamp_probe.error_offset == tga_stamp_extension_offset + 486);

    enum
    {
        tga_developer_payload_offset = 21,
        tga_developer_directory_offset = 24,
        tga_developer_footer_offset = 36,
        tga_developer_size = tga_developer_footer_offset + 26,
    };
    u8 tga_developer_payload[tga_developer_size] = {0};
    tga_developer_payload[2] = 2;
    tga_developer_payload[12] = 1;
    tga_developer_payload[14] = 1;
    tga_developer_payload[16] = 24;
    tga_developer_payload[18] = 30;
    tga_developer_payload[19] = 20;
    tga_developer_payload[20] = 10;
    tga_developer_payload[21] = 1;
    tga_developer_payload[22] = 2;
    tga_developer_payload[23] = 3;
    tga_developer_payload[tga_developer_directory_offset] = 1;
    tga_developer_payload[tga_developer_directory_offset + 2] = 1;
    image_test_u32_le(tga_developer_payload + tga_developer_directory_offset + 4, tga_developer_payload_offset);
    image_test_u32_le(tga_developer_payload + tga_developer_directory_offset + 8, 3);
    image_test_u32_le(tga_developer_payload + tga_developer_footer_offset + 4, tga_developer_directory_offset);
    memcpy(tga_developer_payload + tga_developer_footer_offset + 8, tga_footer_signature, sizeof(tga_footer_signature));
    ByteSlice tga_developer_payload_bytes = BUSTER_ARRAY_TO_BYTE_SLICE(tga_developer_payload);
    BUSTER_TEST(arguments, image_detect_format(tga_developer_payload_bytes) == IMAGE_FORMAT_TGA);
    ImageProbeResult tga_developer_probe = image_probe(tga_developer_payload_bytes, tga_options);
    BUSTER_TEST(arguments, tga_developer_probe.status == IMAGE_DECODE_SUCCESS);
    image_test_u32_le(tga_developer_payload + tga_developer_directory_offset + 4, 20);
    tga_developer_probe = image_probe(tga_developer_payload_bytes, tga_options);
    BUSTER_TEST(arguments, tga_developer_probe.status == IMAGE_DECODE_TRUNCATED && tga_developer_probe.error_offset == 20);
    BUSTER_TEST(arguments, image_test_rejected_without_allocation(arguments->arena, tga_developer_payload_bytes, tga_options,
                                                                   IMAGE_DECODE_TRUNCATED));
    image_test_u32_le(tga_developer_payload + tga_developer_directory_offset + 4, 22);
    tga_developer_probe = image_probe(tga_developer_payload_bytes, tga_options);
    BUSTER_TEST(arguments, tga_developer_probe.status == IMAGE_DECODE_SUCCESS);
    u64 tga_developer_position = arguments->arena->position;
    ImageDecodeResult tga_developer_decode = image_decode(arguments->arena, tga_developer_payload_bytes, tga_options);
    u8 expected_tga_developer_pixel[] = {10, 20, 30, 255};
    BUSTER_TEST(arguments, tga_developer_decode.status == IMAGE_DECODE_SUCCESS &&
                           tga_developer_decode.image.pixels.length == sizeof(expected_tga_developer_pixel) &&
                           !memcmp(tga_developer_decode.image.pixels.pointer, expected_tga_developer_pixel,
                                   sizeof(expected_tga_developer_pixel)));
    arena_set_position(arguments->arena, tga_developer_position);
    image_test_u32_le(tga_developer_payload + tga_developer_directory_offset + 4, 0);
    image_test_u32_le(tga_developer_payload + tga_developer_directory_offset + 8, 0);
    tga_developer_probe = image_probe(tga_developer_payload_bytes, tga_options);
    BUSTER_TEST(arguments, tga_developer_probe.status == IMAGE_DECODE_SUCCESS);

    enum
    {
        tga_developer_work_directory_offset = 21,
        tga_developer_work_footer_offset = 43,
        tga_developer_work_size = tga_developer_work_footer_offset + 26,
    };
    u8 tga_developer_work[tga_developer_work_size] = {0};
    tga_developer_work[2] = 2;
    tga_developer_work[12] = 1;
    tga_developer_work[14] = 1;
    tga_developer_work[16] = 24;
    tga_developer_work[18] = 30;
    tga_developer_work[19] = 20;
    tga_developer_work[20] = 10;
    tga_developer_work[tga_developer_work_directory_offset] = 2;
    image_test_u32_le(tga_developer_work + tga_developer_work_footer_offset + 4,
                      tga_developer_work_directory_offset);
    memcpy(tga_developer_work + tga_developer_work_footer_offset + 8, tga_footer_signature,
           sizeof(tga_footer_signature));
    ByteSlice tga_developer_work_bytes = BUSTER_ARRAY_TO_BYTE_SLICE(tga_developer_work);
    BUSTER_TEST(arguments, image_detect_format(tga_developer_work_bytes) == IMAGE_FORMAT_TGA);
    ImageDecodeOptions tga_work_options = {.format_hint = IMAGE_FORMAT_TGA, .max_work = 1};
    ImageProbeResult tga_work_probe = image_probe(tga_developer_work_bytes, tga_work_options);
    BUSTER_TEST(arguments, tga_work_probe.status == IMAGE_DECODE_LIMIT_EXCEEDED &&
                           tga_work_probe.exceeded_limit == IMAGE_EXCEEDED_LIMIT_WORK &&
                           tga_work_probe.observed_value == 2 && tga_work_probe.limit_value == 1 &&
                           tga_work_probe.error_offset == tga_developer_work_directory_offset + 12);
    u64 tga_work_position = arguments->arena->position;
    ImageDecodeResult tga_work_decode = image_decode(arguments->arena, tga_developer_work_bytes, tga_work_options);
    BUSTER_TEST(arguments, tga_work_decode.status == IMAGE_DECODE_LIMIT_EXCEEDED &&
                           tga_work_decode.exceeded_limit == IMAGE_EXCEEDED_LIMIT_WORK &&
                           tga_work_decode.observed_value == 2 && tga_work_decode.limit_value == 1 &&
                           tga_work_decode.error_offset == tga_developer_work_directory_offset + 12 &&
                           image_test_image_empty(tga_work_decode.image) && arguments->arena->position == tga_work_position);

    u8 unsupported_tga[sizeof(image_test_tga)];
    ImageUnsupportedFeature tga_features[] = {
        IMAGE_UNSUPPORTED_FEATURE_CODING,
        IMAGE_UNSUPPORTED_FEATURE_INTERLACE,
        IMAGE_UNSUPPORTED_FEATURE_PRECISION,
        IMAGE_UNSUPPORTED_FEATURE_COLOR_TRANSFORM,
    };
    u64 tga_feature_offsets[] = {2, 17, 16, 17};
    for (u32 feature_index = 0; feature_index < BUSTER_ARRAY_LENGTH(tga_features); feature_index += 1)
    {
        memcpy(unsupported_tga, image_test_tga, sizeof(unsupported_tga));
        if (feature_index == 0)
        {
            unsupported_tga[2] = 32;
        }
        else if (feature_index == 1)
        {
            unsupported_tga[17] |= 0x40u;
        }
        else if (feature_index == 2)
        {
            unsupported_tga[16] = 12;
        }
        else
        {
            unsupported_tga[17] = (unsupported_tga[17] & 0xf0u) | 4u;
        }
        ImageProbeResult unsupported_tga_probe = image_probe(BUSTER_ARRAY_TO_BYTE_SLICE(unsupported_tga), tga_options);
        BUSTER_TEST(arguments, unsupported_tga_probe.status == IMAGE_DECODE_UNSUPPORTED_FEATURE &&
                               unsupported_tga_probe.unsupported_feature == tga_features[feature_index] &&
                               unsupported_tga_probe.error_offset == tga_feature_offsets[feature_index]);
    }

    // PAM sample meaning is defined only by a recognized tuple type. DEPTH by
    // itself cannot distinguish visual channels from arbitrary application data.
    u8 pam_without_tuple[] = "P7\nWIDTH 1\nHEIGHT 1\nDEPTH 3\nMAXVAL 255\nENDHDR\n\0\0\0";
    ByteSlice pam_without_tuple_bytes = {.pointer = pam_without_tuple, .length = sizeof(pam_without_tuple) - 1u};
    ImageDecodeOptions pnm_options = {.format_hint = IMAGE_FORMAT_PNM};
    ImageProbeResult tuple_probe = image_probe(pam_without_tuple_bytes, pnm_options);
    BUSTER_TEST(arguments, tuple_probe.status == IMAGE_DECODE_UNSUPPORTED_FEATURE &&
                           tuple_probe.unsupported_feature == IMAGE_UNSUPPORTED_FEATURE_COMPONENT_MODEL);
    BUSTER_TEST(arguments, image_test_rejected_without_allocation(arguments->arena, pam_without_tuple_bytes, pnm_options,
                                                                   IMAGE_DECODE_UNSUPPORTED_FEATURE));

    u8 pam_unknown_key[] = "P7\nWIDTH 1\nHEIGHT 1\nDEPTH 3\nMAXVAL 255\nTUPLTYPE RGB\nVENDOR 1\nENDHDR\n\0\0\0";
    ByteSlice pam_unknown_key_bytes = {.pointer = pam_unknown_key, .length = sizeof(pam_unknown_key) - 1u};
    ImageProbeResult unknown_key_probe = image_probe(pam_unknown_key_bytes, pnm_options);
    BUSTER_TEST(arguments, unknown_key_probe.status == IMAGE_DECODE_UNSUPPORTED_FEATURE &&
                           unknown_key_probe.unsupported_feature == IMAGE_UNSUPPORTED_FEATURE_CONTAINER_FEATURE &&
                           unknown_key_probe.error_offset == 52);

    // Exhausting work while consuming the PAM magic line returns an empty
    // internal line slice. Trimming that failure result must remain defined,
    // preserve the first limit diagnostic and leave decode transactional.
    u8 pam_work_limited[] = "P7 \n";
    ByteSlice pam_work_limited_bytes = {.pointer = pam_work_limited, .length = sizeof(pam_work_limited) - 1u};
    ImageDecodeOptions pam_work_options = {.format_hint = IMAGE_FORMAT_PNM, .max_work = 1};
    ImageProbeResult pam_work_probe = image_probe(pam_work_limited_bytes, pam_work_options);
    BUSTER_TEST(arguments, pam_work_probe.status == IMAGE_DECODE_LIMIT_EXCEEDED &&
                           pam_work_probe.exceeded_limit == IMAGE_EXCEEDED_LIMIT_WORK &&
                           pam_work_probe.observed_value == 2 && pam_work_probe.limit_value == 1 &&
                           pam_work_probe.error_offset == 3);
    u64 pam_work_position = arguments->arena->position;
    ImageDecodeResult pam_work_decode = image_decode(arguments->arena, pam_work_limited_bytes, pam_work_options);
    BUSTER_TEST(arguments, pam_work_decode.status == IMAGE_DECODE_LIMIT_EXCEEDED &&
                           pam_work_decode.exceeded_limit == IMAGE_EXCEEDED_LIMIT_WORK &&
                           pam_work_decode.observed_value == 2 && pam_work_decode.limit_value == 1 &&
                           pam_work_decode.error_offset == 3 && image_test_image_empty(pam_work_decode.image) &&
                           arguments->arena->position == pam_work_position);

    // PNM streams may concatenate images without a container. Both ASCII and
    // binary first rasters stop at their exact boundary and report the suffix.
    u8 concatenated_p6[] = "P6\n1 1\n255\n" "\x01\x02\x03" "P5\n1 1\n255\n" "\x04";
    ByteSlice concatenated_p6_bytes = {.pointer = concatenated_p6, .length = sizeof(concatenated_p6) - 1u};
    ImageProbeResult concatenated_p6_probe = image_probe(concatenated_p6_bytes, pnm_options);
    BUSTER_TEST(arguments, concatenated_p6_probe.status == IMAGE_DECODE_SUCCESS &&
                           concatenated_p6_probe.information.width == 1 && concatenated_p6_probe.information.height == 1 &&
                           concatenated_p6_probe.information.frame_count == 1 &&
                           concatenated_p6_probe.information.has_more_images);
    u64 concatenated_p6_position = arguments->arena->position;
    ImageDecodeResult concatenated_p6_decode = image_decode(arguments->arena, concatenated_p6_bytes, pnm_options);
    u8 const concatenated_p6_expected[] = {1, 2, 3, 255};
    BUSTER_TEST(arguments, concatenated_p6_decode.status == IMAGE_DECODE_SUCCESS &&
                           concatenated_p6_decode.information.frame_count == 1 &&
                           concatenated_p6_decode.information.has_more_images &&
                           concatenated_p6_decode.image.pixels.length == sizeof(concatenated_p6_expected) &&
                           !memcmp(concatenated_p6_decode.image.pixels.pointer, concatenated_p6_expected,
                                   sizeof(concatenated_p6_expected)));
    arena_set_position(arguments->arena, concatenated_p6_position);

    ImageDecodeOptions pnm_frame_limit = {.format_hint = IMAGE_FORMAT_PNM, .max_frames = 1};
    ImageProbeResult concatenated_p6_limited_probe = image_probe(concatenated_p6_bytes, pnm_frame_limit);
    BUSTER_TEST(arguments, concatenated_p6_limited_probe.status == IMAGE_DECODE_LIMIT_EXCEEDED &&
                           concatenated_p6_limited_probe.exceeded_limit == IMAGE_EXCEEDED_LIMIT_FRAMES &&
                           concatenated_p6_limited_probe.observed_value == 2 && concatenated_p6_limited_probe.limit_value == 1 &&
                           concatenated_p6_limited_probe.error_offset == 14);
    BUSTER_TEST(arguments, image_test_rejected_without_allocation(arguments->arena, concatenated_p6_bytes, pnm_frame_limit,
                                                                   IMAGE_DECODE_LIMIT_EXCEEDED));

    u8 concatenated_p2[] = "P2\n1 1\n255\n17\n# next image\nP1\n1 1\n0\n";
    ByteSlice concatenated_p2_bytes = {.pointer = concatenated_p2, .length = sizeof(concatenated_p2) - 1u};
    ImageProbeResult concatenated_p2_probe = image_probe(concatenated_p2_bytes, pnm_options);
    BUSTER_TEST(arguments,
                concatenated_p2_probe.status == IMAGE_DECODE_SUCCESS &&
                    concatenated_p2_probe.information.frame_count == 1 && concatenated_p2_probe.information.has_more_images);
    u64 concatenated_p2_position = arguments->arena->position;
    ImageDecodeResult concatenated_p2_decode = image_decode(arguments->arena, concatenated_p2_bytes, pnm_options);
    u8 const concatenated_p2_expected[] = {17, 17, 17, 255};
    BUSTER_TEST(arguments, concatenated_p2_decode.status == IMAGE_DECODE_SUCCESS &&
                           concatenated_p2_decode.information.frame_count == 1 &&
                           concatenated_p2_decode.information.has_more_images &&
                           concatenated_p2_decode.image.pixels.length == sizeof(concatenated_p2_expected) &&
                           !memcmp(concatenated_p2_decode.image.pixels.pointer, concatenated_p2_expected,
                                   sizeof(concatenated_p2_expected)));
    arena_set_position(arguments->arena, concatenated_p2_position);

    u8 p6_trailing_junk[] = "P6\n1 1\n255\n" "\x01\x02\x03" "X";
    ByteSlice p6_trailing_junk_bytes = {.pointer = p6_trailing_junk, .length = sizeof(p6_trailing_junk) - 1u};
    ImageProbeResult p6_trailing_junk_probe = image_probe(p6_trailing_junk_bytes, pnm_options);
    BUSTER_TEST(arguments, p6_trailing_junk_probe.status == IMAGE_DECODE_MALFORMED &&
                           p6_trailing_junk_probe.error_offset == p6_trailing_junk_bytes.length - 1u);
    BUSTER_TEST(arguments, image_test_rejected_without_allocation(arguments->arena, p6_trailing_junk_bytes, pnm_options,
                                                                   IMAGE_DECODE_MALFORMED));

    // Exactly one whitespace byte ends a binary header: after a CR, an LF byte
    // is the first raster sample, not part of a CRLF separator.
    u8 const p5_cr_lf_sample[] = {'P', '5', ' ', '1', ' ', '1', ' ', '2', '5', '5', '\r', '\n'};
    u64 p5_cr_lf_position = arguments->arena->position;
    ImageDecodeResult p5_cr_lf_decode = image_decode(arguments->arena, BUSTER_ARRAY_TO_BYTE_SLICE(p5_cr_lf_sample), pnm_options);
    u8 const p5_cr_lf_expected[] = {10, 10, 10, 255};
    BUSTER_TEST(arguments, p5_cr_lf_decode.status == IMAGE_DECODE_SUCCESS &&
                           p5_cr_lf_decode.image.pixels.length == sizeof(p5_cr_lf_expected) &&
                           !memcmp(p5_cr_lf_decode.image.pixels.pointer, p5_cr_lf_expected, sizeof(p5_cr_lf_expected)));
    arena_set_position(arguments->arena, p5_cr_lf_position);

    // Plain PBM samples need no separators; netpbm writes packed rows.
    u8 p1_packed[] = "P1\n3 2\n010\n1 1\n0\n";
    ByteSlice p1_packed_bytes = {.pointer = p1_packed, .length = sizeof(p1_packed) - 1u};
    u64 p1_packed_position = arguments->arena->position;
    ImageDecodeResult p1_packed_decode = image_decode(arguments->arena, p1_packed_bytes, pnm_options);
    u8 const p1_packed_expected[] = {255, 255, 255, 255, 0, 0, 0, 255, 255, 255, 255, 255,
                                     0, 0, 0, 255, 0, 0, 0, 255, 255, 255, 255, 255};
    BUSTER_TEST(arguments, p1_packed_decode.status == IMAGE_DECODE_SUCCESS &&
                           p1_packed_decode.image.pixels.length == sizeof(p1_packed_expected) &&
                           !memcmp(p1_packed_decode.image.pixels.pointer, p1_packed_expected, sizeof(p1_packed_expected)));
    arena_set_position(arguments->arena, p1_packed_position);
    // A comment ends at CR as well as LF; the CR is then the single raster
    // separator when the comment follows maxval, so an LF sample survives.
    u8 const pnm_cr_header_expected[] = {17, 32, 35, 255};
    u8 const pnm_cr_sample_expected[] = {10, 32, 35, 255};
    BUSTER_TEST(arguments, image_test_pnm_decodes_to(arguments->arena, IMAGE_TEST_TEXT("P6\r#c\r1 1\r255\r\x11\x20\x23"),
                                                     pnm_cr_header_expected, sizeof(pnm_cr_header_expected)));
    BUSTER_TEST(arguments, image_test_pnm_decodes_to(arguments->arena, IMAGE_TEST_TEXT("P6\n1 1\n255#c\r\x0a\x20\x23"),
                                                     pnm_cr_sample_expected, sizeof(pnm_cr_sample_expected)));
    BUSTER_TEST(arguments, image_test_pnm_decodes_to(arguments->arena, IMAGE_TEST_TEXT("P6\n1 1\n255\r\x0a\x20\x23"),
                                                     pnm_cr_sample_expected, sizeof(pnm_cr_sample_expected)));
    BUSTER_TEST(arguments, image_test_pnm_decodes_to(arguments->arena, IMAGE_TEST_TEXT("P6\n1 1#c\r255\n\x0a\x20\x23"),
                                                     pnm_cr_sample_expected, sizeof(pnm_cr_sample_expected)));
    u8 const pnm_cr_gray_expected[] = {10, 10, 10, 255};
    BUSTER_TEST(arguments, image_test_pnm_decodes_to(arguments->arena, IMAGE_TEST_TEXT("P5\n1 1\n255#c\r\x0a"),
                                                     pnm_cr_gray_expected, sizeof(pnm_cr_gray_expected)));
    u8 const pnm_cr_p4_expected[] = {0, 0, 0, 255};
    BUSTER_TEST(arguments, image_test_pnm_decodes_to(arguments->arena, IMAGE_TEST_TEXT("P4\r#c\r1 1#d\r\x80"),
                                                     pnm_cr_p4_expected, sizeof(pnm_cr_p4_expected)));
    u8 const pnm_cr_p2_expected[] = {7, 7, 7, 255};
    BUSTER_TEST(arguments, image_test_pnm_decodes_to(arguments->arena, IMAGE_TEST_TEXT("P2\r1 1\r255#c\r7"),
                                                     pnm_cr_p2_expected, sizeof(pnm_cr_p2_expected)));
    u8 const pnm_cr_raster_zero[] = {0, 0, 0, 255};
    BUSTER_TEST(arguments, image_test_pnm_decodes_to(arguments->arena, IMAGE_TEST_TEXT("P5\n1 1\n255#c\r\x00"),
                                                     pnm_cr_raster_zero, sizeof(pnm_cr_raster_zero)));
    u8 const p6_cr_truncated[] = "P6\n1 1\n255#c\r\x01\x02";
    BUSTER_TEST(arguments, image_test_rejected_without_allocation(arguments->arena, (ByteSlice){.pointer = (u8*)p6_cr_truncated, .length = sizeof(p6_cr_truncated) - 1u},
                                                                   pnm_options, IMAGE_DECODE_TRUNCATED));

    // PAM comments are whole lines; '#' inside TUPLTYPE is part of an opaque
    // tuple identifier and must not be stripped into a supported tuple.
    BUSTER_TEST(arguments, image_test_pam_tuple_unsupported(arguments->arena, "RGB#custom"));
    BUSTER_TEST(arguments, image_test_pam_tuple_unsupported(arguments->arena, "RGB #custom"));
    BUSTER_TEST(arguments, image_test_pam_tuple_unsupported(arguments->arena, "GRAYSCALE#custom"));
    BUSTER_TEST(arguments, image_test_pam_tuple_unsupported(arguments->arena, "RGB_ALPHA#custom"));
    u8 const pam_rgb_expected[] = {17, 34, 51, 255};
    BUSTER_TEST(arguments, image_test_pnm_decodes_to(arguments->arena,
                                                     IMAGE_TEST_TEXT("P7\nWIDTH 1\nHEIGHT 1\nDEPTH 3\nMAXVAL 255\nTUPLTYPE RGB\nENDHDR\n\x11\x22\x33"),
                                                     pam_rgb_expected, sizeof(pam_rgb_expected)));
    static char const pam_commented[] =
        "P7\n# ordinary header comment\nWIDTH 1\nHEIGHT 1\nDEPTH 3\nMAXVAL 255\n  # indented\nTUPLTYPE RGB\nENDHDR\n\x11\x22\x33";
    BUSTER_TEST(arguments, image_test_pnm_decodes_to(arguments->arena, pam_commented, sizeof(pam_commented) - 1u,
                                                     pam_rgb_expected, sizeof(pam_rgb_expected)));

    // Plain PBM may carry whitespace-introduced trailing material after its
    // raster; it is ignored rather than parsed as another image.
    u8 const p1_white_expected[] = {255, 255, 255, 255};
    BUSTER_TEST(arguments, image_test_pnm_decodes_to(arguments->arena, IMAGE_TEST_TEXT("P1\n1 1\n0\nignored"),
                                                     p1_white_expected, sizeof(p1_white_expected)));
    BUSTER_TEST(arguments, image_test_pnm_decodes_to(arguments->arena, IMAGE_TEST_TEXT("P1\n1 1\n0"), p1_white_expected, sizeof(p1_white_expected)));
    BUSTER_TEST(arguments, image_test_pnm_decodes_to(arguments->arena, IMAGE_TEST_TEXT("P1\n1 1\n0 \n"), p1_white_expected, sizeof(p1_white_expected)));
    BUSTER_TEST(arguments, image_test_pnm_decodes_to(arguments->arena, IMAGE_TEST_TEXT("P1\n1 1\n0 # note\nPX trailer"),
                                                     p1_white_expected, sizeof(p1_white_expected)));
    u8 p1_trailer[] = "P1\n1 1\n0\nignored";
    ByteSlice p1_trailer_bytes = {.pointer = p1_trailer, .length = sizeof(p1_trailer) - 1u};
    ImageProbeResult p1_trailer_probe = image_probe(p1_trailer_bytes, pnm_options);
    BUSTER_TEST(arguments, p1_trailer_probe.status == IMAGE_DECODE_SUCCESS && !p1_trailer_probe.information.has_more_images);
    // A frame limit counts only a real following image, never ignored text.
    ImageDecodeOptions p1_one_frame = {.format_hint = IMAGE_FORMAT_PNM, .max_frames = 1};
    ImageProbeResult p1_limited_probe = image_probe(p1_trailer_bytes, p1_one_frame);
    BUSTER_TEST(arguments, p1_limited_probe.status == IMAGE_DECODE_SUCCESS);
    // A following P1-P7 image is still reported, and non-whitespace trailers,
    // missing samples and raw-format junk stay malformed.
    u8 p1_concatenated[] = "P1\n1 1\n0\nP1\n1 1\n1\n";
    ImageProbeResult p1_concatenated_probe = image_probe((ByteSlice){.pointer = p1_concatenated, .length = sizeof(p1_concatenated) - 1u}, pnm_options);
    BUSTER_TEST(arguments, p1_concatenated_probe.status == IMAGE_DECODE_SUCCESS && p1_concatenated_probe.information.has_more_images);
    u8 p1_glued[] = "P1\n1 1\n0x";
    BUSTER_TEST(arguments, image_test_rejected_at_without_allocation(arguments->arena, (ByteSlice){.pointer = p1_glued, .length = sizeof(p1_glued) - 1u},
                                                                      pnm_options, IMAGE_DECODE_MALFORMED, 8));
    u8 p1_missing[] = "P1\n2 1\n0\nignored";
    BUSTER_TEST(arguments, image_test_rejected_without_allocation(arguments->arena, (ByteSlice){.pointer = p1_missing, .length = sizeof(p1_missing) - 1u},
                                                                   pnm_options, IMAGE_DECODE_MALFORMED));
    u8 p2_trailer[] = "P2\n1 1\n255\n7\nignored";
    BUSTER_TEST(arguments, image_test_rejected_at_without_allocation(arguments->arena, (ByteSlice){.pointer = p2_trailer, .length = sizeof(p2_trailer) - 1u},
                                                                      pnm_options, IMAGE_DECODE_MALFORMED, 13));

    u8 p1_bad_digit[] = "P1\n2 1\n02\n";
    ByteSlice p1_bad_digit_bytes = {.pointer = p1_bad_digit, .length = sizeof(p1_bad_digit) - 1u};
    ImageProbeResult p1_bad_digit_probe = image_probe(p1_bad_digit_bytes, pnm_options);
    BUSTER_TEST(arguments, p1_bad_digit_probe.status == IMAGE_DECODE_MALFORMED && p1_bad_digit_probe.error_offset == 8);

    // A caller may intentionally raise the ordinary image limits. A declared
    // PAM raster too large for any u64 ByteSlice is still a truncated bounded
    // input, not a configured-limit failure with an unnamed limit identity.
    static u8 const overflow_pam_header[] =
        "P7\nWIDTH 536903681\nHEIGHT 4294705160\nDEPTH 4\nMAXVAL 65535\nTUPLTYPE RGB_ALPHA\nENDHDR\n";
    u8 overflow_pam[sizeof(overflow_pam_header) - 1u + 64u] = {0};
    memcpy(overflow_pam, overflow_pam_header, sizeof(overflow_pam_header) - 1u);
    ImageDecodeOptions overflow_options = {
        .format_hint = IMAGE_FORMAT_PNM,
        .max_width = UINT32_C(536903681),
        .max_height = UINT32_C(4294705160),
        .max_pixels = UINT64_C(2305843009213693960),
        .max_decoded_bytes = UINT64_C(9223372036854775840),
    };
    ImageProbeResult overflow_probe = image_probe(BUSTER_ARRAY_TO_BYTE_SLICE(overflow_pam), overflow_options);
    BUSTER_TEST(arguments, overflow_probe.status == IMAGE_DECODE_TRUNCATED &&
                           overflow_probe.error_offset == sizeof(overflow_pam) &&
                           overflow_probe.exceeded_limit == IMAGE_EXCEEDED_LIMIT_NONE &&
                           !overflow_probe.observed_value && !overflow_probe.limit_value);
    BUSTER_TEST(arguments, image_test_rejected_without_allocation(arguments->arena, BUSTER_ARRAY_TO_BYTE_SLICE(overflow_pam), overflow_options,
                                                                   IMAGE_DECODE_TRUNCATED));
    return result;
}
#endif
