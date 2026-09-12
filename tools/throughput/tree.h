/* Iterative frozen-input hashing: no input-dependent recursion. The relative
 * path and content of every regular file are hashed; symlinks are rejected.
 * This covers the source and generated-header trees used by self-host trials.
 */
#ifndef BUSTER_THROUGHPUT_TREE_H
#define BUSTER_THROUGHPUT_TREE_H
#ifndef _WIN32
#include <dirent.h>
#endif

typedef struct TpTreeEntry { char* name; int directory; } TpTreeEntry;

static int tp_tree_add(Arena* arena, TpTreeEntry** entries, unsigned* count, unsigned* capacity, char const* name, int directory)
{
    int ok = *count < 32768;
    if (ok && *count == *capacity)
    {
        unsigned next = *capacity ? *capacity * 2 : 128;
        TpTreeEntry* grown = arena_allocate(arena, TpTreeEntry, next);
        ok = grown != NULL;
        if (ok)
        {
            if (*count) memcpy(grown, *entries, (size_t)*count * sizeof(TpTreeEntry));
            *entries = grown;
            *capacity = next;
        }
    }
    if (ok)
    {
        String8 copy = string_duplicate_arena(arena, string_from_pointer(name), true);
        (*entries)[*count] = (TpTreeEntry){copy.pointer, directory};
        ++*count;
    }
    return ok;
}

static int tp_hash_tree(char const* root, char digest[65])
{
    Arena* arena = arena_create((ArenaCreation){.flags = {.no_pool = 1}});
    TpTreeEntry* entries = NULL;
    unsigned count = 0, capacity = 0;
    int ok = tp_tree_add(arena, &entries, &count, &capacity, "", 1);
    for (unsigned directory = 0; directory < count && ok; ++directory)
    {
        if (!entries[directory].directory) continue;
        char path[TP_PATH_CAP];
        ok = tp_path(path, root, entries[directory].name);
#ifdef _WIN32
        char pattern[TP_PATH_CAP];
        ok = ok && tp_path(pattern, path, "*");
        WIN32_FIND_DATAA item;
        HANDLE search = ok ? FindFirstFileA(pattern, &item) : INVALID_HANDLE_VALUE;
        ok = search != INVALID_HANDLE_VALUE;
        int next = ok;
        while (ok && next)
        {
            if (strcmp(item.cFileName, ".") && strcmp(item.cFileName, ".."))
            {
                char relative[TP_PATH_CAP];
                int length = snprintf(relative, sizeof(relative), "%s%s%s", entries[directory].name,
                                      entries[directory].name[0] ? "/" : "", item.cFileName);
                ok = length >= 0 && length < TP_PATH_CAP && !(item.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT);
                if (ok) ok = tp_tree_add(arena, &entries, &count, &capacity, relative, !!(item.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY));
            }
            next = FindNextFileA(search, &item) != 0;
        }
        if (search != INVALID_HANDLE_VALUE)
        {
            if (!next && GetLastError() != ERROR_NO_MORE_FILES) ok = 0;
            FindClose(search);
        }
#else
        DIR* stream = ok ? opendir(path) : NULL;
        ok = stream != NULL;
        if (stream)
        {
            struct dirent* item;
            errno = 0;
            while (ok && (item = readdir(stream)) != NULL)
            {
                if (strcmp(item->d_name, ".") && strcmp(item->d_name, ".."))
                {
                    char relative[TP_PATH_CAP], full[TP_PATH_CAP];
                    int length = snprintf(relative, sizeof(relative), "%s%s%s", entries[directory].name,
                                          entries[directory].name[0] ? "/" : "", item->d_name);
                    struct stat status;
                    ok = length >= 0 && length < TP_PATH_CAP && tp_path(full, root, relative) && lstat(full, &status) == 0 &&
                         (S_ISDIR(status.st_mode) || S_ISREG(status.st_mode));
                    if (ok) ok = tp_tree_add(arena, &entries, &count, &capacity, relative, S_ISDIR(status.st_mode));
                }
                errno = 0;
            }
            if (errno) ok = 0;
            if (closedir(stream) != 0) ok = 0;
        }
#endif
    }
    /* Bottom-up merge sort of the compact pointer array. */
    TpTreeEntry* scratch = ok ? arena_allocate(arena, TpTreeEntry, count) : NULL;
    ok = ok && scratch != NULL;
    for (unsigned width = 1; width < count && ok; width *= 2)
    {
        for (unsigned begin = 0; begin < count; begin += width * 2)
        {
            unsigned middle = begin + width < count ? begin + width : count;
            unsigned end = middle + width < count ? middle + width : count;
            unsigned a = begin, b = middle;
            for (unsigned out = begin; out < end; ++out)
            {
                if (b == end || (a < middle && strcmp(entries[a].name, entries[b].name) <= 0)) scratch[out] = entries[a++];
                else scratch[out] = entries[b++];
            }
        }
        memcpy(entries, scratch, (size_t)count * sizeof(TpTreeEntry));
    }
    if (ok)
    {
        Sha256 hash;
        sha256_init(&hash);
        for (unsigned i = 0; i < count && ok; ++i)
        {
            if (!entries[i].directory)
            {
                char path[TP_PATH_CAP], file_hash[65];
                uint64_t bytes, lines;
                ok = tp_path(path, root, entries[i].name) && tp_hash_file(path, file_hash, &bytes, &lines);
                if (ok)
                {
                    sha256_add(&hash, entries[i].name, strlen(entries[i].name) + 1);
                    sha256_add(&hash, file_hash, 65);
                }
            }
        }
        sha256_finish_hex(&hash, digest);
    }
    arena_destroy(arena, 1);
    return ok;
}
#endif
