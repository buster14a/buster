/* Deterministic zlib compatibility probe.
 *
 * This fixture intentionally uses only the public zlib API.  The corpus is
 * generated from a fixed byte pattern so Buster and Clang builds can compare
 * compressed bytes, decompressed bytes, and the source corpus without relying
 * on a machine-specific file or clock.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

enum { ZLIB_COMPAT_CORPUS_SIZE = 131072 };

static uint64_t zlib_compat_hash(const unsigned char *bytes, size_t count)
{
    uint64_t hash = UINT64_C(14695981039346656037);
    for (size_t index = 0; index < count; index += 1) {
        hash ^= bytes[index];
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

int main(void)
{
    unsigned char *source = (unsigned char *)malloc(ZLIB_COMPAT_CORPUS_SIZE);
    unsigned char *compressed = (unsigned char *)malloc(compressBound(ZLIB_COMPAT_CORPUS_SIZE));
    unsigned char *roundtrip = (unsigned char *)malloc(ZLIB_COMPAT_CORPUS_SIZE);
    if (!source || !compressed || !roundtrip) {
        return 2;
    }
    for (size_t index = 0; index < ZLIB_COMPAT_CORPUS_SIZE; index += 1) {
        /* A stable mix of long runs, short runs, and all byte values. */
        source[index] = (unsigned char)(((index * 17u) ^ (index >> 3) ^ (index >> 11)) & 0xffu);
        if ((index % 257u) < 193u) {
            source[index] = (unsigned char)('A' + ((index / 257u) % 23u));
        }
    }
    uLongf compressed_size = 0;
    uLongf roundtrip_size = 0;
    int result = Z_OK;
    for (unsigned iteration = 0; iteration < 8; iteration += 1) {
        compressed_size = (uLongf)compressBound(ZLIB_COMPAT_CORPUS_SIZE);
        result = compress2(compressed, &compressed_size, source, ZLIB_COMPAT_CORPUS_SIZE, Z_BEST_COMPRESSION);
        if (result != Z_OK) {
            return 3;
        }
        roundtrip_size = ZLIB_COMPAT_CORPUS_SIZE;
        result = uncompress(roundtrip, &roundtrip_size, compressed, compressed_size);
        if (result != Z_OK || roundtrip_size != ZLIB_COMPAT_CORPUS_SIZE ||
            memcmp(source, roundtrip, ZLIB_COMPAT_CORPUS_SIZE) != 0) {
            return 4;
        }
    }
    printf("ZLIB_PROBE corpus_bytes=%u compressed_bytes=%lu corpus_hash=%016llx compressed_hash=%016llx roundtrip_hash=%016llx status=pass\n",
           (unsigned)ZLIB_COMPAT_CORPUS_SIZE, (unsigned long)compressed_size,
           (unsigned long long)zlib_compat_hash(source, ZLIB_COMPAT_CORPUS_SIZE),
           (unsigned long long)zlib_compat_hash(compressed, compressed_size),
           (unsigned long long)zlib_compat_hash(roundtrip, roundtrip_size));
    free(source);
    free(compressed);
    free(roundtrip);
    return 0;
}
