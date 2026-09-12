/* Workload identity policy: shared SHA-256 plus a source byte/line census.
 * Digests and line counting run outside every measured child interval.
 */
#ifndef BUSTER_THROUGHPUT_HASH_H
#define BUSTER_THROUGHPUT_HASH_H
#include <buster/lib/hash.h>

static int tp_hash_file(char const* path, char digest[65], uint64_t* bytes, uint64_t* lines)
{
    OsFileDescriptor* file = os_file_open(string_from_pointer(path), (OpenFlags){.read = 1}, (OpenPermissions){.read = 1});
    int ok = file != NULL;
    *bytes = 0;
    *lines = 0;
    if (ok)
    {
        Sha256 hash;
        unsigned char buffer[32768];
        int last = '\n';
        sha256_init(&hash);
        u64 n = 0;
        while ((ok = os_file_read_attempt(file, (ByteSlice){buffer, sizeof(buffer)}, &n)) && n)
        {
            sha256_add(&hash, buffer, n);
            *bytes += (uint64_t)n;
            for (size_t i = 0; i < n; ++i)
            {
                *lines += buffer[i] == '\n';
            }
            last = buffer[n - 1];
        }
        *lines += last != '\n';
        if (!os_file_close(file))
        {
            ok = 0;
        }
        sha256_finish_hex(&hash, digest);
    }
    return ok;
}
#endif
