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

static int tp_tree_add(TpTreeEntry** entries, unsigned* count, unsigned* capacity, char const* name, int directory)
{
    int ok = *count < 32768;
    if (ok && *count == *capacity)
    {
        unsigned next = *capacity ? *capacity * 2 : 128;
        TpTreeEntry* grown = (TpTreeEntry*)realloc(*entries, (size_t)next * sizeof(TpTreeEntry));
        ok = grown != NULL;
        if (ok) { *entries = grown; *capacity = next; }
    }
    if (ok)
    {
        size_t size = strlen(name) + 1;
        char* copy = (char*)malloc(size);
        ok = copy != NULL;
        if (ok)
        {
            memcpy(copy, name, size);
            (*entries)[*count] = (TpTreeEntry){copy, directory};
            ++*count;
        }
    }
    return ok;
}

static int tp_hash_tree(char const* root, char digest[65])
{
    TpTreeEntry* entries = NULL;
    unsigned count = 0, capacity = 0;
    int ok = tp_tree_add(&entries, &count, &capacity, "", 1);
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
                if (ok) ok = tp_tree_add(&entries, &count, &capacity, relative, !!(item.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY));
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
                    if (ok) ok = tp_tree_add(&entries, &count, &capacity, relative, S_ISDIR(status.st_mode));
                }
                errno = 0;
            }
            if (errno) ok = 0;
            if (closedir(stream) != 0) ok = 0;
        }
#endif
    }
    /* Bottom-up merge sort of the compact pointer array. */
    TpTreeEntry* scratch = ok ? (TpTreeEntry*)malloc((size_t)count * sizeof(TpTreeEntry)) : NULL;
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
        TpHash hash;
        tp_hash_init(&hash);
        for (unsigned i = 0; i < count && ok; ++i)
        {
            if (!entries[i].directory)
            {
                char path[TP_PATH_CAP], file_hash[65];
                uint64_t bytes, lines;
                ok = tp_path(path, root, entries[i].name) && tp_hash_file(path, file_hash, &bytes, &lines);
                if (ok)
                {
                    tp_hash_add(&hash, entries[i].name, strlen(entries[i].name) + 1);
                    tp_hash_add(&hash, file_hash, 65);
                }
            }
        }
        tp_hash_finish(&hash, digest);
    }
    free(scratch);
    for (unsigned i = 0; i < count; ++i) free(entries[i].name);
    free(entries);
    return ok;
}
#endif
