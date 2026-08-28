// Deterministic LZ4 corpus shared by the Buster and Clang halves of the
// test_lz4 compatibility harness: both builds must print byte-identical
// output, so every value below is a property of the compressed bytes and of
// nothing else -- no clock, no address, no allocation order.
//
// What it deliberately covers is what a compiler gets wrong on this library:
// the 32/64-bit integer arithmetic of the match finder and the token/length
// encoders, unaligned loads and stores, explicit little-endian serialization
// independent of host byte order, and inputs past the 64 KiB window where the
// wide copy and block-dependency paths take over. Block API, HC API, and the
// frame API are all driven, because they fail independently.

#include "lz4.h"
#include "lz4frame.h"
#include "lz4hc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PROBE_LARGE_SIZE (4u << 20)
#define PROBE_FRAME_SIZE (1u << 20)
#define PROBE_FRAME_CHUNK 1501u

// FNV-1a over single bytes. A word-wise checksum could cancel an endian bug
// against itself; a byte-at-a-time digest is defined on the byte sequence and
// cannot.
static unsigned long long probe_digest(const void* data, unsigned long long size)
{
    const unsigned char* bytes = (const unsigned char*)data;
    unsigned long long hash = 14695981039346656037ULL;
    unsigned long long index;
    for (index = 0; index < size; index += 1)
    {
        hash ^= (unsigned long long)bytes[index];
        hash *= 1099511628211ULL;
    }
    return hash;
}

static unsigned probe_random(unsigned* state)
{
    unsigned value = *state;
    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    *state = value;
    return value;
}

static void probe_store_le32(unsigned char* out, unsigned value)
{
    out[0] = (unsigned char)(value & 0xffu);
    out[1] = (unsigned char)((value >> 8) & 0xffu);
    out[2] = (unsigned char)((value >> 16) & 0xffu);
    out[3] = (unsigned char)((value >> 24) & 0xffu);
}

static void probe_store_le64(unsigned char* out, unsigned long long value)
{
    unsigned index;
    for (index = 0; index < 8; index += 1)
    {
        out[index] = (unsigned char)((value >> (index * 8)) & 0xffu);
    }
}

static unsigned probe_load_le32(const unsigned char* in)
{
    return (unsigned)in[0] | ((unsigned)in[1] << 8) | ((unsigned)in[2] << 16) | ((unsigned)in[3] << 24);
}

static unsigned long long probe_load_le64(const unsigned char* in)
{
    unsigned long long value = 0;
    unsigned index;
    for (index = 0; index < 8; index += 1)
    {
        value |= (unsigned long long)in[index] << (index * 8);
    }
    return value;
}

// Round-trips 32- and 64-bit words through every byte offset in a window, and
// asserts the stored representation is little-endian rather than merely
// self-consistent, which is the half a symmetric encode/decode pair misses.
static int probe_check_unaligned(void)
{
    unsigned char scratch[64];
    unsigned offset;
    memset(scratch, 0, sizeof(scratch));
    for (offset = 0; offset < 16; offset += 1)
    {
        unsigned const word = 0x01020304u + offset;
        unsigned long long const wide = 0x0102030405060708ULL + offset;
        probe_store_le32(scratch + offset, word);
        probe_store_le64(scratch + offset + 5, wide);
        if (probe_load_le32(scratch + offset) != word)
        {
            return 0;
        }
        if (probe_load_le64(scratch + offset + 5) != wide)
        {
            return 0;
        }
        if (scratch[offset] != (unsigned char)(word & 0xffu))
        {
            return 0;
        }
        if (scratch[offset + 3] != (unsigned char)((word >> 24) & 0xffu))
        {
            return 0;
        }
        if (scratch[offset + 5] != (unsigned char)(wide & 0xffu))
        {
            return 0;
        }
        if (scratch[offset + 12] != (unsigned char)((wide >> 56) & 0xffu))
        {
            return 0;
        }
    }
    return 1;
}

// Alternating 64 KiB regions: repeated text for the match finder, unaligned
// little-endian records, and an incompressible stretch for the literal and
// tail-copy paths. The region size is the LZ4 window, so matches straddle
// every boundary.
static void probe_fill(unsigned char* out, unsigned long long size)
{
    static const char lorem[] = "the quick brown fox jumps over the lazy dog while lz4 hashes four bytes at a time and copies eight bytes at a time ";
    unsigned long long const lorem_length = (unsigned long long)(sizeof(lorem) - 1);
    unsigned long long index = 0;
    unsigned state = 0x9e3779b9u;
    while (index < size)
    {
        unsigned long long const region = (index >> 16) & 3ull;
        if (region == 3ull)
        {
            out[index] = (unsigned char)((probe_random(&state) >> 13) & 0xffu);
            index += 1;
        }
        else if (region == 2ull && index + 15 <= size)
        {
            // Every word below starts at an odd offset from the record base,
            // so no field is naturally aligned for any record alignment.
            out[index] = (unsigned char)(index & 0xffu);
            probe_store_le32(out + index + 1, (unsigned)(index * 2654435761ull));
            probe_store_le64(out + index + 5, index * 1099511628211ull);
            out[index + 13] = (unsigned char)(state & 0xffu);
            out[index + 14] = (unsigned char)((state >> 8) & 0xffu);
            index += 15;
        }
        else
        {
            out[index] = (unsigned char)lorem[index % lorem_length];
            index += 1;
        }
    }
}

typedef struct ProbeBlockCase ProbeBlockCase;
struct ProbeBlockCase
{
    const char* name;
    // Negative selects LZ4_compress_fast acceleration, zero the default
    // compressor, positive an LZ4 HC level.
    int level;
};

typedef struct ProbeFrameCase ProbeFrameCase;
struct ProbeFrameCase
{
    const char* name;
    LZ4F_blockSizeID_t block_size;
    LZ4F_blockMode_t block_mode;
    LZ4F_contentChecksum_t content_checksum;
    LZ4F_blockChecksum_t block_checksum;
    int level;
    unsigned declare_content_size;
};

static unsigned long long probe_compressed_bytes = 0;
static unsigned long long probe_decompressed_bytes = 0;

static int probe_block_case(const unsigned char* source, unsigned long long size, ProbeBlockCase entry, unsigned char* compressed, int capacity,
                            unsigned char* restored)
{
    int compressed_size;
    int restored_size;
    int partial_target = (int)(size / 3u);
    int partial_size;
    if (entry.level < 0)
    {
        compressed_size = LZ4_compress_fast((const char*)source, (char*)compressed, (int)size, capacity, -entry.level);
    }
    else if (entry.level == 0)
    {
        compressed_size = LZ4_compress_default((const char*)source, (char*)compressed, (int)size, capacity);
    }
    else
    {
        compressed_size = LZ4_compress_HC((const char*)source, (char*)compressed, (int)size, capacity, entry.level);
    }
    if (compressed_size <= 0)
    {
        printf("block name=%s source_bytes=%llu status=compress-failed\n", entry.name, (unsigned long long)size);
        return 0;
    }
    restored_size = LZ4_decompress_safe((const char*)compressed, (char*)restored, compressed_size, (int)size);
    partial_size = LZ4_decompress_safe_partial((const char*)compressed, (char*)restored + size, compressed_size, partial_target, (int)size);
    probe_compressed_bytes += size;
    probe_decompressed_bytes += (unsigned long long)(restored_size > 0 ? restored_size : 0);
    probe_decompressed_bytes += (unsigned long long)(partial_size > 0 ? partial_size : 0);
    printf("block name=%s source_bytes=%llu compressed_bytes=%d digest=%016llx restored_bytes=%d restored=%s partial_bytes=%d partial=%s\n", entry.name,
           (unsigned long long)size, compressed_size, probe_digest(compressed, (unsigned long long)compressed_size), restored_size,
           (restored_size == (int)size && memcmp(source, restored, (size_t)size) == 0) ? "ok" : "mismatch", partial_size,
           (partial_size >= partial_target && memcmp(source, restored + size, (size_t)partial_target) == 0) ? "ok" : "mismatch");
    return restored_size == (int)size && memcmp(source, restored, (size_t)size) == 0 && partial_size >= partial_target &&
           memcmp(source, restored + size, (size_t)partial_target) == 0;
}

static int probe_frame_case(const unsigned char* source, unsigned long long size, ProbeFrameCase entry)
{
    LZ4F_preferences_t preferences;
    LZ4F_dctx* context = 0;
    unsigned char* compressed;
    unsigned char* restored;
    size_t bound;
    size_t compressed_size;
    size_t consumed = 0;
    size_t produced = 0;
    int ok = 1;

    memset(&preferences, 0, sizeof(preferences));
    preferences.frameInfo.blockSizeID = entry.block_size;
    preferences.frameInfo.blockMode = entry.block_mode;
    preferences.frameInfo.contentChecksumFlag = entry.content_checksum;
    preferences.frameInfo.blockChecksumFlag = entry.block_checksum;
    preferences.frameInfo.contentSize = entry.declare_content_size ? (unsigned long long)size : 0ull;
    preferences.compressionLevel = entry.level;

    bound = LZ4F_compressFrameBound((size_t)size, &preferences);
    compressed = (unsigned char*)malloc(bound);
    restored = (unsigned char*)malloc((size_t)size);
    if (!compressed || !restored)
    {
        free(compressed);
        free(restored);
        printf("frame name=%s status=allocation-failed\n", entry.name);
        return 0;
    }

    compressed_size = LZ4F_compressFrame(compressed, bound, source, (size_t)size, &preferences);
    if (LZ4F_isError(compressed_size))
    {
        printf("frame name=%s status=compress-failed error=%s\n", entry.name, LZ4F_getErrorName(compressed_size));
        free(compressed);
        free(restored);
        return 0;
    }

    if (LZ4F_isError(LZ4F_createDecompressionContext(&context, LZ4F_VERSION)))
    {
        printf("frame name=%s status=context-failed\n", entry.name);
        free(compressed);
        free(restored);
        return 0;
    }

    // Odd-sized chunks so block boundaries fall at unaligned offsets on both
    // the input and the output side of the streaming decoder.
    while (consumed < compressed_size && produced <= (size_t)size)
    {
        size_t source_chunk = compressed_size - consumed;
        size_t destination_chunk = (size_t)size - produced;
        size_t hint;
        if (source_chunk > PROBE_FRAME_CHUNK)
        {
            source_chunk = PROBE_FRAME_CHUNK;
        }
        hint = LZ4F_decompress(context, restored + produced, &destination_chunk, compressed + consumed, &source_chunk, 0);
        if (LZ4F_isError(hint))
        {
            printf("frame name=%s status=decompress-failed error=%s\n", entry.name, LZ4F_getErrorName(hint));
            ok = 0;
            break;
        }
        consumed += source_chunk;
        produced += destination_chunk;
        if (!source_chunk && !destination_chunk)
        {
            break;
        }
    }

    LZ4F_freeDecompressionContext(context);
    ok = ok && produced == (size_t)size && memcmp(source, restored, (size_t)size) == 0;
    probe_compressed_bytes += size;
    probe_decompressed_bytes += (unsigned long long)produced;
    printf("frame name=%s source_bytes=%llu compressed_bytes=%llu digest=%016llx restored_bytes=%llu restored=%s\n", entry.name, (unsigned long long)size,
           (unsigned long long)compressed_size, probe_digest(compressed, (unsigned long long)compressed_size), (unsigned long long)produced,
           ok ? "ok" : "mismatch");
    free(compressed);
    free(restored);
    return ok;
}

int main(void)
{
    static const ProbeBlockCase block_cases[] = {
        {"default", 0}, {"fast-8", -8}, {"hc-1", 1}, {"hc-9", 9}, {"hc-12", 12},
    };
    static const unsigned long long block_sizes[] = {61ull, 4096ull, 65535ull, 65536ull, 1048576ull, (unsigned long long)PROBE_LARGE_SIZE};
    static const ProbeFrameCase frame_cases[] = {
        {"frame-default", LZ4F_default, LZ4F_blockLinked, LZ4F_noContentChecksum, LZ4F_noBlockChecksum, 0, 0},
        {"frame-64k-independent-checksums", LZ4F_max64KB, LZ4F_blockIndependent, LZ4F_contentChecksumEnabled, LZ4F_blockChecksumEnabled, 1, 1},
        {"frame-256k-linked", LZ4F_max256KB, LZ4F_blockLinked, LZ4F_contentChecksumEnabled, LZ4F_noBlockChecksum, 9, 1},
        {"frame-1m-hc-max", LZ4F_max1MB, LZ4F_blockIndependent, LZ4F_contentChecksumEnabled, LZ4F_blockChecksumEnabled, 12, 1},
        {"frame-4m-linked", LZ4F_max4MB, LZ4F_blockLinked, LZ4F_noContentChecksum, LZ4F_noBlockChecksum, 0, 1},
    };
    unsigned char* source;
    unsigned char* compressed;
    unsigned char* restored;
    int capacity = LZ4_compressBound((int)PROBE_LARGE_SIZE);
    unsigned long long size_index;
    unsigned long long case_index;
    int failures = 0;

    printf("unaligned_endian=%s\n", probe_check_unaligned() ? "ok" : "fail");
    if (!probe_check_unaligned())
    {
        return 3;
    }

    source = (unsigned char*)malloc((size_t)PROBE_LARGE_SIZE);
    compressed = (unsigned char*)malloc((size_t)capacity);
    // The partial decode writes a second copy immediately after the full one,
    // so the restore buffer holds two whole sources.
    restored = (unsigned char*)malloc((size_t)PROBE_LARGE_SIZE * 2u);
    if (!source || !compressed || !restored)
    {
        printf("status=allocation-failed\n");
        return 2;
    }
    probe_fill(source, (unsigned long long)PROBE_LARGE_SIZE);
    printf("corpus_bytes=%llu digest=%016llx\n", (unsigned long long)PROBE_LARGE_SIZE, probe_digest(source, (unsigned long long)PROBE_LARGE_SIZE));

    for (size_index = 0; size_index < sizeof(block_sizes) / sizeof(block_sizes[0]); size_index += 1)
    {
        unsigned long long const size = block_sizes[size_index];
        for (case_index = 0; case_index < sizeof(block_cases) / sizeof(block_cases[0]); case_index += 1)
        {
            // The optimal parser past a megabyte costs more than it proves
            // here; the large input is what exercises the wide copy paths.
            if (size > 1048576ull && block_cases[case_index].level > 1)
            {
                continue;
            }
            failures += !probe_block_case(source, size, block_cases[case_index], compressed, capacity, restored);
        }
    }

    for (case_index = 0; case_index < sizeof(frame_cases) / sizeof(frame_cases[0]); case_index += 1)
    {
        failures += !probe_frame_case(source, (unsigned long long)PROBE_FRAME_SIZE, frame_cases[case_index]);
    }

    printf("lz4_compress_bytes=%llu\n", probe_compressed_bytes);
    printf("lz4_decompress_bytes=%llu\n", probe_decompressed_bytes);
    printf("lz4_workload_bytes=%llu\n", probe_compressed_bytes + probe_decompressed_bytes);
    printf("lz4_failures=%d\n", failures);
    fflush(stdout);
    free(source);
    free(compressed);
    free(restored);
    return failures ? 1 : 0;
}
