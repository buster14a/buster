// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Original getdents64 boundary and guarded-record regressions for #1604. Included by c_preprocessor_test.c.
// The read tests inspect MSan shadow without loading poisoned tails; kernel record padding has no content oracle.

static void pp_test_directory_poison(uint8_t* buffer, uint32_t size)
{
    memset(buffer, 0xa5, size);
#if __has_feature(memory_sanitizer)
    __msan_poison(buffer, size);
#endif
}

static bool pp_test_directory_written(const uint8_t* buffer, uint32_t size, uint32_t begin, uint32_t written)
{
    bool result = begin <= size && written <= size - begin;
    for (uint32_t i = 0; result && i < size; i += 1)
    {
        const bool initialized = i >= begin && i - begin < written;
#if __has_feature(memory_sanitizer)
        result = __msan_test_shadow(buffer + i, 1) == (initialized ? -1 : 0);
#else
        result = initialized || buffer[i] == 0xa5;
#endif
    }
    return result;
}

static bool pp_test_directory_record(FileCache* cache, const uint8_t* bytes, long length, bool accepted, uint32_t expected)
{
    const uint64_t mark = cache->path_arena.length;
    uint32_t* names = NULL;
    uint32_t slots = 0;
    uint32_t count = 0;
    const bool parsed = file_cache_list_names(cache, bytes, length, &names, &slots, &count);
    const bool result = parsed == accepted && count == expected;
    if (!result)
    {
        printf("directory record: length %ld, accepted %u/%u, names %u/%u\n", length, (unsigned)parsed, (unsigned)accepted, count, expected);
    }
    arena_pop_to(&cache->path_arena, mark);
    return result;
}

static bool pp_test_directory_records(void)
{
    const long page_size = sysconf(_SC_PAGESIZE);
    uint8_t* mapping = page_size >= 512 ? mmap(NULL, (size_t)page_size * 2, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0) : MAP_FAILED;
    FileCache cache = { .path_arena = arena_reserve(UINT64_C(1) << 20) };
    bool result = mapping != MAP_FAILED && cache.path_arena.base != NULL && mprotect(mapping + page_size, (size_t)page_size, PROT_NONE) == 0;
    uint8_t record[48] = { 0 };
    const uint16_t size = 24;
    memcpy(record + 16, &size, sizeof(size));
    record[19] = 'a';
    memcpy(record + 24, record, 24);
    record[43] = 'b';
    if (result)
    {
        uint8_t* end = mapping + page_size;
        result = pp_test_directory_record(&cache, end, 0, true, 0) && pp_test_directory_record(&cache, end, -1, false, 0);
        for (uint32_t length = 1; result && length < 24; length += 1)
        {
            memcpy(end - length, record, length);
            result = pp_test_directory_record(&cache, end - length, length, false, 0);
        }
        for (uint32_t length = 24; result && length <= 48; length += 1)
        {
            memcpy(end - length, record, length);
            result = pp_test_directory_record(&cache, end - length, length, length == 24 || length == 48, length == 48 ? 2 : 1);
        }
        static const uint16_t invalid[] = { 0, 19, 25, UINT16_MAX };
        for (uint32_t i = 0; result && i < BUSTER_ARRAY_LENGTH(invalid); i += 1)
        {
            memcpy(end - 24, record, 24);
            memcpy(end - 8, invalid + i, sizeof(invalid[i]));
            result = pp_test_directory_record(&cache, end - 24, 24, false, 0);
        }
        memcpy(end - 24, record, 24);
        memset(end - 5, 'x', 5);
        result = result && pp_test_directory_record(&cache, end - 24, 24, false, 0);
        end[-5] = 0;
        result = result && pp_test_directory_record(&cache, end - 24, 24, false, 0);
        memcpy(end - 24, record, 24);
        end[-5] = 0x80;
        result = result && pp_test_directory_record(&cache, end - 24, 24, false, 0);
        uint8_t* large = end - BUSTER_FILE_CACHE_RECORD_MAX;
        const uint16_t extent = BUSTER_FILE_CACHE_RECORD_MAX;
        memset(large, 0, extent);
        memcpy(large + 16, &extent, sizeof(extent));
        memset(large + 19, 'n', BUSTER_FILE_CACHE_NAME_MAX);
        result = result && pp_test_directory_record(&cache, large, extent, true, 1);
        large[19 + BUSTER_FILE_CACHE_NAME_MAX] = 'n';
        result = result && pp_test_directory_record(&cache, large, extent, false, 0);
    }
    if (mapping != MAP_FAILED)
    {
        munmap(mapping, (size_t)page_size * 2);
    }
    arena_release(&cache.path_arena);
    return result;
}

static bool pp_test_directory_reads(const char* directory)
{
    enum { GUARD = 16 };
    char path[4200];
    const int length = snprintf(path, sizeof(path), "%s/listing-read-boundary", directory);
    const bool made = length > 0 && (uint32_t)length < sizeof(path) && mkdir(path, 0700) == 0;
    FileCache cache = { .path_arena = arena_reserve(UINT64_C(1) << 20) };
    bool result = made && cache.path_arena.base != NULL;
    uint8_t buffer[BUSTER_FILE_CACHE_LISTING_BUFFER + 2 * GUARD] __attribute__((aligned(8)));
    static const uint32_t capacities[] = { 24, 32, BUSTER_FILE_CACHE_SMALL_LISTING, BUSTER_FILE_CACHE_LISTING_BUFFER };
    for (uint32_t c = 0; result && c < BUSTER_ARRAY_LENGTH(capacities); c += 1)
    {
        const uint32_t capacity = capacities[c];
        const int fd = open(path, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NONBLOCK | O_NOCTTY);
        uint32_t* names = NULL;
        uint32_t slots = 0;
        uint32_t count = 0;
        long got = 1;
        result = fd >= 0;
        for (uint32_t reads = 0; result && got > 0 && reads < 4; reads += 1)
        {
            pp_test_directory_poison(buffer, sizeof(buffer));
            got = file_cache_read_directory(fd, buffer + GUARD, capacity);
            result = got >= 0 && (uint64_t)got <= capacity &&
                     pp_test_directory_written(buffer, sizeof(buffer), GUARD, (uint32_t)got);
            if (result && got > 0)
            {
                result = file_cache_list_names(&cache, buffer + GUARD, got, &names, &slots, &count);
            }
        }
        result = result && got == 0 && count == 2;
        if (fd >= 0)
        {
            close(fd);
        }
        arena_pop_to(&cache.path_arena, 0);
    }
    // Use a fresh descriptor: an exhausted directory may return EOF even for a buffer too short for one record.
    const int fd = result ? open(path, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NONBLOCK | O_NOCTTY) : -1;
    if (result)
    {
        result = fd >= 0;
        pp_test_directory_poison(buffer, sizeof(buffer));
        errno = 0;
        const long got = file_cache_read_directory(fd, buffer + GUARD, 1);
        const int error = errno;
        result = result && got == -1 && error == EINVAL && pp_test_directory_written(buffer, sizeof(buffer), GUARD, 0);
    }
    if (fd >= 0)
    {
        close(fd);
    }
    if (result)
    {
        pp_test_directory_poison(buffer, sizeof(buffer));
        errno = 0;
        const long got = file_cache_read_directory(-1, buffer + GUARD, BUSTER_FILE_CACHE_SMALL_LISTING);
        const int error = errno;
        result = got == -1 && error == EBADF && pp_test_directory_written(buffer, sizeof(buffer), GUARD, 0);
    }
    arena_release(&cache.path_arena);
    if (made)
    {
        rmdir(path);
    }
    result = pp_test_directory_records() && result;
    printf("directory read boundary: %s (returned bytes, unread guards/tails, EOF/errors and guarded records)\n", result ? "as expected" : "WRONG");
    return result;
}
